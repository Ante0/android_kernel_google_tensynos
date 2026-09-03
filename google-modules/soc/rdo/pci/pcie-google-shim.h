/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef _PCIE_GOOGLE_SHIM_H
#define _PCIE_GOOGLE_SHIM_H

void exynos_pcie_set_perst_gpio(int ch_num, bool on);
void exynos_pcie_set_ready_cto_recovery(int ch_num);
void exynos_pcie_set_skip_config(int ch_num, bool val);
int exynos_pcie_rc_set_outbound_atu(int ch_num, u32 target_addr, u32 offset, u32 size);
void exynos_pcie_rc_print_msi_register(int ch_num);
void exynos_pcie_rc_register_dump(int ch_num);
void exynos_pcie_rc_dump_all_status(int ch_num);
void exynos_pcie_rc_force_linkdown_work(int ch_num);
bool exynos_pcie_rc_get_sudden_linkdown_state(int ch_num);
void exynos_pcie_rc_set_sudden_linkdown_state(int ch_num, bool recovery);
bool exynos_pcie_rc_get_cpl_timeout_state(int ch_num);
void exynos_pcie_rc_set_cpl_timeout_state(int ch_num, bool recovery);
int exynos_pcie_l1_exit(int ch_num);
int exynos_pcie_rc_l1ss_ctrl(int enable, int id, int ch_num);
int exynos_pcie_rc_chk_link_status(int ch_num);
int exynos_pcie_register_event(void *reg);
int exynos_pcie_deregister_event(void *reg);
int register_separated_msi_vector(int ch_num, irq_handler_t handler, void *context, int *irq_num);

#endif /* _PCIE_GOOGLE_SHIM_H */
