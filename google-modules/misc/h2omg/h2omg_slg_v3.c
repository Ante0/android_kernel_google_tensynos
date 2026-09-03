// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include "h2omg.h"

#define REG_CONTROL 0x4c
#define FLD_CONTROL_REFERENCE_EN     BIT(0)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR0_EN       BIT(1)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR1_EN       BIT(2)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_ARM_FUSE         BIT(3)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR0_MODE     BIT(4)  /* 0: Wet Detect 1: Dry Detect */
#define FLD_CONTROL_SENSOR1_MODE     BIT(5)  /* 0: Wet Detect 1: Dry Detect */
#define FLD_CONTROL_ACMP             BIT(6)  /* 0: Enable  1: Disable */
#define FLD_CONTROL_FAULT_ENABLE     BIT(7)  /* 0: Disable 1: Enable  */

#define CONTROL_FUSE_ENABLE       1
#define CONTROL_FUSE_DISABLE      0

#define CONTROL_ACMP_ON           0
#define CONTROL_ACMP_OFF          1
#define CONTROL_FAULT_ENABLE_ON   1
#define CONTROL_FAULT_ENABLE_OFF  0

#define CONTROL_DISABLED          0x40
#define CONTROL_DEFAULT           0x4f

#define CONTROL_ACMP_ENTER_STEP1  0x00
#define CONTROL_ACMP_ENTER_STEP2  0x01
#define CONTROL_ACMP_RUN_ENABLE   0x01
#define CONTROL_ACMP_RUN_DISABLE  0x00
#define CONTROL_ACMP_EXIT_STEP1   0x40
#define CONTROL_ACMP_EXIT_STEP2   0x41

#define ACMP_READ_DELAY_MS        20
#define ACMP_MAX_READ 5

#define REG_STATUS0 0x48
#define FLD_STATUS_INTS0     BIT(5)
#define FLD_STATUS_INTS1     BIT(6)

#define REG_STATUS1 0x49
#define FLD_STATUS_INTRS     BIT(0)

#define REG_FUSE_STATUS 0x4d
#define FLD_FUSE_STATUS BIT(4)
#define FUSE_DELAY_MS   20

/* VIRTUAL STATUS REGISTER */
#define FLD_VSTATUS_INTS0 BIT(0)
#define FLD_VSTATUS_INTS1 BIT(1)
#define FLD_VSTATUS_INTS2 BIT(2)
#define FLD_VSTATUS_REFS0 BIT(3)
#define FLD_VSTATUS_FUSE  BIT(4)
#define FLD_VSTATUS_EXTS0 BIT(5)
#define FLD_VSTATUS_SNSRS GENMASK(2, 0)
#define FLD_VSTATUS_SNSRS_AND_REF GENMASK(3, 0)

#define REG_ACMP_STATUS 0x4f
#define FLD_ACMP_STATUS      GENMASK(2, 0)
#define FLD_ACMP_STATUS_SNS0 BIT(0)
#define FLD_ACMP_STATUS_SNS1 BIT(1)
#define FLD_ACMP_STATUS_REF0 BIT(2)

#define REG_ACMP_NONINV 0x52
#define FLD_ACMP_NONINV_VDD BIT(0)

#define REG_ACMP0H_VREF0 0x57
#define FLD_ACMP0H_VREF0_GAIN GENMASK(1, 0)

#define REG_ACMP1H_VREF0 0x58
#define FLD_ACMP1H_VREF0_GAIN GENMASK(1, 0)

#define REG_ACMP2L_VREF1 0x59
#define ACMP2L_VREF_DIV 0x60

/* Sensor Timine Settings */
#define REG_CNT5       0x8d
#define CNT5_DIV_4096  0x3a  /* OSC0 CLK / 4096 */

#define REG_CNT1_D     0x98
#define CNT1_D_25MS    0x32  /* sensor0 debounce = 25 ms */

#define REG_CNT2_D     0x9a
#define CNT2_D_25MS    0x32  /* sensor1 debounce = 25 ms */

#define REG_CNT4_D     0x9e
#define CNT4_D_20MS    0x28  /* reference debounce = 20 ms */

#define REG_CNT5_D     0xa0
#define CNT5_D_90S     0x2c  /* sensor off time 90 s */

#define REG_CNT6_D     0xa2
#define CNT6_D_ON_31MS 0x07 /* sensor on 31 ms */

#define REG_RESET      0xf5
#define RESET_VALUE    0x01
#define RESET_DELAY_MS 20

/* dual mode registers */
#define REG_LUT3_10_IN2_11_IN0   0x45
#define REG_LUT3_11_IN0_IN1      0x46
#define REG_LUT3_11_IN1_IN2      0x47
#define REG_LUT3_FUSE_TRGR_OUT   0x9b
#define REG_DFF8_FUSE_TRGR_IN    0x13
#define REG_DFF9_FUSE_TRGR_IN    0x14
#define REG_DFF9_FUSE_TRGR_MSK   0x15
#define REG_GPIO4_5              0x31
#define REG_GPIO5_6              0x32

#define LUT3_10_IN2_11_IN0_SINGLE 0x22
#define LUT3_10_IN2_11_IN0_DUAL   0xa2
#define LUT3_11_IN0_IN1_SINGLE    0x00
#define LUT3_11_IN0_IN1_DUAL      0x51
#define LUT3_11_IN1_IN2_SINGLE    0x00
#define LUT3_11_IN1_IN2_DUAL      0x20
#define LUT3_FUSE_TRGR_OUT_SINGLE 0x00
#define LUT3_FUSE_TRGR_OUT_EITHER 0x08
#define LUT3_FUSE_TRGR_OUT_SEN0   0x04
#define LUT3_FUSE_TRGR_OUT_SEN1   0x02
#define LUT3_FUSE_TRGR_OUT_NOW    0x0e
#define DFF8_FUSE_TRGR_IN_SINGLE  0x90
#define DFF8_FUSE_TRGR_IN_DUAL    0x50
#define DFF9_FUSE_TRGR_IN_SINGLE  0x8e
#define DFF9_FUSE_TRGR_IN_DUAL    0x8d
#define DFF9_FUSE_TRGR_MSK_EITHER_DRY 0x04
#define DFF9_FUSE_TRGR_MSK_SEN0_DRY 0x24
#define DFF9_FUSE_TRGR_MSK_SEN1_DRY 0x25

#define GPIO4_5_HW_OE_TO_RT_EN_N 0x60
#define GPIO4_5_HW_OE_TO_POR     0xe0
#define GPIO5_6_HW_OE_TO_RT_EN_N 0x3e
#define GPIO5_6_HW_OE_TO_POR     0x3f

#define NUM_SENSORS 2

/* slow down chip sampling rate and on time */
static int h2omg_slg_v3_update_timing_regs(struct h2omg_info *info)
{
	static const struct h2omg_reg_val timings[] = {
		{ REG_CNT5, CNT5_DIV_4096 },
		{ REG_CNT1_D, CNT1_D_25MS },
		{ REG_CNT2_D, CNT2_D_25MS },
		{ REG_CNT4_D, CNT4_D_20MS },
		{ REG_CNT5_D, CNT5_D_90S },
		{ REG_CNT6_D, CNT6_D_ON_31MS },
		{ REG_ACMP2L_VREF1, ACMP2L_VREF_DIV },
	};
	size_t update_rows = (info->vref_div) ? ARRAY_SIZE(timings) : ARRAY_SIZE(timings) - 1;

	return h2omg_update_regs(info, timings, update_rows);
}

/* configure chip to trigger either when either sensor is wet (SINGLE_TRIGGER)
 * or only when both sensors get wet at the same time (to the resolution of the chip)
 */
static int h2omg_slg_v3_set_trigger_state(struct h2omg_info *info, enum h2omg_trigger_state state)
{
	struct regmap *regmap = info->regmap;
	int err;

	switch (state) {
	case SINGLE_TRIGGER_INITIAL:
		dev_dbg(info->dev, "SINGLE_TRIGGER_INITIAL\n");
		err = regmap_write(regmap, REG_LUT3_10_IN2_11_IN0,  LUT3_10_IN2_11_IN0_SINGLE);
		if (err)
			return err;
		err = regmap_write(regmap, REG_LUT3_11_IN0_IN1, LUT3_11_IN0_IN1_SINGLE);
		if (err)
			return err;
		err = regmap_write(regmap, REG_LUT3_11_IN1_IN2, LUT3_11_IN1_IN2_SINGLE);
		if (err)
			return err;
		err = regmap_write(regmap, REG_LUT3_FUSE_TRGR_OUT, LUT3_FUSE_TRGR_OUT_SINGLE);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF8_FUSE_TRGR_IN, DFF8_FUSE_TRGR_IN_SINGLE);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_IN, DFF9_FUSE_TRGR_IN_SINGLE);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_MSK, DFF9_FUSE_TRGR_MSK_EITHER_DRY);
		if (err)
			return err;
		err = regmap_write(regmap, REG_GPIO4_5, GPIO4_5_HW_OE_TO_POR);
		if (err)
			return err;
		err = regmap_write(regmap, REG_GPIO5_6, GPIO5_6_HW_OE_TO_POR);
		if (err)
			return err;
	break;
	case DUAL_TRIGGER_INITIAL:
		dev_dbg(info->dev, "DUAL_TRIGGER_INITIAL\n");
		err = regmap_write(regmap, REG_LUT3_10_IN2_11_IN0, LUT3_10_IN2_11_IN0_DUAL);
		if (err)
			return err;
		err = regmap_write(regmap, REG_LUT3_11_IN0_IN1, LUT3_11_IN0_IN1_DUAL);
		if (err)
			return err;
		err = regmap_write(regmap, REG_LUT3_11_IN1_IN2, LUT3_11_IN1_IN2_DUAL);
		if (err)
			return err;
		err = regmap_write(regmap, REG_LUT3_FUSE_TRGR_OUT, LUT3_FUSE_TRGR_OUT_EITHER);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF8_FUSE_TRGR_IN, DFF8_FUSE_TRGR_IN_DUAL);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_IN, DFF9_FUSE_TRGR_IN_DUAL);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_MSK, DFF9_FUSE_TRGR_MSK_EITHER_DRY);
		if (err)
			return err;
		err = regmap_write(regmap, REG_GPIO4_5, GPIO4_5_HW_OE_TO_POR);
		if (err)
			return err;
		err = regmap_write(regmap, REG_GPIO5_6, GPIO5_6_HW_OE_TO_POR);
		if (err)
			return err;
	break;
	case TRIGGER_WHEN_S0_GETS_WET:
		dev_dbg(info->dev, "TRIGGER_WHEN_S0_GETS_WET\n");
		err = regmap_write(regmap, REG_LUT3_FUSE_TRGR_OUT, LUT3_FUSE_TRGR_OUT_SEN0);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_MSK, DFF9_FUSE_TRGR_MSK_SEN0_DRY);
		if (err)
			return err;
	break;
	case TRIGGER_WHEN_S1_GETS_WET:
		dev_dbg(info->dev, "TRIGGER_WHEN_S1_GETS_WET\n");
		err = regmap_write(regmap, REG_LUT3_FUSE_TRGR_OUT, LUT3_FUSE_TRGR_OUT_SEN1);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_MSK, DFF9_FUSE_TRGR_MSK_SEN1_DRY);
		if (err)
			return err;
	break;
	case TRIGGER_NOW:
		dev_info(info->dev, "TRIGGER_NOW\n");
		err = regmap_write(regmap, REG_LUT3_FUSE_TRGR_OUT, LUT3_FUSE_TRGR_OUT_NOW);
		if (err)
			return err;
		err = regmap_write(regmap, REG_DFF9_FUSE_TRGR_MSK, DFF9_FUSE_TRGR_MSK_EITHER_DRY);
		if (err)
			return err;
	break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int h2omg_slg_v3_sensor_valid(enum h2omg_sensor_id id)
{
	return (id == SENSOR0 || id == SENSOR1 || id == SENSOR_REFERENCE);
}

static int h2omg_slg_v3_sensor_enable_id2bit(enum h2omg_sensor_id id, unsigned int *bit)
{
	int err = 0;

	switch (id) {
	case SENSOR0:
			*bit = 1;
		break;
	case SENSOR1:
			*bit = 2;
		break;
	case SENSOR_REFERENCE:
			*bit = 0;
		break;
	default:
		       err = -EINVAL;
		break;
	}
	return err;
}

static unsigned int h2omg_reg_mod(unsigned int mask, unsigned int fld, unsigned int reg_val)
{
	unsigned int shft_val = fld << __bf_shf(mask);

	return (reg_val & ~mask) | shft_val;
}

/* external */
static int h2omg_slg_v3_control_get(struct h2omg_info *info, unsigned int *vreg)
{
	return regmap_read(info->regmap, REG_CONTROL, vreg);
}

/* external */
static int h2omg_slg_v3_control_set(struct h2omg_info *info, unsigned int vreg)
{
	return regmap_write(info->regmap, REG_CONTROL, vreg);
}

/* external */
static int h2omg_slg_v3_fuse_enable_get(struct h2omg_info *info, bool *enable)
{
	unsigned int vreg;
	int err;

	err = h2omg_slg_v3_control_get(info, &vreg);
	if (err)
		return err;

	*enable = FIELD_GET(FLD_CONTROL_ARM_FUSE, vreg);
	return 0;
}

/* external */
static int h2omg_slg_v3_fuse_enable_set(struct h2omg_info *info, bool enable)
{
	unsigned int ctrl;
	int err;

	err = h2omg_slg_v3_control_get(info, &ctrl);
	if (err)
		return err;
	info->control_set = u8_replace_bits(ctrl, enable, FLD_CONTROL_ARM_FUSE);
	return h2omg_slg_v3_control_set(info, info->control_set);
}

/* external */
static int h2omg_slg_v3_fuse_state_get(struct h2omg_info *info, enum h2omg_fuse_state *state)
{
	unsigned int fuse;
	unsigned int ctrl;
	int err;

	err = h2omg_slg_v3_control_get(info, &ctrl);
	if (err)
		return err;

	err = h2omg_slg_v3_control_set(info,
		u32_replace_bits(ctrl, CONTROL_ACMP_ON, FLD_CONTROL_ACMP));
	if (err)
		return err;

	msleep(FUSE_DELAY_MS);
	err = regmap_read(info->regmap, REG_FUSE_STATUS, &fuse);
	if (err)
		return err;

	err = h2omg_slg_v3_control_set(info, ctrl);
	if (err)
		return err;

	/* invert fuse value */
	*state = !FIELD_GET(FLD_FUSE_STATUS, fuse);
	return 0;
}

static int h2omg_slg_v3_status_only_get(struct h2omg_info *info, unsigned int *vreg)
{
	u8 sts[2];
	int err;

	err = regmap_raw_read(info->regmap, REG_STATUS0, sts, sizeof(sts));
	if (err)
		return err;

	*vreg = FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_STATUS_INTRS, sts[1])) |
		FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_STATUS_INTS0, sts[0])) |
		FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_STATUS_INTS1, sts[0]));

	return 0;
}

/* external */
static int h2omg_slg_v3_status_get(struct h2omg_info *info, unsigned int *vreg)
{
	enum h2omg_fuse_state fuse;
	u8 sts[2];
	int err;

	err = regmap_raw_read(info->regmap, REG_STATUS0, sts, sizeof(sts));
	if (err)
		return err;

	err = h2omg_slg_v3_fuse_state_get(info, &fuse);
	if (err)
		return err;

	*vreg = FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_STATUS_INTRS, sts[1])) |
		FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_STATUS_INTS0, sts[0])) |
		FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_STATUS_INTS1, sts[0])) |
		FIELD_PREP(FLD_VSTATUS_FUSE,  fuse);

	return 0;
}

/* external */
static int h2omg_slg_v3_sensor_enable_get(struct h2omg_info *info,
					  enum h2omg_sensor_id id,
					  bool *enable)
{
	unsigned int vreg;
	unsigned int bit;
	int err;

	err = h2omg_slg_v3_sensor_enable_id2bit(id, &bit);
	if (err)
		return err;

	err = h2omg_slg_v3_control_get(info, &vreg);
	if (err)
		return err;

	*enable = !!(vreg & 1 << bit);
	return 0;
}

/* external */
static int h2omg_slg_v3_sensor_enable_set(struct h2omg_info *info,
					  enum h2omg_sensor_id id,
					  bool enable)
{
	unsigned int ctrl;
	unsigned int mask;
	unsigned int bit;
	int err;

	err = h2omg_slg_v3_sensor_enable_id2bit(id, &bit);
	if (err)
		return err;

	mask = 1 << bit;

	err = h2omg_slg_v3_control_get(info, &ctrl);
	if (err)
		return err;
	return h2omg_slg_v3_control_set(info, h2omg_reg_mod(mask, enable, ctrl));
}

/* external */
static int h2omg_slg_v3_sensor_mode_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode *mode)
{
	unsigned int vreg;
	int err;

	if (!h2omg_slg_v3_sensor_valid(id))
		return -EINVAL;

	err = h2omg_slg_v3_control_get(info, &vreg);
	if (err)
		return err;

	if ((id == SENSOR_REFERENCE) &&
	    ((FIELD_GET(FLD_CONTROL_SENSOR0_MODE, vreg) == CONTROL_MODE_DRY_DETECT) ||
	     (FIELD_GET(FLD_CONTROL_SENSOR1_MODE, vreg) == CONTROL_MODE_DRY_DETECT)))
		*mode = DETECT_DRY;
	else if ((id == SENSOR0) &&
		   (FIELD_GET(FLD_CONTROL_SENSOR0_MODE, vreg) == CONTROL_MODE_DRY_DETECT))
		*mode = DETECT_DRY;
	else if ((id == SENSOR1) &&
		   (FIELD_GET(FLD_CONTROL_SENSOR1_MODE, vreg) == CONTROL_MODE_DRY_DETECT))
		*mode = DETECT_DRY;
	else
		*mode = DETECT_WET;

	return 0;
}

/* external */
static int h2omg_slg_v3_sensor_mode_set(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode mode)
{
	if (!h2omg_slg_v3_sensor_valid(id))
		return -EINVAL;

	return -EOPNOTSUPP;
}

static int h2omg_slg_v3_acmp_status_get(struct h2omg_info *info,
					unsigned int *sts)
{
	unsigned int last = 0xff;
	unsigned int curr = 0;
	size_t counter = 0;
	int err;

	while (counter < ACMP_MAX_READ && last != curr) {
		last = curr;
		err = regmap_read(info->regmap, REG_ACMP_STATUS, &curr);
		if (err)
			return err;
		curr = FIELD_GET(FLD_ACMP_STATUS, curr);
		counter++;
	}
	*sts = curr;
	return 0;
}


/* external */
static int h2omg_slg_v3_sensor_acmp_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					unsigned int *acmp)
{
	unsigned int val, ctrl;
	unsigned int sts0;
	int err;

	/* get initial value to restore at the end */
	err = h2omg_slg_v3_control_get(info, &ctrl);
	if (err)
		return err;

	err = regmap_write(info->regmap, REG_RESET, RESET_VALUE);
	if (err)
		return err;
	msleep(RESET_DELAY_MS);

	val = u32_replace_bits(ctrl, CONTROL_ACMP_ON, FLD_CONTROL_ACMP);
	val = u32_replace_bits(val, CONTROL_MODE_WET_DETECT, FLD_CONTROL_SENSOR0_MODE);
	val = u32_replace_bits(val, CONTROL_MODE_WET_DETECT, FLD_CONTROL_SENSOR1_MODE);
	val = u32_replace_bits(val, CONTROL_SENSOR_OFF, FLD_CONTROL_SENSOR0_EN);
	val = u32_replace_bits(val, CONTROL_SENSOR_OFF, FLD_CONTROL_SENSOR1_EN);
	val = u32_replace_bits(val, CONTROL_FUSE_DISABLE, FLD_CONTROL_ARM_FUSE);
	val = u32_replace_bits(val, CONTROL_FAULT_ENABLE_OFF, FLD_CONTROL_FAULT_ENABLE);
	err = h2omg_slg_v3_control_set(info, val);
	if (err)
		return err;

	err = h2omg_slg_v3_update_timing_regs(info);
	if (err)
		return err;

	/* If the reference sensor is disabled, the change ACMP0's noninverting input to VDD */
	if (!FIELD_GET(FLD_CONTROL_REFERENCE_EN, ctrl)) {
		unsigned int ref_sts0;
		unsigned int non_inverting;
		unsigned int vref0;
		unsigned int vref1;
		unsigned int vref_tmp;

		msleep(ACMP_READ_DELAY_MS);
		/* read reference before we change the ACMP_NONINV */
		err = h2omg_slg_v3_acmp_status_get(info, &ref_sts0);
		if (err)
			return err;

		err = regmap_read(info->regmap, REG_ACMP_NONINV, &non_inverting);
		if (err)
			return err;

		err = regmap_read(info->regmap, REG_ACMP0H_VREF0, &vref0);
		if (err)
			return err;

		err = regmap_read(info->regmap, REG_ACMP1H_VREF0, &vref1);
		if (err)
			return err;

		err = regmap_write(info->regmap, REG_ACMP_NONINV,
				   u32_replace_bits(non_inverting, 1, FLD_ACMP_NONINV_VDD));
		if (err)
			return err;

		vref_tmp = u32_replace_bits(vref0, 0x01, FLD_ACMP0H_VREF0_GAIN);
		err = regmap_write(info->regmap, REG_ACMP0H_VREF0, vref_tmp);
		if (err)
			return err;

		vref_tmp = u32_replace_bits(vref1, 0x01, FLD_ACMP1H_VREF0_GAIN);
		err = regmap_write(info->regmap, REG_ACMP1H_VREF0, vref_tmp);
		if (err)
			return err;

		msleep(ACMP_READ_DELAY_MS);

		/* read for sensor0 and sensor1 */
		err = h2omg_slg_v3_acmp_status_get(info, &sts0);
		if (err)
			return err;

		/* put the reference value back */
		sts0 = u32_replace_bits(sts0,
					FIELD_GET(FLD_ACMP_STATUS_REF0, ref_sts0),
					FLD_ACMP_STATUS_REF0);

		/* put register things back */
		err = regmap_write(info->regmap, REG_ACMP_NONINV, non_inverting);
		if (err)
			return err;

		err = regmap_write(info->regmap, REG_ACMP0H_VREF0, vref0);
		if (err)
			return err;

		err = regmap_write(info->regmap, REG_ACMP1H_VREF0, vref1);
		if (err)
			return err;
	} else {
		/* read result */
		msleep(ACMP_READ_DELAY_MS);
		err = h2omg_slg_v3_acmp_status_get(info, &sts0);
		if (err)
			return err;
	}

	err = h2omg_slg_v3_control_set(info,
		u32_replace_bits(ctrl, CONTROL_ACMP_OFF, FLD_CONTROL_ACMP));

	/* put things back */
	msleep(RESET_DELAY_MS);
	err = regmap_write(info->regmap, REG_RESET, RESET_VALUE);
	if (err)
		return err;
	msleep(RESET_DELAY_MS);
	err = h2omg_slg_v3_control_set(info, ctrl);
	if (err)
		return err;
	err = h2omg_slg_v3_update_timing_regs(info);
	if (err)
		return err;
	/* read out results */
	val = FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_ACMP_STATUS_SNS0, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_ACMP_STATUS_SNS1, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_ACMP_STATUS_REF0, sts0));

	*acmp = (val >> (__bf_shf(FLD_VSTATUS_INTS0) + id)) & 0x01;

	return 0;
}

static bool h2omg_slg_v3_id_get_enabled(enum h2omg_sensor_id id, u8 ctrl)
{
	unsigned int bit;
	int err;

	err = h2omg_slg_v3_sensor_enable_id2bit(id, &bit);
	if (err)
		return false;

	return !!(ctrl & 1 << bit);
}

static enum h2omg_detect_mode h2omg_slg_v3_id_get_detect_mode(enum h2omg_sensor_id id,
							      u8 ctrl, u8 sts)
{
	u8 sensor0_enabled = FIELD_GET(FLD_CONTROL_SENSOR0_EN, ctrl);
	u8 sensor1_enabled = FIELD_GET(FLD_CONTROL_SENSOR1_EN, ctrl);
	u8 reference_enabled = FIELD_GET(FLD_CONTROL_REFERENCE_EN, ctrl);
	u8 sensor0_mode = FIELD_GET(FLD_CONTROL_SENSOR0_MODE, ctrl);
	u8 sensor1_mode = FIELD_GET(FLD_CONTROL_SENSOR1_MODE, ctrl);

	if (id == SENSOR_REFERENCE) {
		if (((sensor0_mode == CONTROL_MODE_DRY_DETECT) ||
		     (sensor1_mode == CONTROL_MODE_DRY_DETECT)) &&
		    ((reference_enabled == CONTROL_SENSOR_ON) &&
		     (sensor0_enabled == CONTROL_SENSOR_OFF) &&
		     (sensor1_enabled == CONTROL_SENSOR_OFF))) {
			return DETECT_DRY;
		}

		return DETECT_WET;
	}

	if (id == SENSOR0)
		return (sensor0_mode == CONTROL_MODE_WET_DETECT) ? DETECT_WET : DETECT_DRY;

	return (sensor1_mode == CONTROL_MODE_WET_DETECT) ? DETECT_WET : DETECT_DRY;
}

static enum h2omg_sensor_state h2omg_slg_v3_id_get_state(enum h2omg_sensor_id id,
							 u8 ctrl, u8 sts)
{
	enum h2omg_detect_mode mode = h2omg_slg_v3_id_get_detect_mode(id, ctrl, sts);
	enum h2omg_detect_mode ref_mode = h2omg_slg_v3_id_get_detect_mode(SENSOR_REFERENCE,
									  ctrl, sts);
	bool enabled = h2omg_slg_v3_id_get_enabled(id, ctrl);
	bool ref_enabled = h2omg_slg_v3_id_get_enabled(SENSOR_REFERENCE, ctrl);
	enum h2omg_sensor_state state;
	unsigned int sensor_val = (sts >> (__bf_shf(FLD_VSTATUS_INTS0) + id)) & 0x1;

	if (!enabled)
		state = SENSOR_DIS;
	else if (!ref_enabled)
		state = SENSOR_INV;
	else if ((id != SENSOR_REFERENCE) && (ref_mode == DETECT_DRY))
		state = SENSOR_INV;
	else if (mode == DETECT_WET)
		state = sensor_val ? SENSOR_WET : SENSOR_DRY;
	else
		state = sensor_val ? SENSOR_DRY : SENSOR_WET;

	return state;
}

static int h2omg_slg_v3_state_get(struct h2omg_info *info, struct h2omg_state *state)
{
	unsigned int ctrl;
	unsigned int sts;
	unsigned int sensor_id;
	int err;

	err = h2omg_slg_v3_control_get(info, &ctrl);
	if (err)
		return err;

	err = h2omg_slg_v3_status_get(info, &sts);
	if (err)
		return err;

	state->sensors[SENSOR_REFERENCE] = h2omg_slg_v3_id_get_state(SENSOR_REFERENCE, ctrl, sts);

	for (sensor_id = 0; sensor_id < NUM_SENSORS; sensor_id++)
		state->sensors[sensor_id] = h2omg_slg_v3_id_get_state(sensor_id, ctrl, sts);


	state->fuse = FIELD_GET(FLD_VSTATUS_FUSE, sts) ? FUSE_OPEN : FUSE_SHORT;

	return 0;
}

/* external */
static void h2omg_slg_v3_timeout_handler(struct h2omg_info *info)
{
	struct device *dev = info->dev;
	unsigned int control_save;
	unsigned int state_vreg;
	unsigned int reference_enable;
	unsigned int reference_state;
	unsigned int sensor_enable[2];
	unsigned int sensor_mode[2];
	unsigned int sensor_state[2];
	unsigned int wet_sensor = (info->timer_id == TIMER_ID_SENSOR_0) ? 0 : 1;
	int err;

	dev_warn(dev, "timer expired for sensor %d\n", wet_sensor);
	mutex_lock(&info->lock);
	err = h2omg_slg_v3_control_get(info, &control_save);
	if (err) {
		dev_err(dev, "control read failed (%d)\n", err);
		mutex_unlock(&info->lock);
		return;
	}

	err = h2omg_slg_v3_status_only_get(info, &state_vreg);
	if (err) {
		dev_err(dev, "IRQ: status read failed (%d)\n", err);
		mutex_unlock(&info->lock);
		return;
	}

	reference_enable = FIELD_GET(FLD_CONTROL_REFERENCE_EN, control_save);
	reference_state = FIELD_GET(FLD_VSTATUS_REFS0, state_vreg);

	sensor_enable[0] = FIELD_GET(FLD_CONTROL_SENSOR0_EN, control_save);
	sensor_enable[1] = FIELD_GET(FLD_CONTROL_SENSOR1_EN, control_save);

	sensor_state[0] = FIELD_GET(FLD_VSTATUS_INTS0, state_vreg);
	sensor_state[1] = FIELD_GET(FLD_VSTATUS_INTS1, state_vreg);

	sensor_mode[0] = FIELD_GET(FLD_CONTROL_SENSOR0_MODE, control_save);
	sensor_mode[1] = FIELD_GET(FLD_CONTROL_SENSOR1_MODE, control_save);

	if (reference_state) {
		/* reference state is active (either getting wet or drying out */
		h2omg_timer_stop(info);
	} else if (h2omg_sensor_is_wet(sensor_state[wet_sensor], sensor_mode[wet_sensor])) {
		/* sensor still wet blow the fuse */
		mdelay(20);
		h2omg_slg_v3_control_set(info, CONTROL_DISABLED);
		if (wet_sensor == 0) {
			dev_warn(dev, "sensor0 timed-out and is still wet\n");
			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_WET_DETECT,
							FLD_CONTROL_SENSOR0_MODE);
		} else {
			dev_warn(dev, "sensor1 timed-out and is still wet\n");
			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_WET_DETECT,
							FLD_CONTROL_SENSOR1_MODE);
		}
		h2omg_slg_v3_set_trigger_state(info, TRIGGER_NOW);
		mdelay(20);
		h2omg_slg_v3_control_set(info, control_save);
	}

	mutex_unlock(&info->lock);
}

/* external */
static irqreturn_t h2omg_slg_v3_irq_handler(int irq, void *data)
{
	struct h2omg_info *info = data;
	struct device *dev = info->dev;
	unsigned int control_save;
	unsigned int state_vreg;
	unsigned int reference_enable;
	unsigned int sensor0_enable;
	unsigned int sensor1_enable;
	unsigned int reference_state;
	unsigned int sensor0_state;
	unsigned int sensor1_state;
	unsigned int sensor0_mode;
	unsigned int sensor1_mode;
	int err;

	mutex_lock(&info->lock);
	err = h2omg_slg_v3_control_get(info, &control_save);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_slg_v3_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	err = h2omg_slg_v3_state_get(info, &info->latched_state);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_slg_v3_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	err = h2omg_slg_v3_status_get(info, &state_vreg);
	if (err) {
		dev_err(dev, "IRQ: status read failed (%d)\n", err);
		h2omg_slg_v3_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	h2omg_slg_v3_control_set(info, CONTROL_DISABLED);
	mdelay(20);

	reference_enable = FIELD_GET(FLD_CONTROL_REFERENCE_EN, control_save);
	sensor0_enable = FIELD_GET(FLD_CONTROL_SENSOR0_EN, control_save);
	sensor1_enable = FIELD_GET(FLD_CONTROL_SENSOR1_EN, control_save);

	reference_state = FIELD_GET(FLD_VSTATUS_REFS0, state_vreg);
	sensor0_state = FIELD_GET(FLD_VSTATUS_INTS0, state_vreg);
	sensor1_state = FIELD_GET(FLD_VSTATUS_INTS1, state_vreg);

	sensor0_mode = FIELD_GET(FLD_CONTROL_SENSOR0_MODE, control_save);
	sensor1_mode = FIELD_GET(FLD_CONTROL_SENSOR1_MODE, control_save);

	if (reference_state) {
		if ((!sensor0_enable && !sensor1_enable && reference_enable) &&
		    (sensor0_mode == CONTROL_MODE_DRY_DETECT ||
		     sensor1_mode == CONTROL_MODE_DRY_DETECT)) {
			/* reference just dried out */
			/* go back to original state and start over */
			dev_info(dev, "sensor reference dried out -> initial state\n");
			control_save = info->control_set;
			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_WET_DETECT,
							FLD_CONTROL_SENSOR0_MODE);
			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_WET_DETECT,
							FLD_CONTROL_SENSOR1_MODE);
		} else {
			/* reference just got wet
			 *
			 * go to dry detect mode by setting sensor0 to dry detect and
			 * disabling everything but the reference
			 */
			control_save = info->control_set;

			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_DRY_DETECT,
							FLD_CONTROL_SENSOR0_MODE);

			control_save = u32_replace_bits(control_save,
							CONTROL_SENSOR_ON,
							FLD_CONTROL_REFERENCE_EN);

			control_save = u32_replace_bits(control_save,
							CONTROL_SENSOR_OFF,
							FLD_CONTROL_SENSOR0_EN);

			control_save = u32_replace_bits(control_save,
							CONTROL_SENSOR_OFF,
							FLD_CONTROL_SENSOR1_EN);
			dev_info(dev, "Reference sensor got wet -> reference dry detect\n");
		}

		if (info->dual_trigger) {
			h2omg_slg_v3_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
			h2omg_timer_stop(info);
		}
	} else {
		bool wet[2] = { FALSE, FALSE };

		if (sensor0_state) {
			if (sensor0_mode == CONTROL_MODE_WET_DETECT) {
				/* sensor got wet now put sensor into dry detect */
				wet[0] = TRUE;
				control_save = u32_replace_bits(control_save,
								CONTROL_MODE_DRY_DETECT,
								FLD_CONTROL_SENSOR0_MODE);
				dev_info(dev, "sensor0 got wet -> sensor0 dry detect\n");
			} else {
				/* sensor dried out now put sensor into wet detect */
				wet[0] = FALSE;
				control_save = u32_replace_bits(control_save,
								CONTROL_MODE_WET_DETECT,
								FLD_CONTROL_SENSOR0_MODE);
				dev_info(dev, "sensor0 dried out -> sensor0 wet detect\n");
			}
		} else {
			if (sensor0_mode == CONTROL_MODE_WET_DETECT) {
				/* sensor0 is in wet detect and nothing changed (still wet) */
				wet[0] = FALSE;
			} else {
				/* sensor0 is in dry detect and nothing changed (still wet) */
				wet[0] = TRUE;
			}
		}

		if (sensor1_state) {
			if (sensor1_mode == CONTROL_MODE_WET_DETECT) {
				/* sensor got wet now put sensor into dry detect */
				wet[1] = TRUE;
				control_save = u32_replace_bits(control_save,
								CONTROL_MODE_DRY_DETECT,
								FLD_CONTROL_SENSOR1_MODE);
				dev_info(dev, "sensor1 got wet -> sensor1 dry detect\n");
			} else {
				/* sensor dried out now put sensor into wet detect */
				wet[1] = FALSE;
				control_save = u32_replace_bits(control_save,
								CONTROL_MODE_WET_DETECT,
								FLD_CONTROL_SENSOR1_MODE);
				dev_info(dev, "sensor1 dried out -> sensor1 wet detect\n");
			}
		} else {
			if (sensor1_mode == CONTROL_MODE_WET_DETECT) {
				/* sensor1 is in wet detect and nothing changed (still wet) */
				wet[1] = FALSE;
			} else {
				/* sensor1 is in dry detect and nothing changed (still wet) */
				wet[1] = TRUE;
			}

		}

		if (info->dual_trigger) {
			const bool fuse_blown = info->latched_state.fuse;

			if (fuse_blown) {
				dev_warn(dev, "fuse already blown\n");
				h2omg_slg_v3_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
			} else if (wet[0] && wet[1]) {
				dev_warn(dev, "both sensors are wet\n");
				h2omg_slg_v3_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
			} else if (wet[0] && !wet[1]) {
				dev_warn(dev, "sensor0 is wet\n");
				h2omg_slg_v3_set_trigger_state(info, TRIGGER_WHEN_S1_GETS_WET);
				h2omg_timer_start(info, TIMER_ID_SENSOR_0);
			} else if (!wet[0] && wet[1]) {
				dev_warn(dev, "sensor1 is wet\n");
				h2omg_slg_v3_set_trigger_state(info, TRIGGER_WHEN_S0_GETS_WET);
				h2omg_timer_start(info, TIMER_ID_SENSOR_1);
			} else {
				dev_info(dev, "both sensors are dry\n");
				h2omg_slg_v3_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
				h2omg_timer_stop(info);
			}
		}
	}
	h2omg_slg_v3_control_set(info, control_save);
	mutex_unlock(&info->lock);
	kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	return IRQ_HANDLED;
}

/* external */
static int h2omg_slg_v3_cleanup(struct h2omg_info *info)
{
	return 0;
}

/* external */
static size_t h2omg_slg_v3_sensor_count_get(struct h2omg_info *info)
{
	return NUM_SENSORS;
}

static const struct regmap_range h2omg_read_range[] = {
	regmap_reg_range(REG_CONTROL, REG_CONTROL),
	regmap_reg_range(REG_STATUS0, REG_STATUS1),
	regmap_reg_range(REG_FUSE_STATUS, REG_FUSE_STATUS),
	regmap_reg_range(REG_ACMP_STATUS, REG_ACMP_STATUS),
	regmap_reg_range(REG_ACMP_NONINV, REG_ACMP_NONINV),
	regmap_reg_range(REG_RESET, REG_RESET),
	regmap_reg_range(REG_ACMP0H_VREF0, REG_ACMP1H_VREF0),
	regmap_reg_range(REG_LUT3_10_IN2_11_IN0, REG_LUT3_11_IN1_IN2),
	regmap_reg_range(REG_DFF8_FUSE_TRGR_IN, REG_DFF9_FUSE_TRGR_MSK),
	regmap_reg_range(REG_LUT3_FUSE_TRGR_OUT, REG_LUT3_FUSE_TRGR_OUT),
	regmap_reg_range(REG_GPIO4_5, REG_GPIO5_6),
	regmap_reg_range(REG_CNT5, REG_CNT5),
	regmap_reg_range(REG_CNT1_D, REG_CNT1_D),
	regmap_reg_range(REG_CNT2_D, REG_CNT2_D),
	regmap_reg_range(REG_CNT4_D, REG_CNT4_D),
	regmap_reg_range(REG_CNT5_D, REG_CNT5_D),
	regmap_reg_range(REG_CNT6_D, REG_CNT6_D),
	regmap_reg_range(REG_ACMP2L_VREF1, REG_ACMP2L_VREF1),
};

static const struct regmap_access_table h2omg_read_table = {
	.yes_ranges = h2omg_read_range,
	.n_yes_ranges = ARRAY_SIZE(h2omg_read_range),
};

static const struct regmap_range h2omg_write_range[] = {
	regmap_reg_range(REG_CONTROL, REG_CONTROL),
	regmap_reg_range(REG_ACMP_NONINV, REG_ACMP_NONINV),
	regmap_reg_range(REG_RESET, REG_RESET),
	regmap_reg_range(REG_ACMP0H_VREF0, REG_ACMP1H_VREF0),
	regmap_reg_range(REG_DFF8_FUSE_TRGR_IN, REG_DFF9_FUSE_TRGR_MSK),
	regmap_reg_range(REG_LUT3_10_IN2_11_IN0, REG_LUT3_11_IN1_IN2),
	regmap_reg_range(REG_LUT3_FUSE_TRGR_OUT, REG_LUT3_FUSE_TRGR_OUT),
	regmap_reg_range(REG_GPIO4_5, REG_GPIO5_6),
	regmap_reg_range(REG_CNT5, REG_CNT5),
	regmap_reg_range(REG_CNT1_D, REG_CNT1_D),
	regmap_reg_range(REG_CNT2_D, REG_CNT2_D),
	regmap_reg_range(REG_CNT4_D, REG_CNT4_D),
	regmap_reg_range(REG_CNT5_D, REG_CNT5_D),
	regmap_reg_range(REG_CNT6_D, REG_CNT6_D),
	regmap_reg_range(REG_ACMP2L_VREF1, REG_ACMP2L_VREF1),
};

static const struct regmap_access_table h2omg_write_table = {
	.yes_ranges = h2omg_write_range,
	.n_yes_ranges = ARRAY_SIZE(h2omg_write_range),
};

static const struct regmap_config h2omg_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_RESET,
	.wr_table = &h2omg_write_table,
	.rd_table = &h2omg_read_table,
};

static const struct h2omg_ops h2omg_slg_v3_ops = {
	.control_get = h2omg_slg_v3_control_get,
	.control_set = h2omg_slg_v3_control_set,

	.status_get = h2omg_slg_v3_status_get,

	.fuse_enable_get = h2omg_slg_v3_fuse_enable_get,
	.fuse_enable_set = h2omg_slg_v3_fuse_enable_set,
	.fuse_state_get = h2omg_slg_v3_fuse_state_get,

	.sensor_count_get = h2omg_slg_v3_sensor_count_get,
	.sensor_enable_get = h2omg_slg_v3_sensor_enable_get,
	.sensor_enable_set = h2omg_slg_v3_sensor_enable_set,
	.sensor_mode_get = h2omg_slg_v3_sensor_mode_get,
	.sensor_mode_set = h2omg_slg_v3_sensor_mode_set,
	.sensor_acmp_get = h2omg_slg_v3_sensor_acmp_get,

	.irq_handler = h2omg_slg_v3_irq_handler,
	.cleanup = h2omg_slg_v3_cleanup,
	.timeout_handler = h2omg_slg_v3_timeout_handler,
};

int h2omg_slg_v3_init(struct i2c_client *client)
{
	struct h2omg_info *info = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	unsigned int control_current;
	int err;

	dev_info(dev, "revision slg_v3\n");
	client->dev.init_name = "i2c-h2omg";

	info->regmap = devm_regmap_init_i2c(client, &h2omg_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Regmap init failed");
		return PTR_ERR(info->regmap);
	}
	info->num_sensors = NUM_SENSORS;
	info->ops = &h2omg_slg_v3_ops;

	err = of_property_read_u8(dev->of_node, "control", &info->control_set);
	if (err) {
		dev_warn(dev, "No initial control value using %#02x %d\n", CONTROL_DEFAULT, err);
		info->control_set = CONTROL_DEFAULT;
	}

	err = h2omg_slg_v3_state_get(info, &info->boot_state);
	if (err)
		return -ENODEV;

	/*
	 * update the config based on the device tree setting.  The fuse enable is controlled
	 * by userspace so ignore that field.
	 */
	err = h2omg_slg_v3_control_get(info, &control_current);
	if (err)
		return err;

	/* put the device in single or dual trigger mode */
	err = h2omg_slg_v3_control_set(info, CONTROL_DISABLED);
	if (err)
		return err;

	err = h2omg_slg_v3_update_timing_regs(info);
	if (err)
		return err;

	info->dual_trigger = of_property_read_bool(dev->of_node, "dual-trigger");
	if (info->dual_trigger)
		err = h2omg_slg_v3_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
	else
		err = h2omg_slg_v3_set_trigger_state(info, SINGLE_TRIGGER_INITIAL);
	if (err)
		return err;

	/* put control settings back to original if all but the fuse
	 * field matches the default setting
	 */
	if ((control_current | FLD_CONTROL_ARM_FUSE) == (info->control_set | FLD_CONTROL_ARM_FUSE))
		err = h2omg_slg_v3_control_set(info, control_current);
	else
		err = h2omg_slg_v3_control_set(info, info->control_set);
	if (err)
		return err;

	return h2omg_slg_v3_state_get(info, &info->latched_state);
}
