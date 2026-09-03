// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include "h2omg.h"

#define REG_RESET      0x1400
#define RESET_VALUE    0x01
#define RESET_DELAY_MS 10

#define REG_CONTROL 0x8002
#define FLD_CONTROL_REFERENCE_EN     BIT(0)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR0_EN       BIT(1)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR1_EN       BIT(2)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_ARM_FUSE         BIT(3)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR0_MODE     BIT(4)  /* 0: Wet Detect 1: Dry Detect */
#define FLD_CONTROL_SENSOR1_MODE     BIT(5)  /* 0: Wet Detect 1: Dry Detect */
#define FLD_CONTROL_ACMP             BIT(6)  /* 0: Enable  1: Disable */
#define FLD_CONTROL_FAULT_ENABLE     BIT(7)  /* 0: Disable 1: Enable  */

#define FUSE_READ_DELAY_MIN_US    1000
#define FUSE_READ_DELAY_MAX_US    1500

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

#define REG_STATUS0 0x2400
#define FLD_STATUS_INTRS     BIT(7)

#define REG_STATUS1 0x2401
#define FLD_STATUS_INTS0     BIT(0)
#define FLD_STATUS_INTS1     BIT(1)

#define REG_FUSE_STATUS 0x2406
#define FLD_FUSE_STATUS BIT(5)

/* VIRTUAL STATUS REGISTER */
#define FLD_VSTATUS_INTS0 BIT(0)
#define FLD_VSTATUS_INTS1 BIT(1)
#define FLD_VSTATUS_INTS2 BIT(2)
#define FLD_VSTATUS_REFS0 BIT(3)
#define FLD_VSTATUS_FUSE  BIT(4)
#define FLD_VSTATUS_EXTS0 BIT(5)
#define FLD_VSTATUS_SNSRS GENMASK(2, 0)
#define FLD_VSTATUS_SNSRS_AND_REF GENMASK(3, 0)

#define REG_ACMP_STATUS 0x2404
#define FLD_ACMP_STATUS_SNS0 BIT(0)
#define FLD_ACMP_STATUS_SNS1 BIT(1)
#define FLD_ACMP_STATUS_REF0 BIT(2)

#define REG_ACMP0_CONF 0x9200
#define REG_ACMP1_CONF 0x9300
#define FLD_ACMP_NONINV_IP GENMASK(1, 0)
#define FLD_ACMP_GAIN   GENMASK(3, 2)

#define REG_ACMP2_VREFH 0x9402
#define ACMP2_VREFH_DIV4 0x1a

#define REG_MF16_CTRL_DATA1 0x6102
#define REG_CMO34 0x2022
#define REG_CMO96 0x2060

#define CMO96_SINGLE   0x28
#define CMO96_DUAL     0x2a
#define MF16_CTRL_DATA1_EITHER 0x0e
#define MF16_CTRL_DATA1_BOTH 0x08
#define MF16_CTRL_DATA1_SENS0 0x04
#define MF16_CTRL_DATA1_SENS1 0x02
#define CMO34_EITHER   0x04
#define CMO34_SENS0    0x26
#define CMO34_SENS1    0x27

#define REG_MF17_CTRL_DATA 0x6202
#define MF17_CTRL_DATA_20MS 0x1e  /* reference debounce = 20 ms */

#define REG_MF18_CTRL_DATA 0x6302
#define MF18_CTRL_DATA_25MS 0x26  /* sensor0 debounce = 25 ms */

#define REG_MF19_CTRL_DATA 0x6402
#define MF19_CTRL_DATA_25MS 0x26  /* sensor1 debounce = 25 ms */

#define REG_MF20_CONF1     0x6501
#define MF20_CONF1_65536   0x99  /* sensor off time x 64; osc0 / 65536 */

#define REG_MF20_CTRL_DATA 0x6502
#define MF20_CTRL_DATA_89S 0x21  /* sensor off time 89s */

#define REG_MF22_CTRL_DATA 0x6702
#define MF22_CTRL_DATA_30MS 0x2d  /* sensor on time = 30 ms */

/*
 * The chip doesn't have a true "revision" register but the value at
 * 0x9000 can be used to tell the difference between the original version
 * used in develoment (returning 0x01) and the final configuration used
 * in the shipping version (returning 0x00).
 */
#define REG_OSC0_CONF 0x9000
#define OSC0_CONF_DEV 0x01
#define OSC0_CONF_REL 0x00

#define REVISION_DEV 0
#define REVISION_REL 1

#define NUM_SENSORS 2

static int h2omg_atl_get_revision(struct h2omg_info *info, unsigned int *revision)
{
	struct regmap *regmap = info->regmap;
	unsigned int val;
	int err;

	err = regmap_read(regmap, REG_OSC0_CONF, &val);
	if (err) {
		dev_err(info->dev, "unable to read revision (%d)\n", err);
		return err;
	}
	*revision = (val == OSC0_CONF_REL) ? REVISION_REL : REVISION_DEV;
	return 0;
}

/* slow down chip sampling rate and on time */
static int h2omg_atl_v1_update_timing_regs(struct h2omg_info *info)
{
	static const struct h2omg_reg_val timings[] = {
		{ REG_MF17_CTRL_DATA, MF17_CTRL_DATA_20MS },
		{ REG_MF18_CTRL_DATA, MF18_CTRL_DATA_25MS },
		{ REG_MF19_CTRL_DATA, MF19_CTRL_DATA_25MS },
		{ REG_MF22_CTRL_DATA, MF22_CTRL_DATA_30MS },
		{ REG_MF20_CTRL_DATA, MF20_CTRL_DATA_89S },
		{ REG_MF20_CONF1, MF20_CONF1_65536 },
		{ REG_ACMP2_VREFH, ACMP2_VREFH_DIV4 },
	};
	size_t update_rows = (info->vref_div) ? ARRAY_SIZE(timings) : ARRAY_SIZE(timings) - 1;

	/* only update shipping version */
	if (info->revision < REVISION_REL)
		return 0;

	return h2omg_update_regs(info, timings, update_rows);
}

/* configure chip to trigger either when either sensor is wet (SINGLE_TRIGGER)
 * or only when both sensors get wet at the same time (to the resolution of the chip)
 */
static int h2omg_atl_v1_set_trigger_state(struct h2omg_info *info, enum h2omg_trigger_state state)
{
	struct regmap *regmap = info->regmap;
	int err;

	switch (state) {
	case SINGLE_TRIGGER_INITIAL:
		dev_dbg(info->dev, "SINGLE_TRIGGER_INITIAL\n");
		err = regmap_write(regmap, REG_MF16_CTRL_DATA1, MF16_CTRL_DATA1_EITHER);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO34, CMO34_EITHER);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO96, CMO96_SINGLE);
		if (err)
			return err;
	break;
	case DUAL_TRIGGER_INITIAL:
		dev_dbg(info->dev, "DUAL_TRIGGER_INITIAL\n");
		err = regmap_write(regmap, REG_MF16_CTRL_DATA1, MF16_CTRL_DATA1_BOTH);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO34, CMO34_EITHER);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO96, CMO96_DUAL);
		if (err)
			return err;
	break;
	case TRIGGER_WHEN_S0_GETS_WET: /* sensor 1 is already wet */
		dev_dbg(info->dev, "TRIGGER_WHEN_S0_GETS_WET\n");
		err = regmap_write(regmap, REG_MF16_CTRL_DATA1, MF16_CTRL_DATA1_SENS0);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO34, CMO34_SENS0);
		if (err)
			return err;
	break;
	case TRIGGER_WHEN_S1_GETS_WET: /* sensor 0 is already wet */
		dev_dbg(info->dev, "TRIGGER_WHEN_S1_GETS_WET\n");
		err = regmap_write(regmap, REG_MF16_CTRL_DATA1, MF16_CTRL_DATA1_SENS1);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO34, CMO34_SENS1);
		if (err)
			return err;
	break;
	case TRIGGER_NOW:
		dev_dbg(info->dev, "TRIGGER_NOW\n");
		err = regmap_write(regmap, REG_CMO96, CMO96_DUAL);
		if (err)
			return err;
		err = regmap_write(regmap, REG_MF16_CTRL_DATA1, MF16_CTRL_DATA1_EITHER);
		if (err)
			return err;
		err = regmap_write(regmap, REG_CMO34, CMO34_EITHER);
		if (err)
			return err;
	break;
	default:
		return -EINVAL;
	};

	return 0;
}

static int h2omg_atl_v1_sensor_valid(enum h2omg_sensor_id id)
{
	return (id == SENSOR0 || id == SENSOR1 || id == SENSOR_REFERENCE);
}

static int h2omg_atl_v1_sensor_enable_id2bit(enum h2omg_sensor_id id, unsigned int *bit)
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
static int h2omg_atl_v1_control_get(struct h2omg_info *info, unsigned int *val)
{
	return regmap_read(info->regmap, REG_CONTROL, val);
}

/* external */
static int h2omg_atl_v1_control_set(struct h2omg_info *info, unsigned int val)
{
	return regmap_write(info->regmap, REG_CONTROL, val);
}

/* external */
static int h2omg_atl_v1_fuse_enable_get(struct h2omg_info *info, bool *enable)
{
	unsigned int val;
	int err;

	err = h2omg_atl_v1_control_get(info, &val);
	if (err)
		return err;

	*enable = FIELD_GET(FLD_CONTROL_ARM_FUSE, val);
	return 0;
}

/* external */
static int h2omg_atl_v1_fuse_enable_set(struct h2omg_info *info, bool enable)
{
	unsigned int ctrl;
	int err;

	err = h2omg_atl_v1_control_get(info, &ctrl);
	if (err)
		return err;
	info->control_set = u8_replace_bits(ctrl, enable, FLD_CONTROL_ARM_FUSE);
	return h2omg_atl_v1_control_set(info, info->control_set);
}

/* external */
static int h2omg_atl_v1_fuse_state_get(struct h2omg_info *info, enum h2omg_fuse_state *state)
{
	unsigned int fuse;
	unsigned int ctrl;
	int err;

	err = h2omg_atl_v1_control_get(info, &ctrl);
	if (err)
		return err;

	err = h2omg_atl_v1_control_set(info,
		u32_replace_bits(ctrl, CONTROL_ACMP_ON, FLD_CONTROL_ACMP));
	if (err)
		return err;

	usleep_range(FUSE_READ_DELAY_MIN_US, FUSE_READ_DELAY_MAX_US);
	err = regmap_read(info->regmap, REG_FUSE_STATUS, &fuse);
	if (err)
		return err;

	err = h2omg_atl_v1_control_set(info, ctrl);
	if (err)
		return err;

	/* invert fuse value */
	*state = !FIELD_GET(FLD_FUSE_STATUS, fuse);
	return 0;
}

static int h2omg_atl_v1_status_only_get(struct h2omg_info *info, unsigned int *vreg)
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
static int h2omg_atl_v1_status_get(struct h2omg_info *info, unsigned int *vreg)
{
	enum h2omg_fuse_state fuse;
	u8 sts[2];

	int err;

	err = regmap_raw_read(info->regmap, REG_STATUS0, sts, sizeof(sts));
	if (err)
		return err;

	err = h2omg_atl_v1_fuse_state_get(info, &fuse);
	if (err)
		return err;

	*vreg = FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_STATUS_INTRS, sts[0])) |
		FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_STATUS_INTS0, sts[1])) |
		FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_STATUS_INTS1, sts[1])) |
		FIELD_PREP(FLD_VSTATUS_FUSE,  fuse);

	return 0;
}

/* external */
static int h2omg_atl_v1_sensor_enable_get(struct h2omg_info *info,
					  enum h2omg_sensor_id id,
					  bool *enable)
{
	unsigned int val;
	unsigned int bit;
	int err;

	err = h2omg_atl_v1_sensor_enable_id2bit(id, &bit);
	if (err)
		return err;

	err = h2omg_atl_v1_control_get(info, &val);
	if (err)
		return err;

	*enable = !!(val & 1 << bit);
	return 0;
}

/* external */
static int h2omg_atl_v1_sensor_enable_set(struct h2omg_info *info,
					  enum h2omg_sensor_id id,
					  bool enable)
{
	unsigned int ctrl;
	unsigned int mask;
	unsigned int bit;
	int err;

	err = h2omg_atl_v1_sensor_enable_id2bit(id, &bit);
	if (err)
		return err;

	mask = 1 << bit;

	err = h2omg_atl_v1_control_get(info, &ctrl);
	if (err)
		return err;
	return h2omg_atl_v1_control_set(info, h2omg_reg_mod(mask, enable, ctrl));
}

/* external */
static int h2omg_atl_v1_sensor_mode_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode *mode)
{
	unsigned int vreg;
	int err;

	if (!h2omg_atl_v1_sensor_valid(id))
		return -EINVAL;

	err = h2omg_atl_v1_control_get(info, &vreg);
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
static int h2omg_atl_v1_sensor_mode_set(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode mode)
{
	if (!h2omg_atl_v1_sensor_valid(id))
		return -EINVAL;

	return -EOPNOTSUPP;
}

/* external */
static int h2omg_atl_v1_sensor_acmp_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					unsigned int *acmp)
{
	unsigned int val, ctrl;
	unsigned int sts0;
	int err;

	/* get initial value to restore at the end */
	err = h2omg_atl_v1_control_get(info, &ctrl);
	if (err)
		return err;

	err = h2omg_atl_v1_control_set(info,  ctrl & 0x1);
	if (err)
		return err;

	/* If the reference sensor is disabled, the change ACMP0's acmp0 input to VDD */
	if (!FIELD_GET(FLD_CONTROL_REFERENCE_EN, ctrl)) {
		unsigned int ref_sts0;
		unsigned int acmp0;
		unsigned int acmp1;
		unsigned int acmp_tmp;

		/* read  reference before we change the ACMP_NONINV */
		err = regmap_read(info->regmap, REG_ACMP_STATUS, &ref_sts0);
		if (err)
			return err;

		err = regmap_read(info->regmap, REG_ACMP0_CONF, &acmp0);
		if (err)
			return err;

		err = regmap_read(info->regmap, REG_ACMP1_CONF, &acmp1);
		if (err)
			return err;

		acmp_tmp = u32_replace_bits(acmp0, 0x03, FLD_ACMP_NONINV_IP);
		acmp_tmp = u32_replace_bits(acmp_tmp, 0x01, FLD_ACMP_GAIN);
		err = regmap_write(info->regmap, REG_ACMP0_CONF, acmp_tmp);
		if (err)
			return err;

		acmp_tmp = u32_replace_bits(acmp1, 0x01, FLD_ACMP_GAIN);
		err = regmap_write(info->regmap, REG_ACMP1_CONF, acmp_tmp);
		if (err)
			return err;

		msleep(ACMP_READ_DELAY_MS);

		/* read for sensor0 and sensor1 */
		err = regmap_read(info->regmap, REG_ACMP_STATUS, &sts0);
		if (err)
			return err;

		/* put the reference value back */
		sts0 = u32_replace_bits(sts0,
					FIELD_GET(FLD_ACMP_STATUS_REF0, ref_sts0),
					FLD_ACMP_STATUS_REF0);

		/* put register things back */
		err = regmap_write(info->regmap, REG_ACMP0_CONF, acmp0);
		if (err)
			return err;

		/* put register things back */
		err = regmap_write(info->regmap, REG_ACMP1_CONF, acmp1);
		if (err)
			return err;
	} else {
		/* read result */
		msleep(ACMP_READ_DELAY_MS);
		err = regmap_read(info->regmap, REG_ACMP_STATUS, &sts0);
		if (err)
			return err;
	}

	err = h2omg_atl_v1_control_set(info,
		u32_replace_bits(ctrl, CONTROL_ACMP_OFF, FLD_CONTROL_ACMP));

	/* put things back */
	err = regmap_write(info->regmap, REG_RESET, RESET_VALUE);
	if (err)
		return err;
	msleep(RESET_DELAY_MS);
	err = h2omg_atl_v1_control_set(info, ctrl);
	if (err)
		return err;
	err = h2omg_atl_v1_update_timing_regs(info);
	if (err)
		return err;

	/* read out results */
	val = FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_ACMP_STATUS_SNS0, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_ACMP_STATUS_SNS1, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_ACMP_STATUS_REF0, sts0));

	*acmp = (val >> (__bf_shf(FLD_VSTATUS_INTS0) + id)) & 0x01;

	return 0;
}

static bool h2omg_atl_v1_id_get_enabled(enum h2omg_sensor_id id, u8 ctrl)
{
	unsigned int bit;
	int err;

	err = h2omg_atl_v1_sensor_enable_id2bit(id, &bit);
	if (err)
		return false;

	return !!(ctrl & 1 << bit);
}

static enum h2omg_detect_mode h2omg_atl_v1_id_get_detect_mode(enum h2omg_sensor_id id,
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

static enum h2omg_sensor_state h2omg_atl_v1_id_get_state(enum h2omg_sensor_id id,
							 u8 ctrl, u8 sts)
{
	enum h2omg_detect_mode mode = h2omg_atl_v1_id_get_detect_mode(id, ctrl, sts);
	enum h2omg_detect_mode ref_mode = h2omg_atl_v1_id_get_detect_mode(SENSOR_REFERENCE,
									  ctrl, sts);
	bool enabled = h2omg_atl_v1_id_get_enabled(id, ctrl);
	bool ref_enabled = h2omg_atl_v1_id_get_enabled(SENSOR_REFERENCE, ctrl);
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

static int h2omg_atl_v1_state_get(struct h2omg_info *info, struct h2omg_state *state)
{
	unsigned int ctrl;
	unsigned int sts;
	unsigned int sensor_id;
	int err;

	err = h2omg_atl_v1_control_get(info, &ctrl);
	if (err)
		return err;

	err = h2omg_atl_v1_status_get(info, &sts);
	if (err)
		return err;

	state->sensors[SENSOR_REFERENCE] = h2omg_atl_v1_id_get_state(SENSOR_REFERENCE, ctrl, sts);

	for (sensor_id = 0; sensor_id < NUM_SENSORS; sensor_id++)
		state->sensors[sensor_id] = h2omg_atl_v1_id_get_state(sensor_id, ctrl, sts);


	state->fuse = FIELD_GET(FLD_VSTATUS_FUSE, sts) ? FUSE_OPEN : FUSE_SHORT;

	return 0;
}

/* external */
static void h2omg_atl_v1_timeout_handler(struct h2omg_info *info)
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
	err = h2omg_atl_v1_control_get(info, &control_save);
	if (err) {
		dev_err(dev, "control read failed (%d)\n", err);
		mutex_unlock(&info->lock);
		return;
	}

	err = h2omg_atl_v1_status_only_get(info, &state_vreg);
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

		h2omg_atl_v1_control_set(info, CONTROL_DISABLED);
		mdelay(20);
		if (wet_sensor == 0) {
			dev_warn(dev, "sensor0 timed-out and is still wet\n");
			/* put sensor 0 in wet detect and trigger on sensor 0 wet */
			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_WET_DETECT,
							FLD_CONTROL_SENSOR0_MODE);

			h2omg_atl_v1_set_trigger_state(info, TRIGGER_NOW);
		} else {
			dev_warn(dev, "sensor1 timed-out and is still wet\n");
			/* put sensor 1 in wet detect and trigger on sensor 1 wet */
			control_save = u32_replace_bits(control_save,
							CONTROL_MODE_WET_DETECT,
							FLD_CONTROL_SENSOR1_MODE);
			h2omg_atl_v1_set_trigger_state(info, TRIGGER_NOW);
		}
		h2omg_atl_v1_control_set(info, control_save);
	}

	mutex_unlock(&info->lock);
}

/* external */
static irqreturn_t h2omg_atl_v1_irq_handler(int irq, void *data)
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
	unsigned int control_clr;
	int err;

	mutex_lock(&info->lock);
	err = h2omg_atl_v1_control_get(info, &control_save);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_atl_v1_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	err = h2omg_atl_v1_state_get(info, &info->latched_state);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_atl_v1_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	err = h2omg_atl_v1_status_get(info, &state_vreg);
	if (err) {
		dev_err(dev, "IRQ: status read failed (%d)\n", err);
		h2omg_atl_v1_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	h2omg_atl_v1_control_set(info, CONTROL_DISABLED);
	mdelay(20);

	reference_enable = FIELD_GET(FLD_CONTROL_REFERENCE_EN, control_save);
	sensor0_enable = FIELD_GET(FLD_CONTROL_SENSOR0_EN, control_save);
	sensor1_enable = FIELD_GET(FLD_CONTROL_SENSOR1_EN, control_save);

	reference_state = FIELD_GET(FLD_VSTATUS_REFS0, state_vreg);
	sensor0_state = FIELD_GET(FLD_VSTATUS_INTS0, state_vreg);
	sensor1_state = FIELD_GET(FLD_VSTATUS_INTS1, state_vreg);

	sensor0_mode = FIELD_GET(FLD_CONTROL_SENSOR0_MODE, control_save);
	sensor1_mode = FIELD_GET(FLD_CONTROL_SENSOR1_MODE, control_save);

	control_clr = control_save;
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
			h2omg_atl_v1_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
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
				/* sensor1 is in wet detect and nothing changed (still wet)*/
				wet[1] = FALSE;
			} else {
				/* sensor1 is in dry detect and nothing changed (still wet)*/
				wet[1] = TRUE;
			}

		}

		if (info->dual_trigger) {
			const bool fuse_blown = info->latched_state.fuse;

			if (fuse_blown) {
				dev_warn(dev, "fuse already blown\n");
				h2omg_atl_v1_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
			} else if (wet[0] && wet[1]) {
				dev_warn(dev, "both sensors are wet\n");
				h2omg_atl_v1_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
			} else if (wet[0] && !wet[1]) {
				dev_warn(dev, "sensor0 is wet\n");
				h2omg_atl_v1_set_trigger_state(info, TRIGGER_WHEN_S1_GETS_WET);
				h2omg_timer_start(info, TIMER_ID_SENSOR_0);
			} else if (!wet[0] && wet[1]) {
				dev_warn(dev, "sensor1 is wet\n");
				h2omg_atl_v1_set_trigger_state(info, TRIGGER_WHEN_S0_GETS_WET);
				h2omg_timer_start(info, TIMER_ID_SENSOR_1);
			} else {
				dev_info(dev, "both sensors are dry\n");
				h2omg_atl_v1_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
				h2omg_timer_stop(info);
			}
		}
	}
	h2omg_atl_v1_control_set(info, control_save);
	mutex_unlock(&info->lock);
	kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	return IRQ_HANDLED;
}

/* external */
static int h2omg_atl_v1_cleanup(struct h2omg_info *info)
{
	return 0;
}

/* external */
static size_t h2omg_atl_v1_sensor_count_get(struct h2omg_info *info)
{
	return NUM_SENSORS;
}

static const struct regmap_range h2omg_read_range[] = {
	regmap_reg_range(REG_RESET, REG_RESET),
	regmap_reg_range(REG_CONTROL, REG_CONTROL),
	regmap_reg_range(REG_STATUS0, REG_STATUS1),
	regmap_reg_range(REG_FUSE_STATUS, REG_FUSE_STATUS),
	regmap_reg_range(REG_ACMP_STATUS, REG_ACMP_STATUS),
	regmap_reg_range(REG_ACMP0_CONF, REG_ACMP1_CONF),
	regmap_reg_range(REG_MF16_CTRL_DATA1, REG_MF16_CTRL_DATA1),
	regmap_reg_range(REG_CMO34, REG_CMO34),
	regmap_reg_range(REG_CMO96, REG_CMO96),
	regmap_reg_range(REG_MF22_CTRL_DATA, REG_MF22_CTRL_DATA),
	regmap_reg_range(REG_MF20_CONF1, REG_MF20_CTRL_DATA),
	regmap_reg_range(REG_OSC0_CONF, REG_OSC0_CONF),
	regmap_reg_range(REG_MF17_CTRL_DATA, REG_MF17_CTRL_DATA),
	regmap_reg_range(REG_MF18_CTRL_DATA, REG_MF18_CTRL_DATA),
	regmap_reg_range(REG_MF19_CTRL_DATA, REG_MF19_CTRL_DATA),
	regmap_reg_range(REG_ACMP2_VREFH, REG_ACMP2_VREFH),

};

static const struct regmap_access_table h2omg_read_table = {
	.yes_ranges = h2omg_read_range,
	.n_yes_ranges = ARRAY_SIZE(h2omg_read_range),
};

static const struct regmap_range h2omg_write_range[] = {
	regmap_reg_range(REG_RESET, REG_RESET),
	regmap_reg_range(REG_CONTROL, REG_CONTROL),
	regmap_reg_range(REG_ACMP0_CONF, REG_ACMP1_CONF),
	regmap_reg_range(REG_MF16_CTRL_DATA1, REG_MF16_CTRL_DATA1),
	regmap_reg_range(REG_CMO34, REG_CMO34),
	regmap_reg_range(REG_CMO96, REG_CMO96),
	regmap_reg_range(REG_MF22_CTRL_DATA, REG_MF22_CTRL_DATA),
	regmap_reg_range(REG_MF20_CONF1, REG_MF20_CTRL_DATA),
	regmap_reg_range(REG_OSC0_CONF, REG_OSC0_CONF),
	regmap_reg_range(REG_MF17_CTRL_DATA, REG_MF17_CTRL_DATA),
	regmap_reg_range(REG_MF18_CTRL_DATA, REG_MF18_CTRL_DATA),
	regmap_reg_range(REG_MF19_CTRL_DATA, REG_MF19_CTRL_DATA),
	regmap_reg_range(REG_ACMP2_VREFH, REG_ACMP2_VREFH),
};

static const struct regmap_access_table h2omg_write_table = {
	.yes_ranges = h2omg_write_range,
	.n_yes_ranges = ARRAY_SIZE(h2omg_write_range),
};

static const struct regmap_config h2omg_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x9402,
	.wr_table = &h2omg_write_table,
	.rd_table = &h2omg_read_table,
};

static const struct h2omg_ops h2omg_atl_v1_ops = {
	.control_get = h2omg_atl_v1_control_get,
	.control_set = h2omg_atl_v1_control_set,

	.status_get = h2omg_atl_v1_status_get,

	.fuse_enable_get = h2omg_atl_v1_fuse_enable_get,
	.fuse_enable_set = h2omg_atl_v1_fuse_enable_set,
	.fuse_state_get = h2omg_atl_v1_fuse_state_get,

	.sensor_count_get = h2omg_atl_v1_sensor_count_get,
	.sensor_enable_get = h2omg_atl_v1_sensor_enable_get,
	.sensor_enable_set = h2omg_atl_v1_sensor_enable_set,
	.sensor_mode_get = h2omg_atl_v1_sensor_mode_get,
	.sensor_mode_set = h2omg_atl_v1_sensor_mode_set,
	.sensor_acmp_get = h2omg_atl_v1_sensor_acmp_get,

	.irq_handler = h2omg_atl_v1_irq_handler,
	.cleanup = h2omg_atl_v1_cleanup,
	.timeout_handler = h2omg_atl_v1_timeout_handler,
};

int h2omg_atl_v1_init(struct i2c_client *client)
{
	struct h2omg_info *info = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	unsigned int control_current;
	int err;

	dev_info(dev, "revision atl_v1\n");
	client->dev.init_name = "i2c-h2omg";

	info->regmap = devm_regmap_init_i2c(client, &h2omg_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Regmap init failed");
		return PTR_ERR(info->regmap);
	}
	info->num_sensors = NUM_SENSORS;
	info->ops = &h2omg_atl_v1_ops;

	err = of_property_read_u8(dev->of_node, "control", &info->control_set);
	if (err) {
		dev_warn(dev, "No initial control value using %#02x %d\n", CONTROL_DEFAULT, err);
		info->control_set = CONTROL_DEFAULT;
	}

	err = h2omg_atl_v1_state_get(info, &info->boot_state);
	if (err)
		return -ENODEV;

	/*
	 * update the config based on the device tree setting.  The fuse enable is controlled
	 * by userspace so ignore that field.
	 */
	err = h2omg_atl_v1_control_get(info, &control_current);
	if (err)
		return err;

	/* put the device in single or dual trigger mode */
	err = h2omg_atl_v1_control_set(info, CONTROL_DISABLED);
	if (err)
		return err;

	err = h2omg_atl_get_revision(info, &info->revision);
	if (err)
		return err;

	err = h2omg_atl_v1_update_timing_regs(info);
	if (err)
		return err;

	info->dual_trigger = of_property_read_bool(dev->of_node, "dual-trigger");
	if (info->dual_trigger)
		err = h2omg_atl_v1_set_trigger_state(info, DUAL_TRIGGER_INITIAL);
	else
		err = h2omg_atl_v1_set_trigger_state(info, SINGLE_TRIGGER_INITIAL);
	if (err)
		return err;

	/* put control settings back to original if all but the fuse
	 * field matches the default setting
	 */
	if ((control_current | FLD_CONTROL_ARM_FUSE) ==
	    (info->control_set | FLD_CONTROL_ARM_FUSE))
		err = h2omg_atl_v1_control_set(info, control_current);
	else
		err = h2omg_atl_v1_control_set(info, info->control_set);
	if (err)
		return err;

	return h2omg_atl_v1_state_get(info, &info->latched_state);
}
