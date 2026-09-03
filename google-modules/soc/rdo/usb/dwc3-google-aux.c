// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 * Google's SoC-specific Glue Driver for auxiliary host only dwc3 USB controller
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "gadget.h"
#include "core.h"

#define CREATE_TRACE_POINTS
#include "usb_trace.h"
#include <trace/define_trace.h>
#include <interconnect/google_icc_helper.h>

#include "dwc3-google.h"

#define DWC3_GOOGLE_CONTROLLER_PHY_CONTROL 0x0
#define DWC3_GOOGLE_PMU_PHY_CONTROL 0x3

/* USBCS_HOST PMU CSR offsets */
#define USBCS_HC_STATUS_OFFSET 0x0
#define USBCS_HOST_CFG1_OFFSET 0x4

/* AUX_USB3 PMU CSR offsets */
#define AUX_USB3_CFG_REG0_OFFSET 0x0
#define AUX_USB3_STS_REG0_OFFSET 0x8

/* USBCS_USB_INT CSR offsets */
#define USBCS_USBINT_CFG1_OFFSET 0x0
#define USBCS_USBINT_STATUS_OFFSET 0x4

/* USBCS_USB_INT CSR fields */
#define USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_MSK	BIT(2)
#define USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_INT_EN	BIT(8)
#define USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2_INTR_CLR	BIT(14)

/* USBCS_USB_INT_STATUS CSR fields */
#define USBCS_USBINT_STATUS_USBDRD_PME_GEN_U2P_INTR_STS_RAW	BIT(2)

/* USBCS_TOP_CFG1 CSR fields */
#define USBCS_TOP_CFG1_R_VC_OVRD_EN GENMASK(0, 0)
#define USBCS_TOP_CFG1_R_VC_OVRD_VAL GENMASK(7, 4)
#define USBCS_TOP_CFG1_W_VC_OVRD_EN GENMASK(8, 8)
#define USBCS_TOP_CFG1_W_VC_OVRD_VAL GENMASK(15, 12)

/* USBCS_TOP_CTRL_CFG1 */
#define USBCS_TOP_CTRL_CFG1_OFFSET 0x4

static const struct reg_field google_pmu_reg_fields[]  = {
	[PME_EN] = REG_FIELD(AUX_USB3_CFG_REG0_OFFSET, 8, 8),
	[POWER_STATE_REQUEST] = REG_FIELD(AUX_USB3_CFG_REG0_OFFSET, 9, 10),
	[CURRENT_POWER_STATE_U2PMU] = REG_FIELD(AUX_USB3_STS_REG0_OFFSET, 1, 2),
};

static const struct regmap_config dwc3_google_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.name = "dwc3-google",
};

static int dwc3_google_set_icc_bw(struct dwc3_google *gdwc3, u32 avg_bw, u32 peak_bw)
{
	int ret = 0;

	if (!gdwc3->icc_path)
		return ret;

	google_icc_set_read_bw_gmc(gdwc3->icc_path, avg_bw, peak_bw, 0, gdwc3->usb_vc);
	google_icc_set_write_bw_gmc(gdwc3->icc_path, avg_bw, peak_bw, 0, gdwc3->usb_vc);
	ret = google_icc_update_constraint_async(gdwc3->icc_path);
	if (ret)
		dev_err(gdwc3->dev, "failed to update constraints: (%d)\n", ret);

	return ret;
}

static ssize_t avg_bw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", gdwc3->avg_bw);
}

static ssize_t avg_bw_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t n)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);
	int ret;
	u32 input_value;

	ret = kstrtou32(buf, 10, &input_value);
	if (ret)
		return ret;

	gdwc3->avg_bw = input_value;
	dev_info(gdwc3->dev, "Stored %u as avg_bw\n", gdwc3->avg_bw);

	ret = dwc3_google_set_icc_bw(gdwc3, gdwc3->avg_bw, gdwc3->peak_bw);
	if (ret) {
		dev_err(gdwc3->dev, "failed to update constraints: (%d)\n", ret);
		return ret;
	}
	return n;
}
static DEVICE_ATTR_RW(avg_bw);

static ssize_t peak_bw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", gdwc3->peak_bw);
}

static ssize_t peak_bw_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t n)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);
	int ret;
	u32 input_value;

	ret = kstrtou32(buf, 10, &input_value);
	if (ret)
		return ret;

	gdwc3->peak_bw = input_value;
	dev_info(gdwc3->dev, "Stored %u as peak_bw\n", gdwc3->peak_bw);

	ret = dwc3_google_set_icc_bw(gdwc3, gdwc3->avg_bw, gdwc3->peak_bw);
	if (ret) {
		dev_err(gdwc3->dev, "failed to update constraints: (%d)\n", ret);
		return ret;
	}
	return n;
}
static DEVICE_ATTR_RW(peak_bw);

static struct attribute *dwc3_google_attrs[] = {
	&dev_attr_avg_bw.attr,
	&dev_attr_peak_bw.attr,
	NULL
};
ATTRIBUTE_GROUPS(dwc3_google);

static int dwc3_google_usb_role_switch_set(struct usb_role_switch *sw,
					   enum usb_role role)
{
	struct dwc3_google *gdwc3 = usb_role_switch_get_drvdata(sw);
	unsigned long flags;

	if (role == USB_ROLE_DEVICE) {
		dev_err(gdwc3->dev, "%s: Invalid role, USB_ROLE_DEVICE\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&gdwc3->role_lock, flags);
	gdwc3->desired_role = role;
	spin_unlock_irqrestore(&gdwc3->role_lock, flags);

	mod_delayed_work(system_freezable_wq, &gdwc3->role_switch_work, 0);
	return 0;
}

static int dwc3_google_parse_clocks(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_clks; i++)
		gdwc3->clocks[i].id = gdwc3->drv_data->clk_names[i];

	return devm_clk_bulk_get(gdwc3->dev, gdwc3->drv_data->num_clks, gdwc3->clocks);
}

static int dwc3_google_parse_resets(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_rsts; i++)
		gdwc3->resets[i].id = gdwc3->drv_data->rst_names[i];

	return devm_reset_control_bulk_get_exclusive(gdwc3->dev,
		gdwc3->drv_data->num_rsts, gdwc3->resets);
}

static void dwc3_find_non_sticky_reset(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_rsts; i++) {
		if (!(strcmp(gdwc3->resets[i].id, "usbc_non_sticky"))) {
			gdwc3->usbc_non_sticky_rst = gdwc3->resets[i].rstc;
			return;
		}
	}
	dev_warn(gdwc3->dev, "usbc_non_sticky Reset not found");
}

static int google_configure_glue(struct dwc3_google *gdwc3)
{
	int ret = 0;

	ret = clk_bulk_prepare_enable(gdwc3->drv_data->num_clks, gdwc3->clocks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(gdwc3->drv_data->num_rsts, gdwc3->resets);
	if (ret)
		clk_bulk_disable_unprepare(gdwc3->drv_data->num_clks, gdwc3->clocks);
	return ret;
}

static void google_unconfigure_glue(struct dwc3_google *gdwc3)
{
	reset_control_bulk_assert(gdwc3->drv_data->num_rsts, gdwc3->resets);
	clk_bulk_disable_unprepare(gdwc3->drv_data->num_clks, gdwc3->clocks);
}

static int google_usb_pwr_enable(struct dwc3_google *gdwc3)
{
	int ret;

	if (gdwc3->usb_on) {
		dev_warn(gdwc3->dev, "Trying to enable USB top while it's ON");
		return 0;
	}

	dev_info(gdwc3->dev, "Enabling USB top power\n");

	if (!IS_ERR_OR_NULL(gdwc3->usb_psw_pd)) {
		ret = pm_runtime_resume_and_get(gdwc3->usb_psw_pd);
		if (ret) {
			dev_err(gdwc3->dev, "Failed to enable USB top power, err: %d\n", ret);
			return ret;
		}
	}
	gdwc3->usb_on = true;

	/* Upon power loss any previous configuration is lost, restore it */
	ret = google_configure_glue(gdwc3);
	if (ret) {
		dev_err(gdwc3->dev, "failed to configure glue, err: %d\n", ret);
		goto power_off_usb_top;
	}

	ret = phy_init(gdwc3->u2_phy);
	if (ret)
		goto unconfigure;
	return 0;

unconfigure:
	google_unconfigure_glue(gdwc3);
power_off_usb_top:
	gdwc3->usb_on = false;
	if (!IS_ERR_OR_NULL(gdwc3->usb_psw_pd))
		pm_runtime_put_sync(gdwc3->usb_psw_pd);

	return ret;
}

static int google_usb_pwr_disable(struct dwc3_google *gdwc3)
{
	if (!gdwc3->usb_on) {
		dev_warn(gdwc3->dev, "Trying to disable USB top while it's OFF");
		return 0;
	}

	phy_exit(gdwc3->u2_phy);

	google_unconfigure_glue(gdwc3);

	dev_info(gdwc3->dev, "Disabling USB top power\n");
	if (!IS_ERR_OR_NULL(gdwc3->usb_psw_pd)) {
		pm_runtime_put_sync(gdwc3->usb_psw_pd);
	}

	gdwc3->usb_on = false;
	return 0;
}

static u32 dwc3_google_clear_pme_irqs(struct dwc3_google *gdwc3)
{
	u32 irq_status, reg_set, reg_clear;

	irq_status = readl(gdwc3->usbcs_usbint_base + USBCS_USBINT_STATUS_OFFSET);
	reg_set = readl(gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
	reg_clear = reg_set;
	if (irq_status & USBCS_USBINT_STATUS_USBDRD_PME_GEN_U2P_INTR_STS_RAW) {
		reg_set |= USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2_INTR_CLR;
		reg_clear &= ~USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2_INTR_CLR;
	}
	//TODO(b/342307313) : Review irq clear sequence.
	writel(reg_set, gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
	writel(reg_clear, gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
	return irq_status;
}

static void dwc3_google_enable_pme_irqs(struct dwc3_google *gdwc3)
{
	u32 reg;
	/* Enable and unmask PME interrupt*/
	reg = readl(gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
	reg &= ~(USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_MSK);
	reg |= USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_INT_EN;
	writel(reg, gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
}

static void dwc3_google_disable_pme_irqs(struct dwc3_google *gdwc3)
{
	u32 reg;
	/* Disable and Mask PME interrupt*/
	reg = readl(gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
	reg &= ~(USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_INT_EN);
	reg |= USBCS_USBINT_CFG1_USBDRD_PME_GEN_U2P_INTR_MSK;
	writel(reg, gdwc3->usbcs_usbint_base + USBCS_USBINT_CFG1_OFFSET);
}

static int dwc3_google_pmu_set_state(struct dwc3_google *gdwc3, int state)
{
	u32 val;
	int ret;

	ret = regmap_field_write(gdwc3->pmu_fields[PME_EN], 1);
	if (ret)
		return ret;

	ret = regmap_field_write(gdwc3->pmu_fields[POWER_STATE_REQUEST], state);
	if (ret)
		return ret;

	ret = regmap_field_read_poll_timeout(gdwc3->pmu_fields[CURRENT_POWER_STATE_U2PMU],
					     val, val == state, POLL_DELAY_US, POLL_TIMEOUT_US);
	if (ret)
		dev_err(gdwc3->dev, "PMU state %d poll failed U2:%u\n", state, val);
	return ret;
}

static int dwc3_google_psw_pd_off(struct dwc3_google *gdwc3)
{
	int ret;

	pm_runtime_get_sync(gdwc3->usb_top_pd);
	ret = device_wakeup_enable(gdwc3->usb_top_pd);
	if (ret) {
		dev_err(gdwc3->dev, "Failed to enable wakeup, err: %d\n", ret);
		pm_runtime_put_sync(gdwc3->usb_top_pd);
		return ret;
	}
	ret = pm_runtime_put_sync(gdwc3->usb_psw_pd);
	if (ret) {
		dev_err(gdwc3->dev, "Failed to disable psw power, err: %d\n", ret);
		pm_runtime_put_sync(gdwc3->usb_top_pd);
		device_wakeup_disable(gdwc3->usb_top_pd);
	}
	return ret;
}

static int dwc3_google_psw_pd_on(struct dwc3_google *gdwc3)
{
	int ret;

	ret = pm_runtime_resume_and_get(gdwc3->usb_psw_pd);
	if (ret) {
		dev_err(gdwc3->dev, "Failed to enable USB top power, err: %d\n", ret);
		return ret;
	}
	device_wakeup_disable(gdwc3->usb_top_pd);
	pm_runtime_put_sync(gdwc3->usb_top_pd);
	return 0;
}

static int dwc3_google_psw_pd_notifier(struct notifier_block *nb, unsigned long action, void *d)
{
	struct dwc3_google *gdwc3 = container_of(nb, struct dwc3_google, usb_psw_pd_nb);
	struct dwc3 *dwc = platform_get_drvdata(gdwc3->dwc3);
	int ret;

	if (gdwc3->current_role != USB_ROLE_HOST || !dwc->xhci)
		return NOTIFY_OK;

	if (action == GENPD_NOTIFY_OFF) {
		dev_dbg(gdwc3->dev, "Switching phy control to PMU\n");
		dwc3_google_pmu_set_state(gdwc3, DWC3_GOOGLE_PMU_PHY_CONTROL);
		ret = reset_control_assert(gdwc3->usbc_non_sticky_rst);
		if (ret)
			dev_err(gdwc3->dev, "non-sticky reset assert failed: %d\n", ret);
		if (gdwc3->wakeup)
			dwc3_google_enable_pme_irqs(gdwc3);
	} else if (action == GENPD_NOTIFY_ON) {
		dev_dbg(gdwc3->dev, "Switching phy control to Controller\n");
		dwc3_google_clear_pme_irqs(gdwc3);
		ret = reset_control_deassert(gdwc3->usbc_non_sticky_rst);
		if (ret)
			dev_err(gdwc3->dev, "non-sticky reset deassert failed: %d\n", ret);
		dwc3_google_pmu_set_state(gdwc3, DWC3_GOOGLE_CONTROLLER_PHY_CONTROL);
		if (gdwc3->wakeup)
			dwc3_google_disable_pme_irqs(gdwc3);
	}
	return NOTIFY_OK;
}

static void dwc3_google_configure_qos(struct dwc3_google *gdwc3)
{
	u32 reg;

	reg = readl(gdwc3->usb_top_cfg_reg);
	reg |= (USBCS_TOP_CFG1_R_VC_OVRD_EN | USBCS_TOP_CFG1_W_VC_OVRD_EN);
	reg &= ~(USBCS_TOP_CFG1_R_VC_OVRD_VAL | USBCS_TOP_CFG1_W_VC_OVRD_VAL);
	reg |= (FIELD_PREP(USBCS_TOP_CFG1_R_VC_OVRD_VAL, gdwc3->usb_vc) |
			FIELD_PREP(USBCS_TOP_CFG1_W_VC_OVRD_VAL, gdwc3->usb_vc));
	writel(reg, gdwc3->usb_top_cfg_reg);
}

static void dwc3_google_enable_wakeup_irq(int irq)
{
	if (irq < 0)
		return;

	enable_irq(irq);
	enable_irq_wake(irq);
}

static void dwc3_google_disable_wakeup_irq(int irq)
{
	if (irq < 0)
		return;

	disable_irq_wake(irq);
	disable_irq_nosync(irq);
}

static irqreturn_t dwc3_google_resume_interrupt(int irq, void *_gdwc3)
{
	struct dwc3_google	*gdwc3 = _gdwc3;
	struct dwc3 *dwc = platform_get_drvdata(gdwc3->dwc3);
	u32 irq_status_reg;

	dev_dbg(gdwc3->dev, "resume interrupt irq: %d\n", irq);

	irq_status_reg = dwc3_google_clear_pme_irqs(gdwc3);

	if (!gdwc3->is_suspended) {
		dev_warn(gdwc3->dev, "Spurious pme irq, 0x%x", irq_status_reg);
		return IRQ_HANDLED;
	}

	if (gdwc3->current_role == USB_ROLE_HOST) {
		if (dwc->xhci)
			pm_runtime_resume(&dwc->xhci->dev);
	} else {
		dev_err(gdwc3->dev, "Invalid Role during wakeup interrupt");
	}
	return IRQ_HANDLED;
}

static int dwc3_google_probe_children(struct dwc3_google *gdwc3)
{
	int ret;
	struct device *dev = gdwc3->dev;
	struct device_node *dwc3_np, *node = dev->of_node;

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret) {
		dev_err(dev, "runtime get_sync failed");
		goto disable_rpm;
	}
	pm_runtime_forbid(dev);

	if (of_platform_populate(node, NULL, NULL, dev)) {
		dev_err(dev, "failed to add dwc3 core\n");
		ret = -ENODEV;
		goto allow_rpm;
	}

	dwc3_np = of_get_compatible_child(node, "snps,dwc3");
	if (!dwc3_np) {
		dev_err(dev, "failed to find dwc3 core child\n");
		return -ENODEV;
	}

	gdwc3->dwc3 = of_find_device_by_node(dwc3_np);
	if (!gdwc3->dwc3) {
		ret = -EPROBE_DEFER;
		dev_err(dev, "failed to get dwc3 platform device\n");
		goto dev_depopulate;
	}

	ret = device_init_wakeup(gdwc3->dev, true);
	if (ret)
		goto dev_depopulate;

	/* Without wakeup core driver calls dwc3_core_exit() leading to phy_exit()*/
	ret = device_init_wakeup(&gdwc3->dwc3->dev, true);
	if (ret)
		goto deinit_wakeup;

	pm_runtime_allow(dev);
	pm_runtime_allow(&gdwc3->dwc3->dev);
	/*
	 * b/346776825: host mode hibernation cannot be paused and resumed before it's completed.
	 * Disable DWC3 autosuspend to prevent unnecessary delays after xHCI suspension.
	 */
	pm_runtime_dont_use_autosuspend(&gdwc3->dwc3->dev);
	pm_runtime_put(dev);

	return 0;

deinit_wakeup:
	device_init_wakeup(gdwc3->dev, false);
dev_depopulate:
	of_platform_depopulate(dev);
	of_node_put(dwc3_np);
allow_rpm:
	pm_runtime_allow(dev);
disable_rpm:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	return ret;
}

static void dwc3_google_remove_children(struct dwc3_google *gdwc3)
{
	of_platform_depopulate(gdwc3->dev);
	device_init_wakeup(gdwc3->dev, false);
	device_init_wakeup(&gdwc3->dwc3->dev, false);
	pm_runtime_disable(gdwc3->dev);
	if (!pm_runtime_status_suspended(gdwc3->dev)) {
		google_unconfigure_glue(gdwc3);
		pm_runtime_set_suspended(gdwc3->dev);
	}
}

static void _dwc3_google_set_role(struct work_struct *work)
{
	struct dwc3_google *gdwc3 = container_of(work, struct dwc3_google, role_switch_work.work);
	enum usb_role curr_role, dr_role;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&gdwc3->role_lock, flags);
	curr_role = gdwc3->current_role;
	dr_role = gdwc3->desired_role;
	spin_unlock_irqrestore(&gdwc3->role_lock, flags);

	if (curr_role == dr_role) {
		dev_info(gdwc3->dev, "%s scheduled with same role %s\n", __func__,
			 usb_role_string(dr_role));
		return;
	}

	dev_info(gdwc3->dev, "%s %s\n", __func__, usb_role_string(dr_role));

	if (dr_role == USB_ROLE_NONE) {
		//Set current_role first to avoid hibernation
		gdwc3->current_role = dr_role;
		dwc3_google_remove_children(gdwc3);
		return;
	}

	if (dr_role == USB_ROLE_HOST) {
		ret = dwc3_google_probe_children(gdwc3);
		if (ret) {
			dev_err(gdwc3->dev, "Adding dwc3 children failed(%d)\n", ret);
			return;
		}
	}
	gdwc3->current_role = dr_role;
}

static int dwc3_google_setup_role_switch(struct dwc3_google *gdwc3)
{
	struct usb_role_switch_desc dwc3_role_switch = {NULL};

	spin_lock_init(&gdwc3->role_lock);
	INIT_DELAYED_WORK(&gdwc3->role_switch_work, _dwc3_google_set_role);
	dwc3_role_switch.fwnode = dev_fwnode(gdwc3->dev);
	dwc3_role_switch.set = dwc3_google_usb_role_switch_set;
	dwc3_role_switch.allow_userspace_control = true;
	dwc3_role_switch.driver_data = gdwc3;
	gdwc3->role_sw = usb_role_switch_register(gdwc3->dev, &dwc3_role_switch);
	if (IS_ERR(gdwc3->role_sw))
		return PTR_ERR(gdwc3->role_sw);
	return 0;
}

static int dwc3_google_probe(struct platform_device *pdev)
{
	int ret;
	struct dwc3_google *gdwc3;
	struct regmap *mmio_regmap;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	dev_dbg(dev, "dwc3-google probe\n");

	if (!node) {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		return -ENODEV;
	}

	gdwc3 = devm_kzalloc(dev, sizeof(*gdwc3), GFP_KERNEL);
	if (!gdwc3)
		return -ENOMEM;
	gdwc3->drv_data = of_device_get_match_data(dev);

	gdwc3->usbcs_host_cfg_base =
		devm_platform_ioremap_resource_byname(pdev, "usbcs_host_cfg_csr");
	if (IS_ERR(gdwc3->usbcs_host_cfg_base))
		return PTR_ERR(gdwc3->usbcs_host_cfg_base);

	mmio_regmap = devm_regmap_init_mmio(dev, gdwc3->usbcs_host_cfg_base,
					    &dwc3_google_regmap_config);
	if (IS_ERR(mmio_regmap))
		return PTR_ERR(mmio_regmap);

	ret = devm_regmap_field_bulk_alloc(dev, mmio_regmap,
					   gdwc3->pmu_fields,  gdwc3->drv_data->pmu_reg_fields,
						gdwc3->drv_data->num_pmu_reg_fields);
	if (ret) {
		dev_err(dev, "Host PMU Regmap alloc failed: %d\n", ret);
		return ret;
	}

	gdwc3->usbcs_usbint_base =
		devm_platform_ioremap_resource_byname(pdev, "usbcs_usbint_csr");
	if (IS_ERR(gdwc3->usbcs_usbint_base))
		return PTR_ERR(gdwc3->usbcs_usbint_base);

	if (!device_property_read_u32(&pdev->dev, "usb-vc", &gdwc3->usb_vc)) {
		gdwc3->usb_top_cfg_reg =
			devm_platform_ioremap_resource_byname(pdev, "usb_top_cfg_csr");
		if (IS_ERR(gdwc3->usb_top_cfg_reg))
			return PTR_ERR(gdwc3->usb_top_cfg_reg);
	}

	gdwc3->pme_u2phy_irq = platform_get_irq_byname(pdev, "pme_gen_u2p_intr_agg");
	if (gdwc3->pme_u2phy_irq < 0) {
		dev_err(dev, "failed to fetch USB2 PHY PME gen intr\n");
		return gdwc3->pme_u2phy_irq;
	}

	irq_set_status_flags(gdwc3->pme_u2phy_irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(dev, gdwc3->pme_u2phy_irq, NULL,
					dwc3_google_resume_interrupt,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				"google HS", gdwc3);
	if (ret) {
		dev_err(dev, "pme_u2phy_irq failed: %d\n", ret);
		return ret;
	}

	gdwc3->u2_phy = devm_phy_get(dev, "usb2-phy");
	if (IS_ERR(gdwc3->u2_phy)) {
		ret = PTR_ERR(gdwc3->u2_phy);
		if (ret == -ENODEV) {
			gdwc3->u2_phy = NULL;
			dev_warn(dev, "No u2 phy\n");
		} else {
			return dev_err_probe(dev, ret, "no u2 phy configured\n");
		}
	}

	gdwc3->usb_on = false;
	// TODO(b/298785042): check if we need this flag
	gdwc3->is_suspended = true;
	gdwc3->current_role = USB_ROLE_NONE;
	gdwc3->dev = dev;
	gdwc3->usb_psw_pd = NULL;
	gdwc3->usb_top_pd = NULL;
	gdwc3->avg_bw = USB_HS_GOOGLE_ICC_BW_MB;
	gdwc3->peak_bw = USB_HS_GOOGLE_ICC_BW_MB;
	platform_set_drvdata(pdev, gdwc3);

	ret = dwc3_google_parse_clocks(gdwc3);
	if (ret) {
		dev_err(dev, "Failed to parse clocks\n");
		return ret;
	}

	ret = dwc3_google_parse_resets(gdwc3);
	if (ret) {
		dev_err(dev, "Failed to parse resets\n");
		return ret;
	}
	dwc3_find_non_sticky_reset(gdwc3);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dev_pm_domain_detach(dev, true);
	gdwc3->usb_psw_pd = dev_pm_domain_attach_by_name(dev, "usb_psw_pd");
	if (IS_ERR_OR_NULL(gdwc3->usb_psw_pd)) {
		dev_warn(dev, "Unable to find power-domains, assuming enabled\n");
	} else {
		gdwc3->usb_psw_pd_nb.notifier_call = dwc3_google_psw_pd_notifier;
		ret = dev_pm_genpd_add_notifier(gdwc3->usb_psw_pd, &gdwc3->usb_psw_pd_nb);
		if (ret)
			goto detach_usb_pds;
	}

	gdwc3->usb_top_pd = dev_pm_domain_attach_by_name(dev, "usb_top_pd");
	if (IS_ERR_OR_NULL(gdwc3->usb_top_pd)) {
		dev_warn(dev, "Unable to find power-domains, assuming enabled\n");
	} else {
		device_set_wakeup_capable(gdwc3->usb_top_pd, true);
		gdwc3->usb_top_pd_dl = device_link_add(dev, gdwc3->usb_top_pd, DL_FLAG_STATELESS);
		if (IS_ERR(gdwc3->usb_top_pd_dl))
			goto detach_usb_pds;
	}

	/* init usb icc path for gmc update */
	gdwc3->icc_path = google_devm_of_icc_get(dev, "sswrp-usb");
	if (IS_ERR(gdwc3->icc_path)) {
		ret = PTR_ERR(gdwc3->icc_path);
		dev_err(dev, "devm_of_icc_get(%s) failed", "sswrp-usb");
		goto detach_usb_pds;
	}

	ret = dwc3_google_setup_role_switch(gdwc3);
	if (ret)
		goto detach_usb_pds;
	return 0;

detach_usb_pds:
	if (!IS_ERR_OR_NULL(gdwc3->usb_psw_pd)) {
		dev_pm_genpd_remove_notifier(gdwc3->usb_psw_pd);
		dev_pm_domain_detach(gdwc3->usb_psw_pd, true);
	}
	if (!IS_ERR_OR_NULL(gdwc3->usb_top_pd)) {
		if (!IS_ERR_OR_NULL(gdwc3->usb_top_pd_dl))
			device_link_del(gdwc3->usb_top_pd_dl);
		device_set_wakeup_capable(gdwc3->usb_top_pd, true);
		dev_pm_domain_detach(gdwc3->usb_top_pd, true);
	}
	return ret;
}

static void dwc3_google_remove(struct platform_device *pdev)
{
	struct dwc3_google *gdwc3 = platform_get_drvdata(pdev);

	dev_dbg(gdwc3->dev, "dwc3-google remove\n");

	if (gdwc3->current_role == USB_ROLE_HOST)
		dwc3_google_remove_children(gdwc3);
	usb_role_switch_unregister(gdwc3->role_sw);
	if (!IS_ERR_OR_NULL(gdwc3->usb_psw_pd)) {
		dev_pm_genpd_remove_notifier(gdwc3->usb_psw_pd);
		dev_pm_domain_detach(gdwc3->usb_psw_pd, true);
	}
	if (!IS_ERR_OR_NULL(gdwc3->usb_top_pd)) {
		if (!IS_ERR_OR_NULL(gdwc3->usb_top_pd_dl))
			device_link_del(gdwc3->usb_top_pd_dl);
		device_init_wakeup(gdwc3->usb_top_pd, false);
		dev_pm_domain_detach(gdwc3->usb_top_pd, true);
	}
}

static int dwc3_google_suspend(struct dwc3_google *gdwc3, bool wakeup)
{
	int ret = 0;

	if (gdwc3->is_suspended)
		return 0;

	trace_platform_usb_suspend_start(__func__);

	/* resetting the votes to 0 for gmc */
	ret = dwc3_google_set_icc_bw(gdwc3, 0, 0);
	if (ret) {
		dev_err(gdwc3->dev, "failed to reset bandwidth: (%d)\n", ret);
		return ret;
	}

	switch (gdwc3->current_role) {
	case USB_ROLE_HOST:
		gdwc3->wakeup = wakeup;
		ret = dwc3_google_psw_pd_off(gdwc3);
		if (ret)
			break;
		gdwc3->is_suspended = true;
		if (wakeup)
			dwc3_google_enable_wakeup_irq(gdwc3->pme_u2phy_irq);
		break;
	case USB_ROLE_DEVICE:
	case USB_ROLE_NONE:
		google_usb_pwr_disable(gdwc3);
		gdwc3->is_suspended = true;
	}
	trace_platform_usb_suspend_end(__func__);
	return ret;
}

static int dwc3_google_resume(struct dwc3_google *gdwc3, bool wakeup)
{
	int ret = 0;

	if (!gdwc3->is_suspended)
		return 0;

	trace_platform_usb_resume_start(__func__);

	/* voting for the required read and write bw for gmc */
	ret = dwc3_google_set_icc_bw(gdwc3, gdwc3->avg_bw, gdwc3->peak_bw);
	if (ret) {
		dev_err(gdwc3->dev, "failed to set bandwidth: (%d)\n", ret);
		return ret;
	}

	if (!gdwc3->usb_on) {
		ret = google_usb_pwr_enable(gdwc3);
		if (ret < 0) {
			dev_err(gdwc3->dev, "Unable to turn on USB top\n");
			dwc3_google_set_icc_bw(gdwc3, 0, 0);
			return ret;
		}
		if (gdwc3->usb_top_cfg_reg)
			dwc3_google_configure_qos(gdwc3);
	}

	if (gdwc3->current_role == USB_ROLE_HOST) {
		if (wakeup)
			dwc3_google_disable_wakeup_irq(gdwc3->pme_u2phy_irq);
		ret = dwc3_google_psw_pd_on(gdwc3);
		if (ret) {
			if (wakeup)
				dwc3_google_enable_wakeup_irq(gdwc3->pme_u2phy_irq);
			dwc3_google_set_icc_bw(gdwc3, 0, 0);
			return ret;
		}
	}
	gdwc3->is_suspended = false;
	trace_platform_usb_resume_end(__func__);
	return 0;
}

static int dwc3_google_pm_suspend(struct device *dev)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "pm_suspend. device may wakeup : %d\n", device_may_wakeup(dev));

	ret = dwc3_google_suspend(gdwc3, device_may_wakeup(dev));
	return ret;
}

static int dwc3_google_pm_resume(struct device *dev)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(dev, "pm_resume. device may wakeup : %d\n", device_may_wakeup(dev));

	ret = dwc3_google_resume(gdwc3, device_may_wakeup(dev));

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return ret;
}

static int dwc3_google_runtime_suspend(struct device *dev)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);

	dev_dbg(dev, "runtime suspend\n");

	return dwc3_google_suspend(gdwc3, true);
}

static int dwc3_google_runtime_resume(struct device *dev)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);

	dev_dbg(dev, "runtime resume\n");

	return dwc3_google_resume(gdwc3, true);
}

static const struct dev_pm_ops dwc3_google_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_google_pm_suspend, dwc3_google_pm_resume)
	SET_RUNTIME_PM_OPS(dwc3_google_runtime_suspend,
			   dwc3_google_runtime_resume, NULL)
};

static const struct dwc3_google_driverdata mbu_aux_drvdata = {
	.clk_names = {
		"usbc_non_sticky",
		"usbc_sticky",
	},
	.rst_names = {
		"usbc_non_sticky",
		"usbc_sticky",
	},
	.pmu_reg_fields = google_pmu_reg_fields,
	.num_rsts = 2,
	.num_clks = 2,
	.num_pmu_reg_fields = ARRAY_SIZE(google_pmu_reg_fields),
};

static const struct of_device_id dwc3_google_match[] = {
	{
		.compatible = "google,dwc3-mbu-aux",
		.data = &mbu_aux_drvdata,
	}, {
	}
};

MODULE_DEVICE_TABLE(of, dwc3_google_match);

static struct platform_driver dwc3_google_aux_glue_driver = {
	.probe = dwc3_google_probe,
	.remove = dwc3_google_remove,
	.driver = {
		.name = "dwc3-google-aux",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&dwc3_google_dev_pm_ops),
		.of_match_table = dwc3_google_match,
		.dev_groups = dwc3_google_groups,
	}
};

module_platform_driver(dwc3_google_aux_glue_driver);

MODULE_AUTHOR("GOOGLE LLC");
MODULE_DESCRIPTION("Google DWC3 Glue Driver for Aux Controller");
MODULE_LICENSE("GPL");
