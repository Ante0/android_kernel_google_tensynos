/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP ring management instance
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_NEP_RING_MANAGER_INSTANCE_H__
#define __NOA_NEP_RING_MANAGER_INSTANCE_H__

#ifdef linux
#include <linux/wait.h>
#include <linux/list.h>

#include <common/ring.h>
#include <common/ring_id.h>
#include <common/core.h>
#else /* linux */
#include <cstdint>

#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "linux_port/list.h"
#include "pw_function/function.h"
#include "pw_status/status.h"
#endif /* linux */

struct dma_processor {
#ifndef linux
	void *driver;
#endif /* linux */
	uint8_t channel;
	unsigned int size;
	unsigned long src;
	unsigned long dst;
	struct noa_desc *processing_desc;
};

#ifndef linux
using RingStoppedCallback = pw::InlineCallback<void(pw::Status status), 64U>;
#endif /* linux */

struct ring_manager_instance {
	bool is_stopping;
	uint8_t ring_id;
	uint8_t noa_ring_id;
	const char *name;
	struct noa_ring_wrapper ring;
	char wrap_buf[NOA_DESC_MAX_BYTE];
	// The following variables are only used by the output ring.
	uint8_t doorbell_port_id;
	struct dma_processor dma;
#ifdef linux
	wait_queue_head_t wait_stopping;
#else /* linux */
	RingStoppedCallback stopped_callback;
#endif /* linux */
	int32_t (*stopping)(struct ring_manager_instance *instance, uint8_t path_id);
	void (*stopped)(struct ring_manager_instance *instance);
	void *stop_context;
	struct list_head stage_list;
	void (*reset)(struct ring_manager_instance *instance);
};

struct NoaRingManagerInfo {
	uint8_t path_id;
	struct ring_manager_instance entries[kNoaRingNepDirectionMax];
};

struct NoaRingManagerInfoFlow {
	const uint32_t num;
	struct NoaRingManagerInfo *entries;
};

struct NoaRingManagerInfoNetwork {
	const uint32_t num;
	struct NoaRingManagerInfoFlow **entries;
};

struct NoaRingManagerInfoRoot {
	const uint32_t num;
	struct NoaRingManagerInfoNetwork **entries;
};

struct ring_manager_instance *noa_ring_manager_instance_get(uint8_t id, uint8_t type);
static inline struct noa_ring_wrapper *noa_ring_manager_ring_get(uint8_t id, uint8_t type)
{
	struct ring_manager_instance *instance = noa_ring_manager_instance_get(id, type);
	if (!instance)
		return NULL;
	return &instance->ring;
}
int noa_ring_manager_ring_activate(uint8_t path_id, uint8_t direction);
int noa_ring_manager_ring_deactivate(uint8_t path_id, uint8_t direction);
#ifndef linux
int noa_ring_manager_ring_deactivate(uint8_t path_id, uint8_t direction,
				     RingStoppedCallback &&callback);
int noa_ring_manager_ring_deactivate(struct ring_manager_instance *instance, uint8_t path_id,
				     RingStoppedCallback &&callback);
#endif /* linux */

int noa_ring_manager_instance_init(void);

static inline void NoaRingManagerInfoInit(struct NoaRingManagerInfo *info)
{
	int32_t i;
	for (i = 0; i < kNoaRingNepDirectionMax; i++) {
		struct ring_manager_instance *instance = &info->entries[i];
		instance->name = NULL;
		memset(&instance->ring, 0, sizeof(instance->ring));
		memset(&instance->wrap_buf, 0, sizeof(instance->wrap_buf));
		instance->doorbell_port_id = 0;
		memset(&instance->dma, 0, sizeof(instance->dma));
	}
}

struct ring_manager_instance *NoaRingManagerInfoInstanceGet(uint8_t interface, uint8_t flow,
							    uint8_t category, uint8_t direction);
static inline struct noa_ring_wrapper *NoaRingManagerRingGet(uint8_t interface, uint8_t flow,
							     uint8_t category, uint8_t direction)
{
	struct ring_manager_instance *instance =
		NoaRingManagerInfoInstanceGet(interface, flow, category, direction);
	if (!instance)
		return NULL;
	return &instance->ring;
}
struct ring_manager_instance *NoaRingManagerInfoInstanceGetById(uint8_t id, uint8_t direction);
static inline struct noa_ring_wrapper *NoaRingManagerRingGetById(uint8_t id, uint8_t direction)
{
	struct ring_manager_instance *instance = NoaRingManagerInfoInstanceGetById(id, direction);
	if (!instance)
		return NULL;
	return &instance->ring;
}
/**
 * @brief Get the root instance of the NoaRingManagerInfo.
 *
 * This function retrieves the root instance of the
 * NoaRingManagerInfo structure. It should be called only
 * once during initialization. Subsequent calls will
 * return the same instance.
 *
 * @return  A pointer to the instance of the
 *          NoaRingManagerInfoRoot structure.
 */
struct NoaRingManagerInfoRoot *NoaRingManagerRootInstance(void);
/**
 * @brief Initialize the NoaRingManagerInfo instances.
 *
 * This function initializes all ring instances associated with
 * the provided root instance. It also registers the root instance
 * as a singleton.
 *
 * @param[in] root  A pointer to the root instance of the
 * NoaRingManagerInfo structure.
 * @param[in] dma  A pointer to the dma driver instance.
 *
 * @return  The result of the operation.
 * @retval 0  Success.
 * @retval <0  Error code indicating the reason for failure.
 */
int32_t NoaRingManagerInstanceInit(struct NoaRingManagerInfoRoot *root, void *dma);
/**
 * @brief Registers the root instance for the ring service.
 *
 * This function sets a global pointer to the provided root structure, making it
 * the central entry point for all subsequent service operations. It should be
 * called once during initialization.
 *
 * @param[in] root A pointer to the `NoaRingManagerInfoRoot` structure representing
 * the service's root instance.
 *
 * @return None.
 */
void NoaRingManagerInstanceRootRegister(struct NoaRingManagerInfoRoot *root);
void NoaRingManagerInstancePrint(uint8_t interface, uint8_t flow, uint8_t category,
				 uint8_t direction);
void NoaRingManagerInstancePrintAll(void);
int32_t NoaRingManagerInstanceDumpAll(char *buf, int32_t len);

#endif /* __NOA_NEP_RING_MANAGER_INSTANCE_H__ */
