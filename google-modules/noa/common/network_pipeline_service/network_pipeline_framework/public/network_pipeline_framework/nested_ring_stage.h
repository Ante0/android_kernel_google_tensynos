/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Nested Ring Stage
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_STAGE_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_STAGE_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "common/ring.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/nested_ring_utils.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#else /* linux */
#include <cerrno>
#include <cstdint>

#include "common/ring.h"
#include "linux_port/list.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/nested_ring_utils.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#endif /* linux */

typedef struct {
	uint16_t size;
	uint16_t item_len;
	uint16_t processed_idx;
	uint16_t *head_idx_ptr;
	uint16_t *tail_idx_ptr;
	char *base;
} NestedCachedRing;

/**
 * @brief Initializes a cached ring buffer.
 *
 * @param[in] ring Pointer to the NestedCachedRing structure to initialize.
 * @param[in] base Starting address of the memory buffer used for ring data.
 * @param[in] item_len Size in bytes of each item stored in the ring.
 * @param[in] size The total number of items the ring buffer can hold.
 * @param[in] head Pointer to the external variable holding the head index (offset).
 * @param[in] tail Pointer to the external variable holding the tail index (offset).
 *
 * @return None.
 */
void NestedCachedRingInit(NestedCachedRing *ring, char *base, uint16_t item_len, uint16_t size,
			  uint16_t *head, uint16_t *tail);

typedef struct {
	uint16_t item_len_bitshift;
	uintptr_t base;
	uintptr_t end_mask;
	uintptr_t processed_addr;
	uintptr_t *head_addr_ptr;
	uintptr_t *tail_addr_ptr;
} NestedShadowRing;

/**
 * @brief Initializes a shadow ring buffer.
 *
 * @param[in] ring Pointer to the NestedShadowRing structure to initialize.
 * @param[in] base Starting memory address for the ring buffer data. Must be
 * aligned to the total buffer size (`size` * `item_len`).
 * @param[in] item_len Size in bytes of each item. Must be a power of two.
 * @param[in] size The total number of items the ring can hold. Must be a power of two.
 * @param[in] head Pointer to the external variable holding the head *address*.
 * @param[in] tail Pointer to the external variable holding the tail *address*.
 *
 * @return None.
 */
void NestedShadowRingInit(NestedShadowRing *ring, uintptr_t base, uint16_t item_len, uint16_t size,
			  uintptr_t *head, uintptr_t *tail);

struct NestedRingEngine;

typedef struct {
	// Existing counters
	uint32_t completed_counter;
	// Total number of times the stage processing function was called
	uint32_t run_count;
	// Number of Ring Items (data slots) actually processed
	uint32_t items_processed_count;
	// Number of times the stage processing function returned -EAGAIN
	uint32_t eagain_count;

	// Cumulative time spent executing the stage's core logic
	uint64_t total_execution_time_ticks;
	// Cumulative time spent in func calls that ultimately returned -EAGAIN
	uint64_t eagain_cpu_overhead_ticks;
} NestedRingStageMetrics;

typedef struct NestedRingStage {
	NestedShadowRing shadow_ring;
	uintptr_t **shadow_ring_completed_pptr;
	NestedCachedRing cached_ring;
	uint16_t **cached_ring_completed_pptr;
	void *context;
	struct NestedRingEngine *engine;
	struct NestedRingStage *next_stage;
	NestedRingTask *task;
	bool (*has_remaining_data)(struct NestedRingStage *stage);
	const char *name;
	struct list_head list;
	void (*reset)(void *context);
	NestedRingStageMetrics metrics;
} NestedRingStage;

typedef struct {
	struct NestedRingStage *stage;
	bool completed;
	uintptr_t start_shadow_ring_addr;
	uintptr_t shadow_ring_addr;
	uint16_t start_cached_ring_idx;
	uint16_t cached_ring_idx;
	void *context;
} NestedRingRequest;

typedef struct NestedRingRequestPool {
	uint8_t tail;
	uint8_t head;
	uint8_t size_mask;
	NestedRingRequest *arr;
} NestedRingRequestPool;

typedef struct {
	// Total number of times the post_complete Task was executed
	uint32_t post_complete_task_count;
	// Total number of Requests actually completed (callbacks invoked)
	uint32_t requests_completed_count;
	// Cumulative time spent executing the post_complete callback
	uint64_t total_post_complete_time_ticks;
} NestedRingEngineMetrics;

typedef struct NestedRingEngine {
	int32_t (*processing)(NestedRingStage *, NestedRingRequest *);
	void (*error_handling)(NestedRingStage *, NestedRingRequest *, int32_t ret);
	NestedRingTask *post_complete_task;
	void (*post_complete)(NestedRingRequest *request);
	NestedRingRequestPool *request_pool;
	NepBitmapTaskScheduler *scheduler;
	NestedRingEngineMetrics metrics;
} NestedRingEngine;

/**
 * @brief Initializes a processing stage within the nested ring pipeline.
 *
 * Initializes a `NestedRingStage`, configuring its role within a data processing
 * pipeline. It associates the stage with input rings (shadow and cached), a
 * processing engine, and optional stage-specific context data. The stage is
 * named and linked to a potential subsequent stage (`next_stage`) in the
 * pipeline. An execution task is configured based on whether the stage operates
 * synchronously or asynchronously.
 *
 * The design anticipates that this stage's read indices are linked to the next
 * stage's write indices to enable data flow.
 *
 * @param[in] stage Pointer to the NestedRingStage structure to initialize.
 * @param[in] is_async True if the stage operates asynchronously, false for synchronous.
 * @param[in] shadow_ring_info Configuration data for the shadow ring used by this stage.
 * @param[in] cached_ring_info Configuration data for the cached ring used by this stage.
 * @param[in] context Pointer to custom data used by the engine for this stage.
 * @param[in] engine Pointer to the engine responsible for processing this stage.
 * @param[in] next_stage Pointer to the next stage in the pipeline (can be NULL).
 * @param[in] task Pointer to the task structure associated with this stage's execution.
 * @param[in] name A descriptive name for this stage.
 * @param[in] reset Function pointer to reset the stage's context.
 *
 * @return None.
 */
void NestedRingStageInit(NestedRingStage *stage, bool is_async,
			 const NestedShadowRing shadow_ring_info,
			 const NestedCachedRing cached_ring_info, void *context,
			 NestedRingEngine *engine, NestedRingStage *next_stage,
			 NestedRingTask *task, const char *name, void (*reset)(void *));
/**
 * @brief Initializes a cache-specific processing stage within the nested ring pipeline.
 *
 * Initializes a `NestedRingStage` that is specifically configured for caching
 * operations within a data processing pipeline. It associates the stage with input
 * rings (shadow and cached), a processing engine, and optional stage-specific
 * context data. The stage is named and linked to a potential subsequent stage
 * (`next_stage`). An execution task is configured based on whether the stage
 * operates synchronously or asynchronously.
 *
 * The design anticipates that this stage's read indices are linked to the next
 * stage's write indices to enable data flow.
 *
 * @param[in] stage Pointer to the `NestedRingStage` structure to initialize.
 * @param[in] is_async True if the stage operates asynchronously, false for synchronous.
 * @param[in] shadow_ring_info Configuration data for the shadow ring used by this stage.
 * @param[in] cached_ring_info Configuration data for the cached ring used by this stage.
 * @param[in] context Pointer to custom data used by the engine for this stage.
 * @param[in] engine Pointer to the engine responsible for processing this stage.
 * @param[in] next_stage Pointer to the next stage in the pipeline (can be `NULL`).
 * @param[in] task Pointer to the task structure associated with this stage's execution.
 * @param[in] name A descriptive name for this stage.
 * @param[in] has_remaining_data Function pointer to check for remaining data,
 * used for rescheduling logic.
 * @param[in] reset Function pointer to reset the stage's context.
 *
 * @return None.
 */
void NestedRingCacheStageInit(NestedRingStage *stage, bool is_async,
			      const NestedShadowRing shadow_ring_info,
			      const NestedCachedRing cached_ring_info, void *context,
			      NestedRingEngine *engine, NestedRingStage *next_stage,
			      NestedRingTask *task, const char *name,
			      bool (*has_remaining_data)(struct NestedRingStage *),
			      void (*reset)(void *));
void NestedRingStageReset(NestedRingStage *stage, uint16_t src_ring_idx,
			  uint16_t src_ring_item_len);

/**
 * @brief Executes synchronous processing for a nested ring pipeline stage.
 *
 * Executes the synchronous processing logic for a NestedRingStage.
 * Invoked as a task, it calls the stage's engine to process available data.
 * After processing, it updates the stage's ring index and, if applicable,
 * triggers the scheduling of the next stage in the pipeline.
 *
 * If the current stage still has pending data, it reschedules itself for
 * continued processing.
 *
 * If the engine is temporarily unable to process, t returns a specific error
 * code (EAGAIN) to signal that the operation should be retried later.
 *
 * @param[in] context A pointer typically cast to NestedRingStage, representing
 * the stage to be processed.
 *
 * @return 0 on successful processing or handled error. Returns -EAGAIN if the
 * stage's engine is temporarily busy and processing should be retried.
 */
int32_t NestedRingStageProcessing(void *context);
/**
 * @brief Task to process batches of completed asynchronous requests.
 *
 * Task function responsible for batch post-processing of completed asynchronous
 * requests for a specific engine. Triggered after completion callbacks, it
 * iterates through all requests marked as finished. For each completed request,
 * it optionally invokes a user-defined `post_complete` function for final custom
 * actions. Following this, it finalizes the request processing internally and
 * returns the request resources to the engine's pool.
 *
 * @param[in] context A pointer typically cast to NestedRingEngine, representing
 * the engine whose completed requests are to be processed.
 *
 * @return Always returns 0.
 */
int32_t NestedRingRequestCompleteTask(void *context);
/**
 * @brief Callback signaling the completion of an asynchronous engine request.
 *
 * Callback function invoked when an asynchronous request completes.
 * Typically triggered by hardware or an async mechanism, it notifies the nested
 * ring service that a specific request (`req`) has finished processing. The
 * function marks the request as completed within the associated engine's request
 * pool and schedules a separate task to handle post-completion logic. This
 * allows the callback to remain lightweight and suitable for execution in
 * interrupt context.
 *
 * @param[in] req Pointer to the NestedRingRequest structure that has completed
 * processing.
 *
 * @return None.
 */
void NestedRingRequestCompleteCallback(void *request);
/**
 * @brief Initializes a request pool for asynchronous engine operations.
 *
 * @param[in] pool Pointer to the NestedRingRequestPool structure to initialize.
 * @param[in] size The capacity of the pool (number of requests). Must be a power of two.
 * @param[in] arr Pointer to the pre-allocated array of NestedRingRequest structures.
 *
 * @return None.
 */
void NestedRingRequestPoolInit(NestedRingRequestPool *pool, uint8_t size, NestedRingRequest *arr);
/**
 * @brief Initializes a synchronous nested ring processing engine.
 *
 * Configures a `NestedRingEngine` intended for synchronous stage processing.
 * Initialization primarily involves associating the engine with the specific
 * function that implements the processing logic for stages using this engine.
 *
 * @param[in] engine Pointer to the NestedRingEngine structure to initialize.
 * @param[in] processing Function pointer to the core processing logic routine for
 * this engine.
 *
 * @return None.
 */
void NestedRingEngineInit(NestedRingEngine *engine,
			  int32_t (*processing)(NestedRingStage *, NestedRingRequest *));
/**
 * @brief Initializes an asynchronous nested ring processing engine.
 *
 * Configures a `NestedRingEngine` designed for asynchronous stage processing.
 * This involves providing the core asynchronous `processing` function and linking
 * the engine to essential components for managing the async workflow: a
 * `NestedRingRequestPool` for request objects, and a `NestedRingTask`
 * (`post_complete_task`) dedicated to handling batch completion processing.
 *
 * This completion task is set up internally to execute the necessary cleanup.
 * An optional user-defined function (`post_complete`) can be registered for
 * final handling steps before requests are released back to the pool.
 *
 * @param[in] engine Pointer to the NestedRingEngine structure to initialize.
 * @param[in] processing Function pointer to the core asynchronous processing logic.
 * @param[in] post_complete_task Task used for batch processing of completed requests.
 * @param[in] post_complete Optional function pointer for custom post-completion handling.
 * @param[in] request_pool Pointer to the request pool used by this engine.
 *
 * @return None.
 */
void NestedRingAsyncEngineInit(NestedRingEngine *engine,
			       int32_t (*processing)(NestedRingStage *, NestedRingRequest *),
			       NestedRingTask *post_complete_task,
			       void (*post_complete)(NestedRingRequest *request),
			       NestedRingRequestPool *request_pool,
			       NepBitmapTaskScheduler *scheduler);
/**
 * @brief Initiates asynchronous processing for a nested ring pipeline stage.
 *
 * Initiates asynchronous processing for a `NestedRingStage`. Executed as a task,
 * it acquires a request object from the associated engine's pool. This request
 * is populated with the stage's current processing state and submitted to the
 * engine's non-blocking `processing` function, typically targeting hardware.
 *
 * It allows multiple requests to be inflight concurrently. After successful
 * submission, the stage updates its internal `processed` index indicating the
 * data range covered by the request.
 *
 * The task reschedules itself if more input data may be available. Completion
 * of the request is handled separately via callbacks.
 *
 * @param[in] context A pointer typically cast to `NestedRingStage`, representing
 * the stage for which to initiate asynchronous processing.
 *
 * @return 0 on successful request submission or handled error. Returns -EAGAIN
 * if a request object could not be obtained from the pool or the engine could
 * not accept the request immediately.
 */
int32_t NestedRingStageAsyncProcessing(void *context);

#ifdef linux
#define CAST_NESTED_RING_ITEM(item, addr) ((typeof(item))(addr))
#else /* linux */
#define CAST_NESTED_RING_ITEM(item, addr) (reinterpret_cast<__typeof__(item)>(addr))
#endif /* linux */

static inline uintptr_t shadow_ring_move_pos(NestedShadowRing *ring, uintptr_t pos, uint16_t cnt)
{
	return ring->base | ((pos + (cnt << ring->item_len_bitshift)) & ring->end_mask);
}

/**
 * @brief Macro for iterating over entries in a stage's shadow ring buffer.
 *
 * Provides a convenient loop construct for iterating over data entries in a
 * stage's shadow ring buffer. Intended for use within an engine's `processing`
 * function, this macro simplifies accessing each available data item.
 *
 * The macro automatically updates the address/index in `req` after each
 * iteration, reflecting the consumption progress.
 *
 * @param stage Pointer to the `NestedRingStage` being processed.
 * @param req Pointer to the `NestedRingRequest` associated with the current
 * processing batch. The `shadow_ring_addr` and `cached_ring_idx` within `req` are
 * updated by the macro.
 * @param end A loop variable (`uintptr_t`) set by the macro to the end address
 * (head pointer). Should be declared before the macro call.
 * @param shadow_entry Loop variable declared by the user to hold a pointer to the
 * current data entry. Its type determines the cast used internally.
 */
#define NESTED_RING_FOR_EACH_ENTRY(stage, req, end, shadow_entry)                                  \
	for (end = noa_readptr(stage->shadow_ring.head_addr_ptr),                                  \
	    shadow_entry = CAST_NESTED_RING_ITEM(shadow_entry, req->shadow_ring_addr);             \
	     req->shadow_ring_addr != end;                                                         \
	     req->shadow_ring_addr =                                                               \
		     shadow_ring_move_pos(&stage->shadow_ring, req->shadow_ring_addr, 1),          \
	    req->cached_ring_idx =                                                                 \
		     noa_ring_move_pos(req->cached_ring_idx, 1, stage->cached_ring.size),          \
	    shadow_entry = CAST_NESTED_RING_ITEM(shadow_entry, req->shadow_ring_addr))

/**
 * @brief Iterates over a specified number of entries in a stage's shadow ring.
 *
 * This macro provides a loop to iterate over data entries in a stage's shadow
 * ring, processing up to a specified maximum number of items. It is a variant
 * of `NESTED_RING_FOR_EACH_ENTRY` that adds a `count` parameter for bounded
 * iteration. The loop terminates when either the specified number of entries has
 * been processed or there is no more data available in the ring. The macro
 * automatically updates the ring pointers in the request object during
 * iteration.
 *
 * @param[in] stage Pointer to the `NestedRingStage` being processed.
 * @param[in] req Pointer to the `NestedRingRequest` for the current batch. The macro
 *                updates the `shadow_ring_addr` and `cached_ring_idx` fields.
 * @param[in] end A user-declared loop variable (`uintptr_t`) that the macro sets to
 *                the end address of available data.
 * @param[in] shadow_entry A user-declared loop variable that will hold a pointer to the
 *                         current data entry from the shadow ring.
 * @param[in] count The maximum number of entries to process. The loop decrements this
 *                  variable in each iteration.
 */
#define NESTED_RING_FOR_EACH_ENTRY_N(stage, req, end, shadow_entry, count)                         \
	for (end = noa_readptr(stage->shadow_ring.head_addr_ptr),                                  \
	    shadow_entry = CAST_NESTED_RING_ITEM(shadow_entry, req->shadow_ring_addr);             \
	     count != 0 && req->shadow_ring_addr != end;                                           \
	     req->shadow_ring_addr =                                                               \
		     shadow_ring_move_pos(&stage->shadow_ring, req->shadow_ring_addr, 1),          \
	    req->cached_ring_idx =                                                                 \
		     noa_ring_move_pos(req->cached_ring_idx, 1, stage->cached_ring.size),          \
	    shadow_entry = CAST_NESTED_RING_ITEM(shadow_entry, req->shadow_ring_addr), count--)

// This macro is intended for special use cases only. Prefer `NESTED_RING_FOR_EACH_ENTRY` for
// general-purpose iteration.
#define NESTED_RING_FOR_EACH_DESC_AND_ENTRY(stage, cached_ring_idx_ptr, shadow_ring_addr_ptr,      \
					    shadow_ring_end_addr, cached_desc, shadow_entry)       \
	for (cached_desc = CAST_NESTED_RING_ITEM(                                                  \
		     cached_desc, noa_ring_buf_pos(stage->cached_ring.base, *cached_ring_idx_ptr,  \
						   stage->cached_ring.item_len)),                  \
	    shadow_entry = CAST_NESTED_RING_ITEM(shadow_entry, *shadow_ring_addr_ptr);             \
	     *shadow_ring_addr_ptr != shadow_ring_end_addr;                                        \
	     *shadow_ring_addr_ptr =                                                               \
		     shadow_ring_move_pos(&stage->shadow_ring, *shadow_ring_addr_ptr, 1),          \
	    *cached_ring_idx_ptr =                                                                 \
		     noa_ring_move_pos(*cached_ring_idx_ptr, 1, stage->cached_ring.size),          \
	    cached_desc = CAST_NESTED_RING_ITEM(                                                   \
		    cached_desc, noa_ring_buf_pos(stage->cached_ring.base, *cached_ring_idx_ptr,   \
						  stage->cached_ring.item_len)),                   \
	    shadow_entry = CAST_NESTED_RING_ITEM(shadow_entry, *shadow_ring_addr_ptr))

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_STAGE_H */
