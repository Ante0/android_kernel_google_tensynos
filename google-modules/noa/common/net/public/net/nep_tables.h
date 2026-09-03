/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef NEP_TABLES_H
#define NEP_TABLES_H

#ifdef linux
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <common/map_def.h>
#include "nep.h"
#else
#include "linux_port/types.h"
#include "net/map_def.h"
#endif

#include "nep_map_table.h"

void get_nep_table_shared_info(struct shared_address_info *shared_info);
void nep_tables_init(void);
void nep_tables_destroy(void);
void nep_tables_erase(void);

int32_t nep_tables_downstream4_map_lookup(const Tether4Key *key,
	Tether4Value *value, uint64_t last_used);
int32_t nep_tables_upstream4_map_lookup(const Tether4Key* key,
	Tether4Value *value, uint64_t last_used);
int32_t nep_tables_downstream6_map_lookup(
    const TetherDownstream6Key* key, Tether6Value *value);
int32_t nep_tables_upstream6_map_lookup(const TetherUpstream6Key* key,
	Tether6Value *value);
int32_t nep_tables_ingress6_map_lookup(const ClatIngress6Key* key,
	ClatIngress6Value *value);
int32_t nep_tables_egress4_map_lookup(const ClatEgress4Key* key,
	ClatEgress4Value *value);
int32_t nep_tables_flowid_map_lookup(const TetherFlowIdKey* key,
	TetherFlowIdValue *value);

int32_t nep_tables_downstream4_map_add(const Tether4Key* key,
                                       const Tether4Value* value);
int32_t nep_tables_upstream4_map_add(const Tether4Key* key,
                                     const Tether4Value* value);
int32_t nep_tables_downstream6_map_add(const TetherDownstream6Key* key,
                                       const Tether6Value* value);
int32_t nep_tables_upstream6_map_add(const TetherUpstream6Key* key,
                                     const Tether6Value* value);
int32_t nep_tables_ingress6_map_add(const ClatIngress6Key* key,
                                    const ClatIngress6Value* value);
int32_t nep_tables_egress4_map_add(const ClatEgress4Key* key,
                                   const ClatEgress4Value* value);
int32_t nep_tables_flowid_map_add(const TetherFlowIdKey* key,
                                  const TetherFlowIdValue* value);
int32_t nep_tables_dscp2up_map_add(const uint8_t *dscp_to_up_table);

int32_t nep_tables_downstream4_map_remove(const Tether4Key* key);
int32_t nep_tables_upstream4_map_remove(const Tether4Key* key);
int32_t nep_tables_downstream6_map_remove(const TetherDownstream6Key* key);
int32_t nep_tables_upstream6_map_remove(const TetherUpstream6Key* key);
int32_t nep_tables_ingress6_map_remove(const ClatIngress6Key* key);
int32_t nep_tables_egress4_map_remove(const ClatEgress4Key* key);
int32_t nep_tables_flowid_map_remove(const TetherFlowIdKey* key);

int32_t nep_tables_downstream4_map_get_next(Tether4Key *key,
					Tether4Value *value);
int32_t nep_tables_upstream4_map_get_next(Tether4Key *key,
				      Tether4Value *value);
int32_t nep_tables_downstream6_map_get_next(TetherDownstream6Key *key,
					Tether6Value *value);
int32_t nep_tables_upstream6_map_get_next(TetherUpstream6Key *key,
				      Tether6Value *value);
int32_t nep_tables_ingress6_map_get_next(ClatIngress6Key* key,
					 ClatIngress6Value* value);
int32_t nep_tables_egress4_map_get_next(ClatEgress4Key* key,
					ClatEgress4Value* value);
int32_t nep_tables_flowid_map_get_next(TetherFlowIdKey *key,
				       TetherFlowIdValue *value);

int32_t nep_tables_downstream4_map_dump_next(u32 *bucket, u32 *last,
				       Tether4Key *key, Tether4Value *value);
int32_t nep_tables_upstream4_map_dump_next(u32 *bucket, u32 *last,
				       Tether4Key *key, Tether4Value *value);
int32_t nep_tables_downstream6_map_dump_next(u32 *bucket, u32 *last,
					 TetherDownstream6Key *key, Tether6Value *value);
int32_t nep_tables_upstream6_map_dump_next(u32 *bucket, u32 *last,
				       TetherUpstream6Key *key, Tether6Value *value);
int32_t nep_tables_ingress6_map_dump_next(u32 *bucket, u32 *last,
					  ClatIngress6Key* key, ClatIngress6Value* value);
int32_t nep_tables_egress4_map_dump_next(u32 *bucket, u32 *last,
					 ClatEgress4Key* key, ClatEgress4Value* value);
int32_t nep_tables_flowid_map_dump_next(u32 *bucket, u32 *last,
					TetherFlowIdKey *key, TetherFlowIdValue *value);
#endif //NEP_TABLES_H
