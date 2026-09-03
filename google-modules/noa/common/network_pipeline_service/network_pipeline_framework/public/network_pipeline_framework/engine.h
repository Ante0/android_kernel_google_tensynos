/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Pipeline Engine
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_ENGINE_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_ENGINE_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "packet_table.h"
#include "task_scheduler.h"
#else /* linux */
#include <cstdint>

#include "linux_port/list.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

struct NepEngine {
	int32_t (*func)(struct NepEngine *engine);
	struct NepTaskScheduler *scheduler;
	struct list_head stage_todo_list;
	int32_t (*processing)(struct NepEngine *engine, struct NepProcessingRequest *request);
	struct list_head waiting_hook;
	struct list_head tracking_hook;
};

#define __INIT_NEP_ENGINE_HELPER(name, s, f, p_f)                                                  \
	{                                                                                          \
		.func = f,                                                                         \
		.scheduler = s,                                                                    \
		.stage_todo_list = { &(name.stage_todo_list), &(name.stage_todo_list) },           \
		.processing = p_f,                                                                 \
		.waiting_hook = { &(name.waiting_hook), &(name.waiting_hook) },                    \
		.tracking_hook = { &(name.tracking_hook), &(name.tracking_hook) },                 \
	}

#define __INIT_NEP_ENGINE(name, s, p_f) __INIT_NEP_ENGINE_HELPER(name, s, NepEngineProcessing, p_f)
#define INIT_NEP_ENGINE(name, s, func) struct NepEngine name = __INIT_NEP_ENGINE(name, s, func)

struct NepAsyncEngine;
struct NepAsyncProcessingRequest {
	int32_t id;
	struct NepAsyncEngine *engine;
	struct NepProcessingRequest basic;
	struct NepRunnableTask task;
	int32_t status;
	void *pkt_owner;
	void *data;
};

struct NepAsyncEngine {
	struct NepEngine basic;
	uint8_t size;
	uint32_t free_request_bitmap;
	struct NepAsyncProcessingRequest *request_array;
};

#define __INIT_NEP_ASYNC_ENGINE(name, s, p_f)                                                      \
	__INIT_NEP_ENGINE_HELPER(name, s, NepAsyncEngineProcessing, p_f)
#define NEP_ASYNC_ENGINE_REQUEST_NUM (32U)
#define INIT_NEP_ASYNC_ENGINE_WITH_PREFIX(prefix, name, sz, sche, func)                            \
	static_assert(sz <= NEP_ASYNC_ENGINE_REQUEST_NUM);                                         \
	prefix struct NepAsyncProcessingRequest name##_arr[sz];                                    \
	prefix struct NepAsyncEngine name = {                                                      \
		.basic = __INIT_NEP_ASYNC_ENGINE(name.basic, sche, func),                          \
		.size = sz,                                                                        \
		.free_request_bitmap = (uint32_t)((1ULL << sz) - 1ULL),                            \
		.request_array = &(name##_arr[0]),                                                 \
	}
#define INIT_NEP_ASYNC_ENGINE(name, sz, sche, func)                                                \
	INIT_NEP_ASYNC_ENGINE_WITH_PREFIX(, name, sz, sche, func)

static inline void NepEngineReset(struct NepEngine *engine)
{
	if (!engine) {
		return;
	}
	// TODO(b/367865490) - Stop the Engine
	list_del_init(&engine->stage_todo_list);
	list_del_init(&engine->waiting_hook);
}

static inline void NepAsyncEngineReset(struct NepAsyncEngine *engine)
{
	if (!engine) {
		return;
	}
	NepEngineReset(&engine->basic);
	engine->free_request_bitmap = (uint32_t)((1ULL << engine->size) - 1UL);
}

static inline void NepEngineAddToScheduler(struct NepEngine *engine,
					   struct NepTaskScheduler *scheduler)
{
	if (!engine || !scheduler) {
		return;
	}
	engine->scheduler = scheduler;
	list_add_tail(&engine->tracking_hook, &scheduler->all_engines);
}

static inline void NepAsyncEngineAddToScheduler(struct NepAsyncEngine *engine,
						struct NepTaskScheduler *scheduler)
{
	NepEngineAddToScheduler(&engine->basic, scheduler);
}

/**
 * @brief Initializes an AsyncEngine object.
 *
 * All AsyncEngine objects must be initialized by calling
 * this function before they can be used.
 *
 * @param[in] engine  The pointer to the AsyncEngine object
 * to be initialized.
 */
void NepAsyncEngineInit(struct NepAsyncEngine *engine);

/**
 * @brief This is the request callback function.
 *
 * All AsyncEngine must call this callback to mark the request is done when
 * it finishes the task. This function can be run in interrupt context.
 *
 * @param[in] basic  The pointer to the NepProcessingRequest structure.
 * This structure contains the information about the request.
 * @param[in] status  The completed status of this request.
 *
 * @return 0 if the callback is processed successfully,
 *     otherwise a negative error code is returned.
 */
int32_t NepProcessingRequestCallback(struct NepProcessingRequest *basic, int32_t status);

/**
 * @brief Process packets within the Network Async Engine.
 *
 * This function processes packets that are queued within the Network Engine's
 * stages. It iterates through a list of stages that have pending packets
 * (`stage_todo_list`) and processes each packet using the engine's processing
 * function.
 *
 * Once all awaiting packets have been processed, the engine removes itself
 * from the task scheduler's `engine_tasks` list.
 *
 * @param[in] engine  A pointer to the NepAsyncEngine to perform the processing.
 *
 * @return  The result of the operation.
 * @retval 0  Success, all packets were processed.
 * @retval -EAGAIN  The engine is busy and cannot process further packets at
 * this time.
 * @retval <0  Error code indicating an issue during packet processing.
 */
int32_t NepAsyncEngineProcessing(struct NepEngine *engine);

/**
 * @brief Construct a NepAsyncEngine object.
 *
 * This function initializes a NepAsyncEngine object with the provided parameters.
 * It is equivalent to the INIT_NEP_ASYNC_ENGINE() macro.
 *
 * @param[in] engine The NepAsyncEngine object to be initialized.
 * @param[in] size The number of request slots in the request array.
 * @param[in] request_array The array of NepAsyncProcessingRequest structures. This
 * array holds the requests that will be used by the engine.
 * @param[in] scheduler The NepTaskScheduler to be associated with this engine.
 * @param[in] processing The processing function that will
 * be called by the engine to handle incoming requests.
 */
void NepAsyncEngineConstructor(struct NepAsyncEngine *engine, uint8_t size,
			       struct NepAsyncProcessingRequest *request_array,
			       struct NepTaskScheduler *scheduler,
			       int32_t (*processing)(struct NepEngine *engine,
						     struct NepProcessingRequest *request));

/**
 * @brief Process packets within the Network Engine.
 *
 * This function processes packets that are queued within the Network Engine's
 * stages. It iterates through a list of stages that have pending packets
 * (`stage_todo_list`) and processes each packet using the engine's processing
 * function.
 *
 * Once all awaiting packets have been processed, the engine removes itself
 * from the task scheduler's `engine_tasks` list.
 *
 * @param[in] engine  A pointer to the NepEngine to perform the processing.
 *
 * @return  The result of the operation.
 * @retval 0  Success, all packets were processed.
 * @retval -EAGAIN  The engine is busy and cannot process further packets at
 * this time.
 * @retval <0  Error code indicating an issue during packet processing.
 */
int32_t NepEngineProcessing(struct NepEngine *engine);

/**
 * @brief Construct a NepEngine object.
 *
 * This function initializes a NepEngine object with the provided task scheduler
 * and processing function. It is equivalent to the INIT_NEP_ENGINE() macro.
 *
 * @param[in] engine The NepEngine object to be initialized.
 * @param[in] scheduler The NepTaskScheduler to be associated with this engine.
 * @param[in] processing The processing function that will be called by the engine
 * to handle incoming requests.
 */
void NepEngineConstructor(struct NepEngine *engine, struct NepTaskScheduler *scheduler,
			  int32_t (*processing)(struct NepEngine *engine,
						struct NepProcessingRequest *request));

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_ENGINE_H */
