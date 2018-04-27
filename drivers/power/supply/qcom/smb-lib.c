/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/iio/consumer.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/input/qpnp-power-on.h>
#include <linux/irq.h>
#include <linux/pmic-voter.h>
#include "smb-lib.h"
#include "smb-reg.h"
#include "battery.h"
#include "step-chg-jeita.h"
#include "storm-watch.h"

//ASUS BSP add include files +++
#include "fg-core.h"
#include <linux/gpio.h>
#include <linux/alarmtimer.h>
#include <linux/wakelock.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
//ASUS BSP add include files ---

#define smblib_err(chg, fmt, ...)		\
	pr_err("%s: %s: " fmt, chg->name,	\
		__func__, ##__VA_ARGS__)	\

#define smblib_dbg(chg, reason, fmt, ...)			\
	do {							\
		if (*chg->debug_mask & (reason))		\
			pr_info("%s: %s: " fmt, chg->name,	\
				__func__, ##__VA_ARGS__);	\
		else						\
			pr_debug("%s: %s: " fmt, chg->name,	\
				__func__, ##__VA_ARGS__);	\
	} while (0)
//ASUS BSP : Add debug log +++
#define CHARGER_TAG "[BAT][CHG]"
#define ERROR_TAG "[ERR]"
#define EVENT_TAG 	"[CHG][EVT]"
#define WORK_AROUND_TAG "[CHG][WA]"
#define CHG_DBG(...)  printk(KERN_INFO CHARGER_TAG __VA_ARGS__)
#define CHG_DBG_EVT(...)  printk(KERN_INFO EVENT_TAG __VA_ARGS__)
#define CHG_DBG_WA(...)  printk(KERN_INFO WORK_AROUND_TAG __VA_ARGS__)
#define CHG_DBG_E(...)  printk(KERN_ERR CHARGER_TAG ERROR_TAG __VA_ARGS__)
//ex:CHG_DBG("%s: %d\n", __func__, l_result);
//ASUS BSP : Add debug log ---

//ASUS BSP : Add delayed works +++
extern struct smb_charger *smbchg_dev;
extern struct gpio_control *global_gpio;	//global gpio_control
extern struct timespec last_jeita_time;
static struct alarm bat_alarm;
//ASUS BSP : Add delayed works ---

//ASUS BSP : Add variables +++
static int ASUS_ADAPTER_ID = 0;
static int HVDCP_FLAG = 0;
static int UFP_FLAG = 0;
static int LEGACY_CABLE_FLAG = 1;
static bool asus_flow_processing = 0;
int asus_CHG_TYPE = 0;
extern bool no_input_suspend_flag;
bool asus_adapter_detecting_flag = 0;
extern bool g_Charger_mode;
bool asus_flow_done_flag = 0;
extern bool demo_app_property_flag;
extern bool usb_alert_flag;
volatile bool vbus_without_cc_flag = 0;
extern bool smartchg_stop_flag;
bool fg_batt_id_ready = 0;
int g_CDP_WA =0;
int g_d2d_WA =0; //device to device work around
int g_PD_chg_icon =0; //for reporting PD chg icon ++
/*  WeiYu ++
	g_asus_QC3_WA: QC3 AC to be regonized as QC2 in prior to QC3.
	This issue occurs at scenario transition (MOS <-> COS)
		
	g_non200k_QC3_WA: Same issue, for non 200K AC.
*/
int g_asus_QC3_WA =0;
int g_asus_QC2_WA =0;
int g_non200k_QC3_WA =0;
int g_legacy_check_cnt =0;
#define QC3_200K_ICL	0x3E
#define QC3_NON200K_ICL	0x38
#define SOFT_START_ICL  0x14
#define SOFT_START_END_ICL	0x27
#define SOFT_START_ICL_DELTA  0x0A // 250ma
#define SOFT_START_DELAY_MS  10000
u8 g_SS_begin_icl = SOFT_START_ICL; //ICL_500mA
u8 g_SS_end_icl = SOFT_START_END_ICL;
u8 g_SS_icl_step = SOFT_START_ICL_DELTA;
int g_SS_delay_ms = SOFT_START_DELAY_MS;
void reset_soft_start_arg(void);

extern int charger_limit_value;
extern int ftm_mode;
extern int charger_limit_enable_flag;
extern int BR_countrycode;
bool g_ubatterylife_enable_flag = 0;

#define	UBATLIFE_DISCHG_THD		60
#define	UBATLIFE_CHG_THD		58
int ubatlife_chg_status = UBATLIFE_CHG_THD;

#define DELAY_MONITOR_WORK_MS	15000 //Make sure to detect non-standard AC in first round
//ASUS BSP : Add variables ---
extern void focal_usb_detection(bool plugin);		//ASUS BSP Nancy : notify touch cable in +++

extern struct fg_chip * g_fgChip;	//ASUS BSP : guage +++
extern int gauge_get_prop;

int asus_get_prop_batt_temp(struct smb_charger *chg);
int asus_get_prop_batt_volt(struct smb_charger *chg);
int asus_get_prop_batt_capacity(struct smb_charger *chg);
int asus_get_prop_batt_health(struct smb_charger *chg);
int asus_get_prop_usb_present(struct smb_charger *chg);

extern const char *batt_status_text[];

enum ADAPTER_ID {
	NONE = 0,
	ASUS_750K,
	ASUS_200K,
	PB,
	OTHERS,
	ADC_NOT_READY,
};

static char *asus_id[] = {
	"NONE",
	"ASUS_750K",
	"ASUS_200K",
	"PB",
	"OTHERS",
	"ADC_NOT_READY"
};

char *ufp_type[] = {
	"NONE",
	"DEFAULT",
	"MEDIUM",
	"HIGH",
	"OTHERS"
};

char *health_type[] = {
	"GOOD",
	"COLD",
	"COOL",
	"WARM",
	"OVERHEAT",
	"OVERVOLT",
	"OTHERS"
};

static void asus_smblib_rerun_aicl(struct smb_charger *chg)
{
	smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
			USBIN_AICL_EN_BIT, 0);
	/* Add a delay so that AICL successfully clears */
	msleep(50);
	smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
			USBIN_AICL_EN_BIT, USBIN_AICL_EN_BIT);
}

extern struct wake_lock asus_chg_lock;
extern struct wake_lock asus_charger_lock;
void asus_smblib_stay_awake(struct smb_charger *chg)
{
	CHG_DBG("%s: ASUS set smblib_stay_awake\n", __func__);
	wake_lock(&asus_chg_lock);
}

void asus_smblib_relax(struct smb_charger *chg)
{
	CHG_DBG("%s: ASUS set smblib_relax\n", __func__);
	wake_unlock(&asus_chg_lock);
}

void asus_smblib_SS_stay_awake(struct smb_charger *chg)
{
	CHG_DBG_EVT("%s: ASUS set smblib_stay_awake by soft start\n", __func__);
	wake_lock(&asus_charger_lock);
}

void asus_smblib_SS_relax(struct smb_charger *chg)
{
	CHG_DBG_EVT("%s: ASUS set smblib_relax by soft start\n", __func__);
	wake_unlock(&asus_charger_lock);
}

//ASUS_BSP +++
bool is_ubatlife_dischg(void)
{
	return (ubatlife_chg_status == UBATLIFE_DISCHG_THD) && g_ubatterylife_enable_flag ? true: false;
}
//ASUS_BSP ---

static bool is_secure(struct smb_charger *chg, int addr)
{
	if (addr == SHIP_MODE_REG || addr == FREQ_CLK_DIV_REG)
		return true;
	/* assume everything above 0xA0 is secure */
	return (bool)((addr & 0xFF) >= 0xA0);
}

int smblib_read(struct smb_charger *chg, u16 addr, u8 *val)
{
	unsigned int temp;
	int rc = 0;

	rc = regmap_read(chg->regmap, addr, &temp);
	if (rc >= 0)
		*val = (u8)temp;

	return rc;
}

int smblib_multibyte_read(struct smb_charger *chg, u16 addr, u8 *val,
				int count)
{
	return regmap_bulk_read(chg->regmap, addr, val, count);
}

int smblib_masked_write(struct smb_charger *chg, u16 addr, u8 mask, u8 val)
{
	int rc = 0;

	mutex_lock(&chg->write_lock);
	if (is_secure(chg, addr)) {
		rc = regmap_write(chg->regmap, (addr & 0xFF00) | 0xD0, 0xA5);
		if (rc < 0)
			goto unlock;
	}

	rc = regmap_update_bits(chg->regmap, addr, mask, val);

unlock:
	mutex_unlock(&chg->write_lock);
	return rc;
}

int smblib_write(struct smb_charger *chg, u16 addr, u8 val)
{
	int rc = 0;

	mutex_lock(&chg->write_lock);

	if (is_secure(chg, addr)) {
		rc = regmap_write(chg->regmap, (addr & ~(0xFF)) | 0xD0, 0xA5);
		if (rc < 0)
			goto unlock;
	}

	rc = regmap_write(chg->regmap, addr, val);

unlock:
	mutex_unlock(&chg->write_lock);
	return rc;
}

int smblib_get_step_cc_delta(struct smb_charger *chg, int *cc_delta_ua)
{
	int rc, step_state;
	u8 stat;

	if (!chg->step_chg_enabled) {
		*cc_delta_ua = 0;
		return 0;
	}

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
			rc);
		return rc;
	}

	step_state = (stat & STEP_CHARGING_STATUS_MASK) >>
				STEP_CHARGING_STATUS_SHIFT;
	rc = smblib_get_charge_param(chg, &chg->param.step_cc_delta[step_state],
				     cc_delta_ua);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get step cc delta rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int smblib_get_jeita_cc_delta(struct smb_charger *chg, int *cc_delta_ua)
{
	int rc, cc_minus_ua;
	u8 stat;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_2_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
			rc);
		return rc;
	}

	if (!(stat & BAT_TEMP_STATUS_SOFT_LIMIT_MASK)) {
		*cc_delta_ua = 0;
		return 0;
	}

	rc = smblib_get_charge_param(chg, &chg->param.jeita_cc_comp,
					&cc_minus_ua);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get jeita cc minus rc=%d\n", rc);
		return rc;
	}

	*cc_delta_ua = -cc_minus_ua;
	return 0;
}

int smblib_icl_override(struct smb_charger *chg, bool override)
{
	int rc;

	rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
				ICL_OVERRIDE_AFTER_APSD_BIT,
				override ? ICL_OVERRIDE_AFTER_APSD_BIT : 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't override ICL rc=%d\n", rc);

	return rc;
}

/********************
 * REGISTER GETTERS *
 ********************/

int smblib_get_charge_param(struct smb_charger *chg,
			    struct smb_chg_param *param, int *val_u)
{
	int rc = 0;
	u8 val_raw;

	rc = smblib_read(chg, param->reg, &val_raw);
	if (rc < 0) {
		smblib_err(chg, "%s: Couldn't read from 0x%04x rc=%d\n",
			param->name, param->reg, rc);
		return rc;
	}

	if (param->get_proc)
		*val_u = param->get_proc(param, val_raw);
	else
		*val_u = val_raw * param->step_u + param->min_u;
	smblib_dbg(chg, PR_REGISTER, "%s = %d (0x%02x)\n",
		   param->name, *val_u, val_raw);

	return rc;
}

int smblib_get_usb_suspend(struct smb_charger *chg, int *suspend)
{
	int rc = 0;
	u8 temp;

	rc = smblib_read(chg, USBIN_CMD_IL_REG, &temp);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read USBIN_CMD_IL rc=%d\n", rc);
		return rc;
	}
	*suspend = temp & USBIN_SUSPEND_BIT;

	return rc;
}

struct apsd_result {
	const char * const name;
	const u8 bit;
	const enum power_supply_type pst;
};

enum {
	UNKNOWN,
	SDP,
	CDP,
	DCP,
	OCP,
	FLOAT,
	HVDCP2,
	HVDCP3,
	MAX_TYPES
};

static const struct apsd_result const smblib_apsd_results[] = {
	[UNKNOWN] = {
		.name	= "UNKNOWN",
		.bit	= 0,
		.pst	= POWER_SUPPLY_TYPE_UNKNOWN
	},
	[SDP] = {
		.name	= "SDP",
		.bit	= SDP_CHARGER_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB
	},
	[CDP] = {
		.name	= "CDP",
		.bit	= CDP_CHARGER_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB_CDP
	},
	[DCP] = {
		.name	= "DCP",
		.bit	= DCP_CHARGER_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB_DCP
	},
	[OCP] = {
		.name	= "OCP",
		.bit	= OCP_CHARGER_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB_DCP
	},
	[FLOAT] = {
		.name	= "FLOAT",
		.bit	= FLOAT_CHARGER_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB_FLOAT
	},
	[HVDCP2] = {
		.name	= "HVDCP2",
		.bit	= DCP_CHARGER_BIT | QC_2P0_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB_HVDCP
	},
	[HVDCP3] = {
		.name	= "HVDCP3",
		.bit	= DCP_CHARGER_BIT | QC_3P0_BIT,
		.pst	= POWER_SUPPLY_TYPE_USB_HVDCP_3,
	},
};

static const struct apsd_result *smblib_get_apsd_result(struct smb_charger *chg)
{
	int rc, i;
	u8 apsd_stat, stat;
	const struct apsd_result *result = &smblib_apsd_results[UNKNOWN];

	rc = smblib_read(chg, APSD_STATUS_REG, &apsd_stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
		return result;
	}
	smblib_dbg(chg, PR_REGISTER, "APSD_STATUS = 0x%02x\n", apsd_stat);

	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT))
		return result;

	rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read APSD_RESULT_STATUS rc=%d\n",
			rc);
		return result;
	}
	stat &= APSD_RESULT_STATUS_MASK;

	for (i = 0; i < ARRAY_SIZE(smblib_apsd_results); i++) {
		if (smblib_apsd_results[i].bit == stat)
			result = &smblib_apsd_results[i];
	}

	if (apsd_stat & QC_CHARGER_BIT) {
		/* since its a qc_charger, either return HVDCP3 or HVDCP2 */
		if (result != &smblib_apsd_results[HVDCP3])
			result = &smblib_apsd_results[HVDCP2];
	}

	return result;
}

/********************
 * REGISTER SETTERS *
 ********************/

static int chg_freq_list[] = {
	9600, 9600, 6400, 4800, 3800, 3200, 2700, 2400, 2100, 1900, 1700,
	1600, 1500, 1400, 1300, 1200,
};

int smblib_set_chg_freq(struct smb_chg_param *param,
				int val_u, u8 *val_raw)
{
	u8 i;

	if (val_u > param->max_u || val_u < param->min_u)
		return -EINVAL;

	/* Charger FSW is the configured freqency / 2 */
	val_u *= 2;
	for (i = 0; i < ARRAY_SIZE(chg_freq_list); i++) {
		if (chg_freq_list[i] == val_u)
			break;
	}
	if (i == ARRAY_SIZE(chg_freq_list)) {
		pr_err("Invalid frequency %d Hz\n", val_u / 2);
		return -EINVAL;
	}

	*val_raw = i;

	return 0;
}

static int smblib_set_opt_freq_buck(struct smb_charger *chg, int fsw_khz)
{
	union power_supply_propval pval = {0, };
	int rc = 0;

	rc = smblib_set_charge_param(chg, &chg->param.freq_buck, fsw_khz);
	if (rc < 0)
		dev_err(chg->dev, "Error in setting freq_buck rc=%d\n", rc);

	if (chg->mode == PARALLEL_MASTER && chg->pl.psy) {
		pval.intval = fsw_khz;
		/*
		 * Some parallel charging implementations may not have
		 * PROP_BUCK_FREQ property - they could be running
		 * with a fixed frequency
		 */
		power_supply_set_property(chg->pl.psy,
				POWER_SUPPLY_PROP_BUCK_FREQ, &pval);
	}

	return rc;
}

int smblib_set_charge_param(struct smb_charger *chg,
			    struct smb_chg_param *param, int val_u)
{
	int rc = 0;
	u8 val_raw;

	if (param->set_proc) {
		rc = param->set_proc(param, val_u, &val_raw);
		if (rc < 0)
			return -EINVAL;
	} else {
		if (val_u > param->max_u || val_u < param->min_u) {
			smblib_err(chg, "%s: %d is out of range [%d, %d]\n",
				param->name, val_u, param->min_u, param->max_u);
			return -EINVAL;
		}

		val_raw = (val_u - param->min_u) / param->step_u;
	}

	rc = smblib_write(chg, param->reg, val_raw);
	if (rc < 0) {
		smblib_err(chg, "%s: Couldn't write 0x%02x to 0x%04x rc=%d\n",
			param->name, val_raw, param->reg, rc);
		return rc;
	}

	smblib_dbg(chg, PR_REGISTER, "%s = %d (0x%02x)\n",
		   param->name, val_u, val_raw);

	return rc;
}

int smblib_set_usb_suspend(struct smb_charger *chg, bool suspend)
{
	int rc = 0;
	int irq = chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq;

	CHG_DBG("%s: suspend = %d\n", __func__, suspend);

	if (no_input_suspend_flag) {
		CHG_DBG("%s: Thermal Test, unable to suspend input\n", __func__);
		suspend = 0;
	}

	if (usb_alert_flag) {
		CHG_DBG_EVT("%s: usb alert triggered! Suspend charger input\n", __func__);
		suspend = 1;
		//ASUSErclog(ASUS_USB_THERMAL_ALERT, "USB Thermal Alert is triggered");
	}

	if (suspend && irq) {
		if (chg->usb_icl_change_irq_enabled) {
			disable_irq_nosync(irq);
			chg->usb_icl_change_irq_enabled = false;
		}
	}

	rc = smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT,
				 suspend ? USBIN_SUSPEND_BIT : 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't write %s to USBIN_SUSPEND_BIT rc=%d\n",
			suspend ? "suspend" : "resume", rc);

	if (!suspend && irq) {
		if (!chg->usb_icl_change_irq_enabled) {
			enable_irq(irq);
			chg->usb_icl_change_irq_enabled = true;
		}
	}

	return rc;
}

int smblib_set_dc_suspend(struct smb_charger *chg, bool suspend)
{
	int rc = 0;

	rc = smblib_masked_write(chg, DCIN_CMD_IL_REG, DCIN_SUSPEND_BIT,
				 suspend ? DCIN_SUSPEND_BIT : 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't write %s to DCIN_SUSPEND_BIT rc=%d\n",
			suspend ? "suspend" : "resume", rc);

	return rc;
}

static int smblib_set_adapter_allowance(struct smb_charger *chg,
					u8 allowed_voltage)
{
	int rc = 0;

	/* PM660 only support max. 9V */
	if (chg->smb_version == PM660_SUBTYPE) {
		switch (allowed_voltage) {
		case USBIN_ADAPTER_ALLOW_12V:
		case USBIN_ADAPTER_ALLOW_9V_TO_12V:
			allowed_voltage = USBIN_ADAPTER_ALLOW_9V;
			break;
		case USBIN_ADAPTER_ALLOW_5V_OR_12V:
		case USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V:
			allowed_voltage = USBIN_ADAPTER_ALLOW_5V_OR_9V;
			break;
		case USBIN_ADAPTER_ALLOW_5V_TO_12V:
			allowed_voltage = USBIN_ADAPTER_ALLOW_5V_TO_9V;
			break;
		}
	}

	rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, allowed_voltage);
	if (rc < 0) {
		smblib_err(chg, "Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
			allowed_voltage, rc);
		return rc;
	}

	return rc;
}

#define MICRO_5V	5000000
#define MICRO_9V	9000000
#define MICRO_12V	12000000
static int smblib_set_usb_pd_allowed_voltage(struct smb_charger *chg,
					int min_allowed_uv, int max_allowed_uv)
{
	int rc;
	u8 allowed_voltage;

	if (min_allowed_uv == MICRO_5V && max_allowed_uv == MICRO_5V) {
		allowed_voltage = USBIN_ADAPTER_ALLOW_5V;
		smblib_set_opt_freq_buck(chg, chg->chg_freq.freq_5V);
	} else if (min_allowed_uv == MICRO_9V && max_allowed_uv == MICRO_9V) {
		allowed_voltage = USBIN_ADAPTER_ALLOW_9V;
		smblib_set_opt_freq_buck(chg, chg->chg_freq.freq_9V);
	} else if (min_allowed_uv == MICRO_12V && max_allowed_uv == MICRO_12V) {
		allowed_voltage = USBIN_ADAPTER_ALLOW_12V;
		smblib_set_opt_freq_buck(chg, chg->chg_freq.freq_12V);
	} else if (min_allowed_uv < MICRO_9V && max_allowed_uv <= MICRO_9V) {
		allowed_voltage = USBIN_ADAPTER_ALLOW_5V_TO_9V;
	} else if (min_allowed_uv < MICRO_9V && max_allowed_uv <= MICRO_12V) {
		allowed_voltage = USBIN_ADAPTER_ALLOW_5V_TO_12V;
	} else if (min_allowed_uv < MICRO_12V && max_allowed_uv <= MICRO_12V) {
		allowed_voltage = USBIN_ADAPTER_ALLOW_9V_TO_12V;
	} else {
		smblib_err(chg, "invalid allowed voltage [%d, %d]\n",
			min_allowed_uv, max_allowed_uv);
		return -EINVAL;
	}

	rc = smblib_set_adapter_allowance(chg, allowed_voltage);
	if (rc < 0) {
		smblib_err(chg, "Couldn't configure adapter allowance rc=%d\n",
				rc);
		return rc;
	}

	return rc;
}

/********************
 * HELPER FUNCTIONS *
 ********************/
static int smblib_request_dpdm(struct smb_charger *chg, bool enable)
{
	int rc = 0;

	/* fetch the DPDM regulator */
	if (!chg->dpdm_reg && of_get_property(chg->dev->of_node,
				"dpdm-supply", NULL)) {
		chg->dpdm_reg = devm_regulator_get(chg->dev, "dpdm");
		if (IS_ERR(chg->dpdm_reg)) {
			rc = PTR_ERR(chg->dpdm_reg);
			smblib_err(chg, "Couldn't get dpdm regulator rc=%d\n",
					rc);
			chg->dpdm_reg = NULL;
			return rc;
		}
	}

	if (enable) {
		if (chg->dpdm_reg && !regulator_is_enabled(chg->dpdm_reg)) {
			smblib_dbg(chg, PR_MISC, "enabling DPDM regulator\n");
			rc = regulator_enable(chg->dpdm_reg);
			if (rc < 0)
				smblib_err(chg,
					"Couldn't enable dpdm regulator rc=%d\n",
					rc);
		}
	} else {
		if (chg->dpdm_reg && regulator_is_enabled(chg->dpdm_reg)) {
			smblib_dbg(chg, PR_MISC, "disabling DPDM regulator\n");
			rc = regulator_disable(chg->dpdm_reg);
			if (rc < 0)
				smblib_err(chg,
					"Couldn't disable dpdm regulator rc=%d\n",
					rc);
		}
	}

	return rc;
}

static void smblib_rerun_apsd(struct smb_charger *chg)
{
	int rc;

	smblib_dbg(chg, PR_MISC, "re-running APSD\n");
	if (chg->wa_flags & QC_AUTH_INTERRUPT_WA_BIT) {
		rc = smblib_masked_write(chg,
				USBIN_SOURCE_CHANGE_INTRPT_ENB_REG,
				AUTH_IRQ_EN_CFG_BIT, AUTH_IRQ_EN_CFG_BIT);
		if (rc < 0)
			smblib_err(chg, "Couldn't enable HVDCP auth IRQ rc=%d\n",
									rc);
	}

	CHG_DBG("%s: Qcom Rerun APSD\n", __func__);
	rc = smblib_masked_write(chg, CMD_APSD_REG,
				APSD_RERUN_BIT, APSD_RERUN_BIT);
	if (rc < 0)
		smblib_err(chg, "Couldn't re-run APSD rc=%d\n", rc);
}

static const struct apsd_result *smblib_update_usb_type(struct smb_charger *chg)
{
	const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);
	int type =0;
	static bool once = false; //WeiYu ++ for CDP WA
	
	/* if PD is active, APSD is disabled so won't have a valid result */
	if (chg->pd_active) {
		type =1;
		chg->real_charger_type = POWER_SUPPLY_TYPE_USB_PD;
	}
	else if (asus_adapter_detecting_flag && apsd_result->pst == POWER_SUPPLY_TYPE_UNKNOWN){
		type =2;
		chg->real_charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		chg->usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
	}
	else {
		/*
		 * Update real charger type only if its not FLOAT
		 * detected as as SDP
		 */
		if (!(apsd_result->pst == POWER_SUPPLY_TYPE_USB_FLOAT &&
			chg->real_charger_type == POWER_SUPPLY_TYPE_USB)){
			type =3;
			chg->real_charger_type = apsd_result->pst;
			//WeiYu: healthd seems can not recognize usb_psy_desc.type as HVDCP3,
			//which leads to no charging icon at UI 
			//chg->usb_psy_desc.type = apsd_result->pst;	

			/*
			   WeiYu: 
			   Let CDP WA works at once it detect usb dcp
			   [concern] Check WA wont affect other type
			   		   To confirm CDP wont become SDP at other scnario.\
			   [history] WA for CDP becomes SDP during booting situation
			   [history] another pattern high~dcp~sdp
			   [pattern] Before WA: cdp~vbus auto low&high~unknown~sdp
			*/
			if(g_CDP_WA && chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN){
				++g_CDP_WA;
				CHG_DBG_WA("detected usb tye CDP->unknown, CDP_WA %d\n",g_CDP_WA);
				if(g_CDP_WA >= 6){
					g_CDP_WA =0;
					CHG_DBG_WA("Error! Reset CDP_WA for safty\n");
				}
			}
			else if(!once && chg->real_charger_type ==POWER_SUPPLY_TYPE_USB_CDP){
				g_CDP_WA = 3;
				once = true;
				CHG_DBG_WA("CDP WA is disabled, and do WA one time next round\n");								
			}  
			else if(!once){
				once = true;
				CHG_DBG_WA("CDP WA is disabled\n");				
			}
			else;
		}else{
			type =4; 
			chg->usb_psy_desc.type = apsd_result->pst;
		}

	}


	
	smblib_dbg(chg, PR_MISC, "APSD=%s PD=%d\n",
					apsd_result->name, chg->pd_active);
	CHG_DBG("%s: NO_CC = %d, FAKE = %d, APSD=%d APSD_real=%d PD=%d type %d\n",
			__func__, vbus_without_cc_flag, asus_adapter_detecting_flag, chg->usb_psy_desc.type, chg->real_charger_type,chg->pd_active,type);
	return apsd_result;
}

static int smblib_notifier_call(struct notifier_block *nb,
		unsigned long ev, void *v)
{
	struct power_supply *psy = v;
	struct smb_charger *chg = container_of(nb, struct smb_charger, nb);

	if (!strcmp(psy->desc->name, "bms")) {
		if (!chg->bms_psy)
			chg->bms_psy = psy;
		if (ev == PSY_EVENT_PROP_CHANGED)
			schedule_work(&chg->bms_update_work);
	}

	if (!chg->pl.psy && !strcmp(psy->desc->name, "parallel"))
		chg->pl.psy = psy;

	return NOTIFY_OK;
}

static int smblib_register_notifier(struct smb_charger *chg)
{
	int rc;

	chg->nb.notifier_call = smblib_notifier_call;
	rc = power_supply_reg_notifier(&chg->nb);
	if (rc < 0) {
		smblib_err(chg, "Couldn't register psy notifier rc = %d\n", rc);
		return rc;
	}

	return 0;
}

int smblib_mapping_soc_from_field_value(struct smb_chg_param *param,
					     int val_u, u8 *val_raw)
{
	if (val_u > param->max_u || val_u < param->min_u)
		return -EINVAL;

	*val_raw = val_u << 1;

	return 0;
}

int smblib_mapping_cc_delta_to_field_value(struct smb_chg_param *param,
					   u8 val_raw)
{
	int val_u  = val_raw * param->step_u + param->min_u;

	if (val_u > param->max_u)
		val_u -= param->max_u * 2;

	return val_u;
}

int smblib_mapping_cc_delta_from_field_value(struct smb_chg_param *param,
					     int val_u, u8 *val_raw)
{
	if (val_u > param->max_u || val_u < param->min_u - param->max_u)
		return -EINVAL;

	val_u += param->max_u * 2 - param->min_u;
	val_u %= param->max_u * 2;
	*val_raw = val_u / param->step_u;

	return 0;
}

static void smblib_uusb_removal(struct smb_charger *chg)
{
	int rc;
	struct smb_irq_data *data;
	struct storm_watch *wdata;

	cancel_delayed_work_sync(&chg->pl_enable_work);

	rc = smblib_request_dpdm(chg, false);
	if (rc < 0)
		smblib_err(chg, "Couldn't to disable DPDM rc=%d\n", rc);

	if (chg->wa_flags & BOOST_BACK_WA) {
		data = chg->irq_info[SWITCH_POWER_OK_IRQ].irq_data;
		if (data) {
			wdata = &data->storm_data;
			update_storm_count(wdata, WEAK_CHG_STORM_COUNT);
			vote(chg->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
			vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
					false, 0);
		}
	}
	vote(chg->pl_disable_votable, PL_DELAY_VOTER, true, 0);
	vote(chg->awake_votable, PL_DELAY_VOTER, false, 0);

	/* reset both usbin current and voltage votes */
	vote(chg->pl_enable_votable_indirect, USBIN_I_VOTER, false, 0);
	vote(chg->pl_enable_votable_indirect, USBIN_V_VOTER, false, 0);
	vote(chg->usb_icl_votable, SW_QC3_VOTER, false, 0);

	cancel_delayed_work_sync(&chg->hvdcp_detect_work);

	if (chg->wa_flags & QC_AUTH_INTERRUPT_WA_BIT) {
		/* re-enable AUTH_IRQ_EN_CFG_BIT */
		rc = smblib_masked_write(chg,
				USBIN_SOURCE_CHANGE_INTRPT_ENB_REG,
				AUTH_IRQ_EN_CFG_BIT, AUTH_IRQ_EN_CFG_BIT);
		if (rc < 0)
			smblib_err(chg,
				"Couldn't enable QC auth setting rc=%d\n", rc);
	}

	/* reconfigure allowed voltage for HVDCP */
	rc = smblib_set_adapter_allowance(chg,
			USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V);
	if (rc < 0)
		smblib_err(chg, "Couldn't set USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V rc=%d\n",
			rc);

	chg->voltage_min_uv = MICRO_5V;
	chg->voltage_max_uv = MICRO_5V;
	chg->usb_icl_delta_ua = 0;
	chg->pulse_cnt = 0;
	chg->uusb_apsd_rerun_done = false;

	/* clear USB ICL vote for USB_PSY_VOTER */
	rc = vote(chg->usb_icl_votable, USB_PSY_VOTER, false, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't un-vote for USB ICL rc=%d\n", rc);

	/* clear USB ICL vote for DCP_VOTER */
	rc = vote(chg->usb_icl_votable, DCP_VOTER, false, 0);
	if (rc < 0)
		smblib_err(chg,
			"Couldn't un-vote DCP from USB ICL rc=%d\n", rc);
}

void smblib_suspend_on_debug_battery(struct smb_charger *chg)
{
	int rc;
	union power_supply_propval val;

	if (!chg->suspend_input_on_debug_batt)
		return;

	rc = power_supply_get_property(chg->bms_psy,
			POWER_SUPPLY_PROP_DEBUG_BATTERY, &val);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get debug battery prop rc=%d\n", rc);
		return;
	}

	vote(chg->usb_icl_votable, DEBUG_BOARD_VOTER, val.intval, 0);
	vote(chg->dc_suspend_votable, DEBUG_BOARD_VOTER, val.intval, 0);
	if (val.intval)
		pr_info("Input suspended: Fake battery\n");
}

int smblib_rerun_apsd_if_required(struct smb_charger *chg)
{
	union power_supply_propval val;
	int rc;

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get usb present rc = %d\n", rc);
		return rc;
	}

	if (!val.intval)
		return 0;

	rc = smblib_request_dpdm(chg, true);
	if (rc < 0)
		smblib_err(chg, "Couldn't to enable DPDM rc=%d\n", rc);

	chg->uusb_apsd_rerun_done = true;
	smblib_rerun_apsd(chg);

	return 0;
}

static int smblib_get_hw_pulse_cnt(struct smb_charger *chg, int *count)
{
	int rc;
	u8 val[2];

	switch (chg->smb_version) {
	case PMI8998_SUBTYPE:
		rc = smblib_read(chg, QC_PULSE_COUNT_STATUS_REG, val);
		if (rc) {
			pr_err("failed to read QC_PULSE_COUNT_STATUS_REG rc=%d\n",
					rc);
			return rc;
		}
		*count = val[0] & QC_PULSE_COUNT_MASK;
		break;
	case PM660_SUBTYPE:
		rc = smblib_multibyte_read(chg,
				QC_PULSE_COUNT_STATUS_1_REG, val, 2);
		if (rc) {
			pr_err("failed to read QC_PULSE_COUNT_STATUS_1_REG rc=%d\n",
					rc);
			return rc;
		}
		*count = (val[1] << 8) | val[0];
		break;
	default:
		smblib_dbg(chg, PR_PARALLEL, "unknown SMB chip %d\n",
				chg->smb_version);
		return -EINVAL;
	}

	return 0;
}

static int smblib_get_pulse_cnt(struct smb_charger *chg, int *count)
{
	int rc;

	/* Use software based pulse count if HW INOV is disabled */
	if (get_effective_result(chg->hvdcp_hw_inov_dis_votable) > 0) {
		*count = chg->pulse_cnt;
		return 0;
	}

	/* Use h/w pulse count if autonomous mode is enabled */
	rc = smblib_get_hw_pulse_cnt(chg, count);
	if (rc < 0)
		smblib_err(chg, "failed to read h/w pulse count rc=%d\n", rc);

	return rc;
}

#define USBIN_25MA	25000
#define USBIN_100MA	100000
#define USBIN_150MA	150000
#define USBIN_500MA	500000
#define USBIN_900MA	900000

static int set_sdp_current(struct smb_charger *chg, int icl_ua)
{
	int rc;
	u8 icl_options;
	const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);
	static bool done = false; 
	/* WeiYu ++ unknown AC low current
		do WA once is not enough, so remove arg. "done"
		i.e. force setting 500ma for unknown AC
	*/
	if(!done && chg->real_charger_type==POWER_SUPPLY_TYPE_UNKNOWN){
		//done = true;
		icl_ua = USBIN_500MA;
		CHG_DBG_WA("Set ICL 500ma for unknown AC\n");
	}
		
	/* power source is SDP */
	switch (icl_ua) {
	case USBIN_100MA:
		/* USB 2.0 100mA */
		icl_options = 0;
		break;
	case USBIN_150MA:
		/* USB 3.0 150mA */
		icl_options = CFG_USB3P0_SEL_BIT;
		break;
	case USBIN_500MA:
		/* USB 2.0 500mA */
		icl_options = USB51_MODE_BIT;
		break;
	case USBIN_900MA:
		/* USB 3.0 900mA */
		icl_options = CFG_USB3P0_SEL_BIT | USB51_MODE_BIT;
		break;
	default:
		smblib_err(chg, "ICL %duA isn't supported for SDP\n", icl_ua);
		return -EINVAL;
	}

	if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB &&
		apsd_result->pst == POWER_SUPPLY_TYPE_USB_FLOAT) {
		/*
		 * change the float charger configuration to SDP, if this
		 * is the case of SDP being detected as FLOAT
		 */
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
			FORCE_FLOAT_SDP_CFG_BIT, FORCE_FLOAT_SDP_CFG_BIT);
		if (rc < 0) {
			smblib_err(chg, "Couldn't set float ICL options rc=%d\n",
						rc);
			return rc;
		}
	}

	rc = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
		CFG_USB3P0_SEL_BIT | USB51_MODE_BIT, icl_options);
	if (rc < 0) {
		smblib_err(chg, "Couldn't set ICL options rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int get_sdp_current(struct smb_charger *chg, int *icl_ua)
{
	int rc;
	u8 icl_options;
	bool usb3 = false;

	rc = smblib_read(chg, USBIN_ICL_OPTIONS_REG, &icl_options);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get ICL options rc=%d\n", rc);
		return rc;
	}

	usb3 = (icl_options & CFG_USB3P0_SEL_BIT);

	if (icl_options & USB51_MODE_BIT)
		*icl_ua = usb3 ? USBIN_900MA : USBIN_500MA;
	else
		*icl_ua = usb3 ? USBIN_150MA : USBIN_100MA;

	return rc;
}

int smblib_set_icl_current(struct smb_charger *chg, int icl_ua)
{
	int rc = 0;
	bool override;

	/* suspend and return if 25mA or less is requested */
	if (icl_ua < USBIN_25MA)
		return smblib_set_usb_suspend(chg, true);

	if (icl_ua == INT_MAX)
		goto override_suspend_config;

	/* configure current */
	if (chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT
		&& (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
		rc = set_sdp_current(chg, icl_ua);
		if (rc < 0) {
			smblib_err(chg, "Couldn't set SDP ICL rc=%d\n", rc);
			goto enable_icl_changed_interrupt;
		}
	} else {
		set_sdp_current(chg, 100000);
		rc = smblib_set_charge_param(chg, &chg->param.usb_icl, icl_ua);
		if (rc < 0) {
			smblib_err(chg, "Couldn't set HC ICL rc=%d\n", rc);
			goto enable_icl_changed_interrupt;
		}
	}

override_suspend_config:
	/* determine if override needs to be enforced */
	override = true;
	if (icl_ua == INT_MAX) {
		/* remove override if no voters - hw defaults is desired */
		CHG_DBG_E("%s: Set ICL for customization, override: %d\n", __func__,override);

		/* WeiYu ++ unknown AC low current
			fix unknown AC low current input via 0x1366 and 0x1370;
			Seems it works on modify both 0x1366 and 0x1370;
			The following is 0x1366 part;
			[concern] It may affect AC which to be unknown at initial 
			[pattern] unknown AC low current occurs at scenario trasnsition
		*/
		if(chg->real_charger_type==POWER_SUPPLY_TYPE_UNKNOWN){
			override = false;
			CHG_DBG_EVT("%s: modify for unknown AC, override: %d\n", __func__,override);
		}
		//override = false;

	} else if (chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) {
		if (chg->usb_psy_desc.type == POWER_SUPPLY_TYPE_USB)
			/* For std cable with type = SDP never override */
			override = false;
		else if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP
			&& icl_ua == 1500000)
			/*
			 * For std cable with type = CDP override only if
			 * current is not 1500mA
			 */
			override = false;
	}

	/* enforce override */
	rc = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
		USBIN_MODE_CHG_BIT, override ? USBIN_MODE_CHG_BIT : 0);

	rc = smblib_icl_override(chg, override);
	if (rc < 0) {
		smblib_err(chg, "Couldn't set ICL override rc=%d\n", rc);
		goto enable_icl_changed_interrupt;
	}

	/* unsuspend after configuring current and override */
	rc = smblib_set_usb_suspend(chg, false);
	if (rc < 0) {
		smblib_err(chg, "Couldn't resume input rc=%d\n", rc);
		goto enable_icl_changed_interrupt;
	}

enable_icl_changed_interrupt:
	return rc;
}

int smblib_get_icl_current(struct smb_charger *chg, int *icl_ua)
{
	int rc = 0;
	u8 load_cfg;
	bool override;

	if ((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT
		|| chg->micro_usb_mode)
		&& (chg->usb_psy_desc.type == POWER_SUPPLY_TYPE_USB)) {
		rc = get_sdp_current(chg, icl_ua);
		if (rc < 0) {
			smblib_err(chg, "Couldn't get SDP ICL rc=%d\n", rc);
			return rc;
		}
	} else {
		rc = smblib_read(chg, USBIN_LOAD_CFG_REG, &load_cfg);
		if (rc < 0) {
			smblib_err(chg, "Couldn't get load cfg rc=%d\n", rc);
			return rc;
		}
		override = load_cfg & ICL_OVERRIDE_AFTER_APSD_BIT;
		if (!override)
			return INT_MAX;

		/* override is set */
		rc = smblib_get_charge_param(chg, &chg->param.usb_icl, icl_ua);
		if (rc < 0) {
			smblib_err(chg, "Couldn't get HC ICL rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

/*********************
 * VOTABLE CALLBACKS *
 *********************/

static int smblib_dc_suspend_vote_callback(struct votable *votable, void *data,
			int suspend, const char *client)
{
	struct smb_charger *chg = data;

	/* resume input if suspend is invalid */
	if (suspend < 0)
		suspend = 0;

	return smblib_set_dc_suspend(chg, (bool)suspend);
}

static int smblib_dc_icl_vote_callback(struct votable *votable, void *data,
			int icl_ua, const char *client)
{
	struct smb_charger *chg = data;
	int rc = 0;
	bool suspend;

	if (icl_ua < 0) {
		smblib_dbg(chg, PR_MISC, "No Voter hence suspending\n");
		icl_ua = 0;
	}

	suspend = (icl_ua < USBIN_25MA);
	if (suspend)
		goto suspend;

	rc = smblib_set_charge_param(chg, &chg->param.dc_icl, icl_ua);
	if (rc < 0) {
		smblib_err(chg, "Couldn't set DC input current limit rc=%d\n",
			rc);
		return rc;
	}

suspend:
	rc = vote(chg->dc_suspend_votable, USER_VOTER, suspend, 0);
	if (rc < 0) {
		smblib_err(chg, "Couldn't vote to %s DC rc=%d\n",
			suspend ? "suspend" : "resume", rc);
		return rc;
	}
	return rc;
}

static int smblib_pd_disallowed_votable_indirect_callback(
	struct votable *votable, void *data, int disallowed, const char *client)
{
	struct smb_charger *chg = data;
	int rc;

	rc = vote(chg->pd_allowed_votable, PD_DISALLOWED_INDIRECT_VOTER,
		!disallowed, 0);

	return rc;
}

static int smblib_awake_vote_callback(struct votable *votable, void *data,
			int awake, const char *client)
{
	struct smb_charger *chg = data;

	if (awake)
		pm_stay_awake(chg->dev);
	else
		pm_relax(chg->dev);

	return 0;
}

static int smblib_chg_disable_vote_callback(struct votable *votable, void *data,
			int chg_disable, const char *client)
{
	struct smb_charger *chg = data;
	int rc;

	rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
				 CHARGING_ENABLE_CMD_BIT,
				 chg_disable ? 0 : CHARGING_ENABLE_CMD_BIT);
	if (rc < 0) {
		smblib_err(chg, "Couldn't %s charging rc=%d\n",
			chg_disable ? "disable" : "enable", rc);
		return rc;
	}

	return 0;
}

static int smblib_hvdcp_enable_vote_callback(struct votable *votable,
			void *data,
			int hvdcp_enable, const char *client)
{
	struct smb_charger *chg = data;
	int rc;
	u8 val = HVDCP_AUTH_ALG_EN_CFG_BIT | HVDCP_EN_BIT;
	u8 stat;

	/* vote to enable/disable HW autonomous INOV */
	vote(chg->hvdcp_hw_inov_dis_votable, client, !hvdcp_enable, 0);

	CHG_DBG("%s: start, hvdcp_enable = %d\n", __func__, hvdcp_enable);

	/*
	 * Disable the autonomous bit and auth bit for disabling hvdcp.
	 * This ensures only qc 2.0 detection runs but no vbus
	 * negotiation happens.
	 */
	if (!hvdcp_enable)
		val = HVDCP_EN_BIT;

	rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				 HVDCP_EN_BIT | HVDCP_AUTH_ALG_EN_CFG_BIT,
				 val);
	if (rc < 0) {
		smblib_err(chg, "Couldn't %s hvdcp rc=%d\n",
			hvdcp_enable ? "enable" : "disable", rc);
		return rc;
	}

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read APSD status rc=%d\n", rc);
		return rc;
	}

	/* re-run APSD if HVDCP was detected */
	if (stat & QC_CHARGER_BIT)
		smblib_rerun_apsd(chg);

	return 0;
}

static int smblib_hvdcp_disable_indirect_vote_callback(struct votable *votable,
			void *data, int hvdcp_disable, const char *client)
{
	struct smb_charger *chg = data;

	vote(chg->hvdcp_enable_votable, HVDCP_INDIRECT_VOTER,
			!hvdcp_disable, 0);

	return 0;
}

static int smblib_apsd_disable_vote_callback(struct votable *votable,
			void *data,
			int apsd_disable, const char *client)
{
	struct smb_charger *chg = data;
	int rc;

	if (apsd_disable) {
		rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
							AUTO_SRC_DETECT_BIT,
							0);
		if (rc < 0) {
			smblib_err(chg, "Couldn't disable APSD rc=%d\n", rc);
			return rc;
		}
	} else {
		rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
							AUTO_SRC_DETECT_BIT,
							AUTO_SRC_DETECT_BIT);
		if (rc < 0) {
			smblib_err(chg, "Couldn't enable APSD rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int smblib_hvdcp_hw_inov_dis_vote_callback(struct votable *votable,
				void *data, int disable, const char *client)
{
	struct smb_charger *chg = data;
	int rc;

	if (disable) {
		/*
		 * the pulse count register get zeroed when autonomous mode is
		 * disabled. Track that in variables before disabling
		 */
		rc = smblib_get_hw_pulse_cnt(chg, &chg->pulse_cnt);
		if (rc < 0) {
			pr_err("failed to read QC_PULSE_COUNT_STATUS_REG rc=%d\n",
					rc);
			return rc;
		}
	}

	rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
			HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT,
			disable ? 0 : HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT);
	if (rc < 0) {
		smblib_err(chg, "Couldn't %s hvdcp rc=%d\n",
				disable ? "disable" : "enable", rc);
		return rc;
	}

	return rc;
}

static int smblib_usb_irq_enable_vote_callback(struct votable *votable,
				void *data, int enable, const char *client)
{
	struct smb_charger *chg = data;

	if (!chg->irq_info[INPUT_CURRENT_LIMIT_IRQ].irq ||
				!chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq)
		return 0;

	if (enable) {
		enable_irq(chg->irq_info[INPUT_CURRENT_LIMIT_IRQ].irq);
		enable_irq(chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq);
	} else {
		disable_irq(chg->irq_info[INPUT_CURRENT_LIMIT_IRQ].irq);
		disable_irq(chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq);
	}

	return 0;
}

static int smblib_typec_irq_disable_vote_callback(struct votable *votable,
				void *data, int disable, const char *client)
{
	struct smb_charger *chg = data;

	if (!chg->irq_info[TYPE_C_CHANGE_IRQ].irq)
		return 0;

	if (disable)
		disable_irq_nosync(chg->irq_info[TYPE_C_CHANGE_IRQ].irq);
	else
		enable_irq(chg->irq_info[TYPE_C_CHANGE_IRQ].irq);

	return 0;
}

/*******************
 * VCONN REGULATOR *
 * *****************/

#define MAX_OTG_SS_TRIES 2
static int _smblib_vconn_regulator_enable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc = 0;
	u8 val;

	/*
	 * When enabling VCONN using the command register the CC pin must be
	 * selected. VCONN should be supplied to the inactive CC pin hence using
	 * the opposite of the CC_ORIENTATION_BIT.
	 */
	smblib_dbg(chg, PR_OTG, "enabling VCONN\n");
	val = chg->typec_status[3] &
			CC_ORIENTATION_BIT ? 0 : VCONN_EN_ORIENTATION_BIT;
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 VCONN_EN_VALUE_BIT | VCONN_EN_ORIENTATION_BIT,
				 VCONN_EN_VALUE_BIT | val);
	if (rc < 0) {
		smblib_err(chg, "Couldn't enable vconn setting rc=%d\n", rc);
		return rc;
	}

	return rc;
}

int smblib_vconn_regulator_enable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chg->vconn_oc_lock);
	if (chg->vconn_en)
		goto unlock;

	rc = _smblib_vconn_regulator_enable(rdev);
	if (rc >= 0)
		chg->vconn_en = true;

unlock:
	mutex_unlock(&chg->vconn_oc_lock);
	return rc;
}

static int _smblib_vconn_regulator_disable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc = 0;

	smblib_dbg(chg, PR_OTG, "disabling VCONN\n");
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 VCONN_EN_VALUE_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't disable vconn regulator rc=%d\n", rc);

	return rc;
}

int smblib_vconn_regulator_disable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chg->vconn_oc_lock);
	if (!chg->vconn_en)
		goto unlock;

	rc = _smblib_vconn_regulator_disable(rdev);
	if (rc >= 0)
		chg->vconn_en = false;

unlock:
	mutex_unlock(&chg->vconn_oc_lock);
	return rc;
}

int smblib_vconn_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&chg->vconn_oc_lock);
	ret = chg->vconn_en;
	mutex_unlock(&chg->vconn_oc_lock);
	return ret;
}

/*****************
 * OTG REGULATOR *
 *****************/
#define MAX_RETRY		15
#define MIN_DELAY_US		2000
#define MAX_DELAY_US		9000
static int otg_current[] = {250000, 500000, 1000000, 1500000};
static int smblib_enable_otg_wa(struct smb_charger *chg)
{
	u8 stat;
	int rc, i, retry_count = 0, min_delay = MIN_DELAY_US;

	for (i = 0; i < ARRAY_SIZE(otg_current); i++) {
		smblib_dbg(chg, PR_OTG, "enabling OTG with %duA\n",
						otg_current[i]);
		rc = smblib_set_charge_param(chg, &chg->param.otg_cl,
						otg_current[i]);
		if (rc < 0) {
			smblib_err(chg, "Couldn't set otg limit rc=%d\n", rc);
			return rc;
		}

		rc = smblib_write(chg, CMD_OTG_REG, OTG_EN_BIT);
		if (rc < 0) {
			smblib_err(chg, "Couldn't enable OTG rc=%d\n", rc);
			return rc;
		}

		retry_count = 0;
		min_delay = MIN_DELAY_US;
		do {
			usleep_range(min_delay, min_delay + 100);
			rc = smblib_read(chg, OTG_STATUS_REG, &stat);
			if (rc < 0) {
				smblib_err(chg, "Couldn't read OTG status rc=%d\n",
							rc);
				goto out;
			}

			if (stat & BOOST_SOFTSTART_DONE_BIT) {
				rc = smblib_set_charge_param(chg,
					&chg->param.otg_cl, chg->otg_cl_ua);
				if (rc < 0) {
					smblib_err(chg, "Couldn't set otg limit rc=%d\n",
							rc);
					goto out;
				}
				break;
			}
			/* increase the delay for following iterations */
			if (retry_count > 5)
				min_delay = MAX_DELAY_US;

		} while (retry_count++ < MAX_RETRY);

		if (retry_count >= MAX_RETRY) {
			smblib_dbg(chg, PR_OTG, "OTG enable failed with %duA\n",
								otg_current[i]);
			rc = smblib_write(chg, CMD_OTG_REG, 0);
			if (rc < 0) {
				smblib_err(chg, "disable OTG rc=%d\n", rc);
				goto out;
			}
		} else {
			smblib_dbg(chg, PR_OTG, "OTG enabled\n");
			return 0;
		}
	}

	if (i == ARRAY_SIZE(otg_current)) {
		rc = -EINVAL;
		goto out;
	}

	return 0;
out:
	smblib_write(chg, CMD_OTG_REG, 0);
	return rc;
}

static int _smblib_vbus_regulator_enable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc;

	smblib_dbg(chg, PR_OTG, "halt 1 in 8 mode\n");
	rc = smblib_masked_write(chg, OTG_ENG_OTG_CFG_REG,
				 ENG_BUCKBOOST_HALT1_8_MODE_BIT,
				 ENG_BUCKBOOST_HALT1_8_MODE_BIT);
	if (rc < 0) {
		smblib_err(chg, "Couldn't set OTG_ENG_OTG_CFG_REG rc=%d\n",
			rc);
		return rc;
	}

	smblib_dbg(chg, PR_OTG, "enabling OTG\n");

	if (chg->wa_flags & OTG_WA) {
		rc = smblib_enable_otg_wa(chg);
		if (rc < 0)
			smblib_err(chg, "Couldn't enable OTG rc=%d\n", rc);
	} else {
		rc = smblib_write(chg, CMD_OTG_REG, OTG_EN_BIT);
		if (rc < 0)
			smblib_err(chg, "Couldn't enable OTG rc=%d\n", rc);
	}

	return rc;
}

int smblib_vbus_regulator_enable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chg->otg_oc_lock);
	if (chg->otg_en)
		goto unlock;

	if (!chg->usb_icl_votable) {
		chg->usb_icl_votable = find_votable("USB_ICL");

		if (!chg->usb_icl_votable)
			return -EINVAL;
	}
	vote(chg->usb_icl_votable, USBIN_USBIN_BOOST_VOTER, true, 0);

	rc = _smblib_vbus_regulator_enable(rdev);
	if (rc >= 0)
		chg->otg_en = true;

unlock:
	mutex_unlock(&chg->otg_oc_lock);
	return rc;
}

static int _smblib_vbus_regulator_disable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc;

	if (chg->wa_flags & OTG_WA) {
		/* set OTG current limit to minimum value */
		rc = smblib_set_charge_param(chg, &chg->param.otg_cl,
						chg->param.otg_cl.min_u);
		if (rc < 0) {
			smblib_err(chg,
				"Couldn't set otg current limit rc=%d\n", rc);
			return rc;
		}
	}

	smblib_dbg(chg, PR_OTG, "disabling OTG\n");
	rc = smblib_write(chg, CMD_OTG_REG, 0);
	if (rc < 0) {
		smblib_err(chg, "Couldn't disable OTG regulator rc=%d\n", rc);
		return rc;
	}

	smblib_dbg(chg, PR_OTG, "start 1 in 8 mode\n");
	rc = smblib_masked_write(chg, OTG_ENG_OTG_CFG_REG,
				 ENG_BUCKBOOST_HALT1_8_MODE_BIT, 0);
	if (rc < 0) {
		smblib_err(chg, "Couldn't set OTG_ENG_OTG_CFG_REG rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int smblib_vbus_regulator_disable(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chg->otg_oc_lock);
	if (!chg->otg_en)
		goto unlock;

	rc = _smblib_vbus_regulator_disable(rdev);
	if (rc >= 0)
		chg->otg_en = false;

	if (chg->usb_icl_votable)
		vote(chg->usb_icl_votable, USBIN_USBIN_BOOST_VOTER, false, 0);
unlock:
	mutex_unlock(&chg->otg_oc_lock);
	return rc;
}

int smblib_vbus_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct smb_charger *chg = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&chg->otg_oc_lock);
	ret = chg->otg_en;
	mutex_unlock(&chg->otg_oc_lock);
	return ret;
}

/********************
 * BATT PSY GETTERS *
 ********************/

int smblib_get_prop_input_suspend(struct smb_charger *chg,
				  union power_supply_propval *val)
{
	val->intval
		= (get_client_vote(chg->usb_icl_votable, USER_VOTER) == 0)
		 && get_client_vote(chg->dc_suspend_votable, USER_VOTER);
	return 0;
}

int smblib_get_prop_batt_present(struct smb_charger *chg,
				union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, BATIF_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATIF_INT_RT_STS rc=%d\n", rc);
		return rc;
	}

	val->intval = !(stat & (BAT_THERM_OR_ID_MISSING_RT_STS_BIT
					| BAT_TERMINAL_MISSING_RT_STS_BIT));

	return rc;
}

int smblib_get_prop_batt_capacity(struct smb_charger *chg,
				  union power_supply_propval *val)
{
	int rc = -EINVAL;

	if (chg->fake_capacity >= 0) {
		val->intval = chg->fake_capacity;
		return 0;
	}

	if (chg->bms_psy)
		rc = power_supply_get_property(chg->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, val);
	return rc;
}
/*
mutex let gauge can query status wihout modified;
mutex not always works, ex: other thread get status between
gauge set flag and gauge get status

gauge_get_prop stands for prop-query from "gauge"
*/
bool batt_full_specified(union power_supply_propval *val, char* note){

			bool rc =false;
			mutex_lock(&g_fgChip->charge_status_lock);
			
			if (g_fgChip->charge_full && !gauge_get_prop){ 	
				val->intval = POWER_SUPPLY_STATUS_FULL;
				strcpy(note,"(modified%%)");
				rc =true;				
			}else if ( !gauge_get_prop && asus_get_prop_batt_capacity(smbchg_dev) == 100) {
				val->intval = POWER_SUPPLY_STATUS_FULL;
				strcpy(note,"(modified by reporting 100%%)");		
				rc =true;				
			}else{
				rc = false;
			}
			
			mutex_unlock(&g_fgChip->charge_status_lock);
			gauge_get_prop =0;
			return rc;
			
}

bool temp_alert_specified(union power_supply_propval *val, char* note){

			bool rc =false;

			if(g_Charger_mode && usb_alert_flag){
				val->intval = POWER_SUPPLY_STATUS_THERMAL_ALERT;
				strcpy(note,"(modified by alert)");
				rc = true;
			}
			else rc = false;

			return rc;
			
}

//ASUS_BSP +++ LiJen
bool temp_ubatlife_specified(union power_supply_propval *val, char* note){

			bool rc =false;

			if(is_ubatlife_dischg()){
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
				strcpy(note,"(modified by ubatlife)");
				rc = true;
			}
			else rc = false;

			return rc;
			
}
//ASUS_BSP --- LiJen

/*
Due to google treble, it's not suggest for AP to get QC status here;
But report QC status in charger mode may be OK.
*/
bool charger_mode_specified(union power_supply_propval *val){

			if (g_Charger_mode && (asus_CHG_TYPE == ASUS_QC_AC_ID || 
				asus_CHG_TYPE == ASUS_ABOVE_13P5W_ID||smbchg_dev->pd_active)){
				if(asus_get_prop_batt_capacity(smbchg_dev) <= QC_STATE_SOC_THD) {
					val->intval = POWER_SUPPLY_STATUS_QUICK_CHARGING;
				} else {
					val->intval = POWER_SUPPLY_STATUS_NOT_QUICK_CHARGING;
				}
				//WeiYu ++ force Quick charging for 13.5W
				val->intval = POWER_SUPPLY_STATUS_QUICK_CHARGING;
				return true;
			}
			/*	WeiYu ++
				Report 10W_QUICK_CHARGING will cause PANIC in COS;
				To work around, we report STATUS_QUICK_CHARGING instead.
				Then check switch for 10W/13.5W in COS
			*/
			else if(g_Charger_mode && (asus_CHG_TYPE == ASUS_NORMAL_AC_ID || 
				asus_CHG_TYPE == ASUS_10W_ID)){
				if(asus_get_prop_batt_capacity(smbchg_dev) <= QC_STATE_SOC_THD) {
					val->intval = POWER_SUPPLY_STATUS_QUICK_CHARGING;
				} else {
					val->intval = POWER_SUPPLY_STATUS_NOT_QUICK_CHARGING;
				}
				//WeiYu ++ force Not quick charging for 10W
				val->intval = POWER_SUPPLY_STATUS_NOT_QUICK_CHARGING;	
				return true;
			}
			else return false;
		
}

void batt_status_chg(union power_supply_propval *val){

	char note[32]= "";

	if (fg_batt_id_ready && g_fgChip != NULL && smbchg_dev != NULL){
		if(!batt_full_specified(val,note) && !charger_mode_specified(val))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;		
	}
	else		val->intval = POWER_SUPPLY_STATUS_CHARGING;		
	
	CHG_DBG("%s = %s %s\n",__func__,batt_status_text[val->intval],note);
}

void batt_status_disable(union power_supply_propval *val){

	char note[32]= "";

	if (fg_batt_id_ready && g_fgChip != NULL && smbchg_dev != NULL){
		if(!batt_full_specified(val,note) && !temp_alert_specified(val,note) && !temp_ubatlife_specified(val,note))
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;		
	}
	else		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;	
	
	CHG_DBG("%s = %s %s\n",__func__,batt_status_text[val->intval],note);

}


int smblib_get_prop_batt_status(struct smb_charger *chg,
				union power_supply_propval *val)
{
	union power_supply_propval pval = {0, };
	bool usb_online, dc_online, qnovo_en;
	u8 stat, pt_en_cmd;
	int rc;

	rc = smblib_get_prop_usb_online(chg, &pval);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get usb online property rc=%d\n",
			rc);
		return rc;
	}
	usb_online = (bool)pval.intval;

	rc = smblib_get_prop_dc_online(chg, &pval);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get dc online property rc=%d\n",
			rc);
		return rc;
	}
	dc_online = (bool)pval.intval;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
			rc);
		return rc;
	}
	stat = stat & BATTERY_CHARGER_STATUS_MASK;

	if (!usb_online && !dc_online) {
		switch (stat) {
		case TERMINATE_CHARGE:
		case INHIBIT_CHARGE:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		}
		return rc;
	}

	switch (stat) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FAST_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		batt_status_chg(val);

		break;
	case TERMINATE_CHARGE:
	case INHIBIT_CHARGE:
		printk("[BAT][CHG] Batt_status = FULL\n");
		val->intval = POWER_SUPPLY_STATUS_FULL;
		break;
	case DISABLE_CHARGE:
		batt_status_disable(val);

		break;
	default:
		printk("[BAT][CHG] Batt_status = UNKNOWN\n");
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}

	if ((val->intval != POWER_SUPPLY_STATUS_CHARGING) && (val->intval != POWER_SUPPLY_STATUS_QUICK_CHARGING) && 
			(val->intval != POWER_SUPPLY_STATUS_NOT_QUICK_CHARGING)&& (val->intval != POWER_SUPPLY_STATUS_10W_NOT_QUICK_CHARGING) && 
			(val->intval != POWER_SUPPLY_STATUS_10W_QUICK_CHARGING))
		return 0;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_7_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
				rc);
			return rc;
	}

	stat &= ENABLE_TRICKLE_BIT | ENABLE_PRE_CHARGING_BIT |
		 ENABLE_FAST_CHARGING_BIT | ENABLE_FULLON_MODE_BIT;

	rc = smblib_read(chg, QNOVO_PT_ENABLE_CMD_REG, &pt_en_cmd);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read QNOVO_PT_ENABLE_CMD_REG rc=%d\n",
				rc);
		return rc;
	}

	qnovo_en = (bool)(pt_en_cmd & QNOVO_PT_ENABLE_CMD_BIT);

	/* ignore stat7 when qnovo is enabled */
	if (!qnovo_en && !stat){
		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;

	//ASUS_BSP +++
		if(g_ubatterylife_enable_flag == 1){
			if(ubatlife_chg_status == UBATLIFE_CHG_THD)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			if(ubatlife_chg_status == UBATLIFE_DISCHG_THD)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
	//ASUS_BSP ---
	}
	return 0;
}

int smblib_get_prop_batt_charge_type(struct smb_charger *chg,
				union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
			rc);
		return rc;
	}

	switch (stat & BATTERY_CHARGER_STATUS_MASK) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case FAST_CHARGE:
	case FULLON_CHARGE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case TAPER_CHARGE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_TAPER;
		break;
	default:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	return rc;
}

int smblib_get_prop_batt_health(struct smb_charger *chg,
				union power_supply_propval *val)
{
	union power_supply_propval pval;
	int rc;
	int effective_fv_uv;
	u8 stat;
	u8 chg_stat,vbus_stat;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_2_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
			rc);
		return rc;
	}
	smblib_dbg(chg, PR_REGISTER, "BATTERY_CHARGER_STATUS_2 = 0x%02x\n",
		   stat);

	if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		rc = smblib_get_prop_batt_voltage_now(chg, &pval);
		if (!rc) {
			/*
			 * If Vbatt is within 40mV above Vfloat, then don't
			 * treat it as overvoltage.
			 */
			effective_fv_uv = get_effective_result(chg->fv_votable);
			if (pval.intval >= effective_fv_uv + 40000) {
				val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
				smblib_err(chg, "battery over-voltage vbat_fg = %duV, fv = %duV\n",
						pval.intval, effective_fv_uv);
				goto done;
			}
		}
	}

	if (stat & BAT_TEMP_STATUS_TOO_COLD_BIT)
		val->intval = POWER_SUPPLY_HEALTH_COLD;
	else if (stat & BAT_TEMP_STATUS_TOO_HOT_BIT)
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (stat & BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT)
		val->intval = POWER_SUPPLY_HEALTH_COOL;
	else if (stat & BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT){
		val->intval = POWER_SUPPLY_HEALTH_WARM;
		
		//WeiYu ++ modify for jeita icon issue
		rc = smblib_read(smbchg_dev, USBIN_BASE + INT_RT_STS_OFFSET, &vbus_stat);	
		rc = smblib_read(smbchg_dev, CHARGING_ENABLE_CMD_REG, &chg_stat);	
		if((bool)(vbus_stat & USBIN_PLUGIN_RT_STS_BIT)){
			if((chg_stat&CHARGING_ENABLE_CMD_BIT) == 0){
				val->intval = POWER_SUPPLY_HEALTH_GOOD;
				//CHG_DBG_EVT("Warm-temp but reporting health good due to charging is enabled\n");
			}
			else{
				val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			}
		}

	}
	else
		val->intval = POWER_SUPPLY_HEALTH_GOOD;

done:
	return rc;
}

int smblib_get_prop_system_temp_level(struct smb_charger *chg,
				union power_supply_propval *val)
{
	val->intval = chg->system_temp_level;
	return 0;
}

int smblib_get_prop_input_current_limited(struct smb_charger *chg,
				union power_supply_propval *val)
{
	u8 stat;
	int rc;

	if (chg->fake_input_current_limited >= 0) {
		val->intval = chg->fake_input_current_limited;
		return 0;
	}

	rc = smblib_read(chg, AICL_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read AICL_STATUS rc=%d\n", rc);
		return rc;
	}
	val->intval = (stat & SOFT_ILIMIT_BIT) || chg->is_hdc;
	return 0;
}

int smblib_get_prop_batt_voltage_now(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_VOLTAGE_NOW, val);
	return rc;
}

int smblib_get_prop_batt_current_now(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_CURRENT_NOW, val);
	return rc;
}

int smblib_get_prop_batt_temp(struct smb_charger *chg,
			      union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_TEMP, val);
	return rc;
}

int smblib_get_prop_batt_charge_done(struct smb_charger *chg,
					union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
			rc);
		return rc;
	}

	stat = stat & BATTERY_CHARGER_STATUS_MASK;
	val->intval = (stat == TERMINATE_CHARGE);
	return 0;
}

int smblib_get_prop_charge_qnovo_enable(struct smb_charger *chg,
				  union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, QNOVO_PT_ENABLE_CMD_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read QNOVO_PT_ENABLE_CMD rc=%d\n",
			rc);
		return rc;
	}

	val->intval = (bool)(stat & QNOVO_PT_ENABLE_CMD_BIT);
	return 0;
}

int smblib_get_prop_batt_charge_counter(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_CHARGE_COUNTER, val);
	return rc;
}

/***********************
 * BATTERY PSY SETTERS *
 ***********************/

int smblib_set_prop_input_suspend(struct smb_charger *chg,
				  const union power_supply_propval *val)
{
	int rc;

	/* vote 0mA when suspended */
	rc = vote(chg->usb_icl_votable, USER_VOTER, (bool)val->intval, 0);
	if (rc < 0) {
		smblib_err(chg, "Couldn't vote to %s USB rc=%d\n",
			(bool)val->intval ? "suspend" : "resume", rc);
		return rc;
	}

	rc = vote(chg->dc_suspend_votable, USER_VOTER, (bool)val->intval, 0);
	if (rc < 0) {
		smblib_err(chg, "Couldn't vote to %s DC rc=%d\n",
			(bool)val->intval ? "suspend" : "resume", rc);
		return rc;
	}

	power_supply_changed(chg->batt_psy);
	return rc;
}

int smblib_set_prop_batt_capacity(struct smb_charger *chg,
				  const union power_supply_propval *val)
{
	chg->fake_capacity = val->intval;

	power_supply_changed(chg->batt_psy);

	return 0;
}

int smblib_set_prop_system_temp_level(struct smb_charger *chg,
				const union power_supply_propval *val)
{
	return 0;

	if (val->intval < 0)
		return -EINVAL;

	if (chg->thermal_levels <= 0)
		return -EINVAL;

	if (val->intval > chg->thermal_levels)
		return -EINVAL;

	chg->system_temp_level = val->intval;
	/* disable parallel charge in case of system temp level */
	vote(chg->pl_disable_votable, THERMAL_DAEMON_VOTER,
			chg->system_temp_level ? true : false, 0);

	if (chg->system_temp_level == chg->thermal_levels)
		return vote(chg->chg_disable_votable,
			THERMAL_DAEMON_VOTER, true, 0);

	vote(chg->chg_disable_votable, THERMAL_DAEMON_VOTER, false, 0);
	if (chg->system_temp_level == 0)
		return vote(chg->fcc_votable, THERMAL_DAEMON_VOTER, false, 0);

	vote(chg->fcc_votable, THERMAL_DAEMON_VOTER, true,
			chg->thermal_mitigation[chg->system_temp_level]);
	return 0;
}

int smblib_set_prop_charge_qnovo_enable(struct smb_charger *chg,
				  const union power_supply_propval *val)
{
	int rc = 0;

	rc = smblib_masked_write(chg, QNOVO_PT_ENABLE_CMD_REG,
			QNOVO_PT_ENABLE_CMD_BIT,
			val->intval ? QNOVO_PT_ENABLE_CMD_BIT : 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable qnovo rc=%d\n", rc);
		return rc;
	}

	return rc;
}

int smblib_set_prop_input_current_limited(struct smb_charger *chg,
				const union power_supply_propval *val)
{
	chg->fake_input_current_limited = val->intval;
	return 0;
}

int smblib_rerun_aicl(struct smb_charger *chg)
{
	int rc, settled_icl_ua;
	u8 stat;

	rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n",
								rc);
		return rc;
	}

	/* USB is suspended so skip re-running AICL */
	if (stat & USBIN_SUSPEND_STS_BIT)
		return rc;

	smblib_dbg(chg, PR_MISC, "re-running AICL\n");
	rc = smblib_get_charge_param(chg, &chg->param.icl_stat,
			&settled_icl_ua);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get settled ICL rc=%d\n", rc);
		return rc;
	}

	vote(chg->usb_icl_votable, AICL_RERUN_VOTER, true,
			max(settled_icl_ua - chg->param.usb_icl.step_u,
				chg->param.usb_icl.step_u));
	vote(chg->usb_icl_votable, AICL_RERUN_VOTER, false, 0);

	return 0;
}

static int smblib_dp_pulse(struct smb_charger *chg)
{
	int rc;

	/* QC 3.0 increment */
	rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, SINGLE_INCREMENT_BIT,
			SINGLE_INCREMENT_BIT);
	if (rc < 0)
		smblib_err(chg, "Couldn't write to CMD_HVDCP_2_REG rc=%d\n",
				rc);

	return rc;
}

static int smblib_dm_pulse(struct smb_charger *chg)
{
	int rc;

	/* QC 3.0 decrement */
	rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, SINGLE_DECREMENT_BIT,
			SINGLE_DECREMENT_BIT);
	if (rc < 0)
		smblib_err(chg, "Couldn't write to CMD_HVDCP_2_REG rc=%d\n",
				rc);

	return rc;
}

int smblib_dp_dm(struct smb_charger *chg, int val)
{
	int target_icl_ua, rc = 0;
	union power_supply_propval pval;

	switch (val) {
	case POWER_SUPPLY_DP_DM_DP_PULSE:
		rc = smblib_dp_pulse(chg);
		if (!rc)
			chg->pulse_cnt++;
		smblib_dbg(chg, PR_PARALLEL, "DP_DM_DP_PULSE rc=%d cnt=%d\n",
				rc, chg->pulse_cnt);
		break;
	case POWER_SUPPLY_DP_DM_DM_PULSE:
		rc = smblib_dm_pulse(chg);
		if (!rc && chg->pulse_cnt)
			chg->pulse_cnt--;
		smblib_dbg(chg, PR_PARALLEL, "DP_DM_DM_PULSE rc=%d cnt=%d\n",
				rc, chg->pulse_cnt);
		break;
	case POWER_SUPPLY_DP_DM_ICL_DOWN:
		target_icl_ua = get_effective_result(chg->usb_icl_votable);
		if (target_icl_ua < 0) {
			/* no client vote, get the ICL from charger */
			rc = power_supply_get_property(chg->usb_psy,
					POWER_SUPPLY_PROP_HW_CURRENT_MAX,
					&pval);
			if (rc < 0) {
				smblib_err(chg,
					"Couldn't get max current rc=%d\n",
					rc);
				return rc;
			}
			target_icl_ua = pval.intval;
		}

		/*
		 * Check if any other voter voted on USB_ICL in case of
		 * voter other than SW_QC3_VOTER reset and restart reduction
		 * again.
		 */
		if (target_icl_ua != get_client_vote(chg->usb_icl_votable,
							SW_QC3_VOTER))
			chg->usb_icl_delta_ua = 0;

		chg->usb_icl_delta_ua += 100000;
		vote(chg->usb_icl_votable, SW_QC3_VOTER, true,
						target_icl_ua - 100000);
		smblib_dbg(chg, PR_PARALLEL, "ICL DOWN ICL=%d reduction=%d\n",
				target_icl_ua, chg->usb_icl_delta_ua);
		break;
	case POWER_SUPPLY_DP_DM_ICL_UP:
	default:
		break;
	}

	return rc;
}

int smblib_disable_hw_jeita(struct smb_charger *chg, bool disable)
{
	int rc;
	u8 mask;

	/*
	 * Disable h/w base JEITA compensation if s/w JEITA is enabled
	 */
	mask = JEITA_EN_COLD_SL_FCV_BIT
		| JEITA_EN_HOT_SL_FCV_BIT
		| JEITA_EN_HOT_SL_CCC_BIT
		| JEITA_EN_COLD_SL_CCC_BIT,
	rc = smblib_masked_write(chg, JEITA_EN_CFG_REG, mask,
			disable ? 0 : mask);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure s/w jeita rc=%d\n",
			rc);
		return rc;
	}
	return 0;
}

/*******************
 * DC PSY GETTERS *
 *******************/

int smblib_get_prop_dc_present(struct smb_charger *chg,
				union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, DCIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read DCIN_RT_STS rc=%d\n", rc);
		return rc;
	}

	val->intval = (bool)(stat & DCIN_PLUGIN_RT_STS_BIT);
	return 0;
}

int smblib_get_prop_dc_online(struct smb_charger *chg,
			       union power_supply_propval *val)
{
	int rc = 0;
	u8 stat;

	if (get_client_vote(chg->dc_suspend_votable, USER_VOTER)) {
		val->intval = false;
		return rc;
	}

	rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n",
			rc);
		return rc;
	}
	smblib_dbg(chg, PR_REGISTER, "POWER_PATH_STATUS = 0x%02x\n",
		   stat);


	val->intval = (stat & USE_DCIN_BIT) &&
		      (stat & VALID_INPUT_POWER_SOURCE_STS_BIT);

	return rc;
}

int smblib_get_prop_dc_current_max(struct smb_charger *chg,
				    union power_supply_propval *val)
{
	val->intval = get_effective_result_locked(chg->dc_icl_votable);
	return 0;
}

/*******************
 * DC PSY SETTERS *
 * *****************/

int smblib_set_prop_dc_current_max(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	int rc;

	rc = vote(chg->dc_icl_votable, USER_VOTER, true, val->intval);
	return rc;
}

/*******************
 * USB PSY GETTERS *
 *******************/

int smblib_get_prop_usb_present(struct smb_charger *chg,
				union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read USBIN_RT_STS rc=%d\n", rc);
		return rc;
	}

	val->intval = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);
	return 0;
}

int smblib_get_prop_usb_online(struct smb_charger *chg,
			       union power_supply_propval *val)
{
	int rc = 0;
	u8 stat;
	int usb_otg_present;
	u8 reg;
	
	if (get_client_vote_locked(chg->usb_icl_votable, USER_VOTER) == 0) {
		val->intval = false;
		return rc;
	}

	rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n",
			rc);
		return rc;
	}
	smblib_dbg(chg, PR_REGISTER, "POWER_PATH_STATUS = 0x%02x\n",
		   stat);

	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	usb_otg_present = reg & CC_ATTACHED_BIT;
	
	// temp alert suspend may cause charger mode reboot, so check cc present
//ASUS_BSP +++
	if (asus_adapter_detecting_flag)
		val->intval = 1;
	else if((g_Charger_mode && usb_alert_flag) 
		|| is_ubatlife_dischg()){
		val->intval = usb_otg_present;
		if(val->intval == 1){
			CHG_DBG("force reporting online(%d) due to alert:%d, ubat:%d\n", usb_otg_present, usb_alert_flag, is_ubatlife_dischg());
		}
	}	
//ASUS_BSP ---
	else
		val->intval = (stat & USE_USBIN_BIT) &&
						(stat & VALID_INPUT_POWER_SOURCE_STS_BIT);
	return rc;
}

int smblib_get_prop_usb_voltage_max(struct smb_charger *chg,
				    union power_supply_propval *val)
{
	switch (chg->real_charger_type) {
	case POWER_SUPPLY_TYPE_USB_HVDCP:
	case POWER_SUPPLY_TYPE_USB_PD:
		if (chg->smb_version == PM660_SUBTYPE)
			val->intval = MICRO_9V;
		else
			val->intval = MICRO_12V;
		break;
	default:
		val->intval = MICRO_5V;
		break;
	}

	return 0;
}

int smblib_get_prop_usb_voltage_now(struct smb_charger *chg,
				    union power_supply_propval *val)
{
	int rc = 0;

	rc = smblib_get_prop_usb_present(chg, val);
	if (rc < 0 || !val->intval)
		return rc;

	if (!chg->iio.usbin_v_chan ||
		PTR_ERR(chg->iio.usbin_v_chan) == -EPROBE_DEFER)
		chg->iio.usbin_v_chan = iio_channel_get(chg->dev, "usbin_v");

	if (IS_ERR(chg->iio.usbin_v_chan))
		return PTR_ERR(chg->iio.usbin_v_chan);

	return iio_read_channel_processed(chg->iio.usbin_v_chan, &val->intval);
}

int smblib_get_prop_usb_current_now(struct smb_charger *chg,
				    union power_supply_propval *val)
{
	int rc = 0;

	rc = smblib_get_prop_usb_present(chg, val);
	if (rc < 0 || !val->intval)
		return rc;

	if (!chg->iio.usbin_i_chan ||
		PTR_ERR(chg->iio.usbin_i_chan) == -EPROBE_DEFER)
		chg->iio.usbin_i_chan = iio_channel_get(chg->dev, "usbin_i");

	if (IS_ERR(chg->iio.usbin_i_chan))
		return PTR_ERR(chg->iio.usbin_i_chan);

	return iio_read_channel_processed(chg->iio.usbin_i_chan, &val->intval);
}

int smblib_get_prop_charger_temp(struct smb_charger *chg,
				 union power_supply_propval *val)
{
	int rc;

	if (!chg->iio.temp_chan ||
		PTR_ERR(chg->iio.temp_chan) == -EPROBE_DEFER)
		chg->iio.temp_chan = iio_channel_get(chg->dev, "charger_temp");

	if (IS_ERR(chg->iio.temp_chan))
		return PTR_ERR(chg->iio.temp_chan);

	rc = iio_read_channel_processed(chg->iio.temp_chan, &val->intval);
	val->intval /= 100;
	return rc;
}

int smblib_get_prop_charger_temp_max(struct smb_charger *chg,
				    union power_supply_propval *val)
{
	int rc;

	if (!chg->iio.temp_max_chan ||
		PTR_ERR(chg->iio.temp_max_chan) == -EPROBE_DEFER)
		chg->iio.temp_max_chan = iio_channel_get(chg->dev,
							 "charger_temp_max");
	if (IS_ERR(chg->iio.temp_max_chan))
		return PTR_ERR(chg->iio.temp_max_chan);

	rc = iio_read_channel_processed(chg->iio.temp_max_chan, &val->intval);
	val->intval /= 100;
	return rc;
}

int smblib_get_prop_typec_cc_orientation(struct smb_charger *chg,
					 union power_supply_propval *val)
{
	if (chg->typec_status[3] & CC_ATTACHED_BIT)
		val->intval =
			(bool)(chg->typec_status[3] & CC_ORIENTATION_BIT) + 1;
	else
		val->intval = 0;

	return 0;
}

static const char * const smblib_typec_mode_name[] = {
	[POWER_SUPPLY_TYPEC_NONE]		  = "NONE",
	[POWER_SUPPLY_TYPEC_SOURCE_DEFAULT]	  = "SOURCE_DEFAULT",
	[POWER_SUPPLY_TYPEC_SOURCE_MEDIUM]	  = "SOURCE_MEDIUM",
	[POWER_SUPPLY_TYPEC_SOURCE_HIGH]	  = "SOURCE_HIGH",
	[POWER_SUPPLY_TYPEC_NON_COMPLIANT]	  = "NON_COMPLIANT",
	[POWER_SUPPLY_TYPEC_SINK]		  = "SINK",
	[POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE]   = "SINK_POWERED_CABLE",
	[POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY] = "SINK_DEBUG_ACCESSORY",
	[POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER]   = "SINK_AUDIO_ADAPTER",
	[POWER_SUPPLY_TYPEC_POWERED_CABLE_ONLY]   = "POWERED_CABLE_ONLY",
};

static int smblib_get_prop_ufp_mode(struct smb_charger *chg)
{

	CHG_DBG("%s: 0x%x",__func__,(u8)(chg->typec_status[0] ));

	switch (chg->typec_status[0]) {	
	case UFP_TYPEC_RDSTD_BIT:
		return POWER_SUPPLY_TYPEC_SOURCE_DEFAULT;
	case UFP_TYPEC_RD1P5_BIT:
		return POWER_SUPPLY_TYPEC_SOURCE_MEDIUM;
	case UFP_TYPEC_RD3P0_BIT:
		return POWER_SUPPLY_TYPEC_SOURCE_HIGH;
	default:
		break;
	}

	return POWER_SUPPLY_TYPEC_NONE;
}

static int smblib_get_prop_dfp_mode(struct smb_charger *chg)
{

	CHG_DBG("%s: 0x%x",__func__,(u8)(chg->typec_status[1] & DFP_TYPEC_MASK));

	switch (chg->typec_status[1] & DFP_TYPEC_MASK) {
	case DFP_RA_RA_BIT:
		return POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER;
	case DFP_RD_RD_BIT:
		return POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY;
	case DFP_RD_RA_VCONN_BIT:
		return POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE;
	case DFP_RD_OPEN_BIT:
		return POWER_SUPPLY_TYPEC_SINK;
	default:
		break;
	}

	return POWER_SUPPLY_TYPEC_NONE;
}

static int smblib_get_prop_typec_mode(struct smb_charger *chg)
{
	if (chg->typec_status[3] & UFP_DFP_MODE_STATUS_BIT)
		return smblib_get_prop_dfp_mode(chg);
	else
		return smblib_get_prop_ufp_mode(chg);
}

int smblib_get_prop_typec_power_role(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	int rc = 0;
	u8 ctrl;

	rc = smblib_read(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, &ctrl);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read TYPE_C_INTRPT_ENB_SOFTWARE_CTRL rc=%d\n",
			rc);
		return rc;
	}
	smblib_dbg(chg, PR_REGISTER, "TYPE_C_INTRPT_ENB_SOFTWARE_CTRL = 0x%02x\n",
		   ctrl);

	if (ctrl & TYPEC_DISABLE_CMD_BIT) {
		val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
		return rc;
	}

	switch (ctrl & (DFP_EN_CMD_BIT | UFP_EN_CMD_BIT)) {
	case 0:
		val->intval = POWER_SUPPLY_TYPEC_PR_DUAL;
		break;
	case DFP_EN_CMD_BIT:
		val->intval = POWER_SUPPLY_TYPEC_PR_SOURCE;
		break;
	case UFP_EN_CMD_BIT:
		val->intval = POWER_SUPPLY_TYPEC_PR_SINK;
		break;
	default:
		val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
		smblib_err(chg, "unsupported power role 0x%02lx\n",
			ctrl & (DFP_EN_CMD_BIT | UFP_EN_CMD_BIT));
		return -EINVAL;
	}

	return rc;
}

int smblib_get_prop_pd_allowed(struct smb_charger *chg,
			       union power_supply_propval *val)
{
	val->intval = get_effective_result(chg->pd_allowed_votable);
	return 0;
}

int smblib_get_prop_input_current_settled(struct smb_charger *chg,
					  union power_supply_propval *val)
{
	return smblib_get_charge_param(chg, &chg->param.icl_stat, &val->intval);
}

#define HVDCP3_STEP_UV	200000
int smblib_get_prop_input_voltage_settled(struct smb_charger *chg,
						union power_supply_propval *val)
{
	int rc, pulses;

	switch (chg->real_charger_type) {
	case POWER_SUPPLY_TYPE_USB_HVDCP_3:
		rc = smblib_get_pulse_cnt(chg, &pulses);
		if (rc < 0) {
			smblib_err(chg,
				"Couldn't read QC_PULSE_COUNT rc=%d\n", rc);
			return 0;
		}
		val->intval = MICRO_5V + HVDCP3_STEP_UV * pulses;
		break;
	case POWER_SUPPLY_TYPE_USB_PD:
		val->intval = chg->voltage_min_uv;
		break;
	default:
		val->intval = MICRO_5V;
		break;
	}

	return 0;
}

int smblib_get_prop_pd_in_hard_reset(struct smb_charger *chg,
			       union power_supply_propval *val)
{
	val->intval = chg->pd_hard_reset;
	return 0;
}

int smblib_get_pe_start(struct smb_charger *chg,
			       union power_supply_propval *val)
{
	/*
	 * hvdcp timeout voter is the last one to allow pd. Use its vote
	 * to indicate start of pe engine
	 */
	val->intval
		= !get_client_vote_locked(chg->pd_disallowed_votable_indirect,
			HVDCP_TIMEOUT_VOTER);
	return 0;
}

int smblib_get_prop_die_health(struct smb_charger *chg,
						union power_supply_propval *val)
{
	int rc;
	u8 stat;

	rc = smblib_read(chg, TEMP_RANGE_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read TEMP_RANGE_STATUS_REG rc=%d\n",
									rc);
		return rc;
	}

	/* TEMP_RANGE bits are mutually exclusive */
	switch (stat & TEMP_RANGE_MASK) {
	case TEMP_BELOW_RANGE_BIT:
		val->intval = POWER_SUPPLY_HEALTH_COOL;
		break;
	case TEMP_WITHIN_RANGE_BIT:
		val->intval = POWER_SUPPLY_HEALTH_WARM;
		break;
	case TEMP_ABOVE_RANGE_BIT:
		val->intval = POWER_SUPPLY_HEALTH_HOT;
		break;
	case ALERT_LEVEL_BIT:
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	default:
		val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	return 0;
}

#define SDP_CURRENT_UA			500000
#define CDP_CURRENT_UA			1500000
#define DCP_CURRENT_UA			1500000
#define HVDCP_CURRENT_UA		3000000
#define TYPEC_DEFAULT_CURRENT_UA	900000
#define TYPEC_MEDIUM_CURRENT_UA		1500000
#define TYPEC_HIGH_CURRENT_UA		3000000
static int get_rp_based_dcp_current(struct smb_charger *chg, int typec_mode)
{
	int rp_ua;

	switch (typec_mode) {
	case POWER_SUPPLY_TYPEC_SOURCE_HIGH:
		rp_ua = TYPEC_HIGH_CURRENT_UA;
		break;
	case POWER_SUPPLY_TYPEC_SOURCE_MEDIUM:
	case POWER_SUPPLY_TYPEC_SOURCE_DEFAULT:
	/* fall through */
	default:
		rp_ua = DCP_CURRENT_UA;
	}

	return rp_ua;
}

/*******************
 * USB PSY SETTERS *
 * *****************/

int smblib_set_prop_pd_current_max(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	int rc;

	if (chg->pd_active)
		rc = vote(chg->usb_icl_votable, PD_VOTER, true, val->intval);
	else
		rc = -EPERM;

	return rc;
}

static int smblib_handle_usb_current(struct smb_charger *chg,
					int usb_current)
{
	int rc = 0, rp_ua, typec_mode;

	if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_FLOAT) {
		if (usb_current == -ETIMEDOUT) {
			/*
			 * Valid FLOAT charger, report the current based
			 * of Rp
			 */
			typec_mode = smblib_get_prop_typec_mode(chg);
			rp_ua = get_rp_based_dcp_current(chg, typec_mode);
			rc = vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER,
								true, rp_ua);
			if (rc < 0)
				return rc;
		} else {
			/*
			 * FLOAT charger detected as SDP by USB driver,
			 * charge with the requested current and update the
			 * real_charger_type
			 */
			chg->real_charger_type = POWER_SUPPLY_TYPE_USB;
			rc = vote(chg->usb_icl_votable, USB_PSY_VOTER,
						true, usb_current);
			if (rc < 0)
				return rc;
			rc = vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER,
							false, 0);
			if (rc < 0)
				return rc;
		}
	} else {
		rc = vote(chg->usb_icl_votable, USB_PSY_VOTER,
					true, usb_current);
	}

	return rc;
}

int smblib_set_prop_sdp_current_max(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	int rc = 0;

	if (!chg->pd_active) {
		rc = smblib_handle_usb_current(chg, val->intval);
	} else if (chg->system_suspend_supported) {
		if (val->intval <= USBIN_25MA)
			rc = vote(chg->usb_icl_votable,
				PD_SUSPEND_SUPPORTED_VOTER, true, val->intval);
		else
			rc = vote(chg->usb_icl_votable,
				PD_SUSPEND_SUPPORTED_VOTER, false, 0);
	}
	return rc;
}

int smblib_set_prop_boost_current(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	int rc = 0;

	rc = smblib_set_charge_param(chg, &chg->param.freq_boost,
				val->intval <= chg->boost_threshold_ua ?
				chg->chg_freq.freq_below_otg_threshold :
				chg->chg_freq.freq_above_otg_threshold);
	if (rc < 0) {
		dev_err(chg->dev, "Error in setting freq_boost rc=%d\n", rc);
		return rc;
	}

	chg->boost_current_ua = val->intval;
	return rc;
}

int smblib_set_prop_typec_power_role(struct smb_charger *chg,
				     const union power_supply_propval *val)
{
	int rc = 0;
	u8 power_role;

	switch (val->intval) {
	case POWER_SUPPLY_TYPEC_PR_NONE:
		power_role = TYPEC_DISABLE_CMD_BIT;
		break;
	case POWER_SUPPLY_TYPEC_PR_DUAL:
		power_role = 0;
		break;
	case POWER_SUPPLY_TYPEC_PR_SINK:
		power_role = UFP_EN_CMD_BIT;
		break;
	case POWER_SUPPLY_TYPEC_PR_SOURCE:
		power_role = DFP_EN_CMD_BIT;
		break;
	default:
		smblib_err(chg, "power role %d not supported\n", val->intval);
		return -EINVAL;
	}

	if (power_role == UFP_EN_CMD_BIT) {
		/* disable PBS workaround when forcing sink mode */
		rc = smblib_write(chg, TM_IO_DTEST4_SEL, 0x0);
		if (rc < 0) {
			smblib_err(chg, "Couldn't write to TM_IO_DTEST4_SEL rc=%d\n",
				rc);
		}
	} else {
		/* restore it back to 0xA5 */
		rc = smblib_write(chg, TM_IO_DTEST4_SEL, 0xA5);
		if (rc < 0) {
			smblib_err(chg, "Couldn't write to TM_IO_DTEST4_SEL rc=%d\n",
				rc);
		}
	}

	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK, power_role);
	if (rc < 0) {
		smblib_err(chg, "Couldn't write 0x%02x to TYPE_C_INTRPT_ENB_SOFTWARE_CTRL rc=%d\n",
			power_role, rc);
		return rc;
	}

	return rc;
}

int smblib_set_prop_pd_voltage_min(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	int rc, min_uv;

	min_uv = min(val->intval, chg->voltage_max_uv);
	rc = smblib_set_usb_pd_allowed_voltage(chg, min_uv,
					       chg->voltage_max_uv);
	if (rc < 0) {
		smblib_err(chg, "invalid max voltage %duV rc=%d\n",
			val->intval, rc);
		return rc;
	}

	chg->voltage_min_uv = min_uv;
	power_supply_changed(chg->usb_main_psy);
	return rc;
}

int smblib_set_prop_pd_voltage_max(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	int rc, max_uv;

	max_uv = max(val->intval, chg->voltage_min_uv);
	rc = smblib_set_usb_pd_allowed_voltage(chg, chg->voltage_min_uv,
					       max_uv);
	if (rc < 0) {
		smblib_err(chg, "invalid min voltage %duV rc=%d\n",
			val->intval, rc);
		return rc;
	}

	chg->voltage_max_uv = max_uv;
	return rc;
}

int smblib_set_prop_pd_active(struct smb_charger *chg,
			      const union power_supply_propval *val)
{
	int rc;
	bool orientation, sink_attached, hvdcp;
	u8 stat;
	static bool scheduled = false;
	
	if (!get_effective_result(chg->pd_allowed_votable))
		return -EINVAL;

	chg->pd_active = val->intval;
	if (chg->pd_active) {
		vote(chg->apsd_disable_votable, PD_VOTER, true, 0);
		vote(chg->pd_allowed_votable, PD_VOTER, true, 0);
		vote(chg->usb_irq_enable_votable, PD_VOTER, true, 0);

		/*
		 * VCONN_EN_ORIENTATION_BIT controls whether to use CC1 or CC2
		 * line when TYPEC_SPARE_CFG_BIT (CC pin selection s/w override)
		 * is set or when VCONN_EN_VALUE_BIT is set.
		 */
		orientation = chg->typec_status[3] & CC_ORIENTATION_BIT;
		rc = smblib_masked_write(chg,
				TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				VCONN_EN_ORIENTATION_BIT,
				orientation ? 0 : VCONN_EN_ORIENTATION_BIT);
		if (rc < 0)
			smblib_err(chg,
				"Couldn't enable vconn on CC line rc=%d\n", rc);

		/* SW controlled CC_OUT */
		rc = smblib_masked_write(chg, TAPER_TIMER_SEL_CFG_REG,
				TYPEC_SPARE_CFG_BIT, TYPEC_SPARE_CFG_BIT);
		if (rc < 0)
			smblib_err(chg, "Couldn't enable SW cc_out rc=%d\n",
									rc);

		/*
		 * Enforce 500mA for PD until the real vote comes in later.
		 * It is guaranteed that pd_active is set prior to
		 * pd_current_max
		 */
		rc = vote(chg->usb_icl_votable, PD_VOTER, true, USBIN_500MA);
		if (rc < 0)
			smblib_err(chg, "Couldn't vote for USB ICL rc=%d\n",
									rc);

		/* since PD was found the cable must be non-legacy */
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, false, 0);

		/* clear USB ICL vote for DCP_VOTER */
		rc = vote(chg->usb_icl_votable, DCP_VOTER, false, 0);
		if (rc < 0)
			smblib_err(chg, "Couldn't un-vote DCP from USB ICL rc=%d\n",
									rc);

		/* remove USB_PSY_VOTER */
		rc = vote(chg->usb_icl_votable, USB_PSY_VOTER, false, 0);
		if (rc < 0)
			smblib_err(chg, "Couldn't unvote USB_PSY rc=%d\n", rc);
	} else {
		rc = smblib_read(chg, APSD_STATUS_REG, &stat);
		if (rc < 0) {
			smblib_err(chg, "Couldn't read APSD status rc=%d\n",
									rc);
			return rc;
		}

		hvdcp = stat & QC_CHARGER_BIT;
		vote(chg->apsd_disable_votable, PD_VOTER, false, 0);
		vote(chg->pd_allowed_votable, PD_VOTER, true, 0);
		vote(chg->usb_irq_enable_votable, PD_VOTER, true, 0);
		vote(chg->hvdcp_disable_votable_indirect, PD_INACTIVE_VOTER,
								false, 0);

		/* HW controlled CC_OUT */
		rc = smblib_masked_write(chg, TAPER_TIMER_SEL_CFG_REG,
							TYPEC_SPARE_CFG_BIT, 0);
		if (rc < 0)
			smblib_err(chg, "Couldn't enable HW cc_out rc=%d\n",
									rc);

		/*
		 * This WA should only run for HVDCP. Non-legacy SDP/CDP could
		 * draw more, but this WA will remove Rd causing VBUS to drop,
		 * and data could be interrupted. Non-legacy DCP could also draw
		 * more, but it may impact compliance.
		 */
		 /*  WeiYu ++ typeC 1.5A COS and car charger issue
			make COS/MOS run legacy WA once, for
			COS: typeC 1.5/3A legacy error
			MOS: scnario transition fail, such as COS->MOS typeC 1.5/3A legacy error
			[concern] should check impact on non-legacy cable
			[history] if always run this WA, car charger in COS will 
					in vbus drop&up <-> run WA loop
		 */
		sink_attached = chg->typec_status[3] & UFP_DFP_MODE_STATUS_BIT;
		if (!chg->typec_legacy_valid && !sink_attached && hvdcp)
			schedule_work(&chg->legacy_detection_work);
		// WeiYu ++ else if for typeC 1.5A COS issue
		else if(!chg->typec_legacy_valid && !sink_attached &&  !scheduled){
			schedule_work(&chg->legacy_detection_work);
			scheduled = true;
			CHG_DBG_EVT("asus run legacy once \n");
		}

	}

	smblib_update_usb_type(chg);
	power_supply_changed(chg->usb_psy);
	return rc;
}

int smblib_set_prop_ship_mode(struct smb_charger *chg,
				const union power_supply_propval *val)
{
	int rc;

	smblib_dbg(chg, PR_MISC, "Set ship mode: %d!!\n", !!val->intval);

	rc = smblib_masked_write(chg, SHIP_MODE_REG, SHIP_MODE_EN_BIT,
			!!val->intval ? SHIP_MODE_EN_BIT : 0);
	if (rc < 0)
		dev_err(chg->dev, "Couldn't %s ship mode, rc=%d\n",
				!!val->intval ? "enable" : "disable", rc);

	return rc;
}

int smblib_reg_block_update(struct smb_charger *chg,
				struct reg_info *entry)
{
	int rc = 0;

	while (entry && entry->reg) {
		rc = smblib_read(chg, entry->reg, &entry->bak);
		if (rc < 0) {
			dev_err(chg->dev, "Error in reading %s rc=%d\n",
				entry->desc, rc);
			break;
		}
		entry->bak &= entry->mask;

		rc = smblib_masked_write(chg, entry->reg,
					 entry->mask, entry->val);
		if (rc < 0) {
			dev_err(chg->dev, "Error in writing %s rc=%d\n",
				entry->desc, rc);
			break;
		}
		entry++;
	}

	return rc;
}

int smblib_reg_block_restore(struct smb_charger *chg,
				struct reg_info *entry)
{
	int rc = 0;

	while (entry && entry->reg) {
		rc = smblib_masked_write(chg, entry->reg,
					 entry->mask, entry->bak);
		if (rc < 0) {
			dev_err(chg->dev, "Error in writing %s rc=%d\n",
				entry->desc, rc);
			break;
		}
		entry++;
	}

	return rc;
}

static struct reg_info cc2_detach_settings[] = {
	{
		.reg	= TYPE_C_CFG_2_REG,
		.mask	= TYPE_C_UFP_MODE_BIT | EN_TRY_SOURCE_MODE_BIT,
		.val	= TYPE_C_UFP_MODE_BIT,
		.desc	= "TYPE_C_CFG_2_REG",
	},
	{
		.reg	= TYPE_C_CFG_3_REG,
		.mask	= EN_TRYSINK_MODE_BIT,
		.val	= 0,
		.desc	= "TYPE_C_CFG_3_REG",
	},
	{
		.reg	= TAPER_TIMER_SEL_CFG_REG,
		.mask	= TYPEC_SPARE_CFG_BIT,
		.val	= TYPEC_SPARE_CFG_BIT,
		.desc	= "TAPER_TIMER_SEL_CFG_REG",
	},
	{
		.reg	= TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
		.mask	= VCONN_EN_ORIENTATION_BIT,
		.val	= 0,
		.desc	= "TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG",
	},
	{
		.reg	= MISC_CFG_REG,
		.mask	= TCC_DEBOUNCE_20MS_BIT,
		.val	= TCC_DEBOUNCE_20MS_BIT,
		.desc	= "Tccdebounce time"
	},
	{
	},
};

static int smblib_cc2_sink_removal_enter(struct smb_charger *chg)
{
	int rc, ccout, ufp_mode;
	u8 stat;

	if ((chg->wa_flags & TYPEC_CC2_REMOVAL_WA_BIT) == 0)
		return 0;

	if (chg->cc2_detach_wa_active)
		return 0;

	rc = smblib_read(chg, TYPE_C_STATUS_4_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read TYPE_C_STATUS_4 rc=%d\n", rc);
		return rc;
	}

	ccout = (stat & CC_ATTACHED_BIT) ?
					(!!(stat & CC_ORIENTATION_BIT) + 1) : 0;
	ufp_mode = (stat & TYPEC_DEBOUNCE_DONE_STATUS_BIT) ?
					!(stat & UFP_DFP_MODE_STATUS_BIT) : 0;

	if (ccout != 2)
		return 0;

	if (!ufp_mode)
		return 0;

	chg->cc2_detach_wa_active = true;
	/* The CC2 removal WA will cause a type-c-change IRQ storm */
	smblib_reg_block_update(chg, cc2_detach_settings);
	schedule_work(&chg->rdstd_cc2_detach_work);
	return rc;
}

static int smblib_cc2_sink_removal_exit(struct smb_charger *chg)
{
	if ((chg->wa_flags & TYPEC_CC2_REMOVAL_WA_BIT) == 0)
		return 0;

	if (!chg->cc2_detach_wa_active)
		return 0;

	chg->cc2_detach_wa_active = false;
	cancel_work_sync(&chg->rdstd_cc2_detach_work);
	smblib_reg_block_restore(chg, cc2_detach_settings);
	return 0;
}

int smblib_set_prop_pd_in_hard_reset(struct smb_charger *chg,
				const union power_supply_propval *val)
{
	int rc = 0;

	if (chg->pd_hard_reset == val->intval)
		return rc;

	chg->pd_hard_reset = val->intval;
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			EXIT_SNK_BASED_ON_CC_BIT,
			(chg->pd_hard_reset) ? EXIT_SNK_BASED_ON_CC_BIT : 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't set EXIT_SNK_BASED_ON_CC rc=%d\n",
				rc);

	vote(chg->apsd_disable_votable, PD_HARD_RESET_VOTER,
							chg->pd_hard_reset, 0);

	return rc;
}

static int smblib_recover_from_soft_jeita(struct smb_charger *chg)
{
	u8 stat_1, stat_2;
	int rc;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat_1);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
				rc);
		return rc;
	}

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_2_REG, &stat_2);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
				rc);
		return rc;
	}

	if ((chg->jeita_status && !(stat_2 & BAT_TEMP_STATUS_SOFT_LIMIT_MASK) &&
		((stat_1 & BATTERY_CHARGER_STATUS_MASK) == TERMINATE_CHARGE))) {
		/*
		 * We are moving from JEITA soft -> Normal and charging
		 * is terminated
		 */
		rc = smblib_write(chg, CHARGING_ENABLE_CMD_REG, 0);
		if (rc < 0) {
			smblib_err(chg, "Couldn't disable charging rc=%d\n",
						rc);
			return rc;
		}
		rc = smblib_write(chg, CHARGING_ENABLE_CMD_REG,
						CHARGING_ENABLE_CMD_BIT);
		if (rc < 0) {
			smblib_err(chg, "Couldn't enable charging rc=%d\n",
						rc);
			return rc;
		}
	}

	chg->jeita_status = stat_2 & BAT_TEMP_STATUS_SOFT_LIMIT_MASK;

	return 0;
}

/***********************
* USB MAIN PSY GETTERS *
*************************/
int smblib_get_prop_fcc_delta(struct smb_charger *chg,
				union power_supply_propval *val)
{
	int rc, jeita_cc_delta_ua = 0;

	rc = smblib_get_jeita_cc_delta(chg, &jeita_cc_delta_ua);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get jeita cc delta rc=%d\n", rc);
		jeita_cc_delta_ua = 0;
	}

	val->intval = jeita_cc_delta_ua;
	return 0;
}

/***********************
* USB MAIN PSY SETTERS *
*************************/
int smblib_get_charge_current(struct smb_charger *chg,
				int *total_current_ua)
{
	const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);
	union power_supply_propval val = {0, };
	int rc = 0, typec_source_rd, current_ua;
	bool non_compliant;
	u8 stat5;

	if (chg->pd_active) {
		*total_current_ua =
			get_client_vote_locked(chg->usb_icl_votable, PD_VOTER);
		return rc;
	}

	rc = smblib_read(chg, TYPE_C_STATUS_5_REG, &stat5);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read TYPE_C_STATUS_5 rc=%d\n", rc);
		return rc;
	}
	non_compliant = stat5 & TYPEC_NONCOMP_LEGACY_CABLE_STATUS_BIT;

	/* get settled ICL */
	rc = smblib_get_prop_input_current_settled(chg, &val);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get settled ICL rc=%d\n", rc);
		return rc;
	}

	typec_source_rd = smblib_get_prop_ufp_mode(chg);

	/* QC 2.0/3.0 adapter */
	if (apsd_result->bit & (QC_3P0_BIT | QC_2P0_BIT)) {
		*total_current_ua = HVDCP_CURRENT_UA;
		return 0;
	}

	if (non_compliant) {
		switch (apsd_result->bit) {
		case CDP_CHARGER_BIT:
			current_ua = CDP_CURRENT_UA;
			break;
		case DCP_CHARGER_BIT:
		case OCP_CHARGER_BIT:
		case FLOAT_CHARGER_BIT:
			current_ua = DCP_CURRENT_UA;
			break;
		default:
			current_ua = 0;
			break;
		}

		*total_current_ua = max(current_ua, val.intval);
		return 0;
	}

	switch (typec_source_rd) {
	case POWER_SUPPLY_TYPEC_SOURCE_DEFAULT:
		switch (apsd_result->bit) {
		case CDP_CHARGER_BIT:
			current_ua = CDP_CURRENT_UA;
			break;
		case DCP_CHARGER_BIT:
		case OCP_CHARGER_BIT:
		case FLOAT_CHARGER_BIT:
			current_ua = chg->default_icl_ua;
			break;
		default:
			current_ua = 0;
			break;
		}
		break;
	case POWER_SUPPLY_TYPEC_SOURCE_MEDIUM:
		current_ua = TYPEC_MEDIUM_CURRENT_UA;
		break;
	case POWER_SUPPLY_TYPEC_SOURCE_HIGH:
		current_ua = TYPEC_HIGH_CURRENT_UA;
		break;
	case POWER_SUPPLY_TYPEC_NON_COMPLIANT:
	case POWER_SUPPLY_TYPEC_NONE:
	default:
		current_ua = 0;
		break;
	}

	*total_current_ua = max(current_ua, val.intval);
	return 0;
}

/************************
 * ASUS GET POWER_SUPPLY DATA *
 ************************/
int asus_get_prop_batt_temp(struct smb_charger *chg)
{
	union power_supply_propval temp_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_temp(chg, &temp_val);

	return temp_val.intval;
}

int asus_get_prop_batt_volt(struct smb_charger *chg)
{
	union power_supply_propval volt_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_voltage_now(chg, &volt_val);

	return volt_val.intval;
}

int asus_get_prop_batt_capacity(struct smb_charger *chg)
{
	union power_supply_propval capacity_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_capacity(chg, &capacity_val);

	return capacity_val.intval;
}

int asus_get_prop_batt_health(struct smb_charger *chg)
{
	union power_supply_propval health_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_health(chg, &health_val);

	return health_val.intval;
}

int asus_get_prop_usb_present(struct smb_charger *chg)
{
	union power_supply_propval present_val = {0, };
	int rc;

	rc = smblib_get_prop_usb_present(chg, &present_val);

	return present_val.intval;
}

/************************
 * ASUS FG GET CHARGER PARAMATER NAME *
 ************************/
const char *asus_get_apsd_result(void)
{
	const struct apsd_result *apsd_result;

	apsd_result = smblib_get_apsd_result(smbchg_dev);
	return apsd_result->name;
}

int asus_get_ufp_mode(void)
{
	int ufp_mode;

	ufp_mode = smblib_get_prop_ufp_mode(smbchg_dev);
	if (ufp_mode == POWER_SUPPLY_TYPEC_NONE)
		return 0;
	else if (ufp_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
		return 1;
	else if (ufp_mode == POWER_SUPPLY_TYPEC_SOURCE_MEDIUM)
		return 2;
	else if (ufp_mode == POWER_SUPPLY_TYPEC_SOURCE_HIGH)
		return 3;
	else
		return 4;
}

int asus_get_batt_health(void)
{
	int bat_health;

	bat_health = asus_get_prop_batt_health(smbchg_dev);

	if (bat_health == POWER_SUPPLY_HEALTH_GOOD)
		return 0;
	else if (bat_health == POWER_SUPPLY_HEALTH_COLD) {
		//ASUSErclog(ASUS_JEITA_HARD_COLD, "JEITA Hard Cold is triggered");
		return 1;
	}
	else if (bat_health == POWER_SUPPLY_HEALTH_COOL)
		return 2;
	else if (bat_health == POWER_SUPPLY_HEALTH_WARM)
		return 3;
	else if (bat_health == POWER_SUPPLY_HEALTH_OVERHEAT) {
		//ASUSErclog(ASUS_JEITA_HARD_HOT, "JEITA Hard Hot is triggered");
		return 4;
	}
	else if (bat_health == POWER_SUPPLY_HEALTH_OVERVOLTAGE) {
		//ASUSErclog(ASUS_OUTPUT_OVP, "Battery OVP is triggered");
		return 5;
	}
	else
		return 6;
}

void asus_typec_removal_function(struct smb_charger *chg)
{
	int rc,val;

	rc = smblib_write(smbchg_dev, HVDCP_PULSE_COUNT_MAX, 0x54);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set HVDCP_PULSE_COUNT_MAX\n", __func__);

	rc = smblib_write(smbchg_dev, USBIN_OPTIONS_1_CFG_REG, 0x7D);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);

	val = gpio_get_value(global_gpio->ADC_SW_EN);
	if(val==1){
		rc = gpio_direction_output(global_gpio->ADC_SW_EN, 0);
		if (rc)
			CHG_DBG_E("%s: failed to pull-low ADC_SW_EN-gpios59\n", __func__);
		else
			CHG_DBG("%s: Pull low USBSW_S\n", __func__);
	} 
	else
		CHG_DBG("%s: get USBSW_S gpio val %d\n", __func__,val);
		
	val = gpio_get_value(global_gpio->ADCPWREN_PMI_GP1);
	if(val==1){
		rc = gpio_direction_output(global_gpio->ADCPWREN_PMI_GP1, 0);
		if (rc)
			CHG_DBG_E("%s: failed to pull-low ADCPWREN_PMI_GP1-gpios34\n", __func__);
		else
			CHG_DBG("%s: Pull low ADC_VH_EN\n", __func__);
	}
	else
		CHG_DBG("%s: get ADC_VH_EN gpio val %d\n", __func__,val);

	
	cancel_delayed_work(&chg->asus_chg_flow_work);
	cancel_delayed_work(&chg->asus_adapter_adc_work);
	cancel_delayed_work(&chg->asus_min_monitor_work);
	cancel_delayed_work(&chg->asus_qc3_soft_start_work);	
	cancel_delayed_work(&chg->asus_batt_RTC_work);
	cancel_delayed_work(&chg->asus_set_flow_flag_work);
	cancel_delayed_work(&chg->asus_rerun_DRP_work);//WA for 
	alarm_cancel(&bat_alarm);
	asus_flow_processing = 0;
	asus_CHG_TYPE = 0;
	ASUS_ADAPTER_ID = 0;
	HVDCP_FLAG = 0;
	UFP_FLAG = 0;
	asus_flow_done_flag = 0;
	if(g_CDP_WA)
		--g_CDP_WA;
	g_asus_QC3_WA = 0;
	g_asus_QC2_WA =0;
	g_non200k_QC3_WA =0;
	reset_soft_start_arg();
	g_legacy_check_cnt = 0;
	g_d2d_WA =0;
	g_PD_chg_icon =0;
	asus_smblib_relax(smbchg_dev);
	asus_smblib_SS_relax(smbchg_dev);

	focal_usb_detection(false);		//ASUS BSP Nancy : notify touch cable out +++
}

/************************
 * ASUS ADD BAT_ALARM *
 ************************/
static DEFINE_SPINLOCK(bat_alarm_slock);
static enum alarmtimer_restart batAlarm_handler(struct alarm *alarm, ktime_t now)
{
	CHG_DBG("%s: batAlarm triggered\n", __func__);
	return ALARMTIMER_NORESTART;
}
void asus_batt_RTC_work(struct work_struct *dat)
{
	unsigned long batflags;
	struct timespec new_batAlarm_time;
	struct timespec mtNow;
	int RTCSetInterval = 60;

	if (!smbchg_dev) {
		CHG_DBG("%s: driver not ready yet!\n", __func__);
		return;
	}

	if (!asus_get_prop_usb_present(smbchg_dev)) {
		alarm_cancel(&bat_alarm);
		CHG_DBG("%s: usb not present, cancel\n", __func__);
		return;
	}
	mtNow = current_kernel_time();
	new_batAlarm_time.tv_sec = 0;
	new_batAlarm_time.tv_nsec = 0;

	RTCSetInterval = 60;

	new_batAlarm_time.tv_sec = mtNow.tv_sec + RTCSetInterval;
	printk("[BAT][CHG] %s: alarm start after %ds\n", __FUNCTION__, RTCSetInterval);
	spin_lock_irqsave(&bat_alarm_slock, batflags);
	alarm_start(&bat_alarm, timespec_to_ktime(new_batAlarm_time));
	spin_unlock_irqrestore(&bat_alarm_slock, batflags);
}

/*+++ Add demo app read ADF function +++*/
#define ADF_PATH "/ADF/ADF"
static bool ADF_check_status(void)
{
    char buf[32];
	struct file *fd;
	struct inode *inode;
	off_t fsize;
	loff_t pos;
	mm_segment_t old_fs;

	fd = filp_open(ADF_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fd)) {
        CHG_DBG("%s: OPEN (%s) failed\n", __func__, ADF_PATH);
		return false;
    }

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	inode = fd->f_path.dentry->d_inode;
	fsize = inode->i_size;
	pos = 0;

	vfs_read(fd, buf, fsize, &pos);

	filp_close(fd, NULL);
	set_fs(old_fs);

	if (buf[3] == 1 || buf[3] == 2)
		return true;
	else
		return false;
}
/*--- Add demo app read ADF function--- */

/************************
 * ASUS CHARGER FLOW *
 ************************/

#define ICL_475mA	0x12
#define ICL_500mA	0x14
#define ICL_950mA	0x26
#define ICL_1000mA	0x28
#define ICL_1400mA	0x38
#define ICL_1425mA	0x39
#define ICL_1500mA	0x3C
#define ICL_1550mA	0x3E
#define ICL_1650mA	0x42
#define ICL_1900mA	0x4C
#define ICL_2000mA	0x50
#define ICL_2850mA	0x72
#define ICL_3000mA	0x78

#define ASUS_MONITOR_CYCLE		60000
#define ADC_WAIT_TIME_HVDCP0	15000
#define ADC_WAIT_TIME_HVDCP23	100

#define TITAN_750K_MIN	675
#define TITAN_750K_MAX	851
#define TITAN_200K_MIN	306
#define TITAN_200K_MAX	406
#define VADC_THD_300MV  300
#define VADC_THD_900MV  900
#define VADC_THD_1000MV  1000

//ASUS BSP Add per min monitor jeita & thermal & typeC_DFP +++
void smblib_asus_monitor_start(struct smb_charger *chg, int time)
{
	asus_flow_done_flag = 1;
	cancel_delayed_work(&chg->asus_min_monitor_work);
	schedule_delayed_work(&chg->asus_min_monitor_work, msecs_to_jiffies(time));
}

#define EN_BAT_CHG_EN_COMMAND_TRUE		0
#define EN_BAT_CHG_EN_COMMAND_FALSE 	BIT(0)
#define SMBCHG_FLOAT_VOLTAGE_VALUE_4P064		0x4D
#define SMBCHG_FLOAT_VOLTAGE_VALUE_4P357		0x74
#define SMBCHG_FAST_CHG_CURRENT_VALUE_850MA 	0x22
#define SMBCHG_FAST_CHG_CURRENT_VALUE_1475MA 	0x3B
#define SMBCHG_FAST_CHG_CURRENT_VALUE_1500MA 	0x3C
#define SMBCHG_FAST_CHG_CURRENT_VALUE_2050MA 	0x52
#define SMBCHG_FAST_CHG_CURRENT_VALUE_3000MA 	0x78

enum JEITA_state {
	JEITA_STATE_INITIAL,
	JEITA_STATE_LESS_THAN_0,
	JEITA_STATE_RANGE_0_to_100,
	JEITA_STATE_RANGE_100_to_200,
	JEITA_STATE_RANGE_200_to_500,
	JEITA_STATE_RANGE_500_to_600,
	JEITA_STATE_LARGER_THAN_600,
};

static int SW_recharge(struct smb_charger *chg)
{
	int capacity;
	u8 termination_reg;
	bool termination_done = 0;
	int rc;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &termination_reg);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't read BATTERY_CHARGER_STATUS_1_REG\n", __func__);
	if ((termination_reg & BATTERY_CHARGER_STATUS_MASK) == 0x05)
		termination_done = 1;

	capacity = asus_get_prop_batt_capacity(smbchg_dev);

	CHG_DBG("%s: capacity = %d, termination_reg = 0x%x\n", __func__, capacity, termination_reg);

	if (capacity <= 98 && termination_done) {
		rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
		if (rc < 0) {
			CHG_DBG_E("%s: Couldn't write charging_enable\n", __func__);
			return rc;
		}

		rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
		if (rc < 0) {
			CHG_DBG_E("%s: Couldn't write charging_enable\n", __func__);
			return rc;
		}
	}
	return 0;
}

int smbchg_jeita_judge_state(int old_State, int batt_tempr)
{
	int result_State;

	//decide value to set each reg (Vchg, Charging enable, Fast charge current)
	//batt_tempr < 0
	if (batt_tempr < 0) {
		result_State = JEITA_STATE_LESS_THAN_0;
	//0 <= batt_tempr < 10
	} else if (batt_tempr < 100) {
		result_State = JEITA_STATE_RANGE_0_to_100;
	//10 <= batt_tempr < 20
	} else if (batt_tempr < 200) {
		result_State = JEITA_STATE_RANGE_100_to_200;
	//20 <= batt_tempr < 50
	} else if (batt_tempr < 500) {
		result_State = JEITA_STATE_RANGE_200_to_500;
	//50 <= batt_tempr < 60
	} else if (batt_tempr < 600) {
		result_State = JEITA_STATE_RANGE_500_to_600;
	//60 <= batt_tempr
	} else{
		result_State = JEITA_STATE_LARGER_THAN_600;
	}

	//BSP david: do 3 degree hysteresis
	if (old_State == JEITA_STATE_LESS_THAN_0 && result_State == JEITA_STATE_RANGE_0_to_100) {
		if (batt_tempr <= 30) {
			result_State = old_State;
		}
	}
	if (old_State == JEITA_STATE_RANGE_0_to_100 && result_State == JEITA_STATE_RANGE_100_to_200) {
		if (batt_tempr <= 130) {
			result_State = old_State;
		}
	}
	if (old_State == JEITA_STATE_RANGE_100_to_200 && result_State == JEITA_STATE_RANGE_200_to_500) {
		if (batt_tempr <= 230) {
			result_State = old_State;
		}
	}
	if (old_State == JEITA_STATE_RANGE_500_to_600 && result_State == JEITA_STATE_RANGE_200_to_500) {
		if (batt_tempr >= 470) {
			result_State = old_State;
		}
	}
	if (old_State == JEITA_STATE_LARGER_THAN_600 && result_State == JEITA_STATE_RANGE_500_to_600) {
		if (batt_tempr >= 570) {
			result_State = old_State;
		}
	}
	return result_State;
}

static int jeita_status_regs_write(u8 chg_en, u8 FV_CFG, u8 FCC)
{
	int rc;

	rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG,
			CHARGING_ENABLE_CMD_BIT, chg_en);
	if (rc < 0) {
		printk("[BAT][CHG] Couldn't write charging_enable rc = %d\n", rc);
		return rc;
	}

	rc = smblib_masked_write(smbchg_dev, FLOAT_VOLTAGE_CFG_REG,
			FLOAT_VOLTAGE_SETTING_MASK, FV_CFG);
	if (rc < 0) {
		printk("[BAT][CHG] Couldn't write FV_CFG_reg_value rc = %d\n", rc);
		return rc;
	}

	rc = smblib_masked_write(smbchg_dev, FAST_CHARGE_CURRENT_CFG_REG,
			FAST_CHARGE_CURRENT_SETTING_MASK, FCC);
	if (rc < 0) {
		printk("[BAT][CHG] Couldn't write FCC_reg_value rc = %d\n", rc);
		return rc;
	}

	asus_smblib_rerun_aicl(smbchg_dev);

	return 0;
}

#define	DEMO_DISCHG_THD		60
#define	DEMO_CHG_THD			55
#define	DEMO_NON_CHG_THD	58
int demo_chg_status = DEMO_NON_CHG_THD;

//ASUS_BSP +++
#define CHGLimit_PATH "/cache/charger/CHGLimit"
static bool check_ultrabatterylife_enable(void)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	char buf[8] = "";
	int l_result = -1;

	fp = filp_open(CHGLimit_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fp)) {
		//CHG_DBG_E("%s: open (%s) fail\n", __func__, CHGLimit_PATH);
		return false;	/*No such file or directory*/
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_read(fp,buf,6,&pos_lsts);

	set_fs(old_fs);
	filp_close(fp, NULL);

	sscanf(buf, "%d", &l_result);
	CHG_DBG("%s: %d",__func__, l_result);
	
	if(l_result == 1){
		return true;
	}else{
		return false;
	}
}
//ASUS_BSP ---

bool high_power_pd(void){

	u8 ICL_reg = 0x14; 
	static int cnt=0;
	smblib_read(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG, &ICL_reg);


	if(g_PD_chg_icon && smbchg_dev->pd_active){
		if(g_d2d_WA){
			++cnt;
			if(cnt==1){
			CHG_DBG_EVT("PD active but not high power, icl 0x%x, usb type %d, cnt %d\n",
				ICL_reg,smbchg_dev->real_charger_type,g_PD_chg_icon);  // check status only
			}
			else if(cnt > 10){
			CHG_DBG_EVT("PD active but not high power, icl 0x%x, usb type %d, cnt %d\n",
				ICL_reg,smbchg_dev->real_charger_type,g_PD_chg_icon);  // check status only
				cnt =0;
			}
			return false;
		}
		else
			return true;

	}
	else 
		return false;

}

void update_ubatterylife_info(bool* dismiss){

	bool prev_ubat_flag = g_ubatterylife_enable_flag;
	g_ubatterylife_enable_flag = check_ultrabatterylife_enable(); 
	if(prev_ubat_flag == g_ubatterylife_enable_flag)
		return;

	// handle ubatterylife dismiss
	if(prev_ubat_flag){
		*dismiss = true;
		CHG_DBG_EVT("ultrabatterylife dismiss\n");

	}

	if(g_ubatterylife_enable_flag)
		CHG_DBG_EVT("ultrabatterylife triggered\n");
}

void jeita_rule(void)
{
	static int state = JEITA_STATE_INITIAL;
	int rc;
	int bat_volt;
	int bat_temp;
	int bat_health;
	int bat_capacity;
	u8 charging_enable;
	u8 FV_CFG_reg_value;
	u8 FCC_reg_value;
	u8 FV_reg;
	u8 ICL_reg;
	bool demo_app_state_flag = 0;
	bool demo_stop_charging_flag = 0;
	bool ubatlife_stop_charging_flag = 0;
	//bool suspend_dismiss to handle ubat_flag from 1 to 0
	bool suspend_dismiss =0;

	rc = smblib_write(smbchg_dev, JEITA_EN_CFG_REG, 0x10);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set JEITA_EN_CFG_REG\n", __func__);

	rc = smblib_read(smbchg_dev, FLOAT_VOLTAGE_CFG_REG, &FV_reg);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't read FLOAT_VOLTAGE_CFG_REG\n", __func__);

	rc = smblib_read(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG, &ICL_reg);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't read USBIN_CURRENT_LIMIT_CFG_REG\n", __func__);

	update_ubatterylife_info(&suspend_dismiss);
	bat_health = asus_get_batt_health();
	bat_temp = asus_get_prop_batt_temp(smbchg_dev);
	bat_volt = asus_get_prop_batt_volt(smbchg_dev);
	bat_capacity = asus_get_prop_batt_capacity(smbchg_dev);
	state = smbchg_jeita_judge_state(state, bat_temp);
	CHG_DBG("%s: batt_health = %s, temp = %d, volt = %d, ICL = 0x%x, COUT %d\n",
		__func__, health_type[bat_health], bat_temp, bat_volt, ICL_reg, BR_countrycode);

	switch (state) {
	case JEITA_STATE_LESS_THAN_0:
		charging_enable = EN_BAT_CHG_EN_COMMAND_FALSE;
		FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
		FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_850MA;
		CHG_DBG("%s: temperature < 0\n", __func__);
		break;
	case JEITA_STATE_RANGE_0_to_100:
		charging_enable = EN_BAT_CHG_EN_COMMAND_TRUE;
		FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
		FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_850MA;
		CHG_DBG("%s: 0 <= temperature < 10\n", __func__);
		rc = SW_recharge(smbchg_dev);
		if (rc < 0) {
			CHG_DBG_E("%s: SW_recharge failed rc = %d\n", __func__, rc);
		}
		break;
	case JEITA_STATE_RANGE_100_to_200:
		charging_enable = EN_BAT_CHG_EN_COMMAND_TRUE;
		FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
		FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_1475MA;
		CHG_DBG("%s: 10 <= temperature < 20\n", __func__);
		rc = SW_recharge(smbchg_dev);
		if (rc < 0) {
			CHG_DBG_E("%s: SW_recharge failed rc = %d\n", __func__, rc);
		}
		break;
	case JEITA_STATE_RANGE_200_to_500:
		charging_enable = EN_BAT_CHG_EN_COMMAND_TRUE;
		if (bat_volt <= 4200000) {
			FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
			FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_3000MA;
		} else {
			FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
			FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_1500MA;
		}
		CHG_DBG("%s: 20 <= temperature < 50\n", __func__);
		rc = SW_recharge(smbchg_dev);
		if (rc < 0) {
			CHG_DBG_E("%s: SW_recharge failed rc = %d\n", __func__, rc);
		}
		break;
	case JEITA_STATE_RANGE_500_to_600:
		if (bat_volt >= 4100000 && FV_reg == 0x74) {
			charging_enable = EN_BAT_CHG_EN_COMMAND_FALSE;
			FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
		} else {
			charging_enable = EN_BAT_CHG_EN_COMMAND_TRUE;
			FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P064;
		}
		FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_1475MA;
		CHG_DBG("%s: 50 <= temperature < 60\n", __func__);
		break;
	case JEITA_STATE_LARGER_THAN_600:
		charging_enable = EN_BAT_CHG_EN_COMMAND_FALSE;
		FV_CFG_reg_value = SMBCHG_FLOAT_VOLTAGE_VALUE_4P357;
		FCC_reg_value = SMBCHG_FAST_CHG_CURRENT_VALUE_1475MA;
		CHG_DBG("%s: temperature >= 60\n", __func__);
		break;
	}

	//WeiYu: WA for PD/QC3 current overflow
	if(smbchg_dev->pd_active && ICL_reg == ICL_1650mA
		&& charging_enable == EN_BAT_CHG_EN_COMMAND_TRUE){
		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, ICL_1550mA);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
		else
			CHG_DBG_WA("lower PD current from 0x%x to 0x%x\n",ICL_1650mA,ICL_1550mA);
		//asus_smblib_rerun_aicl(smbchg_dev); check if this is not needed here

	}

	if(g_asus_QC2_WA){
		if(ICL_reg != ICL_1000mA){
			rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
				USBIN_CURRENT_LIMIT_MASK, ICL_1000mA);
			CHG_DBG_WA("fix asus QC2 ICL: 0x%x to 0x%x\n", ICL_reg, ICL_1000mA);
		}
		g_asus_QC2_WA=0;
	}
	
	if(g_asus_QC3_WA){
		
		// WeiYu ++
		//For COS, due to MOS is replaced by soft start;
		if(g_Charger_mode && ICL_reg != QC3_200K_ICL){
			rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
				USBIN_CURRENT_LIMIT_MASK, QC3_200K_ICL);
			CHG_DBG_WA("fix asus QC3 ICL: 0x%x to 0x%x\n", ICL_reg, QC3_200K_ICL);
		}
		
		g_asus_QC3_WA=0;
	}

	if(g_non200k_QC3_WA){
		if(g_Charger_mode && ICL_reg != ICL_1400mA){
			rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
				USBIN_CURRENT_LIMIT_MASK, ICL_1400mA);
			CHG_DBG_WA("fix asus QC2 ICL: 0x%x to 0x%x\n", ICL_reg, ICL_1400mA);
		}
		g_non200k_QC3_WA=2;
	}
	
	if (demo_app_property_flag) {
		demo_app_state_flag = ADF_check_status();
		if(demo_app_state_flag){

			if(bat_capacity > DEMO_DISCHG_THD)
				demo_chg_status = DEMO_DISCHG_THD;				
			else if(bat_capacity >= DEMO_NON_CHG_THD)
				demo_chg_status = DEMO_NON_CHG_THD;
			// need not set status detween DEMO_CHG_THD and DEMO_NON_CHG_THD
			else if(bat_capacity < DEMO_CHG_THD)
				demo_chg_status = DEMO_CHG_THD;
			else;

			if(demo_chg_status == DEMO_DISCHG_THD)
				smblib_set_usb_suspend(smbchg_dev, true);
			else
				smblib_set_usb_suspend(smbchg_dev, false);

			if(demo_chg_status == DEMO_CHG_THD)
				demo_stop_charging_flag = false;
			else				
				demo_stop_charging_flag = true;
				
		}
	}else if (g_ubatterylife_enable_flag) {
		if(bat_capacity > UBATLIFE_DISCHG_THD)	// >60%
			ubatlife_chg_status = UBATLIFE_DISCHG_THD;
		else if(bat_capacity < UBATLIFE_CHG_THD)	// <58%
			ubatlife_chg_status = UBATLIFE_CHG_THD;
		else;
		
		if(ubatlife_chg_status == UBATLIFE_DISCHG_THD)
			smblib_set_usb_suspend(smbchg_dev, true);
		else
			smblib_set_usb_suspend(smbchg_dev, false);
		
		if(ubatlife_chg_status == UBATLIFE_CHG_THD)
			ubatlife_stop_charging_flag = false;
		else				
			ubatlife_stop_charging_flag = true;
	/* WeiYu: suspend_dismiss' priority is lower than the following list:
		demo_app_property_flag
		usb_alert_flag

	In the future,	if the list changes, NEED MORE LOGIC to before setting suspend_dismiss =1
	*/	
	}else if(suspend_dismiss){
		smblib_set_usb_suspend(smbchg_dev, false);
	}else;
	
//Add smart charge & demo app judgment +++
	if (smartchg_stop_flag || demo_stop_charging_flag || ubatlife_stop_charging_flag) {
		CHG_DBG_EVT("%s: Stop charging, smart = %d, demo = %d, ubat = %d\n", __func__, smartchg_stop_flag, demo_stop_charging_flag, ubatlife_stop_charging_flag);
		charging_enable = EN_BAT_CHG_EN_COMMAND_FALSE;
	}
//Add smart charge judgment ---

	rc = jeita_status_regs_write(charging_enable, FV_CFG_reg_value, FCC_reg_value);
	if (rc < 0)
		CHG_DBG("%s: Couldn't write jeita_status_register rc = %d\n", __func__, rc);
}

void update_inov_info(void){

	u8 val,thd_hot,thd_2hot;

	smblib_read(smbchg_dev, 0x4582, &val);
	smblib_read(smbchg_dev, 0x4586, &thd_hot);
	smblib_read(smbchg_dev, 0x4587, &thd_2hot);

	CHG_DBG("INOV info: raw:0x%x, thd(%d,%d), trigger(%d,%d)\n",
		val,(int)(thd_hot>>1)-30,(int)(thd_2hot>>1)-30, 
		val&BIT(4)?1:0, val&BIT(5)?1:0);

}

void reset_icl_for_nonstandard_ac(void){

	u8 set_icl = ICL_500mA;
	int rc;
	int sdp_like=0,dcp_like=0;
	const struct apsd_result *apsd_result;

	apsd_result = smblib_update_usb_type(smbchg_dev);

	switch (apsd_result->bit) {

	case SDP_CHARGER_BIT:
	case FLOAT_CHARGER_BIT:
		if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_3000mA;
		else if (UFP_FLAG == 2 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_1500mA;
		else
			set_icl = ICL_500mA;

		/*
		if(g_CDP_WA >= 2 ){
			if(g_Charger_mode)
				set_icl = ICL_500mA;
			else
				set_icl = ICL_1500mA;				
			CHG_DBG_WA("WA for CDP, icl: 0x%x, WA: %d\n",set_icl, g_CDP_WA);
			g_CDP_WA=0;
		}
		*/
		sdp_like =1;
		break;
	case CDP_CHARGER_BIT:
		if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_3000mA;
		else
			set_icl = ICL_1500mA;

		if(UFP_FLAG == 1 && g_Charger_mode)
			set_icl = ICL_500mA;
		sdp_like =1;
		break;
	case OCP_CHARGER_BIT:
		if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_3000mA;
		else if (UFP_FLAG == 2 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_1500mA;
		else
			set_icl = ICL_1000mA;
		sdp_like =1;
		break;

	case DCP_CHARGER_BIT | QC_3P0_BIT:
	case DCP_CHARGER_BIT | QC_2P0_BIT:
	case DCP_CHARGER_BIT:
		sdp_like = 0;
		dcp_like = 1;

	default:
		break;
	}

//-----

	if(dcp_like && !sdp_like){
	switch (ASUS_ADAPTER_ID) {
		case ASUS_750K:
			if (HVDCP_FLAG == 0) {
				asus_CHG_TYPE = ASUS_NORMAL_AC_ID;
				if (UFP_FLAG ==3 && !LEGACY_CABLE_FLAG)
					set_icl = ICL_3000mA;
				else
					set_icl = ICL_2000mA;
			} else {
				set_icl = ICL_1000mA;
			}
			break;
		case ASUS_200K:
			if (HVDCP_FLAG == 3) {
				asus_CHG_TYPE = ASUS_QC_AC_ID;
				set_icl = ICL_1900mA;
			} else if (HVDCP_FLAG == 0 && UFP_FLAG == 3 && !LEGACY_CABLE_FLAG) {
				set_icl = ICL_3000mA;
			} else if (HVDCP_FLAG == 0 && UFP_FLAG == 2 && !LEGACY_CABLE_FLAG) {
				set_icl = ICL_1500mA;
			} else {
				set_icl = ICL_1000mA;
			}
			break;
		case PB:
			if (HVDCP_FLAG == 0) {
				if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG)
					set_icl = ICL_3000mA;
				else
					set_icl = ICL_2000mA;
			} else {
				set_icl = ICL_1000mA;
			}
			break;
		case OTHERS:
			if(HVDCP_FLAG == 3)
				set_icl = ICL_1500mA;
			else if (HVDCP_FLAG == 0 && UFP_FLAG == 3 && !LEGACY_CABLE_FLAG)
				set_icl = ICL_3000mA;
			else if (HVDCP_FLAG == 0 && UFP_FLAG == 2 && !LEGACY_CABLE_FLAG)

				set_icl = ICL_1500mA;
			else if((BR_countrycode == COUNTRY_BR || BR_countrycode == COUNTRY_IN)
				&& HVDCP_FLAG == 0 && UFP_FLAG == 1)
				set_icl = ICL_2000mA;
			else
				set_icl = ICL_1000mA;
			break;
			/*
		case ADC_NOT_READY:
			set_icl = ICL_1000mA;
			*/
		default:	
			break;
	}

	}


	rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
		USBIN_CURRENT_LIMIT_MASK, set_icl);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
	asus_smblib_rerun_aicl(smbchg_dev);
	
	CHG_DBG_EVT("legacy check result: sdp_like:%d, dcp_like:%d, icl:0x%x\n",
		sdp_like,dcp_like,set_icl);

}


#define	LEGACY_CHECK_THD	3
#define	TYPE_C_1P5A_ICL_THD	0x3B
#define	TYPE_C_3A_ICL_THD		0x64

void check_legacy_for_nonstandard_ac(void){

	u8 stat=0,icl_ret=0;
	int ufp,icl_thd,prev_legacy;
	bool reset= false;

	if(++g_legacy_check_cnt > LEGACY_CHECK_THD){
		--g_legacy_check_cnt;
		//CHG_DBG_EVT("");
		return;
	}
	prev_legacy = LEGACY_CABLE_FLAG;
	
	ufp = asus_get_ufp_mode();	
	// 
	if(ufp!=2 && ufp!=3){
		g_legacy_check_cnt = LEGACY_CHECK_THD+1;
		CHG_DBG_EVT("end legacy check for AC is not in checking list\n");
		return;
	}

	if(ufp==2)
		icl_thd = TYPE_C_1P5A_ICL_THD;
	else
		icl_thd = TYPE_C_3A_ICL_THD;

	smblib_read(smbchg_dev, AICL_STATUS_REG, &stat);
	smblib_read(smbchg_dev, ICL_STATUS_REG, &icl_ret);
		
	if((stat&AICL_DONE_BIT) && (int)icl_ret <= icl_thd){
		reset = true;
		LEGACY_CABLE_FLAG = 8; //legacy type
		g_legacy_check_cnt = LEGACY_CHECK_THD+1;
		CHG_DBG_EVT("end legacy check for reseting ICL as legacy, prev:%d, now:%d\n",
			prev_legacy, LEGACY_CABLE_FLAG);
	}else if(g_legacy_check_cnt==LEGACY_CHECK_THD){
		CHG_DBG_EVT("end legacy check for checking legacy is done, no error\n");
	}else
		CHG_DBG_EVT("legacy check no error for round %d\n", g_legacy_check_cnt);

	if(!reset)
		return;

	reset_icl_for_nonstandard_ac();

	
	
}




void asus_qc3_soft_start_work(struct work_struct *work){

	int rc; 
	int done = false;
	g_SS_begin_icl += g_SS_icl_step;
	if(g_SS_begin_icl >= g_SS_end_icl){
		g_SS_begin_icl = g_SS_end_icl;
		done = true;
	}
	else if(g_SS_begin_icl < SOFT_START_ICL){
		done = true;
		CHG_DBG_EVT("bad start icl, terminate soft start\n");
	}else;

	// if(): for safty, it is dummy
	if(g_SS_begin_icl <= g_SS_end_icl && g_SS_begin_icl >= SOFT_START_ICL){
		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, g_SS_begin_icl);
		CHG_DBG_EVT("soft start set icl 0x%x, target icl:0x%x\n",g_SS_begin_icl,g_SS_end_icl);
	}
	
	if(!done)
		schedule_delayed_work(&smbchg_dev->asus_qc3_soft_start_work, msecs_to_jiffies(g_SS_delay_ms));
	else{
		g_SS_begin_icl = SOFT_START_ICL; //reset
		CHG_DBG_EVT("soft start is done\n");	
		asus_smblib_SS_relax(smbchg_dev);
	}
		

}

void asus_trigger_soft_start(u8 begin, u8 end, u8 step, int delay_ms){

	g_SS_begin_icl = begin;
	g_SS_end_icl = end;
	g_SS_icl_step = step;
	g_SS_delay_ms = delay_ms;
	CHG_DBG_EVT("begin:0x%x, end:0x%x, step:0x%x, delay_ms:0x%x\n",
		g_SS_begin_icl,g_SS_end_icl,g_SS_icl_step,g_SS_delay_ms);

	asus_smblib_SS_stay_awake(smbchg_dev);
	schedule_delayed_work(&smbchg_dev->asus_qc3_soft_start_work, msecs_to_jiffies(g_SS_delay_ms));

}

void reset_soft_start_arg(void){
	g_SS_begin_icl = SOFT_START_ICL;
	g_SS_end_icl = SOFT_START_END_ICL;
	g_SS_icl_step = SOFT_START_ICL_DELTA;
	g_SS_delay_ms = SOFT_START_DELAY_MS;
}
void asus_min_monitor_work(struct work_struct *work)
{
	int rc;

	if (!smbchg_dev) {
		CHG_DBG_E("%s: smbchg_dev is null due to driver probed isn't ready\n", __func__);
		return;
	}

	if (!asus_get_prop_usb_present(smbchg_dev)) {
		asus_typec_removal_function(smbchg_dev);
		return;
	}

	update_inov_info();
	check_legacy_for_nonstandard_ac();

	jeita_rule();

// Charger_limit_function for factory +++
if(ftm_mode && charger_limit_enable_flag){
	if (asus_get_prop_batt_capacity(smbchg_dev) >= charger_limit_value) {
		CHG_DBG("%s: charger limit is enable & over, stop charging\n", __func__);
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 1);
	} else {
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
	}
}
// Charger_limit_function for factory ---

	if (asus_get_prop_usb_present(smbchg_dev)) {
		last_jeita_time = current_kernel_time();
		schedule_delayed_work(&smbchg_dev->asus_min_monitor_work, msecs_to_jiffies(ASUS_MONITOR_CYCLE));
		schedule_delayed_work(&smbchg_dev->asus_batt_RTC_work, 0);
	}
	asus_smblib_relax(smbchg_dev);
}
//ASUS BSP Add per min monitor jeita & thermal & typeC_DFP ---

void asus_chg_flow_work(struct work_struct *work)
{
	const struct apsd_result *apsd_result;
	int rc;
	u8 set_icl;
	u8 legacy_cable_reg;	
	bool cc_attached;
	u8 stat;

	if (vbus_without_cc_flag) {
		rc = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &stat);
		cc_attached = (stat & CC_ATTACHED_BIT);
		CHG_DBG_EVT("%s: vbus_no_cc, cc_attached = %d\n", __func__, cc_attached);

			//WeiYu ++ unknown AC low current
			//remove "return;", let the flow go on.
			vbus_without_cc_flag = 0;
			smblib_update_usb_type(smbchg_dev);
			power_supply_changed(smbchg_dev->usb_psy);
		
	}

	if (!asus_get_prop_usb_present(smbchg_dev)) {
		asus_typec_removal_function(smbchg_dev);
		return;
	}

	apsd_result = smblib_update_usb_type(smbchg_dev);

	if(apsd_result->pst == POWER_SUPPLY_TYPE_USB){
		/*	WeiYu++
			g_d2d_WA: device to device(d2d) will be pd_active and USB type;
			However, it is not high power so shouldn't be "++"
			Let g_d2d_WA=1 to report d2d as normal icon.
		*/
		g_d2d_WA =1;

		//Not sure whether this supply change is necessary, keep it.
		//Maybe for "adb" related issue.
		power_supply_changed(smbchg_dev->usb_psy);
	}


	
	if (apsd_result->bit == (DCP_CHARGER_BIT | QC_3P0_BIT))
		HVDCP_FLAG = 3;
	else if (apsd_result->bit == (DCP_CHARGER_BIT | QC_2P0_BIT))
		HVDCP_FLAG = 2;
	else
		HVDCP_FLAG = 0;
	UFP_FLAG = asus_get_ufp_mode();

	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_5_REG, &legacy_cable_reg);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't read TYPE_C_STATUS_5_REG\n", __func__);
	LEGACY_CABLE_FLAG = legacy_cable_reg & TYPEC_LEGACY_CABLE_STATUS_BIT;

	CHG_DBG_EVT("%s: %s detected, typec mode = %s, LEGACY_CABLE_FLAG = %d\n", __func__, apsd_result->name,
		ufp_type[UFP_FLAG], LEGACY_CABLE_FLAG);

	if ((apsd_result->bit == 0) && (UFP_FLAG != 0)) {
		CHG_DBG("%s: APSD not ready yet, delay 1s\n", __func__);
		msleep(1000);
		apsd_result = smblib_update_usb_type(smbchg_dev);
		if (apsd_result->bit == (DCP_CHARGER_BIT | QC_3P0_BIT))
			HVDCP_FLAG = 3;
		else if (apsd_result->bit == (DCP_CHARGER_BIT | QC_2P0_BIT))
			HVDCP_FLAG = 2;
		else
			HVDCP_FLAG = 0;
		UFP_FLAG = asus_get_ufp_mode();
		CHG_DBG("%s: Retry %s detected, typec mode = %s\n", __func__, apsd_result->name, ufp_type[UFP_FLAG]);
	}

	if (smbchg_dev->pd_active) {
		CHG_DBG_EVT("%s: PD_active\n", __func__);
		//asus_smblib_rerun_aicl(smbchg_dev);
		/*	WeiYu ++ 
			g_PD_chg_icon:make sure charger-side ready then comes to reporting PD chg-icon;
			Otherwise, PD regardless high/low power will becomes icon "++"
		*/
		g_PD_chg_icon =1;
		asus_adapter_detecting_flag = 0;
		smblib_asus_monitor_start(smbchg_dev, DELAY_MONITOR_WORK_MS);		//ASUS BSP Austin_T: Jeita start
		return;
	}

	switch (apsd_result->bit) {

	case SDP_CHARGER_BIT:
	case FLOAT_CHARGER_BIT:
		if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG){
			set_icl = ICL_3000mA;
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;
		}
		else if (UFP_FLAG == 2 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_1500mA;
		else
			set_icl = ICL_500mA;

		//if COS apply WA only, add (&& !g_Charger_mode)
		if(g_CDP_WA >= 2 ){
			if(g_Charger_mode)
				set_icl = ICL_500mA;
			else
				set_icl = ICL_1500mA;				
			CHG_DBG_WA("WA for CDP, icl: 0x%x, WA: %d\n",set_icl, g_CDP_WA);
			g_CDP_WA=0;
		}

		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, set_icl);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
		asus_smblib_rerun_aicl(smbchg_dev);
		asus_adapter_detecting_flag = 0;
		smblib_asus_monitor_start(smbchg_dev, DELAY_MONITOR_WORK_MS);		//ASUS BSP Austin_T: Jeita start
		break;
	case CDP_CHARGER_BIT:
		if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG){
			set_icl = ICL_3000mA;
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;
		}
		else
			set_icl = ICL_1500mA;

		if(UFP_FLAG == 1 && g_Charger_mode)
			set_icl = ICL_500mA;

		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, set_icl);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
		asus_smblib_rerun_aicl(smbchg_dev);
		asus_adapter_detecting_flag = 0;
		smblib_asus_monitor_start(smbchg_dev, DELAY_MONITOR_WORK_MS);		//ASUS BSP Austin_T: Jeita start
		break;
	case OCP_CHARGER_BIT:
		if (UFP_FLAG == 3 && !LEGACY_CABLE_FLAG){
			set_icl = ICL_3000mA;
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;
		}
		else if (UFP_FLAG == 2 && !LEGACY_CABLE_FLAG)
			set_icl = ICL_1500mA;
		else
			set_icl = ICL_1000mA;

		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, set_icl);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
		asus_smblib_rerun_aicl(smbchg_dev);
		asus_adapter_detecting_flag = 0;
		smblib_asus_monitor_start(smbchg_dev, DELAY_MONITOR_WORK_MS);		//ASUS BSP Austin_T: Jeita start
		break;

	case DCP_CHARGER_BIT | QC_3P0_BIT:
	case DCP_CHARGER_BIT | QC_2P0_BIT:
	case DCP_CHARGER_BIT:
		rc = smblib_write(smbchg_dev, HVDCP_PULSE_COUNT_MAX, 0x0);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set HVDCP_PULSE_COUNT_MAX\n", __func__);

		rc = smblib_masked_write(smbchg_dev, USBIN_OPTIONS_1_CFG_REG, AUTO_SRC_DETECT_BIT | HVDCP_EN_BIT, 0x0);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);

		CHG_DBG("%s: Rerun APSD 1st\n", __func__);
		rc = smblib_masked_write(smbchg_dev, CMD_APSD_REG, APSD_RERUN_BIT, APSD_RERUN_BIT);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set CMD_APSD_REG\n", __func__);

		rc = gpio_direction_output(global_gpio->ADC_SW_EN, 1);	//USB DPDM Switch to ADC(2D)
		if (rc) {
			CHG_DBG_E("%s: failed to pull-high ADC_SW_EN-gpios59\n", __func__);
			break;
		} else {
			CHG_DBG("%s: Pull high USBSW_S\n", __func__);
		}

		if (HVDCP_FLAG == 0) {
			CHG_DBG("%s: NOT factory_build, HVDCP_FLAG = 0, ADC_WAIT_TIME = 15s\n", __func__);
			schedule_delayed_work(&smbchg_dev->asus_adapter_adc_work, msecs_to_jiffies(ADC_WAIT_TIME_HVDCP0));
		} else {
			CHG_DBG("%s: NOT factory_build, HVDCP_FLAG = 2or3, ADC_WAIT_TIME = 0.1s\n", __func__);
			schedule_delayed_work(&smbchg_dev->asus_adapter_adc_work, msecs_to_jiffies(ADC_WAIT_TIME_HVDCP23));
		}
		break;
	default:

		/* WeiYu ++ unknown AC low current
			let unknown AC ICL 500ma and  run asus_monitor also.
			This is 0x1370 part
		*/		
		set_icl = ICL_500mA;
		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, set_icl);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
		asus_smblib_rerun_aicl(smbchg_dev);
		asus_adapter_detecting_flag = 0;
		asus_flow_processing = 0;
		smblib_asus_monitor_start(smbchg_dev, DELAY_MONITOR_WORK_MS);	
		CHG_DBG_EVT("detect unknown AC, set ICL 0x%x\n",set_icl);
		break;
	}
}

extern int32_t get_ID_vadc_voltage(void);

static void CHG_TYPE_judge(struct smb_charger *chg)
{
	int adc_result;
	int ret;
	int MIN_750K, MAX_750K, MIN_200K, MAX_200K;

	MIN_750K = TITAN_750K_MIN;
	MAX_750K = TITAN_750K_MAX;
	MIN_200K = TITAN_200K_MIN;
	MAX_200K = TITAN_200K_MAX;

	// read charger ID via pm660 gpio3
	adc_result = get_ID_vadc_voltage();

	if (adc_result <= VADC_THD_300MV) {
		ret = gpio_direction_output(global_gpio->ADCPWREN_PMI_GP1, 1);
		if (ret) {
			CHG_DBG_E("%s: failed to pull-high ADCPWREN_PMI_GP1-gpios34\n", __func__);
		} else {
			CHG_DBG("%s: Pull high ADC_VH_EN\n", __func__);
		}
		msleep(5);

		adc_result = get_ID_vadc_voltage();

		if (adc_result >= VADC_THD_1000MV) {
			ASUS_ADAPTER_ID = OTHERS;
		} else {
			if (adc_result >= MIN_750K && adc_result <= MAX_750K)
				ASUS_ADAPTER_ID = ASUS_750K;
			else if (adc_result >= MIN_200K && adc_result <= MAX_200K)
				ASUS_ADAPTER_ID = ASUS_200K;
			else
				ASUS_ADAPTER_ID = OTHERS;
		}
	} else {
		if (adc_result >= VADC_THD_900MV)
			ASUS_ADAPTER_ID = PB;
		else
			ASUS_ADAPTER_ID = OTHERS;
	}
}

// refer to smblib_legacy_detection_work()
int asus_rerun_legacy(void){

	int rc;
	u8 legacy_cable_reg;
	
	mutex_lock(&smbchg_dev->lock);

	rc = smblib_masked_write(smbchg_dev, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
		TYPEC_DISABLE_CMD_BIT, TYPEC_DISABLE_CMD_BIT);

	msleep(500);

	rc = smblib_masked_write(smbchg_dev, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
		TYPEC_DISABLE_CMD_BIT, 0);

	msleep(100);
	
	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_5_REG, &legacy_cable_reg);
	if (rc < 0)
		CHG_DBG_E("%s: redo in charger mode but Couldn't read TYPE_C_STATUS_5_REG\n", __func__);
	LEGACY_CABLE_FLAG = legacy_cable_reg & TYPEC_LEGACY_CABLE_STATUS_BIT;
	CHG_DBG(" rerun legacy for QC2/3: %d\n",LEGACY_CABLE_FLAG);

	mutex_unlock(&smbchg_dev->lock);
	
	return 0;

}


void asus_adapter_adc_work(struct work_struct *work)
{
	int rc;
	u8 usb_max_current;
	u8 legacy_cable_reg;
	if (!asus_get_prop_usb_present(smbchg_dev)) {
		asus_typec_removal_function(smbchg_dev);
		return;
	}

	rc = smblib_set_usb_suspend(smbchg_dev, 1);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't set 1340_USBIN_SUSPEND_BIT 1\n", __func__);

	msleep(5);

	CHG_TYPE_judge(smbchg_dev);


	//WeiYu: WA for legacy wrong detection of 1.5/3A DCP type
	// remove this WA by adding 0&& due to vbus drop
	if(0&&(HVDCP_FLAG ==0)&&(UFP_FLAG ==3||UFP_FLAG ==2)){
		asus_rerun_legacy();
	}
	
	//determine current-setting value for DCP type AC:

//set_current:
	/* WeiYu ++ typeC 1.5A COS and car charger issue
		revise LEGACY_CABLE_FLAG no matter COS or MOS
		[concern] N/A
		[history] run 1.5A COS legacy WA and update LEGACY_CABLE_FLAG here
	*/
	//WeiYu ++ for 1.5A COS issue
	if((HVDCP_FLAG ==0)&&(UFP_FLAG ==3||UFP_FLAG ==2)){

		rc = smblib_read(smbchg_dev, TYPE_C_STATUS_5_REG, &legacy_cable_reg);
		if (rc < 0)
			CHG_DBG_E("%s: redo in charger mode but Couldn't read TYPE_C_STATUS_5_REG\n", __func__);
		LEGACY_CABLE_FLAG = legacy_cable_reg & TYPEC_LEGACY_CABLE_STATUS_BIT;
		CHG_DBG_EVT(" rerun legacy for QC2/3, result %d\n",LEGACY_CABLE_FLAG);

	}
	
	switch (ASUS_ADAPTER_ID) {
	case ASUS_750K:

		if(HVDCP_FLAG == 3){

			g_non200k_QC3_WA = 2; // make no WA in auth_done func and jeita
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;			
			usb_max_current = QC3_NON200K_ICL;	
			/*	WeiYu ++
				above is original flow, and we handle QC3 soft start here
				Soft Start works on MOS only, COS need not S.S.
			*/
			if(!g_Charger_mode){
			usb_max_current = ICL_500mA;			
			asus_trigger_soft_start(SOFT_START_ICL,QC3_NON200K_ICL,
				SOFT_START_ICL_DELTA,SOFT_START_DELAY_MS);	
			}
		}
		else if(HVDCP_FLAG == 2){
			usb_max_current = ICL_1000mA;
		}
		else if(HVDCP_FLAG == 0 && LEGACY_CABLE_FLAG){
				asus_CHG_TYPE = ASUS_NORMAL_AC_ID;				
				usb_max_current = ICL_2000mA;
		}
		else if(HVDCP_FLAG == 0 && !LEGACY_CABLE_FLAG && UFP_FLAG == 1){
				asus_CHG_TYPE = ASUS_NORMAL_AC_ID;				
				usb_max_current = ICL_2000mA;
		}
		else{
			usb_max_current = ICL_1000mA;
			CHG_DBG_E("No matching condition, set defualt icl: 0x%x\n",usb_max_current);

		}
		break;
	case ASUS_200K:

		if (HVDCP_FLAG == 3) {
			/*WeiYu ++ 
				Make sure asus_CHG_TYPE == ASUS_QC_AC_ID
				SO DOES QC3_200K_ICL INITIAL VALUE
			*/
			asus_CHG_TYPE = ASUS_QC_AC_ID;
			usb_max_current = QC3_200K_ICL;
			
			/*	WeiYu ++
				above is original flow, and we handle soft start here
			*/
			if(!g_Charger_mode){	
				usb_max_current = ICL_500mA;
				asus_trigger_soft_start(SOFT_START_ICL,QC3_200K_ICL,
					SOFT_START_ICL_DELTA,SOFT_START_DELAY_MS);
			}
		}
		/*
			WeiYu: While the following are same icl setting, we list the conditions
		*/
		else if (HVDCP_FLAG == 2){
			usb_max_current = ICL_1000mA;
		}
		else if(HVDCP_FLAG == 0 && LEGACY_CABLE_FLAG){
			usb_max_current = ICL_1000mA;
		}
		else if(HVDCP_FLAG == 0 && !LEGACY_CABLE_FLAG && UFP_FLAG == 1){
			usb_max_current = ICL_1000mA;
		}
		else{
			usb_max_current = ICL_1000mA;
			CHG_DBG_E("No matching condition, set defualt icl: 0x%x\n",usb_max_current);

		}
		break;
	case PB:

		if(HVDCP_FLAG == 3){


			g_non200k_QC3_WA = 2; // make no WA in auth_done func
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;			
			usb_max_current = QC3_NON200K_ICL;		
			/*	WeiYu ++
				above is original flow, and we handle QC3 soft start here
			*/
			if(!g_Charger_mode){	
				usb_max_current = ICL_500mA;			
				asus_trigger_soft_start(SOFT_START_ICL,QC3_NON200K_ICL,
					SOFT_START_ICL_DELTA,SOFT_START_DELAY_MS);	
			}
		}
		else if(HVDCP_FLAG == 2){
			usb_max_current = ICL_1000mA;
		}
		else if(HVDCP_FLAG == 0 && LEGACY_CABLE_FLAG){
				asus_CHG_TYPE = ASUS_NORMAL_AC_ID;				
				usb_max_current = ICL_2000mA;
		}
		else if(HVDCP_FLAG == 0 && !LEGACY_CABLE_FLAG && UFP_FLAG == 1){
				asus_CHG_TYPE = ASUS_NORMAL_AC_ID;				
				usb_max_current = ICL_2000mA;
		}
		else{
			usb_max_current = ICL_1000mA;
			CHG_DBG_E("No matching condition, set defualt icl: 0x%x\n",usb_max_current);

		}
		break;
	case OTHERS:

		if(HVDCP_FLAG == 3){

			g_non200k_QC3_WA = 2; // make no WA in auth_done func
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;			
			usb_max_current = QC3_NON200K_ICL;
			/*	WeiYu ++
				above is original flow, and we handle QC3 soft start here
			*/
			if(!g_Charger_mode){	
			usb_max_current = ICL_500mA;			
			asus_trigger_soft_start(SOFT_START_ICL,QC3_NON200K_ICL,
				SOFT_START_ICL_DELTA,SOFT_START_DELAY_MS);			
			}
		}
		else if(HVDCP_FLAG == 2){
			usb_max_current = ICL_1000mA;
		}
		else if (HVDCP_FLAG == 0 && UFP_FLAG == 3 && !LEGACY_CABLE_FLAG){
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;						
			usb_max_current = ICL_3000mA;
			/*	WeiYu ++
				above is original flow, and we handle soft start here
			*/
			if(!g_Charger_mode){	
				usb_max_current = ICL_500mA;			
				asus_trigger_soft_start(SOFT_START_ICL,ICL_3000mA,
					(SOFT_START_ICL_DELTA),SOFT_START_DELAY_MS);	
			}
		}
		else if (HVDCP_FLAG == 0 && UFP_FLAG == 2 && !LEGACY_CABLE_FLAG){
			usb_max_current = ICL_1500mA;
		}
		else if((BR_countrycode == COUNTRY_BR || BR_countrycode == COUNTRY_IN)
			&& HVDCP_FLAG == 0)
			usb_max_current = ICL_2000mA;
		else 
			usb_max_current = ICL_1000mA;
		break;
	case ADC_NOT_READY:
		usb_max_current = ICL_1000mA;
		break;
	}

	rc = smblib_set_usb_suspend(smbchg_dev, 0);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't set 1340_USBIN_SUSPEND_BIT 0\n", __func__);

	//Ara  close gpio in order +++
	rc = gpio_direction_output(global_gpio->ADCPWREN_PMI_GP1, 0);
	if (rc)
		CHG_DBG_E("%s: failed to pull-low ADCPWREN_PMI_GP1-gpios34\n", __func__);
	else
		CHG_DBG("%s: Pull low ADC_VH_EN\n", __func__);

	
	rc = gpio_direction_output(global_gpio->ADC_SW_EN, 0);
	if (rc)
		CHG_DBG_E("%s: failed to pull-low ADC_SW_EN-gpios59\n", __func__);
	else
		CHG_DBG("%s: Pull low USBSW_S\n", __func__);
	//Ara  close gpio in order ---

	//WeiYu ++ 18W modify from 0x4C to 0x54 for gps issue
	if (asus_CHG_TYPE == ASUS_QC_AC_ID) {
		CHG_DBG("%s: Write HVDCP_PULSE_COUNT_MAX = 54\n", __func__);
		rc = smblib_write(smbchg_dev, HVDCP_PULSE_COUNT_MAX, 0x54);
	} else {	
		CHG_DBG("%s: Write HVDCP_PULSE_COUNT_MAX = 54\n", __func__);
		rc = smblib_write(smbchg_dev, HVDCP_PULSE_COUNT_MAX, 0x54);
	}
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set HVDCP_PULSE_COUNT_MAX\n", __func__);

	rc = smblib_write(smbchg_dev, USBIN_OPTIONS_1_CFG_REG, 0x7D);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);


	// bsp WeiYu: WA for non-legacy 1.5A current fail on Titan O/Ara
	if(!(!LEGACY_CABLE_FLAG && UFP_FLAG==2)){
		
		CHG_DBG("%s: Rerun APSD 2nd\n", __func__);
		rc = smblib_masked_write(smbchg_dev, CMD_APSD_REG, APSD_RERUN_BIT, APSD_RERUN_BIT);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set CMD_APSD_REG\n", __func__);

		msleep(1000);
	}

//Set current:
	CHG_DBG_EVT("%s: ASUS_ADAPTER_ID = %s, setting mA = 0x%x\n", __func__, asus_id[ASUS_ADAPTER_ID], usb_max_current);

	//Set current:
	rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
		USBIN_CURRENT_LIMIT_MASK, usb_max_current);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);

	asus_smblib_rerun_aicl(smbchg_dev);

	asus_adapter_detecting_flag = 0;
	smblib_asus_monitor_start(smbchg_dev, DELAY_MONITOR_WORK_MS);		//ASUS BSP Austin_T: Jeita start

	// WeiYu: charger icon timing issue, intend to send switch uevent
	power_supply_changed(smbchg_dev->batt_psy);

}
//ASUS BSP : Add ASUS Adapter Detecting ---

void asus_insertion_initial_settings(struct smb_charger *chg)
{
	int rc;

	CHG_DBG("%s: start\n", __func__);
//No.1
	rc = smblib_write(chg, PRE_CHARGE_CURRENT_CFG_REG, 0x03);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default PRE_CHARGE_CURRENT_CFG_REG rc=%d\n", rc);
	}
//No.2
	rc = smblib_write(chg, FAST_CHARGE_CURRENT_CFG_REG, 0x3B);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FAST_CHARGE_CURRENT_CFG_REG rc=%d\n", rc);
	}
//No.3
	rc = smblib_write(chg, FLOAT_VOLTAGE_CFG_REG, 0x74);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FLOAT_VOLTAGE_CFG_REG rc=%d\n", rc);
	}
//No.4
	rc = smblib_masked_write(chg, FVC_RECHARGE_THRESHOLD_CFG_REG,
			FVC_RECHARGE_THRESHOLD_MASK, 0x58);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FVC_RECHARGE_THRESHOLD_CFG_REG rc=%d\n", rc);
	}
//No.5
	rc = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
			FVC_RECHARGE_THRESHOLD_MASK, 0x02);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FVC_RECHARGE_THRESHOLD_CFG_REG rc=%d\n", rc);
	}
//No.6
	rc = smblib_masked_write(chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, 0x03);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
	}
//No.7
	rc = smblib_masked_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
			USBIN_ADAPTER_ALLOW_MASK, 0x08);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default USBIN_ADAPTER_ALLOW_CFG_REG rc=%d\n", rc);
	}
//No.8
	rc = smblib_write(chg, CHGR_CFG2_REG, 0x40);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default CHGR_CFG2_REG rc=%d\n", rc);
	}
//No.9
	rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
			CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default CHARGING_ENABLE_CMD_REG rc=%d\n", rc);
	}
//No.10
	rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
			CHARGING_ENABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default CHARGING_ENABLE_CMD_REG rc=%d\n", rc);
	}
//No.11
	rc = smblib_masked_write(chg, VSYS_MIN_SEL_CFG_REG,
			VSYS_MIN_SEL_MASK, 0x02);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default VSYS_MIN_SEL_CFG_REG rc=%d\n", rc);
	}
//No.12
	rc = smblib_masked_write(chg, CMD_HVDCP_2_REG,
			FORCE_5V_BIT, FORCE_5V_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default CMD_HVDCP_2_REG rc=%d\n", rc);
	}
//No.13-1
	rc = smblib_masked_write(chg, HVDCP_PULSE_COUNT_MAX,
			HVDCP_PULSE_COUNT_MAX_QC2P0, 0x00);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default HVDCP_PULSE_COUNT_MAX rc=%d\n", rc);
	}
//No.13-2
	rc = smblib_masked_write(chg, HVDCP_PULSE_COUNT_MAX,
			HVDCP_PULSE_COUNT_MAX_QC3P0, 0x00);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default HVDCP_PULSE_COUNT_MAX rc=%d\n", rc);
	}
//No.14
	rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
			ICL_OVERRIDE_AFTER_APSD_BIT, ICL_OVERRIDE_AFTER_APSD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default USBIN_LOAD_CFG_REG rc=%d\n", rc);
	}
}

//[+++] WA for Type-C detection
void asus_rerun_DRP_work(struct work_struct *work)
{
	bool cc_attached;
	u8 stat;
	int rc;

	//[+++]Check the VBUS status againg before runing the work
	if (!asus_get_prop_usb_present(smbchg_dev)) {
		CHG_DBG_E("Try to run %s, but VBUS_IN is low\n", __func__);
		return;
	}
	//[---]Check the VBUS status againg before runing the work

	CHG_DBG_E("%s +++\n", __func__);
	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &stat);
	cc_attached = (stat & CC_ATTACHED_BIT);
	if (!cc_attached) {
		cancel_delayed_work(&smbchg_dev->asus_chg_flow_work);
		CHG_DBG_E("Run DFP -> DRP WA\n");
		rc = smblib_write(smbchg_dev, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, 0x32);
		msleep(100);
		rc = smblib_read(smbchg_dev, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, &stat);
		CHG_DBG_E("The 1st value of 0x1368 : 0x%x\n", stat);

		rc = smblib_write(smbchg_dev, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, 0x30);
		msleep(100);
		rc = smblib_read(smbchg_dev, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, &stat);
		CHG_DBG_E("The 2nd value of 0x1368 : 0x%x\n", stat);
	}
	schedule_delayed_work(&smbchg_dev->asus_chg_flow_work, msecs_to_jiffies(12000));
	CHG_DBG_E("%s ---\n", __func__);
}
//[---] WA for Type-C detection

void asus_set_flow_flag_work(struct work_struct *work)
{
	int rc;
	u8 stat;
	bool cc_attached;

	//[+++]Check the VBUS status againg before runing the work
	if (!asus_get_prop_usb_present(smbchg_dev)) {
		CHG_DBG_E("Try to run %s, but VBUS_IN is low\n", __func__);
		return;
	}
	//[---]Check the VBUS status againg before runing the work

	if (asus_flow_processing)
		asus_adapter_detecting_flag = 1;

	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &stat);
	cc_attached = stat & CC_ATTACHED_BIT;

	if (!cc_attached) {
		CHG_DBG("%s: vbus_without_cc_attached, fake DCP\n", __func__);
		vbus_without_cc_flag = 1;
		smblib_update_usb_type(smbchg_dev);
		power_supply_changed(smbchg_dev->usb_psy);
		//[+++] WA for Type-C detection
		CHG_DBG_E("Create the DRP WA\n");
		schedule_delayed_work(&smbchg_dev->asus_rerun_DRP_work, msecs_to_jiffies(3000));
		//[---] WA for Type-C detection
	}
}

/************************
 * PARALLEL PSY GETTERS *
 ************************/

int smblib_get_prop_slave_current_now(struct smb_charger *chg,
		union power_supply_propval *pval)
{
	if (IS_ERR_OR_NULL(chg->iio.batt_i_chan))
		chg->iio.batt_i_chan = iio_channel_get(chg->dev, "batt_i");

	if (IS_ERR(chg->iio.batt_i_chan))
		return PTR_ERR(chg->iio.batt_i_chan);

	return iio_read_channel_processed(chg->iio.batt_i_chan, &pval->intval);
}

/**********************
 * INTERRUPT HANDLERS *
 **********************/

irqreturn_t smblib_handle_debug(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_otg_overcurrent(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	int rc;
	u8 stat;

	rc = smblib_read(chg, OTG_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't read OTG_INT_RT_STS rc=%d\n", rc);
		return IRQ_HANDLED;
	}

	if (chg->wa_flags & OTG_WA) {
		if (stat & OTG_OC_DIS_SW_STS_RT_STS_BIT)
			smblib_err(chg, "OTG disabled by hw\n");

		/* not handling software based hiccups for PM660 */
		return IRQ_HANDLED;
	}

	if (stat & OTG_OVERCURRENT_RT_STS_BIT)
		schedule_work(&chg->otg_oc_work);

	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_chg_state_change(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	u8 stat;
	int rc;

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
				rc);
		return IRQ_HANDLED;
	}

	stat = stat & BATTERY_CHARGER_STATUS_MASK;
	power_supply_changed(chg->batt_psy);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_batt_temp_changed(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	int rc;

	rc = smblib_recover_from_soft_jeita(chg);
	if (rc < 0) {
		smblib_err(chg, "Couldn't recover chg from soft jeita rc=%d\n",
				rc);
		return IRQ_HANDLED;
	}

	rerun_election(chg->fcc_votable);
	power_supply_changed(chg->batt_psy);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_batt_psy_changed(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
	power_supply_changed(chg->batt_psy);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_usb_psy_changed(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
	power_supply_changed(chg->usb_psy);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_usbin_uv(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	struct storm_watch *wdata;

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
	if (!chg->irq_info[SWITCH_POWER_OK_IRQ].irq_data)
		return IRQ_HANDLED;

	wdata = &chg->irq_info[SWITCH_POWER_OK_IRQ].irq_data->storm_data;
	reset_storm_count(wdata);
	return IRQ_HANDLED;
}

static void smblib_micro_usb_plugin(struct smb_charger *chg, bool vbus_rising)
{
	if (vbus_rising) {
		/* use the typec flag even though its not typec */
		chg->typec_present = 1;
	} else {
		chg->typec_present = 0;
		smblib_update_usb_type(chg);
		extcon_set_cable_state_(chg->extcon, EXTCON_USB, false);
		smblib_uusb_removal(chg);
	}
}

void smblib_usb_plugin_hard_reset_locked(struct smb_charger *chg)
{
	int rc;
	u8 stat;
	bool vbus_rising;
	struct smb_irq_data *data;
	struct storm_watch *wdata;

	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read USB_INT_RT_STS rc=%d\n", rc);
		return;
	}

	vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);

	if (vbus_rising) {
		smblib_cc2_sink_removal_exit(chg);
	} else {
		smblib_cc2_sink_removal_enter(chg);
		if (chg->wa_flags & BOOST_BACK_WA) {
			data = chg->irq_info[SWITCH_POWER_OK_IRQ].irq_data;
			if (data) {
				wdata = &data->storm_data;
				update_storm_count(wdata,
						WEAK_CHG_STORM_COUNT);
				vote(chg->usb_icl_votable, BOOST_BACK_VOTER,
						false, 0);
				vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
						false, 0);
			}
		}
	}

	power_supply_changed(chg->usb_psy);
	smblib_dbg(chg, PR_INTERRUPT, "IRQ: usbin-plugin %s\n",
					vbus_rising ? "attached" : "detached");
}

#define PL_DELAY_MS			30000
void smblib_usb_plugin_locked(struct smb_charger *chg)
{
	int rc;
	u8 stat;
	bool vbus_rising;
	struct smb_irq_data *data;
	struct storm_watch *wdata;
	CHG_DBG_EVT("%s: start\n", __func__);

	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read USB_INT_RT_STS rc=%d\n", rc);
		return;
	}

	vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);
	smblib_set_opt_freq_buck(chg, vbus_rising ? chg->chg_freq.freq_5V :
						chg->chg_freq.freq_removal);

	if (vbus_rising) {
		rc = smblib_request_dpdm(chg, true);
		if (rc < 0)
			smblib_err(chg, "Couldn't to enable DPDM rc=%d\n", rc);

		/* Schedule work to enable parallel charger */
		vote(chg->awake_votable, PL_DELAY_VOTER, true, 0);
		schedule_delayed_work(&chg->pl_enable_work,
					msecs_to_jiffies(PL_DELAY_MS));
	} else {
		if (chg->wa_flags & BOOST_BACK_WA) {
			data = chg->irq_info[SWITCH_POWER_OK_IRQ].irq_data;
			if (data) {
				wdata = &data->storm_data;
				update_storm_count(wdata,
						WEAK_CHG_STORM_COUNT);
				vote(chg->usb_icl_votable, BOOST_BACK_VOTER,
						false, 0);
				vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
						false, 0);
			}
		}

		rc = smblib_request_dpdm(chg, false);
		if (rc < 0)
			smblib_err(chg, "Couldn't disable DPDM rc=%d\n", rc);
	}

//ASUS BSP Austin_T +++
	CHG_DBG_EVT("%s: start, vbus_rising = %d flow_processing %d\n", __func__, vbus_rising,asus_flow_processing);

	if (vbus_rising) {
		//rc = gpio_direction_output(global_gpio->USB_LID_EN, 0);
		//if (rc)
		//	CHG_DBG_E("%s: failed to pull-low USB_LID_EN\n", __func__);
		if (!asus_flow_processing) {
			asus_flow_processing = 1;
			schedule_delayed_work(&smbchg_dev->asus_set_flow_flag_work, msecs_to_jiffies(2000));
			asus_insertion_initial_settings(smbchg_dev);
			asus_smblib_stay_awake(smbchg_dev);
			if (g_Charger_mode)
				schedule_delayed_work(&smbchg_dev->asus_chg_flow_work, msecs_to_jiffies(12500));
			else
				schedule_delayed_work(&smbchg_dev->asus_chg_flow_work, msecs_to_jiffies(12000));
		}
		focal_usb_detection(true);	//ASUS BSP Nancy : notify touch cable in +++
	} else {
		//rc = gpio_direction_output(global_gpio->USB_LID_EN, 1);
		//if (rc)
		//	CHG_DBG_E("%s: failed to pull-high USB_LID_EN\n", __func__);
		asus_flow_processing = 0;		
		vbus_without_cc_flag = 0;
		asus_adapter_detecting_flag = 0;
		asus_typec_removal_function(chg);
	}
//ASUS BSP Austin_T ---
	if (chg->micro_usb_mode)
		smblib_micro_usb_plugin(chg, vbus_rising);

	power_supply_changed(chg->usb_psy);
	//Do here, avoid to be overrided by other function
	if (!vbus_rising) {
		//[+++]Write the ICL to the default 500mA(0x14)
		rc = smblib_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG, 0x14);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't set default USBIN_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
		}
		//[---]Write the ICL to the default 500mA(0x14)

		//[+++]Write to USB_100_Mode
		rc = smblib_write(smbchg_dev, USBIN_ICL_OPTIONS_REG, 0x02);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't set default USBIN_ICL_OPTIONS_REG rc=%d\n", rc);
		}
		//[---]Write to USB_100_Mode
	}
	smblib_dbg(chg, PR_INTERRUPT, "IRQ: usbin-plugin %s\n",
					vbus_rising ? "attached" : "detached");
}

irqreturn_t smblib_handle_usb_plugin(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	mutex_lock(&chg->lock);
	//if (chg->pd_hard_reset)
	//	smblib_usb_plugin_hard_reset_locked(chg);
	//else
		smblib_usb_plugin_locked(chg);
	mutex_unlock(&chg->lock);
	return IRQ_HANDLED;
}

#define USB_WEAK_INPUT_UA	1400000
#define ICL_CHANGE_DELAY_MS	1000
irqreturn_t smblib_handle_icl_change(int irq, void *data)
{
	u8 stat;
	int rc, settled_ua, delay = ICL_CHANGE_DELAY_MS;
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	static int cnt=0;


	if(chg->pd_active && asus_adapter_detecting_flag){
		if(++cnt >=10){
			CHG_DBG_EVT("skip PD icl change 10 times\n");
			cnt=0;
		}
		return IRQ_HANDLED;
	}
	
	if (chg->mode == PARALLEL_MASTER) {
		rc = smblib_read(chg, AICL_STATUS_REG, &stat);
		if (rc < 0) {
			smblib_err(chg, "Couldn't read AICL_STATUS rc=%d\n",
					rc);
			return IRQ_HANDLED;
		}

		rc = smblib_get_charge_param(chg, &chg->param.icl_stat,
				&settled_ua);
		if (rc < 0) {
			smblib_err(chg, "Couldn't get ICL status rc=%d\n", rc);
			return IRQ_HANDLED;
		}

		/* If AICL settled then schedule work now */
		if ((settled_ua == get_effective_result(chg->usb_icl_votable))
				|| (stat & AICL_DONE_BIT))
			delay = 0;

		cancel_delayed_work_sync(&chg->icl_change_work);
		schedule_delayed_work(&chg->icl_change_work,
						msecs_to_jiffies(delay));
	}

	return IRQ_HANDLED;
}

static void smblib_handle_slow_plugin_timeout(struct smb_charger *chg,
					      bool rising)
{
	CHG_DBG("%s: IRQ: slow-plugin-timeout %s\n", __func__, rising ? "rising" : "falling");

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: slow-plugin-timeout %s\n",
		   rising ? "rising" : "falling");
}

static void smblib_handle_sdp_enumeration_done(struct smb_charger *chg,
					       bool rising)
{
	smblib_dbg(chg, PR_INTERRUPT, "IRQ: sdp-enumeration-done %s\n",
		   rising ? "rising" : "falling");
}

#define QC3_PULSES_FOR_6V	5
#define QC3_PULSES_FOR_9V	20
#define QC3_PULSES_FOR_12V	35
static void smblib_hvdcp_adaptive_voltage_change(struct smb_charger *chg)
{
	int rc;
	u8 stat;
	int pulses;

	power_supply_changed(chg->usb_main_psy);
	if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP) {
		rc = smblib_read(chg, QC_CHANGE_STATUS_REG, &stat);
		if (rc < 0) {
			smblib_err(chg,
				"Couldn't read QC_CHANGE_STATUS rc=%d\n", rc);
			return;
		}

		switch (stat & QC_2P0_STATUS_MASK) {
		case QC_5V_BIT:
			smblib_set_opt_freq_buck(chg,
					chg->chg_freq.freq_5V);
			break;
		case QC_9V_BIT:
			smblib_set_opt_freq_buck(chg,
					chg->chg_freq.freq_9V);
			break;
		case QC_12V_BIT:
			smblib_set_opt_freq_buck(chg,
					chg->chg_freq.freq_12V);
			break;
		default:
			smblib_set_opt_freq_buck(chg,
					chg->chg_freq.freq_removal);
			break;
		}
	}

	if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP_3) {
		rc = smblib_get_pulse_cnt(chg, &pulses);
		if (rc < 0) {
			smblib_err(chg,
				"Couldn't read QC_PULSE_COUNT rc=%d\n", rc);
			return;
		}

		if (pulses < QC3_PULSES_FOR_6V)
			smblib_set_opt_freq_buck(chg,
				chg->chg_freq.freq_5V);
		else if (pulses < QC3_PULSES_FOR_9V)
			smblib_set_opt_freq_buck(chg,
				chg->chg_freq.freq_6V_8V);
		else if (pulses < QC3_PULSES_FOR_12V)
			smblib_set_opt_freq_buck(chg,
				chg->chg_freq.freq_9V);
		else
			smblib_set_opt_freq_buck(chg,
				chg->chg_freq.freq_12V);
	}
}

/* triggers when HVDCP 3.0 authentication has finished */
static void smblib_handle_hvdcp_3p0_auth_done(struct smb_charger *chg,
					      bool rising)
{
	const struct apsd_result *apsd_result;
	int rc;

	CHG_DBG("%s: start\n", __func__);

	if (!rising)
		return;

	if (chg->wa_flags & QC_AUTH_INTERRUPT_WA_BIT) {
		/*
		 * Disable AUTH_IRQ_EN_CFG_BIT to receive adapter voltage
		 * change interrupt.
		 */
		rc = smblib_masked_write(chg,
				USBIN_SOURCE_CHANGE_INTRPT_ENB_REG,
				AUTH_IRQ_EN_CFG_BIT, 0);
		if (rc < 0)
			smblib_err(chg,
				"Couldn't enable QC auth setting rc=%d\n", rc);
	}

	if (chg->mode == PARALLEL_MASTER)
		vote(chg->pl_enable_votable_indirect, USBIN_V_VOTER, true, 0);

	/* the APSD done handler will set the USB supply type */
	apsd_result = smblib_get_apsd_result(chg);
	if (get_effective_result(chg->hvdcp_hw_inov_dis_votable)) {
		if (apsd_result->pst == POWER_SUPPLY_TYPE_USB_HVDCP) {
			/* force HVDCP2 to 9V if INOV is disabled */
			rc = smblib_masked_write(chg, CMD_HVDCP_2_REG,
					FORCE_9V_BIT, FORCE_9V_BIT);
			if (rc < 0)
				smblib_err(chg,
					"Couldn't force 9V HVDCP rc=%d\n", rc);
		}
	}

	if(ASUS_ADAPTER_ID == OTHERS && apsd_result->pst ==POWER_SUPPLY_TYPE_USB_HVDCP){
		g_asus_QC2_WA = 1;
		CHG_DBG_WA("WA for QC2 ICL error\n");
	}
	else if(g_asus_QC2_WA == 1){
		g_asus_QC2_WA =0;
		CHG_DBG_WA("prev and now HCDCP detection not align\n");
	}

	if((ASUS_ADAPTER_ID == ASUS_750K||ASUS_ADAPTER_ID == OTHERS ||
		ASUS_ADAPTER_ID == PB) && 
		apsd_result->pst ==POWER_SUPPLY_TYPE_USB_HVDCP_3){
		/*
			We set g_non200k_QC3_WA=2 if it has been QC3 instead of QC2
		*/
		if(g_non200k_QC3_WA ==0){
			asus_CHG_TYPE = ASUS_ABOVE_13P5W_ID;	
			g_non200k_QC3_WA =1;
			CHG_DBG_WA("WA for non200K QC3 ICL error\n");	
			rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
				USBIN_CURRENT_LIMIT_MASK, ICL_500mA);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);			
			if(!g_Charger_mode){
			asus_trigger_soft_start(SOFT_START_ICL,ICL_1400mA,
				SOFT_START_ICL_DELTA,SOFT_START_DELAY_MS);	
			}
			
		}
	}
	else if(g_non200k_QC3_WA == 1){
		g_non200k_QC3_WA =0;
		CHG_DBG_WA("prev and now HCDCP detection not align for non200K QC3 WA\n");
	}
	
	if (ASUS_ADAPTER_ID == ASUS_200K && asus_CHG_TYPE != ASUS_QC_AC_ID
		&& apsd_result->pst ==POWER_SUPPLY_TYPE_USB_HVDCP_3 ) {

		CHG_DBG_EVT("%s: Fix HVDCP3 sign from ASUS_200K\n", __func__);
		asus_CHG_TYPE = ASUS_QC_AC_ID; //make thks if to be run once only;
		g_asus_QC3_WA = 1;
		CHG_DBG_EVT("%s: Write HVDCP_PULSE_COUNT_MAX = 54\n", __func__);
		rc = smblib_write(smbchg_dev, HVDCP_PULSE_COUNT_MAX, 0x54);		
		rc = smblib_masked_write(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG,
			USBIN_CURRENT_LIMIT_MASK, ICL_500mA);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
		
		if(!g_Charger_mode){	
		asus_trigger_soft_start(SOFT_START_ICL,QC3_200K_ICL,
			SOFT_START_ICL_DELTA,SOFT_START_DELAY_MS);
		}
		//schedule_delayed_work(&smbchg_dev->asus_qc3_soft_start_work, msecs_to_jiffies(SOFT_START_DELAY_MS));		
		asus_smblib_rerun_aicl(smbchg_dev);
		power_supply_changed(chg->batt_psy);
	}
	
	smblib_dbg(chg, PR_INTERRUPT, "IRQ: hvdcp-3p0-auth-done rising; %s detected\n",
		   apsd_result->name);
	CHG_DBG("%s: start, IRQ: hvdcp-3p0-auth-done rising; %s detected\n", __func__, apsd_result->name);
}

static void smblib_handle_hvdcp_check_timeout(struct smb_charger *chg,
					      bool rising, bool qc_charger)
{
	const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);

	/* Hold off PD only until hvdcp 2.0 detection timeout */
	if (rising) {
		vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
								false, 0);

		/* enable HDC and ICL irq for QC2/3 charger */
		if (qc_charger)
			vote(chg->usb_irq_enable_votable, QC_VOTER, true, 0);

		/*
		 * HVDCP detection timeout done
		 * If adapter is not QC2.0/QC3.0 - it is a plain old DCP.
		 */
		if (!qc_charger && (apsd_result->bit & DCP_CHARGER_BIT))
			/* enforce DCP ICL if specified */
			vote(chg->usb_icl_votable, DCP_VOTER,
				chg->dcp_icl_ua != -EINVAL, chg->dcp_icl_ua);
	}

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: smblib_handle_hvdcp_check_timeout %s\n",
		   rising ? "rising" : "falling");
}

/* triggers when HVDCP is detected */
static void smblib_handle_hvdcp_detect_done(struct smb_charger *chg,
					    bool rising)
{
	if (!rising)
		return;

	/* the APSD done handler will set the USB supply type */
	cancel_delayed_work_sync(&chg->hvdcp_detect_work);
	smblib_dbg(chg, PR_INTERRUPT, "IRQ: hvdcp-detect-done %s\n",
		   rising ? "rising" : "falling");
	CHG_DBG("%s: start, IRQ: hvdcp-detect-done %s\n", __func__, rising ? "rising" : "falling");
}

static void smblib_force_legacy_icl(struct smb_charger *chg, int pst)
{
	int typec_mode;
	int rp_ua;
	CHG_DBG("%s: pst = %d, pd_active = %d\n", __func__, pst, chg->pd_active);

	/* while PD is active it should have complete ICL control */
	if (chg->pd_active)
		return;

	switch (pst) {
	case POWER_SUPPLY_TYPE_USB:
		/*
		 * USB_PSY will vote to increase the current to 500/900mA once
		 * enumeration is done. Ensure that USB_PSY has at least voted
		 * for 100mA before releasing the LEGACY_UNKNOWN vote
		 */
		if (!is_client_vote_enabled(chg->usb_icl_votable,
								USB_PSY_VOTER))
			vote(chg->usb_icl_votable, USB_PSY_VOTER, true, 100000);
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, false, 0);
		break;
	case POWER_SUPPLY_TYPE_USB_CDP:
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, 1000000);
		break;
	case POWER_SUPPLY_TYPE_USB_DCP:
		typec_mode = smblib_get_prop_typec_mode(chg);
		rp_ua = get_rp_based_dcp_current(chg, typec_mode);
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, rp_ua);
		break;
	case POWER_SUPPLY_TYPE_USB_FLOAT:
		/*
		 * limit ICL to 100mA, the USB driver will enumerate to check
		 * if this is a SDP and appropriately set the current
		 */
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, 100000);
		break;
	case POWER_SUPPLY_TYPE_USB_HVDCP:
	case POWER_SUPPLY_TYPE_USB_HVDCP_3:
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, 1000000);
		break;
	default:
		smblib_err(chg, "Unknown APSD %d; forcing 500mA\n", pst);
		vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, 500000);
		break;
	}
}

static void smblib_notify_extcon_props(struct smb_charger *chg)
{
	union power_supply_propval val;

	smblib_get_prop_typec_cc_orientation(chg, &val);
	extcon_set_cable_state_(chg->extcon, EXTCON_USB_CC,
					(val.intval == 2) ? 1 : 0);
	extcon_set_cable_state_(chg->extcon, EXTCON_USB_SPEED, true);
}

static void smblib_notify_device_mode(struct smb_charger *chg, bool enable)
{
	if (enable)
		smblib_notify_extcon_props(chg);

	extcon_set_cable_state_(chg->extcon, EXTCON_USB, enable);
}

static void smblib_notify_usb_host(struct smb_charger *chg, bool enable)
{
	if (enable)
		smblib_notify_extcon_props(chg);

	extcon_set_cable_state_(chg->extcon, EXTCON_USB_HOST, enable);
}

#define HVDCP_DET_MS 2500
static void smblib_handle_apsd_done(struct smb_charger *chg, bool rising)
{
	const struct apsd_result *apsd_result;

	if (!rising)
		return;

	apsd_result = smblib_update_usb_type(chg);

	CHG_DBG("%s: apsd_result = 0x%x, typec_legacy_valid = %d, asus_flow_done_flag = %d\n", __func__, 
			apsd_result->bit, chg->typec_legacy_valid, asus_flow_done_flag);

	if (!chg->typec_legacy_valid && !asus_flow_done_flag)
		smblib_force_legacy_icl(chg, apsd_result->pst);

	switch (apsd_result->bit) {
	case SDP_CHARGER_BIT:
	case CDP_CHARGER_BIT:
		if (chg->micro_usb_mode)
			extcon_set_cable_state_(chg->extcon, EXTCON_USB,
					true);
		if (chg->use_extcon)
			smblib_notify_device_mode(chg, true);
	case OCP_CHARGER_BIT:
	case FLOAT_CHARGER_BIT:
		/* if not DCP then no hvdcp timeout happens, Enable pd here. */
		vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
				false, 0);
		break;
	case DCP_CHARGER_BIT:
		if (chg->wa_flags & QC_CHARGER_DETECTION_WA_BIT)
			schedule_delayed_work(&chg->hvdcp_detect_work,
					      msecs_to_jiffies(HVDCP_DET_MS));
		break;
	default:
		break;
	}

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: apsd-done rising; %s detected\n",
		   apsd_result->name);
}

irqreturn_t smblib_handle_usb_source_change(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	int rc = 0;
	u8 stat;

	if(chg->pd_active && asus_adapter_detecting_flag){
		CHG_DBG("skip PD source change\n");
		return IRQ_HANDLED;
	}


	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
		return IRQ_HANDLED;
	}
	smblib_dbg(chg, PR_REGISTER, "APSD_STATUS = 0x%02x\n", stat);
	CHG_DBG("%s: APSD_STATUS = 0x%x\n", __func__, stat);

	if (chg->micro_usb_mode && (stat & APSD_DTC_STATUS_DONE_BIT)
			&& !chg->uusb_apsd_rerun_done) {
		/*
		 * Force re-run APSD to handle slow insertion related
		 * charger-mis-detection.
		 */
		chg->uusb_apsd_rerun_done = true;
		smblib_rerun_apsd(chg);
		return IRQ_HANDLED;
	}

	smblib_handle_apsd_done(chg,
		(bool)(stat & APSD_DTC_STATUS_DONE_BIT));

	smblib_handle_hvdcp_detect_done(chg,
		(bool)(stat & QC_CHARGER_BIT));

	smblib_handle_hvdcp_check_timeout(chg,
		(bool)(stat & HVDCP_CHECK_TIMEOUT_BIT),
		(bool)(stat & QC_CHARGER_BIT));

	smblib_handle_hvdcp_3p0_auth_done(chg,
		(bool)(stat & QC_AUTH_DONE_STATUS_BIT));

	smblib_handle_sdp_enumeration_done(chg,
		(bool)(stat & ENUMERATION_DONE_BIT));

	smblib_handle_slow_plugin_timeout(chg,
		(bool)(stat & SLOW_PLUGIN_TIMEOUT_BIT));

	smblib_hvdcp_adaptive_voltage_change(chg);

	power_supply_changed(chg->usb_psy);

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
		return IRQ_HANDLED;
	}
	smblib_dbg(chg, PR_REGISTER, "APSD_STATUS = 0x%02x\n", stat);

	return IRQ_HANDLED;
}

static void typec_sink_insertion(struct smb_charger *chg)
{
	/* when a sink is inserted we should not wait on hvdcp timeout to
	 * enable pd
	 */
	vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
			false, 0);
	if (chg->use_extcon) {
		smblib_notify_usb_host(chg, true);
		chg->otg_present = true;
	}
}

static void typec_sink_removal(struct smb_charger *chg)
{
	smblib_set_charge_param(chg, &chg->param.freq_boost,
			chg->chg_freq.freq_above_otg_threshold);
	chg->boost_current_ua = 0;
}

static void smblib_handle_typec_removal(struct smb_charger *chg)
{
	int rc;
	struct smb_irq_data *data;
	struct storm_watch *wdata;
	CHG_DBG("%s: start\n", __func__);

	chg->cc2_detach_wa_active = false;

	rc = smblib_request_dpdm(chg, false);
	if (rc < 0)
		smblib_err(chg, "Couldn't disable DPDM rc=%d\n", rc);

	if (chg->wa_flags & BOOST_BACK_WA) {
		data = chg->irq_info[SWITCH_POWER_OK_IRQ].irq_data;
		if (data) {
			wdata = &data->storm_data;
			update_storm_count(wdata, WEAK_CHG_STORM_COUNT);
			vote(chg->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
			vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
					false, 0);
		}
	}

	/* reset APSD voters */
	vote(chg->apsd_disable_votable, PD_HARD_RESET_VOTER, false, 0);
	vote(chg->apsd_disable_votable, PD_VOTER, false, 0);

	cancel_delayed_work_sync(&chg->pl_enable_work);
	cancel_delayed_work_sync(&chg->hvdcp_detect_work);

	/* reset input current limit voters */
	vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, 100000);
	vote(chg->usb_icl_votable, PD_VOTER, false, 0);
	vote(chg->usb_icl_votable, USB_PSY_VOTER, false, 0);
	vote(chg->usb_icl_votable, DCP_VOTER, false, 0);
	vote(chg->usb_icl_votable, PL_USBIN_USBIN_VOTER, false, 0);
	vote(chg->usb_icl_votable, SW_QC3_VOTER, false, 0);

	/* reset hvdcp voters */
	vote(chg->hvdcp_disable_votable_indirect, VBUS_CC_SHORT_VOTER, true, 0);
	vote(chg->hvdcp_disable_votable_indirect, PD_INACTIVE_VOTER, true, 0);

	/* reset power delivery voters */
	vote(chg->pd_allowed_votable, PD_VOTER, false, 0);
	vote(chg->pd_disallowed_votable_indirect, CC_DETACHED_VOTER, true, 0);
	vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER, true, 0);

	/* reset usb irq voters */
	vote(chg->usb_irq_enable_votable, PD_VOTER, false, 0);
	vote(chg->usb_irq_enable_votable, QC_VOTER, false, 0);

	/* reset parallel voters */
	vote(chg->pl_disable_votable, PL_DELAY_VOTER, true, 0);
	vote(chg->pl_enable_votable_indirect, USBIN_I_VOTER, false, 0);
	vote(chg->pl_enable_votable_indirect, USBIN_V_VOTER, false, 0);
	vote(chg->awake_votable, PL_DELAY_VOTER, false, 0);

	chg->vconn_attempts = 0;
	chg->otg_attempts = 0;
	chg->pulse_cnt = 0;
	chg->usb_icl_delta_ua = 0;
	chg->voltage_min_uv = MICRO_5V;
	chg->voltage_max_uv = MICRO_5V;
	chg->pd_active = 0;
	chg->pd_hard_reset = 0;
	chg->typec_legacy_valid = false;

	/* write back the default FLOAT charger configuration */
	rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				(u8)FLOAT_OPTIONS_MASK, chg->float_cfg);
	if (rc < 0)
		smblib_err(chg, "Couldn't write float charger options rc=%d\n",
			rc);

	/* reset back to 120mS tCC debounce */
	rc = smblib_masked_write(chg, MISC_CFG_REG, TCC_DEBOUNCE_20MS_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't set 120mS tCC debounce rc=%d\n", rc);

	/* enable APSD CC trigger for next insertion */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
				APSD_START_ON_CC_BIT, APSD_START_ON_CC_BIT);
	if (rc < 0)
		smblib_err(chg, "Couldn't enable APSD_START_ON_CC rc=%d\n", rc);

	if (chg->wa_flags & QC_AUTH_INTERRUPT_WA_BIT) {
		/* re-enable AUTH_IRQ_EN_CFG_BIT */
		rc = smblib_masked_write(chg,
				USBIN_SOURCE_CHANGE_INTRPT_ENB_REG,
				AUTH_IRQ_EN_CFG_BIT, AUTH_IRQ_EN_CFG_BIT);
		if (rc < 0)
			smblib_err(chg,
				"Couldn't enable QC auth setting rc=%d\n", rc);
	}

	/* reconfigure allowed voltage for HVDCP */
	rc = smblib_set_adapter_allowance(chg,
			USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V);
	if (rc < 0)
		smblib_err(chg, "Couldn't set USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V rc=%d\n",
			rc);

	/* enable DRP */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't enable DRP rc=%d\n", rc);

	/* HW controlled CC_OUT */
	rc = smblib_masked_write(chg, TAPER_TIMER_SEL_CFG_REG,
							TYPEC_SPARE_CFG_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't enable HW cc_out rc=%d\n", rc);

	/* restore crude sensor */
	rc = smblib_write(chg, TM_IO_DTEST4_SEL, 0xA5);
	if (rc < 0)
		smblib_err(chg, "Couldn't restore crude sensor rc=%d\n", rc);

	mutex_lock(&chg->vconn_oc_lock);
	if (!chg->vconn_en)
		goto unlock;

	smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 VCONN_EN_VALUE_BIT, 0);
	chg->vconn_en = false;

unlock:
	mutex_unlock(&chg->vconn_oc_lock);

	/* clear exit sink based on cc */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
						EXIT_SNK_BASED_ON_CC_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't clear exit_sink_based_on_cc rc=%d\n",
				rc);

	typec_sink_removal(chg);
	smblib_update_usb_type(chg);

	if (chg->use_extcon) {
		if (chg->otg_present)
			smblib_notify_usb_host(chg, false);
		else
			smblib_notify_device_mode(chg, false);
	}
	chg->otg_present = false;
}

static void smblib_handle_typec_insertion(struct smb_charger *chg)
{
	int rc;

	CHG_DBG("%s: start\n", __func__);

	vote(chg->pd_disallowed_votable_indirect, CC_DETACHED_VOTER, false, 0);

	/* disable APSD CC trigger since CC is attached */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG, APSD_START_ON_CC_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't disable APSD_START_ON_CC rc=%d\n",
									rc);

	if (chg->typec_status[3] & UFP_DFP_MODE_STATUS_BIT) {
		typec_sink_insertion(chg);
	} else {
		rc = smblib_request_dpdm(chg, true);
		if (rc < 0)
			smblib_err(chg, "Couldn't to enable DPDM rc=%d\n", rc);
		typec_sink_removal(chg);
	}
}

static void smblib_handle_rp_change(struct smb_charger *chg, int typec_mode)
{
	int rp_ua;
	const struct apsd_result *apsd = smblib_get_apsd_result(chg);

	if ((apsd->pst != POWER_SUPPLY_TYPE_USB_DCP)
		&& (apsd->pst != POWER_SUPPLY_TYPE_USB_FLOAT))
		return;

	/*
	 * if APSD indicates FLOAT and the USB stack had detected SDP,
	 * do not respond to Rp changes as we do not confirm that its
	 * a legacy cable
	 */
	if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)
		return;
	/*
	 * We want the ICL vote @ 100mA for a FLOAT charger
	 * until the detection by the USB stack is complete.
	 * Ignore the Rp changes unless there is a
	 * pre-existing valid vote.
	 */
	if (apsd->pst == POWER_SUPPLY_TYPE_USB_FLOAT &&
		get_client_vote(chg->usb_icl_votable,
			LEGACY_UNKNOWN_VOTER) <= 100000)
		return;

	/*
	 * handle Rp change for DCP/FLOAT/OCP.
	 * Update the current only if the Rp is different from
	 * the last Rp value.
	 */
	smblib_dbg(chg, PR_MISC, "CC change old_mode=%d new_mode=%d\n",
						chg->typec_mode, typec_mode);

	rp_ua = get_rp_based_dcp_current(chg, typec_mode);
	vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, rp_ua);
}

static void smblib_handle_typec_cc_state_change(struct smb_charger *chg)
{
	int typec_mode;

	if (chg->pr_swap_in_progress)
		return;

	typec_mode = smblib_get_prop_typec_mode(chg);
	if (chg->typec_present && (typec_mode != chg->typec_mode))
		smblib_handle_rp_change(chg, typec_mode);

	chg->typec_mode = typec_mode;

	if (!chg->typec_present && chg->typec_mode != POWER_SUPPLY_TYPEC_NONE) {
		chg->typec_present = true;
		smblib_dbg(chg, PR_MISC, "TypeC %s insertion\n",
			smblib_typec_mode_name[chg->typec_mode]);
		smblib_handle_typec_insertion(chg);
	} else if (chg->typec_present &&
				chg->typec_mode == POWER_SUPPLY_TYPEC_NONE) {
		chg->typec_present = false;
		smblib_dbg(chg, PR_MISC, "TypeC removal\n");
		smblib_handle_typec_removal(chg);
	}

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: cc-state-change; Type-C %s detected\n",
				smblib_typec_mode_name[chg->typec_mode]);
}

static void smblib_usb_typec_change(struct smb_charger *chg)
{
	int rc;

	rc = smblib_multibyte_read(chg, TYPE_C_STATUS_1_REG,
							chg->typec_status, 5);
	if (rc < 0) {
		smblib_err(chg, "Couldn't cache USB Type-C status rc=%d\n", rc);
		return;
	}

	smblib_handle_typec_cc_state_change(chg);

	if (chg->typec_status[3] & TYPEC_VBUS_ERROR_STATUS_BIT)
		smblib_dbg(chg, PR_INTERRUPT, "IRQ: vbus-error\n");

	if (chg->typec_status[3] & TYPEC_VCONN_OVERCURR_STATUS_BIT)
		schedule_work(&chg->vconn_oc_work);

	power_supply_changed(chg->usb_psy);
}

irqreturn_t smblib_handle_usb_typec_change(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	if (chg->micro_usb_mode) {
		cancel_delayed_work_sync(&chg->uusb_otg_work);
		vote(chg->awake_votable, OTG_DELAY_VOTER, true, 0);
		smblib_dbg(chg, PR_INTERRUPT, "Scheduling OTG work\n");
		schedule_delayed_work(&chg->uusb_otg_work,
				msecs_to_jiffies(chg->otg_delay_ms));
		return IRQ_HANDLED;
	}

	if (chg->cc2_detach_wa_active || chg->typec_en_dis_active) {
		smblib_dbg(chg, PR_INTERRUPT, "Ignoring since %s active\n",
			chg->cc2_detach_wa_active ?
			"cc2_detach_wa" : "typec_en_dis");
		return IRQ_HANDLED;
	}

	mutex_lock(&chg->lock);
	smblib_usb_typec_change(chg);
	mutex_unlock(&chg->lock);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_dc_plugin(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	power_supply_changed(chg->dc_psy);
	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_high_duty_cycle(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;

	chg->is_hdc = true;
	schedule_delayed_work(&chg->clear_hdc_work, msecs_to_jiffies(60));

	return IRQ_HANDLED;
}
// A O no member bb_removal_work

static void smblib_bb_removal_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
						bb_removal_work.work);

	vote(chg->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
	vote(chg->awake_votable, BOOST_BACK_VOTER, false, 0);
}

#define BOOST_BACK_UNVOTE_DELAY_MS		750
#define BOOST_BACK_STORM_COUNT			3
#define WEAK_CHG_STORM_COUNT			8
irqreturn_t smblib_handle_switcher_power_ok(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	struct storm_watch *wdata = &irq_data->storm_data;
	int rc, usb_icl;
	u8 stat;

	if (!(chg->wa_flags & BOOST_BACK_WA))
		return IRQ_HANDLED;

	rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n", rc);
		return IRQ_HANDLED;
	}

	/* skip suspending input if its already suspended by some other voter */
	usb_icl = get_effective_result(chg->usb_icl_votable);
	if ((stat & USE_USBIN_BIT) && usb_icl >= 0 && usb_icl < USBIN_25MA)
		return IRQ_HANDLED;

	if (stat & USE_DCIN_BIT)
		return IRQ_HANDLED;

	if (is_storming(&irq_data->storm_data)) {
		/* This could be a weak charger reduce ICL */
		if (!is_client_vote_enabled(chg->usb_icl_votable,
						WEAK_CHARGER_VOTER)) {
			smblib_err(chg,
				"Weak charger detected: voting %dmA ICL\n",
				*chg->weak_chg_icl_ua / 1000);
			vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
					true, *chg->weak_chg_icl_ua);
			/*
			 * reset storm data and set the storm threshold
			 * to 3 for reverse boost detection.
			 */
			update_storm_count(wdata, BOOST_BACK_STORM_COUNT);
		} else {
			smblib_err(chg,
				"Reverse boost detected: voting 0mA to suspend input\n");
			vote(chg->usb_icl_votable, BOOST_BACK_VOTER, true, 0);
			vote(chg->awake_votable, BOOST_BACK_VOTER, true, 0);
			/*
			 * Remove the boost-back vote after a delay, to avoid
			 * permanently suspending the input if the boost-back
			 * condition is unintentionally hit.
			 */
			schedule_delayed_work(&chg->bb_removal_work,
				msecs_to_jiffies(BOOST_BACK_UNVOTE_DELAY_MS));
		}
	}

	return IRQ_HANDLED;
}

irqreturn_t smblib_handle_wdog_bark(int irq, void *data)
{
	struct smb_irq_data *irq_data = data;
	struct smb_charger *chg = irq_data->parent_data;
	int rc;

	smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);

	rc = smblib_write(chg, BARK_BITE_WDOG_PET_REG, BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		smblib_err(chg, "Couldn't pet the dog rc=%d\n", rc);

	if (chg->step_chg_enabled || chg->sw_jeita_enabled)
		power_supply_changed(chg->batt_psy);

	return IRQ_HANDLED;
}

/**************
 * Additional USB PSY getters/setters
 * that call interrupt functions
***************/

int smblib_get_prop_pr_swap_in_progress(struct smb_charger *chg,
				union power_supply_propval *val)
{
	val->intval = chg->pr_swap_in_progress;
	return 0;
}

int smblib_set_prop_pr_swap_in_progress(struct smb_charger *chg,
				const union power_supply_propval *val)
{
	int rc;

	chg->pr_swap_in_progress = val->intval;
	/*
	 * call the cc changed irq to handle real removals while
	 * PR_SWAP was in progress
	 */
	smblib_usb_typec_change(chg);
	rc = smblib_masked_write(chg, MISC_CFG_REG, TCC_DEBOUNCE_20MS_BIT,
			val->intval ? TCC_DEBOUNCE_20MS_BIT : 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't set tCC debounce rc=%d\n", rc);
	return 0;
}

/***************
 * Work Queues *
 ***************/
static void smblib_uusb_otg_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
						uusb_otg_work.work);
	int rc;
	u8 stat;
	bool otg;

	rc = smblib_read(chg, TYPE_C_STATUS_3_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read TYPE_C_STATUS_3 rc=%d\n", rc);
		goto out;
	}

	otg = !!(stat & (U_USB_GND_NOVBUS_BIT | U_USB_GND_BIT));
	extcon_set_cable_state_(chg->extcon, EXTCON_USB_HOST, otg);
	smblib_dbg(chg, PR_REGISTER, "TYPE_C_STATUS_3 = 0x%02x OTG=%d\n",
			stat, otg);
	power_supply_changed(chg->usb_psy);

out:
	vote(chg->awake_votable, OTG_DELAY_VOTER, false, 0);
}


static void smblib_hvdcp_detect_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
					       hvdcp_detect_work.work);

	vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
				false, 0);
	power_supply_changed(chg->usb_psy);
}

static void bms_update_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
						bms_update_work);

	smblib_suspend_on_debug_battery(chg);

	if (chg->batt_psy)
		power_supply_changed(chg->batt_psy);
}

static void clear_hdc_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
						clear_hdc_work.work);

	chg->is_hdc = 0;
}

static void rdstd_cc2_detach_work(struct work_struct *work)
{
	int rc;
	u8 stat4, stat5;
	struct smb_charger *chg = container_of(work, struct smb_charger,
						rdstd_cc2_detach_work);

	if (!chg->cc2_detach_wa_active)
		return;

	/*
	 * WA steps -
	 * 1. Enable both UFP and DFP, wait for 10ms.
	 * 2. Disable DFP, wait for 30ms.
	 * 3. Removal detected if both TYPEC_DEBOUNCE_DONE_STATUS
	 *    and TIMER_STAGE bits are gone, otherwise repeat all by
	 *    work rescheduling.
	 * Note, work will be cancelled when USB_PLUGIN rises.
	 */

	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 UFP_EN_CMD_BIT | DFP_EN_CMD_BIT,
				 UFP_EN_CMD_BIT | DFP_EN_CMD_BIT);
	if (rc < 0) {
		smblib_err(chg, "Couldn't write TYPE_C_CTRL_REG rc=%d\n", rc);
		return;
	}

	usleep_range(10000, 11000);

	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 UFP_EN_CMD_BIT | DFP_EN_CMD_BIT,
				 UFP_EN_CMD_BIT);
	if (rc < 0) {
		smblib_err(chg, "Couldn't write TYPE_C_CTRL_REG rc=%d\n", rc);
		return;
	}

	usleep_range(30000, 31000);

	rc = smblib_read(chg, TYPE_C_STATUS_4_REG, &stat4);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read TYPE_C_STATUS_4 rc=%d\n", rc);
		return;
	}

	rc = smblib_read(chg, TYPE_C_STATUS_5_REG, &stat5);
	if (rc < 0) {
		smblib_err(chg,
			"Couldn't read TYPE_C_STATUS_5_REG rc=%d\n", rc);
		return;
	}

	if ((stat4 & TYPEC_DEBOUNCE_DONE_STATUS_BIT)
			|| (stat5 & TIMER_STAGE_2_BIT)) {
		smblib_dbg(chg, PR_MISC, "rerunning DD=%d TS2BIT=%d\n",
				(int)(stat4 & TYPEC_DEBOUNCE_DONE_STATUS_BIT),
				(int)(stat5 & TIMER_STAGE_2_BIT));
		goto rerun;
	}

	smblib_dbg(chg, PR_MISC, "Bingo CC2 Removal detected\n");
	chg->cc2_detach_wa_active = false;
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
						EXIT_SNK_BASED_ON_CC_BIT, 0);
	smblib_reg_block_restore(chg, cc2_detach_settings);
	mutex_lock(&chg->lock);
	smblib_usb_typec_change(chg);
	mutex_unlock(&chg->lock);
	return;

rerun:
	schedule_work(&chg->rdstd_cc2_detach_work);
}

static void smblib_otg_oc_exit(struct smb_charger *chg, bool success)
{
	int rc;

	chg->otg_attempts = 0;
	if (!success) {
		smblib_err(chg, "OTG soft start failed\n");
		chg->otg_en = false;
	}

	smblib_dbg(chg, PR_OTG, "enabling VBUS < 1V check\n");
	rc = smblib_masked_write(chg, OTG_CFG_REG,
					QUICKSTART_OTG_FASTROLESWAP_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't enable VBUS < 1V check rc=%d\n", rc);
}

#define MAX_OC_FALLING_TRIES 10
static void smblib_otg_oc_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
								otg_oc_work);
	int rc, i;
	u8 stat;

	if (!chg->vbus_vreg || !chg->vbus_vreg->rdev)
		return;

	smblib_err(chg, "over-current detected on VBUS\n");
	mutex_lock(&chg->otg_oc_lock);
	if (!chg->otg_en)
		goto unlock;

	smblib_dbg(chg, PR_OTG, "disabling VBUS < 1V check\n");
	smblib_masked_write(chg, OTG_CFG_REG,
					QUICKSTART_OTG_FASTROLESWAP_BIT,
					QUICKSTART_OTG_FASTROLESWAP_BIT);

	/*
	 * If 500ms has passed and another over-current interrupt has not
	 * triggered then it is likely that the software based soft start was
	 * successful and the VBUS < 1V restriction should be re-enabled.
	 */
	schedule_delayed_work(&chg->otg_ss_done_work, msecs_to_jiffies(500));

	rc = _smblib_vbus_regulator_disable(chg->vbus_vreg->rdev);
	if (rc < 0) {
		smblib_err(chg, "Couldn't disable VBUS rc=%d\n", rc);
		goto unlock;
	}

	if (++chg->otg_attempts > OTG_MAX_ATTEMPTS) {
		cancel_delayed_work_sync(&chg->otg_ss_done_work);
		smblib_err(chg, "OTG failed to enable after %d attempts\n",
			   chg->otg_attempts - 1);
		smblib_otg_oc_exit(chg, false);
		goto unlock;
	}

	/*
	 * The real time status should go low within 10ms. Poll every 1-2ms to
	 * minimize the delay when re-enabling OTG.
	 */
	for (i = 0; i < MAX_OC_FALLING_TRIES; ++i) {
		usleep_range(1000, 2000);
		rc = smblib_read(chg, OTG_BASE + INT_RT_STS_OFFSET, &stat);
		if (rc >= 0 && !(stat & OTG_OVERCURRENT_RT_STS_BIT))
			break;
	}

	if (i >= MAX_OC_FALLING_TRIES) {
		cancel_delayed_work_sync(&chg->otg_ss_done_work);
		smblib_err(chg, "OTG OC did not fall after %dms\n",
						2 * MAX_OC_FALLING_TRIES);
		smblib_otg_oc_exit(chg, false);
		goto unlock;
	}

	smblib_dbg(chg, PR_OTG, "OTG OC fell after %dms\n", 2 * i + 1);
	rc = _smblib_vbus_regulator_enable(chg->vbus_vreg->rdev);
	if (rc < 0) {
		smblib_err(chg, "Couldn't enable VBUS rc=%d\n", rc);
		goto unlock;
	}

unlock:
	mutex_unlock(&chg->otg_oc_lock);
}

static void smblib_vconn_oc_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
								vconn_oc_work);
	int rc, i;
	u8 stat;

	if (chg->micro_usb_mode)
		return;

	smblib_err(chg, "over-current detected on VCONN\n");
	if (!chg->vconn_vreg || !chg->vconn_vreg->rdev)
		return;

	mutex_lock(&chg->vconn_oc_lock);
	rc = _smblib_vconn_regulator_disable(chg->vconn_vreg->rdev);
	if (rc < 0) {
		smblib_err(chg, "Couldn't disable VCONN rc=%d\n", rc);
		goto unlock;
	}

	if (++chg->vconn_attempts > VCONN_MAX_ATTEMPTS) {
		smblib_err(chg, "VCONN failed to enable after %d attempts\n",
			   chg->otg_attempts - 1);
		chg->vconn_en = false;
		chg->vconn_attempts = 0;
		goto unlock;
	}

	/*
	 * The real time status should go low within 10ms. Poll every 1-2ms to
	 * minimize the delay when re-enabling OTG.
	 */
	for (i = 0; i < MAX_OC_FALLING_TRIES; ++i) {
		usleep_range(1000, 2000);
		rc = smblib_read(chg, TYPE_C_STATUS_4_REG, &stat);
		if (rc >= 0 && !(stat & TYPEC_VCONN_OVERCURR_STATUS_BIT))
			break;
	}

	if (i >= MAX_OC_FALLING_TRIES) {
		smblib_err(chg, "VCONN OC did not fall after %dms\n",
						2 * MAX_OC_FALLING_TRIES);
		chg->vconn_en = false;
		chg->vconn_attempts = 0;
		goto unlock;
	}

	smblib_dbg(chg, PR_OTG, "VCONN OC fell after %dms\n", 2 * i + 1);
	if (++chg->vconn_attempts > VCONN_MAX_ATTEMPTS) {
		smblib_err(chg, "VCONN failed to enable after %d attempts\n",
			   chg->vconn_attempts - 1);
		chg->vconn_en = false;
		goto unlock;
	}

	rc = _smblib_vconn_regulator_enable(chg->vconn_vreg->rdev);
	if (rc < 0) {
		smblib_err(chg, "Couldn't enable VCONN rc=%d\n", rc);
		goto unlock;
	}

unlock:
	mutex_unlock(&chg->vconn_oc_lock);
}

static void smblib_otg_ss_done_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
							otg_ss_done_work.work);
	int rc;
	bool success = false;
	u8 stat;

	mutex_lock(&chg->otg_oc_lock);
	rc = smblib_read(chg, OTG_STATUS_REG, &stat);
	if (rc < 0)
		smblib_err(chg, "Couldn't read OTG status rc=%d\n", rc);
	else if (stat & BOOST_SOFTSTART_DONE_BIT)
		success = true;

	smblib_otg_oc_exit(chg, success);
	mutex_unlock(&chg->otg_oc_lock);
}

static void smblib_icl_change_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
							icl_change_work.work);
	int rc, settled_ua;

	rc = smblib_get_charge_param(chg, &chg->param.icl_stat, &settled_ua);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get ICL status rc=%d\n", rc);
		return;
	}

	power_supply_changed(chg->usb_main_psy);

	smblib_dbg(chg, PR_INTERRUPT, "icl_settled=%d\n", settled_ua);
}

static void smblib_pl_enable_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
							pl_enable_work.work);

	smblib_dbg(chg, PR_PARALLEL, "timer expired, enabling parallel\n");
	vote(chg->pl_disable_votable, PL_DELAY_VOTER, false, 0);
	vote(chg->awake_votable, PL_DELAY_VOTER, false, 0);
}

static void smblib_legacy_detection_work(struct work_struct *work)
{
	struct smb_charger *chg = container_of(work, struct smb_charger,
							legacy_detection_work);
	int rc;
	u8 stat;
	bool legacy, rp_high;

	mutex_lock(&chg->lock);
	chg->typec_en_dis_active = 1;
	smblib_dbg(chg, PR_MISC, "running legacy unknown workaround\n");
	rc = smblib_masked_write(chg,
				TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				TYPEC_DISABLE_CMD_BIT,
				TYPEC_DISABLE_CMD_BIT);
	if (rc < 0)
		smblib_err(chg, "Couldn't disable type-c rc=%d\n", rc);

	/* wait for the adapter to turn off VBUS */
	msleep(500);

	rc = smblib_masked_write(chg,
				TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0)
		smblib_err(chg, "Couldn't enable type-c rc=%d\n", rc);

	/* wait for type-c detection to complete */
	msleep(100);

	rc = smblib_read(chg, TYPE_C_STATUS_5_REG, &stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read typec stat5 rc = %d\n", rc);
		goto unlock;
	}

	chg->typec_legacy_valid = true;
	vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, false, 0);
	legacy = stat & TYPEC_LEGACY_CABLE_STATUS_BIT;
	rp_high = chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_HIGH;
	if (!legacy || !rp_high)
		vote(chg->hvdcp_disable_votable_indirect, VBUS_CC_SHORT_VOTER,
								false, 0);
	CHG_DBG("legacy valid, legacy %d\n", legacy);
unlock:
	chg->typec_en_dis_active = 0;
	smblib_usb_typec_change(chg);
	mutex_unlock(&chg->lock);
}

static int smblib_create_votables(struct smb_charger *chg)
{
	int rc = 0;

	chg->fcc_votable = find_votable("FCC");
	if (chg->fcc_votable == NULL) {
		rc = -EINVAL;
		smblib_err(chg, "Couldn't find FCC votable rc=%d\n", rc);
		return rc;
	}

	chg->fv_votable = find_votable("FV");
	if (chg->fv_votable == NULL) {
		rc = -EINVAL;
		smblib_err(chg, "Couldn't find FV votable rc=%d\n", rc);
		return rc;
	}

	chg->usb_icl_votable = find_votable("USB_ICL");
	if (!chg->usb_icl_votable) {
		rc = -EINVAL;
		smblib_err(chg, "Couldn't find USB_ICL votable rc=%d\n", rc);
		return rc;
	}

	chg->pl_disable_votable = find_votable("PL_DISABLE");
	if (chg->pl_disable_votable == NULL) {
		rc = -EINVAL;
		smblib_err(chg, "Couldn't find votable PL_DISABLE rc=%d\n", rc);
		return rc;
	}

	chg->pl_enable_votable_indirect = find_votable("PL_ENABLE_INDIRECT");
	if (chg->pl_enable_votable_indirect == NULL) {
		rc = -EINVAL;
		smblib_err(chg,
			"Couldn't find votable PL_ENABLE_INDIRECT rc=%d\n",
			rc);
		return rc;
	}

	vote(chg->pl_disable_votable, PL_DELAY_VOTER, true, 0);

	chg->dc_suspend_votable = create_votable("DC_SUSPEND", VOTE_SET_ANY,
					smblib_dc_suspend_vote_callback,
					chg);
	if (IS_ERR(chg->dc_suspend_votable)) {
		rc = PTR_ERR(chg->dc_suspend_votable);
		return rc;
	}

	chg->dc_icl_votable = create_votable("DC_ICL", VOTE_MIN,
					smblib_dc_icl_vote_callback,
					chg);
	if (IS_ERR(chg->dc_icl_votable)) {
		rc = PTR_ERR(chg->dc_icl_votable);
		return rc;
	}

	chg->pd_disallowed_votable_indirect
		= create_votable("PD_DISALLOWED_INDIRECT", VOTE_SET_ANY,
			smblib_pd_disallowed_votable_indirect_callback, chg);
	if (IS_ERR(chg->pd_disallowed_votable_indirect)) {
		rc = PTR_ERR(chg->pd_disallowed_votable_indirect);
		return rc;
	}

	chg->pd_allowed_votable = create_votable("PD_ALLOWED",
					VOTE_SET_ANY, NULL, NULL);
	if (IS_ERR(chg->pd_allowed_votable)) {
		rc = PTR_ERR(chg->pd_allowed_votable);
		return rc;
	}

	chg->awake_votable = create_votable("AWAKE", VOTE_SET_ANY,
					smblib_awake_vote_callback,
					chg);
	if (IS_ERR(chg->awake_votable)) {
		rc = PTR_ERR(chg->awake_votable);
		return rc;
	}

	chg->chg_disable_votable = create_votable("CHG_DISABLE", VOTE_SET_ANY,
					smblib_chg_disable_vote_callback,
					chg);
	if (IS_ERR(chg->chg_disable_votable)) {
		rc = PTR_ERR(chg->chg_disable_votable);
		return rc;
	}


	chg->hvdcp_disable_votable_indirect = create_votable(
				"HVDCP_DISABLE_INDIRECT",
				VOTE_SET_ANY,
				smblib_hvdcp_disable_indirect_vote_callback,
				chg);
	if (IS_ERR(chg->hvdcp_disable_votable_indirect)) {
		rc = PTR_ERR(chg->hvdcp_disable_votable_indirect);
		return rc;
	}

	chg->hvdcp_enable_votable = create_votable("HVDCP_ENABLE",
					VOTE_SET_ANY,
					smblib_hvdcp_enable_vote_callback,
					chg);
	if (IS_ERR(chg->hvdcp_enable_votable)) {
		rc = PTR_ERR(chg->hvdcp_enable_votable);
		return rc;
	}

	chg->apsd_disable_votable = create_votable("APSD_DISABLE",
					VOTE_SET_ANY,
					smblib_apsd_disable_vote_callback,
					chg);
	if (IS_ERR(chg->apsd_disable_votable)) {
		rc = PTR_ERR(chg->apsd_disable_votable);
		return rc;
	}

	chg->hvdcp_hw_inov_dis_votable = create_votable("HVDCP_HW_INOV_DIS",
					VOTE_SET_ANY,
					smblib_hvdcp_hw_inov_dis_vote_callback,
					chg);
	if (IS_ERR(chg->hvdcp_hw_inov_dis_votable)) {
		rc = PTR_ERR(chg->hvdcp_hw_inov_dis_votable);
		return rc;
	}

	chg->usb_irq_enable_votable = create_votable("USB_IRQ_DISABLE",
					VOTE_SET_ANY,
					smblib_usb_irq_enable_vote_callback,
					chg);
	if (IS_ERR(chg->usb_irq_enable_votable)) {
		rc = PTR_ERR(chg->usb_irq_enable_votable);
		return rc;
	}

	chg->typec_irq_disable_votable = create_votable("TYPEC_IRQ_DISABLE",
					VOTE_SET_ANY,
					smblib_typec_irq_disable_vote_callback,
					chg);
	if (IS_ERR(chg->typec_irq_disable_votable)) {
		rc = PTR_ERR(chg->typec_irq_disable_votable);
		return rc;
	}

	return rc;
}

static void smblib_destroy_votables(struct smb_charger *chg)
{
	if (chg->dc_suspend_votable)
		destroy_votable(chg->dc_suspend_votable);
	if (chg->usb_icl_votable)
		destroy_votable(chg->usb_icl_votable);
	if (chg->dc_icl_votable)
		destroy_votable(chg->dc_icl_votable);
	if (chg->pd_disallowed_votable_indirect)
		destroy_votable(chg->pd_disallowed_votable_indirect);
	if (chg->pd_allowed_votable)
		destroy_votable(chg->pd_allowed_votable);
	if (chg->awake_votable)
		destroy_votable(chg->awake_votable);
	if (chg->chg_disable_votable)
		destroy_votable(chg->chg_disable_votable);
	if (chg->apsd_disable_votable)
		destroy_votable(chg->apsd_disable_votable);
	if (chg->hvdcp_hw_inov_dis_votable)
		destroy_votable(chg->hvdcp_hw_inov_dis_votable);
	if (chg->typec_irq_disable_votable)
		destroy_votable(chg->typec_irq_disable_votable);
}

static void smblib_iio_deinit(struct smb_charger *chg)
{
	if (!IS_ERR_OR_NULL(chg->iio.temp_chan))
		iio_channel_release(chg->iio.temp_chan);
	if (!IS_ERR_OR_NULL(chg->iio.temp_max_chan))
		iio_channel_release(chg->iio.temp_max_chan);
	if (!IS_ERR_OR_NULL(chg->iio.usbin_i_chan))
		iio_channel_release(chg->iio.usbin_i_chan);
	if (!IS_ERR_OR_NULL(chg->iio.usbin_v_chan))
		iio_channel_release(chg->iio.usbin_v_chan);
	if (!IS_ERR_OR_NULL(chg->iio.batt_i_chan))
		iio_channel_release(chg->iio.batt_i_chan);
}

int smblib_init(struct smb_charger *chg)
{
	int rc = 0;

	mutex_init(&chg->lock);
	mutex_init(&chg->write_lock);
	mutex_init(&chg->otg_oc_lock);
	mutex_init(&chg->vconn_oc_lock);
	INIT_WORK(&chg->bms_update_work, bms_update_work);
	INIT_WORK(&chg->rdstd_cc2_detach_work, rdstd_cc2_detach_work);
	INIT_DELAYED_WORK(&chg->hvdcp_detect_work, smblib_hvdcp_detect_work);
	INIT_DELAYED_WORK(&chg->clear_hdc_work, clear_hdc_work);
//ASUS work +++
	INIT_DELAYED_WORK(&chg->asus_chg_flow_work, asus_chg_flow_work);
	INIT_DELAYED_WORK(&chg->asus_adapter_adc_work, asus_adapter_adc_work);
	INIT_DELAYED_WORK(&chg->asus_min_monitor_work, asus_min_monitor_work);
	INIT_DELAYED_WORK(&chg->asus_qc3_soft_start_work, asus_qc3_soft_start_work);	
	INIT_DELAYED_WORK(&chg->asus_batt_RTC_work, asus_batt_RTC_work);
	INIT_DELAYED_WORK(&chg->asus_set_flow_flag_work, asus_set_flow_flag_work);
	INIT_DELAYED_WORK(&chg->asus_rerun_DRP_work, asus_rerun_DRP_work);//WA for Type-C detection
	alarm_init(&bat_alarm, ALARM_REALTIME, batAlarm_handler);
//ASUS work ---
	INIT_WORK(&chg->otg_oc_work, smblib_otg_oc_work);
	INIT_WORK(&chg->vconn_oc_work, smblib_vconn_oc_work);
	INIT_DELAYED_WORK(&chg->otg_ss_done_work, smblib_otg_ss_done_work);
	INIT_DELAYED_WORK(&chg->icl_change_work, smblib_icl_change_work);
	INIT_DELAYED_WORK(&chg->pl_enable_work, smblib_pl_enable_work);
	INIT_WORK(&chg->legacy_detection_work, smblib_legacy_detection_work);
	INIT_DELAYED_WORK(&chg->uusb_otg_work, smblib_uusb_otg_work);
	INIT_DELAYED_WORK(&chg->bb_removal_work, smblib_bb_removal_work);
	chg->fake_capacity = -EINVAL;
	chg->fake_input_current_limited = -EINVAL;

	switch (chg->mode) {
	case PARALLEL_MASTER:
		rc = qcom_batt_init();
		if (rc < 0) {
			smblib_err(chg, "Couldn't init qcom_batt_init rc=%d\n",
				rc);
			return rc;
		}

		rc = qcom_step_chg_init(chg->step_chg_enabled,
						chg->sw_jeita_enabled);
		if (rc < 0) {
			smblib_err(chg, "Couldn't init qcom_step_chg_init rc=%d\n",
				rc);
			return rc;
		}

		rc = smblib_create_votables(chg);
		if (rc < 0) {
			smblib_err(chg, "Couldn't create votables rc=%d\n",
				rc);
			return rc;
		}

		rc = smblib_register_notifier(chg);
		if (rc < 0) {
			smblib_err(chg,
				"Couldn't register notifier rc=%d\n", rc);
			return rc;
		}

		chg->bms_psy = power_supply_get_by_name("bms");
		chg->pl.psy = power_supply_get_by_name("parallel");
		break;
	case PARALLEL_SLAVE:
		break;
	default:
		smblib_err(chg, "Unsupported mode %d\n", chg->mode);
		return -EINVAL;
	}

	return rc;
}

int smblib_deinit(struct smb_charger *chg)
{
	switch (chg->mode) {
	case PARALLEL_MASTER:
		cancel_work_sync(&chg->bms_update_work);
		cancel_work_sync(&chg->rdstd_cc2_detach_work);
		cancel_delayed_work_sync(&chg->hvdcp_detect_work);
		cancel_delayed_work_sync(&chg->clear_hdc_work);
		cancel_work_sync(&chg->otg_oc_work);
		cancel_work_sync(&chg->vconn_oc_work);
		cancel_delayed_work_sync(&chg->otg_ss_done_work);
		cancel_delayed_work_sync(&chg->icl_change_work);
		cancel_delayed_work_sync(&chg->pl_enable_work);
		cancel_work_sync(&chg->legacy_detection_work);
		cancel_delayed_work_sync(&chg->uusb_otg_work);
		cancel_delayed_work_sync(&chg->bb_removal_work);
		power_supply_unreg_notifier(&chg->nb);
		smblib_destroy_votables(chg);
		qcom_step_chg_deinit();
		qcom_batt_deinit();
		break;
	case PARALLEL_SLAVE:
		break;
	default:
		smblib_err(chg, "Unsupported mode %d\n", chg->mode);
		return -EINVAL;
	}

	smblib_iio_deinit(chg);

	return 0;
}
