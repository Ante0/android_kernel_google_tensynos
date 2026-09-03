// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tethering NEP tables manager.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#include "nep_table_manager.h"

#include "noa_tethering_manager.h"

#include <common/map_def.h>
#include <net/public/net/nep_map_table.h>

/**
 * @brief The struct for this entry
 *
 * @param ptr The &struct offset_list_head pointer.
 * @param type The type of the struct this is embedded in.
 * @param member The name of the list_head within the struct.
 */
#define offset_list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief Iterate over a list safe against removal of list entry
 *
 * @param pos The &struct offset_list_head to use as a loop cursor.
 * @param n Another &struct offset_list_head to use as temporary storage
 * @param head The head for the list.
 */
#define offset_list_for_each_safe(pos, n, head) \
    for (pos = list_da_to_va((head)->next_offset), \
         n = list_da_to_va(pos->next_offset); \
         pos != (head); \
         pos = n, \
         n = list_da_to_va(pos->next_offset))

static uint64_t addr_diff = 0;
static uint64_t g_upstream4_buckets_addr = 0;
static uint64_t g_downstream4_buckets_addr = 0;

static struct offset_list_head *list_da_to_va(uint64_t daddr);
static void *entry_da_to_va(uint64_t daddr);
static int32_t offset_nep_map_table_dump_next(u32 *bucket,
    u32 *last, void *key, void *value, u32 buckets_addr, u32 num_buckets);

static struct offset_list_head *list_da_to_va(uint64_t daddr)
{
    return (struct offset_list_head *)(daddr + addr_diff);
}

static void *entry_da_to_va(uint64_t daddr)
{
    return (void *)(daddr + addr_diff);
}

static int32_t offset_nep_map_table_dump_next(u32 *bucket,
    u32 *last, void *key, void *value, u32 buckets_addr, u32 num_buckets)
{
    u32 i;
    struct offset_nep_map_entry *entry;
    struct offset_list_head *head, *pos, *tmp, *buckets;

    while (*bucket < num_buckets) {
        i = 0;
        // Get the array of buckets from buckets_addr
        buckets = (struct offset_list_head *)(buckets_addr + addr_diff);
        // Get the head of list in a bucket
        head = &buckets[*bucket];

        offset_list_for_each_safe(pos, tmp, head) {
            if (i < *last) {
                i++;
                continue;
            }
            *last = i + 1;
            entry = offset_list_entry(pos, struct offset_nep_map_entry, node_addr);
            memcpy_fromio(key, entry_da_to_va(entry->key_addr), sizeof(Tether4Key));
            memcpy_fromio(value, entry_da_to_va(entry->value_addr), sizeof(Tether4Value));

            return 0;
        }
        (*bucket)++;
        *last = 0;
    }

    return 1;
}

int register_tethering_shared_info(uint64_t base_addr, uint64_t diff,
    uint32_t upstream4_buckets_addr_offset, uint32_t downstream4_buckets_addr_offset)
{
    if (diff == 0 || upstream4_buckets_addr_offset == 0 ||
            downstream4_buckets_addr_offset == 0) {
        return -EINVAL;
    }

    g_upstream4_buckets_addr = base_addr + upstream4_buckets_addr_offset;
    g_downstream4_buckets_addr = base_addr + downstream4_buckets_addr_offset;

    addr_diff = diff;

    return 0;
}
EXPORT_SYMBOL_GPL(register_tethering_shared_info);

void entry_update_timeout(uint64_t base_time)
{
    u32 bucket = 0, last = 0;
    Tether4Key key;
    Tether4Value value;

    // Iterate upstream4 map
    while(offset_nep_map_table_dump_next(&bucket, &last, (void *)&key, (void *)&value,
        g_upstream4_buckets_addr, NEP_TETHER_UPSTREAM4_BUCKET_NUM) == 0) {
        // Check last_used, then update to tethering module
        if (base_time < value.last_used) {
            send_callback(CMD_CALLBACK_EXTEND_TIMEOUT, (const void*) &key);
        }
    }
    bucket = 0;
    last = 0;

    // Iterate downstream4 map
    while(offset_nep_map_table_dump_next(&bucket, &last, (void *)&key, (void *)&value,
        g_downstream4_buckets_addr, NEP_TETHER_DOWNSTREAM4_BUCKET_NUM) == 0) {
        // Check last_used, then update to tethering module
        if (base_time < value.last_used) {
            send_callback(CMD_CALLBACK_EXTEND_TIMEOUT, (const void*) &key);
        }
    }
    bucket = 0;
    last = 0;
}
EXPORT_SYMBOL_GPL(entry_update_timeout);
