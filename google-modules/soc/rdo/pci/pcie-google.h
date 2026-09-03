/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2026 Google LLC
 */

#ifndef _PCIE_GOOGLE_H_
#define _PCIE_GOOGLE_H_

#include <clk/clk-cpm.h>

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/devm-helpers.h>
#include <linux/pcie_google_if.h>
#include <linux/pinctrl/consumer.h>
#include <linux/notifier.h>
#include <linux/irqdomain.h>
#include <interconnect/google_icc_helper.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pcie-designware-host-customized.h"
#include "pcie-designware.h"
#include "pci.h"

struct firmware;

#define GPCIE_MAX_IRQ_NAME	64

#define GPCIE_INIT_NUM_RSTS 2

#define PSM_READY(reg)			((reg) & BIT(4))
#define PSM_STATE(reg)			((reg) & GENMASK(3, 0))
#define PSM_STATE_EQ(reg, state)	((PSM_STATE(reg)) == (state))
#define PSM_STATE_LE(reg, state)	((PSM_STATE(reg)) <= (state))

#define TOP_PSM_TIMEOUT_US		1000
#define TOP_PSM_SLEEP_US		100

struct fw_patch_entry {
	__le32 addr;
	__le16 val;
} __packed;

enum link_states {
	L0 = 0,
	RECOVERY,
	L0S,
	L1,
	L11,
	L12,
	L2,
	UNKNOWN
};

enum link_duration_opcodes {
	LINK_DURATION_INIT,
	LINK_DURATION_RESET,
	LINK_DURATION_UP,
	LINK_DURATION_DOWN,
	LINK_DURATION_SPD_CHG,
	LINK_DURATION_OPCODE_MAX
};

enum google_pcie_recovery_flags {
	GPCIE_IN_CPL_TIMEOUT = 0,
	GPCIE_IN_LINK_DOWN,
};

#define GPCIE_RECOVERY_MASK \
	(BIT(GPCIE_IN_LINK_DOWN) | BIT(GPCIE_IN_CPL_TIMEOUT))

struct google_pcie {
	struct device *dev;
	struct dw_pcie *pci;
	const struct firmware *phy_fw;
	void __iomem *top_base;
	void __iomem *phy_sram_base;
	void __iomem *sii_base;
	size_t top_size;
	size_t phy_sram_size;
	size_t sii_size;
	void __iomem *ctrl_psm_state;
	u32 curr_mapped_busdev;
	struct phy *phy;

	struct gpio_desc *perstn_gpio;
	unsigned int perst_delay_us;
	u8 max_link_speed;
	u8 max_link_width;
	u8 current_link_speed; /* in range [1-max speed] if link is up. 0 if link is down */
	u8 current_link_width; /* in range [1-max width] if link is up. 0 if link is down */
	u8 target_link_speed;
	u8 target_link_width;

	u8 link_timeout_ms;
	int domain;
	void *debugfs;
	struct pci_saved_state *saved_state;
	struct list_head node;
	bool enumeration_done;
	bool is_link_up;
	bool power_ready; /* external power, such as from pwrctrl */
	unsigned long recovery_flags;
	bool powered_on;
	bool error_recovery_walk_rpm; /* walk rpm in the error recovery path */
	bool perst_walk_rpm; /* walk rpm in the perst path */
	bool l1_pwrgate_disable;
	bool skip_link_eq;
	bool allow_suspend_in_linkup;

	struct workqueue_struct *recovery_wq;
	struct work_struct cpl_timeout_work;
	struct work_struct link_down_work;
	struct work_struct aggr_err_work;

	struct clk *phy_fw_clk;
	struct clk *aux_clk;

	struct notifier_block top_nb;

	struct reset_control *init_rst;
	struct reset_control *pwr_up_rst;
	struct reset_control *perst_rst;
	struct reset_control *app_hold_phy_rst;

	spinlock_t power_stats_lock;	/* Protect power_stats_show from update */
	spinlock_t power_on_lock;	/* Protect powered_on access */
	spinlock_t link_up_lock;	/* Protect EP access */
	spinlock_t link_duration_lock;	/* Protect link_duration_stats access */
	spinlock_t link_stats_lock;	/* Protect link_stats attributes access */
	struct mutex link_lock;		/* Serialize link poweron and poweroff */
	struct mutex rpm_walk_lock; /* Protect PM bus walks for recovery */

	struct google_pcie_power_stats link_up;
	struct google_pcie_power_stats link_down;
	struct google_pcie_link_duration_stats link_duration_stats;
	struct google_pcie_link_stats link_stats;
	struct google_pcie_link_stats link_stats_reported;

	int cpl_timeout_irq;
	char cpl_timeout_irqname[GPCIE_MAX_IRQ_NAME];
	int link_down_irq;
	char link_down_irqname[GPCIE_MAX_IRQ_NAME];
	int aggr_err_irq;
	char aggr_err_irqname[GPCIE_MAX_IRQ_NAME];
	google_pcie_callback_func cb_func;
	void *cb_priv;

	u32 aer;
	u32 exp;
	u32 l1ss;
	u32 posted_rx_q_credits;

	struct google_icc_path *icc_path;
	u32 avg_bw;
	u32 peak_bw;
	struct cpumask msi_ctrl_to_cpu[MAX_MSI_CTRLS];
	u32 l1_entrance_latency;
	struct regmap *subsystem_general;
	struct regmap *top_psm_state;
	atomic_t coredump_in_progress;

	struct pci_eq_presets presets;
	int (*reset_root_port)(struct pci_host_bridge *bridge, struct pci_dev *dev);
	struct wakeup_source *perst_ws; /* perst wakeup source */
	struct wakeup_source *recovery_ws; /* recovery wakeup source */
};

void google_pcie_assert_perst_n(struct google_pcie *gpcie, bool val);
void google_pcie_hot_reset(struct google_pcie *gpcie);

#ifdef CONFIG_DEBUG_FS
int google_pcie_init_debugfs(struct google_pcie *gpcie);
void google_pcie_exit_debugfs(void *data);
#else
static int google_pcie_init_debugfs(struct google_pcie *gpcie) { return 0; }
static void google_pcie_exit_debugfs(void *data) {}
#endif

void google_pcie_init_devcoredump(struct google_pcie *gpcie);
void google_pcie_create_devcoredump(struct google_pcie *gpcie);

static inline bool google_pcie_in_recovery(struct google_pcie *gpcie)
{
	return (READ_ONCE(gpcie->recovery_flags) & GPCIE_RECOVERY_MASK) != 0;
}

#endif /* _PCIE_GOOGLE_H_ */
