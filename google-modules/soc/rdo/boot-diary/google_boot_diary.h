/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */
#ifndef _GOOGLE_BOOT_DIARY_H
#define _GOOGLE_BOOT_DIARY_H

#include <linux/build_bug.h>
#include <linux/types.h>

#define APC_BOOT_DEBUG_LOG_MAX  200
#define STATUS_WITH_TS_SIZE	22
#define BOOT_CONTEXT_SIZE	52

struct status_with_ts {
	u64 time_stamp;
	u16 boot_prog_status;
	s32 args[3];
} __packed;
static_assert(sizeof(struct status_with_ts) == STATUS_WITH_TS_SIZE);

struct boot_diary_log {
	u32 boot_diary_version;
	u32 log_idx;
	bool log_idx_overflow;
	u32 max_log_entries;
	struct status_with_ts data[APC_BOOT_DEBUG_LOG_MAX];
} __packed;
static_assert(sizeof(struct boot_diary_log) ==
	      (13 + sizeof(struct status_with_ts) * APC_BOOT_DEBUG_LOG_MAX));

struct boot_context {
	u64 boot_context_magic;
	u32 boot_context_version;
	struct boot_diary_log *boot_diary;  /* phy_addr_t type pointer */
	u32 boot_interface;
	u32 reset_reason;
	u32 mmu_enable;
	u32 cache_enable;
	u32 usb_dload_disable;
	u32 force_usb_boot_disable;
	u32 enforce_gsa_boot;
	u32 auth_dpm_loaded;
} __packed;
static_assert(sizeof(struct boot_context) == BOOT_CONTEXT_SIZE);

struct google_boot_diary {
	struct device *dev;
	struct dentry *debugfs_root;
};

#endif /* _GOOGLE_BOOT_DIARY_H */
