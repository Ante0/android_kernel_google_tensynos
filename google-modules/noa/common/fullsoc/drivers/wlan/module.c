// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM WLAN Module Loader
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file handles the WLAN driver's probe and remove functions,
 * which are called when the driver is loaded and unloaded, respectively.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <core/print.h>
#include <dal/wlan/api.h>
#include "module.h"
#include "mcu.h"
#include "topbank.h"

struct reg_entry {
	const char *name;
	u32 reg;
};

const char * const drv_state_str[] = {
	[DRV_STATE_BYPASS_MODE]		= "Bypass Mode",
	[DRV_STATE_NOA_MODE]		= "NOA Mode",
};

/**
 * lvm_wlan_probe - Probe function for the WLAN driver
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function is called when the WLAN driver is being probed. It initializes
 * the WLAN driver main data structure and buffers. It also initializes
 * the platform bus operation through the DAL layer.
 *
 * Return: 0 on success, negative error code otherwise
 */
static int lvm_wlan_probe(struct lvm_platform_device *pdev)
{
	struct wlan_data *data;
	int ret = 0;

	if (!pdev)
		return -EINVAL;

	/* Initialize the WLAN driver main data structure */
	ret = lvm_wlan_mcu_init(pdev);
	if (ret) {
		LVM_ERR("LVM wlan mcu init failed: %d\n", ret);
		return ret;
	}

	/* Initialize the WLAN RXBM and TXBM */
	ret = lvm_wlan_buffer_init(pdev);
	if (ret) {
		LVM_ERR("LVM wlan buffer init failed: %d\n", ret);
		goto err_mcu;
	}

	data = dev_get_drvdata(pdev->dev);
	if (!data)
		goto err_buffer;

	ret = lvm_wlan_topbank_init(data);
	if (ret) {
		LVM_ERR("LVM wlan topbank init failed: %d\n", ret);
		goto err_buffer;
	}

	ret = lvm_wlan_dal_init(data);
	if (ret) {
		LVM_ERR("LVM wlan dal init failed: %d\n", ret);
		goto err_topbank;
	}

	ret = lvm_wlan_irq_init(data);
	if (ret) {
		LVM_ERR("LVM wlan irq init failed: %d\n", ret);
		goto err_dal;
	}

	return 0;

err_dal:
	lvm_wlan_dal_deinit(data);

err_topbank:
	lvm_wlan_topbank_deinit(data);

err_buffer:
	lvm_wlan_buffer_deinit(pdev);

err_mcu:
	lvm_wlan_mcu_deinit(pdev);

	return ret;
}

/**
 * lvm_wlan_remove - Remove function for the WLAN driver
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function is called when the WLAN driver is being removed. It
 * deinitializes the WLAN RXBM/TXBM and main data structure.
 *
 * Return: 0 on success, negative error code otherwise
 */
static int lvm_wlan_remove(struct lvm_platform_device *pdev)
{
	struct wlan_data *data;

	if (!pdev)
		return -EINVAL;

	data = dev_get_drvdata(pdev->dev);
	if (!data)
		return -EINVAL;

	lvm_wlan_irq_deinit(data);

	lvm_wlan_dal_deinit(data);

	lvm_wlan_topbank_deinit(data);

	lvm_wlan_buffer_deinit(pdev);

	lvm_wlan_mcu_deinit(pdev);

	return 0;
}

/**
 * lvm_wlan_get_state - Get the current state of the WLAN driver
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function retrieves the current state of the WLAN driver.
 *
 * Return: A string representation of the current driver state.
 */
static const char *lvm_wlan_get_state(struct lvm_platform_driver *driver)
{
	enum drv_state cur_state;

	if (!driver)
		return "Unknown state";

	cur_state = driver->state;
	if (cur_state >= __DRV_STATE_MAX)
		return "Unknown state";

	return drv_state_str[cur_state];
}

/**
 * lvm_wlan_switch_state - Switch the state of the WLAN driver
 * @driver: Pointer to the LVM platform driver structure
 * @new_state: The new state to switch to
 *
 * This function switches the state of the WLAN driver to the specified new state.
 *
 * Return: 0 on success, -EINVAL if the driver pointer is invalid or the new
 *         state is invalid.
 */
static int lvm_wlan_switch_state(struct lvm_platform_driver *driver,
				 int new_state)
{
	enum drv_state cur_state;

	if (!driver)
		return -EINVAL;

	if (new_state != DRV_STATE_BYPASS_MODE &&
	    new_state != DRV_STATE_NOA_MODE)
		return -EINVAL;

	cur_state = driver->state;
	if (cur_state == new_state)
		return 0;

	LVM_INFO("Driver state transited from \"%s\" to \"%s\"\n",
		 drv_state_str[cur_state], drv_state_str[new_state]);

	driver->state = new_state;

	return 0;
}

/**
 * lvm_wlan_dump_reg - Dump WLAN registers
 * @driver: Pointer to the LVM platform driver structure.
 * @buf: Pointer to the character buffer to store the register dump.
 *
 * This function dumps the WLAN topbank and ring registers into the
 * provided buffer. This is used for debugging purpose.
 *
 * Return: The total number of characters written to the buffer.
 */
static ssize_t lvm_wlan_dump_reg(struct lvm_platform_driver *driver, char *buf)
{
#define REG_INFO(reg) { #reg, reg }
#define LOG_FORMAT(entry, val)                                                                     \
	sprintf(buf + offset, "   %-30s [0x%02X]: 0x%08X\n", entry.name, entry.reg, val)
	struct wlan_data *data;
	int offset = 0;
	int i, j;

	const struct reg_entry tregs[] = {
		REG_INFO(WLAN_TOP_REG_DEV_ENABLE),   REG_INFO(WLAN_TOP_REG_DOORBELL_ENABLE),
		REG_INFO(WLAN_TOP_REG_DEV_ID),	     REG_INFO(WLAN_TOP_REG_IN_RING_NUM),
		REG_INFO(WLAN_TOP_REG_OUT_RING_NUM), REG_INFO(WLAN_TOP_REG_OUT_RING_ISR),
		REG_INFO(WLAN_TOP_REG_OUT_RING_IMR), REG_INFO(WLAN_TOP_REG_IN_RING_DOORBELL),
		REG_INFO(WLAN_TOP_REG_MIB_TX_PKT),   REG_INFO(WLAN_TOP_REG_MIB_TX_BYTE),
		REG_INFO(WLAN_TOP_REG_MIB_RX_PKT),   REG_INFO(WLAN_TOP_REG_MIB_RX_BYTE),
		REG_INFO(WLAN_TOP_REG_MIB_TX_CPL),
	};

	const struct reg_entry rregs[] = {
		REG_INFO(WLAN_RING_REG_CTRL),	  REG_INFO(WLAN_RING_REG_INFO),
		REG_INFO(WLAN_RING_REG_DESCBASE), REG_INFO(WLAN_RING_REG_NUM),
		REG_INFO(WLAN_RING_REG_LEN),	  REG_INFO(WLAN_RING_REG_READ),
		REG_INFO(WLAN_RING_REG_WRITE),
	};

	if (!driver || !buf)
		return offset;

	data = dev_get_drvdata(driver->pdev->dev);
	if (!data)
		return offset;

	offset += sprintf(buf + offset, "Dump registers:\n");
	offset += sprintf(buf + offset, "===============================\n");
	offset += sprintf(buf + offset, "Topbank\n");

	for (i = 0; i < ARRAY_SIZE(tregs); i++)
		offset += LOG_FORMAT(tregs[i],
				     lvm_wlan_topbank_reg_read(data->topbank, tregs[i].reg));

	for (i = 0; i < __WLAN_RING_ID_MAX; i++) {
		offset += sprintf(buf + offset, "Ring %d - %s\n", i, data->ring[i]->name);

		for (j = 0; j < ARRAY_SIZE(rregs); j++)
			offset += LOG_FORMAT(rregs[j],
					     lvm_wlan_ring_reg_read(data->ring[i], rregs[j].reg));
	}

	offset += sprintf(buf + offset, "===============================\n");

	return offset;
}

struct lvm_platform_driver_ops wlan_driver_ops = {
	.probe = lvm_wlan_probe,
	.remove = lvm_wlan_remove,
	.get_state = lvm_wlan_get_state,
	.switch_state = lvm_wlan_switch_state,
	.dump_reg = lvm_wlan_dump_reg,
};

struct lvm_platform_driver lvm_wlan_driver = {
	.name = "wlan",
	.state = DRV_STATE_BYPASS_MODE,
	.ops = &wlan_driver_ops,
};

/**
 * lvm_wlan_module_init - Initialization function for the WLAN driver module
 *
 * This function is called when the WLAN driver module is loaded. It registers
 * the WLAN platform driver.
 *
 * Return: 0 on success, negative error code otherwise
 */
int lvm_wlan_module_init(void)
{
	int ret = 0;

	LVM_INFO("LVM wlan module init started\n");

	/* Register the WLAN platform driver */
	ret = lvm_platform_driver_register(&lvm_wlan_driver);
	if (ret)
		return ret;

	return 0;
}

/**
 * lvm_wlan_module_exit - Exit function for the WLAN driver module
 *
 * This function is called when the WLAN driver module is unloaded. It
 * unregisters the WLAN platform driver.
 */
void lvm_wlan_module_exit(void)
{
	/* Unregister the WLAN platform driver */
	lvm_platform_driver_unregister(&lvm_wlan_driver);

	LVM_INFO("LVM wlan module exited\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Yen <henryyen@google.com>");
MODULE_DESCRIPTION("LVM Mock WLAN Vendor Driver");
