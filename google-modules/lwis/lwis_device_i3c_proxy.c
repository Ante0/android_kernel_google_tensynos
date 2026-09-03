// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS I3C Proxy Device with I2C
 *
 * Copyright 2024 Google LLC.
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-i3c-proxy-dev: " fmt

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i3c/master.h>
#include <linux/preempt.h>
#include <linux/errno.h>

#include "lwis_platform.h"
#include "lwis_device_i3c_proxy.h"
#include "lwis_device.h"
#include "lwis_device_i2c.h"
#include "lwis_i3c_proxy.h"
#include "lwis_i2c.h"

#ifdef CONFIG_OF
#include "lwis_dt.h"
#endif

#define LWIS_DRIVER_NAME "lwis-i3c-proxy"

/*
 *  0: Device Tree Configuration (DT config) to I3C Mode by default
 *  1: Force overwrite to I2C Mode
 */
static int force_i2c_mode;
module_param(force_i2c_mode, int, 0644);
MODULE_PARM_DESC(force_i2c_mode,
		 "Force the device to operate in I2C mode, overriding I3C mode (default: false)");

extern struct lwis_device_subclass_operations i2c_vops;

static bool lwis_i3c_device_matches(struct lwis_i2c_device *i2c_dev, struct i3c_dev_desc *i3cdev)
{
	if (!i2c_dev || !i3cdev)
		return false;

	switch (i2c_dev->match_type) {
	case LWIS_I3C_MATCH_PID:
		return i3cdev->info.pid == i2c_dev->match.pid;
	case LWIS_I3C_MATCH_DCR:
		return i3cdev->info.dcr == i2c_dev->match.dcr;
	case LWIS_I3C_MATCH_BCR:
		return i3cdev->info.bcr == i2c_dev->match.bcr;
	case LWIS_I3C_MATCH_NONE:
	default:
		return false;
	}
}

static int lwis_i3c_proxy_device_enable(struct lwis_device *lwis_dev);
static int lwis_i3c_proxy_device_disable(struct lwis_device *lwis_dev);
static int lwis_i3c_proxy_device_resume(struct lwis_device *lwis_dev);
static int lwis_i3c_proxy_device_suspend(struct lwis_device *lwis_dev);
static int lwis_i3c_proxy_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);
static int lwis_i3c_proxy_batch_register_io(struct lwis_device *lwis_dev,
					    struct lwis_io_entry *entries, int batch_size);

static struct lwis_device_subclass_operations i3c_vops = {
	.register_io = lwis_i3c_proxy_register_io,
	.register_io_locked = lwis_i3c_proxy_register_io,
	.batch_register_io = lwis_i3c_proxy_batch_register_io,
	.register_io_barrier = NULL,
	.device_enable = lwis_i3c_proxy_device_enable,
	.device_disable = lwis_i3c_proxy_device_disable,
	.device_resume = lwis_i3c_proxy_device_resume,
	.device_suspend = lwis_i3c_proxy_device_suspend,
	.event_enable = NULL,
	.event_flags_updated = NULL,
	.close = NULL,
};

static void lwis_i3c_ibi_handler(struct i3c_device *i3c, const struct i3c_ibi_payload *payload)
{
	struct lwis_i2c_device *i3c_proxy_dev = i3cdev_get_drvdata(i3c);

	dev_err(i3c_proxy_dev->base_dev.dev, "IBI Event Length: %u bytes:\n", payload->len);
	for (int i = 0; i < payload->len; ++i) {
		dev_err(i3c_proxy_dev->base_dev.dev, "IBI Event Payload[%d] = %02x ", i,
			((uint8_t *)payload->data)[i]);
	}

	lwis_device_error_event_emit(&i3c_proxy_dev->base_dev, LWIS_ERROR_EVENT_ID_I3C_IBI,
				     (void *)payload->data, payload->len);
}

static int lwis_i3c_ibi_setup(struct lwis_i2c_device *i3c_proxy_dev)
{
	int ret = 0;
	const struct i3c_ibi_setup ibi = {
		.max_payload_len = i3c_proxy_dev->ibi_config.ibi_max_payload_len,
		.num_slots = i3c_proxy_dev->ibi_config.ibi_num_slots,
		.handler = lwis_i3c_ibi_handler,
	};
	struct i3c_device_info info;

	i3c_device_get_info(i3c_proxy_dev->i3c, &info);
	ret = i3c_device_request_ibi(i3c_proxy_dev->i3c, &ibi);
	if (ret == -ENOTSUPP) {
		/*
		 * This driver only supports In-Band Interrupt mode.
		 * Support for Polling Mode could be added if required.
		 * (ENOTSUPP is from the i3c layer, not EOPNOTSUPP).
		 */
		dev_warn(i3cdev_to_dev(i3c_proxy_dev->i3c),
			 "Failed, bus driver doesn't support In-Band Interrupts");
		return ret;
	} else if (ret) {
		dev_err(i3cdev_to_dev(i3c_proxy_dev->i3c), "Failed requesting IBI (%d)\n", ret);
		return ret;
	}

	ret = i3c_device_enable_ibi(i3c_proxy_dev->i3c);
	if (ret) {
		dev_err(i3cdev_to_dev(i3c_proxy_dev->i3c), "Failed enabling IBI (%d)\n", ret);
		i3c_device_free_ibi(i3c_proxy_dev->i3c);
		return ret;
	}

	i3c_proxy_dev->ibi_enabled = true;

	return ret;
}

static int lwis_i3c_proxy_device_enable(struct lwis_device *lwis_dev)
{
	return i2c_vops.device_enable(lwis_dev);
}

static int lwis_i3c_proxy_device_disable(struct lwis_device *lwis_dev)
{
	struct lwis_i2c_device *i3c_proxy_dev;
	int ret = 0;

	i3c_proxy_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	ret = i2c_vops.device_disable(lwis_dev);
	if (ret) {
		dev_err(lwis_dev->dev, "Error executing device disable function\n");
		return ret;
	}

	i3c_proxy_dev->base_dev.type = DEVICE_TYPE_I2C;
	return ret;
}

static int lwis_i3c_proxy_device_resume(struct lwis_device *lwis_dev)
{
	int ret;
	struct lwis_i2c_device *i3c_proxy_dev;
	struct i2c_adapter *adap;
	struct i3c_master_controller *master;
	struct i3c_dev_desc *i3cdev;
	struct i3c_device_info info;

	i3c_proxy_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	if (force_i2c_mode == 0 && i3c_proxy_dev->i3c_enabled) {
		ret = lwis_otp_set_config(i3c_proxy_dev, &i3c_proxy_dev->i3c_otp_config);
		if (ret) {
			dev_err(i3c_proxy_dev->base_dev.dev, "Failed to set I3C OTP config(%d)\n",
				ret);
			return ret;
		}

		adap = i3c_proxy_dev->adapter;
		master = container_of(adap, struct i3c_master_controller, i2c);
		ret = i3c_master_do_daa(master);
		if (ret) {
			dev_err(i3c_proxy_dev->base_dev.dev, "Failed to do I3C DAA(%d)\n", ret);
			return ret;
		}

		i3c_bus_for_each_i3cdev(&master->bus, i3cdev) {
			if (i3cdev == master->this)
				continue;

			if (lwis_i3c_device_matches(i3c_proxy_dev, i3cdev)) {
				i3c_proxy_dev->i3c = i3cdev->dev;
				i3cdev_set_drvdata(i3c_proxy_dev->i3c, i3c_proxy_dev);

				/* Get native device info read from hardware during DAA */
				i3c_device_get_info(i3c_proxy_dev->i3c, &info);

				/*
				 * Enable IBI only if:
				 * 1. Hardware BCR indicates IBI capability (Bit 2).
				 * 2. Software policy intends to use IBI (properties present in DT).
				 */
				if (i3c_proxy_dev->ibi_config.ibi_configured_in_dt) {
					if (info.bcr & BIT(2)) {
						ret = lwis_i3c_ibi_setup(i3c_proxy_dev);
						if (ret) {
							dev_warn(i3c_proxy_dev->base_dev.dev,
								 "Failed to setup IBI(%d)\n", ret);
						}
					} else {
						dev_err(i3c_proxy_dev->base_dev.dev,
							"BCR is wrong: IBI configured in DT but not supported by hardware (BCR: 0x%x)\n",
							info.bcr);
					}
				}
				dev_info(i3c_proxy_dev->base_dev.dev, "I3C Mode Enabled\n");
				return 0;
			}
		}
	} else {
		ret = lwis_otp_set_config(i3c_proxy_dev, &i3c_proxy_dev->i2c_otp_config);
		if (ret) {
			dev_err(i3c_proxy_dev->base_dev.dev, "Failed to set I2C OTP config(%d)\n",
				ret);
			return ret;
		}
		dev_info(i3c_proxy_dev->base_dev.dev, "I2C Mode Enabled\n");
		return ret;
	}

	return -EPERM;
}

static int lwis_i3c_proxy_device_suspend(struct lwis_device *lwis_dev)
{
	struct lwis_i2c_device *i3c_proxy_dev;

	i3c_proxy_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	if (force_i2c_mode == 0 && i3c_proxy_dev->i3c_enabled) {
		if (i3c_proxy_dev->i3c != NULL) {
			if (i3c_proxy_dev->ibi_enabled) {
				i3c_device_disable_ibi(i3c_proxy_dev->i3c);
				i3c_device_free_ibi(i3c_proxy_dev->i3c);
				i3c_proxy_dev->ibi_enabled = false;
			}
			i3c_proxy_dev->i3c = NULL;
		} else {
			dev_err(i3c_proxy_dev->base_dev.dev,
				"device suspend with i3c enabled failed.\n");
		}
	}

	return 0;
}

static int lwis_i3c_proxy_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	struct lwis_i2c_device *i3c_proxy_dev;

	i3c_proxy_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	if (!i3c_proxy_dev->i3c && (i3c_proxy_dev->i3c_enabled && force_i2c_mode == 0)) {
		pr_err("Cannot find I3C instance\n");
		return -ECANCELED;
	}

	/* Running in interrupt context is not supported as i3c driver might sleep */
	if (in_interrupt())
		return -EAGAIN;

	lwis_save_register_io_info(lwis_dev, entry);

	if (entry->type == LWIS_IO_ENTRY_MODIFY)
		return (force_i2c_mode == 0 && i3c_proxy_dev->i3c_enabled) ?
			       lwis_i3c_io_entry_mod(i3c_proxy_dev, entry) :
			       lwis_i2c_io_entry_mod(i3c_proxy_dev, entry);

	return (force_i2c_mode == 0 && i3c_proxy_dev->i3c_enabled) ?
		       lwis_i3c_io_entry_rw(i3c_proxy_dev, entry) :
		       lwis_i2c_io_entry_rw(i3c_proxy_dev, entry);
}

static int lwis_i3c_proxy_batch_register_io(struct lwis_device *lwis_dev,
					    struct lwis_io_entry *entries, int batch_size)
{
	struct lwis_i2c_device *i3c_proxy_dev;
	int i;

	i3c_proxy_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

	if (!i3c_proxy_dev->i3c && (i3c_proxy_dev->i3c_enabled && force_i2c_mode == 0)) {
		pr_err("Cannot find I3C instance\n");
		return -ENODEV;
	}

	/* Running in interrupt context is not supported as i3c driver might sleep */
	if (in_interrupt())
		return -EAGAIN;

	for (i = 0; i < batch_size; i++)
		lwis_save_register_io_info(lwis_dev, &entries[i]);

	return (force_i2c_mode == 0 && i3c_proxy_dev->i3c_enabled) ?
		       lwis_i3c_io_entries_rw(i3c_proxy_dev, entries, batch_size) :
		       lwis_i2c_io_entries_rw(i3c_proxy_dev, entries, batch_size);
}

static int lwis_i3c_proxy_device_probe(struct platform_device *plat_dev)
{
	int ret = 0;
	struct lwis_i2c_device *i3c_proxy_dev;
	struct device *dev = &plat_dev->dev;

	/* Allocate I2C device specific data construct */
	i3c_proxy_dev = devm_kzalloc(dev, sizeof(struct lwis_i2c_device), GFP_KERNEL);
	if (!i3c_proxy_dev)
		return -ENOMEM;

	i3c_proxy_dev->base_dev.type = DEVICE_TYPE_I2C;
	i3c_proxy_dev->base_dev.vops = i3c_vops;
	i3c_proxy_dev->base_dev.plat_dev = plat_dev;
	i3c_proxy_dev->base_dev.k_dev = dev;

	/* Call the base device probe function */
	ret = lwis_base_probe(&i3c_proxy_dev->base_dev);
	if (ret) {
		dev_err_probe(dev, ret, "Error in lwis base probe\n");
		return ret;
	}
	platform_set_drvdata(plat_dev, &i3c_proxy_dev->base_dev);

	ret = lwis_i3c_proxy_device_parse_dt(i3c_proxy_dev);
	if (ret) {
		dev_err_probe(i3c_proxy_dev->base_dev.dev, ret, "Failed to parse device tree\n");
		lwis_base_unprobe(&i3c_proxy_dev->base_dev);
		return ret;
	}

	/* Call I2C device specific setup function */
	ret = lwis_i2c_device_setup(i3c_proxy_dev);
	if (ret) {
		dev_err_probe(i3c_proxy_dev->base_dev.dev, ret,
			      "Error in i2c device initialization\n");
		lwis_base_unprobe(&i3c_proxy_dev->base_dev);
		return ret;
	}

	if (!lwis_platform_is_batch_register_io_supported())
		i3c_proxy_dev->base_dev.vops.batch_register_io = NULL;

	/* Create I2C Bus Manager */
	ret = lwis_bus_manager_create(&i3c_proxy_dev->base_dev);
	if (ret) {
		dev_err_probe(i3c_proxy_dev->base_dev.dev, ret,
			      "Error in i2c bus manager creation\n");
		lwis_base_unprobe(&i3c_proxy_dev->base_dev);
		return ret;
	}

	dev_info(i3c_proxy_dev->base_dev.dev, "I3C Proxy Device Probe: Success\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_I3C_PROXY_DEVICE_COMPAT },
	{},
};
// MODULE_DEVICE_TABLE(of, lwis_id_match);

static struct platform_driver lwis_i3c_proxy_driver = {
	.probe = lwis_i3c_proxy_device_probe,
	.driver = {
			.name = LWIS_DRIVER_NAME,
			.owner = THIS_MODULE,
			.of_match_table = lwis_id_match,
		},
};
#else /* CONFIG_OF not defined */
static struct i2c_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(i2c, lwis_driver_id);

static struct platform_driver lwis_i3c_proxy_driver = { .probe = lwis_i3c_proxy_device_probe,
							.id_table = lwis_driver_id,
							.driver = {
								.name = LWIS_DRIVER_NAME,
								.owner = THIS_MODULE,
							} };
#endif /* CONFIG_OF */

int __init lwis_i3c_proxy_device_init(void)
{
	int ret = 0;

	pr_info("I3c proxy device initialization\n");

	ret = platform_driver_register(&lwis_i3c_proxy_driver);
	if (ret)
		pr_err("platform_driver_register failed: %d\n", ret);

	return ret;
}

int lwis_i3c_proxy_device_deinit(void)
{
	platform_driver_unregister(&lwis_i3c_proxy_driver);
	return 0;
}
