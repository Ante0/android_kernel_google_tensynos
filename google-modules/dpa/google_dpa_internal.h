/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_INTERNAL_H
#define _GOOGLE_DPA_INTERNAL_H

#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/types.h>

#include <soc/google/goog_gdmc_regdump_service.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/google_dpa_crash_dump.h>
#include <soc/google/google_dpa_ring_service_proxy_defs.h>

enum google_dpa_fw_state {
	GOOGLE_DPA_FW_UNLOADED,
	GOOGLE_DPA_FW_AUTHENTICATED,
	GOOGLE_DPA_FW_NCP_RELEASED,
	GOOGLE_DPA_FW_RUNNING,
	GOOGLE_DPA_FW_CRASHED,
	GOOGLE_DPA_FW_POWER_OFF_FAILED,
	GOOGLE_DPA_FW_STATE_LAST = GOOGLE_DPA_FW_POWER_OFF_FAILED,
};

struct google_dpa_addr_mapping {
	phys_addr_t mcu_view_addr;
	phys_addr_t soc_view_addr;
	size_t len;
	const char *name;
	void __iomem *ioremapped_addr;
	/*
	 * Use dma_alloc_coherent() to dynamically allocate memory.
	 * The returned DMA address must be saved for subsequent
	 * deallocation.
	 */
	dma_addr_t dma_addr;
	struct list_head node;
};

enum {
	DPA_REGDUMP_NCP = 0,
	DPA_REGDUMP_NEP = 1,
	// DO NOT CHANGE INDEX ABOVE.
	// DO NOT SET INDEX BELOW.
	DPA_REGDUMP_COUNT,
};

// For existing memory dumps, those indexes are explicitly set to be
// compatible with the legacy ramdump parser because the legacy
// ramdump breaks coredump in a pre-defined sequence.
enum {
	DPA_MEMDUMP_SRAM = 0,
	DPA_MEMDUMP_NCP_ITCM = 1,
	DPA_MEMDUMP_NCP_DTCM = 2,
	DPA_MEMDUMP_NEP_ITCM = 3,
	DPA_MEMDUMP_NEP_DTCM = 4,
	DPA_MEMDUMP_NCP_DRAM_CODE = 5,
	DPA_MEMDUMP_NEP_DRAM_CODE = 6,
	DPA_MEMDUMP_NCP_DRAM_BUFFER = 7,
	DPA_MEMDUMP_NEP_DRAM_BUFFER = 8,
	// DO NOT CHANGE INDEX ABOVE.
	// DO NOT SET INDEX BELOW.
	DPA_MEMDUMP_COUNT,
};

struct google_dpa_crash_dump {
	struct gdmc_mba_cortex_m_register_dump reg_dumps[DPA_REGDUMP_COUNT];
	struct google_dpa_mem_dump mem_dumps[DPA_MEMDUMP_COUNT];
	bool mem_dump_valid;
	// This linked list references dump data items that are registered
	// by the Wi-Fi and Modem drivers. Because of this, the DPA driver
	// will not free the data. Its life cycle is maintained by the Wi-Fi
	// and Modem drivers.
	struct list_head registered_ramdump_segments;
	struct mutex registered_ramdump_lock;
};

struct google_dpa_doorbell;

struct google_dpa_mcu_image {
	u8 *data;
	size_t size;
	phys_addr_t pa;
	const u8 *elf_data;
	size_t elf_size;
};

struct google_dpa_mcu {
	struct list_head addr_mappings;
	struct resource_table *rsc_table;
	size_t rsc_table_size;
	const char *firmware_name;
	struct google_dpa_doorbell *doorbell;
	struct google_dpa_mcu_image fw_image;
};

struct google_dpa_debug;

struct google_dpa_netlink;

struct google_dpa_secure_channel;

struct google_dpa_carveout_region {
	phys_addr_t pa;
	u8 *va;
	size_t len;
};

struct google_dpa {
	struct device *dev;
	int doorbell_irq;
	void __iomem *lpm_base;
	u32 lpm_ncp_wait_complete_offset;
	u32 lpm_psm_status_offset;
	/* TODO(b/379034261): Remove this field. */
	u32 shared_info_device_address;
	u64 fw_boot_completion_timeout_us;
	enum google_dpa_fw_state fw_state;
	const char *firmware_name;
	struct google_dpa_mcu ncp;
	struct google_dpa_mcu nep;
	struct gdmc_iface *gdmc_iface;
	struct google_dpa_crash_dump crash_dump;
	struct notifier_block power_domain_notify;
	struct completion power_off_completion;
	struct mutex mutex;
	struct platform_device *sscd_pdev;
	struct clk *uart_clk;
	struct reset_control *uart_clk_reset;
	struct list_head iommu_mappings;
	struct google_dpa_debug *debugfs;
	struct google_dpa_netlink *netlink;
	bool use_secure_boot;
	bool use_log_proxy;
	bool use_merged_firmware_image;
	struct google_dpa_secure_channel *secure_chan;
	struct google_dpa_carveout_region fw_img_carveout;
	struct google_dpa_ring_shared_info *ring_shared_info;
	struct device *pd_vdev;
	struct device_link *pd_link;
	struct google_dpa_doorbell **doorbells;
	size_t num_doorbell;
};

void *google_dpa_da_to_va_internal(struct google_dpa *dpa, struct google_dpa_mcu *mcu, u64 da,
				   size_t len, bool *is_iomem);

void google_dpa_doorbell_deinit(struct google_dpa_doorbell *doorbell);

#ifdef CONFIG_DEBUG_FS
int google_dpa_init_debugfs(struct google_dpa *dpa);
void google_dpa_exit_debugfs(struct google_dpa *dpa);
#else
static int google_dpa_init_debugfs(struct google_dpa *dpa)
{
	return 0;
}

static void google_dpa_exit_debugfs(struct google_dpa *dpa)
{
}

#endif

#endif /* _GOOGLE_DPA_INTERNAL_H */
