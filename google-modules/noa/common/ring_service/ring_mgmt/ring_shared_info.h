/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Ring shared information
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_RING_SHARED_INFO_H__
#define __NOA_RING_SHARED_INFO_H__
#ifdef linux
#include <linux/kernel.h>

#include <common/ring_id.h>
#include <common/noa_ring_id.h>
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa_ring_service_proxy_defs.h>
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#else /* linux */
#include <cstdint>

#include "common/ring_id.h"
#include "common/noa_ring_id.h"
#endif /* linux */

/**
 * noa_ring must be aligned with google_dpa_ring in dpa
 * under include/../google_dpa_ring_service_proxy_defs.h
 */
struct noa_ring {
	// This base address refers to its own Virtual Address (VA) space
	uint64_t base;
	// This dpa base address is specifically designated for ring services within the DPA view space
	uint64_t dpa_base;
	uint32_t write;
	uint32_t read;
	uint32_t ctrl; // the size of the ring
	uint32_t len; // the length of the packet descriptor
} __attribute__((packed, aligned(4)));

#ifdef linux
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
static_assert(sizeof(struct noa_ring) == sizeof(struct google_dpa_ring),
	      "noa_ring and google_dpa_ring not aligend");
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#endif /* linux */

struct ring_shared_info {
	struct noa_ring *ring_infos;
	uint32_t size;
} __attribute__((packed, aligned(4)));

/* Holding ring shared info - "noa_ring" */
struct NoaRingSharedInfoRootBase {
	uint32_t num;
	struct noa_ring entries[NOA_NEP_RING_MAX];
} __attribute__((packed, aligned(4)));

/* Wrapper struct, allowing AP to translate virtual addr. */
struct NoaRingSharedInfoRoot {
	struct NoaRingSharedInfoRootBase *base;
	struct noa_ring *(*get_ring)(const struct NoaRingSharedInfoRoot *root, uint8_t interface,
				     uint8_t flow, uint8_t category, uint8_t direction);
	void *(*memory_map)(void *context, uint64_t da, size_t len);
	void *memory_context;
};

struct ring_shared_info *noa_ring_service_shared_info_instance(void);
struct NoaRingSharedInfoRootBase *NoaRingSharedInfoRootBaseInstance(void);

#endif /* __NOA_RING_SHARED_INFO_H__ */
