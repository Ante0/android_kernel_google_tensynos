// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/panic.h>
#include <linux/pci.h>
#include <linux/pcie_google_if.h>
#include <linux/sysfs.h>

#include "mtk_fsm.h"
#include "pcie-config.h"
#include "radio-utils.h"
#include "link-exception.h"

static const char *const excp_reason_desc[] = {
#define TABLE_GEN(name, bit, desc) [bit] = desc,
	EXCP_REASONS_LIST(TABLE_GEN)
#undef TABLE_GEN
};

static bool panic_on_link_error = false;
module_param(panic_on_link_error, bool, 0644);
MODULE_PARM_DESC(panic_on_link_error, "Trigger panic on driver-detected link error");

static bool panic_on_surprise_down = false;
module_param(panic_on_surprise_down, bool, 0644);
MODULE_PARM_DESC(panic_on_surprise_down, "Trigger panic on surprise down");

static bool panic_on_cpl_timeout = false;
module_param(panic_on_cpl_timeout, bool, 0644);
MODULE_PARM_DESC(panic_on_cpl_timeout, "Trigger panic on completion timeout");

/*
 * This does not cover cases where surprise down or completion timeout are explicitly flagged in
 * the PCIe configuration space.
 */
static bool panic_on_uncor_error = false;
module_param(panic_on_uncor_error, bool, 0644);
MODULE_PARM_DESC(panic_on_uncor_error, "Trigger panic on generic uncorrectable error");

static bool enable_cor_error_handler = false;
module_param(enable_cor_error_handler, bool, 0644);
MODULE_PARM_DESC(enable_cor_error_handler, "Enable correctable error handler");

static bool panic_on_cor_error = false;
module_param(panic_on_cor_error, bool, 0644);
MODULE_PARM_DESC(panic_on_cor_error, "Trigger panic on correctable error");

static bool panic_on_cold_resume = false;
module_param(panic_on_cold_resume, bool, 0644);
MODULE_PARM_DESC(panic_on_cold_resume, "Trigger panic on cold resume");

static bool panic_on_link_unready = false;
module_param(panic_on_link_unready, bool, 0644);
MODULE_PARM_DESC(panic_on_link_unready, "Trigger panic on link unready");

static bool panic_on_suspend_timeout = false;
module_param(panic_on_suspend_timeout, bool, 0644);
MODULE_PARM_DESC(panic_on_suspend_timeout, "Trigger panic on suspend timeout");

static bool panic_on_resume_timeout = false;
module_param(panic_on_resume_timeout, bool, 0644);
MODULE_PARM_DESC(panic_on_resume_timeout, "Trigger panic on resume timeout");

static u32 hot_reset_delay_ms = 0;
module_param(hot_reset_delay_ms, uint, 0644);
MODULE_PARM_DESC(hot_reset_delay_ms, "Delay time before hot reset");

static void trigger_pcie_coredump(void)
{
	if (in_interrupt() || irqs_disabled() || in_atomic()) {
		LOG_WARN("Bypass PCIe coredump in atomic context\n");
		return;
	}

	google_pcie_dump_debug(GOOGLE_PCIE_PORT_NUM);
}

static void check_uncor_error_status(struct link_exception *link_exception)
{
	struct pci_dev *bridge;
	u32 uncor_status;
	int aer_cap;

	bridge = pci_upstream_bridge(to_pci_dev(link_exception->goog->mdev->dev));
	aer_cap = pci_find_ext_capability(bridge, PCI_EXT_CAP_ID_ERR);
	if (aer_cap) {
		pci_read_config_dword(bridge, aer_cap + PCI_ERR_UNCOR_STATUS, &uncor_status);
		if (!PCI_POSSIBLE_ERROR(uncor_status)) {
			if (uncor_status & PCI_ERR_UNC_COMP_TIME)
				atomic_or(EXCP_REASON_CPL_TIMEOUT, &link_exception->reason_flags);
			if (uncor_status & PCI_ERR_UNC_SURPDN)
				atomic_or(EXCP_REASON_LINK_DOWN, &link_exception->reason_flags);
		} else {
			LOG_WARN("Failed to read config space\n");
		}
	} else {
		LOG_WARN("AER capability not found\n");
	}
}

static struct link_exception *dev_to_link_exception(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct radio_google *goog;

	if (!mdev || !mdev->google)
		return NULL;

	goog = mdev->google;

	return goog->link_exception;
}

static ssize_t reason_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct link_exception *link_exception = dev_to_link_exception(dev);
	int flags, bit;

	if (!link_exception)
		return -ENODEV;

	flags = atomic_read(&link_exception->reason_flags);
	if (!flags)
		return sysfs_emit(buf, "\n");

	bit = fls(flags) - 1;
	if (bit >= ARRAY_SIZE(excp_reason_desc)) {
		LOG_ERR("Out-of-bound reason: %d\n", bit);
		return sysfs_emit(buf, "Unknown\n");
	}

	return sysfs_emit(buf, "%s\n", excp_reason_desc[bit]);
}

static ssize_t reason_flags_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct link_exception *link_exception = dev_to_link_exception(dev);

	if (!link_exception)
		return -ENODEV;

	return sysfs_emit(buf, "0x%x\n", atomic_read(&link_exception->reason_flags));
}

static DEVICE_ATTR_RO(reason);
static DEVICE_ATTR_RO(reason_flags);

static struct attribute *link_exception_attrs[] = {
	&dev_attr_reason.attr,
	&dev_attr_reason_flags.attr,
	NULL,
};

static const struct attribute_group link_exception_attr_group = {
	.name = "google_link_exception",
	.attrs = link_exception_attrs,
};

int link_exception_init(struct radio_google *goog)
{
	struct link_exception *link_exception;
	int ret = 0;

	link_exception = devm_kzalloc(goog->mdev->dev, sizeof(*link_exception), GFP_KERNEL);
	if (!link_exception)
		return -ENOMEM;

	link_exception->goog = goog;
	goog->link_exception = link_exception;

	ret = devm_device_add_group(goog->mdev->dev, &link_exception_attr_group);
	if (ret) {
		LOG_ERR("Failed to create sysfs group: %d\n", ret);
		return ret;
	}

	return 0;
}

void link_exception_fsm_state_handler(struct radio_google *goog, struct mtk_fsm_param *param)
{
	switch (param->to) {
	case FSM_STATE_OFF:
		/* Typical case: failed to pack MDEE DB and lead to ZE */
		if (param->from == FSM_STATE_POSTDUMP)
			atomic_set(&goog->link_exception->reason_flags, 0);
		break;
	case FSM_STATE_READY:
		/* Rare case: successfully pack MDEE DB */
		atomic_set(&goog->link_exception->reason_flags, 0);
		break;
	case FSM_STATE_EXCEPTION:
		/*
		 * AER uncorrectable error, link error and link unready case eventually goes here in
		 * the FSM thread. Avoid triggering coredump if UNCOR_ERROR flag is set because PCIe
		 * RC driver already does it.
		 */
		if ((param->fsm_flag & FSM_F_LINK_EXCEPTION) &&
		    !(atomic_read(&goog->link_exception->reason_flags) & EXCP_REASON_UNCOR_ERROR))
			trigger_pcie_coredump();
		break;
	default:
		break;
	}
}

void radio_google_link_error_handler(struct radio_google *goog)
{
	struct link_exception *link_exception;

	LOG_ERR("Link error detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	atomic_or(EXCP_REASON_LINK_ERROR, &link_exception->reason_flags);
	check_uncor_error_status(link_exception);

	/* Trigger only for non-AER cases */
	if (panic_on_link_error &&
	    !(atomic_read(&link_exception->reason_flags) & EXCP_REASON_UNCOR_ERROR))
		panic("Modem PCIe driver-detected link error");
}
EXPORT_SYMBOL_GPL(radio_google_link_error_handler);

void radio_google_uncor_error_handler(struct radio_google *goog)
{
	struct link_exception *link_exception;
	int flags;

	LOG_ERR("Uncorrectable error detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	atomic_or(EXCP_REASON_UNCOR_ERROR, &link_exception->reason_flags);
	check_uncor_error_status(link_exception);

	flags = atomic_read(&link_exception->reason_flags);
	if (panic_on_surprise_down && (flags & EXCP_REASON_LINK_DOWN))
		panic("Modem PCIe surprise down");
	else if (panic_on_cpl_timeout && (flags & EXCP_REASON_CPL_TIMEOUT))
		panic("Modem PCIe completion timeout");
	else if (panic_on_uncor_error &&
		 !(flags & (EXCP_REASON_LINK_DOWN | EXCP_REASON_CPL_TIMEOUT)))
		panic("Modem PCIe uncorrectable error");
}
EXPORT_SYMBOL_GPL(radio_google_uncor_error_handler);

void radio_google_error_detected_post(struct pci_dev *pdev, pci_channel_state_t state)
{
	/* Intentionally delay the hot reset to eusure EP dumping the correct status. */
	if (hot_reset_delay_ms > 0)
		msleep(hot_reset_delay_ms);
}
EXPORT_SYMBOL_GPL(radio_google_error_detected_post);

static void cor_error_handler(struct radio_google *goog)
{
	struct link_exception *link_exception;

	LOG_ERR("Correctable error detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	if (atomic_fetch_or(EXCP_REASON_COR_ERROR, &link_exception->reason_flags) &
	    EXCP_REASON_COR_ERROR)
		return;

	if (panic_on_cor_error)
		panic("Modem PCIe correctable error");

	goog->tmi_ops->pwrctl.force_md_assert();
	trigger_pcie_coredump();
}

void radio_google_cor_error_detected(struct pci_dev *pdev)
{
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);

	if (enable_cor_error_handler)
		cor_error_handler(mdev->google);
}
EXPORT_SYMBOL_GPL(radio_google_cor_error_detected);

void radio_google_cold_resume_handler(struct radio_google *goog)
{
	struct link_exception *link_exception;

	LOG_ERR("Cold resume detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	atomic_or(EXCP_REASON_COLD_RESUME, &link_exception->reason_flags);

	if (panic_on_cold_resume)
		panic("Modem PCIe cold resume");
}
EXPORT_SYMBOL_GPL(radio_google_cold_resume_handler);

void radio_google_link_unready_handler(struct radio_google *goog, u32 resume_state)
{
	struct link_exception *link_exception;

	LOG_ERR("Link unready detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	atomic_or(EXCP_REASON_LINK_UNREADY, &link_exception->reason_flags);

	if (panic_on_link_unready)
		panic("Modem PCIe link unready (resume_state = 0x%x)", resume_state);
}
EXPORT_SYMBOL_GPL(radio_google_link_unready_handler);

void radio_google_suspend_timeout_handler(struct radio_google *goog)
{
	struct link_exception *link_exception;

	LOG_ERR("Suspend timeout detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	atomic_or(EXCP_REASON_SUSPEND_TIMEOUT, &link_exception->reason_flags);

	if (panic_on_suspend_timeout)
		panic("Modem PCIe suspend timeout");

	trigger_pcie_coredump();
}
EXPORT_SYMBOL_GPL(radio_google_suspend_timeout_handler);

void radio_google_resume_timeout_handler(struct radio_google *goog)
{
	struct link_exception *link_exception;

	LOG_ERR("Resume timeout detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	atomic_or(EXCP_REASON_RESUME_TIMEOUT, &link_exception->reason_flags);

	if (panic_on_resume_timeout)
		panic("Modem PCIe resume timeout");

	trigger_pcie_coredump();
}
EXPORT_SYMBOL_GPL(radio_google_resume_timeout_handler);

void radio_google_cldma_error_handler(struct radio_google *goog, enum link_exception_reason reason)
{
	struct link_exception *link_exception;

	LOG_ERR("CLDMA error detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	switch (reason) {
	case EXCP_REASON_CLDMA_ERROR:
		atomic_or(EXCP_REASON_CLDMA_ERROR, &link_exception->reason_flags);
		break;
	case EXCP_REASON_CLDMA_TX_TIMEOUT:
		atomic_or(EXCP_REASON_CLDMA_TX_TIMEOUT, &link_exception->reason_flags);
		break;
	case EXCP_REASON_CLDMA_RX_HWO_ERROR:
		atomic_or(EXCP_REASON_CLDMA_RX_HWO_ERROR, &link_exception->reason_flags);
		break;
	case EXCP_REASON_CLDMA_TX_HWO_ERROR:
		atomic_or(EXCP_REASON_CLDMA_TX_HWO_ERROR, &link_exception->reason_flags);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(radio_google_cldma_error_handler);

void radio_google_port_error_handler(struct radio_google *goog, enum link_exception_reason reason)
{
	struct link_exception *link_exception;

	LOG_ERR("Port error detected!\n");

	if (unlikely(!goog))
		return;

	link_exception = goog->link_exception;
	if (unlikely(!link_exception))
		return;

	switch (reason) {
	case EXCP_REASON_CCCI_PKT_OUT_OF_ORDER:
		atomic_or(EXCP_REASON_CCCI_PKT_OUT_OF_ORDER, &link_exception->reason_flags);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(radio_google_port_error_handler);
