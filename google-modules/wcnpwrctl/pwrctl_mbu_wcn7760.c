/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WCN Subsystem Power Control Driver - MBU WCN7760 Platform Ops
 *
 * This file contains the hardware-specific power sequencing for the
 * WCN7760 chipset on the MBU platform. It implements the precise
 * timing for LDO rails, bootstrap configuration, and host-side
 * pinctrl state transitions required for chipset bring-up.
 */

#include "wcn_pwrctl_priv.h"

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <ap-pmic/da9188.h>

/* PMIC Register Addresses for MBU WCN7760 */
#define SEQ_LDO4M_8		0x1887
#define SEQ_LDO4M_1		0x1880
#define SEQ_LDO4M_2 		0x1881
#define SEQ_XTAL_CTRL_1		0x1988
#define RTC_CLK_OUT_PERI32K1	0x7807

/* CPM Register for Virtual GPIO BT Control */
#define GPIO_VGPIO3		0x7003
#define V_BR_ON_IN		(1 << 1)
#define V_BR_ON_LE		(1 << 2)
#define V_BR_ON_DEFAULT		0x70

/* CPM Virtual GPIO Latch Timing Requirements for BT_EN */
#define BT_EN_LATCH_SETUP_MIN_US	5000
#define BT_EN_LATCH_SETUP_MAX_US	6000

#define BT_EN_LATCH_PULSE_MIN_US	5000
#define BT_EN_LATCH_PULSE_MAX_US	6000

#define BT_EN_LATCH_HOLD_MIN_US		3000
#define BT_EN_LATCH_HOLD_MAX_US		4000

struct pmic_mfd_mbox wcn7760_cpm_mbox;

static void wcn_wifi_pinctrl_setup(struct wcn_pwrctl_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	struct device_node *host_np_wifi;
	struct platform_device *host_pdev_wifi;

        host_np_wifi = of_parse_phandle(np, "wifi-host", 0);
	if (!host_np_wifi)
		return;

	host_pdev_wifi = of_find_device_by_node(host_np_wifi);
	if (host_pdev_wifi) {
		data->pinctrl_host_wifi = pinctrl_get(&host_pdev_wifi->dev);
		if (!IS_ERR(data->pinctrl_host_wifi)) {
			data->wlan_en_sleep =
				pinctrl_lookup_state(data->pinctrl_host_wifi,
						     "wlan_en_sleep");
			data->wlan_shutdown =
				pinctrl_lookup_state(data->pinctrl_host_wifi,
						     "wlan_shutdown");

		}
		put_device(&host_pdev_wifi->dev);
	}
	of_node_put(host_np_wifi);
}

static void wcn_pcie_pinctrl_setup(struct wcn_pwrctl_data *data)
{
        struct device *dev = data->dev;
        struct device_node *np = dev->of_node;
        struct device_node *host_np_pcie;
        struct platform_device *host_pdev_pcie;

	host_np_pcie = of_parse_phandle(np, "pcie-host", 0);
	if (!host_np_pcie)
		return;

	host_pdev_pcie = of_find_device_by_node(host_np_pcie);
	if (host_pdev_pcie) {
		data->pinctrl_host_pcie = devm_pinctrl_get(&host_pdev_pcie->dev);
		if (!IS_ERR(data->pinctrl_host_pcie)) {
			data->pcie_shutdown =
				pinctrl_lookup_state(data->pinctrl_host_pcie,
						     "pcie_shutdown");
		}
		put_device(&host_pdev_pcie->dev);
	}
	of_node_put(host_np_pcie);
}

static void wcn_uart_pinctrl_setup(struct wcn_pwrctl_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	struct device_node *uart_np;
	struct platform_device *uart_pdev;

	uart_np = of_parse_phandle(np, "uart-handle", 0);
	if (!uart_np)
		return;

	uart_pdev = of_find_device_by_node(uart_np);
	if (uart_pdev) {
		data->pinctrl_uart = devm_pinctrl_get(&uart_pdev->dev);
		if (!IS_ERR(data->pinctrl_uart)) {
			data->cts_tx_no_pull =
				pinctrl_lookup_state(data->pinctrl_uart,
						     "cts_tx_no_pull");
			if (IS_ERR(data->cts_tx_no_pull))
				pr_err("cts_tx_no_pull state not found in UART DT\n");

			data->shutdown_bt_uart_ctrl =
				pinctrl_lookup_state(data->pinctrl_uart,
						     "shutdown_bt_uart_ctrl");
			if (IS_ERR(data->shutdown_bt_uart_ctrl))
				pr_err("shutdown_bt_uart_ctrl state not found in UART DT\n");
		} else {
			pr_err("Failed to get UART pinctrl\n");
		}
		put_device(&uart_pdev->dev);
	} else {
		pr_err("Failed to find UART platform device\n");
	}
	of_node_put(uart_np);
}

static int mbu_wcn7760_init(struct wcn_pwrctl_data *data)
{
	struct device *dev = data->dev;
	int ret;

	pr_info("Initializing MBU WCN7760 specific resources\n");

	data->cpm_mbox = &wcn7760_cpm_mbox;

	data->wlan_power_en =
		devm_gpiod_get_optional(dev, "wlan-power-en", GPIOD_OUT_LOW);
	if (IS_ERR(data->wlan_power_en)) {
		ret = PTR_ERR(data->wlan_power_en);
		if (ret != -EPROBE_DEFER)
			pr_err("Failed to get wlan-power-en GPIO: %d\n", ret);
		return ret;
	}

	data->wlan_bs_config =
		devm_gpiod_get_optional(dev, "wlan-bs-config", GPIOD_OUT_LOW);
	if (IS_ERR(data->wlan_bs_config)) {
		ret = PTR_ERR(data->wlan_bs_config);
		if (ret != -EPROBE_DEFER)
			pr_err("Failed to get wlan-bs-config GPIO: %d\n", ret);
		return ret;
	}

	data->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(data->pinctrl)) {
		ret = PTR_ERR(data->pinctrl);
		if (ret != -EPROBE_DEFER)
			pr_err("Failed to get pinctrl: %d\n", ret);
		return ret;
	}

	data->host_wake_init =
		pinctrl_lookup_state(data->pinctrl, "pcie_host_wake_init");
	if (IS_ERR(data->host_wake_init))
		pr_err("pcie_host_wake_init state not found in DT\n");

	data->host_wake_default =
		pinctrl_lookup_state(data->pinctrl, "pcie_host_wake_default");
	if (IS_ERR(data->host_wake_default))
		pr_err("pcie_host_wake_default not found in DT\n");

	data->host_wake_shutdown =
		pinctrl_lookup_state(data->pinctrl, "pcie_host_wake_shutdown");
	if (IS_ERR(data->host_wake_shutdown))
		pr_err("pcie_host_wake_shutdown not found in DT\n");

	wcn_pcie_pinctrl_setup(data);
	wcn_uart_pinctrl_setup(data);

	return 0;
}

static int wcn_set_bt_en(struct wcn_pwrctl_data *data, bool enable)
{
	int ret;
	u8 val_in = enable ? V_BR_ON_IN : 0;
	u8 base_val = V_BR_ON_DEFAULT;

	pr_info("Setting BT_EN to %d via CPM register 0x%04x\n", enable, GPIO_VGPIO3);

	/* Step 1: Set V_BR_ON_IN value, keep V_BR_ON_LE low */
	ret = wcn_cpm_write_reg(data, GPIO_VGPIO3, base_val | val_in);
	if (ret)
		return ret;
	usleep_range(BT_EN_LATCH_SETUP_MIN_US, BT_EN_LATCH_SETUP_MAX_US);

	/* Step 2: Toggle V_BR_ON_LE high */
	ret = wcn_cpm_write_reg(data, GPIO_VGPIO3, base_val | val_in | V_BR_ON_LE);
	if (ret)
		return ret;
	usleep_range(BT_EN_LATCH_PULSE_MIN_US, BT_EN_LATCH_PULSE_MAX_US);

	/* Step 3: Toggle V_BR_ON_LE low */
	ret = wcn_cpm_write_reg(data, GPIO_VGPIO3, base_val | val_in);
	if (ret)
		return ret;
	usleep_range(BT_EN_LATCH_HOLD_MIN_US, BT_EN_LATCH_HOLD_MAX_US);

	return 0;
}

static int mbu_wcn7760_power_on(struct wcn_pwrctl_data *data)
{
	int ret;

	pr_info("Executing MBU WCN7760 WCN Power On Sequence\n");

	/* 1. Set Host Wake to INIT state (Output Low) before power rails come up */
	if (!IS_ERR_OR_NULL(data->host_wake_init)) {
		ret = pinctrl_select_state(data->pinctrl, data->host_wake_init);
		if (ret)
			pr_err("Failed to select host_wake_init: %d\n", ret);
	}

	/* BT reset sequence (one-time reset before LDO clear) */
	wcn_set_bt_en(data, false);

	/* 2. Initial LDO clear */
	ret = wcn_cpm_write_reg(data, SEQ_LDO4M_1, 0x0);
	if (ret) goto err;

	ret = wcn_cpm_write_reg(data, SEQ_LDO4M_8, 0x0);
	if (ret) goto err;
	usleep_range(800000, 800100);

	/* 3. Configure and Enable LDO4M */
	ret = wcn_cpm_write_reg(data, SEQ_LDO4M_2, 0x0b);
	if (ret) goto err;
	usleep_range(3000, 4000);

	ret = wcn_cpm_write_reg(data, SEQ_LDO4M_1, 0x80);
	if (ret) goto err;
	usleep_range(1000, 2000);

	/* 4. Pull WLAN_POWER_EN High */
	if (data->wlan_power_en) {
		gpiod_set_value(data->wlan_power_en, 1);
		pr_info("WLAN_POWER_EN set to: %d\n",
			gpiod_get_value(data->wlan_power_en));
	}
	usleep_range(5000, 6000);

	/* 5. Enable RTC Clock */
	ret = wcn_cpm_write_reg(data, RTC_CLK_OUT_PERI32K1, 0x1);
	if (ret) goto err;

	/* 6. Pull Bootstrap Config High */
	if (data->wlan_bs_config) {
		gpiod_set_value(data->wlan_bs_config, 1);
		pr_info("WLAN_BS_CONFIG set to: %d\n",
			gpiod_get_value(data->wlan_bs_config));
	}

	/* 7. Set Host Wake to DEFAULT state (Input Enable/Pull-up) after power on */
	if (!IS_ERR_OR_NULL(data->host_wake_default)) {
		ret = pinctrl_select_state(data->pinctrl, data->host_wake_default);
		if (ret)
			pr_err("Failed to select host_wake_default: %d\n", ret);
	}

	return 0;
err:
	pr_err("MBU WCN7760 Power On sequence failed\n");
	return ret;
}

static void mbu_wcn7760_power_off(struct wcn_pwrctl_data *data)
{
	int ret = 0;
	pr_info("Executing MBU WCN7760 Power Off Sequence (FMD: %d)\n", data->fmd_enabled);

	wcn_wifi_pinctrl_setup(data);
	if (data->fmd_enabled) {
		/* Set UART CTS and TX to No Pull */
		if (!IS_ERR_OR_NULL(data->cts_tx_no_pull)) {
			ret = pinctrl_select_state(data->pinctrl_uart, data->cts_tx_no_pull);
			if (ret)
				pr_err("Failed to select cts_tx_no_pull state: %d\n", ret);
		}
		if (!IS_ERR_OR_NULL(data->pinctrl_host_wifi)) {
			if (!IS_ERR_OR_NULL(data->wlan_en_sleep)) {
				ret = pinctrl_select_state(data->pinctrl_host_wifi,
					data->wlan_en_sleep);
				if (ret)
					pr_err("Failed to select wlan_en_sleep: %d\n", ret);
			}
			if (!IS_ERR_OR_NULL(data->wlan_shutdown)) {
				ret = pinctrl_select_state(data->pinctrl_host_wifi,
					data->wlan_shutdown);
				if (ret)
					pr_err("Failed to select wlan_shutdown: %d\n", ret);
			}
		}
		pr_info("FMD Enabled: Set LDO4M to AON Mode\n");
		wcn_cpm_write_reg(data, SEQ_LDO4M_8, 0xc0);
		wcn_cpm_write_reg(data, SEQ_XTAL_CTRL_1, 0x0);
		wcn_cpm_write_reg(data, RTC_CLK_OUT_PERI32K1, 0x0);
	} else {
		if (!IS_ERR_OR_NULL(data->shutdown_bt_uart_ctrl)) {
			ret = pinctrl_select_state(data->pinctrl_uart, data->shutdown_bt_uart_ctrl);
			if (ret)
				pr_err("Failed to select shutdown_bt_uart_ctrl state: %d\n", ret);
		}
		if (!IS_ERR_OR_NULL(data->host_wake_shutdown)) {
			ret = pinctrl_select_state(data->pinctrl,
						data->host_wake_shutdown);
			if (ret)
				pr_err("Failed to select host_wake_shutdown: %d\n", ret);
		}
		if (!IS_ERR_OR_NULL(data->pcie_shutdown)
				&& !IS_ERR_OR_NULL(data->pinctrl_host_pcie)) {
			ret = pinctrl_select_state(data->pinctrl_host_pcie,
						data->pcie_shutdown);
			if (ret)
				pr_err("Failed to select pcie_shutdown: %d\n", ret);
		}
		pr_info("Normal Shutdown: Disable LDO4M\n");
		wcn_cpm_write_reg(data, SEQ_XTAL_CTRL_1, 0x0);
		wcn_cpm_write_reg(data, RTC_CLK_OUT_PERI32K1, 0x0);
		if (data->wlan_power_en) {
			gpiod_set_value(data->wlan_power_en, 0);
			pr_info("WLAN_POWER_EN set to: %d\n",
				gpiod_get_value(data->wlan_power_en));
		}
		if (data->wlan_bs_config) {
			gpiod_set_value(data->wlan_bs_config, 0);
			pr_info("WLAN_BS_CONFIG set to: %d\n",
				gpiod_get_value(data->wlan_bs_config));
		}
		if (!IS_ERR_OR_NULL(data->wlan_en_sleep)
				&& !IS_ERR_OR_NULL(data->pinctrl_host_wifi)) {
			ret = pinctrl_select_state(data->pinctrl_host_wifi,
						data->wlan_en_sleep);
			if (ret)
				pr_err("Failed to select wlan_en_sleep: %d\n", ret);
		}
		/* Deassert BT when device shutdown */
		wcn_set_bt_en(data, false);

		wcn_cpm_write_reg(data, SEQ_LDO4M_1, 0x0);
	}
}

/* Bind MBU WCN7760 specific operations */
const struct wcn_pwrctl_ops mbu_wcn7760_pwrctl_ops = {
	.init = mbu_wcn7760_init,
	.power_on = mbu_wcn7760_power_on,
	.power_off = mbu_wcn7760_power_off,
	.mbox_request = da9188_mfd_mbox_request,
	.mbox_send_req = da9188_mfd_mbox_send_req_blocking,
	.mbox_release = da9188_mfd_mbox_release
};
