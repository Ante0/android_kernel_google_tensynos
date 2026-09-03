/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cpif shim layer for exynos SOC PCIE
 *
 * Copyright 2023, Google LLC
 *
 */

#ifndef __CPIF_PCIE_SHIM_EXYNOS__
#define __CPIF_PCIE_SHIM_EXYNOS__

#include <linux/exynos-pci-ctrl.h>
#include <linux/exynos-pci-noti.h>

typedef struct exynos_pcie_notify pcie_notify_t;
typedef struct exynos_pcie_register_event pcie_register_event_t;

#define PCIE_EVENT_INVALID EXYNOS_PCIE_EVENT_INVALID
#define PCIE_EVENT_LINKDOWN EXYNOS_PCIE_EVENT_LINKDOWN
#define PCIE_EVENT_LINKUP EXYNOS_PCIE_EVENT_LINKUP
#define PCIE_EVENT_WAKEUP EXYNOS_PCIE_EVENT_WAKEUP
#define PCIE_EVENT_WAKE_RECOVERY EXYNOS_PCIE_EVENT_WAKE_RECOVERY
#define PCIE_EVENT_NO_ACCESS EXYNOS_PCIE_EVENT_NO_ACCESS
#define PCIE_EVENT_CPL_TIMEOUT EXYNOS_PCIE_EVENT_CPL_TIMEOUT
#define PCIE_EVENT_LINKDOWN_RECOVERY_FAIL EXYNOS_PCIE_EVENT_LINKDOWN_RECOVERY_FAIL

#define PCIE_TRIGGER_CALLBACK EXYNOS_PCIE_TRIGGER_CALLBACK
#define PCIE_TRIGGER_COMPLETION EXYNOS_PCIE_TRIGGER_COMPLETION

extern int exynos_pcie_register_event(struct exynos_pcie_register_event *reg);
extern int exynos_pcie_deregister_event(struct exynos_pcie_register_event *reg);
extern void exynos_pcie_rc_register_dump(int ch_num);
extern void exynos_pcie_rc_dump_all_status(int ch_num);
extern void exynos_pcie_rc_print_msi_register(int ch_num);
extern int exynos_pcie_rc_set_outbound_atu(int ch_num, u32 target_addr, u32 offset, u32 size);
extern bool exynos_pcie_rc_get_cpl_timeout_state(int ch_num);
extern void exynos_pcie_rc_set_cpl_timeout_state(int ch_num, bool recovery);
extern bool exynos_pcie_rc_get_sudden_linkdown_state(int ch_num);
extern void exynos_pcie_rc_set_sudden_linkdown_state(int ch_num, bool recovery);
extern void exynos_pcie_rc_force_linkdown_work(int ch_num);
extern int exynos_pcie_rc_chk_link_status(int ch_num);
extern int exynos_pcie_rc_l1ss_ctrl(int enable, int id, int ch_num);
extern int exynos_pcie_poweron(int ch_num, int spd, int width);
extern int exynos_pcie_poweroff(int ch_num);
extern int exynos_pcie_get_max_link_speed(int ch_num);
extern int exynos_pcie_get_max_link_width(int ch_num);
extern int exynos_pcie_rc_change_link_speed(int ch_num, int target_speed);
extern void exynos_pcie_set_perst_gpio(int ch_num, bool on);
extern void exynos_pcie_set_ready_cto_recovery(int ch_num);
extern int register_separated_msi_vector(int ch_num, irq_handler_t handler,
					 void *context, int *irq_num);
extern int exynos_pcie_set_msi_ctrl_addr(int num, u64 msi_ctrl_addr);

#if IS_ENABLED(CONFIG_LINK_DEVICE_PCIE_IOMMU)
extern bool exynos_pcie_is_sysmmu_enabled(int ch_num);
extern void pcie_iommu_tlb_invalidate_all(int hsi_block_num);
extern int pcie_iommu_map(unsigned long iova, phys_addr_t paddr, size_t size,
			  int prot, int hsi_block_num);
extern size_t pcie_iommu_unmap(unsigned long iova, size_t size, int hsi_block_num);
#endif

#define pcie_register_event(event) exynos_pcie_register_event(event)
#define pcie_deregister_event(event) exynos_pcie_deregister_event(event)
#define pcie_register_dump(ch) exynos_pcie_rc_register_dump(ch)
#define pcie_dump_all_status(ch) exynos_pcie_rc_dump_all_status(ch)
#define pcie_print_rc_msi_register(ch) exynos_pcie_rc_print_msi_register(ch)
#define pcie_set_outbound_atu(ch, target_addr, offset, size) \
	exynos_pcie_rc_set_outbound_atu(ch, target_addr, offset, size)
#define pcie_get_cpl_timeout_state(ch) exynos_pcie_rc_get_cpl_timeout_state(ch)
#define pcie_set_cpl_timeout_state(ch, recovery) exynos_pcie_rc_set_cpl_timeout_state(ch, recovery)
#define pcie_get_sudden_linkdown_state(ch) \
	exynos_pcie_rc_get_sudden_linkdown_state(ch)
#define pcie_set_sudden_linkdown_state(ch, recovery) \
	exynos_pcie_rc_set_sudden_linkdown_state(ch, recovery)
#define pcie_force_linkdown_work(ch) exynos_pcie_rc_force_linkdown_work(ch)
#define pcie_check_link_status(ch) exynos_pcie_rc_chk_link_status(ch)
#define pcie_l1ss_ctrl(aspm_state, ch) \
	exynos_pcie_rc_l1ss_ctrl(!!aspm_state, PCIE_L1SS_CTRL_MODEM_IF, ch)
#define pcie_poweron(ch, speed, width) exynos_pcie_poweron(ch, speed, width)
#define pcie_poweroff(ch) exynos_pcie_poweroff(ch)
#define pcie_get_max_link_speed(ch) exynos_pcie_get_max_link_speed(ch)
#define pcie_get_max_link_width(ch) exynos_pcie_get_max_link_width(ch)
#define pcie_change_link_speed(ch, spd) exynos_pcie_rc_change_link_speed(ch, spd)
#define pcie_set_perst_gpio(ch, on) exynos_pcie_set_perst_gpio(ch, on)
#define pcie_set_ready_cto_recovery(ch) exynos_pcie_set_ready_cto_recovery(ch)
#define pcie_register_separated_msi_vector(ch, handler, context, irq) \
	register_separated_msi_vector(ch, handler, context, irq)
#define pcie_set_msi_ctrl_addr(num, msi_ctrl_addr) \
	exynos_pcie_set_msi_ctrl_addr(num, msi_ctrl_addr)

#if IS_ENABLED(CONFIG_LINK_DEVICE_PCIE_IOMMU)
#define PCIE_CH2HSI(ch)	((ch) + 1)

#define pcie_is_sysmmu_enabled(ch_num) \
	exynos_pcie_is_sysmmu_enabled(ch_num)
#define cpif_iommu_tlb_invalidate_all(mc) \
	pcie_iommu_tlb_invalidate_all(PCIE_CH2HSI((mc)->pcie_ch_num))
#define cpif_iommu_map(iova, pa, size, prot, mc) \
	pcie_iommu_map(iova, pa, size, prot, PCIE_CH2HSI((mc)->pcie_ch_num))
#define cpif_iommu_unmap(iova, size, mc) \
	pcie_iommu_unmap(iova, size, PCIE_CH2HSI((mc)->pcie_ch_num))
#endif

#endif /* __CPIF_PCIE_SHIM_EXYNOS__ */
