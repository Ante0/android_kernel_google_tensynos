// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS I2C Device Driver
 *
 * Copyright (c) 2018 Google, LLC
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-i2c-dev: " fmt

#include "lwis_device_i2c.h"

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/slab.h>
#include <uapi/linux/sched/types.h>

#include "lwis_platform.h"
#include "lwis_device.h"
#include "lwis_i2c.h"
#include "lwis_periodic_io.h"
#include "lwis_util.h"
#include "lwis_trace.h"

#ifdef CONFIG_OF
#include "lwis_dt.h"
#endif

#define LWIS_DRIVER_NAME "lwis-i2c"

#define I2C_DEFAULT_STATE_STRING "default"
#define I2C_ON_STRING "on_i2c"
#define I2C_OFF_STRING "off_i2c"

#define OTP_CMD_WAIT_US 0xFFFF
#define OTP_CMD_WAIT_US_RANGE_ADDITION 300

static struct mutex group_i2c_lock[MAX_I2C_LOCK_NUM];

static int lwis_i2c_device_enable(struct lwis_device *lwis_dev);
static int lwis_i2c_device_disable(struct lwis_device *lwis_dev);
static int lwis_i2c_device_resume(struct lwis_device *lwis_dev);
static int lwis_i2c_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);
static int lwis_i2c_batch_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entries,
				      int batch_size);

struct lwis_device_subclass_operations i2c_vops = {
	.register_io = lwis_i2c_register_io,
	.register_io_locked = lwis_i2c_register_io,
	.batch_register_io = lwis_i2c_batch_register_io,
	.register_io_barrier = NULL,
	.device_enable = lwis_i2c_device_enable,
	.device_disable = lwis_i2c_device_disable,
	.device_resume = lwis_i2c_device_resume,
	.device_suspend = NULL,
	.event_enable = NULL,
	.event_flags_updated = NULL,
	.close = NULL,
};

int lwis_otp_set_config(struct lwis_i2c_device *device, struct lwis_otp_config *otp_config)
{
	int ret = 0;

	for (int i = 0; i < otp_config->setting_count; ++i) {
		uint32_t reg_addr = otp_config->settings[i].reg_addr;
		uint32_t value = otp_config->settings[i].value;

		if (reg_addr == OTP_CMD_WAIT_US) {
			if (value > 0)
				usleep_range(value, value + OTP_CMD_WAIT_US_RANGE_ADDITION);
			continue;
		}

		ret = lwis_i2c_write(device, reg_addr, value);
		if (ret) {
			dev_err(device->base_dev.dev, "Failed to write OTP settings(0x%x, 0x%x)\n",
				reg_addr, value);
			return ret;
		}
	}
	if (otp_config->settle_time_us > 0)
		usleep_range(otp_config->settle_time_us,
			     otp_config->settle_time_us + OTP_CMD_WAIT_US_RANGE_ADDITION);

	return ret;
}

static int lwis_i2c_device_enable(struct lwis_device *lwis_dev)
{
	int ret;
	struct lwis_i2c_device *i2c_dev;

	i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	/* Enable the I2C bus */
	mutex_lock(i2c_dev->group_i2c_lock);
	LWIS_ATRACE_FUNC_BEGIN(lwis_dev, "lwis_i2c_device_enable");
#if IS_ENABLED(CONFIG_INPUT_STMVL53L1)
	if (is_shared_i2c_with_stmvl53l1(i2c_dev->state_pinctrl))
		ret = shared_i2c_set_state(&i2c_dev->client->dev, i2c_dev->state_pinctrl,
					   I2C_ON_STRING);
	else
		ret = lwis_i2c_set_state(i2c_dev, I2C_ON_STRING);
#else
	ret = lwis_i2c_set_state(i2c_dev, I2C_ON_STRING);
#endif

	if (i2c_dev->is_i2c_otp && !lwis_dev->power_up_to_suspend) {
		ret = lwis_otp_set_config(i2c_dev, &i2c_dev->i2c_otp_config);
		if (ret)
			dev_err(i2c_dev->base_dev.dev, "Failed to set I2C OTP config\n");
	}

	mutex_unlock(i2c_dev->group_i2c_lock);
	LWIS_ATRACE_FUNC_END(lwis_dev, "lwis_i2c_device_enable");
	if (ret) {
		dev_err(lwis_dev->dev, "Error enabling i2c bus (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int lwis_i2c_device_disable(struct lwis_device *lwis_dev)
{
	int ret;
	struct lwis_i2c_device *i2c_dev;

	i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	if (IS_ERR_OR_NULL(i2c_dev->state_pinctrl)) {
		dev_err(lwis_dev->dev, "i2c state_pinctrl is invalid (%lu)\n",
			PTR_ERR(i2c_dev->state_pinctrl));
		i2c_dev->state_pinctrl = NULL;
	}

#if IS_ENABLED(CONFIG_INPUT_STMVL53L1)
	if (is_shared_i2c_with_stmvl53l1(i2c_dev->state_pinctrl)) {
		/* Disable the shared i2c bus */
		mutex_lock(i2c_dev->group_i2c_lock);
		ret = shared_i2c_set_state(&i2c_dev->client->dev, i2c_dev->state_pinctrl,
					   I2C_OFF_STRING);
		mutex_unlock(i2c_dev->group_i2c_lock);
		if (ret)
			dev_err(lwis_dev->dev, "Error disabling i2c bus (%d)\n", ret);

		return ret;
	}
#endif

	mutex_lock(i2c_dev->group_i2c_lock);
	if (!lwis_i2c_i3c_dev_is_in_use(lwis_dev)) {
		/* Disable the I2C bus */
		LWIS_ATRACE_FUNC_BEGIN(lwis_dev, "lwis_i2c_device_disable");
		ret = lwis_i2c_set_state(i2c_dev, I2C_OFF_STRING);
		LWIS_ATRACE_FUNC_END(lwis_dev, "lwis_i2c_device_disable");
		if (ret) {
			dev_err(lwis_dev->dev, "Error disabling i2c bus (%d)\n", ret);
			mutex_unlock(i2c_dev->group_i2c_lock);
			return ret;
		}
	}
	mutex_unlock(i2c_dev->group_i2c_lock);

	return 0;
}

static int lwis_i2c_device_resume(struct lwis_device *lwis_dev)
{
	int ret;
	struct lwis_i2c_device *i2c_dev;

	i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);
	if (i2c_dev->is_i2c_otp) {
		ret = lwis_otp_set_config(i2c_dev, &i2c_dev->i2c_otp_config);
		if (ret) {
			dev_err(lwis_dev->dev, "Failed to set I2C OTP config\n");
			return ret;
		}
	}
	return 0;
}

static int lwis_i2c_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	struct lwis_i2c_device *i2c_dev;

	i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	/* Running in interrupt context is not supported as i2c driver might sleep */
	if (in_interrupt())
		return -EAGAIN;

	lwis_save_register_io_info(lwis_dev, entry);

	if (entry->type == LWIS_IO_ENTRY_MODIFY)
		return lwis_i2c_io_entry_mod(i2c_dev, entry);

	return lwis_i2c_io_entry_rw(i2c_dev, entry);
}

static int lwis_i2c_batch_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entries,
				      int batch_size)
{
	struct lwis_i2c_device *i2c_dev;

	i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	/* Running in interrupt context is not supported as i3c driver might sleep */
	if (in_interrupt())
		return -EAGAIN;

	return lwis_i2c_io_entries_rw(i2c_dev, entries, batch_size);
}

static int i2c_addr_matcher(struct device *dev, void *data)
{
	struct i2c_client *client = i2c_verify_client(dev);
	int address = *(int *)data;

	/* Return 0 if error, or address doesn't match */
	if (IS_ERR_OR_NULL(client) || (client->addr != address))
		return 0;

	/* Return 1 when address is found */
	return 1;
}

int lwis_i2c_device_setup(struct lwis_i2c_device *i2c_dev)
{
	int ret;
	struct device *dev;
	struct pinctrl *pinctrl;
	struct pinctrl_state *state;

#ifdef CONFIG_OF
	/* Parse device tree for device configurations */
	ret = lwis_i2c_device_parse_dt(i2c_dev);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Failed to parse device tree\n");
		return ret;
	}
#else
	/* Non-device-tree init: Save for future implementation */
	return -EINVAL;
#endif

	/* Initialize device i2c lock */
	i2c_dev->group_i2c_lock = &group_i2c_lock[i2c_dev->i2c_lock_group_id];

	/* Controller device is set as the parent of adapter device  */
	i2c_dev->base_dev.controller_dev = i2c_dev->adapter->dev.parent;

	/* Find the client instance in the adapter */
	dev = device_find_child(&i2c_dev->adapter->dev, &i2c_dev->address, i2c_addr_matcher);
	if (dev) {
		i2c_dev->client = i2c_verify_client(dev);
	} else {
		struct i2c_board_info info = { .addr = i2c_dev->address };

		/* If not found in the adapter, Create new device and add it on the adpater */
		i2c_dev->client = i2c_new_client_device(i2c_dev->adapter, &info);
	}

	/* Still getting error in obtaining client, return error */
	if (IS_ERR_OR_NULL(i2c_dev->client)) {
		dev_err(i2c_dev->base_dev.dev, "Failed to create or find i2c device\n");
		return -EINVAL;
	}

	dev = &i2c_dev->client->dev;

	/* Parent of the client is the i2c block, which is where the i2c state
	 * pinctrl's are defined
	 *
	 * TODO: Need to figure out why this is parent's parent
	 */
	pinctrl = devm_pinctrl_get(dev->parent->parent);
	if (IS_ERR_OR_NULL(pinctrl)) {
		dev_err(i2c_dev->base_dev.dev, "Cannot instantiate pinctrl instance (%lu)\n",
			PTR_ERR(pinctrl));
		i2c_dev->state_pinctrl = NULL;
		return PTR_ERR(pinctrl);
	}

	/* Verify that on_i2c or off_i2c strings are present */
	i2c_dev->pinctrl_default_state_only = false;
	if (IS_ERR_OR_NULL(pinctrl_lookup_state(pinctrl, I2C_OFF_STRING)) ||
	    IS_ERR_OR_NULL(pinctrl_lookup_state(pinctrl, I2C_ON_STRING))) {
		state = pinctrl_lookup_state(pinctrl, I2C_DEFAULT_STATE_STRING);
		/* Default option also missing, return error */
		if (IS_ERR_OR_NULL(state)) {
			dev_err(i2c_dev->base_dev.dev,
				"Pinctrl states {%s, %s, %s} not found (%lu)\n", I2C_OFF_STRING,
				I2C_ON_STRING, I2C_DEFAULT_STATE_STRING, PTR_ERR(state));
			return PTR_ERR(state);
		}
		/* on_i2c or off_i2c not found, fall back to default */
		dev_warn(i2c_dev->base_dev.dev,
			 "pinctrl state %s or %s not found, fall back to %s\n", I2C_OFF_STRING,
			 I2C_ON_STRING, I2C_DEFAULT_STATE_STRING);
		i2c_dev->pinctrl_default_state_only = true;
	}
	i2c_dev->state_pinctrl = pinctrl;

	return 0;
}

static int lwis_i2c_device_probe(struct platform_device *plat_dev)
{
	int ret = 0;
	struct lwis_i2c_device *i2c_dev;
	struct device *dev = &plat_dev->dev;

	/* Allocate I2C device specific data construct */
	i2c_dev = devm_kzalloc(dev, sizeof(struct lwis_i2c_device), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	i2c_dev->base_dev.type = DEVICE_TYPE_I2C;
	i2c_dev->base_dev.vops = i2c_vops;
	i2c_dev->base_dev.plat_dev = plat_dev;
	i2c_dev->base_dev.k_dev = &plat_dev->dev;

	/* Call the base device probe function */
	ret = lwis_base_probe(&i2c_dev->base_dev);
	if (ret) {
		dev_err_probe(dev, ret, "Error in lwis base probe\n");
		return ret;
	}
	platform_set_drvdata(plat_dev, &i2c_dev->base_dev);

	/* Call I2C device specific setup function */
	ret = lwis_i2c_device_setup(i2c_dev);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error in i2c device initialization\n");
		lwis_base_unprobe(&i2c_dev->base_dev);
		return ret;
	}

	/* Remove resume callback if not needed */
	if (!i2c_dev->is_i2c_otp) {
		dev_info(i2c_dev->base_dev.dev,
			 "is_i2c_otp is false, setting device_resume to NULL\n");
		i2c_dev->base_dev.vops.device_resume = NULL;
	}

	if (!lwis_platform_is_batch_register_io_supported() ||
	    of_property_read_bool(i2c_dev->base_dev.k_dev->of_node, "lwis,disable-group-transfer"))
		i2c_dev->base_dev.vops.batch_register_io = NULL;

	ret = lwis_bus_manager_create(&i2c_dev->base_dev);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error in i2c bus manager creation\n");
		lwis_base_unprobe(&i2c_dev->base_dev);
		return ret;
	}

	dev_info(i2c_dev->base_dev.dev, "I2C Device Probe: Success\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_I2C_DEVICE_COMPAT },
	{},
};
MODULE_DEVICE_TABLE(of, lwis_id_match);

static struct platform_driver lwis_driver = {
	.probe = lwis_i2c_device_probe,
	.driver = {
			.name = LWIS_DRIVER_NAME,
			.owner = THIS_MODULE,
			.of_match_table = lwis_id_match,
		},
};
#else /* CONFIG_OF not defined */
static struct platform_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lwis_driver_id);

static struct platform_driver lwis_driver = { .probe = lwis_i2c_device_probe,
					      .id_table = lwis_driver_id,
					      .driver = {
						      .name = LWIS_DRIVER_NAME,
						      .owner = THIS_MODULE,
					      } };
#endif /* CONFIG_OF */

/*
 *  lwis_i2c_device_init: Init function that will be called by the kernel
 *  initialization routines.
 */
int __init lwis_i2c_device_init(void)
{
	int ret = 0;
	int i;

	pr_info("I2C device initialization\n");

	ret = platform_driver_register(&lwis_driver);
	if (ret)
		pr_err("platform_driver_register failed: %d\n", ret);

	for (i = 0; i < MAX_I2C_LOCK_NUM; ++i)
		mutex_init(&group_i2c_lock[i]);

	return ret;
}

int lwis_i2c_device_deinit(void)
{
	platform_driver_unregister(&lwis_driver);
	return 0;
}
