/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 * Google's SoC-specific Glue Driver for the dwc3 USB controller
 */
#ifndef _DWC3_GOOGLE_H
#define _DWC3_GOOGLE_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/fwnode.h>
#include <linux/usb/role.h>
#include <linux/phy/phy.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>

#define DWC3_GOOGLE_MAX_CLOCKS 10
// One time setup clocks are always on while USB top is on
#define DWC3_GOOGLE_MAX_OTS_CLOCKS 6

#define DWC3_GOOGLE_MAX_RESETS 8
#define DWC3_GOOGLE_MAX_OTS_RESETS 5

#define POLL_DELAY_US 10
#define POLL_TIMEOUT_US 10000
#define DWC3_AUTOSUSPEND_DELAY	5000 /* ms */
#define DWC3_HOST_AUTOSUSPEND_DELAY 500

/* Bandwidth requirements based on speed */
#define USB_HS_GOOGLE_ICC_BW_MB 40
#define USB_SS_GOOGLE_ICC_BW_MB 400
#define USB_SSP_GOOGLE_ICC_BW_MB 1000

enum google_pmu_fields {
	PME_EN = 0,
	POWER_STATE_REQUEST,
	CURRENT_POWER_STATE_U2PMU,
	CURRENT_POWER_STATE_U3PMU,
	MAX_PMU_FIELDS,
};

struct dwc3_google;

struct dwc3_google_driverdata {
	const char	*clk_names[DWC3_GOOGLE_MAX_CLOCKS];
	const char	*rst_names[DWC3_GOOGLE_MAX_RESETS];
	const struct reg_field *pmu_reg_fields;
	int	num_clks;
	int	num_rsts;
	int num_pmu_reg_fields;
	void (*configure_qos)(struct dwc3_google *gdwc3);
};

struct dwc3_google {
	struct device *dev;
	struct platform_device *dwc3;
	void __iomem *usbcs_host_cfg_base;
	void __iomem *usbcs_usbint_base;
	void __iomem *usb_top_cfg_reg;
	const struct dwc3_google_driverdata *drv_data;
	struct clk_bulk_data clocks[DWC3_GOOGLE_MAX_CLOCKS];
	struct reset_control_bulk_data resets[DWC3_GOOGLE_MAX_RESETS];
	struct reset_control *usbc_non_sticky_rst;
	struct regmap_field *pmu_fields[MAX_PMU_FIELDS];
	struct usb_role_switch *role_sw;
	struct usb_role_switch *dwc3_drd_sw;
	struct usb_role_switch *phy_role_sw;
	struct device *usb_psw_pd;
	struct device *usb_top_pd;
	// GenPD Notifier to switch phy control between PMU and dwc3
	struct notifier_block usb_psw_pd_nb;
	// To maintain suspend order of pm virtual dev after glue
	struct device_link *usb_top_pd_dl;
	// To control phy runtime PM from this driver
	struct phy *u2_phy;
	struct phy *u3_phy;
	int pme_u2phy_irq;
	int pme_u3phy_irq;
	int usbdrd_irq;
	bool is_suspended;
	bool usb_on;
	bool wakeup;
	unsigned sideband_at_suspend:1;
	// To protect role variables between work and role_switch_set
	spinlock_t	role_lock;
	enum usb_role	current_role;
	enum usb_role	desired_role;
	struct kernfs_node      *desired_role_kn;
	struct delayed_work	role_switch_work;
	int force_speed;
	int usb_vc;
	struct google_icc_path *icc_path;
	u32 avg_bw;
	u32 peak_bw;
};

#endif
