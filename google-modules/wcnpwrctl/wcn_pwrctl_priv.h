/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WCN Subsystem Power Control Driver - Private Internal Header
 *
 * This file contains the internal data structures and function prototypes
 * used to bridge the core framework and platform-specific operations.
 * It defines the operational callbacks and hardware-mapping structures
 * for the power control sequence.
 */

#ifndef __WCN_PWRCTL_PRIV_H__
#define __WCN_PWRCTL_PRIV_H__

//#define pr_fmt(fmt) "wcn_pwrctl: " fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/notifier.h>
#include <linux/pinctrl/consumer.h>
#include <linux/workqueue.h>

struct wcn_pwrctl_data;
struct pmic_mfd_mbox;
struct mailbox_data;

/* Platform-specific operation callbacks */
struct wcn_pwrctl_ops {
	int (*init)(struct wcn_pwrctl_data *data);
	int (*power_on)(struct wcn_pwrctl_data *data);
	void (*power_off)(struct wcn_pwrctl_data *data);

	/* * Function pointer to handle PMIC-specific mailbox requests.
	 * This abstracts the specific PMIC routing away from the core logic.
	 */
	int (*mbox_send_req)(struct device *dev,
				      struct pmic_mfd_mbox *mbox,
				      u8 mbox_dst, u8 target, u8 cmd,
				      u16 id_or_addr,
				      struct mailbox_data req_data);
	int (*mbox_request)(struct device *dev,
			    struct pmic_mfd_mbox *mbox);
	void (*mbox_release)(struct pmic_mfd_mbox *mbox);
};

/* Core driver data structure */
struct wcn_pwrctl_data {
	struct device *dev;
	const struct wcn_pwrctl_ops *ops;

	/* CPM Mailbox */
	struct pmic_mfd_mbox *cpm_mbox;
	bool cpm_initialized;

	/* GPIOs */
	struct gpio_desc *wlan_power_en;
	struct gpio_desc *wlan_bs_config;

	/* Pinctrl States */
	struct pinctrl *pinctrl;
	struct pinctrl *pinctrl_host_pcie;
	struct pinctrl *pinctrl_host_wifi;
	struct pinctrl_state *host_wake_init;
	struct pinctrl_state *host_wake_default;
	struct pinctrl_state *host_wake_shutdown;
	struct pinctrl_state *pcie_shutdown;
	struct pinctrl_state *wlan_en_sleep;
	struct pinctrl_state *wlan_shutdown;

	/* UART Pinctrl States */
	struct pinctrl *pinctrl_uart;
	struct pinctrl_state *cts_tx_no_pull;
	struct pinctrl_state *shutdown_bt_uart_ctrl;

	/* Reboot Notifier */
	struct notifier_block reboot_nb;

	/* Find My Device (FMD) state */
	bool fmd_enabled;

	/* Async Power On */
	struct work_struct power_on_work;
};

/* Helper function for platform drivers to write PMIC registers */
int _wcn_cpm_write_reg(struct wcn_pwrctl_data *data, u16 reg, const char *reg_name, u8 val);

/* * The Preprocessor Macro.
 * The '#' symbol converts the register name you type into a string automatically.
 */
#define wcn_cpm_write_reg(data, reg, val) \
	_wcn_cpm_write_reg(data, reg, #reg, val)

/* M27 platform operations */
extern const struct wcn_pwrctl_ops mbu_wcn7760_pwrctl_ops;

#endif /* __WCN_PWRCTL_PRIV_H__ */
