// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * Google's SoC-specific Glue Driver for the dwc3 USB controller
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>
#include <linux/fwnode.h>
#include <linux/usb/role.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/phy/phy.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>
#include "core.h"

#define CREATE_TRACE_POINTS
#include "usb_trace.h"
#include <trace/define_trace.h>

#define DWC3_GOOGLE_MAX_CLOCKS 10
// One time setup clocks are always on while USB top is on
#define DWC3_GOOGLE_MAX_OTS_CLOCKS 6

#define DWC3_GOOGLE_MAX_RESETS 8
#define DWC3_GOOGLE_MAX_OTS_RESETS 5

struct dwc3_google_driverdata {
	const char	*clk_names[DWC3_GOOGLE_MAX_CLOCKS];
	const char *ots_clk_names[DWC3_GOOGLE_MAX_OTS_CLOCKS];
	const char	*rst_names[DWC3_GOOGLE_MAX_RESETS];
	const char *ots_rst_names[DWC3_GOOGLE_MAX_OTS_RESETS];
	int	num_clks;
	int num_ots_clks;
	int	num_rsts;
	int num_ots_rsts;
};

struct dwc3_google {
	struct device *dev;
	struct platform_device *dwc3;
	const struct dwc3_google_driverdata *drv_data;
	struct clk_bulk_data clocks[DWC3_GOOGLE_MAX_CLOCKS];
	struct clk_bulk_data ots_clocks[DWC3_GOOGLE_MAX_OTS_CLOCKS];
	struct reset_control_bulk_data resets[DWC3_GOOGLE_MAX_RESETS];
	struct reset_control_bulk_data ots_resets[DWC3_GOOGLE_MAX_OTS_RESETS];
	struct usb_role_switch *role_sw;
	struct usb_role_switch *dwc3_drd_sw;
	struct usb_role_switch *phy_role_sw;
	struct device *genpd_dev;
	// To control phy runtime PM from this driver
	struct phy *u2_phy;
	struct phy *u3_phy;
	int pme_u2phy_irq;
	int pme_u3phy_irq;
	bool is_suspended;
	bool usb_top_on;
	enum usb_role role;
	struct regulator *vdd0p75;
	struct regulator *vdd1p2;
	int force_speed;
};

static ssize_t force_speed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", usb_speed_string(gdwc3->force_speed));
}

static ssize_t force_speed_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t n)
{
	struct dwc3_google *gdwc3 = dev_get_drvdata(dev);
	struct dwc3 *dwc = platform_get_drvdata(gdwc3->dwc3);

	if (sysfs_streq(buf, "super-speed-plus")) {
		gdwc3->force_speed = USB_SPEED_SUPER_PLUS;
	} else if (sysfs_streq(buf, "super-speed")) {
		gdwc3->force_speed = USB_SPEED_SUPER;
	} else if (sysfs_streq(buf, "high-speed")) {
		gdwc3->force_speed = USB_SPEED_HIGH;
	} else if (sysfs_streq(buf, "full-speed")) {
		gdwc3->force_speed = USB_SPEED_FULL;
	} else {
		return -EINVAL;
	}

	usb_udc_vbus_handler(dwc->gadget, false);

	dwc->maximum_speed = gdwc3->force_speed;
	dwc->gadget->max_speed = gdwc3->force_speed;

	msleep(100);

	usb_udc_vbus_handler(dwc->gadget, true);


	return n;
}
static DEVICE_ATTR_RW(force_speed);

static struct attribute *dwc3_google_attrs[] = {
	&dev_attr_force_speed.attr,
	NULL
};
ATTRIBUTE_GROUPS(dwc3_google);

static int dwc3_google_usb_role_switch_set(struct usb_role_switch *sw,
					 enum usb_role role)
{
	struct dwc3_google *gdwc3 = usb_role_switch_get_drvdata(sw);

	dev_info(gdwc3->dev, "%s %s\n", __func__, usb_role_string(role));
	gdwc3->role = role;
	// phy_role_sw callbacks write to dp_top_csr. Ensure USB Top is powered
	pm_runtime_get_sync(gdwc3->dev);
	usb_role_switch_set_role(gdwc3->phy_role_sw, role);
	usb_role_switch_set_role(gdwc3->dwc3_drd_sw, role);
	if (role == USB_ROLE_DEVICE)
		pm_stay_awake(gdwc3->dev);
	else
		pm_relax(gdwc3->dev);
	pm_runtime_put_sync(gdwc3->dev);
	return 0;
}

static enum usb_role dwc3_google_usb_role_switch_get(struct usb_role_switch *sw)
{
	struct dwc3_google *gdwc3 = usb_role_switch_get_drvdata(sw);

	return gdwc3->role;
}

static int dwc3_google_setup_role_switch(struct dwc3_google *gdwc3)
{
	struct usb_role_switch_desc dwc3_role_switch = {NULL};

	dwc3_role_switch.fwnode = dev_fwnode(gdwc3->dev);
	dwc3_role_switch.set = dwc3_google_usb_role_switch_set;
	dwc3_role_switch.get = dwc3_google_usb_role_switch_get;
	dwc3_role_switch.allow_userspace_control = true;
	dwc3_role_switch.driver_data = gdwc3;
	gdwc3->role_sw = usb_role_switch_register(gdwc3->dev, &dwc3_role_switch);
	if (IS_ERR(gdwc3->role_sw))
		return PTR_ERR(gdwc3->role_sw);

	return 0;
}

static int dwc3_google_parse_ots_clocks(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_ots_clks; i++)
		gdwc3->ots_clocks[i].id = gdwc3->drv_data->ots_clk_names[i];

	return devm_clk_bulk_get(gdwc3->dev, gdwc3->drv_data->num_ots_clks, gdwc3->ots_clocks);
}

static int dwc3_google_parse_clocks(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_clks; i++)
		gdwc3->clocks[i].id = gdwc3->drv_data->clk_names[i];

	return devm_clk_bulk_get(gdwc3->dev, gdwc3->drv_data->num_clks, gdwc3->clocks);
}

static int dwc3_google_parse_ots_resets(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_ots_rsts; i++)
		gdwc3->ots_resets[i].id = gdwc3->drv_data->ots_rst_names[i];

	return devm_reset_control_bulk_get_exclusive(gdwc3->dev,
		gdwc3->drv_data->num_ots_rsts, gdwc3->ots_resets);
}

static int dwc3_google_parse_resets(struct dwc3_google *gdwc3)
{
	int i;

	for (i = 0; i < gdwc3->drv_data->num_rsts; i++)
		gdwc3->resets[i].id = gdwc3->drv_data->rst_names[i];

	return devm_reset_control_bulk_get_exclusive(gdwc3->dev,
		gdwc3->drv_data->num_rsts, gdwc3->resets);
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

static int google_usb_top_enable(struct dwc3_google *gdwc3)
{
	int ret;

	if (gdwc3->usb_top_on) {
		dev_warn(gdwc3->dev, "Trying to enable USB top while it's ON");
		return 0;
	}

	dev_info(gdwc3->dev, "Enabling USB Regulators\n");

	ret = regulator_enable(gdwc3->vdd0p75);
	if (ret) {
		dev_err(gdwc3->dev, "Failed to enable PHY digital supply, err: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(gdwc3->vdd1p2);
	if (ret) {
		dev_err(gdwc3->dev, "Failed to enable PHY I/O supply, err: %d\n", ret);
		regulator_disable(gdwc3->vdd0p75);
		return ret;
	}

	if (!IS_ERR_OR_NULL(gdwc3->genpd_dev)) {
		dev_info(gdwc3->dev, "Enabling USB top power\n");
		pm_runtime_get_sync(gdwc3->genpd_dev);
		if (ret) {
			regulator_disable(gdwc3->vdd1p2);
			regulator_disable(gdwc3->vdd0p75);
			return ret;
		}
	}

	gdwc3->usb_top_on = true;
	ret = clk_bulk_prepare_enable(gdwc3->drv_data->num_ots_clks, gdwc3->ots_clocks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(gdwc3->drv_data->num_ots_rsts, gdwc3->ots_resets);
	if (ret)
		clk_bulk_disable_unprepare(gdwc3->drv_data->num_ots_clks, gdwc3->ots_clocks);

	return ret;
}

static int google_usb_top_disable(struct dwc3_google *gdwc3)
{
	if (!gdwc3->usb_top_on) {
		dev_warn(gdwc3->dev, "Trying to disable USB top while it's OFF");
		return 0;
	}

	if (!IS_ERR_OR_NULL(gdwc3->genpd_dev)) {
		dev_info(gdwc3->dev, "Disabling USB top power\n");
		pm_runtime_put_sync(gdwc3->genpd_dev);
	}

	dev_info(gdwc3->dev, "Disabling USB regulators\n");
	regulator_disable(gdwc3->vdd0p75);
	regulator_disable(gdwc3->vdd1p2);
	reset_control_bulk_assert(gdwc3->drv_data->num_ots_rsts, gdwc3->ots_resets);
	clk_bulk_disable_unprepare(gdwc3->drv_data->num_ots_clks, gdwc3->ots_clocks);
	gdwc3->usb_top_on = false;
	return 0;
}

static void dwc3_google_enable_wakeup_irq(int irq)
{
	if (!irq)
		return;

	enable_irq(irq);
	enable_irq_wake(irq);
}

static void dwc3_google_disable_wakeup_irq(int irq)
{
	if (!irq)
		return;

	disable_irq_wake(irq);
	disable_irq_nosync(irq);
}

static irqreturn_t dwc3_google_resume_interrupt(int irq, void *_gdwc3)
{
	struct dwc3_google	*gdwc3 = _gdwc3;
	struct dwc3 *dwc = platform_get_drvdata(gdwc3->dwc3);
	enum usb_role role;

	dev_dbg(gdwc3->dev, "resume interrupt irq: %d\n", irq);
	if (!gdwc3->is_suspended)
		return IRQ_HANDLED;

	role = usb_role_switch_get_role(gdwc3->dwc3_drd_sw);
	if (role == USB_ROLE_HOST && dwc->xhci)
		pm_runtime_resume(&dwc->xhci->dev);
	else if (role == USB_ROLE_DEVICE)
		dev_err(gdwc3->dev, "Invalid Role during wakeup interrupt");

	return IRQ_HANDLED;
}

static int dwc3_google_probe(struct platform_device *pdev)
{
	int ret;
	int irq;
	struct dwc3_google *gdwc3;
	struct device *dev = &pdev->dev;
	struct device_node *dwc3_np, *node = dev->of_node;

	dev_dbg(dev, "dwc3-google probe\n");

	if (!node) {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		return -ENODEV;
	}

	gdwc3 = devm_kzalloc(dev, sizeof(*gdwc3), GFP_KERNEL);
	if (!gdwc3)
		return -ENOMEM;
	gdwc3->drv_data = of_device_get_match_data(dev);

	irq = platform_get_irq_byname(pdev, "pme_gen_u2p_intr_agg");
	if (irq < 0) {
		dev_err(dev, "failed to fetch USB2 PHY PME gen intr\n");
		return irq;
	}

	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(dev, irq, NULL,
				dwc3_google_resume_interrupt,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				"google HS", gdwc3);
	if (ret) {
		dev_err(dev, "pme_u2phy_irq failed: %d\n", ret);
		return ret;
	}
	gdwc3->pme_u2phy_irq = irq;

	gdwc3->pme_u3phy_irq = platform_get_irq_byname(pdev, "pme_gen_u3p_intr_agg");
	if (gdwc3->pme_u3phy_irq < 0) {
		dev_err(dev, "failed to fetch USB3 PHY PME gen intr\n");
		return gdwc3->pme_u3phy_irq;
	}
	irq_set_status_flags(gdwc3->pme_u3phy_irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(dev, gdwc3->pme_u3phy_irq, NULL,
				dwc3_google_resume_interrupt,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				"google SS", gdwc3);
	if (ret) {
		dev_err(dev, "pme_u3phy_irq failed: %d\n", ret);
		return ret;
	}

	gdwc3->u2_phy = devm_phy_get(dev, "usb2-phy");
	if (IS_ERR(gdwc3->u2_phy)) {
		ret = PTR_ERR(gdwc3->u2_phy);
		if (ret == -ENODEV) {
			gdwc3->u2_phy = NULL;
			dev_warn(dev, "No u2 phy\n");
		} else
			return dev_err_probe(dev, ret, "no u2 phy configured\n");
	}

	gdwc3->u3_phy = devm_phy_get(dev, "usb3-phy");
	if (IS_ERR(gdwc3->u3_phy)) {
		ret = PTR_ERR(gdwc3->u3_phy);
		if (ret == -ENODEV) {
			gdwc3->u3_phy = NULL;
			dev_warn(dev, "No u3 phy\n");
		} else
			return dev_err_probe(dev, ret, "no u3 phy configured\n");
	}

	gdwc3->usb_top_on = false;
	// TODO(b/298785042): check if we need this flag
	gdwc3->is_suspended = true;
	gdwc3->role = USB_ROLE_NONE;
	gdwc3->dev = dev;
	gdwc3->genpd_dev = NULL;
	platform_set_drvdata(pdev, gdwc3);
	ret = dwc3_google_parse_ots_clocks(gdwc3);
	if (ret)
		return ret;

	ret = dwc3_google_parse_clocks(gdwc3);
	if (ret) {
		dev_err(dev, "Failed to parse clocks\n");
		return ret;
	}

	ret = dwc3_google_parse_ots_resets(gdwc3);
	if (ret) {
		dev_err(dev, "Failed to parse ots resets\n");
		return ret;
	}

	ret = dwc3_google_parse_resets(gdwc3);
	if (ret) {
		dev_err(dev, "Failed to parse resets\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	gdwc3->vdd0p75 = devm_regulator_get(dev, "vdd0p75");
	if (IS_ERR(gdwc3->vdd0p75)) {
		ret = PTR_ERR(gdwc3->vdd0p75);
		dev_err(dev, "Failed to get 0.75V PHY regulator\n");
		return ret;
	}

	gdwc3->vdd1p2 = devm_regulator_get(dev, "vdd1p2");
	if (IS_ERR(gdwc3->vdd1p2)) {
		ret = PTR_ERR(gdwc3->vdd1p2);
		dev_err(dev, "Failed to get 1.2V PHY regulator\n");
		return ret;
	}

	dev_pm_domain_detach(dev, true);
	gdwc3->genpd_dev = dev_pm_domain_attach_by_id(dev, 0);

	if (IS_ERR(gdwc3->genpd_dev))
		dev_warn(dev, "Unable to find power-domains, assuming enabled\n");

	dwc3_np = of_get_compatible_child(node, "snps,dwc3");
	if (!dwc3_np) {
		ret = -ENODEV;
		dev_err(dev, "failed to find dwc3 core child\n");
		goto detach_genpd_dev;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret) {
		dev_err(dev, "runtime get_sync failed");
		goto disable_rpm;
	}
	pm_runtime_forbid(dev);

	if (google_configure_glue(gdwc3))
		goto disable_rpm;

	if (of_platform_populate(node, NULL, NULL, dev)) {
		dev_err(dev, "failed to add dwc3 core\n");
		ret = -ENODEV;
		goto assert_resets;
	}

	gdwc3->dwc3 = of_find_device_by_node(dwc3_np);
	if (!gdwc3->dwc3) {
		ret = -EPROBE_DEFER;
		dev_err(dev, "failed to get dwc3 platform device\n");
		goto dev_depopulate;
	}
	device_init_wakeup(gdwc3->dev, 1);

	gdwc3->dwc3_drd_sw = usb_role_switch_find_by_fwnode(dev_fwnode(&(gdwc3->dwc3->dev)));
	fwnode_handle_put(dev_fwnode(&(gdwc3->dwc3->dev)));
	if (IS_ERR(gdwc3->dwc3_drd_sw)) {
		gdwc3->dwc3_drd_sw = NULL;
		dev_err(dev, "failed to initialize dwc3_drd_sw\n");
		ret = PTR_ERR(gdwc3->dwc3_drd_sw);
		goto dev_depopulate;
	}

	if (gdwc3->u3_phy) {
		gdwc3->phy_role_sw =
			usb_role_switch_find_by_fwnode(dev_fwnode(&(gdwc3->u3_phy->dev)));
		fwnode_handle_put(dev_fwnode(&(gdwc3->u3_phy->dev)));
		if (IS_ERR(gdwc3->phy_role_sw)) {
			gdwc3->phy_role_sw = NULL;
			dev_err(dev, "phy didn't register a usb_role_switch object\n");
			ret = PTR_ERR(gdwc3->phy_role_sw);
			goto dev_depopulate;
		}
	}

	ret = dwc3_google_setup_role_switch(gdwc3);
	if (ret)
		goto dev_depopulate;

	pm_runtime_allow(dev);
	pm_runtime_allow(&gdwc3->dwc3->dev);
	pm_runtime_put(dev);

	return 0;

dev_depopulate:
	of_platform_depopulate(dev);
	of_node_put(dwc3_np);
assert_resets:
	google_unconfigure_glue(gdwc3);
disable_rpm:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
detach_genpd_dev:
	if (!IS_ERR_OR_NULL(gdwc3->genpd_dev))
		dev_pm_domain_detach(gdwc3->genpd_dev, true);
	return ret;
}

static void dwc3_google_remove(struct platform_device *pdev)
{
	struct dwc3_google *gdwc3 = platform_get_drvdata(pdev);

	dev_dbg(gdwc3->dev, "dwc3-google remove\n");

	usb_role_switch_unregister(gdwc3->role_sw);
	usb_role_switch_put(gdwc3->dwc3_drd_sw);
	usb_role_switch_put(gdwc3->phy_role_sw);
	device_wakeup_disable(gdwc3->dev);
	pm_runtime_disable(&pdev->dev);
	of_platform_depopulate(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev)) {
		google_unconfigure_glue(gdwc3);
		pm_runtime_set_suspended(&pdev->dev);
	}
}


static int dwc3_google_suspend(struct dwc3_google *gdwc3,
					      bool wakeup)
{
	enum usb_role role;
	int ret = 0;

	if (gdwc3->is_suspended)
		return 0;

	trace_platform_usb_suspend_start(__func__);

	phy_exit(gdwc3->u2_phy);
	phy_exit(gdwc3->u3_phy);

	role = usb_role_switch_get_role(gdwc3->dwc3_drd_sw);
	if (role == USB_ROLE_HOST && wakeup) {
		dwc3_google_enable_wakeup_irq(gdwc3->pme_u2phy_irq);
		dwc3_google_enable_wakeup_irq(gdwc3->pme_u3phy_irq);
	}

	google_unconfigure_glue(gdwc3);

	if (gdwc3->role == USB_ROLE_NONE) {
		ret = google_usb_top_disable(gdwc3);
		if (ret < 0) {
			dev_err(gdwc3->dev, "Unable to turn off USB top");
			google_configure_glue(gdwc3);
			if (role == USB_ROLE_HOST && wakeup) {
				dwc3_google_disable_wakeup_irq(gdwc3->pme_u2phy_irq);
				dwc3_google_disable_wakeup_irq(gdwc3->pme_u3phy_irq);
			}
			return ret;
		}
	}

	gdwc3->is_suspended = true;
	trace_platform_usb_suspend_end(__func__);

	return 0;
}

static int dwc3_google_resume(struct dwc3_google *gdwc3,
					     bool wakeup)
{
	int ret = 0;
	enum usb_role role;

	if (!gdwc3->is_suspended)
		return 0;

	trace_platform_usb_resume_start(__func__);

	if (!gdwc3->usb_top_on) {
		ret = google_usb_top_enable(gdwc3);
		if (ret < 0) {
			dev_err(gdwc3->dev, "Unable to turn on USB top\n");
			return ret;
		}
	}

	role = usb_role_switch_get_role(gdwc3->dwc3_drd_sw);
	if (role == USB_ROLE_HOST && wakeup) {
		dwc3_google_disable_wakeup_irq(gdwc3->pme_u2phy_irq);
		dwc3_google_disable_wakeup_irq(gdwc3->pme_u3phy_irq);
	}

	/* Upon power loss any previous configuration is lost, restore it */
	ret = google_configure_glue(gdwc3);
	if (ret)
		return ret;

	phy_init(gdwc3->u2_phy);
	phy_init(gdwc3->u3_phy);

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

static const struct dwc3_google_driverdata lga_drvdata = {
	.clk_names = {
		"usbc_non_sticky",
		"usbc_sticky",
		"u2phy_apb_clk"
	},
	.ots_clk_names = {
	},
	.rst_names = {
		"usbc_non_sticky",
		"usbc_sticky",
		"usb_drd_bus_rst",
		"u2phy_apb_rst",
	},
	.ots_rst_names = {
		"usb_top_csr"
	},
	.num_rsts = 4,
	.num_ots_rsts = 1,
	.num_clks = 3,
	.num_ots_clks = 0,
};

static const struct dwc3_google_driverdata rdo_drvdata = {
	.clk_names = {
		"u2phy_ref_clk",
		"usbdp_suspend_clk",
		"usbc_non_sticky",
		"usbc_sticky",
		"usb_drd_bus_clk",
		"u2phy_apb_clk",
		"csr",
		"dp_vcc"
	},
	.ots_clk_names = {
	},
	.rst_names = {
		"usbc_non_sticky",
		"usbc_sticky",
		"usb_drd_bus_rst",
		"u2phy_apb_rst",
		"csr",
		"usb_top_csr",
		"dp_vcc"
	},
	.ots_rst_names = {
	},
	.num_rsts = 7,
	.num_ots_rsts = 0,
	.num_clks = 8,
	.num_ots_clks = 0,
};

static const struct of_device_id dwc3_google_match[] = {
	{
		.compatible = "google,dwc3-lga",
		.data = &lga_drvdata,
	}, {
		.compatible = "google,dwc3",
		.data = &rdo_drvdata,
	}, {
	}
};

MODULE_DEVICE_TABLE(of, dwc3_google_match);

static struct platform_driver dwc3_google_glue_driver = {
	.probe = dwc3_google_probe,
	.remove = dwc3_google_remove,
	.driver = {
		.name = "dwc3-google",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&dwc3_google_dev_pm_ops),
		.of_match_table = dwc3_google_match,
		.dev_groups = dwc3_google_groups,
	}
};

module_platform_driver(dwc3_google_glue_driver);

MODULE_AUTHOR("GOOGLE LLC");
MODULE_DESCRIPTION("Google DWC3 Glue Driver");
MODULE_LICENSE("GPL");
