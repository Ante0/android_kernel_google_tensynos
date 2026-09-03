// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header for tethering NEP tables manager.
 *
 * This header provides related functions needed
 * for managing tethering NEP tables from the
 * APC netengine driver side.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifndef __NOA_NEP_TABLE_MANAGER_H__
#define __NOA_NEP_TABLE_MANAGER_H__

#include <common/map_def.h>

#define STRUCT_SIZE(name, size) \
  static_assert(sizeof(name) == (size), "Incorrect struct size.")

struct offset_list_head {
    uint32_t next_offset; // Offset to the next list_head
    uint32_t prev_offset; // Offset to the previous list_head
};

struct offset_nep_map_entry {
	uint32_t key_addr;
	uint32_t value_addr;
	uint64_t node_addr;
	u32 hash;
	// Since kernel is 8-byte alignment, which differ from NOA that is 4-byte
	// alignment, pad 4-byte zero to ensure consistency.
	uint8_t zero[4];
};
STRUCT_SIZE(struct offset_nep_map_entry, 24);

/**
 * @brief Set NEP table shared memory region.
 *
 * @param table shared nep_table base address.
 * @return 0 on success, otherwise -EINVAL on failed attempt.
 */
int register_tethering_shared_info(uint64_t base_addr, uint64_t diff,
	uint32_t upstream4_buckets_addr_offset, uint32_t downstream4_buckets_addr_offset);

/**
 * @brief Checks entry that needs update timeout.
 * @param base_time base time to compare, obtained from NOA.
 */
void entry_update_timeout(uint64_t base_time);

#endif  // __NOA_NEP_TABLE_MANAGER_H__
