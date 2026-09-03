// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Buffer Pool Engine
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "network_pipeline_module/buffer_pool_engine.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "common/compiler.h"
#include "common/ring_id.h"
#include "network_pipeline_framework/engine.h"
#include "ring_service/ring_buffer_pool.h"
#else /* linux */
#include "network_pipeline_module/buffer_pool_engine.h"

#include <cerrno>
#include <cstdint>

#include "common/compiler.h"
#include "common/ring_id.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/engine.h"
#include "ring_mgmt/ring_buffer_pool.h"
#endif /* linux */

static int32_t GetBuffer(struct NepEngine *engine, struct NepProcessingRequest *request)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t type;
	int32_t pool_id;
	int32_t ret;
	noa_buffer_pool_desc buffer_desc;
	struct NepPacketContext *packet = request->packet;

	(void)engine;
	NoaRingPathIdParse(packet->dst, &interface, &flow, &type);
	pool_id = NoaRingOutputPathToPort(interface, flow);

	ret = noa_ring_service_buffer_get(pool_id, &buffer_desc);
	if (ret) {
		if (ret != -EAGAIN) {
			pr_err("Failed to get buffer with id %" PRIu32 ", ret: %" PRIu32 "\n",
			       pool_id, ret);
		}
		return -EAGAIN;
	}
	packet->packet_buffer[kOutputPacketBuffer].tkid = buffer_desc.tkid;
	packet->packet_buffer[kOutputPacketBuffer].dp_low = buffer_desc.dp_low;
	packet->packet_buffer[kOutputPacketBuffer].dp_high = (uint16_t)buffer_desc.dp_high;
	packet->packet_buffer[kOutputPacketBuffer].dv = buffer_desc.dv;
	return 0;
}

int32_t NepBufferPoolEngineInit(struct NepBufferPoolEngine *buffer_pool_engine,
				struct NepTaskScheduler *scheduler)
{
	buffer_pool_engine->engine.processing = GetBuffer;
	buffer_pool_engine->engine.scheduler = scheduler;
	NepEngineConstructor(&buffer_pool_engine->engine, scheduler, GetBuffer);
	NepEngineAddToScheduler(&buffer_pool_engine->engine, scheduler);
	return 0;
}

struct NepBufferPoolEngine *NepBufferPoolEngineSingletonGet(void)
{
	SEC_FAST_DATA static struct NepBufferPoolEngine engine;
	return &engine;
}
