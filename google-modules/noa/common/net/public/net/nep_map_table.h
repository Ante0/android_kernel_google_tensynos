/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef NEP_MAP_TABLE_H
#define NEP_MAP_TABLE_H

#ifdef linux
#include <linux/types.h>
#include <common/core.h>
#include <common/map_def.h>
#include "util/memory_pool.h"
#else
#include "linux_port/list.h"
#include "linux_port/spinlock.h"
#include "linux_port/types.h"
#include "memory_pool.h"
#include "map_def.h"
#endif

#define NEP_DSCP_TO_UP_TABLE_SIZE 64
#define NEP_MAP_ENTRY_SIZE sizeof(struct nep_map_entry)

#define NEP_MAP_ENTRY_BLOCK_SIZE                                \
  (NEP_MAP_ENTRY_SIZE > MINIMUM_BLOCK_SIZE ? NEP_MAP_ENTRY_SIZE \
                                           : MINIMUM_BLOCK_SIZE)

// Entry size should be at least MINIMUM_BLOCK_SIZE for memory_pool usage
#define ADJUST_SIZE(a) (a > MINIMUM_BLOCK_SIZE ? a : MINIMUM_BLOCK_SIZE)

#define NEP_BUCKETS_MEM_SIZE(num) (num * sizeof(struct list_head))
#define NEP_ENTRY_MEM_SIZE(num) (num * NEP_MAP_ENTRY_BLOCK_SIZE)
#define NEP_KEY_MEM_SIZE(num, key_size) (num * key_size)
#define NEP_VALUE_MEM_SIZE(num, value_size) (num * value_size)

#define NEP_TABLE_TOTAL_MEM_SIZE(entry_num, bucket_num, key_size, value_size) \
  (NEP_BUCKETS_MEM_SIZE(bucket_num) + NEP_ENTRY_MEM_SIZE(entry_num) +    \
   NEP_KEY_MEM_SIZE(entry_num, key_size) + NEP_VALUE_MEM_SIZE(entry_num, value_size))

#define NEP_BUCKETS_MEM_OFFSET 0
#define NEP_ENTRY_MEM_OFFSET(bucket_num) \
  (NEP_BUCKETS_MEM_OFFSET + NEP_BUCKETS_MEM_SIZE(bucket_num))
#define NEP_KEY_MEM_OFFSET(entry_num, bucket_num) \
  (NEP_ENTRY_MEM_OFFSET(bucket_num) + NEP_ENTRY_MEM_SIZE(entry_num))
#define NEP_VALUE_MEM_OFFSET(entry_num, bucket_num, key_size) \
  (NEP_KEY_MEM_OFFSET(entry_num, bucket_num) + NEP_KEY_MEM_SIZE(entry_num, key_size))

// Bucket number is rounded up to the power of 2.
#ifdef NOA_IS_CI_TESTING_FIRMWARE
// Bucket number is rounded up to the power of 2.
#define NEP_TETHER_DOWNSTREAM4_MAX_ENTRY 32
#define NEP_TETHER_DOWNSTREAM4_BUCKET_NUM 32
#define NEP_TETHER_UPSTREAM4_MAX_ENTRY 32
#define NEP_TETHER_UPSTREAM4_BUCKET_NUM 32
#else /* NOA_IS_CI_TESTING_FIRMWARE */
// Bucket number is rounded up to the power of 2.
#define NEP_TETHER_DOWNSTREAM4_MAX_ENTRY  512
#define NEP_TETHER_DOWNSTREAM4_BUCKET_NUM 512
#define NEP_TETHER_UPSTREAM4_MAX_ENTRY   512
#define NEP_TETHER_UPSTREAM4_BUCKET_NUM  512
#endif /* NOA_IS_CI_TESTING_FIRMWARE */
#define NEP_TETHER_DOWNSTREAM6_MAX_ENTRY  64
#define NEP_TETHER_DOWNSTREAM6_BUCKET_NUM 64
#define NEP_TETHER_UPSTREAM6_MAX_ENTRY   8
#define NEP_TETHER_UPSTREAM6_BUCKET_NUM  8
#define NEP_CLAT_INGRESS6_MAX_ENTRY      3
#define NEP_CLAT_INGRESS6_BUCKET_NUM     4
#define NEP_CLAT_EGRESS4_MAX_ENTRY       3
#define NEP_CLAT_EGRESS4_BUCKET_NUM      4
#define NEP_TETHER_FLOWID_MAX_ENTRY      16
#define NEP_TETHER_FLOWID_BUCKET_NUM     16

#define NEP_TABLE_UPSTREAM4_TOTAL_MEM_SIZE				\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_TETHER_UPSTREAM4_MAX_ENTRY,	\
	NEP_TETHER_UPSTREAM4_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(Tether4Key)), ADJUST_SIZE(sizeof(Tether4Value))))
#define NEP_TABLE_DOWNSTREAM4_TOTAL_MEM_SIZE				\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_TETHER_DOWNSTREAM4_MAX_ENTRY,	\
	NEP_TETHER_DOWNSTREAM4_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(Tether4Key)), ADJUST_SIZE(sizeof(Tether4Value))))
#define NEP_TABLE_UPSTREAM6_TOTAL_MEM_SIZE				\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_TETHER_UPSTREAM6_MAX_ENTRY, 	\
	NEP_TETHER_UPSTREAM6_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(TetherUpstream6Key)), ADJUST_SIZE(sizeof(Tether6Value))))
#define NEP_TABLE_DOWNSTREAM6_TOTAL_MEM_SIZE				\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_TETHER_DOWNSTREAM6_MAX_ENTRY,	\
	NEP_TETHER_DOWNSTREAM6_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(TetherDownstream6Key)), ADJUST_SIZE(sizeof(Tether6Value))))
#define NEP_TABLE_INGRESS6_TOTAL_MEM_SIZE			\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_CLAT_INGRESS6_MAX_ENTRY,	\
	NEP_CLAT_INGRESS6_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(ClatIngress6Key)), ADJUST_SIZE(sizeof(ClatIngress6Value))))
#define NEP_TABLE_EGRESS4_TOTAL_MEM_SIZE			\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_CLAT_EGRESS4_MAX_ENTRY,	\
	NEP_CLAT_EGRESS4_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(ClatEgress4Key)), ADJUST_SIZE(sizeof(ClatEgress4Value))))
#define NEP_TABLE_FLOWID_TOTAL_MEM_SIZE				\
	(NEP_TABLE_TOTAL_MEM_SIZE(NEP_TETHER_FLOWID_MAX_ENTRY,	\
	NEP_TETHER_FLOWID_BUCKET_NUM,				\
	ADJUST_SIZE(sizeof(TetherFlowIdKey)), ADJUST_SIZE(sizeof(TetherFlowIdValue))))

#define ALL_NEP_TABLE_TOTAL_MEM_SIZE sizeof(struct nep_tables)

enum {
  NEP_MAP_TABLE_ERR_NONE = 0,
  NEP_MAP_TABLE_ERR_NO_ENTRY,
  NEP_MAP_TABLE_ERR_NO_RESOURCE,
  NEP_MAP_TABLE_ERR_MAX
};

struct nep_map_entry {
	void *key;
	void *value;
	struct list_head node;
	u32 hash;
};

struct nep_map_table {
	spinlock_t lock;
	struct list_head *buckets;
	unsigned int num_buckets;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int count;
	u32 hash_seed;
	struct memory_pool map_entry_pool;
	struct memory_pool key_pool;
	struct memory_pool value_pool;
};

struct nep_tables {
	struct nep_map_table nep_tether_upstream4_table;
	uint8_t nep_upstream4_buffer[NEP_TABLE_UPSTREAM4_TOTAL_MEM_SIZE];
	struct nep_map_table nep_tether_downstream4_table;
	uint8_t nep_downstream4_buffer[NEP_TABLE_DOWNSTREAM4_TOTAL_MEM_SIZE];
	struct nep_map_table nep_tether_upstream6_table;
	uint8_t nep_upstream6_buffer[NEP_TABLE_UPSTREAM6_TOTAL_MEM_SIZE];
	struct nep_map_table nep_tether_downstream6_table;
	uint8_t nep_downstream6_buffer[NEP_TABLE_DOWNSTREAM6_TOTAL_MEM_SIZE];
	struct nep_map_table nep_clat_ingress6_table;
	uint8_t nep_ingress6_buffer[NEP_TABLE_INGRESS6_TOTAL_MEM_SIZE];
	struct nep_map_table nep_clat_egress4_table;
	uint8_t nep_egress4_buffer[NEP_TABLE_EGRESS4_TOTAL_MEM_SIZE];
	struct nep_map_table nep_tether_flowid_table;
	uint8_t nep_flowid_buffer[NEP_TABLE_FLOWID_TOTAL_MEM_SIZE];
};

// A struct for shared information needed between NOA and APC
struct shared_address_info {
	uint32_t base_addr;
	uint32_t upstream4_buckets_addr_offset;
	uint32_t downstream4_buckets_addr_offset;
	uint32_t memory_size;
};

bool nep_map_table_init(unsigned int max_entries, unsigned int key_size,
	unsigned int value_size, struct nep_map_table *table, u8 *p_mem);
void nep_map_table_destroy(struct nep_map_table *map_table);
int32_t nep_map_table_get(struct nep_map_table *map_table, const void *key, void *value);
void *nep_map_table_get_ptr(struct nep_map_table *table, const void *key);
int32_t nep_map_table_add(struct nep_map_table *map_table, const void *key, const void *value);
int32_t nep_map_table_remove(struct nep_map_table *map_table, const void *key);
void nep_map_table_erase(struct nep_map_table *map_table);
int32_t nep_map_table_get_next(struct nep_map_table *table, void *key, void *value);
int32_t nep_map_table_dump_next(struct nep_map_table *table, u32 *bucket, u32 *last, void *key,
			    void *value);

#endif // NEP_MAP_TABLE_H
