// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Network Pipeline Service
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "network_pipeline_service/service.h"

#include <linux/printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kthread.h>

#include "common/compiler.h"

#include "vpn_pipeline_service.h"
#include "ppf_pipeline_service.h"
#include "tethering_pipeline_service.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_module/buffer_pool_engine.h"
#include "network_pipeline_module/dma_engine.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_pipeline_service/service.h"
#else /* linux */
#include "network_pipeline_service/service.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/compiler.h"
#include "device_mgmt/manager.h"
#include "dma/dma.h"
#include "linux_port/log.h"
#include "net/ppf_pipeline_service.h"
#include "net/tethering_pipeline_service.h"
#include "net/vpn_pipeline_service.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_module/buffer_pool_engine.h"
#include "network_pipeline_module/dma_engine.h"
#include "pw_thread/detached_thread.h"
#include "pw_thread_options_utils/utils.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_pipeline_service/service.h"
#endif /* linux */

#ifdef linux
#define PACKET_TABLE_SIZE (512U)
#else /* linux */
#define PACKET_TABLE_SIZE (16U)
#endif /* linux */

#define NUM_OF_DMA_CHANNEL (8U)

SEC_FAST_DATA static struct NepTaskScheduler g_scheduler;
INIT_NEP_PACKET_TABLE_WITH_PREFIX(SEC_FAST_DATA static, packet_table, PACKET_TABLE_SIZE);
SEC_FAST_DATA static unsigned long g_recycle_bitmap[BITS_TO_LONGS(PACKET_TABLE_SIZE)];
SEC_FAST_DATA static struct RingServiceObsoletePacketHandler g_obsolete_packet_handler;

static bool IsRxPath(const struct NepPacketContext *packet)
{
	(void)packet;
	// Packets that are neither feedthrough nor in the host-to-device
	// direction must be processed by the tethering stage.
	return true;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(rx_path, IsRxPath, NULL, NULL);

static bool IsTxPath(const struct NepPacketContext *packet)
{
	return NoaIsHostToDevicePath(packet->src);
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(tx_path, IsTxPath, NULL, &rx_path);

static bool IsFeedthroughPath(const struct NepPacketContext *packet)
{
	return ((const struct noa_desc *)&packet->noa_desc[0])->mode != NOAD_MODE_DATA;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(feedthrough_path, IsFeedthroughPath, NULL,
						  &tx_path);

SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_CHAIN_ROUTER(service_path_router, &feedthrough_path);

static int32_t SetupTxServicePath(struct NepTaskScheduler *scheduler)
{
	int32_t ret;
	struct NepStage **fallback_stage;

	fallback_stage = &tx_path.next_stage;

	*fallback_stage = NepVpnTxStageSingletonGet();

	ret = NepVpnTxPathSetup(RingServiceSenderEngineGet(), scheduler, &fallback_stage);
	if (ret) {
		pr_err("Failed to setup VPN TX path, ret: %" PRId32 "\n", ret);
		return ret;
	}

	// This path must be the last one.
	*fallback_stage = RingServiceTxFallbackStageSingletonGet();
	return 0;
}

static int32_t SetupRxServicePath(struct NepTaskScheduler *scheduler)
{
	int32_t ret;
	struct NepStage **fallback_stage;
	fallback_stage = &rx_path.next_stage;
	(void)ret;

	*fallback_stage = NepTetheringStageSingletonGet();

	ret = NepTetheringPathSetup(RingServiceSenderEngineGet(), scheduler, &fallback_stage);
	if (ret) {
		pr_err("Failed to setup tethering path, ret: %" PRId32 "\n", ret);
		return ret;
	}

	*fallback_stage = NepVpnRxStageSingletonGet();

	ret = NepVpnRxPathSetup(RingServiceSenderEngineGet(), scheduler, &fallback_stage);
	if (ret) {
		pr_err("Failed to setup VPN RX path, ret: %" PRId32 "\n", ret);
		return ret;
	}

#ifdef linux
	*fallback_stage = NepPpfStageSingletonGet();
 	ret = NepPpfPathSetup(RingServiceSenderEngineGet(), scheduler, &fallback_stage);
	if (ret) {
		pr_err("Failed to setup PPF RX path, ret: %" PRId32 "\n", ret);
		return ret;
	}
#endif /* linux */

	// This path must be the last one.
	*fallback_stage = RingServiceRxFallbackStageSingletonGet();
	return 0;
}

#ifdef linux
struct task_struct *thread = NULL;

static int32_t RunThread(struct NepTaskScheduler *scheduler)
{
	int32_t ret;

	thread = kthread_run(NepTaskSchedulerRunner, scheduler, "network pipeline service");
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		pr_err("Failed to init ring service thread, err: %d\n", ret);
		thread = NULL;
		return ret;
	}
	return 0;
}

static void StopThread(void)
{
	if (thread) {
		kthread_stop(thread);
		thread = NULL;
	}
}
#else /* linux */
static constexpr size_t kPipelineServiceThreadStackSizeWords = (4 * 1024 / sizeof(uint32_t));
static constexpr uint32_t kPipelineServiceThreadPriority =
	::noa::module::thread_utils::GetThreadMaxPriority();
DECLARE_THREAD_OPTIONS_EX(PipelineService, kPipelineServiceThreadStackSizeWords,
			  kPipelineServiceThreadPriority)

static int32_t RunThread(struct NepTaskScheduler *scheduler)
{
	pw::thread::DetachedThread(GetThreadOptionsPipelineService(),
				   [scheduler]() { NepTaskSchedulerRunner(scheduler); });
	return 0;
}

static void StopThread(void)
{
	return;
}
#endif /* linux */

int32_t NetworkPipelineServiceSetup(void)
{
	void *dma_driver = NULL;
	int32_t ret;
	struct NoaRingManagerInfoRoot *ring_root = NoaRingManagerRootInstance();
	struct NepTaskScheduler *scheduler = &g_scheduler;
	struct RingServiceObsoletePacketHandler *obsolete_packet_handler =
		&g_obsolete_packet_handler;

	if (!ring_root) {
		pr_err("Failed to get ring manager\n");
		return -EINVAL;
	}

	NepPacketTableInit(&packet_table);
	InitNepTaskScheduler(scheduler, &packet_table);
	RingServiceObsoletePacketHandlerSetup(obsolete_packet_handler, scheduler, PACKET_TABLE_SIZE,
					      g_recycle_bitmap);
	packet_table.buffer_recycle_handler = obsolete_packet_handler;
	packet_table.buffer_recycle = RingServiceObsoletePacketRecycle;

	ret = RingServiceSenderSetup(scheduler, ring_root);
	if (ret) {
		pr_err("Failed to setup ring sender, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepBufferPoolEngineInit(NepBufferPoolEngineSingletonGet(), scheduler);
	if (ret) {
		pr_err("Failed to setup buffer pool engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = NepDmaEngineSetup(NepDmaEngineSingletonGet(), NUM_OF_DMA_CHANNEL, dma_driver,
				scheduler);
	if (ret) {
		pr_err("Failed to setup dma engine, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = RingServiceFeedthroughStageSetup(RingServiceSenderEngineGet(), scheduler);
	if (ret) {
		pr_err("Failed to setup feedthrough stage, ret: %" PRId32 "\n", ret);
		goto out;
	}
	feedthrough_path.next_stage = RingServiceFeedthroughStageSingletonGet();

	ret = RingServiceFallbackStageSetup(RingServiceSenderEngineGet(), scheduler);
	if (ret) {
		pr_err("Failed to setup fallback stage, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = SetupTxServicePath(scheduler);
	if (ret) {
		pr_err("Failed to setup TX service path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = SetupRxServicePath(scheduler);
	if (ret) {
		pr_err("Failed to setup RX service path, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = RingServiceReceiverSetup(scheduler, ring_root, &service_path_router.basic);
	if (ret) {
		pr_err("Failed to setup ring receiver, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = RunThread(scheduler);
	if (ret) {
		pr_err("Failed to run the thread, ret: %" PRId32 "\n", ret);
		goto out;
	}

	ret = 0;
	pr_info("Ring Service: Enable pipeline service mode\n");

out:
	if (ret) {
		NetworkPipelineServiceExit();
	}
	return ret;
}

void NetworkPipelineServiceExit(void)
{
	struct NepTaskScheduler *scheduler = &g_scheduler;
	scheduler->should_stop = true;
	StopThread();
}
