/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#ifndef _PCIE_GOOGLE_IF_H
#define _PCIE_GOOGLE_IF_H

#include <linux/types.h>

#define GPCIE_NUM_LINK_SPEEDS		4

enum google_pcie_callback_type {
	GPCIE_CB_CPL_TIMEOUT,
	GPCIE_CB_LINK_DOWN,
	GPCIE_CB_FATAL_ERR,
	GPCIE_CB_UNKNOWN,
};

struct google_pcie_power_stats {
	u64 count;
	u64 duration;
	u64 last_entry_ms;
};

struct google_pcie_link_duration_stats {
	int last_link_speed;
	struct link_entry {
		u64 count;
		u64 duration;
		u64 last_entry_ts;
	} speed[GPCIE_NUM_LINK_SPEEDS];
};

struct google_pcie_link_stats {
	u64 link_up_failure_count;
	u64 link_recovery_failure_count;
	u64 link_down_irq_count;
	u64 complete_timeout_irq_count;
	u64 link_up_time_avg;
};

typedef void (*google_pcie_callback_func)(enum google_pcie_callback_type type,
		void *priv);

int google_pcie_rc_poweron(int num);
int google_pcie_rc_poweroff(int num);
void google_pcie_rc_prepare_for_forced_poweroff(int num);
int google_pcie_poweron_withspeed(int num, unsigned int speed);
int google_pcie_set_msi_ctrl_addr(int num, u64 msi_ctrl_addr);
int google_pcie_link_state(int num);
int google_pcie_get_power_stats(int num,
				struct google_pcie_power_stats *link_up,
				struct google_pcie_power_stats *link_down);
int google_pcie_get_link_duration(int num,
				struct google_pcie_link_duration_stats *link_duration);
int google_pcie_get_link_stats(int num,
				struct google_pcie_link_stats *link_stats);
int google_pcie_link_status(int num);
int google_pcie_inbound_atu_cfg(int ch_num, unsigned long long src_addr,
				unsigned long long dst_addr, unsigned long size, int atu_index);
int google_pcie_get_max_link_speed(int num);
int google_pcie_get_max_link_width(int num);
int google_pcie_rc_change_link_speed(int num, unsigned int speed);
int google_pcie_rc_change_link_width(int num, unsigned int width);

int google_pcie_register_callback(int num, google_pcie_callback_func cb_func, void *priv);
int google_pcie_unregister_callback(int num);
void google_pcie_dump_debug(int num);
bool google_pcie_is_link_down(int num);
bool google_pcie_is_cpl_timeout(int num);

struct device;
struct pci_pwrctrl;

void google_pci_pwrctrl_init(struct pci_pwrctrl *pwrctrl, struct device *dev);
int google_pci_pwrctrl_device_set_ready(struct pci_pwrctrl *pwrctrl);
void google_pci_pwrctrl_device_unset_ready(struct pci_pwrctrl *pwrctrl);
int devm_google_pci_pwrctrl_device_set_ready(struct device *dev,
					     struct pci_pwrctrl *pwrctrl);
/*
 * Override pci-pwrctrl, because the upstream framework has not yet integrated
 * PERST and link-training-retry.
 */
#define pci_pwrctrl_init google_pci_pwrctrl_init
#define pci_pwrctrl_device_set_ready google_pci_pwrctrl_device_set_ready
#define pci_pwrctrl_device_unset_ready google_pci_pwrctrl_device_unset_ready
#define devm_pci_pwrctrl_device_set_ready devm_google_pci_pwrctrl_device_set_ready

#endif /* _PCIE_GOOGLE_IF_H */
