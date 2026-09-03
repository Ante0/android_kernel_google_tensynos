/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Buffer Pool Engine
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_MODULE_BUFFER_POOL_ENGINE_H
#define NOA_NETWORK_PIPELINE_MODULE_BUFFER_POOL_ENGINE_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

struct NepBufferPoolEngine {
	struct NepEngine engine;
};

/**
 * @brief Initialize NepBufferPoolEngine.
 *
 * Initialize the NepBufferPoolEngine for buffer management.
 *
 * @param[in] buffer_pool_engine The pointer to the NepBufferPoolEngine structure.
 * @param[in] scheduler The pointer to the NepTaskScheduler used for managing
 * asynchronous operations.
 *
 * @return 0 on success. Negative value indicates an error code.
 */
int32_t NepBufferPoolEngineInit(struct NepBufferPoolEngine *buffer_pool_engine,
				struct NepTaskScheduler *scheduler);
/**
 * @brief Gets the singleton instance of the NepBufferPoolEngine.
 *
 * This function retrieves the singleton instance of the
 * NepBufferPoolEngine. This function is guaranteed to succeed and will
 * always return a valid pointer to the singleton instance.
 *
 * @return A pointer to the NepBufferPoolEngine singleton instance.
 */
struct NepBufferPoolEngine *NepBufferPoolEngineSingletonGet(void);

#endif /* NOA_NETWORK_PIPELINE_MODULE_BUFFER_POOL_ENGINE_H */
