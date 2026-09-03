// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for PE26100 Direct charger
 * Based on existing LN8411 driver
 */


#include <linux/err.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#include <misc/gvotable.h>
#include <misc/logbuffer.h>

#include "pe26100_regs.h"
#include "pe26100_driver.h"
#include "pe26100_cp_charger.h"

#if IS_ENABLED(CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

/* Timer definition */
#define PE26100_CP_VBATMIN_CHECK_T	1000	/* 1000ms */
#define PE26100_CP_CCMODE_CHECK1_T	5000	/* 10000ms -> 500ms */
#define PE26100_CP_CCMODE_CHECK2_T	5000	/* 5000ms */
#define PE26100_CP_CVMODE_CHECK_T	2000	/* 2000ms */
#define PE26100_CP_ENABLE_DELAY_T	500	/* 500ms */
#define PE26100_CP_CVMODE_CHECK2_T	1000	/* 1000ms */

/* Battery Threshold */
#define PE26100_CP_DC_VBAT_MIN		3000000 /* uV */
/* Input Current Limit default value */
#define PE26100_CP_IIN_CFG_DFT		3000000 /* uA*/
/* Charging Float Voltage default value */
#define PE26100_CP_VFLOAT_DFT		4350000	/* uV */
/* Charging Float Voltage max voltage for comp */
#define PE26100_CP_COMP_VFLOAT_MAX	4700000	/* uV */

/* Charging Done Condition */
#define PE26100_CP_IIN_DONE_DFT		500000		/* uA */

/* Maximum TA voltage threshold */
#define PE26100_CP_TA_MAX_VOL		10500000 /* uV */
#define PE26100_CP_TA_MAX_VOL_2_1	11000000 /* UV */
#define PE26100_CP_TA_MAX_VOL_3_1	14000000 /* uV */
/* Maximum TA current threshold, set to max(cc_max) / 2 */
#define PE26100_CP_TA_MAX_CUR		2600000	 /* uA */
/* Minimum TA current threshold */
#define PE26100_CP_TA_MIN_CUR		1000000	/* uA - PPS minimum current */

#define PE26100_CP_TA_VOL_PRE_OFFSET	300000	 /* uV */
#define PE26100_CP_WLC_VOL_PRE_OFFSET	300000   /* uV */
#define PE26100_CP_WLC_VOL_TOLERANCE	80000	 /* uV */
/* Adjust CC mode TA voltage step */
#define PE26100_CP_TA_VOL_STEP_ADJ_CC	40000	/* uV */
/* Pre CV mode TA voltage step */
#define PE26100_CP_TA_VOL_STEP_PRE_CV	20000	/* uV */

/* IIN_CC adc offset for accuracy */
#define PE26100_CP_IIN_ADC_OFFSET	20000	/* uA */
/* IIN_CC compensation offset */
#define PE26100_CP_IIN_CC_COMP_OFFSET_LOW	75000	/* uA */
#define PE26100_CP_IIN_CC_COMP_OFFSET_HIGH	0	/* uA */
/* IIN_CC compensation offset in Power Limit Mode(Constant Power) TA */
#define PE26100_CP_IIN_CC_COMP_OFFSET_CP 20000	/* uA */
/* TA maximum voltage that can support CC in Constant Power Mode */
#define PE26100_CP_TA_MAX_VOL_CP	10250000
/* Offset for cc_max / 2 */
#define PE26100_CP_IIN_MAX_OFFSET	25000 /* uA */
/* Offset for TA max current */
#define PE26100_CP_TA_CUR_MAX_OFFSET	200000 /* uA */
#define IBUS_UCP_ENABLE_TIMEOUT		10 /* 10 sec */
#define PE26100_CP_IIN_ACTIVE_TIMEOUT	10 /* 10 sec */

/* maximum retry counter for restarting charging */
#define PE26100_CP_MAX_RETRY_CNT		3	/* retries */
#define PE26100_CP_MAX_EAGAIN_RETRY_CNT		3	/* retries */
#define PE26100_CP_MAX_LOW_BATT_RETRY_CNT	10	/* retries */
#define PE26100_CP_MAX_RX_VOL_RETRY_CNT		3	/* retries */

/* TA IIN tolerance */
#define PE26100_CP_TA_IIN_OFFSET	100000	/* uA */

/* PD Message Voltage and Current Step */
#define PD_MSG_TA_VOL_STEP		20000	/* uV */
#define PD_MSG_TA_CUR_STEP		50000	/* uA */

/* WCRX voltage Step */
#define WCRX_VOL_STEP			40000	/* uV */
#define WCRX_VOL_ERROR_STEP		50000	/* uV */
#define WCRX_VOL_STEP_SIZE		10000	/* uV */

#define PE26100_CP_TIER_SWITCH_DELTA	25000 /* uV */
#define PE26100_TA_CUR_TOLERANCE	75000 /* mA */

/* Status */
enum sts_mode_t {
	STS_MODE_CHG_LOOP,	/* TODO: There is no such thing */
	STS_MODE_VFLT_LOOP,
	STS_MODE_IIN_LOOP,
	STS_MODE_LOOP_INACTIVE,
	STS_MODE_CHG_DONE,
	STS_MODE_VIN_UVLO,
};

/* Timer ID */
enum timer_id_t {
	TIMER_ID_NONE,
	TIMER_VBATMIN_CHECK,
	TIMER_PRESET_DC,
	TIMER_PRESET_CONFIG,
	TIMER_CHECK_ACTIVE,
	TIMER_ADJUST_CCMODE,
	TIMER_CHECK_CCMODE,
	TIMER_ENTER_CVMODE,
	TIMER_CHECK_CVMODE, /* 8 */
	TIMER_PDMSG_SEND,   /* 9 */
	TIMER_ADJUST_TAVOL,
	TIMER_ADJUST_TACUR,
	TIMER_ERROR_RECOVER,
};


/* TA increment Type */
enum ta_inc_t {
	INC_NONE,	/* No increment */
	INC_TA_VOL,	/* TA voltage increment */
	INC_TA_CUR,	/* TA current increment */
};

/* BATT info Type */
enum batt_info_t {
	BATT_CURRENT,
	BATT_VOLTAGE,
};

struct pe26100_chg_default_reg cp_conf_2_1[] = {
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FREQUENCY, 0xA3) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_EST, 0x01) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0x46) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT, 0x7D) },		/* 125C */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC, 0x03) },		/* 72mA */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_OC, 0xDC) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV, 0x45) },		/* 5.5V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV, 0x96) },		/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV, 0x96) },		/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV, 0x96) },		/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV, 0x9B) },		/* 3.1V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV, 0xE4) },		/* 4.56V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV, 0x9B) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV, 0xF0) },		/* 4.8V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT_WARN, 0x64) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC_WARN, 0x05) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_OC_WARN, 0xD2) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV_WARN, 0x50) },	/* 4.64V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV_WARN, 0x71) },	/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV_WARN, 0x71) },	/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV_WARN, 0x71) },	/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV_WARN, 0xA0) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV_WARN, 0xE1) },	/* 4.5V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV_WARN, 0xA0) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV_WARN, 0xE6) },	/* 4.6V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FLT_MASK1, 0x40) },	/** Mask IIN_UCF */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK1, 0xFF) }, /* Mask all warn */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK2, 0x0F) }, /* Mask all warn */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_DEGLITCH, 0x81) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WATCHDOG, 0x08) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_LIM, 0x64) },
};

struct pe26100_chg_default_reg cp_conf_3_1[] = {
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FREQUENCY, 0x22) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_EST, 0x01) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0x46) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT, 0x7D) },		/* 125C */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC, 0x03) },		/* 72mA */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_OC, 0xDC) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV, 0x71) },		/* 9.04V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV, 0xE1) },		/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV, 0xE1) },		/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV, 0xE1) },		/* 12V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV, 0x9B) },		/* 3.1V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV, 0xE4) },		/* 4.56V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV, 0x9B) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV, 0xF0) },		/* 4.8V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT_WARN, 0x64) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC_WARN, 0x05) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_OC_WARN, 0xD2) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV_WARN, 0x78) },	/* 9.6V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV_WARN, 0xA9) },	/* 13.52V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV_WARN, 0xA9) },	/* 13.52V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV_WARN, 0xA9) },	/* 13.52V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV_WARN, 0xA0) },	/* 3.2V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV_WARN, 0xE1) },	/* 4.5V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV_WARN, 0xA0) },	/* 3.2V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV_WARN, 0xE6) },	/* 4.6V */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FLT_MASK1, 0x40) },	/* Mask IIN_UCF */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK1, 0xFF) }, /* Mask all warn */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK2, 0x0F) }, /* Mask all warn */
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_DEGLITCH, 0x81) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WATCHDOG, 0x08) },
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_LIM, 0x64) },
};

static int pe26100_cp_hw_init(struct pe26100_cp_charger *pe26100_cp);

static inline int conv_chg_mode(const struct pe26100_cp_charger *pe26100_cp, int val)
{
	return (pe26100_cp->chg_mode == CHG_2TO1_DC_MODE) ? val * 2 : val * 3;
}

int get_chip_info(struct pe26100_cp_charger *chg)
{
	uint8_t val;

	int err = pe26100_chg_reg_read(chg->core, PE26100_CHG_CHIPID, &val);

	if (err) {
		dev_err(chg->dev, "Error reading DEVICE_ID (%d)\n", err);
		return err;
	}

	chg->chip_id = val;

	dev_info(chg->dev, "ChipID: %02X\n", chg->chip_id);

	return 0;
}

static ssize_t chip_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct pe26100_cp_charger *chg = dev_get_drvdata(dev);

	ret = get_chip_info(chg);
	if (ret) {
		dev_err(dev, "Error while getting chip info\n");
		return ret;
	}

	ret = scnprintf(buf, PAGE_SIZE, "Chip Id : %#02X\n", chg->chip_id);
	return ret;
}

static DEVICE_ATTR_RO(chip_info);

static struct attribute *pe26100_cp_attr_group[] = {
	&dev_attr_chip_info.attr,
	NULL
};

static int pe26100_cp_set_vfloat(struct pe26100_cp_charger *pe26100_cp,
			      unsigned int v_float)
{

	pe26100_cp->vfloat_reg = v_float;
	dev_info(pe26100_cp->dev, "%s: v_float=%u\n", __func__, v_float);
	return 0;
}

static int pe26100_cp_set_input_current(struct pe26100_cp_charger *pe26100_cp,
					unsigned int iin)
{
	/* Add 50mA margin over iin_cc */
	iin += PD_MSG_TA_CUR_STEP;
	pe26100_cp->iin_reg = iin;

	dev_info(pe26100_cp->dev, "%s: iin=%d\n", __func__, iin);

	return 0;
}

static int pe26100_cp_set_prot(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;
	int iin_ocw, iin_ocf;

	if (pe26100_cp->chg_mode == CHG_2TO1_DC_MODE) {
		iin_ocw = min(pe26100_cp->ilim / 24000 * 102 / 100, 0x7F); /* min(102% ICL, 3.06A)*/
		iin_ocf = min(pe26100_cp->ilim / 24000 * 120 / 100, 0x96); /* min(120% ICL, 3.6A) */
	} else if (pe26100_cp->chg_mode == CHG_3TO1_DC_MODE) {
		iin_ocw = 0x4b; /* 1.8 */
		iin_ocf = 0x53; /* 2A */
	} else {
		dev_err(pe26100_cp->dev, "%s: Invalid chg_mode: %d\n", __func__,
			pe26100_cp->chg_mode);
		return -EINVAL;
	}

	ret = pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IIN_OC_WARN, iin_ocw);
	ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IIN_OC, iin_ocf);
	dev_info(pe26100_cp->dev, "%s: ilim: %d, iin_ocw: %d, iin_ocf: %d (%d)\n", __func__,
		 pe26100_cp->ilim, iin_ocw, iin_ocf, ret);

	return ret;
}

static inline bool pe26100_cp_can_inc_ta_cur(struct pe26100_cp_charger *pe26100_cp)
{
	return pe26100_cp->ta_cur + PD_MSG_TA_CUR_STEP < min(pe26100_cp->ta_max_cur,
		pe26100_cp->iin_cc + PE26100_CP_TA_CUR_MAX_OFFSET);
}

/* Returns the enable or disable value. into 1 or 0. */
static int pe26100_cp_get_charging_enabled(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;
	uint8_t val;

	ret = pe26100_chg_reg_read(pe26100_cp->core, PE26100_CHG_MODE, &val);
	if (ret < 0)
		return ret;

	return (val & PE26100_CHG_MODE_PT_EN_MASK) != 0;
}

/* b/194346461 ramp down VOUT */
#define WLC_VOUT_CFG_STEP	40000

/* the caller will set to vbatt * 4 */
static int pe26100_cp_wlc_ramp_down_vout(struct pe26100_cp_charger *pe26100_cp,
				struct power_supply *wlc_psy)
{
	const int ramp_down_step = WLC_VOUT_CFG_STEP;
	union power_supply_propval pro_val;
	int iin_target = pe26100_cp->wlc_ramp_out_iin_target;
	int ret, iin, vout = 0;

	ret = power_supply_get_property(wlc_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&pro_val);
	if (ret < 0) {
		dev_err(pe26100_cp->dev, "%s: invalid vout %d\n", __func__, ret);
		return ret;
	}

	vout = pro_val.intval;

	while (true) {
		pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
		if (iin < 0) {
			dev_err(pe26100_cp->dev, "%s: invalid iin %d\n", __func__, iin);
			break;
		}

		if (iin <= iin_target) {
			dev_dbg(pe26100_cp->dev, "%s: reached target iin=%d, (target=%d)\n",
				__func__, iin, iin_target);
			return 0;
		}

		pro_val.intval = vout - ramp_down_step;
		dev_dbg(pe26100_cp->dev, "%s:iin=%d, wlc_vout=%d\n", __func__, iin, pro_val.intval);

		ret = power_supply_set_property(wlc_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW,	&pro_val);
		if (ret < 0) {
			dev_err(pe26100_cp->dev, "%s: cannot set vout %d\n", __func__, ret);
			break;
		}
		vout = pro_val.intval;

		msleep(pe26100_cp->wlc_ramp_out_delay);
	}

	return -EIO;
}

static bool pe26100_cp_check_fault(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;
	uint8_t fault, warn;

	ret = pe26100_chg_reg_read(pe26100_cp->core, PE26100_CHG_FLT_STATUS1, &fault);
	if (ret)
		return true;

	ret = pe26100_chg_reg_read(pe26100_cp->core, PE26100_CHG_WARN_STATUS1, &warn);
	if (ret)
		return true;

	if (pe26100_cp->ta_type == TA_TYPE_USBPD)
		ret = !!(fault & BIT(1) || fault & BIT(0) || warn & BIT(1) || warn & BIT(0));

	return ret;
}

static int pe26100_cp_set_status_charging(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = -EINVAL;
	ktime_t start_time = 0, end_time, elapsed_time;
	long long elapsed_ms;

	if (pe26100_cp->chg_mode == CHG_2TO1_DC_MODE) {
		ret = pe26100_chg_reg_write(pe26100_cp->core, 0xF6, 0xA5);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xF7, 0x96);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x8F, 0xC0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x4A, 0x8);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x92, 0xC0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x56, 0x12);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x6B, 0x87);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xA4, 0xEF);
		msleep(1);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE,
				     PE26100_CHG_MODE_EXTG_EN_MASK |
				     BIT(1));
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xA4, 0x0);

		start_time = ktime_get();
		ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE,
				     PE26100_CHG_MODE_EXTG_EN_MASK |
				     PE26100_CHG_MODE_PT_EN_MASK |
				     BIT(1));
		if (pe26100_is_es10(pe26100_cp->core))
			msleep(15);
		else
			msleep(35);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x8F, 0x0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x4A, 0x0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x92, 0x0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x56, 0x32);
	} else if (pe26100_cp->chg_mode == CHG_3TO1_DC_MODE) {
		start_time = ktime_get();
		ret = pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE,
					     PE26100_CHG_MODE_EXTG_EN_MASK |
					     (1 << PE26100_CHG_MODE_EXTGX_SHIFT) |
					     PE26100_CHG_MODE_PT_EN_MASK |
					     BIT(1));
		if (pe26100_is_es10(pe26100_cp->core))
			msleep(20);
		else
			msleep(40);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x8F, 0x0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x4A, 0x0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x92, 0x0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x56, 0x32);
	}

	if (ret)
		dev_err(pe26100_cp->dev, "%s: Error: %d\n", __func__, ret);

	end_time = ktime_get();
	elapsed_time = ktime_sub(end_time, start_time);
	elapsed_ms = ktime_to_ms(elapsed_time);
	dev_dbg(pe26100_cp->dev, "%s: elapsed_ms: %lld\n", __func__, elapsed_ms);


	if (pe26100_cp->mpp && pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
		pe26100_cp->ibus_ucp_disable_timestamp = get_boot_sec();
		pe26100_cp->iin_check_timestamp = get_boot_sec();
	}
	return ret;
}

static int pe26100_cp_check_not_active(struct pe26100_cp_charger *pe26100_cp, int loglevel)
{
	int ret = 0;
	bool eagain_condition;

	logbuffer_prlog(pe26100_cp, loglevel,
			"%s: 0x3c:%#02x, 0x3d:%#02x, 0x3e:%#02x, 0x3f:%#02x, 0x40:%#02x, 0x41:%#02x\n",
			__func__, pe26100_cp->safety_sts[0], pe26100_cp->safety_sts[1],
			pe26100_cp->safety_sts[2], pe26100_cp->safety_sts[3],
			pe26100_cp->safety_sts[4], pe26100_cp->safety_sts[5]);

	if (pe26100_cp->mpp && pe26100_cp->ta_type == TA_TYPE_WIRELESS)
		eagain_condition = !!(pe26100_cp->safety_sts[0] & BIT(0) ||
				      pe26100_cp->safety_sts[0] & BIT(2) ||
				      pe26100_cp->safety_sts[0] & BIT(3) ||
				      pe26100_cp->safety_sts[0] & BIT(6));
	else
		eagain_condition = !!(pe26100_cp->safety_sts[0] & BIT(1) ||
				      pe26100_cp->safety_sts[0] & BIT(2) ||
				      pe26100_cp->safety_sts[0] & BIT(3) ||
				      pe26100_cp->safety_sts[0] & BIT(6));

	if (eagain_condition) {
		pe26100_cp->error = PE26100_CP_ERROR_RETRY;
		ret = -EAGAIN;
	} else {
		pe26100_cp->error = PE26100_CP_ERROR_NOT_ACTIVE;
		ret = -EINVAL;
	}

	return ret;
}

static int pe26100_cp_set_status_disable_charging(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;

	ret = pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE, 0x0);
	return ret;
}

static int pe26100_cp_disable(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;

	ret = pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE, 0x0);
	ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IC_ENABLE, 0x0);

	if (ret)
		dev_info(pe26100_cp->dev, "%s: error disabling chip (%d)\n", __func__, ret);
	return ret;
}

/* call holding mutex_lock(&pe26100_cp->lock); */
static int pe26100_cp_set_charging(struct pe26100_cp_charger *pe26100_cp, bool enable)
{
	int ret;
	int retries = 3;

	dev_dbg(pe26100_cp->dev, "%s: enable=%d ta_type=%d\n", __func__,  enable,
		pe26100_cp->ta_type);

	if (enable && pe26100_cp_get_charging_enabled(pe26100_cp) == enable) {
		dev_dbg(pe26100_cp->dev, "%s: no op, already enabled\n", __func__);
		return 0;
	}

	if (enable) {
		while (retries) {
			ret = pe26100_cp_check_fault(pe26100_cp);
			if (!ret)
				break;

			msleep(500);
			retries--;
		}

		/* Start charging */
		ret = pe26100_cp_set_status_charging(pe26100_cp);
		if (ret < 0)
			goto error;
	} else {
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			struct power_supply *wlc_psy;

			wlc_psy = pe26100_cp_get_rx_psy(pe26100_cp);
			if (wlc_psy && !pe26100_cp->wlc_no_ramp_down) {
				int ret;

				ret = pe26100_cp_wlc_ramp_down_vout(pe26100_cp, wlc_psy);
				if (ret < 0)
					dev_err(pe26100_cp->dev, "cannot ramp out vout :%d\n", ret);

				msleep(1000);
			}
			pe26100_cp->wlc_no_ramp_down = 0;
		}

		/* turn off charging */
		ret = pe26100_cp_set_status_disable_charging(pe26100_cp);

		if (pe26100_cp->mpp && pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			dev_dbg(pe26100_cp->dev, "%s: setting mpp gpio to 0\n", __func__);
			pe26100_cp->pdata->mpp_gpio = devm_gpiod_get(pe26100_cp->dev,
									"pe26100,mpp",
									GPIOD_OUT_LOW);
			devm_gpiod_put(pe26100_cp->dev, pe26100_cp->pdata->mpp_gpio);
		}
	}

error:
	if (ret)
		dev_info(pe26100_cp->dev, "%s: Error: ret:%d\n", __func__, ret);

	return ret;
}

/* To do */
static bool pe26100_cp_err_is_retry(struct pe26100_cp_charger *pe26100_cp, bool *retries)
{
	*retries = !!pe26100_cp->eagain_retry_cnt;
	return pe26100_cp->error == PE26100_CP_ERROR_RETRY;
}

static bool pe26100_cp_err_is_low_batt(struct pe26100_cp_charger *pe26100_cp, bool *retries)
{
	*retries = !!pe26100_cp->low_batt_retry_cnt;
	return pe26100_cp->error == PE26100_CP_ERROR_LOW_VBATT;
}


int pe26100_cp_check_active(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;

	if (!(pe26100_cp->safety_sts[0] || pe26100_cp->safety_sts[1] || pe26100_cp->safety_sts[2]
	    || pe26100_cp->safety_sts[3]))
		ret = pe26100_chg_reg_readn(pe26100_cp->core, PE26100_CHG_FLT_STATUS1,
				    pe26100_cp->safety_sts, sizeof(pe26100_cp->safety_sts));
	if (ret < 0) {
		dev_err(pe26100_cp->dev, "Error %d reading PE26100_CHG_IC_STATUS1\n", ret);
		return ret;
	}

	return !(pe26100_cp->safety_sts[4] & BIT(0)) && !!(pe26100_cp->safety_sts[5] & BIT(2));
}

static bool pe26100_cp_check_iin_active_timeout(struct pe26100_cp_charger *pe26100_cp,
						ktime_t check_ts, int iin)
{
	const ktime_t now = get_boot_sec();

	if (pe26100_cp->no_iin_active_check)
		return true;

	if (check_ts && now - check_ts >= PE26100_CP_IIN_ACTIVE_TIMEOUT && iin == 0) {
		dev_info(pe26100_cp->dev, "%s: iin 0 even after timeout %d secs\n",
			 __func__, PE26100_CP_IIN_ACTIVE_TIMEOUT);
		pe26100_cp->iin_check_timestamp = 0;
		return false;
	}

	return true;
}

static bool pe26100_cp_check_ibus_ucp_enable(struct pe26100_cp_charger *pe26100_cp,
					     bool active, ktime_t disable_ts, int iin)
{
	const ktime_t now = get_boot_sec();

	if (disable_ts && active && now - disable_ts >= IBUS_UCP_ENABLE_TIMEOUT) {
		dev_info(pe26100_cp->dev, "%s: enabling ibus_ucp\n", __func__);

		/* Enable IIN_UCF */
		pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_FLT_MASK1, 0);
		return true;
	}

	return false;
}

/*
 * Check Active status, 0 is active (or in RCP), <0 indicates a problem.
 * The function is called from different contexts/functions, errors are fatal
 * (i.e. stop charging) from all contexts except when this is called from
 * pe26100_cp_check_active_state().
 *
 * Other contexts:
 * . pe26100_cp_charge_adjust_ccmode
 * . pe26100_cp_charge_ccmode
 * . pe26100_cp_charge_start_cvmode
 * . pe26100_cp_charge_cvmode
 * . pe26100_cp_adjust_ta_voltage
 * . pe26100_cp_adjust_rx_voltage
 * . pe26100_cp_adjust_ta_current
 * call holding mutex_lock(&pe26100_cp->lock)
 */
static int pe26100_cp_check_error(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = -EINVAL, vbatt;
	bool active;
	int iin;

	ret = pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
	active = pe26100_cp_check_active(pe26100_cp) == 1;
	active &= pe26100_cp_check_iin_active_timeout(pe26100_cp, pe26100_cp->iin_check_timestamp,
						      iin);
	if (pe26100_cp_check_ibus_ucp_enable(pe26100_cp, active,
					     pe26100_cp->ibus_ucp_disable_timestamp, iin))
		pe26100_cp->ibus_ucp_disable_timestamp = 0;

	/* PE26100_CP is active state */
	if (active) {
		pe26100_cp->error = PE26100_CP_ERROR_NONE;
		pe26100_cp->low_batt_retry_cnt = PE26100_CP_MAX_LOW_BATT_RETRY_CNT;
		dev_dbg(pe26100_cp->dev, "%s: Active Status ok.\n", __func__);

		return 0;
	}

	/* PE26100_CP is charging */
	/* Check whether the battery voltage is over the minimum */
	ret = pe26100_chg_read_vbatt(pe26100_cp->core, &vbatt);
	if (ret) {
		dev_err(pe26100_cp->dev, "%s: Error %d reading vbatt\n", __func__, ret);
		return ret;
	}

	if (vbatt <= PE26100_CP_DC_VBAT_MIN)
		/* Abnormal battery level */
		dev_err(pe26100_cp->dev, "%s: Error abnormal battery voltage=%d\n",	__func__,
			vbatt);

	ret = pe26100_cp_check_not_active(pe26100_cp, LOGLEVEL_ERR);

	/*
	 * Sometimes battery driver might call set_property function
	 * to stop charging during msleep. At this case, charging
	 * state would change DC_STATE_NO_CHARGING. PE26100_CP should
	 * stop checking RCP condition and exit timer_work
	 */
	if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING)
		dev_err(pe26100_cp->dev, "%s: other driver forced stop\n", __func__);

	dev_dbg(pe26100_cp->dev, "%s: Not Active Status=%d\n", __func__, ret);
	return ret;
}

static int pe26100_cp_get_icn(struct pe26100_cp_charger *pe26100_cp, int *icn)
{
	int temp, ret;

	ret = pe26100_chg_current_now(pe26100_cp->core, &temp, PE26100_CHG_MODE_CP);
	if (ret < 0)
		return ret;

	*icn = conv_chg_mode(pe26100_cp, temp);
	return 0;
}

/* only needed for logging */
static int pe26100_cp_get_batt_info(struct pe26100_cp_charger *pe26100_cp, int info_type, int *info)
{
	union power_supply_propval val;
	enum power_supply_property psp;
	int ret;

	if (!pe26100_cp->batt_psy)
		pe26100_cp->batt_psy = power_supply_get_by_name("battery");
	if (!pe26100_cp->batt_psy)
		return -EINVAL;

	if (info_type == BATT_CURRENT)
		psp = POWER_SUPPLY_PROP_CURRENT_NOW;
	else
		psp = POWER_SUPPLY_PROP_VOLTAGE_NOW;

	ret = power_supply_get_property(pe26100_cp->batt_psy, psp, &val);
	if (ret == 0)
		*info = val.intval;

	return ret;
}

/* only needed for logging */
static int pe26100_cp_get_ibatt(struct pe26100_cp_charger *pe26100_cp, int *info)
{
	return pe26100_cp_get_batt_info(pe26100_cp, BATT_CURRENT, info);
}

static int pe26100_cp_get_current_adcs(struct pe26100_cp_charger *pe26100_cp, int *pibat, int *picn,
				       int *piin)
{
	int rc = pe26100_cp_get_ibatt(pe26100_cp, pibat);

	if (rc)
		goto error;

	rc = pe26100_cp_get_icn(pe26100_cp, picn);
	if (rc)
		goto error;

	rc = pe26100_chg_current_now(pe26100_cp->core, piin, PE26100_CHG_MODE_CP);
	if (rc)
		goto error;

	return 0;

error:
	logbuffer_prlog(pe26100_cp, LOGLEVEL_ERR, "%s: Error: rc=%d", __func__, rc);
	return rc;
}

static void pe26100_cp_prlog_state(struct pe26100_cp_charger *pe26100_cp, const char *fn)
{
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;
	int  vbat;

	rc = pe26100_chg_read_vbatt(pe26100_cp->core, &vbat);

	rc |= pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		goto error;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: vbat=%d, iin=%d, iin_cc=%d, icn=%d ibat=%d, cc_max=%d rc=%d",
			fn, vbat, iin, pe26100_cp->iin_cc, icn, ibat, pe26100_cp->cc_max, rc);
	return;

error:
	dev_info(pe26100_cp->dev, "Error reading ibatt or icn: rc: %d, ibatt: %d, icn: %d\n",
		 rc, ibat, icn);
}

static int pe26100_cp_read_status(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;
	int vbat, iin;

	ret = pe26100_cp_get_batt_info(pe26100_cp, BATT_VOLTAGE, &vbat);
	if (ret)
		return ret;

	ret = pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
	if (ret)
		return ret;

	if (iin >= pe26100_cp->iin_reg)
		ret = STS_MODE_IIN_LOOP;
	else if (vbat >= pe26100_cp->vfloat_reg)
		ret = STS_MODE_VFLT_LOOP;
	else
		ret = STS_MODE_LOOP_INACTIVE;

	return ret;
}

/* Return the constant charge voltage programmed into the charger in uV. */
static int pe26100_cp_const_charge_voltage(struct pe26100_cp_charger *pe26100_cp)
{
	if (!pe26100_cp->mains_online)
		return -ENODATA;

	return pe26100_cp->vfloat_reg;
}

static int pe26100_cp_check_status(struct pe26100_cp_charger *pe26100_cp)
{
	int icn = -EINVAL, ibat = -EINVAL, vbat = -EINVAL;
	int rc = 0, status;

	status = pe26100_cp_read_status(pe26100_cp);
	if (status < 0)
		goto error;

	rc = pe26100_cp_get_icn(pe26100_cp, &icn);
	if (rc)
		goto error;

	rc = pe26100_cp_get_batt_info(pe26100_cp, BATT_CURRENT, &ibat);
	if (rc)
		goto error;

	rc = pe26100_cp_get_batt_info(pe26100_cp, BATT_VOLTAGE, &vbat);

error:
	dev_dbg(pe26100_cp->dev, "%s: status=%d rc=%d icn:%d ibat:%d delta_c=%d, vbat:%d, fv:%d, cc_max:%d\n",
		 __func__, status, rc, icn, ibat, icn - ibat, vbat,
		 pe26100_cp->fv_uv, pe26100_cp->cc_max);

	return status;
}

/* hold mutex_lock(&pe26100_cp->lock); */
static int pe26100_cp_recover_ta(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;

	if (pe26100_cp->ftm_mode)
		return 0;

	if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
		pe26100_cp->ta_vol = 0; /* set to a value to change rx vol */
		ret = pe26100_cp_send_rx_voltage(pe26100_cp, MSG_REQUEST_FIXED_PDO);
	} else {
		/* TODO: recover TA to value before handoff, or use DT */
		pe26100_cp->ta_vol = 9000000;
		pe26100_cp->ta_cur = 2200000;
		pe26100_cp->ta_objpos = 1; /* PDO1 - fixed 5V */
		ret = pe26100_cp_send_pd_message(pe26100_cp, MSG_REQUEST_FIXED_PDO);
	}

	/* will not be able to recover if TA is offline */
	if (ret < 0)
		dev_dbg(pe26100_cp->dev, "%s: cannot recover TA (%d)\n", __func__, ret);

	return 0;
}

/* Stop Charging */
static int pe26100_cp_stop_charging(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;

	/* mark the end with \n in logbuffer */
	logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
			"%s: pe26100_cp->charging_state=%d ret=%d\n",
			__func__, pe26100_cp->charging_state, ret);

	mutex_lock(&pe26100_cp->lock);

	/* Check the current state */
	if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING)
		goto done;

	/* Stop Direct charging  */
	cancel_delayed_work(&pe26100_cp->timer_work);
	cancel_delayed_work(&pe26100_cp->pps_work);
	pe26100_cp->timer_id = TIMER_ID_NONE;
	pe26100_cp->timer_period = 0;

	/* Clear parameter */
	if (pe26100_cp->charging_state != DC_STATE_ERROR)
		pe26100_cp->charging_state = DC_STATE_NO_CHARGING;
	pe26100_cp->ret_state = DC_STATE_NO_CHARGING;
	pe26100_cp->prev_iin = 0;
	pe26100_cp->prev_inc = INC_NONE;
	pe26100_cp->chg_mode = CHG_NO_DC_MODE;

	/* restore to config */
	pe26100_cp->pdata->iin_cfg = pe26100_cp->pdata->iin_cfg_max;

	/*
	 * Clear charging configuration
	 * TODO: use defaults when these are negative or zero at startup
	 * NOTE: cc_max is twice of IIN + headroom
	 */
	if (!pe26100_cp->maintain_fv_cc_max) {
		pe26100_cp->cc_max = -1;
		pe26100_cp->fv_uv = -1;

		/* Clear requests for new Vfloat and new IIN */
		pe26100_cp->new_vfloat = 0;
		pe26100_cp->new_iin = 0;
	}

	if (pe26100_cp->maintain_fv_cc_max)
		pe26100_cp->maintain_fv_cc_max = false;

	/* Clear requests for new Vfloat and new IIN */
	pe26100_cp->new_vfloat = 0;
	pe26100_cp->new_iin = 0;

	/* used to start DC and during errors */
	pe26100_cp->retry_cnt = 0;

	pe26100_cp->prev_ta_cur = 0;
	pe26100_cp->prev_ta_vol = 0;
	pe26100_cp->no_inc_ta_vol = 0;

	/* close stats */
	pe26100_cp_chg_stats_done(&pe26100_cp->chg_data, pe26100_cp);
	pe26100_cp_chg_stats_dump(pe26100_cp);

	/* TODO: something here to prep TA for the switch */

	ret = pe26100_cp_set_charging(pe26100_cp, false);
	if (ret < 0)
		dev_err(pe26100_cp->dev, "%s: Error-set_charging(main)\n", __func__);

	ret = pe26100_cp_disable(pe26100_cp);

	/* stop charging and recover TA voltage */
	if (pe26100_cp->mains_online == true)
		pe26100_cp_recover_ta(pe26100_cp);

	power_supply_changed(pe26100_cp->mains);

done:
	pe26100_cp_set_ta_type(pe26100_cp, 0);
	pe26100_chg_set_online(pe26100_cp->core, 0, PE26100_CHG_MODE_CP);
	mutex_unlock(&pe26100_cp->lock);
	__pm_relax(pe26100_cp->monitor_wake_lock);
	dev_dbg(pe26100_cp->dev, "%s: END, ret=%d\n", __func__, ret);
	return ret;
}

#define FCC_TOLERANCE_RATIO		99
#define FCC_POWER_INCREASE_THRESHOLD	99

/*
 * Compensate TA current for the target input current called from
 * pe26100_cp_charge_ccmode() when loop becomes not active.
 *
 * pe26100_cp_charge_ccmode() ->
 *	-> pe26100_cp_set_rx_voltage_comp()
 *	-> pe26100_cp_set_ta_voltage_comp()
 *	-> pe26100_cp_set_ta_current_comp2()
 *
 * NOTE: call holding mutex_lock(&pe26100_cp->lock);
 */
static int pe26100_cp_set_ta_current_comp(struct pe26100_cp_charger *pe26100_cp)
{
	const int iin_high = pe26100_cp->iin_cc + pe26100_cp->pdata->iin_cc_comp_offset_high;
	const int iin_low = pe26100_cp->iin_cc - pe26100_cp->pdata->iin_cc_comp_offset_low;
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;

	/* IIN = IBAT+SYSLOAD */
	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=[%d,%d,%d], icn=%d ibat=%d, cc_max=%d rc=%d prev_iin=%d",
			__func__, iin, iin_low, pe26100_cp->iin_cc, iin_high,
			icn, ibat, pe26100_cp->cc_max, rc,
			pe26100_cp->prev_iin);
	if (iin < 0)
		return iin;

	/* Compare IIN ADC with target input current */
	if (iin > iin_high) {

		/* TA current is higher than the target input current */
		if (pe26100_cp->ta_cur > pe26100_cp->iin_cc) {
			/* TA current is over than IIN_CC */
			/* Decrease TA current (50mA) */
			pe26100_cp->ta_cur = pe26100_cp->ta_cur - PD_MSG_TA_CUR_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont1: ta_cur=%u",
					pe26100_cp->ta_cur);

		/* TA current is already less than IIN_CC */
		/* Compara IIN_ADC with the previous IIN_ADC */
		} else if (iin < (pe26100_cp->prev_iin - PE26100_CP_IIN_ADC_OFFSET)) {
			/* Assume that TA operation mode is CV mode */
			/* Decrease TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol - PD_MSG_TA_VOL_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2-1: ta_vol=%u",
					pe26100_cp->ta_vol);
		} else {
			/* Assume TA operation mode is CL mode */
			/* Decrease TA current (50mA) */
			pe26100_cp->ta_cur = pe26100_cp->ta_cur - PD_MSG_TA_CUR_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2-2: ta_cur=%u",
					pe26100_cp->ta_cur);
		}

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

	} else if (iin < iin_low) {

		/* compare IIN ADC with previous IIN ADC + 20mA */
		if (iin > (pe26100_cp->prev_iin + PE26100_CP_IIN_ADC_OFFSET)) {
			/*
			 * TA voltage is not enough to supply the operating
			 * current of RDO: increase TA voltage
			 */

			/* Compare TA max voltage */
			if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
				/* TA voltage is already the maximum voltage */
				/* Compare TA max current */
				if (!pe26100_cp_can_inc_ta_cur(pe26100_cp)) {
					/* TA voltage and current are at max */
					logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
							"End1: ta_vol=%u, ta_cur=%u",
							pe26100_cp->ta_vol, pe26100_cp->ta_cur);

					/* Set timer */
					pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
					pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
				} else {
					/* Increase TA current (50mA) */
					pe26100_cp->ta_cur = pe26100_cp->ta_cur +
							     PD_MSG_TA_CUR_STEP;

					logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
							"Cont3: ta_cur=%u",
							pe26100_cp->ta_cur);

					/* Send PD Message */
					pe26100_cp->timer_id = TIMER_PDMSG_SEND;
					pe26100_cp->timer_period = 0;

					/* Set TA increment flag */
					pe26100_cp->prev_inc = INC_TA_CUR;
				}
			} else {
				/* Increase TA voltage (20mV) */
				pe26100_cp->ta_vol = pe26100_cp->ta_vol + PD_MSG_TA_VOL_STEP;
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"Cont4: ta_vol=%u", pe26100_cp->ta_vol);

				/* Send PD Message */
				pe26100_cp->timer_id = TIMER_PDMSG_SEND;
				pe26100_cp->timer_period = 0;

				/* Set TA increment flag */
				pe26100_cp->prev_inc = INC_TA_VOL;
			}

		/* TA current is lower than the target input current */
		/* Check the previous TA increment */
		} else if (pe26100_cp->prev_inc == INC_TA_VOL) {
			/*
			 * The previous increment is TA voltage, but
			 * input current does not increase.
			 */

			/* Try to increase TA current */
			/* Compare TA max current */
			if (!pe26100_cp_can_inc_ta_cur(pe26100_cp)) {

				/* TA current is already the maximum current */
				/* Compare TA max voltage */
				if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
					/*
					 * TA voltage and current are already
					 * the maximum values
					 */
					logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
							"End2: ta_vol=%u, ta_cur=%u",
							pe26100_cp->ta_vol, pe26100_cp->ta_cur);

					pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
					pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
				} else {
					/* Increase TA voltage (20mV) */
					pe26100_cp->ta_vol = pe26100_cp->ta_vol +
							     PD_MSG_TA_VOL_STEP;
					logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
							"Cont5: ta_vol=%u",
							pe26100_cp->ta_vol);

					/* Send PD Message */
					pe26100_cp->timer_id = TIMER_PDMSG_SEND;
					pe26100_cp->timer_period = 0;

					/* Set TA increment flag */
					pe26100_cp->prev_inc = INC_TA_VOL;
				}
			} else {
				const unsigned int ta_cur = pe26100_cp->ta_cur +
							    PD_MSG_TA_CUR_STEP;

				/* Increase TA current (50mA) */
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"Cont6: ta_cur=%u->%u",
						pe26100_cp->ta_cur, ta_cur);

				pe26100_cp->ta_cur = pe26100_cp->ta_cur + PD_MSG_TA_CUR_STEP;
				pe26100_cp->timer_id = TIMER_PDMSG_SEND;
				pe26100_cp->timer_period = 0;

				pe26100_cp->prev_inc = INC_TA_CUR;
			}

		/*
		 * The previous increment was TA current, but input current
		 * did not increase. Try to increase TA voltage.
		 */
		} else if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
			/* TA voltage is already the maximum voltage */

			/* Compare TA maximum current */
			if (!pe26100_cp_can_inc_ta_cur(pe26100_cp)) {
				/*
				 * TA voltage and current are already at the
				 * maximum values
				 */
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"End3: ta_vol=%u, ta_cur=%u",
						 pe26100_cp->ta_vol, pe26100_cp->ta_cur);

				pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
				pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
			} else {
				/* Increase TA current (50mA) */
				pe26100_cp->ta_cur = pe26100_cp->ta_cur + PD_MSG_TA_CUR_STEP;
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"Cont7: ta_cur=%u", pe26100_cp->ta_cur);

				/* Send PD Message */
				pe26100_cp->timer_id = TIMER_PDMSG_SEND;
				pe26100_cp->timer_period = 0;

				/* Set TA increment flag */
				pe26100_cp->prev_inc = INC_TA_CUR;
			}
		} else {
			/* Increase TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol + PD_MSG_TA_VOL_STEP;

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"Comp. Cont8: ta_vol=%u->%u",
					pe26100_cp->ta_vol, pe26100_cp->ta_vol);

			/* Send PD Message */
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;

			/* Set TA increment flag */
			pe26100_cp->prev_inc = INC_TA_VOL;
		}

	} else {
		/* IIN ADC is in valid range */
		/* IIN_CC - 50mA < IIN ADC < IIN_CC + 50mA  */
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"Comp. End4(valid): ta_vol=%u, ta_cur=%u",
				pe26100_cp->ta_vol, pe26100_cp->ta_cur);
		/* Set timer */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;

		/* b/186969924: reset increment state on valid */
		pe26100_cp->prev_inc = INC_NONE;
	}

	/* Save previous iin adc */
	pe26100_cp->prev_iin = iin;
	return 0;
}

/* Compensate TA current for constant power mode */
/* hold mutex_lock(&pe26100_cp->lock), schedule on return 0 */
static int pe26100_cp_set_ta_current_comp2(struct pe26100_cp_charger *pe26100_cp)
{
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;

	/* IIN = IBAT+SYSLOAD */
	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=[%d,%d,%d], iin_cfg=%d icn=%d ibat=%d, cc_max=%d rc=%d",
			__func__, iin,
			pe26100_cp->iin_cc - PE26100_CP_IIN_CC_COMP_OFFSET_CP,
			pe26100_cp->iin_cc,
			pe26100_cp->iin_cc + PE26100_CP_IIN_CC_COMP_OFFSET_CP,
			pe26100_cp->pdata->iin_cfg,
			icn, ibat, pe26100_cp->cc_max, rc);
	if (iin < 0)
		return iin;

	/* Compare IIN ADC with target input current */
	if (iin > (pe26100_cp->pdata->iin_cfg + pe26100_cp->pdata->iin_cc_comp_offset_high)) {
		/* TA current is higher than the target input current limit */
		pe26100_cp->ta_cur = pe26100_cp->ta_cur - PD_MSG_TA_CUR_STEP;

		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	} else if (iin < (pe26100_cp->iin_cc - PE26100_CP_IIN_CC_COMP_OFFSET_CP)) {

		/* TA current is lower than the target input current */
		/* IIN_ADC < IIN_CC -20mA */
		if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
			const int iin_cc_lb = pe26100_cp->iin_cc -
				      pe26100_cp->pdata->iin_cc_comp_offset_low;

			/* Check IIN_ADC < lb */
			if (iin < iin_cc_lb) {
				unsigned int ta_max_vol = 0;
				unsigned int iin_apdo;
				unsigned int val;

				if (pe26100_cp->chg_mode == CHG_2TO1_DC_MODE)
					ta_max_vol =
					  pe26100_cp->pdata->ta_max_vol_2_1 * CHG_2TO1_DC_MODE;
				else if (pe26100_cp->chg_mode == CHG_3TO1_DC_MODE)
					ta_max_vol = pe26100_cp->pdata->ta_max_vol_3_1;

				/* Set new IIN_CC to lb */
				pe26100_cp->iin_cc = iin_cc_lb;

				/* Set new TA_MAX_VOL to TA_MAX_PWR/IIN_CC */
				/* Adjust new IIN_CC with APDO resolution */
				iin_apdo = pe26100_cp->iin_cc / PD_MSG_TA_CUR_STEP;
				iin_apdo = iin_apdo * PD_MSG_TA_CUR_STEP;
				/* in mV */
				if (iin_apdo == 0) {
					dev_warn(pe26100_cp->dev, "Comp.: iin_apdo too low (0), stop comp\n");
					return -EINVAL;
				}
				val = (pe26100_cp->ta_max_pwr * 1000) / iin_apdo;
				/* Adjust values with APDO resolution(20mV) */
				val = val * 1000 / PD_MSG_TA_VOL_STEP;
				val = val * PD_MSG_TA_VOL_STEP; /* uV */

				/* Set new TA_MAX_VOL */
				pe26100_cp->ta_max_vol = min(val, ta_max_vol);

				/* Increase TA voltage(40mV) */
				pe26100_cp->ta_vol = pe26100_cp->ta_vol + PD_MSG_TA_VOL_STEP * 2;

				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"Cont1: ta_vol=%u",
						pe26100_cp->ta_vol);

				/* Send PD Message */
				pe26100_cp->timer_id = TIMER_PDMSG_SEND;
				pe26100_cp->timer_period = 0;
			} else {
				/* Wait for next current step compensation */
				/* lb < IIN ADC < IIN_CC - 20mA */
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"Comp.(wait): ta_vol=%u",
						pe26100_cp->ta_vol);

				/* Set timer */
				pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
				pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK2_T;
			}
		} else {
			/* Increase TA voltage(40mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol + PD_MSG_TA_VOL_STEP * 2;
			if (pe26100_cp->ta_vol > pe26100_cp->ta_max_vol)
				pe26100_cp->ta_vol = pe26100_cp->ta_max_vol;

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2: ta_vol=%u",
					pe26100_cp->ta_vol);

			/* Send PD Message */
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;
		}
	} else {
		/* IIN ADC is in valid range */
		/* IIN_CC - 50mA < IIN ADC < IIN_CFG + 50mA */
		dev_dbg(pe26100_cp->dev, "End(valid): ta_vol=%u\n", pe26100_cp->ta_vol);

		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK2_T;

		/* b/186969924: reset increment state on valid */
		pe26100_cp->prev_inc = INC_NONE;
	}

	/* Save previous iin adc */
	pe26100_cp->prev_iin = iin;
	return 0;
}

/* Compensate TA voltage for the target input current */
/* hold mutex_lock(&pe26100_cp->lock), schedule on return 0 */
static int pe26100_cp_set_ta_voltage_comp(struct pe26100_cp_charger *pe26100_cp)
{
	const int iin_high = pe26100_cp->iin_cc + pe26100_cp->pdata->iin_cc_comp_offset_high;
	const int iin_low = pe26100_cp->iin_cc - pe26100_cp->pdata->iin_cc_comp_offset_low;
	const int ibat_limit = (pe26100_cp->cc_max * FCC_POWER_INCREASE_THRESHOLD) / 100;
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: = charging_state=%u ==\n", __func__,
		pe26100_cp->charging_state);

	/* IIN = IBAT+SYSLOAD */
	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=[%d,%d,%d], icn=%d ibat=%d, cc_max=%d rc=%d",
			__func__, iin, iin_low, pe26100_cp->iin_cc, iin_high,
			icn, ibat, pe26100_cp->cc_max, rc);

	if (iin < 0)
		return iin;

	/* Compare IIN ADC with target input current */
	if (iin > iin_high) {
		/* TA current is higher than the target input current */
		/* Decrease TA voltage (20mV) */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol - PD_MSG_TA_VOL_STEP;
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont1: ta_vol=%u",
				pe26100_cp->ta_vol);

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

	} else if (iin < iin_low) {

		/* TA current is lower than the target input current */
		/* Compare TA max voltage */
		if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
			/* TA is already at maximum voltage */
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End1(max TA vol): ta_vol=%u",
					pe26100_cp->ta_vol);

			/* Set timer */
			/* Check the current charging state */
			if (pe26100_cp->charging_state == DC_STATE_CC_MODE) {
				/* CC mode */
				pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
				pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
			} else {
				/* CV mode */
				pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
				pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
			}
		} else {
			const unsigned int ta_vol = pe26100_cp->ta_vol;

			/* Increase TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol + PD_MSG_TA_VOL_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2: ta_vol:%u->%u",
					ta_vol, pe26100_cp->ta_vol);

			/* Send PD Message */
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;
		}
	} else {
		/* IIN ADC is in valid range */
		/* IIN_CC - 50mA < IIN ADC < IIN_CC + 50mA  */
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"End(valid): ta_vol=%u low_ibat=%d\n",
				pe26100_cp->ta_vol, ibat < ibat_limit);

		/* Check the current charging state */
		if (pe26100_cp->charging_state == DC_STATE_CC_MODE) {
			pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
			pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
		} else {
			pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
			pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
		}
	}

	return 0;
}

/* hold mutex_lock(&pe26100_cp->lock), schedule on return 0 */
static int pe26100_cp_set_rx_voltage_comp(struct pe26100_cp_charger *pe26100_cp)
{
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);

	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=[%d,%d,%d], icn=%d ibat=%d, cc_max=%d rc=%d",
			__func__, iin,
			pe26100_cp->iin_cc - pe26100_cp->pdata->iin_cc_comp_offset_low,
			pe26100_cp->iin_cc,
			pe26100_cp->iin_cc + pe26100_cp->pdata->iin_cc_comp_offset_high,
			icn, ibat, pe26100_cp->cc_max, rc);
	if (iin < 0)
		return iin;

	/* Compare IIN ADC with target input current */
	if (iin > (pe26100_cp->iin_cc + pe26100_cp->pdata->iin_cc_comp_offset_high)) {

		/* RX current is higher than the target input current */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol - pe26100_cp->pdata->wcrx_vol_down_step;
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont1: rx_vol=%u",
				pe26100_cp->ta_vol);

		/* Set RX Voltage */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

	} else if (iin < (pe26100_cp->iin_cc - pe26100_cp->pdata->iin_cc_comp_offset_low)) {

		/* RX current is lower than the target input current */
		/* Compare RX max voltage */
		if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {

			/* TA current is already the maximum voltage */
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"End1(max RX vol): rx_vol=%u",
					pe26100_cp->ta_vol);

			/* Check the current charging state */
			if (pe26100_cp->charging_state == DC_STATE_CC_MODE) {
				/* CC mode */
				pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
				pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
			} else {
				/* CV mode */
				pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
				pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
			}
		} else {
			/* Increase RX voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol +
					     pe26100_cp->pdata->wcrx_vol_up_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2: rx_vol=%u",
					pe26100_cp->ta_vol);

			/* Set RX Voltage */
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;
		}
	} else {
		/* IIN ADC is in valid range */
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End(valid): rx_vol=%u",
				pe26100_cp->ta_vol);

		if (pe26100_cp->charging_state == DC_STATE_CC_MODE) {
			pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
			pe26100_cp->timer_period = PE26100_CP_CCMODE_CHECK1_T;
		} else {
			pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
			pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
		}
	}

	return 0;
}

/*
 * max iin given cc_max and iin_cfg.
 * TODO: maybe use pdata->iin_cfg if cc_max is zero or negative.
 */
static int pe26100_cp_get_iin_max(const struct pe26100_cp_charger *pe26100_cp, int cc_max)
{
	int cc_limit = pe26100_cp->pdata->iin_max_offset + cc_max / conv_chg_mode(pe26100_cp, 1);
	int iin_max;

	if (pe26100_cp->chg_mode == CHG_3TO1_DC_MODE)
		cc_limit += 15000;

	iin_max = min_t(unsigned int, pe26100_cp->pdata->iin_cfg_max, cc_limit);

	dev_dbg(pe26100_cp->dev, "%s: iin_max=%d iin_cfg=%u iin_cfg_max=%d cc_max=%d cc_limit=%d\n",
		 __func__, iin_max, pe26100_cp->pdata->iin_cfg,
		 pe26100_cp->pdata->iin_cfg_max, cc_max, cc_limit);

	return iin_max;
}

/*
 * iin limit for the adapter for the chg_mode
 * Minimum between the configuration, cc_max (scaled with offset) and the
 * adapter capabilities.
 */
static int pe26100_cp_get_iin_limit(const struct pe26100_cp_charger *pe26100_cp)
{
	int iin_cc;

	iin_cc = pe26100_cp_get_iin_max(pe26100_cp, pe26100_cp->cc_max);
	if (pe26100_cp->ta_max_cur < iin_cc)
		iin_cc = pe26100_cp->ta_max_cur;

	dev_dbg(pe26100_cp->dev, "%s: iin_cc=%d ta_max_cur=%u, chg_mode=%d\n", __func__,
		 iin_cc, pe26100_cp->ta_max_cur, pe26100_cp->chg_mode);

	return iin_cc;
}

/* recalculate ->ta_vol looking at demand (cc_max) */
static int pe26100_cp_set_wireless_dc(struct pe26100_cp_charger *pe26100_cp, int vbat)
{
	unsigned long long val;

	int iin_cc = pe26100_cp_get_iin_limit(pe26100_cp);

	if (pe26100_cp->mpp) {
		int power_tgt = pe26100_cp->power;

		/* Don't recalculate ta_vol on tier switch */
		if (pe26100_cp->charging_state == DC_STATE_PRESET_DC) {
			val = (pe26100_cp->init_vol_mult * (unsigned long long)vbat)
			      + (pe26100_cp->init_vol_offset * 1000000);
			pe26100_cp->ta_vol = val / 1000;

			pe26100_cp->iin_cc = iin_cc;
		} else if (!power_tgt) {
			pe26100_cp->iin_cc = iin_cc;
		} else if (!pe26100_cp->no_inc_ta_vol || iin_cc <= pe26100_cp->iin_cc) {
			pe26100_cp->iin_cc = iin_cc;
		}
	} else {
		pe26100_cp->ta_vol = 3 * vbat + PE26100_CP_WLC_VOL_PRE_OFFSET;
		pe26100_cp->iin_cc = iin_cc;
	}

	/* RX voltage resolution is 100mV */
	val = pe26100_cp->ta_vol / WCRX_VOL_STEP_SIZE;
	pe26100_cp->ta_vol = val * WCRX_VOL_STEP_SIZE;
	/* Set RX voltage to MIN[RX voltage, RX_MAX_VOL*chg_mode] */
	pe26100_cp->ta_vol = min(pe26100_cp->ta_vol, pe26100_cp->ta_max_vol);
	pe26100_cp->prev_ta_vol = pe26100_cp->ta_vol;

	/* ta_cur is ignored */
	logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
			"%s: iin_cc=%d, vbat=%d, ta_vol=%d ta_max_vol=%d", __func__,
			pe26100_cp->iin_cc, vbat, pe26100_cp->ta_vol, pe26100_cp->ta_max_vol);

	return 0;
}

/* recalculate ->ta_vol and ->ta_cur looking at demand (cc_max) */
static int pe26100_cp_set_wired_dc(struct pe26100_cp_charger *pe26100_cp, int vbat)
{
	const unsigned long ta_max_vol = pe26100_cp->ta_max_vol;
	unsigned long val;
	int iin_cc;

	pe26100_cp->iin_cc = pe26100_cp_get_iin_limit(pe26100_cp);
	/* Update OCP_WARN_THRES as chg_mode might have changed to 2:1 */
	pe26100_cp_set_input_current(pe26100_cp, pe26100_cp->iin_cc);
	pe26100_cp->pdata->iin_cfg = pe26100_cp->iin_cc;

	/* Calculate new TA max voltage, current */
	val = pe26100_cp->iin_cc / PD_MSG_TA_CUR_STEP;
	iin_cc = val * PD_MSG_TA_CUR_STEP;

	if (iin_cc == 0) {
		dev_warn(pe26100_cp->dev, "%s: iin_cc is 0, cannot calc ta_max_pwr\n", __func__);
		return -EINVAL;
	}
	val = (pe26100_cp->ta_max_pwr * 1000) / iin_cc; /* mV */

	/* Adjust values with APDO resolution(20mV) */
	val = val * 1000 / PD_MSG_TA_VOL_STEP;
	val = val * PD_MSG_TA_VOL_STEP; /* uV */
	pe26100_cp->ta_max_vol = min(val, ta_max_vol);

	pe26100_cp->ta_vol = 2 * vbat + PE26100_CP_TA_VOL_PRE_OFFSET;

	/* PPS voltage resolution is 20mV */
	val = pe26100_cp->ta_vol / PD_MSG_TA_VOL_STEP;
	pe26100_cp->ta_vol = val * PD_MSG_TA_VOL_STEP;
	pe26100_cp->ta_vol = min(pe26100_cp->ta_vol, pe26100_cp->ta_max_vol);

	pe26100_cp->ta_cur = min((int)pe26100_cp->ta_max_cur,
				 iin_cc + PE26100_CP_TA_CUR_MAX_OFFSET);

	logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
			"%s: iin_cc=%d, ta_vol=%d ta_cur=%d ta_max_vol=%d vbat=%d",
			__func__, pe26100_cp->iin_cc, pe26100_cp->ta_vol, pe26100_cp->ta_cur,
			pe26100_cp->ta_max_vol, vbat);

	return 0;
}

/*
 * like pe26100_cp_preset_dcmode() but will not query the TA.
 * Called from timer:
 * [pe26100_cp_charge_ccmode | pe26100_cp_charge_cvmode] ->
 *	pe26100_cp_apply_new_iin() ->
 *		pe26100_cp_adjust_ta_current() ->
 *			pe26100_cp_reset_dcmode()
 *	pe26100_cp_apply_new_vfloat() ->
 *		pe26100_cp_reset_dcmode()
 *
 * NOTE: caller holds mutex_lock(&pe26100_cp->lock);
 */
static int pe26100_cp_reset_dcmode(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = -EINVAL, vbat;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: = charging_state=%u ==\n", __func__,
		 pe26100_cp->charging_state);

	if (pe26100_cp->cc_max < 0) {
		dev_err(pe26100_cp->dev, "%s: invalid cc_max=%d\n", __func__, pe26100_cp->cc_max);
		goto error;
	}

	/*
	 * VBAT is over threshold but it might be "bouncy" due to transitory
	 * used to determine ta_vout.
	 */
	ret = pe26100_chg_read_vbatt(pe26100_cp->core, &vbat);
	if (ret < 0)
		return ret;

	/* Check the TA type and set the charging mode */
	if (pe26100_cp->ta_type == TA_TYPE_WIRELESS)
		ret = pe26100_cp_set_wireless_dc(pe26100_cp, vbat);
	else
		ret = pe26100_cp_set_wired_dc(pe26100_cp, vbat);

	/* Clear previous IIN ADC, TA increment flag */
	pe26100_cp->prev_inc = INC_NONE;
	pe26100_cp->prev_iin = 0;
error:
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

/*
 * The caller was triggered from pe26100_cp_apply_new_iin(), return to the
 * calling CC or CV loop.
 * call holding mutex_unlock(&pe26100_cp->lock);
 */
static void pe26100_cp_return_to_loop(struct pe26100_cp_charger *pe26100_cp)
{
	switch (pe26100_cp->ret_state) {
	case DC_STATE_ADJUST_CC:
		pe26100_cp->timer_id = TIMER_ADJUST_CCMODE;
		break;
	case DC_STATE_CC_MODE:
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		break;
	case DC_STATE_START_CV:
		pe26100_cp->timer_id = TIMER_ENTER_CVMODE;
		break;
	case DC_STATE_CV_MODE:
		pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
		break;
	default:
		dev_err(pe26100_cp->dev, "%s: invalid ret_state=%u\n",
			__func__, pe26100_cp->ret_state);
		return;
	}

	dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
		 pe26100_cp->charging_state, pe26100_cp->ret_state);

	pe26100_cp->charging_state = pe26100_cp->ret_state;
	pe26100_cp->timer_period = 1000;
	pe26100_cp->ret_state = 0;
	pe26100_cp->new_iin = 0;
}

/*
 * Kicked from pe26100_cp_apply_new_iin() when pe26100_cp->new_iin!=0 and completed
 * off the timer. Never called on WLC_DC.
 * NOTE: Will return to the calling loop in ->ret_state
 */
static int pe26100_cp_adjust_ta_current(struct pe26100_cp_charger *pe26100_cp)
{
	const int ta_limit = pe26100_cp->iin_cc;
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;
	int ret = 0;

	rc = pe26100_cp_check_error(pe26100_cp);
	if (rc != 0)
		return rc;

	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=%d ta_limit=%d, iin_cfg=%d icn=%d ibat=%d, cc_max=%d rc=%d",
			__func__, iin, pe26100_cp->iin_cc, ta_limit, pe26100_cp->pdata->iin_cfg,
			icn, ibat, pe26100_cp->cc_max, rc);

	if (pe26100_cp->charging_state != DC_STATE_ADJUST_TACUR)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_ADJUST_TACUR);

	pe26100_cp->charging_state = DC_STATE_ADJUST_TACUR;

	if (pe26100_cp->ta_cur == ta_limit) {

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"adj. End, ta_cur=%u, ta_vol=%u, iin_cc=%u, chg_mode=%u",
				pe26100_cp->ta_cur, pe26100_cp->ta_vol,
				pe26100_cp->iin_cc, pe26100_cp->chg_mode);

		/* "Recover" IIN_CC to the original value (new_iin) */
		pe26100_cp->iin_cc = pe26100_cp->new_iin;
		pe26100_cp_return_to_loop(pe26100_cp);

	} else if (pe26100_cp->iin_cc > pe26100_cp->pdata->iin_cfg) {
		const int old_iin_cfg = pe26100_cp->pdata->iin_cfg;

		/* Raise iin_cfg to the new iin_cc value (why??!?!?) */
		pe26100_cp->pdata->iin_cfg = pe26100_cp->iin_cc;

		ret = pe26100_cp_set_input_current(pe26100_cp, pe26100_cp->iin_cc);
		if (ret == 0)
			ret = pe26100_cp_reset_dcmode(pe26100_cp);
		if (ret < 0)
			goto error;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"New IIN, ta_max_vol=%u, ta_max_cur=%u, ta_max_pwr=%lu, iin_cc=%u, iin_cfg=%d->%d chg_mode=%u",
				pe26100_cp->ta_max_vol, pe26100_cp->ta_max_cur,
				pe26100_cp->ta_max_pwr, pe26100_cp->iin_cc,
				old_iin_cfg, pe26100_cp->iin_cc,
				pe26100_cp->chg_mode);

		pe26100_cp->new_iin = 0;

		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_ADJUST_CC);

		/* Send PD Message and go to Adjust CC mode */
		pe26100_cp->charging_state = DC_STATE_ADJUST_CC;
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	} else {
		unsigned int val;

		/*
		 * Adjust IIN_CC with APDO resolution(50mA)
		 * pe26100_cp->iin_cc will be reset to pe26100_cp->new_iin when
		 * ->ta_cur reaches the ta_limit at the beginning of the
		 * function
		 */
		val = pe26100_cp->iin_cc / PD_MSG_TA_CUR_STEP;
		pe26100_cp->iin_cc = val * PD_MSG_TA_CUR_STEP;
		pe26100_cp->ta_cur = pe26100_cp->iin_cc;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "adjust iin=%u ta_cur=%d chg_mode=%d",
				pe26100_cp->iin_cc, pe26100_cp->ta_cur, pe26100_cp->chg_mode);

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	}

	/* reschedule on ret == 0 */
error:
	return ret;
}

/* Kicked from apply_new_iin() then run off the timer
 * call holding mutex_lock(&pe26100_cp->lock);
 */
static int pe26100_cp_adjust_ta_voltage(struct pe26100_cp_charger *pe26100_cp)
{
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;

	if (pe26100_cp->charging_state != DC_STATE_ADJUST_TAVOL)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_ADJUST_TAVOL);

	pe26100_cp->charging_state = DC_STATE_ADJUST_TAVOL;

	rc = pe26100_cp_check_error(pe26100_cp);
	if (rc != 0)
		return rc;

	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;

	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=[%d,%d,%d], icn=%d ibat=%d, cc_max=%d rc=%d",
			__func__, iin, pe26100_cp->iin_cc - PD_MSG_TA_CUR_STEP,
			pe26100_cp->iin_cc, pe26100_cp->iin_cc + PD_MSG_TA_CUR_STEP,
			icn, ibat, pe26100_cp->cc_max, rc);

	if (iin < 0)
		return iin;


	/* Compare IIN ADC with targer input current */
	if (iin > (pe26100_cp->iin_cc + PD_MSG_TA_CUR_STEP)) {
		/* TA current is higher than the target input current */
		/* Decrease TA voltage (20mV) */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol - PD_MSG_TA_VOL_STEP;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont1, ta_vol=%u",
				pe26100_cp->ta_vol);

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	} else if (iin < (pe26100_cp->iin_cc - PD_MSG_TA_CUR_STEP)) {
		/* TA current is lower than the target input current */

		if (pe26100_cp_check_status(pe26100_cp) == STS_MODE_VFLT_LOOP) {
			/* IIN current may not able to increase in CV */

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"End1-1, skip adjust for cv, ta_cur=%u, ta_vol=%u, iin_cc=%u, chg_mode=%u",
					pe26100_cp->ta_cur, pe26100_cp->ta_vol,
					pe26100_cp->iin_cc, pe26100_cp->chg_mode);

			pe26100_cp_return_to_loop(pe26100_cp);
		} else if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
			/* TA TA voltage is already at the maximum voltage */

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"End1, ta_cur=%u, ta_vol=%u, iin_cc=%u, chg_mode=%u",
					pe26100_cp->ta_cur, pe26100_cp->ta_vol,
					pe26100_cp->iin_cc, pe26100_cp->chg_mode);

			pe26100_cp_return_to_loop(pe26100_cp);
		} else {
			/* Increase TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol + PD_MSG_TA_VOL_STEP;

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2, ta_vol=%u",
					pe26100_cp->ta_vol);

			/* Send PD Message */
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;
		}
	} else {
		/* IIN ADC is in valid range */
		/* IIN_CC - 50mA < IIN ADC < IIN_CC + 50mA  */

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"End2, ta_cur=%u, ta_vol=%u, iin_cc=%u, chg_mode=%u",
				pe26100_cp->ta_cur, pe26100_cp->ta_vol,
			pe26100_cp->iin_cc, pe26100_cp->chg_mode);

		pe26100_cp_return_to_loop(pe26100_cp);
	}

	return 0;
}

/*
 * Kicked from apply_new_iin() then run off the timer
 * * NOTE: caller must hold mutex_lock(&pe26100_cp->lock)
 */
static int pe26100_cp_adjust_rx_voltage(struct pe26100_cp_charger *pe26100_cp)
{
	const int iin_high = pe26100_cp->iin_cc + pe26100_cp->pdata->iin_cc_comp_offset_high;
	const int iin_low = pe26100_cp->iin_cc - pe26100_cp->pdata->iin_cc_comp_offset_low;
	int rc, ibat, icn = -EINVAL, iin = -EINVAL;
	bool ovc_flag;

	if (pe26100_cp->charging_state != DC_STATE_ADJUST_TAVOL)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_ADJUST_TAVOL);

	pe26100_cp->charging_state = DC_STATE_ADJUST_TAVOL;

	rc = pe26100_cp_check_error(pe26100_cp);
	if (rc != 0)
		return rc;

	rc = pe26100_cp_get_current_adcs(pe26100_cp, &ibat, &icn, &iin);
	if (rc)
		return rc;


	ovc_flag = ibat > pe26100_cp->cc_max;
	if (ovc_flag)
		pe26100_cp_chg_stats_inc_ovcf(&pe26100_cp->chg_data, ibat, pe26100_cp->cc_max);

	logbuffer_prlog(pe26100_cp, ovc_flag ? LOGLEVEL_WARNING : LOGLEVEL_DEBUG,
			"%s: iin=%d, iin_cc=[%d,%d,%d], icn=%d ibat=%d, cc_max=%d rc=%d",
			__func__, iin, iin_low, pe26100_cp->iin_cc, iin_high,
			icn, ibat, pe26100_cp->cc_max, rc);

	if (iin < 0)
		return iin;

	/* Compare IIN ADC with targer input current */
	if (iin > iin_high) {
		/* RX current is higher than the target input current */

		/* Decrease RX voltage (100mV) */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol - pe26100_cp->pdata->wcrx_vol_down_step;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont1, rx_vol=%u",
				pe26100_cp->ta_vol);

		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	} else if (iin < iin_low) {
		/* RX current is lower than the target input current */

		if (pe26100_cp_check_status(pe26100_cp) == STS_MODE_VFLT_LOOP) {
			/* RX current may not able to increase in CV */
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"End1-1, skip adjust for cv, rx_vol=%u, iin_cc=%u",
					pe26100_cp->ta_vol, pe26100_cp->iin_cc);

			pe26100_cp_return_to_loop(pe26100_cp);
		} else if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
			/* RX current is already the maximum voltage */
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"End1, rx_vol=%u, iin_cc=%u, chg_mode=%u",
					pe26100_cp->ta_vol, pe26100_cp->iin_cc,
					pe26100_cp->chg_mode);

			/* Return charging state to the previous state */
			pe26100_cp_return_to_loop(pe26100_cp);
		} else {
			/* Increase RX voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol +
					     pe26100_cp->pdata->wcrx_vol_up_step;

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2, rx_vol=%u",
					pe26100_cp->ta_vol);

			/* Set RX voltage */
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;
		}
	} else {
		/* IIN ADC is in valid range */

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"End2, rx_vol=%u, iin_cc=%u, chg_mode=%u",
				pe26100_cp->ta_vol, pe26100_cp->iin_cc,
				pe26100_cp->chg_mode);

		/* Return charging state to the previous state */
		pe26100_cp_return_to_loop(pe26100_cp);
	}

	return 0;
}

/*
 * Called from CC and CV loops to set a new IIN (i.e. a new cc_max charging
 * current). Should also change the iin_cfg to avoid overcurrents.
 * NOTE: caller must hold mutex_lock(&pe26100_cp->lock)
 */
static int pe26100_cp_apply_new_iin(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;

	logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
			"new_iin=%d (cc_max=%d), ta_type=%d charging_state=%d",
			pe26100_cp->new_iin, pe26100_cp->cc_max,
			pe26100_cp->ta_type, pe26100_cp->charging_state);

	/* iin_cfg is adjusted UP in pe26100_cp_set_input_current() */
	ret = pe26100_cp_set_input_current(pe26100_cp, pe26100_cp->new_iin);
	if (ret < 0)
		return ret;
	pe26100_cp->pdata->iin_cfg = pe26100_cp->new_iin;

	 /*
	  * ->ret_state is used to go back to the loop (CC or CV) that called
	  * this function.
	  */
	pe26100_cp->ret_state = pe26100_cp->charging_state;

	/*
	 * new_iin is used to trigger the process which might span one or more
	 * timer ticks the new_iin . The flag will be cleared once the target
	 * is reached.
	 */
	pe26100_cp->iin_cc = pe26100_cp->new_iin;
	if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
		ret = pe26100_cp_adjust_rx_voltage(pe26100_cp);
	} else if (pe26100_cp->iin_cc < PE26100_CP_TA_MIN_CUR) {
		/* TA current = PE26100_CP_TA_MIN_CUR(1.0A) */
		pe26100_cp->ta_cur = PE26100_CP_TA_MIN_CUR;
		ret = pe26100_cp_adjust_ta_voltage(pe26100_cp);
	} else {
		ret = pe26100_cp_adjust_ta_current(pe26100_cp);
	}

	/* need reschedule on ret != 0 */

	dev_dbg(pe26100_cp->dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

/*
 * also called from pe26100_cp_set_new_cc_max()
 * call holding mutex_unlock(&pe26100_cp->lock);
 */
static int pe26100_cp_set_new_iin(struct pe26100_cp_charger *pe26100_cp, int iin)
{
	int ret = 0;

	if (iin < 0) {
		dev_dbg(pe26100_cp->dev, "%s: ignore negative iin=%d\n", __func__, iin);
		return 0;
	}

	/* same as previous request nevermind */
	if (iin == pe26100_cp->new_iin)
		return 0;

	dev_dbg(pe26100_cp->dev, "%s: new_iin=%d->%d state=%d\n", __func__,
		 pe26100_cp->new_iin, iin, pe26100_cp->charging_state);

	/* apply iin_cc in pe26100_cp_preset_config() at start */
	if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING ||
	    pe26100_cp->charging_state == DC_STATE_CHECK_VBAT) {

		/* used on start vs the ->iin_cfg one */
		pe26100_cp->pdata->iin_cfg = iin;
		pe26100_cp->iin_cc = iin;
	} else if (pe26100_cp->ret_state == 0) {
		/*
		 * pe26100_cp_apply_new_iin() has not picked out the value yet
		 * and the value can be changed safely.
		 */
		pe26100_cp->new_iin = iin;

		/* might want to tickle the loop now */
	} else {
		/* the caller must retry */
		ret = -EAGAIN;
	}

	dev_dbg(pe26100_cp->dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

/*
 * The is no CC loop in this part: current must be controlled on TA side
 * adjusting output power. cc_max (the charging current) is scaled to iin
 *
 */
static int pe26100_cp_set_new_cc_max(struct pe26100_cp_charger *pe26100_cp, int cc_max)
{
	const int prev_cc_max = pe26100_cp->cc_max;
	int iin_max, ret = 0;

	cc_max = (cc_max == GBMS_MSC_FCC_CHARGE_OFF) ? 0 : cc_max;

	if (cc_max < 0) {
		dev_dbg(pe26100_cp->dev, "%s: ignore negative cc_max=%d\n", __func__, cc_max);
		return 0;
	}

	mutex_lock(&pe26100_cp->lock);

	/* same as previous request nevermind */
	if (cc_max == pe26100_cp->cc_max)
		goto done;

	/* iin will be capped by the adapter capabilities in reset_dcmode() */
	iin_max = pe26100_cp_get_iin_max(pe26100_cp, cc_max);
	if (iin_max <= 0) {
		dev_dbg(pe26100_cp->dev, "%s: ignore negative iin_max=%d\n", __func__, iin_max);
		goto done;
	}

	ret = pe26100_cp_set_new_iin(pe26100_cp, iin_max);
	if (ret == 0)
		pe26100_cp->cc_max = cc_max;

	logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
			"%s: charging_state=%d cc_max=%d->%d iin_max=%d, ret=%d",
			__func__, pe26100_cp->charging_state, prev_cc_max,
			cc_max, iin_max, ret);

done:
	dev_dbg(pe26100_cp->dev, "%s: ret=%d\n", __func__, ret);
	mutex_unlock(&pe26100_cp->lock);
	return ret;
}

/*
 * Apply pe26100_cp->new_vfloat to the charging voltage.
 * Called from CC and CV loops, needs mutex_lock(&pe26100_cp->lock)
 */
static int pe26100_cp_apply_new_vfloat(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;

	if (pe26100_cp->fv_uv == pe26100_cp->new_vfloat)
		goto error_done;

	/* actually change the hardware */
	ret = pe26100_cp_set_vfloat(pe26100_cp, pe26100_cp->new_vfloat);
	if (ret < 0)
		goto error_done;

	/* Restart the process if tier switch happened (either direction) */
	if ((pe26100_cp->charging_state == DC_STATE_CV_MODE
	     || pe26100_cp->charging_state == DC_STATE_START_CV)
	    && abs(pe26100_cp->new_vfloat - pe26100_cp->fv_uv) > PE26100_CP_TIER_SWITCH_DELTA) {
		ret = pe26100_cp_reset_dcmode(pe26100_cp);
		if (ret < 0) {
			pr_err("%s: cannot reset dcmode (%d)\n", __func__, ret);
		} else {
			dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
				pe26100_cp->charging_state, DC_STATE_ADJUST_CC);

			pe26100_cp->charging_state = DC_STATE_ADJUST_CC;
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			pe26100_cp->timer_period = 0;
		}
	}

	pe26100_cp->fv_uv = pe26100_cp->new_vfloat;

error_done:
	logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
			"%s: new_vfloat=%d, ret=%d", __func__,
			pe26100_cp->new_vfloat, ret);

	if (ret == 0)
		pe26100_cp->new_vfloat = 0;

	return ret;
}

static int pe26100_cp_set_new_vfloat(struct pe26100_cp_charger *pe26100_cp, int vfloat)
{
	int ret = 0;

	if (vfloat < 0) {
		dev_dbg(pe26100_cp->dev, "%s: ignore negative vfloat %d\n", __func__, vfloat);
		return 0;
	}

	mutex_lock(&pe26100_cp->lock);
	if (pe26100_cp->new_vfloat == vfloat)
		goto done;

	/* use fv_uv at start in pe26100_cp_preset_config() */
	if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING ||
	    pe26100_cp->charging_state == DC_STATE_CHECK_VBAT) {
		pe26100_cp->fv_uv = vfloat;
	} else {
		/* applied in pe26100_cp_apply_new_vfloat() from CC or in CV loop */
		pe26100_cp->new_vfloat = vfloat;
		pr_debug("%s: new_vfloat=%d\n", __func__, pe26100_cp->new_vfloat);

		/* might want to tickle the cycle */
	}

done:
	mutex_unlock(&pe26100_cp->lock);
	return ret;
}

/* called on loop inactive */
static void pe26100_cp_adjust_ccmode_wireless(struct pe26100_cp_charger *pe26100_cp, int iin)
{
	/* IIN_ADC > IIN_CC -20mA ? */
	if (iin > (pe26100_cp->iin_cc - PE26100_CP_IIN_ADC_OFFSET)) {
		/* Input current is already over IIN_CC */
		/* End RX voltage adjustment */

		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CC_MODE);

		/* change charging state to CC mode */
		pe26100_cp->charging_state = DC_STATE_CC_MODE;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End1: IIN_ADC=%d, rx_vol=%u",
				iin, pe26100_cp->ta_vol);

		/* Clear TA increment flag */
		pe26100_cp->prev_inc = INC_NONE;
		/* Go to CC mode */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = 0;

	/* Check RX voltage */
	} else if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
		/* RX voltage is already max value */
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End2: MAX value, rx_vol=%u max=%d",
				pe26100_cp->ta_vol, pe26100_cp->ta_max_vol);

		/* Clear TA increment flag */
		pe26100_cp->prev_inc = INC_NONE;
		/* Go to CC mode */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = 0;
	} else {
		/* Try to increase RX voltage(100mV) */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol + pe26100_cp->pdata->wcrx_vol_up_step;
		if (pe26100_cp->ta_vol > pe26100_cp->ta_max_vol)
			pe26100_cp->ta_vol = pe26100_cp->ta_max_vol;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont: rx_vol=%u",
				pe26100_cp->ta_vol);
		/* Set RX voltage */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	}
}

/* called on loop inactive */
static void pe26100_cp_adjust_ccmode_wired(struct pe26100_cp_charger *pe26100_cp, int iin)
{

	/* USBPD TA is connected */
	if (iin > (pe26100_cp->iin_cc - PE26100_CP_IIN_ADC_OFFSET)) {
		/* IIN_ADC > IIN_CC -20mA ? */
		/* Input current is already over IIN_CC */
		/* End TA voltage and current adjustment */

		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CC_MODE);

		/* change charging state to CC mode */
		pe26100_cp->charging_state = DC_STATE_CC_MODE;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"End1: IIN_ADC=%d, ta_vol=%u, ta_cur=%u",
				iin, pe26100_cp->ta_vol, pe26100_cp->ta_cur);

		/* Clear TA increment flag */
		pe26100_cp->prev_inc = INC_NONE;
		/* Go to CC mode */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = 0;

	/* Check TA voltage */
	} else if (pe26100_cp->ta_vol == pe26100_cp->ta_max_vol) {
		/* TA voltage is already max value */
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"End2: MAX value, ta_vol=%u, ta_cur=%u",
				pe26100_cp->ta_vol, pe26100_cp->ta_cur);

		/* Clear TA increment flag */
		pe26100_cp->prev_inc = INC_NONE;
		/* Go to CC mode */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = 0;

		/* Check TA tolerance
		 * The current input current compares the final input
		 * current(IIN_CC) with 100mA offset PPS current tolerance
		 * has +/-150mA, so offset defined 100mA(tolerance +50mA)
		 */
	} else if (iin < (pe26100_cp->iin_cc - PE26100_CP_TA_IIN_OFFSET)) {
		/*
		 * TA voltage too low to enter TA CC mode, so we
		 * should increase TA voltage
		 */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol + PE26100_CP_TA_VOL_STEP_ADJ_CC *
					pe26100_cp->chg_mode;

		if (pe26100_cp->ta_vol > pe26100_cp->ta_max_vol)
			pe26100_cp->ta_vol = pe26100_cp->ta_max_vol;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont1: ta_vol=%u",
				pe26100_cp->ta_vol);

		/* Set TA increment flag */
		pe26100_cp->prev_inc = INC_TA_VOL;
		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

	/* compare IIN ADC with previous IIN ADC + 20mA */
	} else if (iin > (pe26100_cp->prev_iin + PE26100_CP_IIN_ADC_OFFSET)) {
		/* TA can supply more current if TA voltage is high */
		/* TA voltage too low for TA CC mode: increase it */
		pe26100_cp->ta_vol = pe26100_cp->ta_vol +
					PE26100_CP_TA_VOL_STEP_ADJ_CC *
					pe26100_cp->chg_mode;
		if (pe26100_cp->ta_vol > pe26100_cp->ta_max_vol)
			pe26100_cp->ta_vol = pe26100_cp->ta_max_vol;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont2: ta_vol=%u",
				pe26100_cp->ta_vol);
		/* Set TA increment flag */
		pe26100_cp->prev_inc = INC_TA_VOL;

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

	/* Check the previous increment */
	} else if (pe26100_cp->prev_inc == INC_TA_CUR) {
		/*
		 * The previous increment is TA current, but input
		 * current does not increase. Try with voltage.
		 */

		pe26100_cp->ta_vol = pe26100_cp->ta_vol +
					PE26100_CP_TA_VOL_STEP_ADJ_CC *
					pe26100_cp->chg_mode;
		if (pe26100_cp->ta_vol > pe26100_cp->ta_max_vol)
			pe26100_cp->ta_vol = pe26100_cp->ta_max_vol;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont3: ta_vol=%u",
				pe26100_cp->ta_vol);

		pe26100_cp->prev_inc = INC_TA_VOL;
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

		/*
		 * The previous increment is TA voltage, but input
		 * current does not increase
		 */

		/* Try to increase TA current */
		/* Check APDO max current */
	} else if (!pe26100_cp_can_inc_ta_cur(pe26100_cp)) {
		/* TA current is maximum current */

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"End(MAX_CUR): IIN_ADC=%d, ta_vol=%u, ta_cur=%u",
				iin, pe26100_cp->ta_vol, pe26100_cp->ta_cur);

		pe26100_cp->prev_inc = INC_NONE;

		/* Go to CC mode */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = 0;
	} else {
		/* TA has tolerance and compensate it as real current */
		/* Increase TA current(50mA) */
		pe26100_cp->ta_cur = pe26100_cp->ta_cur + PD_MSG_TA_CUR_STEP;
		if (pe26100_cp->ta_cur > pe26100_cp->ta_max_cur)
			pe26100_cp->ta_cur = pe26100_cp->ta_max_cur;

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "Cont4: ta_cur=%u",
				pe26100_cp->ta_cur);

		pe26100_cp->prev_inc = INC_TA_CUR;
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
	}
}

static int pe26100_cp_vote_dc_avail(struct pe26100_cp_charger *pe26100_cp, int vote)
{
	int ret = 0;

	if (!pe26100_cp->dc_avail)
		pe26100_cp->dc_avail = gvotable_election_get_handle(VOTABLE_DC_CHG_AVAIL);

	if (pe26100_cp->dc_avail) {
		ret = gvotable_cast_int_vote(pe26100_cp->dc_avail, REASON_DC_DRV,
					     GBMS_ACTIVE_CHG_DISABLE, !vote);
		if (ret < 0)
			dev_err(pe26100_cp->dev, "Unable to cast vote for DC Chg avail (%d)\n",
				ret);
	}

	if (pe26100_cp->charging_state == DC_STATE_ERROR)
		logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
				"%s: Voting dc_avail when in error state", __func__);

	return ret;
}

/* <0 error, 0 no new limits, >0 new limits */
static int pe26100_cp_apply_new_limits(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = -1;

	if (pe26100_cp->new_iin && pe26100_cp->new_iin < pe26100_cp->iin_cc)
		ret = pe26100_cp_apply_new_iin(pe26100_cp);
	else if (pe26100_cp->new_vfloat)
		ret = pe26100_cp_apply_new_vfloat(pe26100_cp);
	else if (pe26100_cp->new_iin)
		ret = pe26100_cp_apply_new_iin(pe26100_cp);

	return ret == 0 ? 1 : 0;
}

/* Direct Charging Adjust CC MODE control
 * called at the beginnig of CC mode charging. Will be followed by
 * pe26100_cp_charge_ccmode with which share some of the adjustments.
 */
static int pe26100_cp_charge_adjust_ccmode(struct pe26100_cp_charger *pe26100_cp)
{
	int  iin, ccmode, vbatt, vin_vol;
	int ret = 0;

	mutex_lock(&pe26100_cp->lock);

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);
	pe26100_cp_prlog_state(pe26100_cp, __func__);

	if (pe26100_cp->charging_state != DC_STATE_ADJUST_CC)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_ADJUST_CC);

	pe26100_cp->charging_state = DC_STATE_ADJUST_CC;

	ret = pe26100_cp_check_error(pe26100_cp);
	if (ret != 0)
		goto error; /*This is not active mode. */

	ret = pe26100_cp_apply_new_limits(pe26100_cp);
	if (ret < 0)
		goto error;
	if (ret > 0)
		goto done;

	ccmode = pe26100_cp_check_status(pe26100_cp);
	if (ccmode < 0) {
		ret = ccmode;
		goto error;
	}

	switch (ccmode) {
	case STS_MODE_IIN_LOOP:
		fallthrough;
	case STS_MODE_CHG_LOOP:	/* CHG_LOOP does't exist */
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Decrease RX voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
					     pe26100_cp->pdata->wcrx_vol_down_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End1: rx_vol=%u",
					 pe26100_cp->ta_vol);
		} else if (pe26100_cp->ta_cur > PE26100_CP_TA_MIN_CUR) {
			/* TA current is higher than 1.0A */
			/* Decrease TA current (50mA) */
			pe26100_cp->ta_cur = pe26100_cp->ta_cur - PD_MSG_TA_CUR_STEP;

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End2: ta_cur=%u, ta_vol=%u",
					pe26100_cp->ta_cur, pe26100_cp->ta_vol);
		} else {
			/* Decrease TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol - PD_MSG_TA_VOL_STEP;

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End3: ta_cur=%u, ta_vol=%u",
					pe26100_cp->ta_cur, pe26100_cp->ta_vol);
		}

		pe26100_cp->prev_inc = INC_NONE;

		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CC_MODE);

		/* Send PD Message and then go to CC mode */
		pe26100_cp->charging_state = DC_STATE_CC_MODE;
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_VFLT_LOOP:
		ret = pe26100_chg_read_vbatt(pe26100_cp->core, &vbatt);

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "End4: vbatt=%d, ta_vol=%u (%d)",
				vbatt, pe26100_cp->ta_vol, ret);

		/* Clear TA increment flag */
		pe26100_cp->prev_inc = INC_NONE;
		/* Go to Pre-CV mode */
		pe26100_cp->timer_id = TIMER_ENTER_CVMODE;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_LOOP_INACTIVE:

		ret = pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"Inactive: iin=%d, iin_cc=%d, cc_max=%d (%d)",
				iin, pe26100_cp->iin_cc, pe26100_cp->cc_max, ret);
		if (ret < 0)
			break;

		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS)
			pe26100_cp_adjust_ccmode_wireless(pe26100_cp, iin);
		else
			pe26100_cp_adjust_ccmode_wired(pe26100_cp, iin);

		pe26100_cp->prev_iin = iin;
		break;

	case STS_MODE_VIN_UVLO:
		/* VIN UVLO - just notification , it works by hardware */
		ret = pe26100_chg_read_vin(pe26100_cp->core, &vin_vol);

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "VIN_UVLO: ta_vol=%u, vin_vol=%d (%d)",
				pe26100_cp->ta_cur, vin_vol, ret);

		/* Check VIN after 1sec */
		pe26100_cp->timer_id = TIMER_ADJUST_CCMODE;
		pe26100_cp->timer_period = 1000;
		break;

	default:
		goto error;
	}

done:
	mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
			 msecs_to_jiffies(pe26100_cp->timer_period));
error:
	mutex_unlock(&pe26100_cp->lock);
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

/* 2:1 Direct Charging CC MODE control */
static int pe26100_cp_charge_ccmode(struct pe26100_cp_charger *pe26100_cp)
{
	int ccmode = -1, vin_vol, iin, ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);

	mutex_lock(&pe26100_cp->lock);

	if (pe26100_cp->charging_state != DC_STATE_CC_MODE)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CC_MODE);

	pe26100_cp->charging_state = DC_STATE_CC_MODE;

	pe26100_cp_prlog_state(pe26100_cp, __func__);

	ret = pe26100_cp_check_error(pe26100_cp);
	if (ret != 0)
		goto error_exit;

	/*
	 * A change in VFLOAT here means that we have busted the tier, a
	 * change in iin means that the thermal engine had changed cc_max.
	 * pe26100_cp_apply_new_limits() changes pe26100_cp->charging_state to
	 * DC_STATE_ADJUST_TAVOL or DC_STATE_ADJUST_TACUR when new limits
	 * need to be applied.
	 */
	ret = pe26100_cp_apply_new_limits(pe26100_cp);
	if (ret < 0)
		goto error_exit;
	if (ret > 0)
		goto done;

	ccmode = pe26100_cp_check_status(pe26100_cp);
	if (ccmode < 0) {
		ret = ccmode;
		goto error_exit;
	}

	switch (ccmode) {
	case STS_MODE_LOOP_INACTIVE:
		/* Set input current compensation */
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Need RX voltage compensation */
			ret = pe26100_cp_set_rx_voltage_comp(pe26100_cp);

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "INACTIVE1: rx_vol=%u",
					pe26100_cp->ta_vol);
		} else {
			const int ta_max_vol = pe26100_cp->ta_max_vol;
			int ta_max_vol_cp = 0;

			ta_max_vol_cp = pe26100_cp->pdata->ta_max_vol_2_1 * CHG_2TO1_DC_MODE;

			/* Check TA current with TA_MIN_CUR */
			if (pe26100_cp->ta_cur <= PE26100_CP_TA_MIN_CUR) {
				pe26100_cp->ta_cur = PE26100_CP_TA_MIN_CUR;

				ret = pe26100_cp_set_ta_voltage_comp(pe26100_cp);
			} else if (ta_max_vol >= ta_max_vol_cp) {
				ret = pe26100_cp_set_ta_current_comp(pe26100_cp);
			} else {
				/* constant power mode */
				ret = pe26100_cp_set_ta_current_comp2(pe26100_cp);
			}

			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"INACTIVE2: ta_cur=%u, ta_vol=%u",
					pe26100_cp->ta_cur,
					pe26100_cp->ta_vol);
		}
		break;

	case STS_MODE_VFLT_LOOP:
		/* TODO: adjust fv_uv here based on real vbatt */

		ret = pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "CC VFLOAT: iin=%d (%d)", iin, ret);

		/* go to Pre-CV mode */
		pe26100_cp->timer_id = TIMER_ENTER_CVMODE;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_IIN_LOOP:
		fallthrough;
	case STS_MODE_CHG_LOOP:
		ret = pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
		if (ret < 0)
			break;

		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Decrease RX voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
					     pe26100_cp->pdata->wcrx_vol_down_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"IIN_LOOP1: iin=%d, next_rx_vol=%u",
					iin, pe26100_cp->ta_vol);
		} else if (pe26100_cp->ta_cur <= PE26100_CP_TA_MIN_CUR) {
			/* Decrease TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol - PD_MSG_TA_VOL_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"IIN_LOOP2: iin=%d, next_ta_vol=%u",
					iin, pe26100_cp->ta_vol);
		} else {
			/* Decrease TA current (50mA) */
			pe26100_cp->ta_cur = pe26100_cp->ta_cur - PD_MSG_TA_CUR_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"IIN_LOOP3: iin=%d, next_ta_cur=%u",
					iin, pe26100_cp->ta_cur);
		}

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_VIN_UVLO:
		/* VIN UVLO - just notification, it works by hardware */
		ret = pe26100_chg_read_vin(pe26100_cp->core, &vin_vol);

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"VIN_UVLO: ta_cur=%u ta_vol=%u, vin_vol=%d (%d)",
				pe26100_cp->ta_cur, pe26100_cp->ta_vol, vin_vol, ret);

		/* Check VIN after 1sec */
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		pe26100_cp->timer_period = 1000;
		break;

	default:
		break;
	}

done:
	mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
			 msecs_to_jiffies(pe26100_cp->timer_period));

error_exit:
	mutex_unlock(&pe26100_cp->lock);
	dev_dbg(pe26100_cp->dev, "%s: End, ccmode=%d timer_id=%d, timer_period=%lu ret=%d\n",
		 __func__, ccmode, pe26100_cp->timer_id, pe26100_cp->timer_period,
		 ret);
	return ret;
}


/* Direct Charging Start CV MODE control - Pre CV MODE */
static int pe26100_cp_charge_start_cvmode(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;
	int cvmode;
	int vin_vol;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);

	mutex_lock(&pe26100_cp->lock);

	if (pe26100_cp->charging_state != DC_STATE_START_CV)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_START_CV);

	pe26100_cp->charging_state = DC_STATE_START_CV;

	/* Check the charging type */
	ret = pe26100_cp_check_error(pe26100_cp);
	if (ret != 0)
		goto error_exit;

	ret = pe26100_cp_apply_new_limits(pe26100_cp);
	if (ret < 0)
		goto error_exit;
	if (ret > 0)
		goto done;

	/* Check the status */
	cvmode = pe26100_cp_check_status(pe26100_cp);
	if (cvmode < 0) {
		ret = cvmode;
		goto error_exit;
	}

	switch (cvmode) {
	case STS_MODE_CHG_LOOP:
		fallthrough;
	case STS_MODE_IIN_LOOP:
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Decrease RX voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
					     pe26100_cp->pdata->wcrx_vol_down_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG, "%s: PreCV IIN_LOOP: rx_vol=%u",
				 __func__, pe26100_cp->ta_vol);
		} else {
			/* Check TA current */
			if (pe26100_cp->ta_cur > PE26100_CP_TA_MIN_CUR) {
				/* TA current is higher than 1.0A */

				/* Decrease TA current (50mA) */
				pe26100_cp->ta_cur = pe26100_cp->ta_cur - PD_MSG_TA_CUR_STEP;
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"%s: PreCV IIN_LOOP: ta_cur=%u",
						__func__, pe26100_cp->ta_cur);
			} else {
				/* TA current is less than 1.0A */
				/* Decrease TA voltage (20mV) */
				pe26100_cp->ta_vol = pe26100_cp->ta_vol - PD_MSG_TA_VOL_STEP;
				logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
						"%s: PreCV IIN_LOOP: ta_vol=%u",
						__func__, pe26100_cp->ta_vol);
			}
		}

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_VFLT_LOOP:
		/* Check the TA type */
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Decrease RX voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
					     pe26100_cp->pdata->wcrx_vol_down_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: PreCV VF Cont: rx_vol=%u",
					__func__, pe26100_cp->ta_vol);
		} else {
			/* Decrease TA voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
						PE26100_CP_TA_VOL_STEP_PRE_CV *
						pe26100_cp->chg_mode;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: PreCV VF Cont: ta_vol=%u",
					__func__, pe26100_cp->ta_vol);
		}

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_LOOP_INACTIVE:
		/* Exit Pre CV mode */
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: PreCV End: ta_vol=%u, ta_cur=%u",
				__func__, pe26100_cp->ta_vol, pe26100_cp->ta_cur);

		/* Need to implement notification to other driver */
		/* To do here */

		/* Go to CV mode */
		pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_VIN_UVLO:
		/* VIN UVLO - just notification , it works by hardware */
		ret = pe26100_chg_read_vin(pe26100_cp->core, &vin_vol);

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: PreCV VIN_UVLO: ta_vol=%u, vin_vol=%u (%d)",
				__func__, pe26100_cp->ta_cur, vin_vol, ret);

		/* Check VIN after 1sec */
		pe26100_cp->timer_id = TIMER_ENTER_CVMODE;
		pe26100_cp->timer_period = 1000;
		break;

	default:
		break;
	}

done:
	mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
			 msecs_to_jiffies(pe26100_cp->timer_period));
error_exit:
	mutex_unlock(&pe26100_cp->lock);
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

static int pe26100_cp_check_eoc(struct pe26100_cp_charger *pe26100_cp)
{
	const int eoc_tolerance = 25000; /* 25mV under max float voltage */
	const int vlimit = PE26100_CP_COMP_VFLOAT_MAX - eoc_tolerance;
	int iin, vbat;
	int ret;

	ret = pe26100_chg_current_now(pe26100_cp->core, &iin, PE26100_CHG_MODE_CP);
	if (ret < 0) {
		dev_err(pe26100_cp->dev, "%s: error reading %d iin\n", __func__, ret);
		return ret;
	}

	ret = pe26100_chg_read_vbatt(pe26100_cp->core, &vbat);
	if (ret < 0) {
		dev_err(pe26100_cp->dev, "%s: error reading vbat =%d\n", __func__, ret);
		return ret;
	}

	dev_dbg(pe26100_cp->dev, "%s: iin=%d, topoff=%u, vbat=%d vlimit=%d\n", __func__,
		 iin, pe26100_cp->pdata->iin_topoff,
		 vbat, vlimit);

	return iin < pe26100_cp->pdata->iin_topoff && vbat >= vlimit;
}

/*  Direct Charging CV MODE control */
static int pe26100_cp_charge_cvmode(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;
	int cvmode;
	int vin_vol;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);

	mutex_lock(&pe26100_cp->lock);

	if (pe26100_cp->charging_state != DC_STATE_CV_MODE)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CV_MODE);

	pe26100_cp->charging_state = DC_STATE_CV_MODE;

	ret = pe26100_cp_check_error(pe26100_cp);
	if (ret != 0)
		goto error_exit;

	/*
	 * A change in vfloat and cc_max here is a normal tier transition, a
	 * change in iin  means that the thermal engine has changed cc_max.
	 */
	ret = pe26100_cp_apply_new_limits(pe26100_cp);
	if (ret < 0)
		goto error_exit;
	if (ret > 0)
		goto done;

	cvmode = pe26100_cp_check_status(pe26100_cp);
	if (cvmode < 0) {
		ret = cvmode;
		goto error_exit;
	}

	if (cvmode == STS_MODE_LOOP_INACTIVE) {
		ret = pe26100_cp_check_eoc(pe26100_cp);
		if (ret < 0)
			goto error_exit;
		if (ret)
			cvmode = STS_MODE_CHG_DONE;
	}

	switch (cvmode) {
	case STS_MODE_CHG_DONE: {
		const bool done_already = pe26100_cp->charging_state ==
					  DC_STATE_CHARGING_DONE;

		if (!done_already)
			dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n",
				 __func__, pe26100_cp->charging_state,
				 DC_STATE_CHARGING_DONE);


		/* Keep CV mode until driver send stop charging */
		pe26100_cp->charging_state = DC_STATE_CHARGING_DONE;
		power_supply_changed(pe26100_cp->mains);

		/* _cpm already came in */
		if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING) {
			dev_dbg(pe26100_cp->dev, "%s: Already stop DC\n", __func__);
			break;
		}

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: done_already=%d charge Done\n", __func__,
				done_already);

		pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
		pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
	} break;

	case STS_MODE_CHG_LOOP:
		fallthrough;
	case STS_MODE_IIN_LOOP:
		/* Check the TA type */
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Decrease RX Voltage (100mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
						pe26100_cp->pdata->wcrx_vol_down_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: CV LOOP, Cont: rx_vol=%u",
					__func__, pe26100_cp->ta_vol);

		/* Check TA current */
		} else if (pe26100_cp->ta_cur > PE26100_CP_TA_MIN_CUR) {
			/* TA current is higher than (1.0A*chg_mode) */
			/* Decrease TA current (50mA) */
			pe26100_cp->ta_cur = pe26100_cp->ta_cur -
						PD_MSG_TA_CUR_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: CV LOOP, Cont: ta_cur=%u",
					__func__, pe26100_cp->ta_cur);
		} else {
			/* TA current is less than (1.0A*chg_mode) */
			/* Decrease TA Voltage (20mV) */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
						PD_MSG_TA_VOL_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: CV LOOP, Cont: ta_vol=%u",
					__func__, pe26100_cp->ta_vol);
		}

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_VFLT_LOOP:
		/* Check the TA type */
		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			/* Decrease RX voltage */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol -
					     pe26100_cp->pdata->wcrx_vol_down_step;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: CV VFLOAT, Cont: rx_vol=%u",
					__func__, pe26100_cp->ta_vol);
		} else {
			/* Decrease TA voltage */
			pe26100_cp->ta_vol = pe26100_cp->ta_vol - 2 * PD_MSG_TA_VOL_STEP;
			logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
					"%s: CV VFLOAT, Cont: ta_vol=%u",
					__func__, pe26100_cp->ta_vol);
		}

		/* Send PD Message */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;
		break;

	case STS_MODE_LOOP_INACTIVE:
		pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
		pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
		break;

	case STS_MODE_VIN_UVLO:
		/* VIN UVLO - just notification, it works by hardware */
		ret = pe26100_chg_read_vin(pe26100_cp->core, &vin_vol);
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: CC VIN_UVLO: ta_cur=%u ta_vol=%u, vin_vol=%d (%d)",
				__func__, pe26100_cp->ta_cur, pe26100_cp->ta_vol,
				vin_vol, ret);

		/* Check VIN after 1sec */
		pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
		pe26100_cp->timer_period = 1000;
		break;

	default:
		break;
	}

done:
	dev_dbg(pe26100_cp->dev, "%s: reschedule next id=%d period=%ld chg_state=%d\n",
		 __func__, pe26100_cp->timer_id, pe26100_cp->timer_period,
		pe26100_cp->charging_state);

	mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
			 msecs_to_jiffies(pe26100_cp->timer_period));
error_exit:
	mutex_unlock(&pe26100_cp->lock);
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d next\n", __func__, ret);
	return ret;
}

static int pe26100_cp_set_chg_mode_by_apdo(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;

	/*
	 * Get the APDO max and set chg_mode.
	 * Returns ->ta_max_vol, ->ta_max_cur, ->ta_max_pwr and
	 * ->ta_objpos for the given ta_max_vol and ta_max_cur.
	 */
	ret = pe26100_cp_get_apdo_max_power(pe26100_cp,
					    pe26100_cp->pdata->ta_max_vol_2_1 * CHG_2TO1_DC_MODE,
					    PE26100_CP_TA_MAX_CUR);
	if (ret == 0) {
		pe26100_cp->chg_mode = CHG_2TO1_DC_MODE;
		goto done;
	}

	dev_warn(pe26100_cp->dev, "%s: No APDO to support 2:1 for %d, max_voltage: %d\n",
		 __func__, PE26100_CP_TA_MAX_CUR,
		 pe26100_cp->pdata->ta_max_vol_2_1 * CHG_2TO1_DC_MODE);

	ret = pe26100_cp_get_apdo_max_power(pe26100_cp,
					   pe26100_cp->pdata->ta_max_vol_2_1 * CHG_2TO1_DC_MODE, 0);
	if (ret == 0) {
		pe26100_cp->chg_mode = CHG_2TO1_DC_MODE;
		goto done;
	}

	dev_err(pe26100_cp->dev, "%s: No APDO to support 2:1\n", __func__);
	pe26100_cp->chg_mode = CHG_NO_DC_MODE;
	pe26100_cp->ta_max_vol = 0;
	pe26100_cp->error = PE26100_CP_ERROR_APDO;

done:
	if (pe26100_cp->ta_max_cur >= PE26100_TA_CUR_TOLERANCE)
		pe26100_cp->ta_max_cur -= PE26100_TA_CUR_TOLERANCE;
	return ret;
}

/*
 * Preset TA voltage and current for Direct Charging Mode using
 * the configured cc_max and fv_uv limits. Used only on start
 */
static int pe26100_cp_preset_dcmode(struct pe26100_cp_charger *pe26100_cp)
{
	int vbat;
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: = charging_state=%u ==\n", __func__,
		pe26100_cp->charging_state);

	/* gcpm set ->cc_max and ->fv_uv before starting */
	if (pe26100_cp->cc_max < 0 || pe26100_cp->fv_uv < 0) {
		dev_err(pe26100_cp->dev, "%s: cc_max=%d fv_uv=%d invalid\n", __func__,
		       pe26100_cp->cc_max, pe26100_cp->fv_uv);
		return -EINVAL;
	}

	if (pe26100_cp->charging_state != DC_STATE_PRESET_DC)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_PRESET_DC);

	pe26100_cp->charging_state = DC_STATE_PRESET_DC;

	/* VBAT is over threshold but it might be "bouncy" due to transitory */
	ret = pe26100_chg_read_vbatt(pe26100_cp->core, &vbat);
	if (ret < 0)
		goto error;

	/* v_float is set on start from GCPM */
	if (vbat > pe26100_cp->fv_uv) {
		uint8_t val;
		int err = pe26100_chg_reg_read(pe26100_cp->core, PE26100_CHG_IC_ENABLE, &val);

		dev_err(pe26100_cp->dev, "%s: vbat adc=%d is higher than VFLOAT=%d IC_EN: %d (%d)\n",
			__func__, vbat, pe26100_cp->fv_uv, val, err);
		ret = -EINVAL;
		goto error;
	}

	/* determined by ->cfg_iin and cc_max */
	pe26100_cp->ta_max_cur = pe26100_cp_get_iin_max(pe26100_cp, pe26100_cp->cc_max);

	/* Check the TA type and set the charging mode */
	if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
		if (pe26100_cp->ftm_mode)
			goto error;

		/*
		 * Set the RX max voltage to enough high value to find RX
		 * maximum voltage initially
		 */
		pe26100_cp->ta_max_vol = PE26100_CP_TA_MAX_VOL_3_1;

		/* Get the RX max current/voltage(RX_MAX_CUR/VOL) */
		ret = pe26100_cp_get_rx_max_power(pe26100_cp);
		if (ret < 0) {
			dev_err(pe26100_cp->dev, "%s: no RX voltage to support 3:1 (%d)\n",
				__func__, ret);
			pe26100_cp->chg_mode = CHG_NO_DC_MODE;
			goto error;
		}

		ret = pe26100_cp_set_wireless_dc(pe26100_cp, vbat);
		if (ret < 0) {
			dev_err(pe26100_cp->dev, "%s: set wired failed (%d)\n", __func__, ret);
			pe26100_cp->chg_mode = CHG_NO_DC_MODE;
			goto error;
		}

		logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
				"Preset DC, rx_max_vol=%u, rx_max_cur=%u, rx_max_pwr=%lu, iin_cc=%u, chg_mode=%u",
				pe26100_cp->ta_max_vol, pe26100_cp->ta_max_cur,
				pe26100_cp->ta_max_pwr, pe26100_cp->iin_cc, pe26100_cp->chg_mode);
	} else {
		if (!pe26100_cp->ftm_mode)
			ret = pe26100_cp_set_chg_mode_by_apdo(pe26100_cp);
		if (ret < 0) {
			pe26100_cp_vote_dc_avail(pe26100_cp, 0);
			pe26100_cp->charging_state = DC_STATE_ERROR;
			goto error;
		}
		dev_dbg(pe26100_cp->dev, "%s: ta_max_cur=%u, iin_cfg=%u, pe26100_cp->ta_type=%d\n",
			__func__, pe26100_cp->ta_max_cur, pe26100_cp->pdata->iin_cfg,
			pe26100_cp->ta_type);

		/*
		 * ->ta_max_cur is too high for startup, needs to target
		 * CC before hitting max current AND work to ta_max_cur
		 * from there.
		 */
		ret = pe26100_cp_set_wired_dc(pe26100_cp, vbat);
		if (ret < 0) {
			dev_err(pe26100_cp->dev, "%s: set wired failed (%d)\n", __func__, ret);
			pe26100_cp->chg_mode = CHG_NO_DC_MODE;
			goto error;
		}

		logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
				"Preset DC, objpos=%d ta_max_vol=%u, ta_max_cur=%u, ta_max_pwr=%lu, iin_cc=%u, chg_mode=%u",
				pe26100_cp->ta_objpos, pe26100_cp->ta_max_vol,
				pe26100_cp->ta_max_cur,	pe26100_cp->ta_max_pwr, pe26100_cp->iin_cc,
				pe26100_cp->chg_mode);
	}

error:
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

/* Preset direct charging configuration and start charging */
static int pe26100_cp_preset_config(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);

	mutex_lock(&pe26100_cp->lock);

	dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			pe26100_cp->charging_state, DC_STATE_PRESET_DC);

	pe26100_cp->charging_state = DC_STATE_PRESET_DC;

	/* wait till ta_vol has reached initial set value */
	if (pe26100_cp->mpp && pe26100_cp->ta_type == TA_TYPE_WIRELESS
	    && pe26100_cp->wlc_rx_vol_retry_cnt
	    && pe26100_cp->wlc_rx_vol_check) {
		int rx_vol_now;

		rx_vol_now = pe26100_cp_get_rx_voltage(pe26100_cp);
		if (rx_vol_now <= pe26100_cp->ta_vol - PE26100_CP_WLC_VOL_TOLERANCE) {
			pe26100_cp->wlc_rx_vol_retry_cnt--;
			dev_info(pe26100_cp->dev, "%s: rx_vol_now %d not within %d of ta_vol %d\n",
				 __func__, rx_vol_now, PE26100_CP_WLC_VOL_TOLERANCE,
				 pe26100_cp->ta_vol);

			mutex_unlock(&pe26100_cp->lock);
			mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
					 msecs_to_jiffies(pe26100_cp->wcrx_vol_delay));
			return 0;
		}

		dev_info(pe26100_cp->dev, "%s: rx_vol_now %d within %d of ta_vol %d\n",
			__func__, rx_vol_now, PE26100_CP_WLC_VOL_TOLERANCE,
			pe26100_cp->ta_vol);
	}

	if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
		ret = pe26100_chg_reg_write(pe26100_cp->core, 0xF6, 0xA5);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xF7, 0x96);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xA4, 0xEF);
		msleep(1);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE,
						PE26100_CHG_MODE_EXTG_EN_MASK |
						(1 << PE26100_CHG_MODE_EXTGX_SHIFT) |
						PE26100_CHG_MODE_PT_EN_MASK |
						BIT(2) |
						BIT(1));
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xA4, 0x0);
		msleep(100);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IC_ENABLE, 0x0);
		msleep(10);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IC_ENABLE, 0x1);
		msleep(10);

		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xF6, 0xA5);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xF7, 0x96);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x8F, 0xC0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x4A, 0x8);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x92, 0xC0);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x56, 0x12);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0x6B, 0x87);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xA4, 0xEF);
		msleep(1);
		ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_MODE,
						PE26100_CHG_MODE_EXTG_EN_MASK |
						(1 << PE26100_CHG_MODE_EXTGX_SHIFT) |
						BIT(1));
		ret |= pe26100_chg_reg_write(pe26100_cp->core, 0xA4, 0x0);
		if (ret)
			goto error;

		ret = pe26100_chg_register_reg_profile(pe26100_cp->core,
						PE26100_CHG_REG_PROFILE_CP_3_1,
						cp_conf_3_1,
						(sizeof(cp_conf_3_1) /
						sizeof(struct pe26100_chg_default_reg)));
		if (ret)
			goto error;

		ret = pe26100_chg_apply_default_reg_config(pe26100_cp->core,
								PE26100_CHG_REG_PROFILE_CP_3_1);
		if (ret)
			goto error;

		ret = pe26100_cp_set_prot(pe26100_cp);
		if (ret)
			goto error;
	} else {
		ret = pe26100_chg_register_reg_profile(pe26100_cp->core,
				PE26100_CHG_REG_PROFILE_CP_2_1,
				cp_conf_2_1,
				(sizeof(cp_conf_2_1) /
				sizeof(struct pe26100_chg_default_reg)));
		if (ret)
			goto error;

		ret = pe26100_chg_apply_default_reg_config(pe26100_cp->core,
							   PE26100_CHG_REG_PROFILE_CP_2_1);
		if (ret)
			goto error;

		ret = pe26100_cp_set_prot(pe26100_cp);
		if (ret)
			goto error;

	}

	/* ->iin_cc and ->fv_uv are configured externally */
	ret = pe26100_cp_set_input_current(pe26100_cp, pe26100_cp->pdata->iin_cfg);
	if (ret < 0)
		goto error;

	ret = pe26100_cp_set_vfloat(pe26100_cp, pe26100_cp->fv_uv);
	if (ret < 0)
		goto error;

	/* Enable PE26100_CP unless aready enabled */
	ret = pe26100_cp_set_charging(pe26100_cp, true);
	if (ret < 0)
		goto error;

	/* Clear previous iin adc */
	pe26100_cp->prev_iin = 0;
	pe26100_cp->prev_inc = INC_NONE;

	/* Go to CHECK_ACTIVE state after 500ms */
	pe26100_cp->timer_id = TIMER_CHECK_ACTIVE;
	pe26100_cp->timer_period = PE26100_CP_ENABLE_DELAY_T;
	mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
			   msecs_to_jiffies(pe26100_cp->timer_period));
error:
	mutex_unlock(&pe26100_cp->lock);
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

/*
 * Check the charging status at start before entering the adjust cc mode or
 * from pe26100_cp_send_message() after a failure.
 */
static int pe26100_cp_check_active_state(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: ======START=======\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: = charging_state=%u ==\n", __func__,
		 pe26100_cp->charging_state);

	mutex_lock(&pe26100_cp->lock);

	if (pe26100_cp->charging_state != DC_STATE_CHECK_ACTIVE)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CHECK_ACTIVE);

	pe26100_cp->charging_state = DC_STATE_CHECK_ACTIVE;

	ret = pe26100_cp_check_error(pe26100_cp);
	if (ret == 0) {
		/* PE26100_CP is active state */
		pe26100_cp->retry_cnt = 0;
		pe26100_cp->timer_id = TIMER_ADJUST_CCMODE;
		pe26100_cp->timer_period = 0;

		if (pe26100_cp->mpp && pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
			dev_info(pe26100_cp->dev, "%s: setting mpp gpio to 1\n", __func__);
			pe26100_cp->pdata->mpp_gpio = devm_gpiod_get(pe26100_cp->dev, "pe26100,mpp",
								     GPIOD_OUT_HIGH);
			devm_gpiod_put(pe26100_cp->dev, pe26100_cp->pdata->mpp_gpio);
		} else {
			/* Enable IIN_UCF */
			ret = pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_FLT_MASK1, 0);
		}
	} else {
		int ret1 = pe26100_dump_all_regs(pe26100_cp->core);

		if (ret1)
			dev_err(pe26100_cp->dev, "%s: Error dumping regs (%d)\n", __func__, ret1);
	}

	/* Implement error handler function if it is needed */
	if (ret < 0) {
		logbuffer_prlog(pe26100_cp, LOGLEVEL_ERR,
				"%s: charging_state=%d, not active or error (%d)",
				__func__, pe26100_cp->charging_state, ret);
		if (ret != -EAGAIN) {
			pe26100_cp->timer_id = TIMER_ID_NONE;
			dev_err(pe26100_cp->dev, "%s: Error! disabling pe26100_cp: ret(%d)\n",
				__func__, ret);
			pe26100_cp_vote_dc_avail(pe26100_cp, 0);
			pe26100_cp->charging_state = DC_STATE_ERROR;
		}
		pe26100_cp->timer_period = 0;
	}

	if (!pe26100_cp->ftm_mode)
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));
	mutex_unlock(&pe26100_cp->lock);
	return ret;
}

/* Enter direct charging algorithm */
static int pe26100_cp_start_direct_charging(struct pe26100_cp_charger *pe26100_cp)
{
	struct pe26100_cp_chg_stats *chg_data = &pe26100_cp->chg_data;
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: =========START=========\n", __func__);
	mutex_lock(&pe26100_cp->lock);

	/* configure DC charging type for the requested index */
	if (!pe26100_cp->ftm_mode)
		ret = pe26100_cp_set_ta_type(pe26100_cp, pe26100_cp->pps_index);
	dev_info(pe26100_cp->dev, "%s: Current ta_type=%d, chg_mode=%d\n", __func__,
		pe26100_cp->ta_type, pe26100_cp->chg_mode);
	if (ret < 0)
		goto error_done;

	/* wake lock */
	__pm_stay_awake(pe26100_cp->monitor_wake_lock);

	/* Preset charging configuration and TA condition */
	ret = pe26100_cp_preset_dcmode(pe26100_cp);
	if (ret == 0) {
		/* Configure the TA  and start charging */
		pe26100_cp->timer_id = TIMER_PDMSG_SEND;
		pe26100_cp->timer_period = 0;

		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));
	}

error_done:
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);

	pe26100_cp_chg_stats_update(chg_data, pe26100_cp);
	mutex_unlock(&pe26100_cp->lock);
	return ret;
}

/* Check Vbat minimum level to start direct charging */
static int pe26100_cp_check_vbatmin(struct pe26100_cp_charger *pe26100_cp)
{
	int ret = 0, vbat;

	dev_dbg(pe26100_cp->dev, "%s: =========START=========\n", __func__);

	mutex_lock(&pe26100_cp->lock);

	if (pe26100_cp->charging_state != DC_STATE_CHECK_VBAT)
		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CHECK_VBAT);

	pe26100_cp->charging_state = DC_STATE_CHECK_VBAT;

	if (pe26100_cp_hw_init(pe26100_cp))
		goto error;

	ret = pe26100_cp_get_batt_info(pe26100_cp, BATT_VOLTAGE, &vbat);
	if (ret < 0) {
		goto error;
	} else if (vbat <= PE26100_CP_DC_VBAT_MIN) {
		ret = -EAGAIN;
		pe26100_cp->error = PE26100_CP_ERROR_LOW_VBATT;
		goto error;
	}

	/* wait for hw init and CPM to send in the params */
	if (pe26100_cp->cc_max < 0 || pe26100_cp->fv_uv < 0) {
		dev_info(pe26100_cp->dev, "%s: not yet fv_uv=%d, cc_max=%d vbat=%d\n",
			 __func__, pe26100_cp->fv_uv, pe26100_cp->cc_max, vbat);

		/* retry again after 1sec */
		pe26100_cp->timer_id = TIMER_VBATMIN_CHECK;
		pe26100_cp->timer_period = PE26100_CP_VBATMIN_CHECK_T;
		pe26100_cp->retry_cnt += 1;
	} else {
		logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
				"%s: starts at fv_uv=%d, cc_max=%d vbat=%d (min=%d)",
				__func__, pe26100_cp->fv_uv, pe26100_cp->cc_max, vbat,
				PE26100_CP_DC_VBAT_MIN);

		pe26100_cp->timer_id = TIMER_PRESET_DC;
		pe26100_cp->timer_period = 50;
		pe26100_cp->retry_cnt = 0; /* start charging */
	}

	/* timeout for VBATMIN or charging parameters */
	if (pe26100_cp->retry_cnt > PE26100_CP_MAX_RETRY_CNT) {
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: TIMEOUT fv_uv=%d, cc_max=%d vbat=%d limit=%d",
				__func__, pe26100_cp->fv_uv, pe26100_cp->cc_max, vbat,
				PE26100_CP_DC_VBAT_MIN);
		ret = -ETIMEDOUT;
	} else {
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));
	}


error:
	mutex_unlock(&pe26100_cp->lock);
	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

/*
 * return 1 if apdo switch, 0 if no switch. < 0 on err.
 * TODO : lower input current per TCPC spec
 */
static int pe26100_cp_check_apdo_switch(struct pe26100_cp_charger *pe26100_cp)
{
	unsigned int ta_max_vol, ta_max_cur, ta_objpos;
	unsigned int new_ta_cur, new_ta_max_cur, val;
	int ret;

	dev_dbg(pe26100_cp->dev, "%s: START: ta_vol: %d, prev_ta_vol: %d, ta_cur: %d, prev_ta_cur: %d\n",
		__func__, pe26100_cp->ta_vol, pe26100_cp->prev_ta_vol, pe26100_cp->ta_cur,
		pe26100_cp->prev_ta_cur);

	ta_max_vol = pe26100_cp->ta_vol;
	ta_max_cur = pe26100_cp->ta_cur;

	ret = pe26100_cp_get_apdo_index(pe26100_cp, &ta_max_vol, &ta_max_cur, &ta_objpos);
	if (ret) {
		dev_dbg(pe26100_cp->dev, "%s: error getting apdo index (%d)\n", __func__, ret);
		return ret;
	}

	if (pe26100_cp->prev_ta_cur == pe26100_cp->ta_cur &&
	    pe26100_cp->prev_ta_vol == pe26100_cp->ta_vol) {
		pe26100_cp->ta_objpos = ta_objpos;
		return 0;
	}

	if (ta_objpos == pe26100_cp->ta_objpos) {
		dev_dbg(pe26100_cp->dev, "%s: stay at apdo %d\n", __func__, ta_objpos);
		return 0;
	}

	if (pe26100_cp->prev_ta_cur < pe26100_cp->ta_cur) {
		/* Should never happen as we limit to ta_max_cur */
		dev_err(pe26100_cp->dev, "%s: ta_cur: %d > ta_max_cur %d causing APDO switch\n",
				__func__, pe26100_cp->ta_cur, pe26100_cp->ta_max_cur);
		pe26100_cp->ta_cur = pe26100_cp->ta_max_cur;
		ret = -EINVAL;
	} else if (pe26100_cp->prev_ta_vol != pe26100_cp->ta_vol) {
		const long power = (pe26100_cp->prev_ta_cur / 1000) *
				   (pe26100_cp->prev_ta_vol / 1000);

		new_ta_cur = (power / (pe26100_cp->ta_vol / 1000)) * 1000;
		new_ta_max_cur = new_ta_cur;
		ta_max_vol = pe26100_cp->ta_vol;
		dev_dbg(pe26100_cp->dev, "%s: find new ta_cur: ta_vol: %d, ta_cur: %d\n",
			__func__, pe26100_cp->ta_vol, new_ta_cur);
		ret = pe26100_cp_get_apdo_index(pe26100_cp, &ta_max_vol, &new_ta_max_cur,
						&ta_objpos);
		if (ret) {
			new_ta_max_cur = 0;
			ta_max_vol = pe26100_cp->ta_vol;
			ret = pe26100_cp_get_apdo_index(pe26100_cp, &ta_max_vol, &new_ta_max_cur,
							&ta_objpos);
			if (ret) {
				dev_err(pe26100_cp->dev, "No available APDO to switch to (%d)\n",
					ret);
			} else {
				/* APDO can't provide needed ta_cur, so limit to max */
				val = new_ta_max_cur / PD_MSG_TA_CUR_STEP;
				pe26100_cp->ta_cur = val * PD_MSG_TA_CUR_STEP -
						     PE26100_TA_CUR_TOLERANCE;
				pe26100_cp->ta_max_cur = pe26100_cp->ta_cur;
			}
		} else {
			val = new_ta_cur / PD_MSG_TA_CUR_STEP;
			pe26100_cp->ta_cur = val * PD_MSG_TA_CUR_STEP -
					     PE26100_TA_CUR_TOLERANCE;
			pe26100_cp->ta_max_cur = new_ta_max_cur;
		}
	}

	if (!ret && ta_objpos != pe26100_cp->ta_objpos) {
		const int temp_cur = pe26100_cp->ta_cur;

		dev_info(pe26100_cp->dev, "ta_vol: %d->%d, ta_cur: %d->%d, ta_pos: %d->%d\n",
			pe26100_cp->prev_ta_vol, pe26100_cp->ta_vol, pe26100_cp->prev_ta_cur,
			pe26100_cp->ta_cur,
			pe26100_cp->ta_objpos, ta_objpos);

		pe26100_cp->ta_objpos = ta_objpos;

		/* Send one message immediately */
		/* force only voltage change */
		pe26100_cp->ta_cur = pe26100_cp->prev_ta_cur;
		ret = pe26100_cp_send_pd_message(pe26100_cp, PD_MSG_REQUEST_APDO);
		if (ret < 0)
			return ret;
		pe26100_cp->ta_cur = temp_cur;
		ret = 1;
	}

	return ret;
}

static int pe26100_cp_set_iin_cc_from_power(struct pe26100_cp_charger *pe26100_cp)
{
	int power_tgt = pe26100_cp->power;
	unsigned long long iin;

	if (pe26100_cp->ta_vol == 0)
		return -EINVAL;

	/* power limit based control for MPP HPM (25W) */
	iin = ((unsigned long long)power_tgt * 1000) / (pe26100_cp->ta_vol / 1000);
	iin = iin * 1000;
	pe26100_cp->iin_cc = min_t(int, pe26100_cp_get_iin_limit(pe26100_cp), iin);
	dev_info(pe26100_cp->dev, "%s: iin_cc=%d, power=%d, ta_vol=%d\n",
			__func__, pe26100_cp->iin_cc, power_tgt, pe26100_cp->ta_vol);
	return 0;
}

static int pe26100_cp_send_message(struct pe26100_cp_charger *pe26100_cp)
{
	int val, ret;
	const int timer_id = pe26100_cp->timer_id;

	/* Go to the next state */
	mutex_lock(&pe26100_cp->lock);

	dev_dbg(pe26100_cp->dev, "%s: ====== START =======\n", __func__);

	if (pe26100_cp->ftm_mode)
		goto skip_pps;

	pe26100_cp->ta_vol = min(pe26100_cp->ta_vol, pe26100_cp->ta_max_vol);

	/* Adjust TA current and voltage step */
	if (pe26100_cp->ta_type == TA_TYPE_WIRELESS) {
		int power_tgt = pe26100_cp->power;

		/* RX voltage resolution is 40mV */
		val = pe26100_cp->ta_vol / WCRX_VOL_STEP_SIZE;
		pe26100_cp->ta_vol = val * WCRX_VOL_STEP_SIZE;

		dev_dbg(pe26100_cp->dev, "power: %d, ta_vol: %d, prev_ta_vol: %d, no_inc: %d\n",
			 pe26100_cp->power, pe26100_cp->ta_vol, pe26100_cp->prev_ta_vol,
			 pe26100_cp->no_inc_ta_vol);

		if (power_tgt && pe26100_cp->ta_vol != pe26100_cp->prev_ta_vol)
			pe26100_cp_set_iin_cc_from_power(pe26100_cp);

		/* power limit based control for MPP HPM (25W) */
		if (pe26100_cp->no_inc_ta_vol && pe26100_cp->ta_vol > pe26100_cp->prev_ta_vol) {
			pe26100_cp->ta_vol = pe26100_cp->prev_ta_vol;
			dev_dbg(pe26100_cp->dev, "%s: updating ta_vol to %d no_inc: %d\n", __func__,
				pe26100_cp->ta_vol, pe26100_cp->no_inc_ta_vol);
			goto skip_pps;
		}

		/* Set RX voltage */
		dev_dbg(pe26100_cp->dev, "%s: ta_type=%d, ta_vol=%d\n", __func__,
			pe26100_cp->ta_type, pe26100_cp->ta_vol);
		ret = pe26100_cp_send_rx_voltage(pe26100_cp, WCRX_REQUEST_VOLTAGE);
		if (ret == 0) {
			pe26100_cp->prev_ta_vol = pe26100_cp->ta_vol;
			dev_dbg(pe26100_cp->dev, "%s: updating prev_ta_vol to %d\n", __func__,
				pe26100_cp->ta_vol);
		} else if (ret == -EAGAIN && pe26100_cp->prev_ta_vol) {
			pe26100_cp->ta_vol = pe26100_cp->prev_ta_vol;
			dev_dbg(pe26100_cp->dev, "%s: updating ta_vol to %d\n", __func__,
				pe26100_cp->ta_vol);
		}
	} else {
		/* PPS voltage resolution is 20mV */
		val = pe26100_cp->ta_vol / PD_MSG_TA_VOL_STEP;
		pe26100_cp->ta_vol = val * PD_MSG_TA_VOL_STEP;
		/* PPS current resolution is 50mA */
		val = pe26100_cp->ta_cur / PD_MSG_TA_CUR_STEP;
		pe26100_cp->ta_cur = val * PD_MSG_TA_CUR_STEP;
		/* PPS minimum current is 1000mA */
		if (pe26100_cp->ta_cur < PE26100_CP_TA_MIN_CUR)
			pe26100_cp->ta_cur = PE26100_CP_TA_MIN_CUR;

		dev_dbg(pe26100_cp->dev, "%s: ta_type=%d, ta_vol=%d ta_cur=%d\n", __func__,
				pe26100_cp->ta_type, pe26100_cp->ta_vol, pe26100_cp->ta_cur);

		if (!pe26100_cp->prev_ta_cur)
			pe26100_cp->prev_ta_cur = pe26100_cp->ta_cur;
		if (!pe26100_cp->prev_ta_vol)
			pe26100_cp->prev_ta_vol = pe26100_cp->ta_vol;

		pe26100_cp_check_apdo_switch(pe26100_cp);
		/* Send PD Message */
		ret = pe26100_cp_send_pd_message(pe26100_cp, PD_MSG_REQUEST_APDO);
		if (ret >= 0) {
			pe26100_cp->prev_ta_cur = pe26100_cp->ta_cur;
			pe26100_cp->prev_ta_vol = pe26100_cp->ta_vol;
		}
	}

skip_pps:
	switch (pe26100_cp->charging_state) {
	case DC_STATE_PRESET_DC:
		if (ret != -EAGAIN)
			pe26100_cp->timer_id = TIMER_PRESET_CONFIG;
		break;
	case DC_STATE_ADJUST_CC:
		pe26100_cp->timer_id = TIMER_ADJUST_CCMODE;
		break;
	case DC_STATE_CC_MODE:
		pe26100_cp->timer_id = TIMER_CHECK_CCMODE;
		break;
	case DC_STATE_START_CV:
		pe26100_cp->timer_id = TIMER_ENTER_CVMODE;
		break;
	case DC_STATE_CV_MODE:
		pe26100_cp->timer_id = TIMER_CHECK_CVMODE;
		break;
	case DC_STATE_ADJUST_TAVOL:
		pe26100_cp->timer_id = TIMER_ADJUST_TAVOL;
		break;
	case DC_STATE_ADJUST_TACUR:
		pe26100_cp->timer_id = TIMER_ADJUST_TACUR;
		break;
	case DC_STATE_ERROR_RECOVER:
		pe26100_cp->timer_id = TIMER_ERROR_RECOVER;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == -EAGAIN)
		ret = 0;

	if (ret < 0) {
		dev_err(pe26100_cp->dev, "%s: Error-send_pd_message to %d (%d)\n",
			__func__, pe26100_cp->ta_type, ret);
		pe26100_cp->timer_id = TIMER_CHECK_ACTIVE;
	}

	/* Ensure both TA voltage and current get set before enabling charging */
	if (pe26100_cp->ftm_mode)
		pe26100_cp->timer_period = 0;
	else if (pe26100_cp->timer_id == TIMER_ERROR_RECOVER)
		pe26100_cp->timer_period = PE26100_CP_ERROR_WAIT_T;
	else if (pe26100_cp->ta_type == TA_TYPE_WIRELESS)
		pe26100_cp->timer_period = pe26100_cp->wcrx_vol_delay;
	else if (pe26100_cp->timer_id == TIMER_PRESET_CONFIG)
		pe26100_cp->timer_period = PE26100_CP_TA_CONFIG_WAIT_T;
	else if ((pe26100_cp->charging_state == DC_STATE_CV_MODE) ||
		 (pe26100_cp->charging_state == DC_STATE_START_CV))
		pe26100_cp->timer_period = PE26100_CP_CVMODE_CHECK_T;
	else
		pe26100_cp->timer_period = PE26100_CP_PDMSG_WAIT_T;

	logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
			"%s: charging_state=%u timer_id:%d->%d ret=%d",
			__func__, pe26100_cp->charging_state,
			timer_id, pe26100_cp->timer_id, ret);

	mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
			 msecs_to_jiffies(pe26100_cp->timer_period));

	dev_dbg(pe26100_cp->dev, "%s: End: timer_id=%d timer_period=%lu\n", __func__,
		 pe26100_cp->timer_id, pe26100_cp->timer_period);

	mutex_unlock(&pe26100_cp->lock);
	return ret;
}

/* delayed work function for charging timer */
static void pe26100_cp_timer_work(struct work_struct *work)
{
	struct pe26100_cp_charger *pe26100_cp =
		container_of(work, struct pe26100_cp_charger, timer_work.work);
	unsigned int charging_state;
	int timer_id;
	int ret = 0;
	bool retries = false;

	dev_dbg(pe26100_cp->dev, "%s: ========= START =========\n", __func__);

	/* TODO: remove locks from the calls and run all of this locked */
	mutex_lock(&pe26100_cp->lock);

	pe26100_cp_chg_stats_update(&pe26100_cp->chg_data, pe26100_cp);
	charging_state = pe26100_cp->charging_state;
	timer_id = pe26100_cp->timer_id;

	dev_dbg(pe26100_cp->dev, "%s: timer id=%d, charging_state=%u\n", __func__,
		 pe26100_cp->timer_id, charging_state);

	mutex_unlock(&pe26100_cp->lock);

	switch (timer_id) {

	/* charging_state <- DC_STATE_CHECK_VBAT */
	case TIMER_VBATMIN_CHECK:
		ret = pe26100_cp_check_vbatmin(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	/* charging_state <- DC_STATE_PRESET_DC */
	case TIMER_PRESET_DC:
		ret = pe26100_cp_start_direct_charging(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	/*
	 * charging_state <- DC_STATE_PRESET_DC
	 *	preset configuration, start charging
	 */
	case TIMER_PRESET_CONFIG:
		ret = pe26100_cp_preset_config(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	/*
	 * charging_state <- DC_STATE_PRESET_DC
	 *	150 ms after preset_config
	 */
	case TIMER_CHECK_ACTIVE:
		ret = pe26100_cp_check_active_state(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	case TIMER_ADJUST_CCMODE:
		ret = pe26100_cp_charge_adjust_ccmode(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	case TIMER_CHECK_CCMODE:
		ret = pe26100_cp_charge_ccmode(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	case TIMER_ENTER_CVMODE:
		/* Enter Pre-CV mode */
		ret = pe26100_cp_charge_start_cvmode(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	case TIMER_CHECK_CVMODE:
		ret = pe26100_cp_charge_cvmode(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	case TIMER_PDMSG_SEND:
		ret = pe26100_cp_send_message(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	/* called from 2 contexts */
	case TIMER_ADJUST_TAVOL:
		mutex_lock(&pe26100_cp->lock);

		if (pe26100_cp->ta_type == TA_TYPE_WIRELESS)
			ret = pe26100_cp_adjust_rx_voltage(pe26100_cp);
		else
			ret = pe26100_cp_adjust_ta_voltage(pe26100_cp);
		if (ret < 0) {
			mutex_unlock(&pe26100_cp->lock);
			goto error;
		}

		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));
		mutex_unlock(&pe26100_cp->lock);
		break;

	/* called from 2 contexts */
	case TIMER_ADJUST_TACUR:
		mutex_lock(&pe26100_cp->lock);
		ret = pe26100_cp_adjust_ta_current(pe26100_cp);
		if (ret < 0) {
			mutex_unlock(&pe26100_cp->lock);
			goto error;
		}

		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));
		mutex_unlock(&pe26100_cp->lock);
		break;

	case TIMER_ERROR_RECOVER:
		pe26100_cp->charging_state = DC_STATE_CHECK_VBAT;
		pe26100_cp->timer_id = TIMER_VBATMIN_CHECK;
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work, 0);
		break;

	case TIMER_ID_NONE:
		ret = pe26100_cp_stop_charging(pe26100_cp);
		if (ret < 0)
			goto error;
		break;

	default:
		break;
	}

	/* Check the charging state again */
	if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING) {
		cancel_delayed_work(&pe26100_cp->timer_work);
		cancel_delayed_work(&pe26100_cp->pps_work);
	}

	dev_dbg(pe26100_cp->dev, "%s: timer_id=%d->%d, charging_state=%u->%u, period=%ld\n",
		 __func__, timer_id, pe26100_cp->timer_id, charging_state,
		 pe26100_cp->charging_state, pe26100_cp->timer_period);

	return;

error:
	dev_dbg(pe26100_cp->dev, "%s: ========= ERROR =========\n", __func__);
	logbuffer_prlog(pe26100_cp, LOGLEVEL_ERR,
			"%s: timer_id=%d->%d, charging_state=%u->%u, period=%ld err=%d ret=%d ucp_count:%d low_batt_count:%d",
			__func__, timer_id, pe26100_cp->timer_id, charging_state,
			pe26100_cp->charging_state, pe26100_cp->timer_period, pe26100_cp->error,
			ret,
			pe26100_cp->eagain_retry_cnt, pe26100_cp->low_batt_retry_cnt);

	if (ret == -EAGAIN && pe26100_cp_err_is_retry(pe26100_cp, &retries) && retries) {
		/* Retry for IBUS UCP case */
		pe26100_cp->eagain_retry_cnt--;
		pe26100_cp->timer_id = TIMER_ERROR_RECOVER;
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work, 0);
	} else if (ret == -EAGAIN && pe26100_cp_err_is_low_batt(pe26100_cp, &retries) && retries) {
		pe26100_cp->low_batt_retry_cnt--;
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(PE26100_CP_ENABLE_DELAY_T));
	} else {
		pe26100_cp_stop_charging(pe26100_cp);
		if (ret == -EAGAIN && ((pe26100_cp_err_is_retry(pe26100_cp, &retries) && !retries)
		    || (pe26100_cp_err_is_low_batt(pe26100_cp, &retries) && !retries)))
			dev_err(pe26100_cp->dev, "%s: retry failed err:%d\n", __func__,
				pe26100_cp->error);

		pe26100_cp_vote_dc_avail(pe26100_cp, 0);
		pe26100_cp->charging_state = DC_STATE_ERROR;
	}
}

/* delayed work function for pps periodic timer */
static void pe26100_cp_pps_request_work(struct work_struct *work)
{
	struct pe26100_cp_charger *pe26100_cp = container_of(work,
					struct pe26100_cp_charger, pps_work.work);
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: =========START=========\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: = charging_state=%u ==\n", __func__,
		 pe26100_cp->charging_state);

	if (!pe26100_cp->ftm_mode)
		ret = pe26100_cp_send_pd_message(pe26100_cp, PD_MSG_REQUEST_APDO);
	if (ret < 0)
		dev_err(pe26100_cp->dev, "%s: Error-send_pd_message\n", __func__);

	/* TODO: do other background stuff */

	dev_dbg(pe26100_cp->dev, "%s: ret=%d\n", __func__, ret);
}

static int pe26100_cp_soft_reset(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;

	ret = pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IC_ENABLE, 0);
	msleep(10);
	ret |= pe26100_chg_reg_write(pe26100_cp->core, PE26100_CHG_IC_ENABLE, 1);
	msleep(10);

	return ret;
}

static int pe26100_cp_hw_init(struct pe26100_cp_charger *pe26100_cp)
{
	int ret;


	dev_info(pe26100_cp->dev, "%s: reset chip\n", __func__);
	ret = pe26100_cp_soft_reset(pe26100_cp);
	if (ret)
		goto error_done;


	pe26100_cp->ibus_ucp_disable_timestamp = 0;
	memset(pe26100_cp->safety_sts, 0, sizeof(pe26100_cp->safety_sts));
	dev_info(pe26100_cp->dev, "HW init done");
	return 0;

error_done:
	dev_err(pe26100_cp->dev, "%s: Error %d during hw_init\n", __func__, ret);
	return ret;
}

/* Returns the input current limit programmed into the charger in uA. */
int pe26100_cp_input_current_limit(struct pe26100_cp_charger *pe26100_cp)
{
	if (!pe26100_cp->mains_online)
		return -ENODATA;

	return pe26100_cp->ilim;
}

/* Returns the constant charge current requested from GCPM */
static int get_const_charge_current(struct pe26100_cp_charger *pe26100_cp)
{
	/* Charging current cannot be controlled directly */
	return pe26100_cp->cc_max;
}

#define get_boot_sec() div_u64(ktime_to_ns(ktime_get_boottime()), NSEC_PER_SEC)

/* index is the PPS source to use */
static int pe26100_cp_set_charging_enabled(struct pe26100_cp_charger *pe26100_cp, int index)
{
	int ret = 0;

	if (index < 0 || index >= PPS_INDEX_MAX)
		return -EINVAL;

	mutex_lock(&pe26100_cp->lock);

	/* Done is detected in CV when iin goes UNDER topoff. */
	if (pe26100_cp->charging_state == DC_STATE_CHARGING_DONE)
		index = 0;

	if (index == 0) {

		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: stop pps_idx=%d->%d charging_state=%d timer_id=%d",
				__func__, pe26100_cp->pps_index, index,
				pe26100_cp->charging_state,
				pe26100_cp->timer_id);

		/* this is the same as stop charging */
		pe26100_cp->pps_index = 0;

		cancel_delayed_work(&pe26100_cp->timer_work);
		cancel_delayed_work(&pe26100_cp->pps_work);

		/* will call pe26100_cp_stop_charging() in timer_work() */
		pe26100_cp->timer_id = TIMER_ID_NONE;
		pe26100_cp->timer_period = 0;
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));
	} else if (pe26100_cp->charging_state == DC_STATE_NO_CHARGING) {
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: start pps_idx=%d->%d charging_state=%d timer_id=%d",
				__func__, pe26100_cp->pps_index, index,
				pe26100_cp->charging_state,
				pe26100_cp->timer_id);

		/* Start Direct Charging on Index */
		pe26100_cp->dc_start_time = get_boot_sec();
		pe26100_cp_chg_stats_init(&pe26100_cp->chg_data);
		pe26100_cp->pps_index = index;

		dev_info(pe26100_cp->dev, "%s: charging_state=%u->%u\n", __func__,
			 pe26100_cp->charging_state, DC_STATE_CHECK_VBAT);

		/* PD is already in PE_SNK_STATE */
		pe26100_cp->charging_state = DC_STATE_CHECK_VBAT;
		pe26100_cp->timer_id = TIMER_VBATMIN_CHECK;
		pe26100_cp->timer_period = 0;
		mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work,
				 msecs_to_jiffies(pe26100_cp->timer_period));

		/* Set the initial charging step */
		power_supply_changed(pe26100_cp->mains);
	} else if (pe26100_cp->charging_state == DC_STATE_ERROR) {
		logbuffer_prlog(pe26100_cp, LOGLEVEL_DEBUG,
				"%s: error pps_idx=%d->%d charging_state=%d timer_id=%d",
				__func__, pe26100_cp->pps_index, index,
		pe26100_cp->charging_state,
		pe26100_cp->timer_id);
		ret = -EINVAL;
	}

	mutex_unlock(&pe26100_cp->lock);

	return ret;
}

static int pe26100_cp_set_ta_pwr(struct pe26100_cp_charger *pe26100_cp, int power)
{
	if (power < 0)
		return -EINVAL;

	mutex_lock(&pe26100_cp->lock);

	if (power == pe26100_cp->power)
		goto done;

	logbuffer_prlog(pe26100_cp, LOGLEVEL_INFO,
			"%s: charging_state=%d ta_pwr=%d->%d, ta_max_pwr=%lu\n",
			__func__, pe26100_cp->charging_state, pe26100_cp->power, power,
			pe26100_cp->ta_max_pwr);
	pe26100_cp->power = power;
	/* Re-calculate iin_cc */
	pe26100_cp_set_iin_cc_from_power(pe26100_cp);
done:
	mutex_unlock(&pe26100_cp->lock);
	return 0;
}

static int pe26100_cp_mains_set_property(struct power_supply *psy,
				      enum power_supply_property prop,
				      const union power_supply_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: =========START=========\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: prop=%d, val=%d\n", __func__, prop, val->intval);
	if (!pe26100_cp->init_done)
		return -EAGAIN;

	switch (prop) {

	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval == 0) {
			if (!pe26100_cp->mpp || pe26100_cp->ta_type == TA_TYPE_USBPD)
				ret = pe26100_cp_stop_charging(pe26100_cp);
			if (ret < 0)
				dev_err(pe26100_cp->dev, "%s: cannot stop charging (%d)\n",
				       __func__, ret);
		}
		pe26100_cp->mains_online = val->intval;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = pe26100_cp_set_new_vfloat(pe26100_cp, val->intval);
		break;

	/*
	 * dc charger cannot control charging current directly so need to control
	 * current on TA side resolving cc_max for TA_VOL*TA_CUT on vbat.
	 * NOTE: iin should be equivalent to iin = cc_max /2
	 */
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = pe26100_cp_set_new_cc_max(pe26100_cp, val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		pe26100_cp->ilim = val->intval;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

static int pe26100_cp_mains_get_property(struct power_supply *psy,
				     enum power_supply_property prop,
				     union power_supply_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	int intval, rc, ret = 0;

	if (!pe26100_cp->init_done)
		return -EAGAIN;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = pe26100_cp->mains_online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		mutex_lock(&pe26100_cp->lock);
		val->intval = pe26100_cp_is_present(pe26100_cp);
		if (val->intval < 0)
			val->intval = 0;
		mutex_unlock(&pe26100_cp->lock);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = pe26100_cp_const_charge_voltage(pe26100_cp);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = get_const_charge_current(pe26100_cp);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = pe26100_cp_input_current_limit(pe26100_cp);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* return the output current - uA unit */
		mutex_lock(&pe26100_cp->lock);
		rc = pe26100_chg_current_now(pe26100_cp->core, &val->intval, PE26100_CHG_MODE_CP);
		if (rc < 0)
			dev_err(pe26100_cp->dev, "Invalid IIN ADC (%d)\n", rc);
		mutex_unlock(&pe26100_cp->lock);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		mutex_lock(&pe26100_cp->lock);
		rc = pe26100_chg_read_vout(pe26100_cp->core, &intval);
		mutex_unlock(&pe26100_cp->lock);
		if (rc < 0)
			return rc;
		val->intval = intval;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		mutex_lock(&pe26100_cp->lock);
		ret = pe26100_chg_read_vbatt(pe26100_cp->core, &intval);
		mutex_unlock(&pe26100_cp->lock);
		if (ret < 0)
			return ret;
		val->intval = intval;
		break;

	/* TODO: read NTC temperature? */
	case POWER_SUPPLY_PROP_TEMP:
		mutex_lock(&pe26100_cp->lock);
		ret = pe26100_chg_read_temp(pe26100_cp->core, &intval);
		mutex_unlock(&pe26100_cp->lock);
		if (ret < 0)
			return ret;
		val->intval = intval;
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = pe26100_cp_get_charge_type(pe26100_cp);
		break;

	case POWER_SUPPLY_PROP_STATUS:
		mutex_lock(&pe26100_cp->lock);
		val->intval = pe26100_cp_get_status(pe26100_cp);
		mutex_unlock(&pe26100_cp->lock);
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = pe26100_cp_input_current_limit(pe26100_cp);
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * GBMS not visible
 * POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
 * POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
 */
static enum power_supply_property pe26100_cp_mains_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	/* same as POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT */
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int pe26100_cp_mains_is_writeable(struct power_supply *psy,
				      enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_POWER_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static int pe26100_cp_gbms_mains_set_property(struct power_supply *psy,
					  enum gbms_property prop,
					  const union gbms_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	int ret = 0;

	dev_dbg(pe26100_cp->dev, "%s: =========START=========\n", __func__);
	dev_dbg(pe26100_cp->dev, "%s: prop=%d, val=%d\n", __func__, prop, val->prop.intval);
	if (!pe26100_cp->init_done)
		return -EAGAIN;

	switch (prop) {

	case GBMS_PROP_CHARGING_ENABLED:
		if (val->prop.intval && pe26100_cp->charging_state == DC_STATE_ERROR)
			ret = -EINVAL;
		else if (!pe26100_cp->mpp || val->prop.intval == PPS_INDEX_TCPM
			 || (val->prop.intval == PPS_INDEX_DISABLED
			     && pe26100_cp->ta_type != TA_TYPE_WIRELESS)) {
			if (val->prop.intval == PPS_INDEX_TCPM)
				pe26100_chg_set_online(pe26100_cp->core, 1, PE26100_CHG_MODE_CP);
			ret = pe26100_cp_set_charging_enabled(pe26100_cp, val->prop.intval);
		}
		break;

	case GBMS_PROP_CHARGE_DISABLE:
		dev_dbg(pe26100_cp->dev, "%s: ChargeDisable %d, chg_state:%d\n", __func__,
			val->prop.intval, pe26100_cp->charging_state);

		/* Reset state on disconnect event */
		if (val->prop.intval) {
			if (pe26100_cp->charging_state == DC_STATE_ERROR)
				pe26100_cp->charging_state = DC_STATE_NO_CHARGING;
			pe26100_cp_vote_dc_avail(pe26100_cp, 1);

			pe26100_cp->eagain_retry_cnt = PE26100_CP_MAX_EAGAIN_RETRY_CNT;
			pe26100_cp->error = PE26100_CP_ERROR_NONE;
			pe26100_cp->low_batt_retry_cnt = PE26100_CP_MAX_LOW_BATT_RETRY_CNT;
			pe26100_cp->wlc_rx_vol_retry_cnt = PE26100_CP_MAX_RX_VOL_RETRY_CNT;
		}
		break;

	default:
		pr_debug("%s: route to pe26100_cp_mains_set_property, psp:%d\n", __func__, prop);
		return -ENODATA;
	}

	dev_dbg(pe26100_cp->dev, "%s: End, ret=%d\n", __func__, ret);
	return ret;
}

static int pe26100_cp_gbms_mains_get_property(struct power_supply *psy,
					  enum gbms_property prop,
					  union gbms_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	union gbms_charger_state chg_state;
	int ret = 0;

	if (!pe26100_cp->init_done)
		return -EAGAIN;

	switch (prop) {
	case GBMS_PROP_CHARGE_DISABLE:
		ret = pe26100_cp_get_charging_enabled(pe26100_cp);
		if (ret < 0)
			return ret;
		val->prop.intval = !ret;
		break;

	case GBMS_PROP_CHARGING_ENABLED:
		ret = pe26100_cp_get_charging_enabled(pe26100_cp);
		if (ret < 0)
			return ret;
		val->prop.intval = ret;
		break;

	case GBMS_PROP_CHARGE_CHARGER_STATE:
		mutex_lock(&pe26100_cp->lock);
		ret = pe26100_cp_get_chg_chgr_state(pe26100_cp, &chg_state);
		if (ret < 0)
			return ret;
		val->int64val = chg_state.v;
		mutex_unlock(&pe26100_cp->lock);
		break;

	case GBMS_PROP_CURRENT_NOW:
		mutex_lock(&pe26100_cp->lock);
		/* return the input current - uA unit */
		ret = pe26100_cp_get_icn(pe26100_cp, &val->prop.intval);
		mutex_unlock(&pe26100_cp->lock);
		if (ret < 0)
			dev_err(pe26100_cp->dev, "Invalid IIN ADC (%d)\n", ret);
		break;

	default:
		pr_debug("%s: route to pe26100_cp_mains_get_property, psp:%d\n", __func__, prop);
		return -ENODATA;
	}

	return 0;
}

static int pe26100_cp_gbms_mains_is_writeable(struct power_supply *psy,
					  enum gbms_property psp)
{
	switch (psp) {
	case GBMS_PROP_CHARGING_ENABLED:
	case GBMS_PROP_CHARGE_DISABLE:
		return 1;
	default:
		break;
	}

	return 0;
}

static struct gbms_desc pe26100_cp_mains_desc = {
	.psy_dsc.name		= "pe26100-cp-mains",
	/* b/179246019 will not look online to Android */
	.psy_dsc.type		= POWER_SUPPLY_TYPE_UNKNOWN,
	.psy_dsc.properties	= pe26100_cp_mains_properties,
	.psy_dsc.get_property	= pe26100_cp_mains_get_property,
	.psy_dsc.set_property	= pe26100_cp_mains_set_property,
	.psy_dsc.property_is_writeable = pe26100_cp_mains_is_writeable,
	.get_property		= pe26100_cp_gbms_mains_get_property,
	.set_property		= pe26100_cp_gbms_mains_set_property,
	.property_is_writeable	= pe26100_cp_gbms_mains_is_writeable,
	.psy_dsc.num_properties	= ARRAY_SIZE(pe26100_cp_mains_properties),
	.forward		= true,
};

/* -------------------------------------------------------------------------------------------*/

static int pe26100_cp_wcin_get_prop(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	int rc = 0;

	dev_dbg(pe26100_cp->dev, "%s psp:%d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		rc = pe26100_cp_input_current_limit(pe26100_cp);
		if (rc < 0)
			return rc;
		val->intval = rc;
		break;
	default:
		return -EINVAL;
	}

	if (rc < 0) {
		dev_dbg(pe26100_cp->dev, "Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int pe26100_cp_wcin_set_prop(struct power_supply *psy,
				      enum power_supply_property psp,
				      const union power_supply_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(pe26100_cp->dev, "%s psp:%d, val: %d\n", __func__, psp, val->intval);

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_POWER_LIMIT:
		ret = pe26100_cp_set_ta_pwr(pe26100_cp, val->intval);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int pe26100_cp_gbms_wcin_get_prop(struct power_supply *psy,
					   enum gbms_property psp,
					   union gbms_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);

	dev_info(pe26100_cp->dev, "%s: route to pe26100_cp_wcin_get_prop, psp:%d\n", __func__, psp);

	return -ENODATA;
}

static int pe26100_cp_gbms_wcin_set_prop(struct power_supply *psy,
					   enum gbms_property psp,
					   const union gbms_propval *val)
{
	struct pe26100_cp_charger *pe26100_cp = power_supply_get_drvdata(psy);
	int rc = 0;

	dev_dbg(pe26100_cp->dev, "%s psp:%d, val:%d, online: %d\n", __func__, psp, val->prop.intval,
		pe26100_cp->mains_online);

	switch (psp) {
	/* called from google_cpm when switching chargers */
	case GBMS_PROP_WLC_LOAD_DECREASE:
		dev_dbg(pe26100_cp->dev, "%s: GBMS_PROP_WLC_LOAD_DECREASE: %d\n", __func__,
			val->prop.intval);
		if (!pe26100_cp->mains_online || pe26100_cp->charging_state == DC_STATE_NO_CHARGING)
			return -EINVAL;
		if (val->prop.intval) {
			pe26100_cp->no_inc_ta_vol = 1;
			pe26100_cp->ta_vol -= val->prop.intval;
			pe26100_cp->timer_id = TIMER_PDMSG_SEND;
			mod_delayed_work(pe26100_cp->dc_wq, &pe26100_cp->timer_work, 0);
		} else {
			pe26100_cp->no_inc_ta_vol = 0;
		}
		break;

	case GBMS_PROP_ENABLE_SWITCH_CAP:
		if (val->prop.intval == CP_ENABLE_SWITCH_CAP) {
			if (!pe26100_cp->mains_online)
				return -EINVAL;
			pe26100_chg_set_online(pe26100_cp->core, 1, PE26100_CHG_MODE_CP);
			pe26100_cp_set_charging_enabled(pe26100_cp, PPS_INDEX_WLC);
		} else {
			pe26100_cp->maintain_fv_cc_max = true;
			if (val->prop.intval == CP_DISABLE_SWITCH_CAP_NO_RAMP_DOWN)
				pe26100_cp->wlc_no_ramp_down = 1;
			else
				pe26100_cp->wlc_no_ramp_down = 0;
			pe26100_cp_set_charging_enabled(pe26100_cp, 0);
		}
		break;
	default:
		dev_dbg(pe26100_cp->dev, "%s: route to pe26100_cp_wcin_set_prop, psp:%d\n",
			__func__, psp);
		return -ENODATA;
	}

	return rc;
}

static enum power_supply_property pe26100_cp_wcin_properties[] = {
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_POWER_LIMIT,
};

static int pe26100_cp_wcin_is_writeable(struct power_supply *psy,
				      enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_POWER_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static int pe26100_cp_gbms_wcin_is_writeable(struct power_supply *psy,
					  enum gbms_property psp)
{
	switch (psp) {
	case GBMS_PROP_WLC_LOAD_DECREASE:
	case GBMS_PROP_ENABLE_SWITCH_CAP:
		return 1;
	default:
		break;
	}

	return 0;
}

static struct gbms_desc pe26100_cp_wcin_psy_desc = {
	.psy_dsc.name = "wlcin-pe26100-cp",
	.psy_dsc.type = POWER_SUPPLY_TYPE_WIRELESS,
	.psy_dsc.properties = pe26100_cp_wcin_properties,
	.psy_dsc.num_properties = GOOGLE_WLCIN_PROP_SIZE,
	.psy_dsc.get_property = pe26100_cp_wcin_get_prop,
	.psy_dsc.set_property = pe26100_cp_wcin_set_prop,
	.psy_dsc.property_is_writeable = pe26100_cp_wcin_is_writeable,
	.get_property = pe26100_cp_gbms_wcin_get_prop,
	.set_property = pe26100_cp_gbms_wcin_set_prop,
	.property_is_writeable = pe26100_cp_gbms_wcin_is_writeable,
	.forward = true,
};

#if IS_ENABLED(CONFIG_OF)
static int of_pe26100_cp_dt(struct device *dev,
			 struct pe26100_cp_platform_data *pdata)
{
	struct device_node *np_pe26100_cp = dev->of_node;
	int ret;

	if (!np_pe26100_cp)
		return -EINVAL;

	/* irq gpio */
	pdata->irq_gpio = devm_gpiod_get(dev, "pe26100_cp,irq", GPIOD_IN);
	dev_info(dev, "irq-gpio: %d\n",
		 (IS_ERR(pdata->irq_gpio)
		 ? (int)PTR_ERR(pdata->irq_gpio)
		 : desc_to_gpio(pdata->irq_gpio)));

	/* input current limit */
	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,input-current-limit",
				   &pdata->iin_cfg_max);
	if (ret) {
		dev_warn(dev, "pe26100_cp,input-current-limit is Empty\n");
		pdata->iin_cfg_max = PE26100_CP_IIN_CFG_DFT;
	}
	pdata->iin_cfg = pdata->iin_cfg_max;
	dev_info(dev, "pe26100_cp,iin_cfg is %u\n", pdata->iin_cfg);

	/* TA max voltage limit */
	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,ta-max-vol-3_1",
				   &pdata->ta_max_vol_3_1);
	if (ret) {
		dev_warn(dev, "pe26100_cp,ta-max-vol-3_1 is Empty\n");
		pdata->ta_max_vol_3_1 = PE26100_CP_TA_MAX_VOL_3_1;
	}
	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,ta-max-vol-2_1",
				   &pdata->ta_max_vol_2_1);
	if (ret) {
		dev_warn(dev, "pe26100_cp,ta-max-vol_2_1 is Empty\n");
		pdata->ta_max_vol_2_1 = PE26100_CP_TA_MAX_VOL_2_1;
	}

	/* input topoff current */
	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,input-itopoff",
				   &pdata->iin_topoff);
	if (ret) {
		dev_warn(dev, "pe26100_cp,input-itopoff is Empty\n");
		pdata->iin_topoff = PE26100_CP_IIN_DONE_DFT;
	}
	dev_info(dev, "pe26100_cp,iin_topoff is %u\n", pdata->iin_topoff);

	/* iin offsets */
	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,iin-max-offset",
				   &pdata->iin_max_offset);
	if (ret)
		pdata->iin_max_offset = PE26100_CP_IIN_MAX_OFFSET;
	dev_info(dev, "pe26100_cp,iin_max_offset is %u\n", pdata->iin_max_offset);

	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,iin-cc_comp-offset-low",
				   &pdata->iin_cc_comp_offset_low);
	if (ret)
		pdata->iin_cc_comp_offset_low = PE26100_CP_IIN_CC_COMP_OFFSET_LOW;

	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,iin-cc_comp-offset-high",
				   &pdata->iin_cc_comp_offset_high);
	if (ret)
		pdata->iin_cc_comp_offset_high = PE26100_CP_IIN_CC_COMP_OFFSET_HIGH;

	dev_info(dev, "pe26100_cp,iin_cc_comp_offset is low:%d, high:%d\n",
		 pdata->iin_cc_comp_offset_low, pdata->iin_cc_comp_offset_high);

	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,wcrx-vol-up-step",
				   &pdata->wcrx_vol_up_step);
	if (ret)
		pdata->wcrx_vol_up_step = WCRX_VOL_STEP;
	dev_info(dev, "pe26100_cp,wcrx_vol_up_step is %u\n", pdata->wcrx_vol_up_step);

	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,wcrx-vol-down-step",
				   &pdata->wcrx_vol_down_step);
	if (ret)
		pdata->wcrx_vol_down_step = WCRX_VOL_STEP;
	dev_info(dev, "pe26100_cp,wcrx_vol_down_step is %u\n", pdata->wcrx_vol_down_step);

	ret = of_property_read_u32(np_pe26100_cp, "pe26100_cp,wcrx-vol-loop-delay",
				   &pdata->wcrx_vol_loop_delay);
	if (ret)
		pdata->wcrx_vol_loop_delay = PE26100_CP_PDMSG_WLC_WAIT_T;
	dev_info(dev, "pe26100_cp,wcrx-vol-loop-delay is %u\n", pdata->wcrx_vol_loop_delay);

#if IS_ENABLED(CONFIG_THERMAL)
	/* USBC thermal zone */
	ret = of_property_read_string(np_pe26100_cp, "google,usb-port-tz-name",
				      &pdata->usb_tz_name);
	if (ret) {
		dev_info(dev, "google,usb-port-tz-name is Empty\n");
		pdata->usb_tz_name = NULL;
	} else {
		dev_info(dev, "google,usb-port-tz-name is %s\n", pdata->usb_tz_name);
	}
#endif

	return 0;
}
#else
static int of_pe26100_cp_dt(struct device *dev,
			 struct pe26100_cp_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

#if IS_ENABLED(CONFIG_THERMAL)
static int pe26100_cp_usb_tz_read_temp(struct thermal_zone_device *tz, int *temp)
{
	struct pe26100_cp_charger *pe26100_cp = thermal_zone_device_priv(tz);
	int ret;

	if (!pe26100_cp)
		return -ENODEV;
	ret = pe26100_chg_read_temp(pe26100_cp->core, temp);

	return ret;
}

static struct thermal_zone_device_ops pe26100_cp_usb_tzd_ops = {
	.get_temp = pe26100_cp_usb_tz_read_temp,
};
#endif

static int debug_apply_offsets(void *data, u64 val)
{
	struct pe26100_cp_charger *chip = data;
	int ret;

	ret = pe26100_cp_set_new_cc_max(chip, chip->cc_max);
	dev_info(chip->dev, "Apply offsets iin_max_o=%d iin_cc_comp_o low=%d high=%d ret=%d\n",
		chip->pdata->iin_max_offset, chip->pdata->iin_cc_comp_offset_low,
		chip->pdata->iin_cc_comp_offset_high, ret);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(apply_offsets_debug_ops, NULL, debug_apply_offsets, "%#02llx\n");

static int debug_ftm_mode_get(void *data, u64 *val)
{
	struct pe26100_cp_charger *pe26100_cp = data;
	*val = pe26100_cp->ftm_mode;
	return 0;
}

static int debug_ftm_mode_set(void *data, u64 val)
{
	struct pe26100_cp_charger *pe26100_cp = data;

	if (val) {
		pe26100_cp->ftm_mode = true;
		pe26100_cp->ta_type = TA_TYPE_USBPD;
		pe26100_cp->chg_mode = CHG_2TO1_DC_MODE;
	} else {
		pe26100_cp->ftm_mode = false;
	}

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(debug_ftm_mode_ops, debug_ftm_mode_get, debug_ftm_mode_set, "%llu\n");

static int debug_pps_index_get(void *data, u64 *val)
{
	struct pe26100_cp_charger *pe26100_cp = data;

	*val = pe26100_cp->pps_index;
	return 0;
}

static int debug_pps_index_set(void *data, u64 val)
{
	struct pe26100_cp_charger *pe26100_cp = data;

	return pe26100_cp_set_charging_enabled(pe26100_cp, (int)val);
}

DEFINE_SIMPLE_ATTRIBUTE(debug_pps_index_ops, debug_pps_index_get,
			debug_pps_index_set, "%llu\n");

static ssize_t chg_stats_show(struct device *dev, struct device_attribute *attr,
				    char *buff)
{
	struct pe26100_cp_charger *pe26100_cp = dev_get_drvdata(dev);
	struct pe26100_cp_chg_stats *chg_data = &pe26100_cp->chg_data;
	const int max_size = PAGE_SIZE;
	int len = -ENODATA;

	mutex_lock(&pe26100_cp->lock);

	if (!pe26100_cp_chg_stats_valid(chg_data))
		goto exit_done;

	len = scnprintf(buff, max_size,
			"D:%#x,%#x %#x,%#x,%#x,%#x,%#x\n",
			chg_data->adapter_capabilities[0],
			chg_data->adapter_capabilities[1],
			chg_data->receiver_state[0],
			chg_data->receiver_state[1],
			chg_data->receiver_state[2],
			chg_data->receiver_state[3],
			chg_data->receiver_state[4]);
	len += scnprintf(&buff[len], max_size - len,
			"N: ovc=%d,ovc_ibatt=%d,ovc_delta=%d rcp=%d,stby=%d\n",
			chg_data->ovc_count, chg_data->ovc_max_ibatt, chg_data->ovc_max_delta,
			chg_data->rcp_count,
			chg_data->stby_count);
	len += scnprintf(&buff[len], max_size - len,
			"C: nc=%d,pre=%d,ca=%d,cc=%d,cv=%d,adj=%d\n",
			chg_data->nc_count,
			chg_data->pre_count,
			chg_data->ca_count,
			chg_data->cc_count,
			chg_data->cv_count,
			chg_data->adj_count);

exit_done:
	mutex_unlock(&pe26100_cp->lock);
	return len;
}

static ssize_t chg_stats_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pe26100_cp_charger *pe26100_cp = dev_get_drvdata(dev);

	mutex_lock(&pe26100_cp->lock);
	pe26100_cp_chg_stats_init(&pe26100_cp->chg_data);
	mutex_unlock(&pe26100_cp->lock);

	return count;
}

static DEVICE_ATTR_RW(chg_stats);

static ssize_t soft_reset_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pe26100_cp_charger *pe26100_cp = dev_get_drvdata(dev);

	pe26100_cp_hw_init(pe26100_cp);

	return count;
}
static DEVICE_ATTR_WO(soft_reset);

static int pe26100_cp_create_fs_entries(struct pe26100_cp_charger *chip)
{

	device_create_file(chip->dev, &dev_attr_chg_stats);
	device_create_file(chip->dev, &dev_attr_soft_reset);

	chip->debug_root = debugfs_create_dir("charger-pe26100_cp", NULL);
	if (IS_ERR_OR_NULL(chip->debug_root)) {
		dev_err(chip->dev, "Couldn't create debug dir\n");
		return -ENOENT;
	}

	debugfs_create_bool("wlc_no_ramp_down", 0644, chip->debug_root,
			     &chip->wlc_no_ramp_down);
	debugfs_create_u32("wlc_rampout_iin_target", 0644, chip->debug_root,
			     &chip->wlc_ramp_out_iin_target);
	debugfs_create_u32("wlc_rampout_delay", 0644, chip->debug_root,
			   &chip->wlc_ramp_out_delay);


	debugfs_create_u32("debug_level", 0644, chip->debug_root,
			   &debug_printk_prlog);
	debugfs_create_u32("no_logbuffer", 0644, chip->debug_root,
			   &debug_no_logbuffer);

	debugfs_create_u32("iin_max_offset", 0644, chip->debug_root,
			   &chip->pdata->iin_max_offset);
	debugfs_create_u32("iin_cc_comp_offset_low", 0644, chip->debug_root,
			   &chip->pdata->iin_cc_comp_offset_low);
	debugfs_create_u32("iin_cc_comp_offset_high", 0644, chip->debug_root,
			   &chip->pdata->iin_cc_comp_offset_high);
	debugfs_create_file("apply_offsets", 0644, chip->debug_root, chip,
			    &apply_offsets_debug_ops);

	debugfs_create_file("pps_index", 0644, chip->debug_root, chip,
			    &debug_pps_index_ops);
	debugfs_create_file("ftm_mode", 0644, chip->debug_root, chip,
			    &debug_ftm_mode_ops);

	debugfs_create_u32("mpp_init_ta_vol_mult", 0644, chip->debug_root,
			   &chip->init_vol_mult);
	debugfs_create_u32("mpp_init_ta_vol_offset", 0644, chip->debug_root,
			   &chip->init_vol_offset);
	debugfs_create_u32("rx_voltage_up_step", 0644, chip->debug_root,
			   &chip->pdata->wcrx_vol_up_step);
	debugfs_create_u32("rx_voltage_down_step", 0644, chip->debug_root,
			   &chip->pdata->wcrx_vol_down_step);
	debugfs_create_u32("rx_voltage_loop_delay", 0644, chip->debug_root,
			   &chip->wcrx_vol_delay);
	debugfs_create_bool("no_iin_active_check", 0644, chip->debug_root,
			    &chip->no_iin_active_check);
	return 0;
}


static int pe26100_cp_probe(struct platform_device *pdev)
{
	static const char * const battery[] = { "pe26100_cp-battery" };
	static char *wlcin_mains_name[] = { GOOGLE_WLCIN_MAINS_NAME };
	struct power_supply_config mains_cfg = {};
	struct power_supply_config wcin_psy_cfg = { 0 };
	struct pe26100_cp_platform_data *pdata;
	struct pe26100_cp_charger *pe26100_cp;
	struct device *dev = &pdev->dev;
	const char *psy_name = NULL;
	int ret;

	dev_dbg(dev, "%s: =========START=========\n", __func__);

	pe26100_cp = devm_kzalloc(dev, sizeof(*pe26100_cp), GFP_KERNEL);
	if (!pe26100_cp)
		return -ENOMEM;

	pe26100_cp->dev = dev;
	pe26100_cp->core = dev->parent;
	platform_set_drvdata(pdev, pe26100_cp);

#if IS_ENABLED(CONFIG_OF)
	if (dev->of_node) {
		pdata = devm_kzalloc(dev, sizeof(struct pe26100_cp_platform_data), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = of_pe26100_cp_dt(dev, pdata);
		if (ret < 0) {
			if (ret == -EPROBE_DEFER) {
				dev_err(dev, "Defer probe due to of_node not ready\n");
				return -EPROBE_DEFER;
			}

			dev_err(dev, "Failed to get device of_node\n");
			return -ENOMEM;
		}

		dev->platform_data = pdata;
	} else {
		pdata = dev->platform_data;
	}
#else
	pdata = dev->platform_data;
#endif
	if (!pdata)
		return -EINVAL;

	ret = get_chip_info(pe26100_cp);
	if (ret) {
		dev_err(dev, "ERROR: Cannot read chip info!\n");
		return -ENODEV;
	}

	mutex_init(&pe26100_cp->lock);
	pe26100_cp->pdata = pdata;
	pe26100_cp->charging_state = DC_STATE_NO_CHARGING;
	pe26100_cp->wlc_ramp_out_iin_target = 300000;
	pe26100_cp->wlc_ramp_out_delay = 300; /* 300 ms default */
	pe26100_cp->eagain_retry_cnt = PE26100_CP_MAX_EAGAIN_RETRY_CNT;
	pe26100_cp->low_batt_retry_cnt = PE26100_CP_MAX_LOW_BATT_RETRY_CNT;
	pe26100_cp->wcrx_vol_delay = pdata->wcrx_vol_loop_delay;
	if (of_property_read_bool(pe26100_cp->dev->of_node, "pe26100,mpp-gpio")) {
		pe26100_cp->mpp = 1;
		pe26100_cp->init_vol_mult = 3000;
		pe26100_cp->init_vol_offset = 600;
		pe26100_cp->wlc_rx_vol_retry_cnt = PE26100_CP_MAX_RX_VOL_RETRY_CNT;
		pe26100_cp->wlc_rx_vol_check = 1;
		dev_info(dev, "Product supports MPP\n");
	}

	/* Create a work queue for the direct charger */
	pe26100_cp->dc_wq = alloc_ordered_workqueue("pe26100_cp_dc_wq", WQ_MEM_RECLAIM);
	if (pe26100_cp->dc_wq == NULL) {
		dev_err(pe26100_cp->dev, "failed to create work queue\n");
		mutex_destroy(&pe26100_cp->lock);
		return -ENOMEM;
	}

	pe26100_cp->monitor_wake_lock =
		wakeup_source_register(NULL, "pe26100_cp-charger-monitor");
	if (!pe26100_cp->monitor_wake_lock) {
		dev_err(dev, "Failed to register wakeup source\n");
		destroy_workqueue(pe26100_cp->dc_wq);
		mutex_destroy(&pe26100_cp->lock);
		return -ENODEV;
	}

	/* initialize work */
	INIT_DELAYED_WORK(&pe26100_cp->timer_work, pe26100_cp_timer_work);
	pe26100_cp->timer_id = TIMER_ID_NONE;
	pe26100_cp->timer_period = 0;

	INIT_DELAYED_WORK(&pe26100_cp->pps_work, pe26100_cp_pps_request_work);
	ret = of_property_read_string(dev->of_node,
				      "pe26100_cp,psy_name", &psy_name);
	if (ret == 0) {
		pe26100_cp_mains_desc.psy_dsc.name = devm_kstrdup(dev, psy_name, GFP_KERNEL);
		if (!pe26100_cp_mains_desc.psy_dsc.name)
			return -ENOMEM;
	}

	ret = pe26100_cp_probe_pps(pe26100_cp);
	if (ret < 0) {
		dev_warn(dev, "pe26100_cp: PPS not available (%d)\n", ret);
	} else {
		const char *logname = "pe26100_cp_mains";

		pe26100_cp->log = logbuffer_register(logname);
		if (IS_ERR(pe26100_cp->log)) {
			dev_err(dev, "no logbuffer (%ld)\n", PTR_ERR(pe26100_cp->log));
			pe26100_cp->log = NULL;
		}
	}

	mains_cfg.supplied_to = (char **)battery;
	mains_cfg.num_supplicants = ARRAY_SIZE(battery);
	mains_cfg.drv_data = pe26100_cp;
	pe26100_cp->mains = devm_power_supply_register(dev,
							&pe26100_cp_mains_desc.psy_dsc,
							&mains_cfg);
	if (IS_ERR(pe26100_cp->mains)) {
		ret = -ENODEV;
		goto error;
	}

	wcin_psy_cfg.drv_data = pe26100_cp;
	wcin_psy_cfg.of_node = dev->of_node;
	wcin_psy_cfg.supplied_to = wlcin_mains_name;
	wcin_psy_cfg.num_supplicants = ARRAY_SIZE(wlcin_mains_name);
	pe26100_cp->wcin_psy = devm_power_supply_register(dev,
							  &pe26100_cp_wcin_psy_desc.psy_dsc,
							  &wcin_psy_cfg);
	if (IS_ERR(pe26100_cp->wcin_psy)) {
		dev_err(dev, "Failed to register psy rc = %ld\n", PTR_ERR(pe26100_cp->wcin_psy));
		return PTR_ERR(pe26100_cp->wcin_psy);
	}

	pe26100_cp->attrs.attrs = pe26100_cp_attr_group;
	ret = pe26100_cp_create_fs_entries(pe26100_cp);
	if (ret < 0)
		dev_err(dev, "error while registering debugfs %d\n", ret);

#if IS_ENABLED(CONFIG_THERMAL)
	if (pdata->usb_tz_name) {
		pe26100_cp->usb_tzd =
			thermal_tripless_zone_device_register(pdata->usb_tz_name,
							      pe26100_cp,
							      &pe26100_cp_usb_tzd_ops,
							      NULL);
		if (IS_ERR(pe26100_cp->usb_tzd)) {
			pe26100_cp->usb_tzd = NULL;
			ret = PTR_ERR(pe26100_cp->usb_tzd);
			dev_err(dev, "Couldn't register usb connector thermal zone ret=%d\n",
				ret);
		} else {
			thermal_zone_device_update(pe26100_cp->usb_tzd, THERMAL_DEVICE_UP);
			thermal_zone_device_enable(pe26100_cp->usb_tzd);
		}
	}
#endif

	pe26100_cp->dc_avail = NULL;
	pe26100_cp->init_done = true;
	dev_info(dev, "pe26100_cp: probe_done\n");
	return 0;

error:
	destroy_workqueue(pe26100_cp->dc_wq);
	mutex_destroy(&pe26100_cp->lock);
	wakeup_source_unregister(pe26100_cp->monitor_wake_lock);
	return ret;
}

static void pe26100_cp_remove(struct platform_device *pdev)
{
	struct pe26100_cp_charger *pe26100_cp = platform_get_drvdata(pdev);

	/* stop charging if it is active */
	pe26100_cp_stop_charging(pe26100_cp);

	if (pe26100_cp->irq) {
		free_irq(pe26100_cp->irq, pe26100_cp);
		gpiod_put(pe26100_cp->pdata->irq_gpio);
	}

	destroy_workqueue(pe26100_cp->dc_wq);

	wakeup_source_unregister(pe26100_cp->monitor_wake_lock);

#if IS_ENABLED(CONFIG_THERMAL)
	if (pe26100_cp->usb_tzd)
		thermal_zone_device_unregister(pe26100_cp->usb_tzd);
#endif
	if (pe26100_cp->log)
		logbuffer_unregister(pe26100_cp->log);
	pps_free(&pe26100_cp->pps_data);
}

static const struct platform_device_id pe26100_cp_id[] = {
	{ "pe26100-cp-charger", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, pe26100_cp_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id pe26100_cp_dt_ids[] = {
	{ .compatible = "pe26100-cp",},
	{ },
};

MODULE_DEVICE_TABLE(of, pe26100_cp_dt_ids);
#endif /* CONFIG_OF */

#if IS_ENABLED(CONFIG_PM)
#if IS_ENABLED(CONFIG_RTC_HCTOSYS)
static int get_current_time(struct pe26100_cp_charger *pe26100_cp, unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		dev_err(pe26100_cp->dev, "%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		dev_err(pe26100_cp->dev, "Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		dev_err(pe26100_cp->dev, "Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	*now_tm_sec = rtc_tm_to_time64(&tm);

close_time:
	rtc_class_close(rtc);
	return rc;
}

static void
pe26100_cp_check_and_update_charging_timer(struct pe26100_cp_charger *pe26100_cp)
{
	unsigned long current_time = 0, next_update_time, time_left;

	get_current_time(pe26100_cp, &current_time);

	if (pe26100_cp->timer_id != TIMER_ID_NONE)	{
		next_update_time = pe26100_cp->last_update_time +
				(pe26100_cp->timer_period / 1000); /* seconds */

		dev_dbg(pe26100_cp->dev, "%s: current_time=%ld, next_update_time=%ld\n",
			__func__, current_time, next_update_time);

		if (next_update_time > current_time)
			time_left = next_update_time - current_time;
		else
			time_left = 0;

		mutex_lock(&pe26100_cp->lock);
		pe26100_cp->timer_period = time_left * 1000; /* ms unit */
		mutex_unlock(&pe26100_cp->lock);
		schedule_delayed_work(&pe26100_cp->timer_work,
				msecs_to_jiffies(pe26100_cp->timer_period));

		dev_dbg(pe26100_cp->dev, "%s: timer_id=%d, time_period=%ld\n", __func__,
			 pe26100_cp->timer_id, pe26100_cp->timer_period);
	}
	pe26100_cp->last_update_time = current_time;
}
#endif

static int pe26100_cp_suspend(struct device *dev)
{
	struct pe26100_cp_charger *pe26100_cp = dev_get_drvdata(dev);

	dev_dbg(pe26100_cp->dev, "%s: cancel delayed work\n", __func__);

	/* cancel delayed_work */
	cancel_delayed_work(&pe26100_cp->timer_work);
	return 0;
}

static int pe26100_cp_resume(struct device *dev)
{
	struct pe26100_cp_charger *pe26100_cp = dev_get_drvdata(dev);

	dev_dbg(pe26100_cp->dev, "%s: update_timer\n", __func__);

	/* Update the current timer */
#if IS_ENABLED(CONFIG_RTC_HCTOSYS)
	pe26100_cp_check_and_update_charging_timer(pe26100_cp);
#else
	if (pe26100_cp->timer_id != TIMER_ID_NONE) {
		mutex_lock(&pe26100_cp->lock);
		pe26100_cp->timer_period = 0;	/* ms unit */
		mutex_unlock(&pe26100_cp->lock);
		schedule_delayed_work(&pe26100_cp->timer_work,
				      msecs_to_jiffies(pe26100_cp->timer_period));
	}
#endif
	return 0;
}
#else
#define pe26100_cp_suspend		NULL
#define pe26100_cp_resume		NULL
#endif

static const struct dev_pm_ops pe26100_cp_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pe26100_cp_suspend, pe26100_cp_resume)
};

static struct platform_driver pe26100_cp_driver = {
	.driver = {
		.name = "pe26100-cp-charger",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = pe26100_cp_dt_ids,
#endif /* CONFIG_OF */
#if IS_ENABLED(CONFIG_PM)
		.pm = &pe26100_cp_pm_ops,
#endif
	},
	.probe        = pe26100_cp_probe,
	.remove       = pe26100_cp_remove,
	.id_table     = pe26100_cp_id,


};

module_platform_driver(pe26100_cp_driver);

MODULE_AUTHOR("Prasanna Prapancham <prapancham@google.com>");
MODULE_DESCRIPTION("PE26100_CP Charger Pump Driver");
MODULE_LICENSE("GPL");
