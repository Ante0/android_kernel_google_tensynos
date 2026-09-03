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
#define FLD_CONTROL_ENABLE       BIT(0)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR0      BIT(1)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR1      BIT(2)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SENSOR2      BIT(3)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_ARM_FUSE     BIT(4)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_DRY_DETECT   BIT(5)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_FORCE_ACMP   BIT(6)  /* 0: Enable  1: Disable */
#define FLD_CONTROL_FAULT_ENABLE BIT(7)  /* 0: Disable 1: Enable  */
#define FLD_CONTROL_SNSRS        GENMASK(3, 1)

#define CONTROL_ENABLE_ON        1
#define CONTROL_ENABLE_OFF       0
#define CONTROL_SENSOR_ON        1
#define CONTROL_SENSOR_OFF       0
#define CONTROL_DRY_DETECT_ON    1
#define CONTROL_DRY_DETECT_OFF   0
#define CONTROL_FORCE_ACMP_ON    0
#define CONTROL_FORCE_ACMP_OFF   1
#define CONTROL_FAULT_ENABLE_ON  1
#define CONTROL_FAULT_ENABLE_OFF 0

#define CONTROL_DISABLED         0x40
#define CONTROL_DEFAULT          0x4f

#define CONTROL_ACMP_ENTER_STEP1 0x00
#define CONTROL_ACMP_ENTER_STEP2 0x01
#define CONTROL_ACMP_RUN_ENABLE  0x01
#define CONTROL_ACMP_RUN_DISABLE 0x00
#define CONTROL_ACMP_EXIT_STEP1  0x40
#define CONTROL_ACMP_EXIT_STEP2  0x41

#define ACMP_READ_DELAY_MS       20

#define REG_STATUS0 0x48
#define FLD_STATUS0_INTS0 BIT(5)
#define FLD_STATUS0_INTS1 BIT(6)
#define FLD_STATUS0_INTS2 BIT(7)

#define REG_STATUS1 0x49
#define FLD_STATUS1_REFS0 BIT(0)

#define REG_FUSE_STATUS 0x4d
#define FLD_FUSE_STATUS BIT(4)

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
#define FLD_ACMP_STATUS_SNS0 BIT(0)
#define FLD_ACMP_STATUS_SNS1 BIT(1)
#define FLD_ACMP_STATUS_SNS2 BIT(2)
#define FLD_ACMP_STATUS_REF0 BIT(3)

/* Comparators */
#define REG_ACMP 0x4f
#define FLD_ACMP GENMASK(3, 0)

#define NUM_SENSORS 3

static int h2omg_slg_v2_sensor_valid(enum h2omg_sensor_id id)
{
	return (id <= SENSOR_REFERENCE);
}

static int h2omg_slg_v2_sensor_enable_id2bit(enum h2omg_sensor_id id, unsigned int *bit)
{
	int err = 0;

	switch (id) {
	case SENSOR0:
			*bit = 1;
		break;
	case SENSOR1:
			*bit = 2;
		break;
	case SENSOR2:
			*bit = 3;
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

static int h2omg_slg_v2_control_get(struct h2omg_info *info, unsigned int *val)
{
	return regmap_read(info->regmap, REG_CONTROL, val);
}

static int h2omg_slg_v2_control_set(struct h2omg_info *info, unsigned int val)
{
	return regmap_write(info->regmap, REG_CONTROL, val);
}

static int h2omg_slg_v2_fuse_enable_get(struct h2omg_info *info, bool *enable)
{
	unsigned int vreg;
	int err;

	err = h2omg_slg_v2_control_get(info, &vreg);
	if (err)
		return err;

	*enable = FIELD_GET(FLD_CONTROL_ARM_FUSE, vreg);
	return 0;
}

static int h2omg_slg_v2_fuse_enable_set(struct h2omg_info *info, bool enable)
{
	unsigned int ctrl;
	int err;

	err = h2omg_slg_v2_control_get(info, &ctrl);
	if (err)
		return err;
	info->control_set = u8_replace_bits(ctrl, enable, FLD_CONTROL_ARM_FUSE);
	return h2omg_slg_v2_control_set(info, info->control_set);
}

static int h2omg_slg_v2_fuse_state_get(struct h2omg_info *info, enum h2omg_fuse_state *state)
{
	unsigned int fuse;
	int err;

	err = regmap_read(info->regmap, REG_FUSE_STATUS, &fuse);
	if (err)
		return err;

	/* invert fuse value */
	*state = !FIELD_PREP(FLD_VSTATUS_FUSE,  FIELD_GET(FLD_FUSE_STATUS, fuse));
	return 0;
}

static int h2omg_slg_v2_status_get(struct h2omg_info *info, unsigned int *vreg)
{
	unsigned int sts0;
	unsigned int sts1;
	enum h2omg_fuse_state fuse;
	int err;

	err = regmap_read(info->regmap, REG_STATUS0, &sts0);
	if (err)
		return err;

	err = regmap_read(info->regmap, REG_STATUS1, &sts1);
	if (err)
		return err;

	err = h2omg_slg_v2_fuse_state_get(info, &fuse);
	if (err)
		return err;

	*vreg = FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_STATUS1_REFS0, sts1)) |
		FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_STATUS0_INTS0, sts0)) |
		FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_STATUS0_INTS1, sts0)) |
		FIELD_PREP(FLD_VSTATUS_INTS2, FIELD_GET(FLD_STATUS0_INTS2, sts0)) |
		FIELD_PREP(FLD_VSTATUS_FUSE,  fuse);

	return 0;
}

static int h2omg_slg_v2_sensor_enable_get(struct h2omg_info *info,
					  enum h2omg_sensor_id id,
					  bool *enable)
{
	unsigned int vreg;
	unsigned int bit;
	int err;

	err = h2omg_slg_v2_sensor_enable_id2bit(id, &bit);
	if (err)
		return err;

	err = h2omg_slg_v2_control_get(info, &vreg);
	if (err)
		return err;

	*enable = !!(vreg & 1 << bit);
	return 0;
}

static int h2omg_slg_v2_sensor_enable_set(struct h2omg_info *info,
					  enum h2omg_sensor_id id,
					  bool enable)
{
	unsigned int ctrl;
	unsigned int mask;
	unsigned int bit;
	int err;

	err = h2omg_slg_v2_sensor_enable_id2bit(id, &bit);
	if (err)
		return err;

	mask = 1 << bit;

	err = h2omg_slg_v2_control_get(info, &ctrl);
	if (err)
		return err;
	return h2omg_slg_v2_control_set(info, h2omg_reg_mod(mask, enable, ctrl));
}

static int h2omg_slg_v2_sensor_mode_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode *mode)
{
	unsigned int val;
	int err;

	err = h2omg_slg_v2_control_get(info, &val);
	if (err)
		return err;

	if (FIELD_GET(FLD_CONTROL_DRY_DETECT, val) == CONTROL_DRY_DETECT_ON)
		*mode = DETECT_DRY;
	else
		*mode = DETECT_WET;

	return 0;
}

/* version slg v2 only has a single mode, changing any sensor mode changes them all */
static int h2omg_slg_v2_sensor_mode_set(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode mode)
{
	unsigned int ctrl;
	unsigned int val = mode;
	unsigned int mask = FLD_CONTROL_DRY_DETECT;
	int err;

	if (!h2omg_slg_v2_sensor_valid(id))
		return -EINVAL;

	err = h2omg_slg_v2_control_get(info, &ctrl);
	if (err)
		return err;

	return h2omg_slg_v2_control_set(info, h2omg_reg_mod(mask, val, ctrl));
}

static int h2omg_slg_v2_sensor_acmp_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					unsigned int *acmp)
{
	unsigned int val, ctrl;
	unsigned int sts0;
	int err;

	/* get initial value to restore at the end */
	err = h2omg_slg_v2_control_get(info, &ctrl);
	if (err)
		goto err_out;

	err = h2omg_slg_v2_control_set(info, CONTROL_ACMP_ENTER_STEP1);
	if (err)
		goto err_out;

	err = h2omg_slg_v2_control_set(info, CONTROL_ACMP_ENTER_STEP2);
	if (err)
		goto err_out;

	/* run the test based on current setting of the control bit */
	if (ctrl & FLD_CONTROL_ENABLE)
		err = h2omg_slg_v2_control_set(info, CONTROL_ACMP_RUN_ENABLE);
	else
		err = h2omg_slg_v2_control_set(info, CONTROL_ACMP_RUN_DISABLE);
	if (err)
		goto err_out;

	msleep(ACMP_READ_DELAY_MS);

	/* read  result */
	err = regmap_read(info->regmap, REG_ACMP_STATUS, &sts0);
	if (err) {
		mutex_unlock(&info->lock);
		return err;
	}

	err = h2omg_slg_v2_control_set(info, CONTROL_ACMP_EXIT_STEP1);
	if (err)
		goto err_out;
	err = h2omg_slg_v2_control_set(info, CONTROL_ACMP_EXIT_STEP2);
	if (err)
		goto err_out;

	/* put things back */
	err = h2omg_slg_v2_control_set(info, ctrl);
	if (err)
		goto err_out;

	/* read out results */
	val = FIELD_PREP(FLD_VSTATUS_INTS0, FIELD_GET(FLD_ACMP_STATUS_SNS0, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_INTS1, FIELD_GET(FLD_ACMP_STATUS_SNS1, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_INTS2, FIELD_GET(FLD_ACMP_STATUS_SNS2, sts0)) |
	      FIELD_PREP(FLD_VSTATUS_REFS0, FIELD_GET(FLD_ACMP_STATUS_REF0, sts0));

	*acmp = (val >> (__bf_shf(FLD_VSTATUS_INTS0) + id)) & 0x01;
err_out:
	return err;
}

static int h2omg_slg_v2_state_get(struct h2omg_info *info, struct h2omg_state *state)
{
	unsigned int ctrl;
	unsigned int sts;
	unsigned int ndx;
	int err;

	err = h2omg_slg_v2_control_get(info, &ctrl);
	if (err)
		return err;

	err = h2omg_slg_v2_status_get(info, &sts);
	if (err)
		return err;

	state->detect_enabled = FIELD_GET(FLD_CONTROL_ENABLE, ctrl);
	if (!state->detect_enabled)
		state->sensors[SENSOR_REFERENCE] = SENSOR_DIS;
	else if (FIELD_GET(FLD_VSTATUS_REFS0, sts))
		state->sensors[SENSOR_REFERENCE] = SENSOR_WET;
	else
		state->sensors[SENSOR_REFERENCE] = SENSOR_DRY;

	for (ndx = 0; ndx < NUM_SENSORS; ndx++) {
		unsigned int sensor_enabled = (ctrl >> (__bf_shf(FLD_CONTROL_SENSOR0) + ndx)) & 0x1;
		unsigned int sensor_val = (sts >> (__bf_shf(FLD_VSTATUS_INTS0) + ndx)) & 0x1;

		if (!(state->detect_enabled && sensor_enabled))
			state->sensors[ndx] = SENSOR_DIS;
		else if (state->sensors[SENSOR_REFERENCE] == SENSOR_WET)
			state->sensors[ndx] = SENSOR_INV;
		else if (sensor_val)
			state->sensors[ndx] = SENSOR_WET;
		else
			state->sensors[ndx] = SENSOR_DRY;
	}
	state->fuse = FIELD_GET(FLD_VSTATUS_FUSE, sts);

	return 0;
}

static irqreturn_t h2omg_slg_v2_irq_handler(int irq, void *data)
{
	struct h2omg_info *info = data;
	struct device *dev = info->dev;
	unsigned int control_save;
	unsigned int state_vreg;
	unsigned int state_reference;
	unsigned int state_sensor0;
	unsigned int state_sensor1;
	unsigned int state_sensor2;
	unsigned int ctrl_sensor0;
	unsigned int ctrl_sensor1;
	unsigned int ctrl_sensor2;
	int err;
	bool wet_detect;

	mutex_lock(&info->lock);
	err = h2omg_slg_v2_control_get(info, &control_save);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_slg_v2_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	err = h2omg_slg_v2_state_get(info, &info->latched_state);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_slg_v2_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	err = h2omg_slg_v2_status_get(info, &state_vreg);
	if (err) {
		dev_err(dev, "IRQ: control read failed (%d)\n", err);
		h2omg_slg_v2_control_set(info, CONTROL_DISABLED);
		disable_irq(irq);
		mutex_unlock(&info->lock);
		return IRQ_HANDLED;
	}

	h2omg_slg_v2_control_set(info, CONTROL_DISABLED);
	mdelay(20);

	state_reference = FIELD_GET(FLD_VSTATUS_REFS0, state_vreg);
	state_sensor0 = FIELD_GET(FLD_VSTATUS_INTS0, state_vreg);
	state_sensor1 = FIELD_GET(FLD_VSTATUS_INTS1, state_vreg);
	state_sensor2 = FIELD_GET(FLD_VSTATUS_INTS2, state_vreg);
	ctrl_sensor0 = FIELD_GET(FLD_CONTROL_SENSOR0, control_save);
	ctrl_sensor1 = FIELD_GET(FLD_CONTROL_SENSOR1, control_save);
	ctrl_sensor2 = FIELD_GET(FLD_CONTROL_SENSOR2, control_save);

	wet_detect = !FIELD_GET(FLD_CONTROL_DRY_DETECT, control_save);
	if (wet_detect) {
		/* wet detect */
		if (state_reference) {
			/* reference is invalid need to dry that alone */
			dev_info(dev, "Reference sensor got wet -> reference dry detect\n");
			control_save = u32_replace_bits(control_save,
							CONTROL_DRY_DETECT_ON,
							FLD_CONTROL_DRY_DETECT);
			control_save = u32_replace_bits(control_save,
							CONTROL_ENABLE_ON,
							FLD_CONTROL_ENABLE);

			/* disable all sensors */
			control_save = u32_replace_bits(control_save,
							CONTROL_SENSOR_OFF,
							FLD_CONTROL_SENSOR0);
			control_save = u32_replace_bits(control_save,
							CONTROL_SENSOR_OFF,
							FLD_CONTROL_SENSOR1);
			control_save = u32_replace_bits(control_save,
							CONTROL_SENSOR_OFF,
							FLD_CONTROL_SENSOR2);

		} else if (FIELD_GET(FLD_VSTATUS_SNSRS, state_vreg)) {
			/* At least one of the sensors is wet */
			dev_info(dev, "sensor(s) got wet -> dry detect\n");
			control_save = u32_replace_bits(control_save,
							CONTROL_DRY_DETECT_ON,
							FLD_CONTROL_DRY_DETECT);
			control_save = u32_replace_bits(control_save,
							CONTROL_ENABLE_ON,
							FLD_CONTROL_ENABLE);

			/* disable any non-triggering sensors */
			control_save = u32_replace_bits(control_save,
							state_sensor0,
							FLD_CONTROL_SENSOR0);
			control_save = u32_replace_bits(control_save,
							state_sensor1,
							FLD_CONTROL_SENSOR1);
			control_save = u32_replace_bits(control_save,
							state_sensor2,
							FLD_CONTROL_SENSOR2);
		} else {
			/* Spurious interrupt stay in wet mode */
			dev_info(dev, "IRQ: spurious interrupt %#02x\n", state_vreg);
		}
	} else {
		/* dry detect */
		if (state_reference) {
			/* reference is now valid, go to wet mode */
			dev_info(dev, "sensor reference dried out -> initial state\n");
			control_save = info->control_set;
		} else if (state_sensor0 | state_sensor1 | state_sensor2) {
			/* At least one of the sensors is dried out */
			if (ctrl_sensor0 && state_sensor0)
				control_save = u32_replace_bits(control_save,
								CONTROL_SENSOR_OFF,
								FLD_CONTROL_SENSOR0);
			if (ctrl_sensor1 && state_sensor1)
				control_save = u32_replace_bits(control_save,
								CONTROL_SENSOR_OFF,
								FLD_CONTROL_SENSOR1);
			if (ctrl_sensor2 && state_sensor2)
				control_save = u32_replace_bits(control_save,
								CONTROL_SENSOR_OFF,
								FLD_CONTROL_SENSOR2);

			if (!FIELD_GET(FLD_CONTROL_SNSRS, control_save)) {
				/* all the enabled sensors are dry so go back to wet detect */
				dev_info(dev, "IRQ: All sensors are dry %#02x\n", state_vreg);
				control_save = u32_replace_bits(info->control_set,
								CONTROL_DRY_DETECT_OFF,
								FLD_CONTROL_DRY_DETECT);
			} else  {
				/* not all the enabled sensors are dry state in dry detect */
				dev_info(dev, "IRQ: DRY -> DRY [sens] %#02x\n", state_vreg);
			}
		} else {
			/* Spurious interrupt stay in dry mode */
			dev_err(dev, "IRQ: DRY -> DRY [spurious] (0x%02x)\n", state_vreg);
		}
	}

	h2omg_slg_v2_control_set(info, control_save);

	mutex_unlock(&info->lock);
	kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	return IRQ_HANDLED;
}

/* external */
static int h2omg_slg_v2_cleanup(struct h2omg_info *info)
{
	return 0;
}

/* external */
static size_t h2omg_slg_v2_sensor_count_get(struct h2omg_info *info)
{
	return NUM_SENSORS;
}

static const struct regmap_range h2omg_read_range[] = {
	regmap_reg_range(REG_CONTROL, REG_CONTROL),
	regmap_reg_range(REG_STATUS0, REG_STATUS1),
	regmap_reg_range(REG_FUSE_STATUS, REG_FUSE_STATUS),
	regmap_reg_range(REG_ACMP, REG_ACMP),
};

static const struct regmap_access_table h2omg_read_table = {
	.yes_ranges = h2omg_read_range,
	.n_yes_ranges = ARRAY_SIZE(h2omg_read_range),
};

static const struct regmap_range h2omg_write_range[] = {
	regmap_reg_range(REG_CONTROL, REG_CONTROL),
};

static const struct regmap_access_table h2omg_write_table = {
	.yes_ranges = h2omg_write_range,
	.n_yes_ranges = ARRAY_SIZE(h2omg_write_range),
};

static const struct regmap_config h2omg_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x60,
	.wr_table = &h2omg_write_table,
	.rd_table = &h2omg_read_table,
};

static const struct h2omg_ops h2omg_slg_v2_ops = {
	.control_get = h2omg_slg_v2_control_get,
	.control_set = h2omg_slg_v2_control_set,

	.status_get = h2omg_slg_v2_status_get,

	.fuse_enable_get = h2omg_slg_v2_fuse_enable_get,
	.fuse_enable_set = h2omg_slg_v2_fuse_enable_set,
	.fuse_state_get = h2omg_slg_v2_fuse_state_get,

	.sensor_count_get = h2omg_slg_v2_sensor_count_get,
	.sensor_enable_get = h2omg_slg_v2_sensor_enable_get,
	.sensor_enable_set = h2omg_slg_v2_sensor_enable_set,
	.sensor_mode_get = h2omg_slg_v2_sensor_mode_get,
	.sensor_mode_set = h2omg_slg_v2_sensor_mode_set,
	.sensor_acmp_get = h2omg_slg_v2_sensor_acmp_get,

	.irq_handler = h2omg_slg_v2_irq_handler,
	.cleanup = h2omg_slg_v2_cleanup,
};

int h2omg_slg_v2_init(struct i2c_client *client)
{
	struct h2omg_info *info = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	unsigned int control_current;
	int err;

	dev_info(dev, "revision slg_v2\n");
	client->dev.init_name = "i2c-h2omg";

	info->regmap = devm_regmap_init_i2c(client, &h2omg_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Regmap init failed");
		return PTR_ERR(info->regmap);
	}
	info->num_sensors = NUM_SENSORS;
	info->ops = &h2omg_slg_v2_ops;

	err = of_property_read_u8(dev->of_node, "control", &info->control_set);
	if (err) {
		dev_warn(dev, "No initial control value using %#02x %d\n", CONTROL_DEFAULT, err);
		info->control_set = CONTROL_DEFAULT;
	}

	err = h2omg_slg_v2_state_get(info, &info->boot_state);
	if (err)
		return -ENODEV;
	/*
	 * update the config based on the device tree setting.  The fuse enable is controlled
	 * by userspace so ignore that field.
	 */
	err = h2omg_slg_v2_control_get(info, &control_current);
	if (err)
		return err;

	if ((control_current | FLD_CONTROL_ARM_FUSE) !=
	    (info->control_set | FLD_CONTROL_ARM_FUSE)) {
		err = h2omg_slg_v2_control_set(info, CONTROL_DISABLED);
		if (err)
			return err;
		err = h2omg_slg_v2_control_set(info, info->control_set);
		if (err)
			return err;
	}
	return h2omg_slg_v2_state_get(info, &info->latched_state);
}
