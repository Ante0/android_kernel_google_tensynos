/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Ring Service Buffer Pool Component
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_RING_SERVICE_BUFFER_POOL_H__
#define __NOA_RING_SERVICE_BUFFER_POOL_H__

#ifdef linux
#include <common/ring.h>

#include "common/core.h"
#else /* linux */
#include <cstddef>

#include "common/core.h"
#include "common/ring.h"
#include "linux_port/types.h"
#endif /* linux */

typedef struct nep_ring_service_buffer_pool {
	struct noa_ring_wrapper *ring[NOA_RING_SERVICE_BUFFER_POOL_NUMBER];
} nep_ring_service_buffer_pool;

/**
 * noa_ring_service_buffer_pool_singleton - Retrieve the singleton instance of the buffer pool
 *
 * This function returns a pointer to the static singleton instance of the
 * ring service buffer pool. This ensures a single global point of access
 * for the pool structure.
 *
 * Return: Pointer to the singleton @nep_ring_service_buffer_pool instance.
 */
nep_ring_service_buffer_pool *noa_ring_service_buffer_pool_singleton(void);

/**
 * noa_ring_service_buffer_pool_register - Register the global buffer pool instance
 *
 * Assigns the provided buffer pool pointer to the global context. This registered
 * instance is subsequently used by `noa_ring_service_buffer_pool_init` and
 * `noa_ring_service_buffer_get`.
 *
 * @buffer_pool: Pointer to the @nep_ring_service_buffer_pool to register
 */
void noa_ring_service_buffer_pool_register(nep_ring_service_buffer_pool *buffer_pool);

/**
 * noa_ring_service_buffer_pool_init - Initialize the buffer pool rings
 *
 * Iterates through all defined buffer pool IDs (WLAN, MODEM, NETENGINE). For each ID,
 * it resolves the corresponding hardware/software ring ID and retrieves the ring
 * instance from the Ring Manager. The retrieved ring pointers are stored in the global
 * buffer pool structure.
 *
 * Return:
 * * 0 - Success
 * * -EINVAL - If a corresponding ring instance cannot be found for a pool ID.
 */
int32_t noa_ring_service_buffer_pool_init(void);

/**
 * noa_ring_service_buffer_get - Get a buffer descriptor from a specific pool
 *
 * Attempts to consume a buffer descriptor from the ring associated with the
 * given pool ID. It checks if the ring is valid, active, and not empty.
 * If data is available, it reads from the current tail, updates the tail index,
 * and copies the data to @buffer_item.
 *
 * @id: The buffer pool ID (e.g., `NOA_RING_SERVICE_BUFFER_POOL_WLAN`)
 * @buffer_item: Pointer to where the retrieved buffer descriptor will be stored
 *
 * Return:
 * * 0 - Success
 * * -EINVAL - If @id is invalid, or the associated ring is NULL or inactive.
 * * -EAGAIN - If the ring is empty (no buffers available).
 */
int32_t noa_ring_service_buffer_get(uint8_t id, noa_buffer_pool_desc *buffer_item);

#endif /* __NOA_RING_SERVICE_BUFFER_POOL_H__ */
