// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ring Service Buffer Pool Component
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#include "ring_buffer_pool.h"

#ifdef linux
#include <linux/kernel.h>

#include "common/compiler.h"
#include "common/inttypes.h"
#include "common/noa_share/types.h"
#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/memory.h"
#include "ring_manager_instance.h"
#else /* linux */
#include <cinttypes>
#include <cstdint>

#include "arch/memory.h"
#include "common/ring_id.h"
#include "common/core.h"
#include "common/compiler.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/noa_share/types.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

nep_ring_service_buffer_pool *g_buffer_pool;

nep_ring_service_buffer_pool *noa_ring_service_buffer_pool_singleton(void)
{
	SEC_FAST_DATA static nep_ring_service_buffer_pool pool;
	return &pool;
}

void noa_ring_service_buffer_pool_register(nep_ring_service_buffer_pool *buffer_pool)
{
	g_buffer_pool = buffer_pool;
}

static int32_t pool_id_to_ring_id(uint8_t pool_id, uint8_t *ring_id)
{
	switch (pool_id) {
	case NOA_RING_SERVICE_BUFFER_POOL_WLAN:
		*ring_id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
						kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool);
		break;
	case NOA_RING_SERVICE_BUFFER_POOL_MODEM:
		*ring_id =
			NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
					     kNoaModemNepBufferPool);
		break;
	case NOA_RING_SERVICE_BUFFER_POOL_NETENGINE:
		*ring_id = NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel,
						kNoaNetengineBufferPool);
		break;
	default:
		pr_err("Invalid pool id %" PRIu16 "\n", pool_id);
		return -EINVAL;
	}

	return 0;
}

int32_t noa_ring_service_buffer_pool_init(void)
{
	int32_t ret;
	uint8_t pool_id = 0;
	for (pool_id = 0; pool_id < NOA_RING_SERVICE_BUFFER_POOL_NUMBER; ++pool_id) {
		uint8_t ring_id = 0;
		struct ring_manager_instance *ring_instance = NULL;
		ret = pool_id_to_ring_id(pool_id, &ring_id);
		if (ret) {
			return -EINVAL;
		}

		ring_instance = NoaRingManagerInfoInstanceGetById(ring_id, kNoaRingNepInput);
		if (!ring_instance) {
			pr_err("Invalid ring instance %" PRIu16 " when setup buffer pool\n",
			       ring_id);
			return -EINVAL;
		}
		g_buffer_pool->ring[pool_id] = &ring_instance->ring;
	}
	return 0;
}

int32_t noa_ring_service_buffer_get(uint8_t id, noa_buffer_pool_desc *buffer_item)
{
	uint16_t head;
	struct noa_ring_wrapper *ring = NULL;
	struct noa_buffer_pool_desc *item = NULL;

	if (id >= NOA_RING_SERVICE_BUFFER_POOL_NUMBER) {
		return -EINVAL;
	}

	ring = g_buffer_pool->ring[id];
	if (!ring || !is_noa_ring_activate(ring)) {
		return -EINVAL;
	}

	head = noa_ring_head_read_once(ring);
	if (__noa_ring_is_empty(head, ring->basic.tail)) {
		return -EAGAIN;
	}
	item = (struct noa_buffer_pool_desc *)noa_ring_curr_tail_pos(&ring->basic);
#ifndef linux
	InvalidateDCache(item, sizeof(struct noa_buffer_pool_desc));
#endif /* linux */
	*buffer_item = *item;
	noa_ring_info_tail_move(&ring->basic, 1);
	noa_ring_tail_write_once(ring, ring->basic.tail);

	return 0;
}
