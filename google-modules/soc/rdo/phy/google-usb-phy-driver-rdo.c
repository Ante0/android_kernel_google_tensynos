// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * Google's SoC-specific Glue Driver for the dwc3 USB controller
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/phy/phy.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/role.h>

#include "google-usb-phy-internal-rdo.h"
#include "dwc3_phy_fw.h"

#define GPHY_DELAY_US 10
#define GPHY_TIMEOUT_US 10000

struct google_usb_phy_instance {
	int index;
	struct phy *phy;
};

#define GOOGLE_NUM_USB_PHYS	2
#define GOOGLE_U2PHY_NUM_CLOCKS 1
#define GOOGLE_U3PHY_MAX_CLOCKS 2

extern const struct fw_patch_entry phy_fw_patch[];
extern const unsigned int phy_fw_patch_len;

struct google_usb_phy_driverdata {
	const char *u3p_clk_names[GOOGLE_U3PHY_MAX_CLOCKS];
	int	num_u3p_clks;
	const struct phy_ops google_u2phy_ops;
	const struct phy_ops google_u3phy_ops;
	void (*pm_callback)(struct device *dev, bool resume);
};

struct google_usb_phy {
	struct device *dev;
	const struct google_usb_phy_driverdata *drv_data;
	struct google_usb_phy_instance inst[GOOGLE_NUM_USB_PHYS];
	void __iomem *usb_cfg_csr_base;
	void __iomem *dp_top_csr_base;
	void __iomem *dptx_tca_regs_base;
	void __iomem *dptx_phy_regs_base;
	void __iomem *gp_ss_csr_base;
	struct clk_bulk_data u2phy_clocks[GOOGLE_U2PHY_NUM_CLOCKS];
	struct clk_bulk_data u3phy_clocks[GOOGLE_U3PHY_MAX_CLOCKS];
	struct reset_control *u2phy_reset, *u3phy_reset;
	struct typec_switch_dev *typec_switch;
	struct typec_mux_dev *typec_mux;
	int usb_vc;
	int typec_polarity;
	int typec_mux_ctrl;
	struct usb_role_switch *role_sw;
	enum usb_role role;
	int powered;
	void __iomem *phy_sram_base;
	size_t phy_sram_size;
	struct mutex lock;
};

static int wait_for_sram_init(struct google_usb_phy *gphy)
{
	DP_USBCS_PHY_CFG1 creg;

	return readl_poll_timeout(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET,
		creg.reg_value, creg.bf.phy0_sram_init_done, GPHY_DELAY_US, GPHY_TIMEOUT_US);
}

void enable_bandgap_override(struct google_usb_phy *gphy, int value)
{
	SUP_DIG_ANA_BG_OVRD_OUT bg_override;

	bg_override.reg_value = readw(gphy->dptx_phy_regs_base + SUP_DIG_ANA_BG_OVRD_OUT_OFFSET);
	bg_override.bf.BG_EN = 0;
	bg_override.bf.BG_OVRD_EN = value;
	writew(bg_override.reg_value, gphy->dptx_phy_regs_base + SUP_DIG_ANA_BG_OVRD_OUT_OFFSET);
}

static void clear_ac_cap(struct google_usb_phy *gphy)
{
	DP_PHY_CFG3 phy_cfg3;
	DP_USBCS_PHY_CFG1 phy_cfg1;

	phy_cfg3.reg_value = readl(gphy->dp_top_csr_base + DP_PHY_CFG3_OFFSET);
	phy_cfg3.bf.dp_tx0_dcc_byp_ac_cap = 0;
	phy_cfg3.bf.dp_tx1_dcc_byp_ac_cap = 0;
	phy_cfg3.bf.dp_tx2_dcc_byp_ac_cap = 0;
	phy_cfg3.bf.dp_tx3_dcc_byp_ac_cap = 0;
	writel(phy_cfg3.reg_value, gphy->dp_top_csr_base + DP_PHY_CFG3_OFFSET);

	phy_cfg1.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg1.bf.phy_ext_tx_dcc_byp_ac_cap = 0;
	writel(phy_cfg1.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
}

static void set_sram_bypass(struct google_usb_phy *gphy, int bypass)
{
	DP_USBCS_PHY_CFG1 phy_cfg1;

	phy_cfg1.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg1.bf.phy0_sram_bypass = bypass;
	writel(phy_cfg1.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
}

static void set_ext_load_done(struct google_usb_phy *gphy, int done)
{
	DP_USBCS_PHY_CFG1 phy_cfg1;

	phy_cfg1.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg1.bf.phy0_sram_ext_ld_done = done;
	writel(phy_cfg1.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
}

static void enable_usb2_phy(struct google_usb_phy *gphy)
{
	USB2PHY_CFG21 phy_cfg21;

	phy_cfg21.reg_value = readl(gphy->usb_cfg_csr_base + USB2PHY_CFG21_OFFSET);
	phy_cfg21.bf.phy_enable = 1;
	writel(phy_cfg21.reg_value, gphy->usb_cfg_csr_base + USB2PHY_CFG21_OFFSET);
}

static void disable_usb2_phy(struct google_usb_phy *gphy)
{
	USB2PHY_CFG21 phy_cfg21;

	phy_cfg21.reg_value = readl(gphy->usb_cfg_csr_base + USB2PHY_CFG21_OFFSET);
	phy_cfg21.bf.phy_enable = 0;
	writel(phy_cfg21.reg_value, gphy->usb_cfg_csr_base + USB2PHY_CFG21_OFFSET);
}

static void enable_vbus_override(struct google_usb_phy *gphy)
{
	TCA_X_REG1 ovr_sel;

	ovr_sel.reg_value = readl(gphy->dp_top_csr_base + TCA_OVERRIDE_SEL_REG1_OFFSET);
	ovr_sel.bf.sys_vbusvalid = 1;
	writel(ovr_sel.reg_value, gphy->dp_top_csr_base + TCA_OVERRIDE_SEL_REG1_OFFSET);
}

static void set_vbus_valid(struct google_usb_phy *gphy)
{
	// TODO(maurora): Consider whether accesses to gphy->role need serialization
	TCA_X_REG1 tca_override;
	int valid = gphy->role == USB_ROLE_DEVICE ? 1 : 0;

	dev_dbg(gphy->dev, "%s %d\n", __func__, valid);
	tca_override.reg_value = readl(gphy->dp_top_csr_base + TCA_OVERRIDE_REG1_OFFSET);
	tca_override.bf.sys_vbusvalid = valid;
	writel(tca_override.reg_value, gphy->dp_top_csr_base + TCA_OVERRIDE_REG1_OFFSET);
}

static void clear_dp_tca(struct google_usb_phy *gphy)
{
	TCA_TCPC tcpc;
	TCA_INTR_STS intr_sts;

	tcpc.reg_value = readl(gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET);
	tcpc.bf.tcpc_mux_control = TCPC_MUX_NO_CONN;
	tcpc.bf.tcpc_connector_orientation = 0;
	tcpc.bf.tcpc_low_power_en = 1;
	tcpc.bf.tcpc_valid = 1;
	writel(tcpc.reg_value, gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET);
	readl_poll_timeout(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET,
		intr_sts.reg_value, intr_sts.bf.xa_ack_evt == 1, GPHY_DELAY_US, GPHY_TIMEOUT_US);
}

static int program_dp_tca(struct google_usb_phy *gphy)
{
	TCA_TCPC tcpc;
	TCA_INTR_STS intr_sts;

	tcpc.reg_value = readl(gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET);
	if (gphy->powered) {
		/* For an already running Phy, we can immediately update mux and polarity settings */
		tcpc.bf.tcpc_mux_control = gphy->typec_mux_ctrl;
		tcpc.bf.tcpc_connector_orientation = gphy->typec_polarity;
	} else {
		/* For an uninitialized Phy, we set USB_ONLY and CC1. This will be updated after init */
		tcpc.bf.tcpc_mux_control = TCPC_MUX_USB_ONLY;
		tcpc.bf.tcpc_connector_orientation = 0;
	}
	tcpc.bf.tcpc_low_power_en = 0;
	tcpc.bf.tcpc_valid = 1;
	writel(tcpc.reg_value, gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET);
	return readl_poll_timeout(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET,
		intr_sts.reg_value, intr_sts.bf.xa_ack_evt == 1, GPHY_DELAY_US, GPHY_TIMEOUT_US);
}

static void lga_pm_callback(struct device *dev, bool resume)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	USBDP_TOP_CFG_REG cfg_reg;

	// write to pmgt_clk_req
	cfg_reg.reg_value = readl(gphy->dp_top_csr_base + USBDP_TOP_CFG_REG_OFFSET);
	cfg_reg.bf.pmgt_ref_clk_req_n = resume ? 1 : 0;
	writel(cfg_reg.reg_value, gphy->dp_top_csr_base + USBDP_TOP_CFG_REG_OFFSET);
}

#define GPHY_SET_IDS(bulk_array, names_array, length)\
	{ \
		int i; \
		for (i = 0; i < length; i++) \
			bulk_array[i].id = names_array[i]; \
	}

static int google_usb_phy_parse_clocks(struct google_usb_phy *gphy)
{
	int ret;
	const char *u2phy_clock_names[GOOGLE_U2PHY_NUM_CLOCKS] = {
		"usb2_phy_reset_clk"
	};

	GPHY_SET_IDS(gphy->u2phy_clocks, u2phy_clock_names, GOOGLE_U2PHY_NUM_CLOCKS);
	ret = devm_clk_bulk_get(gphy->dev, GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	if (ret)
		return ret;

	GPHY_SET_IDS(gphy->u3phy_clocks,
		gphy->drv_data->u3p_clk_names, gphy->drv_data->num_u3p_clks);
	return devm_clk_bulk_get(gphy->dev, gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
}

static int google_usb_phy_parse_resets(struct google_usb_phy *gphy)
{
	gphy->u2phy_reset = devm_reset_control_get_exclusive(gphy->dev, "usb2_phy_reset");
	if (IS_ERR(gphy->u2phy_reset)) {
		dev_err(gphy->dev, "Couldn't get usb2_phy_reset\n");
		return PTR_ERR(gphy->u2phy_reset);
	}

	gphy->u3phy_reset = devm_reset_control_get_exclusive(gphy->dev, "usb3dp_phy_reset");
	if (IS_ERR(gphy->u3phy_reset)) {
		dev_err(gphy->dev, "Couldn't get usb3dp_phy_reset\n");
		return PTR_ERR(gphy->u3phy_reset);
	}
	return 0;
}

static void google_configure_usb_qos(struct google_usb_phy *gphy)
{
	union USBCS_TOP_CFG1 top_cfg1 = {0};

	top_cfg1.bf.aruservc = gphy->usb_vc;
	top_cfg1.bf.awuservc = gphy->usb_vc;

	writel(top_cfg1.reg_value, gphy->usb_cfg_csr_base + USBCS_TOP_CFG1_OFFSET);
}

#define gphy_instance_to_gphy(gphy_inst) \
	container_of((gphy_inst), struct google_usb_phy, inst[gphy_inst->index])

static int google_u2phy_init(struct phy *phy)
{
	int err;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u2phy init\n");

	// TODO(b/293372107): Program USB2PHY_CFG19, 20, 21 registers with OTP values
	err = clk_bulk_enable(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	if (err)
		return err;

	err = reset_control_deassert(gphy->u2phy_reset);
	if (err)
		goto disable_u2phy_clocks;

	enable_usb2_phy(gphy);
	google_configure_usb_qos(gphy);
	return 0;
disable_u2phy_clocks:
	clk_bulk_disable(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return err;
}

static int google_u2phy_exit(struct phy *phy)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u2phy exit\n");
	disable_usb2_phy(gphy);
	reset_control_assert(gphy->u2phy_reset);
	clk_bulk_disable(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return 0;
}

static int google_u3phy_init(struct phy *phy)
{
	int ret;
	union GP_SS_CSR_CLK_EN clk_en;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u3phy init\n");

	clk_en.reg_value = readl(gphy->gp_ss_csr_base + GP_SS_CLK_EN_OFFSET);
	clk_en.bf.usb_force_clk_en = 1;
	writel(clk_en.reg_value, gphy->gp_ss_csr_base + GP_SS_CLK_EN_OFFSET);

	enable_vbus_override(gphy);
	set_vbus_valid(gphy);

	clear_ac_cap(gphy);

	// First initialization of phy with firmware running from ROM
	set_sram_bypass(gphy, 1);
	set_ext_load_done(gphy, 1);

	usleep_range(1000, 1100);

	ret = clk_bulk_enable(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
	if (ret)
		return ret;

	ret = reset_control_deassert(gphy->u3phy_reset);
	if (ret)
		goto disable_u3phy_clocks;

	if (gphy->drv_data->pm_callback)
		gphy->drv_data->pm_callback(gphy->dev, 1);

	usleep_range(100, 110);
	ret = wait_for_sram_init(gphy);
	if (ret)
		goto disable_u3phy_clocks;

	// Reset phy after the first initialization is done
	ret = reset_control_assert(gphy->u3phy_reset);
	if (ret)
		goto disable_u3phy_clocks;

	// Second initialization of phy with firmware running from SRAM
	set_sram_bypass(gphy, 0);
	set_ext_load_done(gphy, 0);
	usleep_range(100, 110);

	ret = reset_control_deassert(gphy->u3phy_reset);
	if (ret)
		goto disable_u3phy_clocks;

	ret = wait_for_sram_init(gphy);
	if (ret)
		goto disable_u3phy_clocks;

	if (gphy->phy_sram_base) {
		int i;

		dev_dbg(gphy->dev, "Updating SRAM\n");
		usleep_range(100, 110);
		for (i = 0; i < phy_fw_patch_len; i++) {
			if (phy_fw_patch[i].addr < gphy->phy_sram_size)
				writew(phy_fw_patch[i].val, gphy->phy_sram_base + phy_fw_patch[i].addr);
		}

		usleep_range(100, 110);
	}

	set_ext_load_done(gphy, 1);
	usleep_range(100, 110);

	mutex_lock(&gphy->lock);
	// This program_dp_tca call is already mutually exclusive with other invocations:
	// google_u3phy_power_on is called in the same thread after phy init is done
	// google_usb_set_orientation only calls the func after u3phy_power_on is called
	// Asserting that we don't really need to hold this mutex here:
	WARN_ON(gphy->powered);
	ret = program_dp_tca(gphy);
	mutex_unlock(&gphy->lock);
	if (ret)
		goto disable_u3phy_clocks;

	google_configure_usb_qos(gphy);
	return 0;
disable_u3phy_clocks:
	clk_bulk_disable(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
	return ret;
}

static int google_u3phy_exit(struct phy *phy)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u3phy exit\n");
	mutex_lock(&gphy->lock);
	clear_dp_tca(gphy);
	gphy->powered = 0;
	mutex_unlock(&gphy->lock);

	if (gphy->drv_data->pm_callback)
		gphy->drv_data->pm_callback(gphy->dev, 0);
	reset_control_assert(gphy->u3phy_reset);
	clk_bulk_disable(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
	return 0;
}

static int google_u3phy_power_on(struct phy *phy)
{
	int ret;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	mutex_lock(&gphy->lock);
	gphy->powered = 1;
	dev_dbg(gphy->dev, "%s %d\n", __func__, gphy->typec_polarity);
	ret = program_dp_tca(gphy);
	mutex_unlock(&gphy->lock);
	return ret;
}

static const struct dev_pm_ops google_usb_phy_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, NULL)
	SET_RUNTIME_PM_OPS(NULL, NULL, NULL)
};

static struct phy *google_usb_phy_xlate(struct device *dev,
	struct of_phandle_args *args)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	if (GOOGLE_NUM_USB_PHYS > (unsigned int)args->args[0])
		return gphy->inst[args->args[0]].phy;

	return ERR_PTR(-ENODEV);
}

static char *orientation_name(enum typec_orientation orientation)
{
	switch (orientation) {
	case TYPEC_ORIENTATION_NORMAL:
		return "CC1";
	case TYPEC_ORIENTATION_REVERSE:
		return "CC2";
	default:
		return "None";
	}
}

static int google_usb_set_orientation(struct typec_switch_dev *sw,
				enum typec_orientation orientation)
{
	int ret = 0;
	struct google_usb_phy *gphy = typec_switch_get_drvdata(sw);

	dev_dbg(gphy->dev, "%s: %s\n", __func__, orientation_name(orientation));
	mutex_lock(&gphy->lock);
	gphy->typec_polarity = (orientation == TYPEC_ORIENTATION_REVERSE) ? 1 : 0;
	if (orientation != TYPEC_ORIENTATION_NONE && gphy->powered)
		ret = program_dp_tca(gphy);
	mutex_unlock(&gphy->lock);
	return ret;
}

static char *mux_mode_name(unsigned long mode)
{
	switch (mode) {
	case TYPEC_STATE_SAFE:
		return "TYPEC_STATE_SAFE";
	case TYPEC_STATE_USB:
		return "TYPEC_STATE_USB";
	case TYPEC_DP_STATE_C:
		return "TYPEC_DP_STATE_C";
	case TYPEC_DP_STATE_E:
		return "TYPEC_DP_STATE_E";
	case TYPEC_DP_STATE_D:
		return "TYPEC_DP_STATE_D";
	default:
		return "Unknown";
	}
}

static int google_usb_set_mux_control(struct typec_mux_dev *mux, struct typec_mux_state *state)
{
	int ret = 0;
	struct google_usb_phy *gphy = typec_mux_get_drvdata(mux);

	dev_dbg(gphy->dev, "%s: %s\n", __func__, mux_mode_name(state->mode));
	mutex_lock(&gphy->lock);
	switch (state->mode) {
	case TYPEC_STATE_SAFE:
		fallthrough;
	case TYPEC_STATE_USB:
		gphy->typec_mux_ctrl = TCPC_MUX_USB_ONLY;
		break;
	case TYPEC_DP_STATE_C:
		fallthrough;
	case TYPEC_DP_STATE_E:
		gphy->typec_mux_ctrl = TCPC_MUX_DP_ONLY;
		break;
	case TYPEC_DP_STATE_D:
		gphy->typec_mux_ctrl = TCPC_MUX_USB_DP;
		break;
	default:
		break;
	}
	if (gphy->powered)
		ret = program_dp_tca(gphy);

	mutex_unlock(&gphy->lock);

	return ret;
}

static int u3phy_usb_role_switch_set(struct usb_role_switch *sw,
					 enum usb_role role)
{
	struct google_usb_phy *gphy = usb_role_switch_get_drvdata(sw);

	gphy->role = role;
	set_vbus_valid(gphy);
	return 0;
}

static enum usb_role u3phy_usb_role_switch_get(struct usb_role_switch *sw)
{
	struct google_usb_phy *gphy = usb_role_switch_get_drvdata(sw);

	return gphy->role;
}

static int google_u3phy_setup_role_switch(struct google_usb_phy *gphy)
{
	struct usb_role_switch_desc gphy_role_switch = {NULL};

	gphy_role_switch.fwnode = dev_fwnode(gphy->dev);
	gphy_role_switch.set = u3phy_usb_role_switch_set;
	gphy_role_switch.get = u3phy_usb_role_switch_get;
	gphy_role_switch.allow_userspace_control = true;
	gphy_role_switch.driver_data = gphy;
	gphy->role_sw = usb_role_switch_register(gphy->dev, &gphy_role_switch);
	if (IS_ERR(gphy->role_sw))
		return PTR_ERR(gphy->role_sw);

	return 0;
}

static void google_usb_phy_destroy_mutex(void *lock)
{
	mutex_destroy(lock);
}

static int google_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct google_usb_phy *gphy;
	struct device_node *node;
	struct phy_provider *provider;
	struct resource *phy_sram_res;
	struct typec_switch_desc tc_sw_desc = { };
	struct typec_mux_desc tc_mux_desc = { };
	int ret;

	dev = &pdev->dev;
	node = dev->of_node;
	dev_dbg(dev, "Google USB phy driver probe\n");
	if (!node) {
		dev_err(dev, "Missing USB phy node in dts\n");
		return -ENODEV;
	}
	gphy = devm_kzalloc(dev, sizeof(*gphy), GFP_KERNEL);
	if (!gphy)
		return -ENOMEM;
	gphy->drv_data = of_device_get_match_data(dev);

	gphy->dev = dev;
	dev_set_drvdata(dev, gphy);

	gphy->powered = 0;
	gphy->typec_polarity = 0;
	gphy->typec_mux_ctrl = TCPC_MUX_USB_ONLY;

	mutex_init(&gphy->lock);
	ret = devm_add_action_or_reset(dev, google_usb_phy_destroy_mutex, &gphy->lock);
	if (ret)
		return ret;

	gphy->inst[0].phy = devm_phy_create(dev, NULL, &gphy->drv_data->google_u2phy_ops);
	gphy->inst[0].index = 0;
	if (IS_ERR(gphy->inst[0].phy)) {
		dev_err(dev, "Couldn't create USB2 phy\n");
		return PTR_ERR(gphy->inst[0].phy);
	}
	phy_set_drvdata(gphy->inst[0].phy, &gphy->inst[0]);

	gphy->inst[1].phy = devm_phy_create(dev, NULL, &gphy->drv_data->google_u3phy_ops);
	gphy->inst[1].index = 1;
	if (IS_ERR(gphy->inst[1].phy)) {
		dev_err(dev, "Couldn't create USB3 phy\n");
		return PTR_ERR(gphy->inst[1].phy);
	}
	phy_set_drvdata(gphy->inst[1].phy, &gphy->inst[1]);

	gphy->usb_cfg_csr_base = devm_platform_ioremap_resource_byname(pdev, "usb_cfg_csr");
	if (IS_ERR(gphy->usb_cfg_csr_base))
		return PTR_ERR(gphy->usb_cfg_csr_base);

	gphy->dp_top_csr_base = devm_platform_ioremap_resource_byname(pdev, "dp_top_csr");
	if (IS_ERR(gphy->dp_top_csr_base))
		return PTR_ERR(gphy->dp_top_csr_base);

	gphy->dptx_tca_regs_base = devm_platform_ioremap_resource_byname(pdev, "dptx_tca_regs");
	if (IS_ERR(gphy->dptx_tca_regs_base))
		return PTR_ERR(gphy->dptx_tca_regs_base);

	gphy->dptx_phy_regs_base = devm_platform_ioremap_resource_byname(pdev, "dptx_phy_regs");
	if (IS_ERR(gphy->dptx_phy_regs_base))
		return PTR_ERR(gphy->dptx_phy_regs_base);

	gphy->gp_ss_csr_base = devm_platform_ioremap_resource_byname(pdev, "gp_ss_csr");
	if (IS_ERR(gphy->gp_ss_csr_base))
		return PTR_ERR(gphy->gp_ss_csr_base);

	if (device_property_read_u32(&pdev->dev, "usb-vc", &gphy->usb_vc)) {
		dev_info(dev, "usb-vc undefined, defaulting to 0\n");
		gphy->usb_vc = 0;
	}

	phy_sram_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_sram");
	if (phy_sram_res) {
		gphy->phy_sram_base = devm_ioremap_resource(dev, phy_sram_res);
		if (IS_ERR(gphy->phy_sram_base))
			return PTR_ERR(gphy->phy_sram_base);

		gphy->phy_sram_size = resource_size(phy_sram_res);
		dev_dbg(dev, "U3Phy SRAM %llx-%llx\n", phy_sram_res->start, phy_sram_res->end);
	}

	ret = google_usb_phy_parse_clocks(gphy);
	if (ret)
		return ret;

	ret = google_usb_phy_parse_resets(gphy);
	if (ret)
		return ret;

	ret = clk_bulk_prepare(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
	if (ret)
		goto unprepare_u2phy_clocks;

	tc_sw_desc.fwnode = dev_fwnode(dev);
	tc_sw_desc.drvdata = gphy;
	tc_sw_desc.name = fwnode_get_name(dev_fwnode(dev));
	tc_sw_desc.set = google_usb_set_orientation;

	gphy->typec_switch = typec_switch_register(dev, &tc_sw_desc);
	if (IS_ERR(gphy->typec_switch)) {
		ret = PTR_ERR(gphy->typec_switch);
		dev_err(dev, "TypeC Switch Register Failed:%d\n", ret);
		goto unprepare_u3phy_clocks;
	}

	tc_mux_desc.fwnode = dev_fwnode(dev);
	tc_mux_desc.drvdata = gphy;
	tc_mux_desc.name = fwnode_get_name(dev_fwnode(dev));
	tc_mux_desc.set = google_usb_set_mux_control;

	gphy->typec_mux = typec_mux_register(dev, &tc_mux_desc);
	if (IS_ERR(gphy->typec_mux)) {
		ret = PTR_ERR(gphy->typec_mux);
		dev_err(dev, "TypeC Mux Register Failed:%d\n", ret);
		goto unregister_typec_switch;
	}

	ret = google_u3phy_setup_role_switch(gphy);
	if (ret)
		goto unregister_typec_mux;

	provider = devm_of_phy_provider_register(dev, google_usb_phy_xlate);
	if (IS_ERR(provider)) {
		ret = PTR_ERR(provider);
		dev_err(dev, "Failed to register provider\n");
		goto unregister_role_switch;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
unregister_role_switch:
	usb_role_switch_unregister(gphy->role_sw);
unregister_typec_mux:
	typec_mux_unregister(gphy->typec_mux);
unregister_typec_switch:
	typec_switch_unregister(gphy->typec_switch);
unprepare_u3phy_clocks:
	clk_bulk_unprepare(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
unprepare_u2phy_clocks:
	clk_bulk_unprepare(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return ret;
}

static void google_usb_phy_remove(struct platform_device *pdev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "Google USB Phy driver remove\n");
	typec_switch_unregister(gphy->typec_switch);
	usb_role_switch_unregister(gphy->role_sw);
	typec_mux_unregister(gphy->typec_mux);
}

static const struct google_usb_phy_driverdata lga_drvdata = {
	.u3p_clk_names = {
		"usb3dp_phy_reset_clk"
	},
	.num_u3p_clks = 1,
	.google_u2phy_ops = {
		.init = NULL,
		.exit = NULL,
		.owner = THIS_MODULE,
	},
	.google_u3phy_ops = {
		.init = NULL,
		.power_on = NULL,
		.exit = NULL,
		.owner = THIS_MODULE,
	},
	.pm_callback = lga_pm_callback,
};

static const struct google_usb_phy_driverdata rdo_drvdata = {
	.u3p_clk_names = {
		"u3phy_ref_clk",
		"usb3dp_phy_reset_clk"
	},
	.num_u3p_clks = 2,
	.google_u2phy_ops = {
		.init = google_u2phy_init,
		.exit = google_u2phy_exit,
		.owner = THIS_MODULE,
	},
	.google_u3phy_ops = {
		.init = google_u3phy_init,
		.power_on = google_u3phy_power_on,
		.exit = google_u3phy_exit,
		.owner = THIS_MODULE,
	},
	.pm_callback = NULL,
};

static const struct of_device_id google_usb_phy_match[] = {
	{
		.compatible = "google,usb_phy-lga",
		.data = &lga_drvdata,
	}, {
		.compatible = "google,usb_phy",
		.data = &rdo_drvdata,
	}, {
	}
};

static struct platform_driver google_usb_phy_driver = {
	.probe = google_usb_phy_probe,
	.remove = google_usb_phy_remove,
	.driver = {
		.name = "google_usb_phy",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&google_usb_phy_dev_pm_ops),
		.of_match_table = google_usb_phy_match,
	}
};

module_platform_driver(google_usb_phy_driver);
MODULE_AUTHOR("GOOGLE LLC");
MODULE_DESCRIPTION("Google USB PHY Driver");
MODULE_LICENSE("GPL");
