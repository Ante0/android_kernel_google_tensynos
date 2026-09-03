// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * Google's SoC-specific Glue Driver for the dwc3 USB controller
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-consumer.h>
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
#include <linux/firmware.h>
#include <soc/google/google-usb-phy.h>
#include <soc/google/google-usb-phy-dp.h>

#include "google-usb-phy-dp-tune.h"
#include "google-usb-phy-internal.h"
#include "dwc3_phy_fw.h"

#define USB_PHY_FW_BIN_NAME "usb_phy_fw.bin"
#define GPHY_DELAY_US 10
/* TODO(rdbabiera): timeout is set to 10x of old value for USB->DP TCA switching */
#define GPHY_TIMEOUT_US 2500000
#define GPHY_TX_RX_ACK_DELAY_US 5000
#define GPHY_TX_RX_ACK_TIMEOUT_US 100000

/* 0.500 ms, 10 ms */
#define GPHY_DP_WA_DELAY_US 500
#define GPHY_DP_WA_TIMEOUT_US 10000

/* Phy AON_FW_VERSION parsing*/
#define AON_FW_VERSION_0(a,b,c) \
	(((a & 0xf) << 12) | ((b & 0xff) << 4) | (c & 0xf))
#define AON_FW_VERSION_a(ver_0) ((ver_0 >> 12) & 0xf)
#define AON_FW_VERSION_b(ver_0) ((ver_0 >> 4) & 0xff)
#define AON_FW_VERSION_c(ver_0) (ver_0 & 0xf)
#define AON_FW_VERSION_DAY(ver_1) ((ver_1 >> 7) & 0x1f)
#define AON_FW_VERSION_MONTH(ver_1) ((ver_1 >> 3) & 0xf)
#define AON_FW_VERSION_YEAR(ver_1) ((ver_1 & 0x7) + 2018)

#define AON_FW_VERSION_2_27_0 AON_FW_VERSION_0(2, 27, 0)

#define USB_PHY_PARA_LST_DPTX		phy_tune_params
#define USB_PHY_PARA_NUM_DPTX		num_phy_tune_params
#define USB_PHY_PARA_LST_DPTOP		phy_tune_params_g1
#define USB_PHY_PARA_NUM_DPTOP		num_phy_tune_params_g1

#define USB_PHY_LVL_OVRD_IN_NAME	"sup_dig_lvl_ovrd_in_val"

#define USB_PHY_READ_DPTX(_offset)		readw(gphy->dptx_phy_regs_base + _offset)
#define USB_PHY_READ_DPTOP(_offset)		readl(gphy->dp_top_csr_base + _offset)

#define USB_PHY_WRITE_DPTX(_offset, _value) \
	writew(_value, gphy->dptx_phy_regs_base + _offset)
#define USB_PHY_WRITE_DPTOP(_offset, _value) \
	writel(_value, gphy->dp_top_csr_base + _offset)

#define USB_PHY_PARA_CHK_DPTX(_name, _offset)	(strcmp(params[i].name, #_name) == 0)
#define USB_PHY_PARA_CHK_DPTOP(_name, _offset)	(_offset == params[i].offset)

#define GOOGLE_USB_PHY_REG_ATTR(_name, _offset, _reg_group)			\
static ssize_t _name##_show(struct device *dev,					\
				struct device_attribute *attr, char *buf)	\
{										\
	struct google_usb_phy *gphy = dev_get_drvdata(dev);			\
	u32 res;								\
										\
	mutex_lock(&gphy->lock);						\
	if (gphy->phy_state != COMBO_PHY_TCA_READY) {				\
		mutex_unlock(&gphy->lock);					\
		return -EPERM;							\
	}									\
	res = (u32)USB_PHY_READ_##_reg_group(_offset);				\
	mutex_unlock(&gphy->lock);						\
										\
	return sysfs_emit(buf, "offset=0x%X, value=0x%08X\n", _offset, res);	\
}										\
										\
static ssize_t _name##_store(struct device *dev,				\
				struct device_attribute *attr,			\
				const char *buf, size_t n)			\
{										\
	struct google_usb_phy *gphy = dev_get_drvdata(dev);			\
	u32 res;								\
										\
	if (kstrtoint(buf, 16, &res) < 0)					\
		return -EINVAL;							\
										\
	struct phy_tune_param *params = gphy->USB_PHY_PARA_LST_##_reg_group;	\
	for (int i = 0; i < gphy->USB_PHY_PARA_NUM_##_reg_group; i++) {		\
		if (USB_PHY_PARA_CHK_##_reg_group(_name##_val, _offset)) {	\
			params[i].value = res;					\
			break;							\
		}								\
	}									\
										\
	mutex_lock(&gphy->lock);						\
	if (gphy->phy_state != COMBO_PHY_TCA_READY) {				\
		mutex_unlock(&gphy->lock);					\
		return -EPERM;							\
	}									\
	USB_PHY_WRITE_##_reg_group(_offset, res);				\
	mutex_unlock(&gphy->lock);						\
										\
	return n;								\
}										\
static DEVICE_ATTR_RW(_name)

#define GOOGLE_USB_PHY_COEFF_ATTR(_name, _reg_group, _field, _type)		\
static ssize_t _field##_show(struct device *dev,				\
				struct device_attribute *attr, char *buf)	\
{										\
	struct google_usb_phy *gphy = dev_get_drvdata(dev);			\
	u32 res;								\
	int len = 0;								\
										\
	mutex_lock(&gphy->lock);						\
	if (gphy->phy_state != COMBO_PHY_TCA_READY) {				\
		mutex_unlock(&gphy->lock);					\
		return -EPERM;							\
	}									\
	res = (u32) USB_PHY_READ_##_reg_group(DP_##_type##_OFFSET);		\
	mutex_unlock(&gphy->lock);						\
										\
	_type _name = {0};							\
	_name.reg_value = res;							\
	len += sysfs_emit_at(buf, len, "0x%02X,0x%02X (%2d,%2d)\n",		\
			_name.bf.protocol1_ext_tx_eq_##_field##_g1_lane0,	\
			_name.bf.protocol1_ext_tx_eq_##_field##_g1_lane1,	\
			_name.bf.protocol1_ext_tx_eq_##_field##_g1_lane0,	\
			_name.bf.protocol1_ext_tx_eq_##_field##_g1_lane1);	\
										\
	return len;								\
}										\
										\
static ssize_t _field##_store(struct device *dev,				\
				struct device_attribute *attr,			\
				const char *buf, size_t n)			\
{										\
	struct google_usb_phy *gphy = dev_get_drvdata(dev);			\
	u32 res;								\
	_type _name = {0};							\
										\
	if (kstrtoint(buf, 10, &res) < 0)					\
		return -EINVAL;							\
										\
	struct phy_tune_param *params = gphy->USB_PHY_PARA_LST_##_reg_group;	\
	for (int i = 0; i < gphy->USB_PHY_PARA_NUM_##_reg_group; i++) {		\
		if (params[i].offset == DP_##_type##_OFFSET) {			\
			_name.reg_value = params[i].value;			\
			_name.bf.protocol1_ext_tx_eq_##_field##_g1_lane0 = res;	\
			_name.bf.protocol1_ext_tx_eq_##_field##_g1_lane1 = res;	\
			params[i].value = _name.reg_value;			\
			break;							\
		}								\
	}									\
										\
	mutex_lock(&gphy->lock);						\
	if (gphy->phy_state != COMBO_PHY_TCA_READY) {				\
		mutex_unlock(&gphy->lock);					\
		return -EPERM;							\
	}									\
	USB_PHY_WRITE_##_reg_group(DP_##_type##_OFFSET, _name.reg_value);	\
	mutex_unlock(&gphy->lock);						\
										\
	return n;								\
}										\
static DEVICE_ATTR_RW(_field)

#define GOOGLE_USB_PHY_PARA_ATTR(_name, _reg_group, _field, _type)		\
static ssize_t _field##_show(struct device *dev,				\
				struct device_attribute *attr, char *buf)	\
{										\
	struct google_usb_phy *gphy = dev_get_drvdata(dev);			\
	u32 res;								\
	int len = 0;								\
										\
	mutex_lock(&gphy->lock);						\
	if (gphy->phy_state != COMBO_PHY_TCA_READY) {				\
		mutex_unlock(&gphy->lock);					\
		return -EPERM;							\
	}									\
	res = (u32) USB_PHY_READ_##_reg_group(SUP_DIG_##_type);			\
	mutex_unlock(&gphy->lock);						\
										\
	_type _name = {0};							\
	_name.reg_value = res;							\
	len += sysfs_emit_at(buf, len, "0x%02X (%2d)\n",			\
					_name.bf.tx_##_field##_lvl,		\
					_name.bf.tx_##_field##_lvl);		\
										\
	return len;								\
}										\
										\
static ssize_t _field##_store(struct device *dev,				\
				struct device_attribute *attr,			\
				const char *buf, size_t n)			\
{										\
	struct google_usb_phy *gphy = dev_get_drvdata(dev);			\
	u32 res;								\
	_type _name = {0};							\
										\
	if (kstrtoint(buf, 10, &res) < 0)					\
		return -EINVAL;							\
										\
	struct phy_tune_param *params = gphy->USB_PHY_PARA_LST_##_reg_group;	\
	for (int i = 0; i < gphy->USB_PHY_PARA_NUM_##_reg_group; i++) {		\
		if (strcmp(params[i].name, USB_PHY_##_type##_NAME) == 0) {	\
			_name.reg_value = params[i].value;			\
			_name.bf.tx_##_field##_lvl = res;			\
			params[i].value = _name.reg_value;			\
			break;							\
		}								\
	}									\
	mutex_lock(&gphy->lock);						\
	if (gphy->phy_state != COMBO_PHY_TCA_READY) {				\
		mutex_unlock(&gphy->lock);					\
		return -EPERM;							\
	}									\
	USB_PHY_WRITE_##_reg_group(SUP_DIG_##_type, _name.reg_value);		\
	mutex_unlock(&gphy->lock);						\
										\
	return n;								\
}										\
static DEVICE_ATTR_RW(_field)

static unsigned int fw_name_to_version(const char *name)
{
	unsigned int ver_a, ver_b, ver_c;
	int ret;

	ret = sscanf(name, "usb_phy_fw_%u.%u.%u.bin", &ver_a, &ver_b, &ver_c);
	if (ret == 3)
		return AON_FW_VERSION_0(ver_a, ver_b, ver_c);
	else
		return 0;
}

/*
 * dp_tune_ovrd_en is true when the PHY driver uses the tuning override registers for DP
 * TX EQ values. When false, the PHY driver uses the TX control registers to map vs/pe
 * to the hardware defined levels.
 */
static bool dp_tune_ovrd_en;
module_param(dp_tune_ovrd_en, bool, 0644);
MODULE_PARM_DESC(dp_tune_ovrd_en, "Use tuning override registers for DP TX EQ values.");

enum google_usb_otp_type {
	GOOGLE_USB2_PHY_CFG_PLL_CPBIAS_CNTRL,
	GOOGLE_USB2_PHY_CFG_PLL_FB_DIV,
	GOOGLE_USB2_PHY_CFG_PLL_GMP_CNTRL,
	GOOGLE_USB2_PHY_CFG_PLL_INT_CNTRL,
	GOOGLE_USB2_PHY_CFG_PLL_PROP_CNTRL,
	GOOGLE_USB2_PHY_CFG_PLL_REF_DIV,
	GOOGLE_USB2_PHY_CFG_PLL_VCO_CNTRL,
	GOOGLE_USB2_PHY_CFG_PLL_VREF_TUNE,
	GOOGLE_USB2_PHY_CFG_RX_EQ_CTLE,
	GOOGLE_USB2_PHY_CFG_RX_HS_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_FSLS_SLEW_RATE_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_FSLS_VREF_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_HS_VREF_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_HS_XV_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_PREEMP_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_RES_TUNE,
	GOOGLE_USB2_PHY_CFG_TX_RISE_TUNE,
	GOOGLE_USB2_REXT,
	GOOGLE_USB3_REXT,
	GOOGLE_USB_NUM_OTPS
};

struct google_usb_phy_instance {
	int index;
	struct phy *phy;
};

#define GOOGLE_NUM_USB_PHYS	3
#define GOOGLE_U2PHY_NUM_CLOCKS 1
#define GOOGLE_U3PHY_MAX_CLOCKS 2

/*
 * combo_phy_state
 *	COMBO_PHY_IDLE: The comboPHY has been torn down and USB3 has not completed
 *			bringup
 *	COMBO_PHY_INIT_DONE: combo_phy_bringup_locked has been completed.
 *	COMBO_PHY_TCA_READY: The PoR => NC transition has been completed, the TCA
 *			     can be moved into USB, and DP afterwards if applicable.
 */
enum combo_phy_state {
	COMBO_PHY_IDLE,
	COMBO_PHY_INIT_DONE,
	COMBO_PHY_TCA_READY,
};

#define COMBO_PHY_USER_USB3		BIT(0)
#define COMBO_PHY_USER_DP		BIT(1)

#define COMBO_PHY_MUX_USB(_mode_)	(((_mode_) == TYPEC_STATE_SAFE) ||\
					 ((_mode_) == TYPEC_STATE_USB))
#define COMBO_PHY_MUX_DP(_mode_)	(((_mode_) == TYPEC_DP_STATE_C) ||\
					 ((_mode_) == TYPEC_DP_STATE_D) ||\
					 ((_mode_) == TYPEC_DP_STATE_E))
#define COMBO_PHY_MUX_DP_ONLY(_mode_)	(((_mode_) == TYPEC_DP_STATE_C) ||\
					 ((_mode_) == TYPEC_DP_STATE_E))

/* Use safe number for custom mode */
#define TYPEC_STATE_NO_CONN		10

/* Phy Workarounds */
#define U3_PHY_FORCE_SS_RXDET_DISABLE	BIT(0)
#define U3_PHY_FORCE_USB31_CONNECTED	BIT(1)

struct google_usb_phy_driverdata {
	const char *u3p_clk_names[GOOGLE_U3PHY_MAX_CLOCKS];
	int	num_u3p_clks;
	const struct phy_ops google_u2phy_ops;
	const struct phy_ops google_u3phy_ops;
	const struct phy_ops google_dpphy_ops;
	void (*pm_callback)(struct device *dev, bool resume);
	void (*program_usb2_otps)(struct device *dev);
	void (*program_usb3_otps)(struct device *dev);
	void (*set_power_config)(struct device *dev);
	void (*typec_disable_ack_set)(struct device *dev, int val);
	u8 mplla_coarse_start_0_val;
};

struct google_usb_phy_otpdata {
	u8 valid;
	u8 enable;
	u32 data;
};

struct phy_tune_param {
	char name[48];
	u32 offset;
	u32 value;
};

struct aux_tune_params {
	u32 aux_ctrl : 4;
	u32 aux_dp_dn_swap : 1;
	u32 aux_hys_tune : 2;
	u32 aux_pwdnb : 1;
	u32 aux_vod_tune : 1;
	u32 reserved_32_9 : 23;
};

struct google_usb_phy {
	struct device *dev;
	const struct google_usb_phy_driverdata *drv_data;
	struct google_usb_phy_instance inst[GOOGLE_NUM_USB_PHYS];
	void __iomem *usb2_cfg_csr_base;
	void __iomem *dp_top_csr_base;
	void __iomem *dptx_tca_regs_base;
	void __iomem *dptx_phy_regs_base;
	void __iomem *gp_ss_csr_base;
	void __iomem *dp_pipe_lane0_regs_base;
	void __iomem *dp_pipe_lane1_regs_base;
	void __iomem *dwc3_lcsr_deemph_base;
	void __iomem *usb2_ss_base;
	void __iomem *upcs_config_reg;
	struct clk_bulk_data u2phy_clocks[GOOGLE_U2PHY_NUM_CLOCKS];
	struct clk_bulk_data u3phy_clocks[GOOGLE_U3PHY_MAX_CLOCKS];
	struct clk *phy_fw_clk;
	struct reset_control *u2phy_reset, *u3phy_reset;
	struct typec_switch_dev *typec_switch;
	struct typec_mux_dev *typec_mux;
	int typec_polarity;
	unsigned long typec_mode;
	struct usb_role_switch *role_sw;
	enum usb_role role;
	int phy_users;
	enum combo_phy_state phy_state;
	void __iomem *phy_sram_base;
	size_t phy_sram_size;
	const struct firmware *fw;
	const char *fw_name;
	struct mutex lock;
	struct google_usb_phy_otpdata otp_data[GOOGLE_USB_NUM_OTPS];
	u8 dp_tune_main[DP_TUNE_NUM_RATES][DP_TUNE_NUM_VS][DP_TUNE_NUM_PE];
	u8 dp_tune_pre[DP_TUNE_NUM_RATES][DP_TUNE_NUM_VS][DP_TUNE_NUM_PE];
	u8 dp_tune_post[DP_TUNE_NUM_RATES][DP_TUNE_NUM_VS][DP_TUNE_NUM_PE];
	struct dp_tune_kobj_data dp_tune_kobjs;
	struct dentry *debugfs_root;
	int num_phy_tune_params;
	struct phy_tune_param *phy_tune_params;
	int num_phy_tune_params_g1;
	struct phy_tune_param *phy_tune_params_g1;
	u32 phy_workarounds;
	bool aux_tune_ovrd_en;
	struct aux_tune_params aux_tune_params_ovrd;
};

struct google_usb_phy_otp_names {
	char *otp_enable_string;
	char *otp_string;
};

static struct google_usb_phy_otp_names otp_names[GOOGLE_USB_NUM_OTPS] = {
	[GOOGLE_USB2_PHY_CFG_PLL_CPBIAS_CNTRL] = {
		.otp_enable_string = "usb2_phy_cfg_pll_cpbias_cntrl_enable",
		.otp_string = "usb2_phy_cfg_pll_cpbias_cntrl"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_FB_DIV] = {
		.otp_enable_string = "usb2_phy_cfg_pll_fb_div_enable",
		.otp_string = "usb2_phy_cfg_pll_fb_div"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_GMP_CNTRL] = {
		.otp_enable_string = "usb2_phy_cfg_pll_gmp_cntrl_enable",
		.otp_string = "usb2_phy_cfg_pll_gmp_cntrl"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_INT_CNTRL] = {
		.otp_enable_string = "usb2_phy_cfg_pll_int_cntrl_enable",
		.otp_string = "usb2_phy_cfg_pll_int_cntrl"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_PROP_CNTRL] = {
		.otp_enable_string = "usb2_phy_cfg_pll_prop_cntrl_enable",
		.otp_string = "usb2_phy_cfg_pll_prop_cntrl"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_REF_DIV] = {
		.otp_enable_string = "usb2_phy_cfg_pll_ref_div_enable",
		.otp_string = "usb2_phy_cfg_pll_ref_div"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_VCO_CNTRL] = {
		.otp_enable_string = "usb2_phy_cfg_pll_vco_cntrl_enable",
		.otp_string = "usb2_phy_cfg_pll_vco_cntrl"
	},
	[GOOGLE_USB2_PHY_CFG_PLL_VREF_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_pll_vref_tune_enable",
		.otp_string = "usb2_phy_cfg_pll_vref_tune"
	},
	[GOOGLE_USB2_PHY_CFG_RX_EQ_CTLE] = {
		.otp_enable_string = "usb2_phy_cfg_rx_eq_ctle_enable",
		.otp_string = "usb2_phy_cfg_rx_eq_ctle"
	},
	[GOOGLE_USB2_PHY_CFG_RX_HS_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_rx_hs_tune_enable",
		.otp_string = "usb2_phy_cfg_rx_hs_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_FSLS_SLEW_RATE_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_fsls_slew_rate_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_fsls_slew_rate_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_FSLS_VREF_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_fsls_vref_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_fsls_vref_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_HS_VREF_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_hs_vref_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_hs_vref_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_HS_XV_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_hs_xv_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_hs_xv_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_PREEMP_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_preemp_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_preemp_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_RES_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_res_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_res_tune"
	},
	[GOOGLE_USB2_PHY_CFG_TX_RISE_TUNE] = {
		.otp_enable_string = "usb2_phy_cfg_tx_rise_tune_enable",
		.otp_string = "usb2_phy_cfg_tx_rise_tune"
	},
	[GOOGLE_USB2_REXT] = {
		.otp_enable_string = "usb2_rext_external_mode",
		.otp_string = "usb2_rext_ctrl"
	},
	[GOOGLE_USB3_REXT] = {
		.otp_enable_string = "usb3_rext_external_mode",
		.otp_string = "usb3_rext_ctrl"
	}
};

static int wait_for_sram_init(struct google_usb_phy *gphy)
{
	DP_USBCS_PHY_CFG1 creg;

	return readl_poll_timeout(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET,
		creg.reg_value, creg.bf.phy0_sram_init_done, GPHY_DELAY_US, GPHY_TIMEOUT_US);
}

static void phy_debug_dump(struct google_usb_phy *gphy)
{
	dev_err(gphy->dev, "TCA_TCPC = 0x%x, TCA_INTR_STS = 0x%x, TCA_PSTATE_0 = 0x%x, TCA_PSTATE_1 = 0x%x, TCA_PSTATE_2 = 0x%x, TCA_CTRLSYNCMODE_DBG0 = 0x%x, TCA_CTRLSYNCMODE_DBG1 = 0x%x, TCA_CTRLSYNCMODE_DBG2 = 0x%x\n",
		readl(gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_PSTATE_0_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_PSTATE_1_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_PSTATE_2_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG0_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG1_OFFSET),
		readl(gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG2_OFFSET));

#ifdef GOOGLE_PHY_DEBUG
	TCA_PSTATE_1 tca_pstate_1;
	/* Skip dumping phy debug registers if phy is in P4 state */
	tca_pstate_1.reg_value = readl(gphy->dptx_tca_regs_base + TCA_PSTATE_1_OFFSET);
	if (tca_pstate_1.bf.usb32_lane0_powerdown == 0xc)
		return;

	dev_err(gphy->dev, "RAWLANE0_DIG_TX_PCS_XF_OUT_0 = 0x%x, RAWLANE1_DIG_RX_PCS_XF_OUT_0 = 0x%x, RAWLANE0_DIG_TX_CTL_FW_PWRUP_DONE = 0x%x, RAWLANE1_DIG_TX_CTL_FW_PWRUP_DONE = 0x%x, RAWCMN_DIG_FW_PWRUP_DONE = 0x%x, SUP_DIG_MPLLA_MPLL_PWR_CTL_STAT = 0x%x, LANE1_DIG_RX_VCOCAL_RX_VCO_STAT_0 = 0x%x, LANE1_DIG_RX_VCOCAL_RX_VCO_STAT_1 = 0x%x, LANE1_DIG_RX_VCOCAL_RX_VCO_STAT_2 = 0x%x\n",
		readw(gphy->dptx_phy_regs_base + RAWLANE0_DIG_TX_PCS_XF_OUT_0_OFFSET),
		readw(gphy->dptx_phy_regs_base + RAWLANE1_DIG_RX_PCS_XF_OUT_0_OFFSET),
		readw(gphy->dptx_phy_regs_base + RAWLANE0_DIG_TX_CTL_FW_PWRUP_DONE_OFFSET),
		readw(gphy->dptx_phy_regs_base + RAWLANE1_DIG_TX_CTL_FW_PWRUP_DONE_OFFSET),
		readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_FW_PWRUP_DONE_OFFSET),
		readw(gphy->dptx_phy_regs_base + SUP_DIG_MPLLA_MPLL_PWR_CTL_STAT_OFFSET),
		readw(gphy->dptx_phy_regs_base + LANE1_DIG_RX_VCOCAL_RX_VCO_STAT_0_OFFSET),
		readw(gphy->dptx_phy_regs_base + LANE1_DIG_RX_VCOCAL_RX_VCO_STAT_1_OFFSET),
		readw(gphy->dptx_phy_regs_base + LANE1_DIG_RX_VCOCAL_RX_VCO_STAT_2_OFFSET));

	for (int i = 0; i < 8; i++) {
		dev_err(gphy->dev, "Run %d: RAWLANE0_DIG_FSM_MEM_ADDR_MON = 0x%x, RAWLANE1_DIG_FSM_MEM_ADDR_MON = 0x%x\n",
			i, readw(gphy->dptx_phy_regs_base + RAWLANE0_DIG_FSM_MEM_ADDR_MON_OFFSET),
			readw(gphy->dptx_phy_regs_base + RAWLANE1_DIG_FSM_MEM_ADDR_MON_OFFSET));
	}
#endif
}

static int wait_for_lane0_phystatus(struct google_usb_phy *gphy)
{
	int ret;
	TCA_PSTATE_0 creg;

	/* Dump the value of PSTATE_* to check if PSTATE changes while polling */
	dev_info(gphy->dev,
		 "%s: polarity:%d TCA_PSTATE_0:0x%x TCA_PSTATE_1:0x%x TCA_PSTATE_2:0x%x\n",
		 __func__, gphy->typec_polarity,
		 readl(gphy->dptx_tca_regs_base + TCA_PSTATE_0_OFFSET),
		 readl(gphy->dptx_tca_regs_base + TCA_PSTATE_1_OFFSET),
		 readl(gphy->dptx_tca_regs_base + TCA_PSTATE_2_OFFSET));

	ret = readl_poll_timeout(gphy->dptx_tca_regs_base + TCA_PSTATE_0_OFFSET,
		creg.reg_value, (creg.bf.upcs_lane0_phystatus == 0), GPHY_DELAY_US,
		GPHY_TIMEOUT_US);

	if (ret) {
		dev_err(gphy->dev, "%s: timed out\n", __func__);
		phy_debug_dump(gphy);
	}

	return ret;
}

/**
 * The tuning parameters are applied through register preservation
 * as a part of usb3 phy firmware loading.
 * Thus need to be called before set_ext_load_done().
 */
static void google_tune_usb_phy(struct google_usb_phy *gphy)
{
	struct phy_tune_param *params = gphy->phy_tune_params;

	for (int i = 0; i < gphy->num_phy_tune_params; i++)
		writew(params[i].value, gphy->phy_sram_base + params[i].offset);

}

/**
 * The tuning parameters are effective only when USB3 Gen1 is active.
 * Need to be called before set_ext_load_done(),
 * and not necessarily part of USB3 PHY firmware loading.
 */
static void google_tune_usb3g1_phy(struct google_usb_phy *gphy)
{
	struct phy_tune_param *params = gphy->phy_tune_params_g1;

	for (int i = 0; i < gphy->num_phy_tune_params_g1; i++)
		writel(params[i].value, gphy->dp_top_csr_base + params[i].offset);
}

static void set_sram_bypass(struct google_usb_phy *gphy, int bypass)
{
	DP_USBCS_PHY_CFG1 phy_cfg1;

	phy_cfg1.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg1.bf.phy0_sram_bypass_mode = bypass;
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

	phy_cfg21.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);
	phy_cfg21.bf.phy_enable = 1;
	writel(phy_cfg21.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);
}

static void disable_usb2_phy(struct google_usb_phy *gphy)
{
	USB2PHY_CFG21 phy_cfg21;

	phy_cfg21.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);
	phy_cfg21.bf.phy_enable = 0;
	writel(phy_cfg21.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);
}

static void lga_set_power_config(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	PHY_power_config_reg1 power_config;

	power_config.reg_value = readl(gphy->dp_top_csr_base
		+ DP_TOP_CSR_PHY_POWER_CONFIG_REG1_OFFSET);
	power_config.bf.pg_mode_en = 1;
	power_config.bf.upcs_pipe_config = 0x260;
	writel(power_config.reg_value, gphy->dp_top_csr_base
		+ DP_TOP_CSR_PHY_POWER_CONFIG_REG1_OFFSET);
}

static void mbu_set_power_config(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	PHY_power_config_reg1 power_config;

	power_config.reg_value = readl(gphy->dp_top_csr_base
		+ DP_TOP_CSR_PHY_POWER_CONFIG_REG1_OFFSET);
	power_config.bf.pg_mode_en = 1;
	writel(power_config.reg_value, gphy->dp_top_csr_base
		+ DP_TOP_CSR_PHY_POWER_CONFIG_REG1_OFFSET);

	writel(0x260, gphy->upcs_config_reg);
}

static void set_vbus_valid(struct google_usb_phy *gphy)
{
	USBCS_PHY_CFG1 phy_cfg1;
	USBCS_PHY_CFG2 phy_cfg2;
	int vbus_valid = gphy->role == USB_ROLE_DEVICE ? 1 : 0;

	phy_cfg1.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg2.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG2_OFFSET);
	phy_cfg1.bf.sys_vbusvalid = vbus_valid;
	phy_cfg2.bf.phy_lane0_power_present = vbus_valid;
	writel(phy_cfg1.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	writel(phy_cfg2.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG2_OFFSET);
	dev_dbg(gphy->dev, "%s: %d\n", __func__, vbus_valid);
}

static void set_phy0_mplla_ssc_en(struct google_usb_phy *gphy)
{
	USBCS_PHY_CFG1 phy_cfg1;

	phy_cfg1.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg1.bf.phy0_mplla_ssc_en = 1;
	writel(phy_cfg1.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
}

static int wait_dp_tca_xa_ack(struct google_usb_phy *gphy)
	   __must_hold(&gphy->lock)
{
	int ret;
	TCA_INTR_STS intr_sts;

	ret = readl_poll_timeout(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET,
		intr_sts.reg_value, intr_sts.bf.xa_ack_evt == 1, GPHY_DELAY_US, GPHY_TIMEOUT_US);
	if (ret) {
		dev_err(gphy->dev, "%s: xa_ack timeout ret = %d\n", __func__, ret);
		phy_debug_dump(gphy);
	}

	return ret;
}

static int program_dp_tca_locked(struct google_usb_phy *gphy)
	   __must_hold(&gphy->lock)
{
	int ret;
	TCA_TCPC tcpc;
	TCA_INTR_STS intr_sts;

	if (gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: TCA not ready, aborting", __func__);
		return -EPERM;
	}

	// Clear any pending status
	intr_sts.reg_value = readl(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET);
	writel(intr_sts.reg_value, gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET);
	intr_sts.reg_value = readl(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET);
	if (intr_sts.bf.xa_ack_evt == 1) {
		dev_warn(gphy->dev, "%s: ack bit not cleared TCA_INTR_STS = 0x%x\n",
			 __func__, intr_sts.reg_value);
	}

	/* Update mux and polarity settings */
	tcpc.reg_value = readl(gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET);

	/* Set Type-C Mux */
	switch (gphy->typec_mode) {
	case TYPEC_STATE_NO_CONN:
		tcpc.bf.tcpc_mux_control = TCPC_MUX_NO_CONN;
		break;
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_E:
		tcpc.bf.tcpc_mux_control = TCPC_MUX_DP_ONLY;
		break;
	case TYPEC_DP_STATE_D:
	case TYPEC_DP_STATE_F:
		tcpc.bf.tcpc_mux_control = TCPC_MUX_USB_DP;
		break;
	case TYPEC_STATE_SAFE:
	case TYPEC_STATE_USB:
	default:
		tcpc.bf.tcpc_mux_control = TCPC_MUX_USB_ONLY;
		break;
	}

	/* Set polarity settings */
	tcpc.bf.tcpc_connector_orientation = gphy->typec_polarity;

	// TODO(mnkumar): tcpc_low_power_en bit is gone in LGA from this register.
	// Verify if this is needed
	tcpc.bf.tcpc_valid = 1;
	writel(tcpc.reg_value, gphy->dptx_tca_regs_base + TCA_TCPC_OFFSET);
	ret = wait_dp_tca_xa_ack(gphy);
	dev_info(gphy->dev, "%s: TCA switch %s, mux:%d orientation:%d", __func__,
		 ret ? "failed" : "success", tcpc.bf.tcpc_mux_control,
		 tcpc.bf.tcpc_connector_orientation);

	intr_sts.reg_value = readl(gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET);
	writel(intr_sts.reg_value, gphy->dptx_tca_regs_base + TCA_INTR_STS_OFFSET);

	return ret;
}

static void configure_usb2_phy(struct google_usb_phy *gphy)
{
	USB2PHY_CFG21 phy_cfg21;
	USB2PHY_CFG19 phy_cfg19;

	phy_cfg21.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);
	phy_cfg21.bf.ref_freq_sel = 0;
	phy_cfg21.bf.phy_tx_dig_bypass_sel = 0;
	writel(phy_cfg21.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);

	phy_cfg19.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG19_OFFSET);
	phy_cfg19.bf.phy_cfg_pll_fb_div = 368;
	writel(phy_cfg19.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG19_OFFSET);
}

static void lga_pm_callback(struct device *dev, bool resume)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	USBDP_TOP_CFG_REG cfg_reg;

	dev_dbg(gphy->dev, "resuming ? %d\n", resume);

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

	ret = devm_clk_bulk_get_optional(gphy->dev, gphy->drv_data->num_u3p_clks,
					 gphy->u3phy_clocks);
	if (ret)
		return ret;

	gphy->phy_fw_clk = devm_clk_get_optional(gphy->dev, "phy_fw_clk");
	if (IS_ERR(gphy->phy_fw_clk))
		return PTR_ERR(gphy->phy_fw_clk);

	return 0;
}

static int google_usb_phy_parse_resets(struct google_usb_phy *gphy)
{
	gphy->u2phy_reset = devm_reset_control_get_exclusive(gphy->dev, "usb2_phy_reset");
	if (IS_ERR(gphy->u2phy_reset)) {
		dev_err(gphy->dev, "Couldn't get usb2_phy_reset\n");
		return PTR_ERR(gphy->u2phy_reset);
	}

	gphy->u3phy_reset = devm_reset_control_get_optional_exclusive(gphy->dev,
								      "usb3dp_phy_reset");
	if (IS_ERR(gphy->u3phy_reset)) {
		dev_err(gphy->dev, "Couldn't get usb3dp_phy_reset\n");
		return PTR_ERR(gphy->u3phy_reset);
	}

	return 0;
}

#define gphy_instance_to_gphy(gphy_inst) \
	container_of((gphy_inst), struct google_usb_phy, inst[gphy_inst->index])

static int google_u2phy_init(struct phy *phy)
{
	int err;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u2phy init\n");

	configure_usb2_phy(gphy);

	mutex_lock(&gphy->lock);
	set_vbus_valid(gphy);
	mutex_unlock(&gphy->lock);

	if (gphy->drv_data->program_usb2_otps)
		gphy->drv_data->program_usb2_otps(gphy->dev);

	err = clk_bulk_prepare_enable(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	if (err)
		return err;

	err = reset_control_deassert(gphy->u2phy_reset);
	if (err)
		goto disable_u2phy_clocks;

	enable_usb2_phy(gphy);
	return 0;
disable_u2phy_clocks:
	clk_bulk_disable_unprepare(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return err;
}

static int google_u2phy_exit(struct phy *phy)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u2phy exit\n");
	disable_usb2_phy(gphy);
	reset_control_assert(gphy->u2phy_reset);
	clk_bulk_disable_unprepare(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return 0;
}

/* ---------- USB3 PHY Workarounds Start ---------- */

static void google_u3phy_force_ss_rx_det_disable_internal(struct google_usb_phy *gphy,
							  bool force_override)
{
	union TCA_CTRLSYNCMODE_DBG2 tca_dbg2;

	if (!(gphy->phy_workarounds & U3_PHY_FORCE_SS_RXDET_DISABLE))
		return;

	dev_info(gphy->dev, "%s: %d", __func__, force_override);

	tca_dbg2.reg_value = readl(gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG2_OFFSET);
	if (force_override)
		tca_dbg2.bf.ctrl_if_ovrrd_en |= (CTRL_IF_OVVRD_SS_RXDET_DISABLE);
	else
		tca_dbg2.bf.ctrl_if_ovrrd_en &= ~(CTRL_IF_OVVRD_SS_RXDET_DISABLE);
	tca_dbg2.bf.ctrl_if_ovrrd_val &= ~(CTRL_IF_OVVRD_SS_RXDET_DISABLE);
	writel(tca_dbg2.reg_value, gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG2_OFFSET);
}

static void google_u3phy_force_usb31_connected(struct google_usb_phy *gphy, bool val)
{
	union TCA_CTRLSYNCMODE_DBG2 tca_dbg2;

	if (!(gphy->phy_workarounds & U3_PHY_FORCE_USB31_CONNECTED))
		return;

	dev_info(gphy->dev, "%s: %d", __func__, val);

	tca_dbg2.reg_value = readl(gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG2_OFFSET);
	if (val)
		tca_dbg2.bf.ctrl_if_ovrrd_en |= (CTRL_IF_OVVRD_USB31_CTRL_CONNECTED);
	else
		tca_dbg2.bf.ctrl_if_ovrrd_en &= ~(CTRL_IF_OVVRD_USB31_CTRL_CONNECTED);
	tca_dbg2.bf.ctrl_if_ovrrd_val |= CTRL_IF_OVVRD_USB31_CTRL_CONNECTED;
	writel(tca_dbg2.reg_value, gphy->dptx_tca_regs_base + TCA_CTRLSYNCMODE_DBG2_OFFSET);
}

/* ---------- USB3 PHY Workarounds End ---------- */

/*
 * combo_phy_bringup_locked: bringup USB+DP Combo PHY
 *
 * TCA programming is not done here
 *
 */
static int combo_phy_bringup_locked(struct google_usb_phy *gphy)
	   __must_hold(&gphy->lock)
{
	int ret = 0;
	u16 aon_fw_ver_0, aon_fw_ver_1, reg;
	union SUP_DIG_MPLLA_MPLL_PWR_CTL_MPLL_PWR_FSM_CFG_0 fsm_cfg;
	union SUP_DIG_MPLLA_MPLL_PWR_CTL_MPLL_TUNE_VAL tune_val;

	/* Used for comparing phy firmware that was written Vs read back */
	u16 *phy_fw_val_read_back = NULL;

	if (gphy->phy_fw_clk) {
		ret = clk_set_rate(gphy->phy_fw_clk, 400000000);
		if (ret) {
			dev_err(gphy->dev, "Couldn't set rate for fw clock\n");
			return ret;
		}
	}

	gphy->drv_data->set_power_config(gphy->dev);
	set_vbus_valid(gphy);

	set_phy0_mplla_ssc_en(gphy);
	set_ext_load_done(gphy, 0);
	if (gphy->phy_sram_base)
		set_sram_bypass(gphy, 0);
	else
		set_sram_bypass(gphy, 1);

	if (gphy->drv_data->pm_callback)
		gphy->drv_data->pm_callback(gphy->dev, 1);

	if (gphy->drv_data->program_usb3_otps)
		gphy->drv_data->program_usb3_otps(gphy->dev);

	/* W4A: Step 1.1 */
	google_u3phy_force_ss_rx_det_disable_internal(gphy, true);
	google_u3phy_force_usb31_connected(gphy, true);

	ret = clk_bulk_prepare_enable(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
	if (ret)
		return ret;

	ret = reset_control_deassert(gphy->u3phy_reset);
	if (ret)
		goto disable_u3phy_clocks;

	usleep_range(1000, 1100);
	ret = wait_for_sram_init(gphy);
	if (ret) {
		dev_err(gphy->dev, "wait for sram init timed out");
		goto disable_u3phy_clocks;
	}

	if (gphy->phy_sram_base) {
		dev_dbg(gphy->dev, "Updating SRAM\n");
		usleep_range(100, 110);
		memcpy_toio(gphy->phy_sram_base, gphy->fw->data, gphy->fw->size);
		usleep_range(100, 110);

		phy_fw_val_read_back = kvmalloc(gphy->fw->size, GFP_KERNEL);
		if (phy_fw_val_read_back) {
			memcpy_fromio(phy_fw_val_read_back, gphy->phy_sram_base, gphy->fw->size);
			if (memcmp(gphy->fw->data, phy_fw_val_read_back, gphy->fw->size)) {
				dev_err(gphy->dev, "%s: phy fw written does not match read back\n",
					__func__);
				ret = -EIO;
				goto free_mem;
			}
		} else {
			dev_err(gphy->dev, "%s: phy_fw_val_read_back alloc failed\n", __func__);
		}

		google_tune_usb_phy(gphy);

		/*
		 * FW (2.27.0):
		 * Set RAWLANEAON_DIG_RX_AFE_BUF_IDAC_OFST[6] to 1 to enable WAR, which skips the
		 * CCA stages (phase slicer, data slicer, AFE/DFE adapt and FOM).
		 * Related to cases 01698072 and 01706410.
		 *
		 * Further Notes:
		 * As per SNPS, it would be needed for future FW releases as well.
		 *   - This feature allows skipping stages in the CCA for the USB3G1 protocol.
		 *   - This feature is configurable and is disabled by default.
		 */
		if (fw_name_to_version(gphy->fw_name) >= AON_FW_VERSION_2_27_0) {
			dev_info(gphy->dev, "Apply FW 2.27.0 WAR");
			reg = readw(gphy->dptx_phy_regs_base + RAWLANEAONX_DIG_RX_AFE_BUF_IDAC);
			reg |= BIT(6);
			writew(reg, gphy->dptx_phy_regs_base + RAWLANEAONX_DIG_RX_AFE_BUF_IDAC);
		}
	}

	google_tune_usb3g1_phy(gphy);

	if (gphy->drv_data->mplla_coarse_start_0_val > 0) {
		dev_info(gphy->dev, "RAWCMN_DIG_AON_MPLLA_CAL_BANK_SEL: 0x%x\n",
			 readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_AON_MPLLA_CAL_BANK_SEL));
		dev_info(gphy->dev, "RAWCMN_DIG_AON_MPLLA_TUNE_DONE: 0x%x\n",
			 readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_AON_MPLLA_TUNE_DONE));

		fsm_cfg.reg_value =
			readw(gphy->dptx_phy_regs_base +
			      SUP_DIG_MPLLA_MPLL_PWR_CTL_MPLL_PWR_FSM_CFG_0_OFFSET);
		fsm_cfg.bf.coarse_start_0 = gphy->drv_data->mplla_coarse_start_0_val;
		writew(fsm_cfg.reg_value, gphy->dptx_phy_regs_base +
		       SUP_DIG_MPLLA_MPLL_PWR_CTL_MPLL_PWR_FSM_CFG_0_OFFSET);
	}

	set_ext_load_done(gphy, 1);

	ret = wait_for_lane0_phystatus(gphy);
	if (ret) {
		dev_err(gphy->dev, "wait for lane0 phy status timed out");
		goto free_mem;
	}

	if (gphy->drv_data->mplla_coarse_start_0_val > 0) {
		tune_val.reg_value = readw(gphy->dptx_phy_regs_base +
					   SUP_DIG_MPLLA_MPLL_PWR_CTL_MPLL_TUNE_VAL_OFFSET);
		dev_info(gphy->dev, "RAWCMN_DIG_AON_MPLLA_CAL_BANK_SEL: 0x%x\n",
			 readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_AON_MPLLA_CAL_BANK_SEL));
		dev_info(gphy->dev, "RAWCMN_DIG_AON_MPLLA_TUNE_DONE: 0x%x\n",
			 readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_AON_MPLLA_TUNE_DONE));
		dev_info(gphy->dev, "MPLLA coarse tune val: 0x%x\n",
			 tune_val.bf.mpll_coarse_tune_val);

		if (tune_val.bf.mpll_coarse_tune_val <= gphy->drv_data->mplla_coarse_start_0_val) {
			dev_err(gphy->dev, "MPLLA coarse tune value matches coarse start value\n");
			ret = -EIO;
			goto free_mem;
		}
	}

	/* Read FW version post loading*/
	aon_fw_ver_0 = readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_AON_FW_VERSION_0);
	aon_fw_ver_1 = readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_AON_FW_VERSION_1);

	dev_info(gphy->dev, "%s: fw ver:%d.%d.%d date:%d/%d/%d cfg:0x%x",
		 __func__, AON_FW_VERSION_a(aon_fw_ver_0), AON_FW_VERSION_b(aon_fw_ver_0),
		 AON_FW_VERSION_c(aon_fw_ver_0), AON_FW_VERSION_YEAR(aon_fw_ver_1),
		 AON_FW_VERSION_MONTH(aon_fw_ver_1), AON_FW_VERSION_DAY(aon_fw_ver_1),
		 readw(gphy->dptx_phy_regs_base + RAWCMN_DIG_CONFIG_MASTER_VERSION));

	if (phy_fw_val_read_back)
		kvfree(phy_fw_val_read_back);
	return ret;

free_mem:
	if (phy_fw_val_read_back)
		kvfree(phy_fw_val_read_back);
disable_u3phy_clocks:
	clk_bulk_disable_unprepare(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);
	return ret;
}

static void combo_phy_teardown_locked(struct google_usb_phy *gphy)
	    __must_hold(&gphy->lock)
{
	dev_info(gphy->dev, "Google PHY teardown");

	if (gphy->drv_data->pm_callback)
		gphy->drv_data->pm_callback(gphy->dev, 0);

	reset_control_assert(gphy->u3phy_reset);
	clk_bulk_disable_unprepare(gphy->drv_data->num_u3p_clks, gphy->u3phy_clocks);

	gphy->phy_state = COMBO_PHY_IDLE;
}

static int google_u3phy_init(struct phy *phy)
{
	int ret = 0;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_info(gphy->dev, "Google u3phy init\n");

	mutex_lock(&gphy->lock);

	if (gphy->phy_state == COMBO_PHY_IDLE) {
		ret = combo_phy_bringup_locked(gphy);
		if (!ret)
			gphy->phy_state = COMBO_PHY_INIT_DONE;
	} else {
		dev_warn(gphy->dev, "%s called in combo_phy_state %d", __func__, gphy->phy_state);
	}

	if (!ret)
		gphy->phy_users |= COMBO_PHY_USER_USB3;

	mutex_unlock(&gphy->lock);

	return ret;
}

static int google_u3phy_exit(struct phy *phy)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_info(gphy->dev, "Google u3phy exit\n");

	mutex_lock(&gphy->lock);
	gphy->phy_users &= ~COMBO_PHY_USER_USB3;
	if (!gphy->phy_users)
		combo_phy_teardown_locked(gphy);

	// TODO(maurora): Return U3 phy controller to NC before powering down
	mutex_unlock(&gphy->lock);
	return 0;
}

static int google_u3phy_power_on(struct phy *phy)
{
	int ret;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	unsigned long mode_cache;

	dev_info(gphy->dev, "Google u3phy power_on\n");

	if (gphy->phy_state == COMBO_PHY_TCA_READY) {
		dev_info(gphy->dev, "u3phy already powered on\n");
		return 0;
	}

	/* Write TX Deemphasis in DWC3 Link Registers per LGA HPG */
	writel(0x6984, gphy->dwc3_lcsr_deemph_base + DWC3_LINK_LCSR_TX_DEEMPH_OFFSET);

	mutex_lock(&gphy->lock);
	/* Wait for PoR->NC transition before programming tca if phy just comes out of reset */
	ret = wait_dp_tca_xa_ack(gphy);
	if (ret)
		dev_err(gphy->dev, "PoR->NC transition timeout\n");
	gphy->phy_state = COMBO_PHY_TCA_READY;
	dev_dbg(gphy->dev, "%s %d\n", __func__, gphy->typec_polarity);
	/*
	 * If DP Alt Mode is configured when USB bringup is deferred, then this TCA switch
	 * would attempt to enter a DP configuration. So, we need to force USB here.
	 */
	mode_cache = gphy->typec_mode;
	gphy->typec_mode = TYPEC_STATE_USB;
	ret = program_dp_tca_locked(gphy);
	gphy->typec_mode = mode_cache;
	mutex_unlock(&gphy->lock);
	return ret;
}

/* ---------- USB3 PHY Callbacks Start ---------- */

extern void google_u3phy_force_ss_rx_det_disable(struct phy *phy,
						 bool force_override)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	google_u3phy_force_ss_rx_det_disable_internal(gphy, force_override);
}
EXPORT_SYMBOL_GPL(google_u3phy_force_ss_rx_det_disable);

extern int google_u3phy_soft_reset(struct phy *phy, bool mpll_override)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	union SUP_DIG_MPLLA_OVRD_IN_0_REG mplla_ovrd;
	union SUP_DIG_ASIC_OUT_0_REG asic_out;
	TCA_PSTATE_1 pstate;
	int ret;

	dev_info(gphy->dev, "%s: %d", __func__, mpll_override);

	pstate.reg_value = readl(gphy->dptx_tca_regs_base + TCA_PSTATE_1_OFFSET);
	dev_info(gphy->dev, "TCA_PSTATE_1 = 0x%x\n", pstate.reg_value);

	/* mpll_override will result in fabric timeout if USB controller below P2 */
	ret = readl_poll_timeout_atomic(gphy->dptx_tca_regs_base + TCA_PSTATE_1_OFFSET,
			pstate.reg_value, pstate.bf.usb32_lane0_powerdown < TCA_PSTATE_POWERDOWN_P3,
			GPHY_DP_WA_DELAY_US, GPHY_DP_WA_TIMEOUT_US);
	if (ret) {
		dev_info(gphy->dev, "%s: PHY is not in P2 or higher, pstate 0x%x",
			__func__, pstate.bf.usb32_lane0_powerdown);
		return -1;
	}

	/* Entry Sequence */
	if (mpll_override) {
		/*
		 * Enable MPLLA override, turn of MPLLA
		 * W4a: Step#5 and Step#6
		 */
		mplla_ovrd.reg_value = readw(gphy->dptx_phy_regs_base + SUP_DIG_MPLLA_OVRD_IN_0);
		mplla_ovrd.bf.mplla_en_orvd_val = 0;
		mplla_ovrd.bf.mplla_en_orvd_en = 1;
		writew(mplla_ovrd.reg_value, gphy->dptx_phy_regs_base + SUP_DIG_MPLLA_OVRD_IN_0);

		/*
		 * Poll for MPLLA state off
		 * W4a: Step#7
		 */
		ret = readw_poll_timeout_atomic(gphy->dptx_phy_regs_base + SUP_DIG_ASIC_OUT_0,
						asic_out.reg_value, asic_out.bf.mplla_state == 0,
						GPHY_DELAY_US, GPHY_TIMEOUT_US);
		dev_info(gphy->dev, "%s: mplla poll %s\n", __func__, ret ? "failed" : "succeeded");

		/*
		 * Wait for 10 us
		 * W4a: Step#8
		 */
		udelay(10);
	/* Exit Sequence */
	} else {
		/*
		 * Disable MPLLA override
		 * W4a: Step#11
		 */
		mplla_ovrd.reg_value = readw(gphy->dptx_phy_regs_base + SUP_DIG_MPLLA_OVRD_IN_0);
		mplla_ovrd.bf.mplla_en_orvd_en = 0;
		writew(mplla_ovrd.reg_value, gphy->dptx_phy_regs_base + SUP_DIG_MPLLA_OVRD_IN_0);
		dev_info(gphy->dev, "%s: mplla override cleared\n", __func__);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(google_u3phy_soft_reset);

/* ---------- USB3 PHY Callbacks End ---------- */

/* ---------- AUX Controller PHY Callbacks Start ---------- */
static int google_u2phy_aux_init(struct phy *phy)
{
	int err;
	AUX_USB2_PHY_CFG aux_u2phy_cfg;
	USB2_SS_CTRL_1 u2ss_ctrl_1;
	USB2_SS_CTRL_4 u2ss_ctrl_4;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u2phy aux init\n");

	u2ss_ctrl_4.reg_value = readl(gphy->usb2_ss_base + USB2SS_CTRL4_OFFSET);
	u2ss_ctrl_4.bf.usb2phy_ref_freq_sel = 0;
	writel(u2ss_ctrl_4.reg_value, gphy->usb2_ss_base + USB2SS_CTRL4_OFFSET);

	aux_u2phy_cfg.reg_value = readl(gphy->usb2_cfg_csr_base +
					DP_TOP_CSR_AUX_U2PHY_CFG_OFFSET);
	aux_u2phy_cfg.bf.usb2phy_phy_tx_dig_bypass_sel = 0;
	writel(aux_u2phy_cfg.reg_value,
	       gphy->usb2_cfg_csr_base + DP_TOP_CSR_AUX_U2PHY_CFG_OFFSET);

	u2ss_ctrl_1.reg_value = readl(gphy->usb2_ss_base + USB2SS_CTRL1_OFFSET);
	u2ss_ctrl_1.bf.usb2phy_phy_cfg_pll_fb_div = 368;
	writel(u2ss_ctrl_1.reg_value, gphy->usb2_ss_base + USB2SS_CTRL1_OFFSET);

	err = clk_bulk_prepare_enable(GOOGLE_U2PHY_NUM_CLOCKS,
				      gphy->u2phy_clocks);
	if (err)
		return err;

	err = reset_control_deassert(gphy->u2phy_reset);
	if (err)
		goto disable_u2phy_aux_clocks;

	aux_u2phy_cfg.reg_value = readl(gphy->usb2_cfg_csr_base +
					DP_TOP_CSR_AUX_U2PHY_CFG_OFFSET);
	aux_u2phy_cfg.bf.usb2phy_phy_enable = 1;
	writel(aux_u2phy_cfg.reg_value,
	       gphy->usb2_cfg_csr_base + DP_TOP_CSR_AUX_U2PHY_CFG_OFFSET);

	return 0;

disable_u2phy_aux_clocks:
	clk_bulk_disable_unprepare(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return err;
}

static int google_u2phy_aux_exit(struct phy *phy)
{
	AUX_USB2_PHY_CFG aux_u2phy_cfg;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);

	dev_dbg(gphy->dev, "Google u2phy aux exit\n");

	aux_u2phy_cfg.reg_value = readl(gphy->usb2_cfg_csr_base +
					DP_TOP_CSR_AUX_U2PHY_CFG_OFFSET);
	aux_u2phy_cfg.bf.usb2phy_phy_enable = 0;
	writel(aux_u2phy_cfg.reg_value,
	       gphy->usb2_cfg_csr_base + DP_TOP_CSR_AUX_U2PHY_CFG_OFFSET);

	reset_control_assert(gphy->u2phy_reset);
	clk_bulk_disable_unprepare(GOOGLE_U2PHY_NUM_CLOCKS, gphy->u2phy_clocks);
	return 0;
}

/* ---------- DP PHY Callbacks Start ---------- */

extern int google_dpphy_set_maxpclk(struct phy *phy, u32 lane0, u32 lane1)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	USBCS_PHY_CFG2 phy_cfg;
	int ret = 0;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	phy_cfg.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG2_OFFSET);
	phy_cfg.bf.dp_pipe_lane0_maxpclkreq = lane0;
	phy_cfg.bf.dp_pipe_lane1_maxpclkreq = lane1;
	writel(phy_cfg.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG2_OFFSET);

	ret = readl_poll_timeout(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG2_OFFSET,
		phy_cfg.reg_value,
		(phy_cfg.bf.dp_pipe_lane0_maxpclkreq == phy_cfg.bf.dp_pipe_lane0_maxpclkack) &&
		(phy_cfg.bf.dp_pipe_lane1_maxpclkreq == phy_cfg.bf.dp_pipe_lane1_maxpclkack),
		GPHY_DELAY_US, GPHY_TIMEOUT_US);

	dev_info(gphy->dev, "%s %s, pipe0 req %d ack %d, pipe1 req %d ack %d", __func__,
		 ret ? "failed" : "succeeded",
		 phy_cfg.bf.dp_pipe_lane0_maxpclkreq, phy_cfg.bf.dp_pipe_lane0_maxpclkack,
		 phy_cfg.bf.dp_pipe_lane1_maxpclkreq, phy_cfg.bf.dp_pipe_lane1_maxpclkack);

	mutex_unlock(&gphy->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(google_dpphy_set_maxpclk);

extern int google_dpphy_set_pipe_pclk_on(struct phy *phy, bool on)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	USBCS_PHY_CFG1 phy_cfg;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	phy_cfg.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);
	phy_cfg.bf.dp_pipe_pclk_on = on ? 1 : 0;
	writel(phy_cfg.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG1_OFFSET);

	mutex_unlock(&gphy->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_set_pipe_pclk_on);

extern int google_dpphy_config_write(struct phy *phy, u32 mask, u32 val)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	union DP_CONFIG_REG dp_config;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	dp_config.reg_value = readl(gphy->dp_top_csr_base + DP_TOP_CSR_DP_CONFIG_OFFSET);
	dp_config.reg_value &= ~mask;
	dp_config.reg_value |= val;
	writel(dp_config.reg_value, gphy->dp_top_csr_base + DP_TOP_CSR_DP_CONFIG_OFFSET);

	mutex_unlock(&gphy->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_config_write);

extern int google_dpphy_init_upcs_pipe_config(struct phy *phy)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	PHY_power_config_reg1 power_config;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	power_config.reg_value = readl(gphy->dp_top_csr_base +
				       DP_TOP_CSR_PHY_POWER_CONFIG_REG1_OFFSET);
	power_config.bf.upcs_pipe_config = 0x260;
	writel(power_config.reg_value, gphy->dp_top_csr_base +
	       DP_TOP_CSR_PHY_POWER_CONFIG_REG1_OFFSET);

	mutex_unlock(&gphy->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_init_upcs_pipe_config);

extern int google_dpphy_aux_powerup(struct phy *phy)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	union AUX_CTRL aux_ctrl;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	aux_ctrl.reg_value = readl(gphy->dp_top_csr_base + DP_TOP_CSR_AUX_CTRL_OFFSET);
	if (gphy->aux_tune_ovrd_en) {
		aux_ctrl.bf.phy_aux_ctrl = gphy->aux_tune_params_ovrd.aux_ctrl;
		aux_ctrl.bf.phy_aux_hys_tune = gphy->aux_tune_params_ovrd.aux_hys_tune;
		aux_ctrl.bf.phy_aux_vod_tune = gphy->aux_tune_params_ovrd.aux_vod_tune;
	} else {
		aux_ctrl.bf.phy_aux_ctrl = 1;
		aux_ctrl.bf.phy_aux_hys_tune = 2;
		aux_ctrl.bf.phy_aux_vod_tune = 1;
	}
	writel(aux_ctrl.reg_value, gphy->dp_top_csr_base + DP_TOP_CSR_AUX_CTRL_OFFSET);

	udelay(300);

	aux_ctrl.reg_value = readl(gphy->dp_top_csr_base + DP_TOP_CSR_AUX_CTRL_OFFSET);
	aux_ctrl.bf.phy_aux_pwdnb = 1;
	writel(aux_ctrl.reg_value, gphy->dp_top_csr_base + DP_TOP_CSR_AUX_CTRL_OFFSET);

	usleep_range(100000, 110000);

	mutex_unlock(&gphy->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_aux_powerup);

static void mbu_typec_disable_ack_set(struct device *dev, int val)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	union TCA_OVERRIDE_REG0 tca_ovrd;

	tca_ovrd.reg_value = readl(gphy->dp_top_csr_base + DP_TOP_CSR_TCA_OVERRIDE_REG0_OFFSET);
	tca_ovrd.bf.reg_dpalt_disable_ack = val;
	writel(tca_ovrd.reg_value, gphy->dp_top_csr_base + DP_TOP_CSR_TCA_OVERRIDE_REG0_OFFSET);
}

/* ---------- DP PHY Callbacks End ---------- */

/* ---------- DP PIPE Callbacks Start ---------- */
extern int google_dpphy_pipe_lane_disable_tx(struct phy *phy, int max_lanes, int used_lanes)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	void __iomem *lane0_reg_base = gphy->dp_pipe_lane0_regs_base;
	void __iomem *lane1_reg_base = gphy->dp_pipe_lane1_regs_base;
	int lane0_disable = used_lanes < 4;
	int lane1_tx0_disable = used_lanes == 1;
	u8 reg;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	/* Configure tx0 and tx1 for lane0 if used by DP */
	if (max_lanes == 4) {
		/* Update tx0 */
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);
		reg &= ~DISABLE_SINGLE_TX_MASK;
		reg |= DISABLE_SINGLE_TX_VAL(lane0_disable);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);

		/* Update tx1 */
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL2_OFFSET);
		reg &= ~DISABLE_SINGLE_TX_MASK;
		reg |= DISABLE_SINGLE_TX_VAL(lane0_disable);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL2_OFFSET);
	}

	/* Configure tx0 for lane1 for one lane usage */
	/* Update tx0 */
	reg = readb(lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);
	reg &= ~DISABLE_SINGLE_TX_MASK;
	reg |= DISABLE_SINGLE_TX_VAL(lane1_tx0_disable);
	writeb(reg, lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);

	mutex_unlock(&gphy->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_pipe_lane_disable_tx);

extern int google_dpphy_pipe_tx_eq_set(struct phy *phy, int lane, enum dp_tx_eq_level vs,
				       enum dp_tx_eq_level pe)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	void __iomem *lane0_reg_base = gphy->dp_pipe_lane0_regs_base;
	void __iomem *lane1_reg_base = gphy->dp_pipe_lane1_regs_base;
	u8 reg;

	/* No-op if dp_tune_ovrd_en is enabled */
	if (dp_tune_ovrd_en) {
		dev_info(gphy->dev, "%s dp_tune_ovrd_en is enabled, skipping", __func__);
		return 0;
	}

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	switch (lane) {
	/* DP lane 0 = Rx2+/- = PIPE dp_pipe_tx1_serdes_data2 */
	case 0:
		/* Update tx1 preemp */
		reg = readb(lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL2_OFFSET);
		reg &= ~TX_DEEMPH_MASK;
		reg |= TX_DEEMPH_VAL(pe);
		writeb(reg, lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL2_OFFSET);
		/* Update tx1 margin */
		reg = readb(lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL8_OFFSET);
		reg &= ~TX_MARGIN_MASK;
		reg |= TX_MARGIN_VAL(vs);
		writeb(reg, lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL8_OFFSET);
		break;
	/* DP lane 1 = Tx2+/- = PIPE dp_pipe_tx1_serdes_data */
	case 1:
		/* Update tx0 preemp */
		reg = readb(lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);
		reg &= ~TX_DEEMPH_MASK;
		reg |= TX_DEEMPH_VAL(pe);
		writeb(reg, lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);
		/* Update tx0 margin */
		reg = readb(lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL8_OFFSET);
		reg &= ~TX_MARGIN_MASK;
		reg |= TX_MARGIN_VAL(vs);
		writeb(reg, lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL8_OFFSET);
		break;
	/* DP lane 2 = Tx1+/- = PIPE dp_pipe_tx0_serdes_data */
	case 2:
		/* Update tx0 preemp */
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);
		reg &= ~TX_DEEMPH_MASK;
		reg |= TX_DEEMPH_VAL(pe);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL2_OFFSET);
		/* Update tx0 margin */
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL8_OFFSET);
		reg &= ~TX_MARGIN_MASK;
		reg |= TX_MARGIN_VAL(vs);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_TX_CONTROL8_OFFSET);
		break;
	/* DP lane 3 = Rx1+/- = PIPE dp_pipe_tx0_serdes_data2 */
	case 3:
		/* Update tx1 preemp */
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL2_OFFSET);
		reg &= ~TX_DEEMPH_MASK;
		reg |= TX_DEEMPH_VAL(pe);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL2_OFFSET);
		/* Update tx1 margin */
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL8_OFFSET);
		reg &= ~TX_MARGIN_MASK;
		reg |= TX_MARGIN_VAL(vs);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_HDP_TX_CONTROL8_OFFSET);
		break;
	default:
		dev_warn(gphy->dev, "%s: invalid lane parameter %d", __func__, lane);
		break;
	}

	dev_info(gphy->dev, "dptx TXEQ: lane:%d vs:%d pe:%d", lane, vs, pe);

	mutex_unlock(&gphy->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_pipe_tx_eq_set);

/* ---------- DP PIPE Callbacks End ---------- */

/* ---------- DP Tuning Callbacks Start ---------- */
extern int google_dpphy_eq_tune_ovrd_enable(struct phy *phy, int max_lanes, bool enable)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	void __iomem *lane0_reg_base = gphy->dp_pipe_lane0_regs_base;
	void __iomem *lane1_reg_base = gphy->dp_pipe_lane1_regs_base;
	u8 reg;

	/* No-op if dp_tune_ovrd_en is not enabled */
	if (!dp_tune_ovrd_en) {
		dev_info(gphy->dev, "%s dp_tune_ovrd_en is disabled, skipping", __func__);
		return 0;
	}

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	/* Enable or Disable Override on DP Lanes 0 and 1 */
	reg = readb(lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_VDR_OVRD_OFFSET);
	reg &= ~TX_EQ_OVRD_G1_MASK;
	reg |= TX_EQ_OVRD_G1_VAL(enable);
	reg &= ~TX_HDP_EQ_OVRD_G1_MASK;
	reg |= TX_HDP_EQ_OVRD_G1_VAL(enable);
	writeb(reg, lane1_reg_base + UPCSLANE_PIPE_LPC_PHY_VDR_OVRD_OFFSET);

	/* Enable or Disable Override on DP Lanes 2 and 3 */
	if (max_lanes == 4) {
		reg = readb(lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_VDR_OVRD_OFFSET);
		reg &= ~TX_EQ_OVRD_G1_MASK;
		reg |= TX_EQ_OVRD_G1_VAL(enable);
		reg &= ~TX_HDP_EQ_OVRD_G1_MASK;
		reg |= TX_HDP_EQ_OVRD_G1_VAL(enable);
		writeb(reg, lane0_reg_base + UPCSLANE_PIPE_LPC_PHY_VDR_OVRD_OFFSET);
	}

	mutex_unlock(&gphy->lock);

	dev_info(gphy->dev, "dptx: Successfully %s eq_tune override",
		 enable ? "enabled" : "disabled");

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_eq_tune_ovrd_enable);

extern int google_dpphy_eq_tune_ovrd_apply(struct phy *phy, int lane, enum dp_pipe_rate rate,
					   enum dp_tx_eq_level vs, enum dp_tx_eq_level pe)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	void __iomem *lane_reg_base = lane < 2 ? gphy->dp_pipe_lane1_regs_base :
						 gphy->dp_pipe_lane0_regs_base;
	u32 main_offset, pre_offset, post_offset;
	u8 reg;

	/* No-op if dp_tune_ovrd_en is not enabled */
	if (!dp_tune_ovrd_en) {
		dev_info(gphy->dev, "%s dp_tune_ovrd_en is disabled, skipping", __func__);
		return 0;
	}

	/* Get Pre/Post/Main Offsets */
	switch (lane) {
	/* DP lane 0/3 */
	case 0:
	case 3:
		main_offset = UPCSLANE_PIPE_LPC_PHY_HDP_VDR_MAIN_OVRD_OFFSET;
		pre_offset = UPCSLANE_PIPE_LPC_PHY_HDP_VDR_PRE_OVRD_OFFSET;
		post_offset = UPCSLANE_PIPE_LPC_PHY_HDP_VDR_POST_OVRD_OFFSET;
		break;
	case 1:
	case 2:
		main_offset = UPCSLANE_PIPE_LPC_PHY_VDR_MAIN_OVRD_OFFSET;
		pre_offset = UPCSLANE_PIPE_LPC_PHY_VDR_PRE_OVRD_OFFSET;
		post_offset = UPCSLANE_PIPE_LPC_PHY_VDR_POST_OVRD_OFFSET;
		break;
	default:
		dev_warn(gphy->dev, "%s: invalid lane parameter %d", __func__, lane);
		return -EINVAL;
	}

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	/* Apply Tuning Value */
	/* Update tx main */
	reg = readb(lane_reg_base + main_offset);
	reg &= ~TX_EQ_OVRD_MASK;
	reg |= TX_EQ_OVRD_VAL(gphy->dp_tune_main[rate][vs][pe]);
	writeb(reg, lane_reg_base + main_offset);
	/* Update tx pre */
	reg = readb(lane_reg_base + pre_offset);
	reg &= ~TX_EQ_OVRD_MASK;
	reg |= TX_EQ_OVRD_VAL(gphy->dp_tune_pre[rate][vs][pe]);
	writeb(reg, lane_reg_base + pre_offset);
	/* Update tx post */
	reg = readb(lane_reg_base + post_offset);
	reg &= ~TX_EQ_OVRD_MASK;
	reg |= TX_EQ_OVRD_VAL(gphy->dp_tune_post[rate][vs][pe]);
	writeb(reg, lane_reg_base + post_offset);

	dev_info(gphy->dev, "dptx TXEQ: lane:%d vs:%d pe:%d main:%d pre:%d post:%d",
		lane, vs, pe,
		gphy->dp_tune_main[rate][vs][pe],
		gphy->dp_tune_pre[rate][vs][pe],
		gphy->dp_tune_post[rate][vs][pe]);

	mutex_unlock(&gphy->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_eq_tune_ovrd_apply);

extern int google_dpphy_eq_tune_asic_read(struct phy *phy, int lane,
					  enum plug_orientation orientation)
{
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	u16 reg, pre, post, main;
	/* ASIC doesn't take orientation into account */
	int phy_lane = orientation == PLUG_FLIPPED ? ((-lane) + 5) % 4 : (lane + 2) % 4;

	mutex_lock(&gphy->lock);

	if (!(gphy->phy_users & COMBO_PHY_USER_DP) || gphy->phy_state != COMBO_PHY_TCA_READY) {
		dev_err(gphy->dev, "%s: DP phy not initialized", __func__);
		mutex_unlock(&gphy->lock);
		return -EPERM;
	}

	reg = readw(gphy->dptx_phy_regs_base + LANEN_DIG_ASIC_TX_ASIC_IN_1(phy_lane));
	main = ((reg & TX_MAIN_CURSOR_MASK) >> TX_MAIN_CURSOR_OFFSET) / 2;
	reg = readw(gphy->dptx_phy_regs_base + LANEN_DIG_ASIC_TX_ASIC_IN_2(phy_lane));
	pre = ((reg & TX_PRE_CURSOR_MASK) >> TX_PRE_CURSOR_OFFSET) / 2;
	post = ((reg & TX_POST_CURSOR_MASK) >> TX_POST_CURSOR_OFFSET) / 2;

	dev_info(gphy->dev,
		 "dptx TXEQ: lane:%d phy_lane:%d ASIC values: main:%d pre:%d post:%d",
		 lane, phy_lane, main, pre, post);

	mutex_unlock(&gphy->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(google_dpphy_eq_tune_asic_read);

/* ---------- DP Tuning Callbacks End ---------- */

#define DP_PHY_INIT_MAX_RETRIES		18
#define DP_PHY_INIT_RETRY_MS		50

/*
 * google_dpphy_init - initialize ComboPHY for DP
 *
 * DP phy_init is expected to run after the ComboPHY has moved from
 * PoR => NC => USB. The sequence in the ComboPHY driver is:
 *	1. google_u3phy_init
 *	2. google_u3phy_power_on
 *	3. google_dpphy_init
 * DP phy_init will test to see if google_u3phy_power_on completes by
 * evaluating if gphy->phy_state equals COMBO_PHY_TCA_READY.
 *
 * DP phy_init also needs to be called after pm_runtime_enable on DP_TOP to
 * ensure DP controller access.
 *
 */
static int google_dpphy_init(struct phy *phy)
{
	TCA_CLK_RST tca_clk_rst;
	TCA_GCFG tca_gcfg;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	int ret = 0;
	u32 mode_cache;

	dev_info(gphy->dev, "Google DP PHY init\n");

	/* DP PHY waits for COMBO_PHY_TCA_READY, which means USB3 is completely brought up */
	for (int i = 0; i < DP_PHY_INIT_MAX_RETRIES; i++) {
		mutex_lock(&gphy->lock);

		/* 1. TCA is ready, can proceed with phy_init */
		if (gphy->phy_state == COMBO_PHY_TCA_READY)
			goto tca_ready;

		/* 2. TCA is not ready, retry */
		mutex_unlock(&gphy->lock);
		dev_err(gphy->dev, "%s: USB3 PHY init not complete, attempt %d", __func__, i);
		msleep(DP_PHY_INIT_RETRY_MS);
	}

	/* gphy->lock is dropped, safe to return */
	return -EPERM;

tca_ready:
	/* gphy->lock is held in this section */

	if (!COMBO_PHY_MUX_DP(gphy->typec_mode)) {
		dev_err(gphy->dev, "%s: typec_mux not in DP mode", __func__);
		ret = -EPERM;
		goto done;
	}

	if (gphy->drv_data->typec_disable_ack_set)
		gphy->drv_data->typec_disable_ack_set(gphy->dev, 1);

	tca_clk_rst.reg_value = readl(gphy->dptx_tca_regs_base + TCA_CLK_RST_OFFSET);
	tca_clk_rst.bf.tca_ref_clk_en = 1;
	writel(tca_clk_rst.reg_value, gphy->dptx_tca_regs_base + TCA_CLK_RST_OFFSET);

	tca_gcfg.reg_value = readl(gphy->dptx_tca_regs_base + TCA_GCFG_OFFSET);
	tca_gcfg.bf.auto_mode_en = 0;
	writel(tca_gcfg.reg_value, gphy->dptx_tca_regs_base + TCA_GCFG_OFFSET);

	/* Move to NC if DP4 is requested */
	if (COMBO_PHY_MUX_DP_ONLY(gphy->typec_mode)) {
		mode_cache = gphy->typec_mode;
		gphy->typec_mode = TYPEC_STATE_NO_CONN;

		ret = program_dp_tca_locked(gphy);
		if (ret)
			dev_err(gphy->dev, "USB => NC => DP transition failed at NC step");

		gphy->typec_mode = mode_cache;
	}

	/* Move to DP state */
	ret = program_dp_tca_locked(gphy);
	if (ret)
		goto done;

	/* Flag phy_users to prevent tear down by USB */
	gphy->phy_users |= COMBO_PHY_USER_DP;

	if (gphy->drv_data->typec_disable_ack_set)
		gphy->drv_data->typec_disable_ack_set(gphy->dev, 0);

done:
	mutex_unlock(&gphy->lock);
	return ret;
}

/*
 * google_dpphy_exit - exit ComboPHY for DP
 *
 * DP phy_exit should guarantee that USB_TOP is still on by not dropping
 * it's pm_runtime count.
 */
static int google_dpphy_exit(struct phy *phy)
{
	TCA_CLK_RST tca_clk_rst;
	TCA_GCFG tca_gcfg;
	struct google_usb_phy_instance *gphy_inst = phy_get_drvdata(phy);
	struct google_usb_phy *gphy = gphy_instance_to_gphy(gphy_inst);
	unsigned long mode_cache;

	dev_info(gphy->dev, "Google DP PHY exit\n");

	mutex_lock(&gphy->lock);

	if (gphy->drv_data->typec_disable_ack_set)
		gphy->drv_data->typec_disable_ack_set(gphy->dev, 1);

	gphy->phy_users &= ~COMBO_PHY_USER_DP;
	/* Tear down ComboPHY is DP is last PHY user */
	if (!gphy->phy_users) {
		combo_phy_teardown_locked(gphy);
	/*
	 * Switch to USB if cable is still connected. Every dpphy_init() should involve a real
	 * USB => DP transition. USB2/DP2 will trigger b/404882561, so even it should transition
	 * to USB only.
	 */
	} else if (gphy->phy_state == COMBO_PHY_TCA_READY) {
		mode_cache = gphy->typec_mode;
		gphy->typec_mode = TYPEC_STATE_USB;
		program_dp_tca_locked(gphy);
		gphy->typec_mode = mode_cache;
	} else {
		dev_warn(gphy->dev, "%s: phy_state is not COMBO_PHY_TCA_READY\n", __func__);
	}

	tca_gcfg.reg_value = readl(gphy->dptx_tca_regs_base + TCA_GCFG_OFFSET);
	tca_gcfg.bf.auto_mode_en = 1;
	writel(tca_gcfg.reg_value, gphy->dptx_tca_regs_base + TCA_GCFG_OFFSET);

	tca_clk_rst.reg_value = readl(gphy->dptx_tca_regs_base + TCA_CLK_RST_OFFSET);
	tca_clk_rst.bf.tca_ref_clk_en = 0;
	writel(tca_clk_rst.reg_value, gphy->dptx_tca_regs_base + TCA_CLK_RST_OFFSET);

	mutex_unlock(&gphy->lock);
	return 0;
}

static const struct dev_pm_ops google_usb_phy_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, NULL)
	SET_RUNTIME_PM_OPS(NULL, NULL, NULL)
};

static struct phy *google_usb_phy_xlate(struct device *dev,
	const struct of_phandle_args *args)
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

	dev_info(gphy->dev, "%s: %s\n", __func__, orientation_name(orientation));
	mutex_lock(&gphy->lock);
	gphy->typec_polarity = (orientation == TYPEC_ORIENTATION_REVERSE) ? 1 : 0;
	/* No need to TCA switch when DP is active, it will switch to USB on exit */
	if (orientation != TYPEC_ORIENTATION_NONE &&
	    gphy->phy_state == COMBO_PHY_TCA_READY &&
	    !(gphy->phy_users & COMBO_PHY_USER_DP))
		ret = program_dp_tca_locked(gphy);

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

	dev_info(gphy->dev, "%s: %s\n", __func__, mux_mode_name(state->mode));
	mutex_lock(&gphy->lock);
	/*
	 * We cache the mode here for DP usage. TCA uses the mode in these instances:
	 *	1. google_dpphy_init() - switches TCA to cached DP mode after hotplug detect instead
	 * of on Type-C Alt Mode Entry. Fails if cached mode is not DP.
	 *	2. google_u3phy_power_on() - always switches to USB regardless of gphy->typec_mode
	 *	3. google_dpphy_exit() - always switches to USB regardless of gphy->typec_mode
	 *	4. google_usb_set_orientation() - allows for orientation change in USB mode before
	 * DWC suspends.
	 */
	gphy->typec_mode = state->mode;
	mutex_unlock(&gphy->lock);

	return ret;
}

static int u3phy_usb_role_switch_set(struct usb_role_switch *sw,
					 enum usb_role role)
{
	struct google_usb_phy *gphy = usb_role_switch_get_drvdata(sw);
	int ret = 0;

	mutex_lock(&gphy->lock);
	if (!!(gphy->phy_users & COMBO_PHY_USER_DP)) {
		ret = -EBUSY;
		goto unlock;
	}
	gphy->role = role;
	set_vbus_valid(gphy);
unlock:
	mutex_unlock(&gphy->lock);
	return ret;
}

static int google_u3phy_setup_role_switch(struct google_usb_phy *gphy)
{
	struct usb_role_switch_desc gphy_role_switch = {0};

	gphy_role_switch.fwnode = dev_fwnode(gphy->dev);
	gphy_role_switch.set = u3phy_usb_role_switch_set;
	gphy_role_switch.driver_data = gphy;
	gphy->role_sw = usb_role_switch_register(gphy->dev, &gphy_role_switch);
	if (IS_ERR(gphy->role_sw))
		return PTR_ERR(gphy->role_sw);

	return 0;
}

static int google_usb_nvmem_read_u32(struct device *dev, char *id, u32 *var)
{
	struct nvmem_cell *cell;
	void *buf;
	size_t len;

	cell = nvmem_cell_get(dev, id);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR(buf)) {
		nvmem_cell_put(cell);
		return PTR_ERR(buf);
	}

	if (len > sizeof(*var))
		return -EINVAL;

	memcpy(var, buf, len);
	kfree(buf);
	nvmem_cell_put(cell);
	return 0;
}

static int google_usb_phy_parse_otp_data(struct google_usb_phy *gphy)
{
	struct google_usb_phy_otpdata *otp = gphy->otp_data;
	struct device *dev = gphy->dev;
	int i, ret;

	for (i = 0; i < GOOGLE_USB_NUM_OTPS; i++) {
		ret = nvmem_cell_read_u8(dev, otp_names[i].otp_enable_string, &otp[i].enable);

		/* OTP definition not present in DT */
		if (ret == -ENOENT)
			continue;
		if (ret < 0) {
			dev_err(dev, "OTP %s read fail(%d)\n", otp_names[i].otp_enable_string, ret);
			return ret;
		}

		ret = google_usb_nvmem_read_u32(dev, otp_names[i].otp_string, &otp[i].data);
		if (ret < 0) {
			dev_err(dev, "OTP %s read fail(%d)\n",  otp_names[i].otp_string, ret);
			return ret;
		}
		otp[i].valid = 1;
		dev_info(dev, "Phy OTP %s - en:%d val:0x%x\n",  otp_names[i].otp_string,
			otp[i].enable, otp[i].data);
	}
	return 0;
}

#define usb2_ovrd_otp(cfg_reg, field_name, index) \
	do { \
		u8 index_ = index; \
		if (otp[index_].valid && otp[index_].enable) \
			cfg_reg.bf.field_name = otp[index_].data; \
	} while (0)

static void google_usb_program_usb2_otps(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	struct google_usb_phy_otpdata *otp = gphy->otp_data;
	USB2PHY_CFG19 phy_cfg19;
	USB2PHY_CFG20 phy_cfg20;
	USB2PHY_CFG21 phy_cfg21;

	phy_cfg19.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG19_OFFSET);
	phy_cfg20.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG20_OFFSET);
	phy_cfg21.reg_value = readl(gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);

	usb2_ovrd_otp(phy_cfg19, phy_cfg_pll_cpbias_cntrl, GOOGLE_USB2_PHY_CFG_PLL_CPBIAS_CNTRL);
	usb2_ovrd_otp(phy_cfg19, phy_cfg_pll_fb_div, GOOGLE_USB2_PHY_CFG_PLL_FB_DIV);
	usb2_ovrd_otp(phy_cfg19, phy_cfg_pll_gmp_cntrl, GOOGLE_USB2_PHY_CFG_PLL_GMP_CNTRL);
	usb2_ovrd_otp(phy_cfg19, phy_cfg_pll_int_cntrl, GOOGLE_USB2_PHY_CFG_PLL_INT_CNTRL);

	usb2_ovrd_otp(phy_cfg20, phy_cfg_pll_prop_cntrl, GOOGLE_USB2_PHY_CFG_PLL_PROP_CNTRL);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_pll_ref_div, GOOGLE_USB2_PHY_CFG_PLL_REF_DIV);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_pll_vco_cntrl, GOOGLE_USB2_PHY_CFG_PLL_VCO_CNTRL);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_pll_vref_tune, GOOGLE_USB2_PHY_CFG_PLL_VREF_TUNE);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_rx_eq_ctle, GOOGLE_USB2_PHY_CFG_RX_EQ_CTLE);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_rx_hs_tune, GOOGLE_USB2_PHY_CFG_RX_HS_TUNE);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_tx_fsls_slew_rate_tune,
		      GOOGLE_USB2_PHY_CFG_TX_FSLS_SLEW_RATE_TUNE);
	usb2_ovrd_otp(phy_cfg20, phy_cfg_tx_fsls_vref_tune, GOOGLE_USB2_PHY_CFG_TX_FSLS_VREF_TUNE);

	usb2_ovrd_otp(phy_cfg21, phy_cfg_tx_hs_vref_tune, GOOGLE_USB2_PHY_CFG_TX_HS_VREF_TUNE);
	usb2_ovrd_otp(phy_cfg21, phy_cfg_tx_hs_xv_tune, GOOGLE_USB2_PHY_CFG_TX_HS_XV_TUNE);
	usb2_ovrd_otp(phy_cfg21, phy_cfg_tx_preemp_tune, GOOGLE_USB2_PHY_CFG_TX_PREEMP_TUNE);
	usb2_ovrd_otp(phy_cfg21, phy_cfg_tx_res_tune, GOOGLE_USB2_PHY_CFG_TX_RES_TUNE);
	usb2_ovrd_otp(phy_cfg21, phy_cfg_tx_rise_tune, GOOGLE_USB2_PHY_CFG_TX_RISE_TUNE);

	if (gphy->otp_data[GOOGLE_USB2_REXT].valid &&
	    !gphy->otp_data[GOOGLE_USB2_REXT].enable) {
		phy_cfg19.bf.phy_cfg_rcal_bypass = 1;
		phy_cfg20.bf.phy_cfg_rcal_offset = 0;
		phy_cfg20.bf.phy_cfg_rcal_code = gphy->otp_data[GOOGLE_USB2_REXT].data;
	}

	writel(phy_cfg19.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG19_OFFSET);
	writel(phy_cfg20.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG20_OFFSET);
	writel(phy_cfg21.reg_value, gphy->usb2_cfg_csr_base + USB2PHY_CFG21_OFFSET);
}

static void google_usb_program_usb3_otps(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	union USBCS_PHY_CFG3 phy_cfg3 = {0};

	if (gphy->otp_data[GOOGLE_USB3_REXT].valid &&
	    !gphy->otp_data[GOOGLE_USB3_REXT].enable) {
		phy_cfg3.reg_value = readl(gphy->dp_top_csr_base + DP_USBCS_PHY_CFG3_OFFSET);
		phy_cfg3.bf.phy0_res_ext_en = 1;
		phy_cfg3.bf.phy0_res_ext_rcal = gphy->otp_data[GOOGLE_USB3_REXT].data;
		writel(phy_cfg3.reg_value, gphy->dp_top_csr_base + DP_USBCS_PHY_CFG3_OFFSET);
	}
}

static int google_dpphy_tune_init(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node, *child, *tune_params_np;
	u32 res[3];
	int rate, vs, pe, ret = 0;
	char tune_name[32];

	/* Initialize tuning parameters. If tuning parameters don't exist, then use message bus */
	tune_params_np = of_get_child_by_name(np, "dp_tune_params");
	if (tune_params_np)
		dp_tune_ovrd_en = true;

	for (rate = 0; rate < DP_TUNE_NUM_RATES; rate++) {
		for (vs = 0; vs < DP_TUNE_NUM_VS; vs++) {
			for (pe = 0; pe < DP_TUNE_NUM_PE; pe++) {
				memset(&tune_name, 0, sizeof(char) * 32);
				snprintf(tune_name, 32, "dp_tune_%s_vs_%d_pe_%d",
					 eq_tune_rates[rate], vs, pe);
				child = of_get_child_by_name(tune_params_np, tune_name);
				if (!child)
					continue;

				ret = of_property_read_u32_array(child, "tune_value", res, 3);
				of_node_put(child);
				if (!ret) {
					gphy->dp_tune_main[rate][vs][pe] = res[0];
					gphy->dp_tune_pre[rate][vs][pe] = res[1];
					gphy->dp_tune_post[rate][vs][pe] = res[2];
					dev_info(dev,
						 "dptx TXEQ: rate:%s vs:%d pe:%d main:%d pre:%d post:%d",
						 eq_tune_rates[rate], vs, pe, res[0], res[1],
						 res[2]);
				/*
				 * dp_tune_main/pre/post are sparse arrays, we don't expect invalid
				 * combinations to be present in the device tree.
				 */
				} else if (ret != -EINVAL) {
					dev_err(dev, "node %s data is not formatted properly, ret %d",
						tune_name, ret);
					goto err_put_tune_params_np;
				}
			}
		}
	}

err_put_tune_params_np:
	of_node_put(tune_params_np);
	return ret;
}

static ssize_t dp_tune_coeff_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct dp_tune_attribute *dp_tune_attr = container_of(attr, struct dp_tune_attribute, attr);
	struct kobject *parent = kobj->parent->parent->parent->parent;
	struct device *dev = container_of(parent, struct device, kobj);
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	u8 res;

	switch (dp_tune_attr->coeff) {
	case DP_TUNE_MAIN:
		res = gphy->dp_tune_main[dp_tune_attr->rate][dp_tune_attr->vs][dp_tune_attr->pe];
		break;
	case DP_TUNE_PRE:
		res = gphy->dp_tune_pre[dp_tune_attr->rate][dp_tune_attr->vs][dp_tune_attr->pe];
		break;
	case DP_TUNE_POST:
		res = gphy->dp_tune_post[dp_tune_attr->rate][dp_tune_attr->vs][dp_tune_attr->pe];
		break;
	default:
		return sysfs_emit(buf, "no coefficient given\n");
	}

	return sysfs_emit(buf, "%d\n", res);
}

static ssize_t dp_tune_coeff_store(struct kobject *kobj, struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct dp_tune_attribute *dp_tune_attr = container_of(attr, struct dp_tune_attribute, attr);
	struct kobject *parent = kobj->parent->parent->parent->parent;
	struct device *dev = container_of(parent, struct device, kobj);
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	int res;

	if (kstrtoint(buf, 10, &res) < 0)
		return count;

	switch (dp_tune_attr->coeff) {
	case DP_TUNE_MAIN:
		gphy->dp_tune_main[dp_tune_attr->rate][dp_tune_attr->vs][dp_tune_attr->pe] = res;
		break;
	case DP_TUNE_PRE:
		gphy->dp_tune_pre[dp_tune_attr->rate][dp_tune_attr->vs][dp_tune_attr->pe] = res;
		break;
	case DP_TUNE_POST:
		gphy->dp_tune_post[dp_tune_attr->rate][dp_tune_attr->vs][dp_tune_attr->pe] = res;
		break;
	default:
		return count;
	}

	return count;
}

static void google_dpphy_sysfs_destroy(struct google_usb_phy *gphy)
{
	struct dp_tune_kobj_data *tune_data = &gphy->dp_tune_kobjs;
	int rate, vs, pe;

	for (rate = 0; rate < DP_TUNE_NUM_RATES; rate++) {
		for (vs = 0; vs < DP_TUNE_NUM_VS; vs++) {
			for (pe = 0; pe < DP_TUNE_NUM_PE; pe++) {
				if (!tune_data->rates[rate].vs[vs].pe[pe].kobj)
					continue;

				if (vs + pe <= 3) {
					sysfs_remove_group(
						tune_data->rates[rate].vs[vs].pe[pe].kobj,
						dp_tune_attr_grps[rate][vs][pe]);
				}
				kobject_put(tune_data->rates[rate].vs[vs].pe[pe].kobj);
				tune_data->rates[rate].vs[vs].pe[pe].kobj = NULL;
			}
			if (tune_data->rates[rate].vs[vs].kobj) {
				kobject_put(tune_data->rates[rate].vs[vs].kobj);
				tune_data->rates[rate].vs[vs].kobj = NULL;
			}
		}
		if (tune_data->rates[rate].kobj) {
			kobject_put(tune_data->rates[rate].kobj);
			tune_data->rates[rate].kobj = NULL;
		}
	}
	kobject_put(tune_data->kobj);
	tune_data->kobj = NULL;
}

static int google_dpphy_sysfs_init(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	struct dp_tune_kobj_data *tune_data = &gphy->dp_tune_kobjs;
	struct kobject *parent = &dev->kobj, *child;
	int rate, vs, pe;
	int ret;

	google_dpphy_sysfs_init_attr_groups();

	/* Create Parent Directory */
	child = kobject_create_and_add("dp_tune", parent);
	if (IS_ERR_OR_NULL(child))
		return -ENODEV;

	tune_data->kobj = child;

	/* Rate Directory */
	for (rate = 0; rate < DP_TUNE_NUM_RATES; rate++) {
		parent = tune_data->kobj;
		child = kobject_create_and_add(eq_tune_rates[rate], parent);
		if (IS_ERR_OR_NULL(child))
			goto error;

		tune_data->rates[rate].kobj = child;
		/* vs Directory */
		for (vs = 0; vs < DP_TUNE_NUM_VS; vs++) {
			parent = tune_data->rates[rate].kobj;
			child = kobject_create_and_add(eq_tune_vs[vs], parent);
			if (IS_ERR_OR_NULL(child))
				goto error;

			tune_data->rates[rate].vs[vs].kobj = child;
			/* pe Directory */
			for (pe = 0; pe < DP_TUNE_NUM_PE; pe++) {
				parent = tune_data->rates[rate].vs[vs].kobj;
				child = kobject_create_and_add(eq_tune_pe[pe], parent);
				if (IS_ERR_OR_NULL(child))
					goto error;
				tune_data->rates[rate].vs[vs].pe[pe].kobj = child;

				/* Main Pre Post */
				if (vs + pe <= 3) {
					ret = sysfs_create_group(child,
								 dp_tune_attr_grps[rate][vs][pe]);
					if (ret)
						goto error;
				}
			}
		}
	}

	return 0;

error:
	google_dpphy_sysfs_destroy(gphy);
	return -ENODEV;
}

GOOGLE_USB_PHY_DEBUGFS_EQ_TUNE_ASIC_READ_LANE_N(0);
GOOGLE_USB_PHY_DEBUGFS_EQ_TUNE_ASIC_READ_LANE_N(1);
GOOGLE_USB_PHY_DEBUGFS_EQ_TUNE_ASIC_READ_LANE_N(2);
GOOGLE_USB_PHY_DEBUGFS_EQ_TUNE_ASIC_READ_LANE_N(3);

static const struct file_operations *google_usb_phy_debugfs_eq_tune_asic_read_main[4] = {
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_0_main_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_1_main_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_2_main_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_3_main_fops,
};

static const struct file_operations *google_usb_phy_debugfs_eq_tune_asic_read_pre[4] = {
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_0_pre_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_1_pre_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_2_pre_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_3_pre_fops,
};

static const struct file_operations *google_usb_phy_debugfs_eq_tune_asic_read_post[4] = {
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_0_post_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_1_post_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_2_post_fops,
	&google_usb_phy_debugfs_eq_tune_asic_read_lane_3_post_fops,
};

static int google_usb_phy_debugfs_init(struct device *dev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	struct dentry *debugfs_lane, *debugfs_coeff, *debugfs_tx_eq;
	int ret;
	char lane_name[16];

	gphy->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR_OR_NULL(gphy->debugfs_root))
		return PTR_ERR(gphy->debugfs_root);

	debugfs_tx_eq = debugfs_create_dir("tx_eq", gphy->debugfs_root);
	if (IS_ERR_OR_NULL(debugfs_tx_eq))
		return PTR_ERR(debugfs_tx_eq);

	/* TX_EQ per lane */
	for (int i = 0; i < 4; i++) {
		memset(&lane_name, 0, (sizeof(char) * 16));
		snprintf(lane_name, 16, "lane%d", i);
		debugfs_lane = debugfs_create_dir(lane_name, debugfs_tx_eq);
		if (IS_ERR_OR_NULL(debugfs_lane)) {
			ret = PTR_ERR(debugfs_lane);
			goto error;
		}
		/* Main */
		debugfs_coeff = debugfs_create_file("main", 0444, debugfs_lane, gphy,
				google_usb_phy_debugfs_eq_tune_asic_read_main[i]);
		if (IS_ERR_OR_NULL(debugfs_coeff)) {
			ret = PTR_ERR(debugfs_coeff);
			goto error;
		}
		/* Pre */
		debugfs_coeff = debugfs_create_file("pre", 0444, debugfs_lane, gphy,
			google_usb_phy_debugfs_eq_tune_asic_read_pre[i]);
		if (IS_ERR_OR_NULL(debugfs_coeff)) {
			ret = PTR_ERR(debugfs_coeff);
			goto error;
		}
		/* Post */
		debugfs_coeff = debugfs_create_file("post", 0444, debugfs_lane, gphy,
			google_usb_phy_debugfs_eq_tune_asic_read_post[i]);
		if (IS_ERR_OR_NULL(debugfs_coeff)) {
			ret = PTR_ERR(debugfs_coeff);
			goto error;
		}
	}

	return 0;
error:
	debugfs_remove_recursive(gphy->debugfs_root);
	return ret;
}

static void google_usb_phy_debugfs_remove(struct google_usb_phy *gphy)
{
	debugfs_remove_recursive(gphy->debugfs_root);
}

static int google_usb_phy_parse_tune_params(struct google_usb_phy *gphy,
		const char *node_name, struct phy_tune_param **params_ptr,
		int *num_params)
{
	int i = 0, cnt, ret = 0;
	const char *name;
	struct device *dev = gphy->dev;
	struct device_node *np = dev->of_node, *child, *tune_params_np;
	struct phy_tune_param *params;
	u32 res[2];

	tune_params_np = of_get_child_by_name(np, node_name);
	cnt = of_get_child_count(tune_params_np);

	params = devm_kcalloc(dev, cnt, sizeof(struct phy_tune_param), GFP_KERNEL);
	if (!params) {
		ret = -ENOMEM;
		goto err_put_tune_params_np;
	}
	*params_ptr = params;

	for_each_child_of_node(tune_params_np, child) {
		ret = of_property_read_string(child, "tune_name", &name);
		if (ret == 0) {
			memcpy(params[i].name, name, strlen(name));
		} else {
			dev_err(dev, "failed to read tune name from %s node\n", child->name);
			goto err_put_tune_params_np;
		}

		ret = of_property_read_u32_array(child, "tune_value", res, 2);
		if (ret == 0) {
			params[i].offset = res[0];
			params[i].value = res[1];
		} else {
			dev_err(dev, "failed to read tune value from %s node\n", child->name);
			goto err_put_tune_params_np;
		}
		dev_dbg(dev, "%s, tune name=%s, offset=0x%x, value=0x%x\n", __func__,
			params[i].name, params[i].offset, params[i].value);
		i++;
	}

err_put_tune_params_np:
	*num_params = i;
	of_node_put(tune_params_np);
	return ret;
}

static ssize_t phy_tune_params_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	struct phy_tune_param *params = gphy->phy_tune_params;
	int len = 0;

	len += sysfs_emit_at(buf, len, "%-48s %s\t%s\n", "Name", "Offset", "Value");
	for (int i = 0; i < gphy->num_phy_tune_params; i++) {
		len += sysfs_emit_at(buf, len, "%-48s 0x%04X\t0x%04X\n",
				     params[i].name, params[i].offset, params[i].value);
	}

	params = gphy->phy_tune_params_g1;

	for (int i = 0; i < gphy->num_phy_tune_params_g1; i++) {
		len += sysfs_emit_at(buf, len, "%-48s 0x%04X\t0x%08X\n",
				     params[i].name, params[i].offset, params[i].value);
	}

	return len;
}
static DEVICE_ATTR_RO(phy_tune_params);

static ssize_t aux_tune_ovrd_en_show(struct device *dev, struct device_attribute *attr,
				     char *buf)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", gphy->aux_tune_ovrd_en);
}

static ssize_t aux_tune_ovrd_en_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	bool val;
	int res;

	res = kstrtobool(buf, &val);
	if (res)
		return res;

	gphy->aux_tune_ovrd_en = val;
	return size;
}
static DEVICE_ATTR_RW(aux_tune_ovrd_en);

static ssize_t aux_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", gphy->aux_tune_params_ovrd.aux_ctrl);
}

static ssize_t aux_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t size)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	int res;
	u8 val;

	res = kstrtou8(buf, 0, &val);
	if (res)
		return res;

	if (val > 0xF) {
		dev_err(dev, "invalid aux_ctrl value: 0x%x\n", val);
		return -EINVAL;
	}

	gphy->aux_tune_params_ovrd.aux_ctrl = val;
	return size;
}
static DEVICE_ATTR_RW(aux_ctrl);

static ssize_t aux_hys_tune_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", gphy->aux_tune_params_ovrd.aux_hys_tune);
}

static ssize_t aux_hys_tune_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	int res;
	u8 val;

	res = kstrtou8(buf, 0, &val);
	if (res)
		return res;

	if (val > 0x3) {
		dev_err(dev, "invalid aux_hys_tune value: 0x%x\n", val);
		return -EINVAL;
	}

	gphy->aux_tune_params_ovrd.aux_hys_tune = val;
	return size;
}
static DEVICE_ATTR_RW(aux_hys_tune);

static ssize_t aux_vod_tune_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", gphy->aux_tune_params_ovrd.aux_vod_tune);
}

static ssize_t aux_vod_tune_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);
	int res;
	u8 val;

	res = kstrtou8(buf, 0, &val);
	if (res)
		return res;

	if (val > 0x1) {
		dev_err(dev, "invalid aux_vod_tune value: 0x%x\n", val);
		return -EINVAL;
	}

	gphy->aux_tune_params_ovrd.aux_vod_tune = val;
	return size;
}
static DEVICE_ATTR_RW(aux_vod_tune);

GOOGLE_USB_PHY_REG_ATTR(sup_dig_lvl_ovrd_in, SUP_DIG_LVL_OVRD_IN, DPTX);

GOOGLE_USB_PHY_REG_ATTR(phy_cfg4, DP_USBCS_PHY_CFG4_OFFSET, DPTOP);
GOOGLE_USB_PHY_REG_ATTR(phy_cfg5, DP_USBCS_PHY_CFG5_OFFSET, DPTOP);
GOOGLE_USB_PHY_REG_ATTR(phy_cfg6, DP_USBCS_PHY_CFG6_OFFSET, DPTOP);

static struct attribute *phy_register_attrs[] = {
	&dev_attr_sup_dig_lvl_ovrd_in.attr,
	&dev_attr_phy_cfg4.attr,
	&dev_attr_phy_cfg5.attr,
	&dev_attr_phy_cfg6.attr,
	NULL
};

static const struct attribute_group phy_registers_group = {
	.name = "phy_registers",
	.attrs = phy_register_attrs,
};

GOOGLE_USB_PHY_COEFF_ATTR(phy_cfg4, DPTOP, main, USBCS_PHY_CFG4);
GOOGLE_USB_PHY_COEFF_ATTR(phy_cfg5, DPTOP, post, USBCS_PHY_CFG5);
GOOGLE_USB_PHY_COEFF_ATTR(phy_cfg6, DPTOP, pre, USBCS_PHY_CFG6);
GOOGLE_USB_PHY_PARA_ATTR(phy_lvl_ovrd, DPTX, vboost, LVL_OVRD_IN);

static struct attribute *phy_coeff_attrs[] = {
	&dev_attr_main.attr,
	&dev_attr_post.attr,
	&dev_attr_pre.attr,
	&dev_attr_vboost.attr,
	NULL
};

static const struct attribute_group phy_coeffs_group = {
	.name = "phy_coeffs",
	.attrs = phy_coeff_attrs,
};

static struct attribute *usb_phy_attrs[] = {
	&dev_attr_phy_tune_params.attr,
	NULL
};

static const struct attribute_group usb_phy_group = {
	.attrs = usb_phy_attrs,
};

static struct attribute *aux_tune_params_attrs[] = {
	&dev_attr_aux_tune_ovrd_en.attr,
	&dev_attr_aux_ctrl.attr,
	&dev_attr_aux_hys_tune.attr,
	&dev_attr_aux_vod_tune.attr,
	NULL
};

static const struct attribute_group aux_tune_params_group = {
	.name = "aux_tune_params",
	.attrs = aux_tune_params_attrs,
};

static const struct attribute_group *usb_phy_groups[] = {
	&usb_phy_group,
	&phy_registers_group,
	&phy_coeffs_group,
	&aux_tune_params_group,
	NULL,
};

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
	struct resource *res;
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

	gphy->phy_state = COMBO_PHY_IDLE;
	gphy->typec_polarity = 0;
	gphy->typec_mode = TYPEC_STATE_SAFE;

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

	gphy->inst[2].phy = devm_phy_create(dev, NULL, &gphy->drv_data->google_dpphy_ops);
	gphy->inst[2].index = 2;
	if (IS_ERR(gphy->inst[2].phy)) {
		dev_err(dev, "Couldn't create DP phy\n");
		return PTR_ERR(gphy->inst[2].phy);
	}
	phy_set_drvdata(gphy->inst[2].phy, &gphy->inst[2]);

	/*
	 * usb2_cfg_csr_base points to the base addess of
	 * USBCS_USB2PHY_CFG19 inside usb_cfg_csr in prim controller
	 * and points to DP_TOP_CSR_AUX_U2PHY_CFG in AUX controllers
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "usb2_cfg_csr");
	if (res) {
		gphy->usb2_cfg_csr_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->usb2_cfg_csr_base))
			return PTR_ERR(gphy->usb2_cfg_csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dp_top_csr");
	if (res) {
		gphy->dp_top_csr_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->dp_top_csr_base))
			return PTR_ERR(gphy->dp_top_csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dptx_tca_regs");
	if (res) {
		gphy->dptx_tca_regs_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->dptx_tca_regs_base))
			return PTR_ERR(gphy->dptx_tca_regs_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dptx_phy_regs");
	if (res) {
		gphy->dptx_phy_regs_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->dptx_phy_regs_base))
			return PTR_ERR(gphy->dptx_phy_regs_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gp_ss_csr");
	if (res) {
		gphy->gp_ss_csr_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->gp_ss_csr_base))
			return PTR_ERR(gphy->gp_ss_csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dp_pipe_lane0");
	if (res) {
		gphy->dp_pipe_lane0_regs_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->dp_pipe_lane0_regs_base))
			return PTR_ERR(gphy->dp_pipe_lane0_regs_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dp_pipe_lane1");
	if (res) {
		gphy->dp_pipe_lane1_regs_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->dp_pipe_lane1_regs_base))
			return PTR_ERR(gphy->dp_pipe_lane1_regs_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dwc3_lcsr_deemph");
	if (res) {
		gphy->dwc3_lcsr_deemph_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->dwc3_lcsr_deemph_base))
			return PTR_ERR(gphy->dwc3_lcsr_deemph_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "usb2_ss");
	if (res) {
		gphy->usb2_ss_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->usb2_ss_base))
			return PTR_ERR(gphy->usb2_ss_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "upcs_config_reg");
	if (res) {
		gphy->upcs_config_reg = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->upcs_config_reg))
			return PTR_ERR(gphy->upcs_config_reg);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_sram");
	if (res) {
		gphy->phy_sram_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(gphy->phy_sram_base))
			return PTR_ERR(gphy->phy_sram_base);

		gphy->phy_sram_size = resource_size(res);
		dev_dbg(dev, "U3Phy SRAM %llx-%llx\n", res->start, res->end);

		ret = device_property_read_string(gphy->dev, "fw-name", &gphy->fw_name);
		if (!ret) {
			dev_info(dev, "Requesting firmware %s\n", gphy->fw_name);
		} else {
			dev_info(dev, "Requesting default firmware %s\n", USB_PHY_FW_BIN_NAME);
			gphy->fw_name = USB_PHY_FW_BIN_NAME;
		}

		ret = request_firmware(&gphy->fw, gphy->fw_name, gphy->dev);
		if (ret) {
			/*
			 * Since the firmware binary resides in userspace (ramdisk), if requested
			 * firmware version isn't found, the system falls back to the default
			 * firmware version instead of failing to load.
			 */
			if (strcmp(gphy->fw_name, USB_PHY_FW_BIN_NAME) != 0) {
				gphy->fw_name = USB_PHY_FW_BIN_NAME;
				dev_info(dev, "Fall back to the default firmware\n");

				ret = request_firmware(&gphy->fw, gphy->fw_name, gphy->dev);
			}

			if (ret == -ENOENT) {
				dev_err(dev, "Firmware not found, defer probing\n");
				return -EPROBE_DEFER;
			}

			if (ret) {
				dev_err(dev, "Fail to request firmware %s\n", gphy->fw_name);
				return ret;
			}
		}

		if (gphy->fw->size > gphy->phy_sram_size) {
			dev_err(dev, "Invalid firmware size\n");
			ret = -EIO;
			goto free_firmware;
		}
	}

	/* PHY Workarounds */
	if (of_property_read_bool(node, "soft-reset-war"))
		gphy->phy_workarounds |= U3_PHY_FORCE_SS_RXDET_DISABLE;

	if (of_property_read_bool(node, "force-u31-war"))
		gphy->phy_workarounds |= U3_PHY_FORCE_USB31_CONNECTED;

	ret = google_dpphy_tune_init(dev);
	if (ret && ret != -EINVAL)
		goto free_firmware;

	ret = google_dpphy_sysfs_init(dev);
	if (ret)
		goto free_firmware;

	ret = google_usb_phy_debugfs_init(dev);
	if (ret)
		goto deinit_sysfs;

	ret = google_usb_phy_parse_clocks(gphy);
	if (ret)
		goto deinit_debugfs;

	ret = google_usb_phy_parse_resets(gphy);
	if (ret)
		goto deinit_debugfs;

	ret = google_usb_phy_parse_otp_data(gphy);
	if (ret)
		goto deinit_debugfs;

	ret = google_usb_phy_parse_tune_params(gphy, "tune_params",
			&gphy->phy_tune_params, &gphy->num_phy_tune_params);
	if (ret)
		goto free_firmware;

	ret = google_usb_phy_parse_tune_params(gphy, "tune_params_g1",
			&gphy->phy_tune_params_g1, &gphy->num_phy_tune_params_g1);
	if (ret)
		goto free_firmware;

	tc_sw_desc.fwnode = dev_fwnode(dev);
	tc_sw_desc.drvdata = gphy;
	tc_sw_desc.name = fwnode_get_name(dev_fwnode(dev));
	tc_sw_desc.set = google_usb_set_orientation;

	gphy->typec_switch = typec_switch_register(dev, &tc_sw_desc);
	if (IS_ERR(gphy->typec_switch)) {
		ret = PTR_ERR(gphy->typec_switch);
		dev_err(dev, "TypeC Switch Register Failed:%d\n", ret);
		goto deinit_debugfs;
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
deinit_debugfs:
	google_usb_phy_debugfs_remove(gphy);
deinit_sysfs:
	google_dpphy_sysfs_destroy(gphy);
free_firmware:
	release_firmware(gphy->fw);
	return ret;
}

static void google_usb_phy_remove(struct platform_device *pdev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "Google USB Phy driver remove\n");
	typec_switch_unregister(gphy->typec_switch);
	usb_role_switch_unregister(gphy->role_sw);
	typec_mux_unregister(gphy->typec_mux);
	google_usb_phy_debugfs_remove(gphy);
	google_dpphy_sysfs_destroy(gphy);
	release_firmware(gphy->fw);
}

static const struct google_usb_phy_driverdata lga_drvdata = {
	.u3p_clk_names = {
		"usb3dp_phy_reset_clk"
	},
	.num_u3p_clks = 1,
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
	.google_dpphy_ops = {
		.init = google_dpphy_init,
		.exit = google_dpphy_exit,
		.owner = THIS_MODULE,
	},
	.pm_callback = lga_pm_callback,
	.program_usb2_otps = google_usb_program_usb2_otps,
	.program_usb3_otps = google_usb_program_usb3_otps,
	.set_power_config = lga_set_power_config,
};

static const struct google_usb_phy_driverdata mbu_drvdata = {
	.u3p_clk_names = {
		"usb3dp_phy_reset_clk"
	},
	.num_u3p_clks = 1,
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
	.google_dpphy_ops = {
		.init = google_dpphy_init,
		.exit = google_dpphy_exit,
		.owner = THIS_MODULE,
	},
	// pm_callback to configure pmgt_ref_clk_req is not required in MBU per b/322477603
	.program_usb2_otps = google_usb_program_usb2_otps,
	.program_usb3_otps = google_usb_program_usb3_otps,
	.set_power_config = mbu_set_power_config,
	.typec_disable_ack_set = mbu_typec_disable_ack_set,
	.mplla_coarse_start_0_val = 0x50,
};

static const struct google_usb_phy_driverdata mbu_aux_drvdata = {
	.google_u2phy_ops = {
		.init = google_u2phy_aux_init,
		.exit = google_u2phy_aux_exit,
		.owner = THIS_MODULE,
	},
};

static const struct of_device_id google_usb_phy_match[] = {
	{
		.compatible = "google,usb_phy-lga",
		.data = &lga_drvdata,
	},
	{
		.compatible = "google,usb_phy-mbu",
		.data = &mbu_drvdata,
	},
	{
		.compatible = "google,usb_phy-mbu-aux",
		.data = &mbu_aux_drvdata,
	},
	{ /* sentinel */ },
};

static struct platform_driver google_usb_phy_driver = {
	.probe = google_usb_phy_probe,
	.remove = google_usb_phy_remove,
	.driver = {
		.name = "google_usb_phy",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&google_usb_phy_dev_pm_ops),
		.of_match_table = google_usb_phy_match,
		.dev_groups = usb_phy_groups,
	}
};

module_platform_driver(google_usb_phy_driver);
MODULE_AUTHOR("GOOGLE LLC");
MODULE_DESCRIPTION("Google USB PHY Driver");
MODULE_LICENSE("GPL");
