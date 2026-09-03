// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Google Inc.
 *
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

// NOA md wrapper related headers
#include "noa_md_wrapper.h"
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
#include "noa_md_wrapper_t900.h"
#endif

#include "noa_md.h"
#include "noa_md_trace.h"

#define NOA_MD_WPR_DEVICE_NAME "noa_md_wpr"

// Declare the noa wwan device entity
struct noa_md_wpr_dev *md_wpr_dev = NULL;

// PM OPS implement
struct noa_md_wpr_pm_ops noa_md_wpr_pm_ops_imp = {
	.suspend = noa_md_wpr_pm_suspend,
	.suspend_late = noa_md_wpr_pm_suspend_late,
	.resume_early = noa_md_wpr_pm_resume_early,
	.resume = noa_md_wpr_pm_resume
};

// Initialization function
int noa_md_wpr_module_init(void *data)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	// Call the underlying modem's (e.g., T900) initialization function
	int ret = md_wpr_dev->ops->init(data);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
		return ret; // Propagate the error from the underlying modem's init
	}
	NOA_MD_WPR_INFO("initialized");
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_module_init);

// Exit function
int noa_md_wpr_module_exit(void *data)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	// Call the underlying modem's exit function
	int ret = md_wpr_dev->ops->exit(data);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	NOA_MD_WPR_INFO("exit");
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_module_exit);

// Update network device information
int noa_md_wpr_update_netdev(struct net_device_info *info)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	// Call the underlying modem's function to update netdev info
	int ret = md_wpr_dev->ops->update_netdev(info);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	NOA_MD_WPR_INFO("net device updated");
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_update_netdev);

// Start queues
int noa_md_wpr_start_queue(enum netdev_drv_dir dir)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	// Call the underlying modem's function to start the queue
	int ret = md_wpr_dev->ops->start_queue(dir);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	NOA_MD_WPR_INFO("queue started, dir: %d", dir);
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_start_queue);

// Stop queues
int noa_md_wpr_stop_queue(enum netdev_drv_dir dir)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	// Call the underlying modem's function to stop the queue
	int ret = md_wpr_dev->ops->stop_queue(dir);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	NOA_MD_WPR_INFO("queue stopped, dir: %d", dir);
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_stop_queue);

// Transmit TX data
int noa_md_wpr_tx_data(void *dcb, struct sk_buff *skb, u64 data)
{
	ENSURE_NOA_ENABLE(ENODEV);
	ENSURE_NOA_MD_WPR_READY(ENODEV);
	ENSURE_NOA_MD_WPR_ENABLE(ENODEV);

	// Call the underlying modem's TX data function
	NOA_MD_WRAPPER_INFO_LIMIT(
		"dpmaif_dcb=[0x%p]", dcb);
	int ret = md_wpr_dev->ops->tx_data(dcb, skb, data);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	return ret;
}
EXPORT_SYMBOL(noa_md_wpr_tx_data);

// Receive RX data
int noa_md_wpr_rx_data(void *data)
{
	ENSURE_NOA_ENABLE(ENODEV);
	ENSURE_NOA_MD_WPR_READY(ENODEV);
	ENSURE_NOA_MD_WPR_ENABLE(ENODEV);

	// Receive RX data
	NOA_MD_WRAPPER_INFO_LIMIT("data received");
	int ret = md_wpr_dev->ops->rx_data(data);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_rx_data);

// Feature command handler
int noa_md_wpr_feature_cmd(enum noa_md_wpr_drv_cmd cmd, void *data)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	switch (cmd) {
		case MOA_MD_WPR_CUSTOM_FEATURE: {
			struct noa_md_wpr_custom_cmd* custom_cmd = data;
			// Call the underlying modem's feature command handler
			int ret = md_wpr_dev->ops->feature_cmd(
				custom_cmd->cmd, custom_cmd->data);
			if (ret != 0) {
				NOA_MD_WPR_ERROR("ret:%d", ret);
			}
			break;
		}
		case MOA_MD_WPR_CUSTOM_WWAN_NOTIFY: {
			ENSURE_NOA_MD_WPR_ENABLE(ENODEV);
			struct noa_md_wpr_custom_cmd* custom_cmd = data;
			void *evt_dat = custom_cmd->data;
			int ret = md_wpr_dev->ops->wwan_notify(evt_dat);
			if (ret != 0) {
				NOA_MD_WPR_ERROR("ret:%d", ret);
			}
			break;
		}
		default: {
			NOA_MD_WPR_ERROR("unsupported feature command %d", cmd);
			break;
		}
	}
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_feature_cmd);

// Feature command handler
int noa_md_wpr_custom_feature_cmd(int cmd, void *data)
{
	struct noa_md_wpr_custom_cmd custom_cmd = {
		.cmd = cmd,
		.data = data
	};
	int ret = noa_md_wpr_feature_cmd(
		MOA_MD_WPR_CUSTOM_FEATURE, (void *)&custom_cmd);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret:%d", ret);
	}
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_custom_feature_cmd);

int noa_md_wpr_custom_data_event(void *evt_dat)
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);
	ENSURE_NOA_MD_WPR_ENABLE(ENODEV);

	if (!evt_dat) {
		NOA_MD_WPR_ERROR("evt_dat is null");
		return -ENODEV;
	}

	struct noa_md_wpr_custom_cmd custom_cmd = {
		.cmd = MOA_MD_WPR_CUSTOM_WWAN_NOTIFY,
		.data = (void*)evt_dat
	};

	int ret = noa_md_wpr_feature_cmd(
		MOA_MD_WPR_CUSTOM_WWAN_NOTIFY, (void *)&custom_cmd);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_custom_data_event);

bool noa_md_wpr_is_noa_enable(void)
{
	return md_dev.feature_ctrl.enabled;
}
EXPORT_SYMBOL(noa_md_wpr_is_noa_enable);

bool noa_md_wpr_is_noa_tx_enable(void)
{
	struct noa_md_feature_ctrl *feature_ctrl = &md_dev.feature_ctrl;
	return feature_ctrl->enabled && feature_ctrl->tx_enabled;
}
EXPORT_SYMBOL(noa_md_wpr_is_noa_tx_enable);

bool noa_md_wpr_is_noa_rx_enable(void)
{
	struct noa_md_feature_ctrl *feature_ctrl = &md_dev.feature_ctrl;
	return feature_ctrl->enabled && feature_ctrl->rx_enabled;
}
EXPORT_SYMBOL(noa_md_wpr_is_noa_rx_enable);

bool noa_md_wpr_is_noa_unified_desc_enable(void)
{
	struct noa_md_feature_ctrl *feature_ctrl = &md_dev.feature_ctrl;
	return feature_ctrl->enabled && feature_ctrl->unified_desc_enabled;
}
EXPORT_SYMBOL(noa_md_wpr_is_noa_unified_desc_enable);

int noa_md_wpr_pm_suspend(void *data)
{
	return noa_md_pm_suspend(data);
}

int noa_md_wpr_pm_suspend_late(void *data)
{
	return noa_md_pm_suspend_late(data);
}

int noa_md_wpr_pm_resume_early(void *data)
{
	return noa_md_pm_resume_early(data);
}

int noa_md_wpr_pm_resume(void *data)
{
	return noa_md_pm_resume(data);
}

// Handle data interrupts
int noa_md_wpr_data_irq_handle(void *data)
{
	ENSURE_NOA_ENABLE(ENODEV);
	ENSURE_NOA_MD_WPR_READY(ENODEV);
	ENSURE_NOA_MD_WPR_ENABLE(ENODEV);
	// NOA md data rx step 2: Call the data_irq_handle function of the
	// underlying layer (E.g. T900) to handle the interrupt
	NOA_MD_WPR_DATA_LIMIT("noa_md_wpr_data_irq_handle");
	int ret = md_wpr_dev->ops->data_irq_handle(data);
	if (ret != 0) {
		NOA_MD_WPR_ERROR("ret=[%d]", ret);
	}
	return ret;
}
EXPORT_SYMBOL(noa_md_wpr_data_irq_handle);

// Set the DPMAIF event IRQ handler function
int noa_md_wpr_set_modem_irq_handler(int (*noa_md_data_irq_handle_ptr)(void*))
{
	ENSURE_NOA_MD_WPR_READY(ENODEV);

	md_wpr_dev->ops->modem_event_irq_handle_func =
		noa_md_data_irq_handle_ptr;

	return 0;
}

static ssize_t noa_md_wpr_enable_show(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	ENSURE_NOA_MD_WPR_READY(0);
	return sprintf(buf, "%d\n", md_wpr_dev->enabled ? 1 : 0);
}

static ssize_t noa_md_wpr_enable_store(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf, size_t count)
{
	ENSURE_NOA_MD_WPR_READY(0);
	int val;

	if (kstrtoint(buf, 0, &val) != 0 || val < 0 || val > 1)
		return -EINVAL;

	md_wpr_dev->enabled = val;
	return count;
}

static DEVICE_ATTR(
	enable, 0644, noa_md_wpr_enable_show, noa_md_wpr_enable_store);

// Initializes the NOA modem wrapper device.
int noa_md_wpr_init(void)
{
	struct noa_md_wpr_dev *dev = NULL;
	int ret = 0;
	// Alloc memory for noa_md_wpr_dev
	dev = kzalloc(sizeof(struct noa_md_wpr_dev), GFP_KERNEL);
	if (!dev) {
		NOA_MD_WPR_ERROR("Failed to allocate memory for wrapper device");
		return ret;
	}

	// Initialize device
	ret = alloc_chrdev_region(&dev->dev_num, 0, 1, NOA_MD_WPR_DEVICE_NAME);
	if (ret < 0) {
		NOA_MD_WPR_ERROR(
			"Failed to allocate major number for NOA modem device");
		return ret;
	}

	dev->cls = class_create(NOA_MD_WPR_DEVICE_NAME);
	if (IS_ERR(dev->cls)) {
		NOA_MD_WPR_ERROR("Failed to create NOA modem device class");
		unregister_chrdev_region(dev->dev_num, 1);
		return PTR_ERR(dev->cls);
	}

	dev->dev = device_create(
		dev->cls, NULL, dev->dev_num, NULL, NOA_MD_WPR_DEVICE_NAME "_dev");
	if (IS_ERR(dev->dev)) {
		NOA_MD_WPR_ERROR("Failed to create NOA modem device");
		class_destroy(dev->cls);
		unregister_chrdev_region(dev->dev_num, 1);
		return PTR_ERR(dev->dev);
	}

	// Handle T900 IRQ, this feature is only available for simulators
	// using only one thread is more consistent with the ISR behavior
	dev->noa_md_tx_event_workqueue = alloc_workqueue(
		"noa_md_tx_t900_irq_handle_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	dev->noa_md_rx_event_workqueue = alloc_workqueue(
		"noa_md_rx_t900_irq_handle_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);

	//  Initialize members of Full SoC or T900 wrapper
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
	dev->enabled = 1;
#elif IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
	noa_md_wpr_mtk_t900_init();
	dev->ops = noa_md_wpr_t900_get_drv_ops();
	dev->enabled = 1;
#endif

	dev->pm_ops = &noa_md_wpr_pm_ops_imp;

	ret = device_create_file(dev->dev, &dev_attr_enable);
	if (ret != 0) {
		NOA_MD_WPR_INFO("device_create_file failure, ret=[%d]", ret);
	}

	md_wpr_dev = dev;

	noa_md_wpr_module_init(NULL);

	NOA_MD_WPR_INFO("wrapper device created");
	return 0;
}

// Cleans up the NOA modem wrapper device.
void noa_md_wpr_exit(void)
{
	NOA_MD_WPR_INFO("enter");

	if (!md_wpr_dev) {
		NOA_MD_WPR_ERROR("invalid wrapper device pointer");
		return;
	}

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
	md_wpr_dev->enabled = 0;
#elif IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
	md_wpr_dev->enabled = 0;
	noa_md_wpr_mtk_t900_exit();
#endif

	destroy_workqueue(md_wpr_dev->noa_md_tx_event_workqueue);
	destroy_workqueue(md_wpr_dev->noa_md_rx_event_workqueue);

	noa_md_wpr_module_exit(NULL);

	// Release noa_md_wpr_dev memory
	device_remove_file(md_wpr_dev->dev, &dev_attr_enable);
	device_destroy(md_wpr_dev->cls, md_wpr_dev->dev_num);
	class_destroy(md_wpr_dev->cls);
	unregister_chrdev_region(md_wpr_dev->dev_num, 1);
	kfree(md_wpr_dev);
	md_wpr_dev = NULL;

	NOA_MD_WPR_INFO("wrapper device destroyed");
}
