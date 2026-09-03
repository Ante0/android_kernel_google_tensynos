// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include "nep.h"
#include "nep_map_table.h"
#include "util/memory_pool.h"
#else
#include "linux_port/memory-alloc.h"
#include "linux_port/spinlock.h"
#include "linux_port/types.h"
#include "pw_log/log.h"
#include "net/memory_pool.h"
#include "net/nep_map_table.h"
#endif

#include "jenkins_hash.h"

static u32 nep_map_hash_fn(struct nep_map_table *table, const void *key);
static struct nep_map_entry *nep_map_alloc_entry(struct nep_map_table *table);
static void nep_map_free_entry(struct nep_map_table *table, struct nep_map_entry *entry);
static struct list_head *nep_table_find_bucket(struct nep_map_table *table, u32 hash);
static void *nep_map_table_lookup(struct nep_map_table *table, const void *key);

static uint32_t Max(uint32_t a, uint32_t b) {
  return a > b ? a : b;
}

static uint32_t round_up_pow_of_two(uint32_t n) {
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n++;
  return n;
}

static u32 nep_map_hash_fn(struct nep_map_table *table, const void *key)
{
	/* The key must be an array of u32. It is checked in nep_map_table_init() */
	return jenkins_hash((const u32 *) key, table->key_size / sizeof(u32), table->hash_seed);
}

static void nep_map_log_err_msg(const char *msg)
{
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	dev_err(&sim->dev, msg);
#else
	PW_LOG_ERROR("%s", msg);
#endif
}

static struct nep_map_entry *nep_map_alloc_entry(struct nep_map_table *table)
{
	struct nep_map_entry *entry = NULL;
	int ret;

	ret = memory_pool_allocate(&table->map_entry_pool, (void **)&entry);
	if (ret != NEP_MEMORY_POOL_ERR_NONE) {
		if (ret == NEP_MEMORY_POOL_ERR_MEMORY_CORRUPTION) {
			nep_map_log_err_msg("Memory corruption in table->map_entry_pool.");
		}
		return NULL;
	}
	ret = memory_pool_allocate(&table->key_pool, (void **)&entry->key);
	if (ret != NEP_MEMORY_POOL_ERR_NONE) {
		if (ret == NEP_MEMORY_POOL_ERR_MEMORY_CORRUPTION) {
			nep_map_log_err_msg("Memory corruption in table->key_pool.");
		}
		memory_pool_free(&table->map_entry_pool, entry);
		return NULL;
	}
	ret = memory_pool_allocate(&table->value_pool, (void **)&entry->value);
	if (ret != NEP_MEMORY_POOL_ERR_NONE) {
		if (ret == NEP_MEMORY_POOL_ERR_MEMORY_CORRUPTION) {
			nep_map_log_err_msg("Memory corruption in table->value_pool.");
		}
		memory_pool_free(&table->key_pool, entry->key);
		memory_pool_free(&table->map_entry_pool, entry);
		return NULL;
	}
	return entry;
}

static void nep_map_free_entry(struct nep_map_table *table, struct nep_map_entry *entry)
{
	memory_pool_free(&table->key_pool, entry->key);
	memory_pool_free(&table->value_pool, entry->value);
	memory_pool_free(&table->map_entry_pool, entry);
}

static u32 nep_table_get_bucket_id(struct nep_map_table *table, u32 hash)
{
	return hash & (table->num_buckets - 1);
}

static struct list_head *nep_table_find_bucket(struct nep_map_table *table, u32 hash)
{
	return &table->buckets[hash & (table->num_buckets - 1)];
}

bool nep_map_table_init(unsigned int max_entries, unsigned int key_size,
	unsigned int value_size, struct nep_map_table *table, u8 *p_mem)
{
	unsigned int key_block_size;
	unsigned int value_block_size;
	int i;
	u8 *entry_mem;
	u8 *key_mem;
	u8 *value_mem;

	/* hash table size must be power of 2 */
	int num_buckets = round_up_pow_of_two(max_entries);

#ifdef linux
	entry_mem = p_mem;
	key_mem = p_mem;
#else
	entry_mem = p_mem + NEP_ENTRY_MEM_OFFSET(num_buckets);
	key_mem = p_mem + NEP_KEY_MEM_OFFSET(max_entries, num_buckets);
#endif

	// table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (table == NULL) {
		return false;
	}

	/* The key must be an array of u32 (for hash function) */
	if (key_size % 4 != 0) {
		return false;
	}

	spin_lock_init(&table->lock);

#ifdef linux
	table->buckets = kmalloc_array(num_buckets, sizeof(struct list_head), GFP_KERNEL);
#else
	table->buckets = (struct list_head*)(p_mem + NEP_BUCKETS_MEM_OFFSET);
#endif
	if (table->buckets == NULL) {
		goto fail1;
	}
	for (i = 0; i < num_buckets; i++) {
		INIT_LIST_HEAD(&table->buckets[i]);
	}
	if (memory_pool_init(&table->map_entry_pool, NEP_MAP_ENTRY_BLOCK_SIZE * max_entries,
                NEP_MAP_ENTRY_BLOCK_SIZE, entry_mem) != NEP_MEMORY_POOL_ERR_NONE) {
		goto fail2;
	}
	key_block_size = Max(key_size, (unsigned int)MINIMUM_BLOCK_SIZE);
#ifdef linux
	value_mem = p_mem;
#else
	value_mem = p_mem + NEP_VALUE_MEM_OFFSET(max_entries, num_buckets, key_block_size);
#endif
	if (memory_pool_init(&table->key_pool, key_block_size * max_entries,
			     key_block_size, key_mem) != NEP_MEMORY_POOL_ERR_NONE) {
		goto fail3;
	}
	value_block_size = Max(value_size, (unsigned int)MINIMUM_BLOCK_SIZE);
	if (memory_pool_init(&table->value_pool, value_block_size * max_entries,
			     value_block_size, value_mem) != NEP_MEMORY_POOL_ERR_NONE) {
		goto fail4;
	}

	table->num_buckets = num_buckets;
	table->max_entries = max_entries;
	table->count = 0;
	table->key_size = key_size;
	table->value_size = value_size;
#ifdef linux
	get_random_bytes(&table->hash_seed, sizeof(u32));
#else
	table->hash_seed = 0;
#endif
	return true;

fail4:
	memory_pool_destroy(&table->key_pool);
fail3:
	memory_pool_destroy(&table->map_entry_pool);
fail2:
#ifdef linux
	kfree(table->buckets);
#endif
fail1:
#ifdef linux
	kfree(table);
#endif
	return false;
}

void nep_map_table_destroy(struct nep_map_table *table)
{
	spin_lock(&table->lock);
	memory_pool_destroy(&table->map_entry_pool);
	memory_pool_destroy(&table->key_pool);
	memory_pool_destroy(&table->value_pool);
	spin_unlock(&table->lock);

#ifdef linux
	kfree(table->buckets);
	kfree(table);
#endif
}

static void *nep_map_table_lookup(struct nep_map_table *table, const void *key)
{
	u32 hash;
	struct nep_map_entry *entry;
	struct list_head *pos, *tmp;

	hash = nep_map_hash_fn(table, key);
	list_for_each_safe(pos, tmp, nep_table_find_bucket(table, hash)) {
    	entry = list_entry(pos, struct nep_map_entry, node);
		if (entry->hash == hash && memcmp(entry->key, key, table->key_size) == 0) {
			return entry->value;
		}
	}
	return NULL;
}

int32_t nep_map_table_get(struct nep_map_table *table, const void *key, void *value)
{
	void *tmp;

	spin_lock(&table->lock);
	tmp = nep_map_table_lookup(table, key);
	if (tmp != NULL) {
		memcpy(value, tmp, table->value_size);
	}
	spin_unlock(&table->lock);

	return tmp == NULL? NEP_MAP_TABLE_ERR_NO_ENTRY : NEP_MAP_TABLE_ERR_NONE;
}

void *nep_map_table_get_ptr(struct nep_map_table *table, const void *key)
{
	return nep_map_table_lookup(table, key);
}

int32_t nep_map_table_add(struct nep_map_table *table, const void *key, const void *value)
{
	u32 hash;
	struct nep_map_entry *entry;
	void *tmp;

	spin_lock(&table->lock);
	tmp = nep_map_table_lookup(table, key);
	if (tmp != NULL) {
		memcpy(tmp, value, table->value_size);
		spin_unlock(&table->lock);
		return NEP_MAP_TABLE_ERR_NONE;
	}
	if (table->count >= table->max_entries) {
		spin_unlock(&table->lock);
		return NEP_MAP_TABLE_ERR_NO_RESOURCE;;
	}

	entry = nep_map_alloc_entry(table);
	if (entry == NULL) {
		spin_unlock(&table->lock);
		return NEP_MAP_TABLE_ERR_NO_RESOURCE;
	}

	hash = nep_map_hash_fn(table, key);
	memcpy(entry->key, key, table->key_size);
	memcpy(entry->value, value, table->value_size);
	entry->hash = hash;

	list_add(&entry->node, nep_table_find_bucket(table, hash));
	table->count++;
	spin_unlock(&table->lock);

	return NEP_MAP_TABLE_ERR_NONE;
}

int32_t nep_map_table_remove(struct nep_map_table *table, const void *key)
{
	u32 hash;
	struct nep_map_entry *entry;
	struct list_head *pos, *tmp;

	spin_lock(&table->lock);
	hash = nep_map_hash_fn(table, key);
	list_for_each_safe(pos, tmp, nep_table_find_bucket(table, hash)) {
    	entry = list_entry(pos, struct nep_map_entry, node);
		if (entry->hash == hash && memcmp(entry->key, key, table->key_size) == 0) {
			list_del(&entry->node);
			nep_map_free_entry(table, entry);
			table->count--;
			spin_unlock(&table->lock);
			return NEP_MAP_TABLE_ERR_NONE;
		}
	}
	spin_unlock(&table->lock);

	return NEP_MAP_TABLE_ERR_NONE;
}

void nep_map_table_erase(struct nep_map_table *table)
{
	struct nep_map_entry *entry;
	struct list_head *pos, *tmp;
	uint32_t i;

	spin_lock(&table->lock);
	for (i = 0; i < table->num_buckets; i++) {
		list_for_each_safe(pos, tmp, &table->buckets[i]) {
      		entry = list_entry(pos, struct nep_map_entry, node);
			list_del(&entry->node);
			nep_map_free_entry(table, entry);
		}
	}
	table->count = 0;
	spin_unlock(&table->lock);
}

int32_t nep_map_table_get_next(struct nep_map_table *table, void *key, void *value)
{
	u32 hash;
	u32 bucket_id = 0;
	struct nep_map_entry *entry;
	struct list_head *head, *pos, *tmp;
	int get_first = 1;
	int found = 1;
	uint32_t i;

	/* all zero in key means get first element */
	for (i = 0; i < table->key_size; ++i) {
		if (((u8 *)key)[i] != 0) {
			get_first = 0;
			break;
		}
	}
	spin_lock(&table->lock);
	hash = nep_map_hash_fn(table, key);
	if (!get_first) {
		bucket_id = nep_table_get_bucket_id(table, hash);
		found = 0;
	}
	while (bucket_id < table->num_buckets) {
		head = &table->buckets[bucket_id];
		list_for_each_safe(pos, tmp, head) {
      		entry = list_entry(pos, struct nep_map_entry, node);
			if (found) {
				memcpy(key, entry->key, table->key_size);
				memcpy(value, entry->value, table->value_size);
				spin_unlock(&table->lock);
				return 0;
			}
			if (entry->hash == hash && memcmp(entry->key, key, table->key_size) == 0) {
				found = 1;
			}
		}
		if (!found) {
			break;
		}
		bucket_id++;
	}
	spin_unlock(&table->lock);
	return NEP_MAP_TABLE_ERR_NO_ENTRY;
}

int32_t nep_map_table_dump_next(struct nep_map_table *table, u32 *bucket, u32 *last, void *key,
			    void *value)
{
	struct nep_map_entry *entry;
	struct list_head *head, *pos, *tmp;
	u32 i;

	spin_lock(&table->lock);
	while (*bucket < table->num_buckets) {
		i = 0;
		head = &table->buckets[*bucket];
		list_for_each_safe(pos, tmp, head) {
			if (i < *last) {
				i++;
				continue;
			}
			*last = i + 1;
			entry = list_entry(pos, struct nep_map_entry, node);
			memcpy(key, entry->key, table->key_size);
			memcpy(value, entry->value, table->value_size);
			spin_unlock(&table->lock);
			return NEP_MAP_TABLE_ERR_NONE;
		}
		(*bucket)++;
		*last = 0;
	}

	spin_unlock(&table->lock);
	return NEP_MAP_TABLE_ERR_NO_ENTRY;
}
