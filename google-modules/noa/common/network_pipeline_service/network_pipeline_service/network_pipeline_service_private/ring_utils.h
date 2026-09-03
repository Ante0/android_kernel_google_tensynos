/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal API for Nested Ring Service.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_SERVICE_RING_UTILS_H
#define NOA_NETWORK_PIPELINE_SERVICE_RING_UTILS_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/nested_ring_stage.h"
#include "ring_mgmt/ring_manager_instance.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/nested_ring_stage.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

/**
 * @brief Registers a nested ring stage with a ring manager instance.
 *
 * This function associates a specific processing stage (NestedRingStage) with a ring
 * manager instance. This registration is essential for the system to manage and reset
 * all associated stages when the ring manager instance itself is reset. By adding the
 * stage to a list within the instance, it ensures that the stage will be included in
 * reset operations.
 *
 * @param[in] instance A pointer to the ring_manager_instance to which the stage
 *                     will be registered.
 * @param[in] stage A pointer to the NestedRingStage to be registered.
 */
void NepRingStageRegister(struct ring_manager_instance *instance, NestedRingStage *stage);

/**
 * @brief Registers a reset function for a ring manager instance.
 *
 * This function assigns a specific reset handler to the provided ring manager
 * instance. When the instance's reset mechanism is invoked, this registered
 * function will be executed. The "Reset" function is responsible for iterating
 * through all previously registered stages (using `NepRingStageRegister`).
 *
 * @param[in] instance A pointer to the ring_manager_instance for which the reset
 *                     function will be registered.
 */
void NepRingRegisterResetFunction(struct ring_manager_instance *instance);

/**
 * @brief Prints the status of all stages within a ring manager instance.
 *
 * This function outputs the current status of the ring, including the tail, head,
 * size, and item length. It then iterates through each registered stage, printing
 * its name, scheduled status, cached indices, and shadow addresses.
 *
 * @param[in] instance A pointer to the ring_manager_instance whose stages' status
 *                     will be printed.
 */
void NepRingStageStatusPrint(struct ring_manager_instance *instance);

#endif /* NOA_NETWORK_PIPELINE_SERVICE_RING_UTILS_H */
