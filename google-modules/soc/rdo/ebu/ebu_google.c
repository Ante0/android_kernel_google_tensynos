// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * Platform driver for Google's EBU IP
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <ebu/ebu.h>
#include "ebu_fw.h"
#include "ebu_google.h"
#include "ebu_platform.h"
#include <soc/google/goog-mba-ebu-iface.h>
#include <soc/google/goog_ebu_service_ids.h>

#define GOOGLE_EBU_DEFAULT_M0P_FIRMWARE_NAME "ebu_m0plus.bin"

u32 serial_data[4] = { 0 };
enum usb_device_speed ebu_default_speed = USB_SPEED_SUPER_PLUS;

uint32_t ebu_readl(struct google_ebu *gebu, uint32_t offset)
{
	return readl(gebu->csr_base + offset);
}

void ebu_writel(struct google_ebu *gebu, uint32_t offset, uint32_t value)
{
	writel(value, gebu->csr_base + offset);
}

#define NIBBLE_POS(idx) (4 * (idx))
#define NIBBLE_MASK(idx) ((uint64_t)0xf << NIBBLE_POS(idx))
#define GET_NIBBLE(dword, idx) (((dword) >> NIBBLE_POS(idx)) & 0xf)

void stop_ebu(struct google_ebu *gebu)
{
	gebu->state = EBU_STOPPED;
}

int start_ebu(struct google_ebu *gebu)
{
	gebu->state = EBU_RUNNING;
	return 0;
}

int google_ebu_add_mapping(struct ebu_controller *ebu, u8 channel, u8 epaddr)
{
	int ret = 0;
	u8 dir = 0;
	u8 prev_channel, prev_epnum;
	u8 epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	if (epaddr & USB_DIR_IN)
		dir = 1;

	if (channel >= MAX_CHANNELS) {
		dev_err(gebu->dev, "Channel out of range\n");
		return -EINVAL;
	}

	mutex_lock(&gebu->lock);
	if (gebu->ch_use[dir] & (0x1 << channel)) {
		dev_err(gebu->dev, "Channel already assigned\n");
		ret = -EBUSY;
		goto unlock;
	}

	prev_epnum = GET_NIBBLE(gebu->ch_map[dir], channel);
	prev_channel = GET_NIBBLE(gebu->ep_map[dir], epnum);

	dev_dbg(gebu->dev,
		"epnum: %d, prev_epnum: %d, channel: %d, prev_channel: %d dir: %d\n",
		epnum, prev_epnum, channel, prev_channel, dir);

	/* All the LOW/HIGH BIT_<index> should be programmed to unique value
	 * To maintain this, we need to swap the nibble corresponding to epnum
	 * with the nibble corresponding to the ep where channel was prev mapped
	 */
	gebu->ep_map[dir] &= ~(NIBBLE_MASK(epnum) | NIBBLE_MASK(prev_epnum));
	gebu->ep_map[dir] |= ((uint64_t)channel << NIBBLE_POS(epnum)) |
			     ((uint64_t)prev_channel << NIBBLE_POS(prev_epnum));

	/* Maintain channel-ep (reverse) map to avoid a linear search */
	// gebu->ch_map &= ~(NIBBLE_MASK(channel) | NIBBLE_MASK(prev_channel));
	gebu->ch_map[dir] &=
		~(NIBBLE_MASK(channel) | NIBBLE_MASK(prev_channel));
	gebu->ch_map[dir] |= ((uint64_t)epnum << NIBBLE_POS(channel)) |
			     ((uint64_t)prev_epnum << NIBBLE_POS(prev_channel));

	gebu->ch_use[dir] |= (0x1 << channel);
unlock:
	mutex_unlock(&gebu->lock);
	return ret;
}

int google_ebu_release_mapping(struct ebu_controller *ebu, u8 channel)
{
	int ret = 0;
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	if (channel >= MAX_CHANNELS) {
		dev_err(gebu->dev, "Channel out of range\n");
		return -EINVAL;
	}

	mutex_lock(&gebu->lock);
	if ((((gebu->ch_use[0] >> channel) & 1) == 0) &&
	    (((gebu->ch_use[1] >> channel) & 1) == 0)) {
		ret = -EINVAL;
		goto unlock;
	}

	gebu->ch_use[0] &= ~(0x1 << channel);
	gebu->ch_use[1] &= ~(0x1 << channel);
unlock:
	mutex_unlock(&gebu->lock);
	return ret;
}

int google_ebu_enable_data(struct ebu_controller *ebu,
			   enum usb_device_speed speed)
{
	int ret;
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	mutex_lock(&gebu->lock);
	switch (speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
	case USB_SPEED_HIGH:
		break;
	default:
		/* EBU Only supports HS, SS and SSP */
		ret = -EINVAL;
		goto unlock;
	}
	pm_runtime_get_sync(gebu->dev);
	ret = gebu->drv_data->reinit_ebu(gebu, speed);
unlock:
	mutex_unlock(&gebu->lock);
	return ret;
}

void google_ebu_disable_data(struct ebu_controller *ebu)
{
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	pm_runtime_put(gebu->dev);
}

dma_addr_t google_ebu_get_fifo(struct ebu_controller *ebu, u8 channel)
{
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	switch (channel) {
	case EBU_TRACE_CHANNEL:
		return gebu->trace_fifo_base;
	case EBU_UD_CHANNEL:
		return gebu->ud_fifo_base;
	default:
		dev_err(gebu->dev, "Invalid Channel for get_fifo\n");
		return -EINVAL;
	}
}

static ssize_t amb_debug_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct ebu_controller *ebu = dev_get_drvdata(dev);
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	if (gebu->amb_debug_enabled)
		return sysfs_emit(buf, "%s\n", "enabled");
	else
		return sysfs_emit(buf, "%s\n", "disabled");
}

enum ebu_usb_message_type {
	kUsbMessageSerialNumberLow = 0,
	kUsbMessageSerialNumberHigh = 1,
	kUsbMessageUsbStop = 2,
	kUsbMessageUsbResumePostPowerOn = 3,
};

int ebu_send_usb_service_request(struct google_ebu *gebu, void *mba_client,
				 enum ebu_usb_message_type type, uint32_t data1,
				 uint32_t data2)
{
	int ret = 0;
	struct ebu_iface_payload request = {
		0,
	};
	struct ebu_iface_payload response = {
		0,
	};

	request.data[0] = type;
	request.data[1] = data1;
	request.data[2] = data2;
	struct ebu_iface_message message = {
		.dst_service_id = EBU_MBA_SERVICE_ID_USB,
		.request = &request,
		.response = &response,
	};
	ret = ebu_send_request(mba_client, &message);
	if (ret)
		dev_err(gebu->dev, "Failed to send USB service: %d\n", ret);
	return ret;
}

static ssize_t amb_debug_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t n)
{
	struct ebu_controller *ebu = dev_get_drvdata(dev);
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);
	int ret = 0;

	if (sysfs_streq(buf, "enable") && !gebu->amb_debug_enabled) {
		pm_runtime_get_sync(gebu->dev);
		ret = phy_init(gebu->u2_phy);
		if (ret) {
			dev_err(gebu->dev, "u2phy init failed:%d\n", ret);
			return ret;
		}
		ret = phy_init(gebu->u3_phy);
		if (ret) {
			dev_err(gebu->dev, "u3phy init failed:%d\n", ret);
			return ret;
		}

		// Don't let it power down in ambient
		ret = device_wakeup_enable(gebu->dev);
		if (ret) {
			dev_err(gebu->dev, "Failed to enable wakeup, err: %d\n",
				ret);
			return ret;
		}

		ret = ebu_send_usb_service_request(gebu, gebu->mba_client,
						   kUsbMessageSerialNumberLow,
						   serial_data[0],
						   serial_data[1]);
		if (ret) {
			dev_err(gebu->dev,
				"Failed to send USB serial service: %d\n", ret);
			return ret;
		}
		ret = ebu_send_usb_service_request(gebu, gebu->mba_client,
						   kUsbMessageSerialNumberHigh,
						   serial_data[2],
						   serial_data[3]);
		if (ret) {
			dev_err(gebu->dev,
				"Failed to send USB serial service: %d\n", ret);
			return ret;
		}
		// Phy power on must be called after SUSPENDABLE bit is set
		ret = phy_power_on(gebu->u2_phy);
		if (ret) {
			dev_err(gebu->dev, "u2phy power on failed:%d\n", ret);
			return ret;
		}
		ret = phy_power_on(gebu->u3_phy);
		if (ret) {
			dev_err(gebu->dev, "u3phy power on failed:%d\n", ret);
			return ret;
		}
		ret = ebu_send_usb_service_request(gebu, gebu->mba_client,
						   kUsbMessageUsbResumePostPowerOn,
						   ebu_default_speed, 0);
		if (ret) {
			dev_err(gebu->dev,
				"Failed to send USB resume service: %d\n", ret);
			return ret;
		}
		gebu->amb_debug_enabled = true;
	} else if (strncmp(buf, "serialno ", 9) == 0) {
		const char *serial_str = buf + 9;
		size_t len = n - 9;

		/* Strip trailing newline if present */
		if (len > 0 && buf[n - 1] == '\n')
			len--;

		if (len > sizeof(serial_data)) {
			dev_warn(gebu->dev,
				 "Serial number too long (%zu bytes), max %zu\n",
				 len, sizeof(serial_data));
			return -EINVAL;
		}

		memcpy(serial_data, serial_str, len);

		dev_info(gebu->dev, "Parsed serial: %08x %08x %08x %08x\n",
			 serial_data[0], serial_data[1], serial_data[2],
			 serial_data[3]);

	} else if (strncmp(buf, "speed ", 6) == 0) {
		const char *speed_str = buf + 6;

		if (strncmp(speed_str, "super-speed-plus", 16) == 0)
			ebu_default_speed = USB_SPEED_SUPER_PLUS;
		else if (strncmp(speed_str, "super-speed", 11) == 0)
			ebu_default_speed = USB_SPEED_SUPER;
		else if (strncmp(speed_str, "high-speed", 10) == 0)
			ebu_default_speed = USB_SPEED_HIGH;
		else
			dev_err(gebu->dev, "Invalid Speed %s\n", speed_str);

		dev_dbg(gebu->dev, "USB default Speed %d", ebu_default_speed);

	} else if (sysfs_streq(buf, "disable") && gebu->amb_debug_enabled) {
		ret = ebu_send_usb_service_request(gebu, gebu->mba_client,
						   kUsbMessageUsbStop, 0, 0);
		if (ret) {
			dev_err(gebu->dev, "Failed to send USB service\n");
			return ret;
		}
		gebu->amb_debug_enabled = false;
		device_wakeup_disable(gebu->dev);
		phy_power_off(gebu->u2_phy);
		phy_power_off(gebu->u3_phy);
		phy_exit(gebu->u2_phy);
		phy_exit(gebu->u3_phy);
		pm_runtime_put(gebu->dev);
	} else {
		return -EINVAL;
	}

	return n;
}
static DEVICE_ATTR_RW(amb_debug);

static struct attribute *ebu_google_attrs[] = {
	&dev_attr_amb_debug.attr,
	NULL
};
ATTRIBUTE_GROUPS(ebu_google);

static int goog_ebu_clk_init_ebu_mailbox(struct google_ebu *gebu)
{
	int ret = 0;

	gebu->mba_client = ebu_iface_get(gebu->dev);
	if (IS_ERR(gebu->mba_client)) {
		ret = PTR_ERR(gebu->mba_client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(gebu->dev,
				"ebu interface not ready. Try again later\n");
		else
			dev_err(gebu->dev,
				"Failed to request ebu client ret %d\n", ret);
	}
	return ret;
}

static int google_ebu_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct google_ebu *gebu;

	dev_dbg(dev, "google_ebu probe\n");
	gebu = devm_kzalloc(dev, sizeof(*gebu), GFP_KERNEL);
	if (!gebu)
		return -ENOMEM;
	gebu->drv_data = of_device_get_match_data(dev);
	gebu->dev = dev;

	ret = gebu->drv_data->ebu_setup(gebu, pdev);
	if (ret) {
		dev_err(gebu->dev, "EBU setup failed\n");
		return ret;
	}

	gebu->u2_phy = devm_phy_get(gebu->dev, "usb2-phy");
	if (IS_ERR(gebu->u2_phy)) {
		ret = PTR_ERR(gebu->u2_phy);
		if (ret == -ENODEV) {
			gebu->u2_phy = NULL;
			dev_warn(gebu->dev, "No u2 phy\n");
		} else {
			return dev_err_probe(dev, ret,
					     "no u2 phy configured\n");
		}
	}

	gebu->u3_phy = devm_phy_get(gebu->dev, "usb3-phy");
	if (IS_ERR(gebu->u3_phy)) {
		ret = PTR_ERR(gebu->u3_phy);
		if (ret == -ENODEV) {
			gebu->u3_phy = NULL;
			dev_warn(gebu->dev, "No u3 phy\n");
		} else {
			return dev_err_probe(dev, ret,
					     "no u3 phy configured\n");
		}
	}
	goog_ebu_clk_init_ebu_mailbox(gebu);

	device_set_wakeup_capable(gebu->dev, true);

	ret = of_property_read_u32(gebu->dev->of_node, "tid", &gebu->trace_tid);
	if (ret) {
		dev_err(gebu->dev, "No TID specified for EBU\n");
		return ret;
	}

	gebu->ep_map[0] = 0x0123456789abcdef;
	gebu->ep_map[1] = 0x0123456789abcdef;
	gebu->ch_map[0] = 0x0123456789abcdef;
	gebu->ch_map[1] = 0x0123456789abcdef;
	gebu->ch_use[0] = 0x0;
	gebu->ch_use[1] = 0x0;

	gebu->ebu.add_mapping = google_ebu_add_mapping;
	gebu->ebu.release_mapping = google_ebu_release_mapping;
	gebu->ebu.get_fifo = google_ebu_get_fifo;
	gebu->ebu.enable_data = google_ebu_enable_data;
	gebu->ebu.disable_data = google_ebu_disable_data;
	mutex_init(&gebu->lock);
	platform_set_drvdata(pdev, &gebu->ebu);

	pm_runtime_enable(gebu->dev);

	return 0;
}

static void google_ebu_remove(struct platform_device *pdev)
{
	struct ebu_controller *ebu = platform_get_drvdata(pdev);
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	stop_ebu(gebu);
	device_set_wakeup_capable(gebu->dev, false);
	mutex_destroy(&gebu->lock);
}

static int ebu_google_suspend(struct device *dev)
{
	struct ebu_controller *ebu = dev_get_drvdata(dev);
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	dev_dbg(dev, "RT suspend\n");
	stop_ebu(gebu);
	if (!gebu->amb_debug_enabled)
		stop_firmware(gebu);

	return 0;
}

static int ebu_google_resume(struct device *dev)
{
	struct ebu_controller *ebu = dev_get_drvdata(dev);
	struct google_ebu *gebu = container_of(ebu, struct google_ebu, ebu);

	dev_dbg(dev, "RT resume\n");
	if (!gebu->amb_debug_enabled)
		load_and_release_firmware(gebu, GOOGLE_EBU_DEFAULT_M0P_FIRMWARE_NAME);
	start_ebu(gebu);

	return 0;
}

static const struct dev_pm_ops ebu_google_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(ebu_google_suspend, ebu_google_resume, NULL)
};

static const struct ebu_google_driverdata lga_drvdata = {
	.ebu_setup = lga_google_ebu_setup,
	.reinit_ebu = lga_reinit_ebu,
};

static const struct ebu_google_driverdata mbu_drvdata = {
	.ebu_setup = mbu_google_ebu_setup,
	.reinit_ebu = mbu_reinit_ebu,
};

static const struct of_device_id google_ebu_match[] = {
	{
		.compatible = "google,ebu",
		.data = &lga_drvdata,
	},
	{
		.compatible = "google,ebu-mbu",
		.data = &mbu_drvdata,
	},
	{},
};

static struct platform_driver google_ebu_driver = {
	.probe = google_ebu_probe,
	.remove = google_ebu_remove,
	.driver = {
		.name = "google-ebu",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&ebu_google_dev_pm_ops),
		.of_match_table = google_ebu_match,
		.dev_groups = ebu_google_groups,
	}
};

module_platform_driver(google_ebu_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Driver for Google's EBU IP.");
MODULE_LICENSE("GPL");
