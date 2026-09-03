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
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include "nep_tables.h"
#include "nep_map_table.h"
#include "nep_service.h"
#else /* linux */
#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "linux_port/types.h"
#include "pw_log/log.h"
#include "net/nep_map_table.h"
#include "net/nep_tables.h"
#include "net/nep_service.h"
#endif /* linux */

#ifdef linux
#define LOG_ERROR(msg)               \
  do {                               \
    dev_err(&sim->dev, "%s", #msg);  \
  } while (0)

#else /* linux */
#ifdef NEP_TABLE_TEST
#undef SEC_FAST_DATA
#define SEC_FAST_DATA
#endif /* NEP_TABLE_TEST */

#define LOG_ERROR(msg)         \
  do {                         \
    PW_LOG_ERROR("%s", #msg);  \
  } while (0)

SEC_FAST_DATA static uint8_t nep_tether_dscp_to_up_table[NEP_DSCP_TO_UP_TABLE_SIZE];

// Allocate a big block of data for session tables at once.
SEC_FAST_DATA static struct nep_tables g_nep_tables;

#endif /* linux */

static struct nep_map_table *get_upstream4_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_tether_upstream4_table;
#else
	return &g_nep_tables.nep_tether_upstream4_table;
#endif
}

static struct nep_map_table *get_downstream4_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_tether_downstream4_table;
#else
	return &g_nep_tables.nep_tether_downstream4_table;
#endif
}

static struct nep_map_table *get_upstream6_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_tether_upstream6_table;
#else
	return &g_nep_tables.nep_tether_upstream6_table;
#endif
}

static struct nep_map_table *get_downstream6_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_tether_downstream6_table;
#else
	return &g_nep_tables.nep_tether_downstream6_table;
#endif
}

static struct nep_map_table *get_ingress6_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_clat_ingress6_table;
#else
	return &g_nep_tables.nep_clat_ingress6_table;
#endif
}

static struct nep_map_table *get_egress4_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_clat_egress4_table;
#else
	return &g_nep_tables.nep_clat_egress4_table;
#endif
}

static struct nep_map_table *get_flowid_table(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return &sim->g_nep_tables.nep_tether_flowid_table;
#else
	return &g_nep_tables.nep_tether_flowid_table;
#endif
}

static u8 *get_dscp_to_up_table(void)
{
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->nep_tether_dscp_to_up_table;
#else
	return nep_tether_dscp_to_up_table;
#endif
}

static u8* get_upstream4_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_upstream4_buffer;
#else
	return g_nep_tables.nep_upstream4_buffer;
#endif
}

static u8* get_downstream4_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_downstream4_buffer;
#else
	return g_nep_tables.nep_downstream4_buffer;
#endif
}

static u8* get_upstream6_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_upstream6_buffer;
#else
	return g_nep_tables.nep_upstream6_buffer;
#endif
}

static u8* get_downstream6_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_downstream6_buffer;
#else
	return g_nep_tables.nep_downstream6_buffer;
#endif
}

static u8* get_ingress6_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_ingress6_buffer;
#else
	return g_nep_tables.nep_ingress6_buffer;
#endif
}

static u8* get_egress4_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_egress4_buffer;
#else
	return g_nep_tables.nep_egress4_buffer;
#endif
}

static u8* get_flowid_buffer(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	return sim->g_nep_tables.nep_flowid_buffer;
#else
	return g_nep_tables.nep_flowid_buffer;
#endif
}

static int nep_map_table_get_v4(struct nep_map_table *table, const Tether4Key *key, Tether4Value *value, uint64_t last_used)
{
	Tether4Value *tmp;

	spin_lock(&table->lock);
	tmp = (Tether4Value *)nep_map_table_get_ptr(table, key);
	if (tmp != NULL) {
		tmp->last_used = last_used;
		memcpy(value, tmp, table->value_size);
	}
	spin_unlock(&table->lock);

	return tmp == NULL? NEP_MAP_TABLE_ERR_NO_ENTRY : NEP_MAP_TABLE_ERR_NONE;
}

void nep_tables_init(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
#endif /* linux */

	if (!nep_map_table_init(NEP_TETHER_UPSTREAM4_MAX_ENTRY, sizeof(Tether4Key),
		sizeof(Tether4Value), get_upstream4_table(), get_upstream4_buffer())) {
		LOG_ERROR("Failed to init nep_tether_upstream4_table.");
	}

	if (!nep_map_table_init(NEP_TETHER_DOWNSTREAM4_MAX_ENTRY, sizeof(Tether4Key),
		sizeof(Tether4Value), get_downstream4_table(), get_downstream4_buffer())) {
		LOG_ERROR("Failed to init nep_tether_downstream4_table.");
	}

	if (!nep_map_table_init(NEP_TETHER_UPSTREAM6_MAX_ENTRY, sizeof(TetherUpstream6Key),
		sizeof(Tether6Value), get_upstream6_table(), get_upstream6_buffer())) {
		LOG_ERROR("Failed to init nep_tether_upstream6_table.");
	}

	if (!nep_map_table_init(NEP_TETHER_DOWNSTREAM6_MAX_ENTRY, sizeof(TetherDownstream6Key),
		sizeof(Tether6Value), get_downstream6_table(), get_downstream6_buffer())) {
		LOG_ERROR("Failed to init nep_tether_downstream6_table.");
	}

	if (!nep_map_table_init(NEP_CLAT_INGRESS6_MAX_ENTRY, sizeof(ClatIngress6Key),
		sizeof(ClatIngress6Value), get_ingress6_table(), get_ingress6_buffer())) {
		LOG_ERROR("Failed to init nep_clat_ingress6_table.");
	}

	if (!nep_map_table_init(NEP_CLAT_EGRESS4_MAX_ENTRY, sizeof(ClatEgress4Key),
		sizeof(ClatEgress4Value), get_egress4_table(), get_egress4_buffer())) {
		LOG_ERROR("Failed to init nep_clat_egress4_table.");
	}

	if (!nep_map_table_init(NEP_TETHER_FLOWID_MAX_ENTRY, sizeof(TetherFlowIdKey),
		sizeof(TetherFlowIdValue), get_flowid_table(), get_flowid_buffer())) {
		LOG_ERROR("Failed to init nep_tether_flowid_table.");
	}
}

void nep_tables_destroy(void) {
	nep_map_table_destroy(get_upstream4_table());
	nep_map_table_destroy(get_downstream4_table());
	nep_map_table_destroy(get_upstream6_table());
	nep_map_table_destroy(get_downstream6_table());
	nep_map_table_destroy(get_ingress6_table());
	nep_map_table_destroy(get_egress4_table());
	nep_map_table_destroy(get_flowid_table());
}

void nep_tables_erase(void) {
	nep_map_table_erase(get_upstream4_table());
	nep_map_table_erase(get_downstream4_table());
	nep_map_table_erase(get_upstream6_table());
	nep_map_table_erase(get_downstream6_table());
	nep_map_table_erase(get_ingress6_table());
	nep_map_table_erase(get_egress4_table());
	nep_map_table_erase(get_flowid_table());
}

// Lookup functions
int32_t nep_tables_downstream4_map_lookup(const Tether4Key *key,
	Tether4Value *value, uint64_t last_used)
{
	return nep_map_table_get_v4(
			get_downstream4_table(), key, value, last_used);
}

int32_t nep_tables_upstream4_map_lookup(const Tether4Key *key,
	Tether4Value *value, uint64_t last_used)
{
	return nep_map_table_get_v4(
			get_upstream4_table(), key, value, last_used);
}

int32_t nep_tables_downstream6_map_lookup(const TetherDownstream6Key *key,
	Tether6Value *value)
{
	return nep_map_table_get(
			get_downstream6_table(), (void *)key, (void *)value);
}

int32_t nep_tables_upstream6_map_lookup(const TetherUpstream6Key *key,
	Tether6Value *value)
{
	return nep_map_table_get(
			get_upstream6_table(), (void *)key, (void *)value);
}

int32_t nep_tables_ingress6_map_lookup(const ClatIngress6Key* key,
	ClatIngress6Value *value)
{
	return nep_map_table_get(get_ingress6_table(),
			(void *)key, (void *)value);
}

int32_t nep_tables_egress4_map_lookup(const ClatEgress4Key* key,
	ClatEgress4Value *value)
{
	return nep_map_table_get(get_egress4_table(),
			(void *)key, (void *)value);
}

int32_t nep_tables_flowid_map_lookup(const TetherFlowIdKey *key,
	TetherFlowIdValue *value)
{
	return nep_map_table_get(get_flowid_table(),
			(void *)key, (void *)value);
}

// Add functions
int nep_tables_downstream4_map_add(const Tether4Key *key,
	const Tether4Value *value)
{
	return nep_map_table_add(get_downstream4_table(), (void *)key, (void *)value);
}

int nep_tables_upstream4_map_add(const Tether4Key *key, const Tether4Value *value)
{
	return nep_map_table_add(get_upstream4_table(), (void *)key, (void *)value);
}

int nep_tables_downstream6_map_add(const TetherDownstream6Key *key,
	const Tether6Value *value)
{
	return nep_map_table_add(get_downstream6_table(), (void *)key, (void *)value);
}

int nep_tables_upstream6_map_add(const TetherUpstream6Key *key,
	const Tether6Value *value)
{
	return nep_map_table_add(get_upstream6_table(), (void *)key, (void *)value);
}

int32_t nep_tables_ingress6_map_add(const ClatIngress6Key* key,
				    const ClatIngress6Value* value)
{
	return nep_map_table_add(get_ingress6_table(), (void *)key, (void *)value);
}

int32_t nep_tables_egress4_map_add(const ClatEgress4Key* key,
				   const ClatEgress4Value* value)
{
	return nep_map_table_add(get_egress4_table(), (void *)key, (void *)value);
}

int nep_tables_flowid_map_add(const TetherFlowIdKey *key,
	const TetherFlowIdValue *value)
{
	return nep_map_table_add(get_flowid_table(), (void *)key, (void *)value);
}

int nep_tables_dscp2up_map_add(const uint8_t *tbl_ptr)
{
	u8 *dscp_to_up_table = get_dscp_to_up_table();

	if (tbl_ptr == NULL) {
		return -1;
	}

	memcpy(dscp_to_up_table, tbl_ptr, sizeof(uint8_t) * NEP_DSCP_TO_UP_TABLE_SIZE);
	return 0;
}

// Remove functions
int nep_tables_downstream4_map_remove(const Tether4Key *key)
{
	return nep_map_table_remove(get_downstream4_table(), (void *)key);
}

int nep_tables_upstream4_map_remove(const Tether4Key *key)
{
	return nep_map_table_remove(get_upstream4_table(), (void *)key);
}

int nep_tables_downstream6_map_remove(const TetherDownstream6Key *key)
{
	return nep_map_table_remove(get_downstream6_table(), (void *)key);
}

int nep_tables_upstream6_map_remove(const TetherUpstream6Key *key)
{
	return nep_map_table_remove(get_upstream6_table(), (void *)key);
}

int32_t nep_tables_ingress6_map_remove(const ClatIngress6Key* key)
{
	return nep_map_table_remove(get_ingress6_table(), (void *)key);
}

int32_t nep_tables_egress4_map_remove(const ClatEgress4Key* key)
{
	return nep_map_table_remove(get_egress4_table(), (void *)key);
}

int nep_tables_flowid_map_remove(const TetherFlowIdKey *key)
{
	return nep_map_table_remove(get_flowid_table(), (void *)key);
}

// Get next functions
int nep_tables_downstream4_map_get_next(Tether4Key *key,
					Tether4Value *value)
{
	return nep_map_table_get_next(get_downstream4_table(), (void *)key,
				      (void *)value);
}

int nep_tables_upstream4_map_get_next(Tether4Key *key,
				      Tether4Value *value)
{
	return nep_map_table_get_next(get_upstream4_table(), (void *)key, (void *)value);
}

int nep_tables_downstream6_map_get_next(TetherDownstream6Key *key,
					Tether6Value *value)
{
	return nep_map_table_get_next(get_downstream6_table(), (void *)key,
				      (void *)value);
}

int nep_tables_upstream6_map_get_next(TetherUpstream6Key *key,
				      Tether6Value *value)
{
	return nep_map_table_get_next(get_upstream6_table(), (void *)key, (void *)value);
}

int32_t nep_tables_ingress6_map_get_next(ClatIngress6Key* key,
					 ClatIngress6Value* value)
{
	return nep_map_table_get_next(get_ingress6_table(), (void *)key, (void *)value);
}

int32_t nep_tables_egress4_map_get_next(ClatEgress4Key* key,
					ClatEgress4Value* value)
{
	return nep_map_table_get_next(get_egress4_table(), (void *)key, (void *)value);
}

int nep_tables_flowid_map_get_next(TetherFlowIdKey *key,
				   TetherFlowIdValue *value)
{
	return nep_map_table_get_next(get_flowid_table(), (void *)key, (void *)value);
}

// Dump table functions
int nep_tables_downstream4_map_dump_next(u32 *bucket, u32 *last,
				       Tether4Key *key, Tether4Value *value)
{
	return nep_map_table_dump_next(get_downstream4_table(), bucket, last, (void *)key,
				       (void *)value);
}

int nep_tables_upstream4_map_dump_next(u32 *bucket, u32 *last,
				       Tether4Key *key, Tether4Value *value)
{
	return nep_map_table_dump_next(get_upstream4_table(), bucket, last, (void *)key,
				       (void *)value);
}

int nep_tables_downstream6_map_dump_next(u32 *bucket, u32 *last,
					 TetherDownstream6Key *key, Tether6Value *value)
{
	return nep_map_table_dump_next(get_downstream6_table(), bucket, last, (void *)key,
				       (void *)value);
}

int nep_tables_upstream6_map_dump_next(u32 *bucket, u32 *last,
				       TetherUpstream6Key *key, Tether6Value *value)
{
	return nep_map_table_dump_next(get_upstream6_table(), bucket, last, (void *)key,
				       (void *)value);
}

int32_t nep_tables_ingress6_map_dump_next(u32 *bucket, u32 *last,
					  ClatIngress6Key* key, ClatIngress6Value* value)
{
	return nep_map_table_dump_next(get_ingress6_table(), bucket, last, (void *)key,
				       (void *)value);
}

int32_t nep_tables_egress4_map_dump_next(u32 *bucket, u32 *last,
					 ClatEgress4Key* key, ClatEgress4Value* value)
{
	return nep_map_table_dump_next(get_egress4_table(), bucket, last, (void *)key,
				       (void *)value);
}


int nep_tables_flowid_map_dump_next(u32 *bucket, u32 *last,
				    TetherFlowIdKey *key, TetherFlowIdValue *value)
{
	return nep_map_table_dump_next(get_flowid_table(), bucket, last, (void *)key,
				       (void *)value);
}

void get_nep_table_shared_info(struct shared_address_info *shared_info) {
#ifndef linux
	uint32_t nep_table_addr = (uint32_t) &g_nep_tables;
	uint32_t upstream4_buckets_addr_offset = (uintptr_t) get_upstream4_table()->buckets;
	uint32_t downstream4_buckets_addr_offset = (uintptr_t) get_downstream4_table()->buckets;
	uint32_t memory_size = NEP_TABLE_UPSTREAM4_TOTAL_MEM_SIZE +
			NEP_TABLE_DOWNSTREAM4_TOTAL_MEM_SIZE + 2 * sizeof(nep_map_table);

	memcpy(&shared_info->base_addr, &nep_table_addr, sizeof(uint32_t));
	memcpy(&shared_info->upstream4_buckets_addr_offset,
		&upstream4_buckets_addr_offset, sizeof(uint32_t));
	memcpy(&shared_info->downstream4_buckets_addr_offset,
		&downstream4_buckets_addr_offset, sizeof(uint32_t));
	memcpy(&shared_info->memory_size, &memory_size, sizeof(uint32_t));
#endif
}

#ifdef linux
EXPORT_SYMBOL_GPL(nep_tables_init);
EXPORT_SYMBOL_GPL(nep_tables_destroy);
EXPORT_SYMBOL_GPL(nep_tables_erase);
EXPORT_SYMBOL_GPL(nep_tables_downstream4_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_upstream4_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_downstream6_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_upstream6_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_ingress6_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_egress4_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_flowid_map_lookup);
EXPORT_SYMBOL_GPL(nep_tables_downstream4_map_add);
EXPORT_SYMBOL_GPL(nep_tables_upstream4_map_add);
EXPORT_SYMBOL_GPL(nep_tables_downstream6_map_add);
EXPORT_SYMBOL_GPL(nep_tables_upstream6_map_add);
EXPORT_SYMBOL_GPL(nep_tables_ingress6_map_add);
EXPORT_SYMBOL_GPL(nep_tables_egress4_map_add);
EXPORT_SYMBOL_GPL(nep_tables_flowid_map_add);
EXPORT_SYMBOL_GPL(nep_tables_downstream4_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_upstream4_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_downstream6_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_upstream6_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_ingress6_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_egress4_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_flowid_map_remove);
EXPORT_SYMBOL_GPL(nep_tables_downstream4_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_upstream4_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_downstream6_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_upstream6_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_ingress6_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_egress4_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_flowid_map_get_next);
EXPORT_SYMBOL_GPL(nep_tables_downstream4_map_dump_next);
EXPORT_SYMBOL_GPL(nep_tables_upstream4_map_dump_next);
EXPORT_SYMBOL_GPL(nep_tables_downstream6_map_dump_next);
EXPORT_SYMBOL_GPL(nep_tables_upstream6_map_dump_next);
EXPORT_SYMBOL_GPL(nep_tables_ingress6_map_dump_next);
EXPORT_SYMBOL_GPL(nep_tables_egress4_map_dump_next);
EXPORT_SYMBOL_GPL(nep_tables_flowid_map_dump_next);
#endif
