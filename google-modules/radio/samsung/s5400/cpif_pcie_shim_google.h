/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cpif shim layer for google SOC PCIE
 *
 * Copyright 2023, Google LLC
 *
 */

#ifndef __CPIF_PCIE_SHIM_GOOGLE__
#define __CPIF_PCIE_SHIM_GOOGLE__

#include <linux/pcie_google_if.h>
#include "modem_prj.h"

enum pcie_event {
	PCIE_EVENT_INVALID = 0,
	PCIE_EVENT_LINKDOWN = BIT(0),
	PCIE_EVENT_LINKUP = BIT(1),
	PCIE_EVENT_WAKEUP = BIT(2),
	PCIE_EVENT_WAKE_RECOVERY = BIT(3),
	PCIE_EVENT_NO_ACCESS = BIT(4),
	PCIE_EVENT_CPL_TIMEOUT = BIT(5),
	PCIE_EVENT_LINKDOWN_RECOVERY_FAIL = BIT(6),
};

enum pcie_trigger {
	PCIE_TRIGGER_CALLBACK,
	PCIE_TRIGGER_COMPLETION,
};

typedef struct google_pcie_notify {
	enum pcie_event event;
	struct pci_dev *user;
	void *data;
	u32 options;
} pcie_notify_t;

typedef struct google_pcie_register_event {
	u32 events;
	struct pci_dev *user;
	enum pcie_trigger mode;
	void (*callback)(struct google_pcie_notify *notify);
	struct google_pcie_notify notify;
	struct completion *completion;
	u32 options;
} pcie_register_event_t;

void pcie_unimplemented_return_void(const char *s);
int pcie_unimplemented_return_int_0(const char *s);
int pcie_l1ss_ctrl(int aspm_state, int ch_num);
int pcie_register_event(pcie_register_event_t *reg);
int pcie_deregister_event(pcie_register_event_t *reg);
void pcie_check_pending_msi(struct modem_ctl *mc);

#if IS_ENABLED(CONFIG_LINK_DEVICE_PCIE_IOMMU)
bool pcie_is_sysmmu_enabled(int ch_num);
void cpif_iommu_tlb_invalidate_all(struct modem_ctl *mc);
int cpif_iommu_map(unsigned long iova, phys_addr_t paddr, size_t size,
			  int prot, struct modem_ctl *mc);
size_t cpif_iommu_unmap(unsigned long iova, size_t size, struct modem_ctl *mc);
#endif

#define pcie_register_dump(ch) google_pcie_dump_debug(ch)
#define pcie_dump_all_status(ch) pcie_unimplemented_return_void("pcie_dump_all_status")
#define pcie_print_rc_msi_register(ch) pcie_unimplemented_return_void("pcie_print_rc_msi_register")
#define pcie_set_outbound_atu(ch, target_addr, offset, size) \
	pcie_unimplemented_return_int_0("pcie_set_outbound_atu")
#define pcie_get_cpl_timeout_state(ch)  google_pcie_is_cpl_timeout(ch)
#define pcie_set_cpl_timeout_state(...) \
	pcie_unimplemented_return_void("pcie_set_cpl_timeout_state")
#define pcie_get_sudden_linkdown_state(ch) google_pcie_is_link_down(ch)
#define pcie_set_sudden_linkdown_state(...) \
	pcie_unimplemented_return_void("pcie_set_sudden_linkdown_state")
#define pcie_force_linkdown_work(ch) pcie_unimplemented_return_void("pcie_force_linkdown_work")
#define pcie_check_link_status(ch) google_pcie_link_status(ch)
#define pcie_poweron(ch, speed, width) google_pcie_poweron_withspeed(ch, speed)
#define pcie_poweroff(ch) google_pcie_rc_poweroff(ch)
#define pcie_get_max_link_speed(ch) google_pcie_get_max_link_speed(ch)
#define pcie_get_max_link_width(ch) google_pcie_get_max_link_width(ch)
#define pcie_change_link_speed(ch,spd) google_pcie_rc_change_link_speed(ch, spd)
#define pcie_set_perst_gpio(ch, on) pcie_unimplemented_return_void("pcie_set_perst_gpio")
#define pcie_set_ready_cto_recovery(ch) google_pcie_rc_poweroff(ch)
#define pcie_set_msi_ctrl_addr(num, addr) google_pcie_set_msi_ctrl_addr(num, addr);
#define pcie_register_separated_msi_vector(ch, handler, context, irq) \
	pcie_unimplemented_return_int_0("pcie_register_separated_msi_vector")

#endif /* __CPIF_PCIE_SHIM_GOOGLE__ */
