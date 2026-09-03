// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Network Pipeline Service With Nested Ring.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "network_pipeline_service/nested_ring_service.h"

#include <linux/printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kthread.h>

#include "common/compiler.h"
#include "common/inttypes.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#include "network_pipeline_service/task_manager.h"
#include "network_pipeline_service/task_manager_internal.h"
#include "network_pipeline_service/wlan_data_path.h"
#include "network_pipeline_service/modem_data_path.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_isr_handler.h"
#include "ring_pipeline_service/ring_data_cache.h"
#include "ring_pipeline_service/ring_data_route.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#include "ring_pipeline_service/packet_header_fetch.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "vpn_pipeline_service.h"
#else /* linux */
#include "network_pipeline_service/nested_ring_service.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/compiler.h"
#include "linux_port/log.h"
#include "device_mgmt/manager.h"
#include "dma/templated_dma.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#include "network_pipeline_service/task_manager.h"
#include "network_pipeline_service_private/task_manager_internal.h"
#include "network_pipeline_service_private/wlan_data_path.h"
#include "network_pipeline_service_private/modem_data_path.h"
#include "net/vpn_pipeline_service.h"
#include "pw_thread/detached_thread.h"
#include "pw_thread_options_utils/utils.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_isr_handler.h"
#include "ring_pipeline_service/ring_data_cache.h"
#include "ring_pipeline_service/ring_data_route.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#include "ring_pipeline_service/packet_header_fetch.h"
#endif /* linux */

SEC_FAST_DATA static NepBitmapTaskScheduler g_scheduler;

#ifdef linux
struct task_struct *nested_ring_thread = NULL;

static int32_t RunThread(NepBitmapTaskScheduler *scheduler)
{
	int32_t ret;

	nested_ring_thread =
		kthread_run(NepBitmapTaskSchedulerRunner, scheduler, "nested ring service");
	if (IS_ERR(nested_ring_thread)) {
		ret = PTR_ERR(nested_ring_thread);
		pr_err("Failed to init ring service thread, err: %d\n", ret);
		nested_ring_thread = NULL;
		return ret;
	}
	return 0;
}

static void StopThread(void)
{
	if (nested_ring_thread) {
		kthread_stop(nested_ring_thread);
		nested_ring_thread = NULL;
	}
}
#else /* linux */
static constexpr size_t kPipelineServiceThreadStackSizeWords = (4 * 1024 / sizeof(uint32_t));
static constexpr uint32_t kPipelineServiceThreadPriority =
	::noa::module::thread_utils::GetThreadDefaultPriority();
DECLARE_THREAD_OPTIONS_EX(NestedRingPipelineService, kPipelineServiceThreadStackSizeWords,
			  kPipelineServiceThreadPriority)

static int32_t RunThread(NepBitmapTaskScheduler *scheduler)
{
	pw::thread::DetachedThread(GetThreadOptionsNestedRingPipelineService(),
				   [scheduler]() { NepBitmapTaskSchedulerRunner(scheduler); });
	return 0;
}

static void StopThread(void)
{
	return;
}
#endif /* linux */

#ifdef linux
static int32_t NepDriversInit(void)
{
	return 0;
}
#else /* linux */
static int32_t NepDriversInit(void)
{
	NestedRingTask *task = NepTaskGet(kNepSystemTaskGroup, kNepDmaAbortTask);
	if (!task) {
		pr_err("Failed to get dma abort task\n");
		return -EINVAL;
	}

	using ::noa::driver::dma::TemplatedDma;
	using noa::module::device_mgmt::DeviceManager;
	auto *dma = DeviceManager::Instance()->GetDevice<TemplatedDma>();
	NestedRingTaskSetup(task, dma, TemplatedDma::DmaAbortTaskRun);
	dma->RegisterAbortSchedulerAndTask(
		[](void *task) {
			NestedRingTaskQueueToScheduler(reinterpret_cast<NestedRingTask *>(task));
		},
		task);

	return 0;
}
#endif /* linux */

int32_t NepNestedRingServiceSetup(void)
{
	int32_t ret;
	NepBitmapTaskScheduler *scheduler = &g_scheduler;
	NestedRingTask *cache_engine_post_complete_task = NULL;
	NestedRingTask *buffer_pool_engine_post_complete_task = NULL;
	NestedRingTask *fetch_header_engine_post_complete_task = NULL;
	NestedRingTask *route_engine_post_complete_task = NULL;

	ret = RingServiceDoorbellNotifierInitAll();
	if (ret) {
		pr_err("Failed to init doorbell notifier, ret: %" PRId32 "\n", ret);
		goto out;
	}

	RingServiceIsrHandlerInitAll(scheduler);
	NepTaskInitAll();
	NepBitmapTaskSchedulerInit(scheduler, kMaxNepTaskGroupNum, NepTaskGroupBitmapGet(),
				   NepTaskGroupsGet());

	ret = NepDriversInit();
	if (ret) {
		pr_err("Failed to init NEP drivers, ret %" PRId32 "\n", ret);
		goto out;
	}

	cache_engine_post_complete_task =
		NepTaskGet(kNepSystemTaskGroup, kNepCacheDescCompleteTask);
	if (!cache_engine_post_complete_task) {
		pr_err("Failed to get cache descriptor engine complete task\n");
		ret = -EINVAL;
		goto out;
	}

	ret = NestedRingCacheEngineInit(NestedRingCacheEngineSingletonGet(), scheduler,
					cache_engine_post_complete_task);
	if (ret) {
		pr_err("Failed to init ring data cache engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	buffer_pool_engine_post_complete_task =
		NepTaskGet(kNepSystemTaskGroup, kNepCacheBufferPoolCompleteTask);
	if (!buffer_pool_engine_post_complete_task) {
		pr_err("Failed to get buffer pool engine complete task\n");
		ret = -EINVAL;
		goto out;
	}

	ret = NepBufferPoolCacheEngineInit(NepBufferPoolCacheEngineSingletonGet(), scheduler,
					   buffer_pool_engine_post_complete_task);
	if (ret) {
		pr_err("Failed to init buffer pool engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	fetch_header_engine_post_complete_task =
		NepTaskGet(kNepSystemTaskGroup, kNepFetchHeaderCompleteTask);
	if (!fetch_header_engine_post_complete_task) {
		pr_err("Failed to get fetch header engine complete task\n");
		ret = -EINVAL;
		goto out;
	}

	ret = NestedRingFetchHeaderEngineInit(NestedRingFetchHeaderEngineSingletonGet(), scheduler,
					      fetch_header_engine_post_complete_task);
	if (ret) {
		pr_err("Failed to init fetch header engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	route_engine_post_complete_task =
		NepTaskGet(kNepSystemTaskGroup, kNepRouteDataCompleteTask);
	if (!route_engine_post_complete_task) {
		pr_err("Failed to get route data engine complete task\n");
		ret = -EINVAL;
		goto out;
	}

	ret = NestedRingRouteEngineInit(NestedRingRouteEngineSingletonGet(), scheduler,
					route_engine_post_complete_task);
	if (ret) {
		pr_err("Failed to init ring data route engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NestedRingVpnEngineSetup(scheduler);
	if (ret) {
		pr_err("Failed to init VPN engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepWlanBufferPoolSetup();
	if (ret) {
		pr_err("Failed to setup WLAN buffer pool path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepWlanDeviceToHostPathSetup();
	if (ret) {
		pr_err("Failed to setup WLAN device to host path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepWlanHostToDevicePathSetup();
	if (ret) {
		pr_err("Failed to setup WLAN host to device path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepModemBufferPoolSetup();
	if (ret) {
		pr_err("Failed to setup modem buffer pool path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepModemDeviceToHostPathSetup();
	if (ret) {
		pr_err("Failed to setup modem device to host path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepModemHostToDevicePathSetup();
	if (ret) {
		pr_err("Failed to setup modem host to device path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	// Enable ISR handler only after data path is set up to prevent race conditions.
	ret = RingServiceIsrHandlerEnableAll();
	if (ret) {
		pr_err("Failed to enable ring service ISR handler, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = RunThread(scheduler);
	if (ret) {
		pr_err("Failed to run the thread, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = 0;
	pr_info("Ring Service: Enable nested ring service mode\n");

out:
	if (ret) {
		NepNestedRingServiceExit();
	}
	return ret;
}

void NepNestedRingServiceExit(void)
{
	NepBitmapTaskScheduler *scheduler = &g_scheduler;
	scheduler->should_stop = true;
	StopThread();
}
