// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of NEP Pipeline Engine
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "engine.h"
#include "stage.h"
#else /* linux */
#include <cerrno>
#include <cstdint>

#include "linux_port/log.h"
#include "linux_port/list.h"
#include "linux_port/container_of.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#endif /* linux */

static void ConstructEngineHelper(struct NepEngine *engine, struct NepTaskScheduler *scheduler,
				  int32_t (*func)(struct NepEngine *engine),
				  int32_t (*processing)(struct NepEngine *engine,
							struct NepProcessingRequest *request));

static void ReleaseRequest(struct NepAsyncProcessingRequest *req)
{
	struct NepAsyncEngine *engine = req->engine;
	memset(&req->basic, 0, sizeof(req->basic));
	req->status = 0;
	req->pkt_owner = NULL;
	engine->free_request_bitmap |= req->id;
}

static struct NepAsyncProcessingRequest *GetFreeRequest(struct NepAsyncEngine *engine)
{
	struct NepAsyncProcessingRequest *req;
	int32_t idx;
	if (!engine->free_request_bitmap) {
		return NULL;
	}

	idx = ffs(engine->free_request_bitmap) - 1;
	req = &engine->request_array[idx];
	engine->free_request_bitmap &= ~(req->id);
	return req;
}

int32_t NepProcessingRequestCallback(struct NepProcessingRequest *basic, int32_t status)
{
	struct NepAsyncProcessingRequest *req =
		container_of(basic, struct NepAsyncProcessingRequest, basic);
	struct NepStage *stage;
	if (!req || !req->pkt_owner) {
		return -EINVAL;
	}
	req->status = status;
	stage = (struct NepStage *)req->pkt_owner;
	NepRunnableTaskSchedule(&req->task, stage->scheduler);
	return 0;
}

static int32_t CompleteAsyncProcessingRequest(struct NepRunnableTask *task, void *data)
{
	struct NepAsyncProcessingRequest *req =
		container_of(task, struct NepAsyncProcessingRequest, task);
	struct NepStage *stage = (struct NepStage *)req->pkt_owner;
	struct PktContainer *container = stage->container;

	(void)data;
	NepPacketTablePacketRelease(req->basic.packet);
	container->complete(container, stage, req->basic.item, req->status);
	ReleaseRequest(req);
	return 0;
}

void NepAsyncEngineConstructor(struct NepAsyncEngine *engine, uint8_t size,
			       struct NepAsyncProcessingRequest *request_array,
			       struct NepTaskScheduler *scheduler,
			       int32_t (*processing)(struct NepEngine *engine,
						     struct NepProcessingRequest *request))
{
	ConstructEngineHelper(&engine->basic, scheduler, NepAsyncEngineProcessing, processing);
	engine->size = size;
	engine->free_request_bitmap = (uint32_t)((1ULL << engine->size) - 1UL);
	engine->request_array = request_array;
}

void NepAsyncEngineInit(struct NepAsyncEngine *engine)
{
	uint8_t i;

	if (!engine) {
		return;
	}
	NepAsyncEngineReset(engine);

	for (i = 0; i < engine->size; ++i) {
		struct NepAsyncProcessingRequest *req = &engine->request_array[i];
		req->id = 1U << i;
		req->engine = engine;
		NepRunnableTaskConstructor(&req->task, CompleteAsyncProcessingRequest, NULL);
		memset(&req->basic, 0, sizeof(req->basic));
		req->status = 0;
		req->pkt_owner = NULL;
	}
}

int32_t NepAsyncEngineProcessing(struct NepEngine *engine)
{
	struct NepAsyncEngine *async_engine = container_of(engine, struct NepAsyncEngine, basic);

	struct NepPacketTable *table;
	if (!engine || !engine->scheduler || !engine->scheduler->pkt_table) {
		pr_err("Invalid argument when perform engine processing\n");
		return -EINVAL;
	}
	table = engine->scheduler->pkt_table;

	while (!list_empty(&engine->stage_todo_list)) {
		int32_t ret;
		struct NepAsyncProcessingRequest *req;
		struct NepStage *stage =
			list_first_entry(&engine->stage_todo_list, struct NepStage, eng_hook);
		struct PktContainer *container = stage->container;

		req = GetFreeRequest(async_engine);
		if (!req) {
			return -EAGAIN;
		}

		ret = container->get_awaiting_packet(container, &req->basic.item);
		if (ret) {
			goto next;
		}
		req->pkt_owner = (void *)stage;

		ret = NepPacketTablePacketGet(table, req->basic.item.pkt_id, &req->basic.packet);
		if (ret) {
			goto next;
		}

		ret = engine->processing(engine, &req->basic);
		if (ret == -EAGAIN) {
			NepPacketTablePacketRelease(req->basic.packet);
			ReleaseRequest(req);
			return ret;
		}

	next:
		container->processing(container, req->basic.item);
		if (ret) {
			if (req->basic.item.pkt_id) {
				container->complete(container, stage, req->basic.item, ret);
			}
			if (req->basic.packet) {
				NepPacketTablePacketRelease(req->basic.packet);
			}
			ReleaseRequest(req);
		}
		if (container->has_awaiting_packet(container)) {
			// Implement a round-robin algorithm to prevent starvation
			list_move_tail(&stage->eng_hook, &engine->stage_todo_list);
		} else {
			list_del_init(&stage->eng_hook);
		}
	}

	list_del_init(&engine->waiting_hook);

	return 0;
}

int32_t NepEngineProcessing(struct NepEngine *engine)
{
	struct NepPacketTable *table;
	if (!engine || !engine->scheduler || !engine->scheduler->pkt_table) {
		pr_err("Invalid argument when perform engine processing\n");
		return -EINVAL;
	}
	table = engine->scheduler->pkt_table;

	while (!list_empty(&engine->stage_todo_list)) {
		int32_t ret;
		struct NepProcessingRequest req = { { 0, 0 }, NULL };
		struct NepStage *stage =
			list_first_entry(&engine->stage_todo_list, struct NepStage, eng_hook);
		struct PktContainer *container = stage->container;

		ret = container->get_awaiting_packet(container, &req.item);
		if (ret) {
			goto err;
		}

		ret = NepPacketTablePacketGet(table, req.item.pkt_id, &req.packet);
		if (ret) {
			goto err;
		}

		ret = engine->processing(engine, &req);
		NepPacketTablePacketRelease(req.packet);
		if (ret == -EAGAIN) {
			return ret;
		} else if (ret) {
			goto err;
		}

	err:
		container->processing(container, req.item);
		if (req.item.pkt_id) {
			container->complete(container, stage, req.item, ret);
		}
		if (container->has_awaiting_packet(container)) {
			// Implement a round-robin algorithm to prevent starvation
			list_move_tail(&stage->eng_hook, &engine->stage_todo_list);
		} else {
			list_del_init(&stage->eng_hook);
		}
	}

	list_del_init(&engine->waiting_hook);

	return 0;
}

static void ConstructEngineHelper(struct NepEngine *engine, struct NepTaskScheduler *scheduler,
				  int32_t (*func)(struct NepEngine *engine),
				  int32_t (*processing)(struct NepEngine *engine,
							struct NepProcessingRequest *request))
{
	engine->func = func;
	engine->processing = processing;
	engine->scheduler = scheduler;
	INIT_LIST_HEAD(&engine->stage_todo_list);
	INIT_LIST_HEAD(&engine->waiting_hook);
	INIT_LIST_HEAD(&engine->tracking_hook);
}

void NepEngineConstructor(struct NepEngine *engine, struct NepTaskScheduler *scheduler,
			  int32_t (*processing)(struct NepEngine *engine,
						struct NepProcessingRequest *request))
{
	ConstructEngineHelper(engine, scheduler, NepEngineProcessing, processing);
}
