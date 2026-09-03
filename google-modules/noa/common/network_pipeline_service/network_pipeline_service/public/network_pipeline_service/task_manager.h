/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ID table for all NEP task.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_SERVICE_TASK_MANAGER_H
#define NOA_NETWORK_PIPELINE_SERVICE_TASK_MANAGER_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/nested_ring_task.h"
#else /* linux */
#include <cstdint>
#include <atomic>

#include "network_pipeline_framework/nested_ring_task.h"
#endif /* linux */

/**
 * As a 16-bit bitmap is used to track the tasks for each group,
 * a maximum of 16 tasks can be supported (indexed 0-15).
 */
#define MAX_NEP_TASKS_PER_GROUP (16U)

enum NepTaskGroupId {
	// Place all callback-related tasks within this system group.
	kNepSystemTaskGroup = 0,
	kNepCacheDataTaskGroup,
	kNepCacheBufferPoolTaskGroup,
	kNepFetchHeaderTaskGroup,
	kNepAnalyzeDataTaskGroup,
	kNepRouteDataTaskGroup,
	kNepVpnTaskGroup,
	kMaxNepTaskGroupNum,
};

enum NepSystemTaskGroup {
	kNepCacheDescCompleteTask = 0,
	kNepRouteDataCompleteTask,
	kNepVpnCompleteTask,
	kNepVpnScheduledTask,
	kNepDmaAbortTask,
	kNepFetchHeaderCompleteTask,
	kNepCacheBufferPoolCompleteTask,
	kMaxNepTaskForSystem,
};
static_assert(kMaxNepTaskForSystem <= MAX_NEP_TASKS_PER_GROUP);

enum NepCacheBufferPoolTaskGroup {
	kNepCacheWlanNepBufferPoolPathTask = 0,
	kNepCacheModemNepBufferPoolPathTask,
	kMaxNepTaskForCacheBufferPool,
};
static_assert(kMaxNepTaskForCacheBufferPool <= MAX_NEP_TASKS_PER_GROUP);

enum NepCacheDataTaskGroup {
	kNepCacheWlanDeviceToHostPathTask = 0,
	kNepCacheWlanHostToDevicePathTask,
	kNepCacheModemDeviceToHostPathTask,
	kNepCacheModemDeviceToHostRxq0PathTask,
	kNepCacheModemDeviceToHostRxq1PathTask,
	kNepCacheModemDeviceToHostRxq2PathTask,
	kNepCacheModemHostToDevicePathTask,
	kMaxNepTaskForCacheData,
};
static_assert(kMaxNepTaskForCacheData <= MAX_NEP_TASKS_PER_GROUP);

enum NepFetchHeaderTaskGroup {
	kNepFetchHeaderWlanDeviceToHostPathTask = 0,
	kNepFetchHeaderModemDeviceToHostPathTask,
	kNepFetchHeaderModemDeviceToHostRxq0PathTask,
	kNepFetchHeaderModemDeviceToHostRxq1PathTask,
	kNepFetchHeaderModemDeviceToHostRxq2PathTask,
	kMaxNepFetchHeaderTask,
};
static_assert(kMaxNepFetchHeaderTask <= MAX_NEP_TASKS_PER_GROUP);

enum NepAnalyzeDataTaskGroup {
	kNepAnalyzeWlanPacketTask = 0,
	kNepAnalyzeModemPacketTask,
	kNepAnalyzeModemRxq0PacketTask,
	kNepAnalyzeModemRxq1PacketTask,
	kNepAnalyzeModemRxq2PacketTask,
	kNepInspectWlanPpfPacketTask,
	kNepVpnWlanRxPacketTask,
	kNepVpnModemRxPacketTask,
	kNepVpnWlanTxPacketTask,
	kNepVpnModemTxPacketTask,
	kMaxNepAnalyzeDataTask,
};
static_assert(kMaxNepAnalyzeDataTask <= MAX_NEP_TASKS_PER_GROUP);

enum NepRouteDataTaskGroup {
	kNepRouteWlanDeviceToHostPathTask = 0,
	kNepRouteWlanHostToDevicePathTask,
	kNepRouteModemDeviceToHostPathTask,
	kNepRouteModemDeviceToHostRxq0PathTask,
	kNepRouteModemDeviceToHostRxq1PathTask,
	kNepRouteModemDeviceToHostRxq2PathTask,
	kNepRouteModemHostToDevicePathTask,
	kMaxNepTaskForRouteData,
};
static_assert(kMaxNepTaskForRouteData <= MAX_NEP_TASKS_PER_GROUP);

/**
 * @brief Retrieves a specific task object based on its group and ID.
 *
 * @param[in] group The identifier of the group the task belongs to.
 * @param[in] id The identifier of the specific task within the group.
 *
 * @return A pointer to the requested `NestedRingTask` structure if found,
 * otherwise `NULL`.
 */
NestedRingTask *NepTaskGet(uint8_t group, uint8_t id);

#endif /* NOA_NETWORK_PIPELINE_SERVICE_TASK_MANAGER_H */
