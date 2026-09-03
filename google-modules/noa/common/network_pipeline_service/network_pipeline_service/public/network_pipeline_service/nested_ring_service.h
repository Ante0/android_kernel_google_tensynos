/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Network Pipeline Service With Nested Ring.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_NESTED_RING_SERVICE_H
#define NOA_NETWORK_PIPELINE_NESTED_RING_SERVICE_H

#ifdef linux
#include <linux/types.h>
#else /* linux */
#include <cstdint>
#endif /* linux */

/**
 * @brief Initializes and starts the Network Pipeline Service.
 *
 * This function initializes all stages of the Network Pipeline Service
 * and creates the TaskSchedule's running thread, thereby starting the
 * Pipeline Service operation.
 *
 * @return 0 on success, negative error code otherwise.
 */
int32_t NepNestedRingServiceSetup(void);

/**
 * @brief Stops the Network Pipeline Service thread.
 *
 * This function stops the pipeline service thread.
 */
void NepNestedRingServiceExit(void);

#endif /* NOA_NETWORK_PIPELINE_NESTED_RING_SERVICE_H */
