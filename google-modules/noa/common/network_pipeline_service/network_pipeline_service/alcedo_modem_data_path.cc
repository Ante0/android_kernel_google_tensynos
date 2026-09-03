// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Network Pipeline Service With Nested Ring.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "network_pipeline_service/modem_data_path.h"

#include <linux/printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/core.h"
#include "common/compiler.h"
#include "common/inttypes.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "network_pipeline_service/task_manager.h"
#include "network_pipeline_service/ring_utils.h"
#include "tethering_pipeline_service.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_data_cache.h"
#include "ring_pipeline_service/ring_data_route.h"
#include "ring_pipeline_service/ring_isr_handler.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#include "ring_pipeline_service/packet_header_fetch.h"
#include "network_pipeline_service/option.h"
#else /* linux */
#include "network_pipeline_service_private/modem_data_path.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/core.h"
#include "common/compiler.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "dma/templated_dma.h"
#include "device_mgmt/manager.h"
#include "linux_port/log.h"
#include "network_pipeline_service/task_manager.h"
#include "network_pipeline_service_private/ring_utils.h"
#include "network_pipeline_service_private/option.h"
#include "net/tethering_pipeline_service.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_data_cache.h"
#include "ring_pipeline_service/ring_data_route.h"
#include "ring_pipeline_service/ring_isr_handler.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#include "ring_pipeline_service/packet_header_fetch.h"
#endif /* linux */

#ifndef linux
using noa::driver::dma::TemplatedDma;
using noa::module::device_mgmt::DeviceManager;
#endif /* linux */

int32_t NepModemBufferPoolSetup(void)
{
	const uint8_t src_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool);
	SEC_FAST_DATA static char cached_ring_buf[NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN *
						  NEP_BUFFER_POOL_CACHED_RING_SIZE];
	NepBufferPoolContext *context =
		NepBufferPoolContextSingletonGet(NOA_RING_SERVICE_BUFFER_POOL_MODEM);
	SEC_FAST_DATA static uint16_t cached_ring_head;
	SEC_FAST_DATA static uint16_t cached_ring_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_head;
	SEC_FAST_DATA static uintptr_t shadow_ring_tail;
	int32_t ret;
	NestedCachedRing cached_ring;
	NestedShadowRing shadow_ring;
	NepBufferPoolCacheStageSetupParams stage_params = {};
	NestedRingTask *task;
	struct ring_manager_instance *src_ring =
		NoaRingManagerInfoInstanceGetById(src_path_id, kNoaRingNepInput);
	const struct DataPathOptions options = GetDataPathOptions();

	NepRingRegisterResetFunction(src_ring);

	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0],
			     NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN, NEP_BUFFER_POOL_CACHED_RING_SIZE,
			     &cached_ring_head, &cached_ring_tail);

	// Set to 0 because the cached buffer pool does not utilize the shadow ring.
	NestedShadowRingInit(&shadow_ring, (uintptr_t)0, NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN,
			     NEP_BUFFER_POOL_CACHED_RING_SIZE, &shadow_ring_head,
			     &shadow_ring_tail);

	task = NepTaskGet(kNepCacheBufferPoolTaskGroup, kNepCacheModemNepBufferPoolPathTask);
	if (!task) {
		pr_err("Failed to get modem buffer pool cache stage task\n");
		return -EINVAL;
	}
	stage_params.name = "modem_buffer_pool";
	stage_params.task = task;
	stage_params.stage = &context->cache_stage;
	stage_params.shadow_ring_info = &shadow_ring;
	stage_params.cached_ring_info = &cached_ring;
	stage_params.engine = NepBufferPoolCacheEngineSingletonGet();
	stage_params.next_stage = NULL;
	stage_params.context = context;
	stage_params.path_id = src_path_id;
#ifdef linux
	stage_params.dma = NULL;
#else /* linux */
	stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NepBufferPoolCacheStageSetup(&stage_params);
	if (ret) {
		pr_err("Failed to setup buffer pool stage on modem path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &context->cache_stage);

	ret = RingServiceRegisterIsr(options.modem_d2h_isr_port,
				     options.modem_d2h_isr_buffer_pool_ring_id,
				     context->cache_stage.task);
	if (ret) {
		pr_err("Failed to register ISR for modem buffer pool path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}

	return 0;
}

#define D2H_CACHED_RING_SIZE (32U)
#define D2H_CACHED_RING_ITEM_LEN (NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE)
#define D2H_SHADOW_RING_ITEM_LEN (MAX_NEP_DEVICE_ENTRY_SIZE)
#define D2H_SHADOW_RING_BYTES (D2H_SHADOW_RING_ITEM_LEN * D2H_CACHED_RING_SIZE)
#define D2H_HEADER_RING_BYTES (NEP_HEADER_BUFFER_SIZE * D2H_CACHED_RING_SIZE)

// Contains all the static resources required for a single modem device-to-host data path.
typedef struct {
	char cached_ring_buf[D2H_CACHED_RING_ITEM_LEN * D2H_CACHED_RING_SIZE];
	__attribute__((aligned(D2H_SHADOW_RING_BYTES))) char shadow_ring_buf[D2H_SHADOW_RING_BYTES];
	__attribute__((aligned(D2H_HEADER_RING_BYTES))) char header_ring_buf[D2H_HEADER_RING_BYTES];
	NestedRingCacheContext cache_context;
	NestedRingFetchHeaderContext fetch_header_context;
	NestedRingRouteContext route_context;
	uint16_t cached_ring_head;
	uint16_t cached_ring_tail;
	uint16_t cached_ring_fetch_header_stage_tail;
	uint16_t cached_ring_tethering_stage_tail;
	uintptr_t shadow_ring_head;
	uintptr_t shadow_ring_tail;
	uintptr_t shadow_ring_fetch_header_stage_tail;
	uintptr_t shadow_ring_tethering_stage_tail;
	NestedRingStage cache_stage;
	NestedRingEngine cache_engine;
	NestedRingStage fetch_header_stage;
	NestedRingStage tethering_stage;
	NestedRingEngine tethering_engine;
	NestedRingStage route_stage;
	char cache_stage_name[16];
	char fetch_header_stage_name[16];
	char tethering_stage_name[16];
	char route_stage_name[16];
} ModemD2HPathResources;

SEC_FAST_DATA static ModemD2HPathResources g_d2h_path_resources[kNoaModemRingRxDataEnd];

// Helper function to set up a single device-to-host path for a given modem ring.
// It initializes all the necessary stages (cache, tethering, route) for the data path.
static int32_t SetupSingleModemDeviceToHostPath(enum NoaModemDeviceToHostRing ring_id,
						uint8_t cache_desc_task_id,
						uint8_t fetch_header_task_id,
						uint8_t tethering_task_id,
						uint8_t route_data_task_id, bool use_vendor_ring,
						const char *name_suffix, ModemD2HPathResources *res)
{
	const uint8_t src_path_id = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
							 kNoaNetworkFlowDeviceToHost, ring_id);
	int32_t ret;
	NestedCachedRing cached_ring;
	NestedShadowRing shadow_ring;
	NestedRingCacheStageSetupParams cache_stage_params = {};
	NestedRingFetchHeaderStageSetupParams fetch_header_stage_params = {};
	NestedRingTetheringStageSetupParams tethering_stage_params = {};
	NestedRingRouteStageSetupParams route_stage_params = {};
	NestedRingTask *task;
	struct ring_manager_instance *src_ring =
		NoaRingManagerInfoInstanceGetById(src_path_id, kNoaRingNepInput);
	const struct DataPathOptions options = GetDataPathOptions();

	if (!src_ring) {
		pr_err("Failed to get ring instance for path_id %u\n", src_path_id);
		return -EINVAL;
	}

	NepRingRegisterResetFunction(src_ring);

	/*************************** Cache Descriptor Stage ***************************/
	NestedCachedRingInit(&cached_ring, &res->cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &res->cached_ring_head, &res->cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&res->shadow_ring_buf[0],
			     D2H_SHADOW_RING_ITEM_LEN, D2H_CACHED_RING_SIZE, &res->shadow_ring_head,
			     &res->shadow_ring_tail);

	task = NepTaskGet(kNepCacheDataTaskGroup, cache_desc_task_id);
	if (!task) {
		pr_err("Failed to get modem device to host cache stage task\n");
		return -EINVAL;
	}
	snprintf(res->cache_stage_name, sizeof(res->cache_stage_name), "md_d2h_%s_cache",
		 name_suffix);
	cache_stage_params.name = res->cache_stage_name;
	cache_stage_params.is_dram_ring = true;
	if (options.is_ci_testing) {
		cache_stage_params.is_dram_ring = true;
	}
	cache_stage_params.task = task;
	cache_stage_params.stage = &res->cache_stage;
	cache_stage_params.shadow_ring_info = &shadow_ring;
	cache_stage_params.cached_ring_info = &cached_ring;
	if (cache_stage_params.is_dram_ring) {
		// If this is a DRAM ring, we use the DMA330's cache engine to read the data from DRAM.
		cache_stage_params.engine = NestedRingCacheEngineSingletonGet();
	} else {
		cache_stage_params.engine = &res->cache_engine;
	}
	cache_stage_params.next_stage = &res->fetch_header_stage;
	cache_stage_params.cache_context = &res->cache_context;
	cache_stage_params.path_id = src_path_id;
	cache_stage_params.fill_entry = NepFillDeviceEntry;
#ifdef linux
	cache_stage_params.dma = NULL;
#else /* linux */
	cache_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingCacheStageSetup(&cache_stage_params);
	if (ret) {
		pr_err("Failed to setup cache stage on modem d2h path for %s, ret %" PRId32 "\n",
		       name_suffix, ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &res->cache_stage);

	/****************************** Fetching Header Stage *******************************/
	NestedCachedRingInit(&cached_ring, &res->cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &res->cached_ring_head,
			     &res->cached_ring_fetch_header_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&res->shadow_ring_buf[0],
			     D2H_SHADOW_RING_ITEM_LEN, D2H_CACHED_RING_SIZE, &res->shadow_ring_head,
			     &res->shadow_ring_fetch_header_stage_tail);

	task = NepTaskGet(kNepFetchHeaderTaskGroup, fetch_header_task_id);
	if (!task) {
		pr_err("Failed to get modem fetch header stage task\n");
		return -EINVAL;
	}
	snprintf(res->fetch_header_stage_name, sizeof(res->fetch_header_stage_name), "md_%s_header",
		 name_suffix);
	fetch_header_stage_params.name = res->fetch_header_stage_name;
	fetch_header_stage_params.task = task;
	fetch_header_stage_params.stage = &res->fetch_header_stage;
	fetch_header_stage_params.shadow_ring_info = &shadow_ring;
	fetch_header_stage_params.cached_ring_info = &cached_ring;
	fetch_header_stage_params.engine = NestedRingFetchHeaderEngineSingletonGet();
	fetch_header_stage_params.next_stage = &res->tethering_stage;
	fetch_header_stage_params.fetch_header_context = &res->fetch_header_context;
#ifdef linux
	fetch_header_stage_params.dma = NULL;
#else /* linux */
	fetch_header_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */
	fetch_header_stage_params.header_buffer_ring_base = (uintptr_t)&res->header_ring_buf[0];
	fetch_header_stage_params.header_buffer_ring_size = D2H_CACHED_RING_SIZE;

	ret = NestedRingFetchHeaderStageSetup(&fetch_header_stage_params);
	if (ret) {
		pr_err("Failed to setup fetch header stage on modem path for %s, ret %" PRId32 "\n",
		       name_suffix, ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &res->fetch_header_stage);

	/****************************** Tethering Stage *******************************/
	NestedCachedRingInit(&cached_ring, &res->cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &res->cached_ring_fetch_header_stage_tail,
			     &res->cached_ring_tethering_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&res->shadow_ring_buf[0],
			     D2H_SHADOW_RING_ITEM_LEN, D2H_CACHED_RING_SIZE,
			     &res->shadow_ring_fetch_header_stage_tail,
			     &res->shadow_ring_tethering_stage_tail);

	task = NepTaskGet(kNepAnalyzeDataTaskGroup, tethering_task_id);
	if (!task) {
		pr_err("Failed to get modem tethering stage task\n");
		return -EINVAL;
	}
	snprintf(res->tethering_stage_name, sizeof(res->tethering_stage_name), "md_%s_tethering",
		 name_suffix);
	tethering_stage_params.name = res->tethering_stage_name;
	tethering_stage_params.task = task;
	tethering_stage_params.stage = &res->tethering_stage;
	tethering_stage_params.shadow_ring_info = &shadow_ring;
	tethering_stage_params.cached_ring_info = &cached_ring;
	tethering_stage_params.engine = &res->tethering_engine;
	tethering_stage_params.next_stage = &res->route_stage;

	ret = NestedRingTetheringStageSetup(&tethering_stage_params, false);
	if (ret) {
		pr_err("Failed to setup tethering stage on modem path for %s, ret %" PRId32 "\n",
		       name_suffix, ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &res->tethering_stage);

	/****************************** Route Data Stage ******************************/
	NestedCachedRingInit(&cached_ring, &res->cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &res->cached_ring_tethering_stage_tail,
			     &res->cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&res->shadow_ring_buf[0],
			     D2H_SHADOW_RING_ITEM_LEN, D2H_CACHED_RING_SIZE,
			     &res->shadow_ring_tethering_stage_tail, &res->shadow_ring_tail);

	task = NepTaskGet(kNepRouteDataTaskGroup, route_data_task_id);
	if (!task) {
		pr_err("Failed to get Modem device to host route stage task\n");
		return -EINVAL;
	}
	snprintf(res->route_stage_name, sizeof(res->route_stage_name), "md_d2h_%s_route",
		 name_suffix);
	route_stage_params.name = res->route_stage_name;
	route_stage_params.network_type = kNoaNetworkInterfaceModem;
	route_stage_params.from_device = true;
	route_stage_params.use_vendor_ring = use_vendor_ring;
	route_stage_params.support_forward_wlan = true;
	route_stage_params.wlan_buffer_pool_context =
		NepBufferPoolContextSingletonGet(NOA_RING_SERVICE_BUFFER_POOL_WLAN);
	route_stage_params.support_forward_modem = false;
	route_stage_params.support_vpn = false;
	route_stage_params.task = task;
	route_stage_params.stage = &res->route_stage;
	route_stage_params.shadow_ring_info = &shadow_ring;
	route_stage_params.cached_ring_info = &cached_ring;
	route_stage_params.engine = NestedRingRouteEngineSingletonGet();
	route_stage_params.next_stage = NULL;
	route_stage_params.route_context = &res->route_context;
	route_stage_params.src_path_id = route_stage_params.feedthrough_path_id = src_path_id;
	// The feedback is sent back to its origin for buffer recycling, so the direction is reversed.
	route_stage_params.feedback_path_id =
		NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				     NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT);
	route_stage_params.forward_wlan_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);
	route_stage_params.feedthrough_path_notifier =
		RingServiceDoorbellNotifierGet(options.modem_d2h_feedthrough_notifier_port);
	route_stage_params.feedback_path_notifier =
		RingServiceDoorbellNotifierGet(options.modem_d2h_feedback_notifier_port);
	route_stage_params.forward_wlan_path_notifier =
		RingServiceDoorbellNotifierGet(options.modem_d2h_forward_wlan_notifier_port);
#ifdef linux
	route_stage_params.dma = NULL;
#else /* linux */
	route_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingRouteStageSetup(&route_stage_params);
	if (ret) {
		pr_err("Failed to setup route stage on modem device to host path for %s, ret %" PRId32
		       "\n",
		       name_suffix, ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &res->route_stage);

	ret = RingServiceRegisterIsr(options.modem_d2h_isr_port,
				     ring_id + options.modem_d2h_isr_ring_id_offset,
				     res->cache_stage.task);
	if (ret) {
		pr_err("Failed to register ISR for modem device to host path for %s, ret %" PRId32
		       "\n",
		       name_suffix, ret);
		return -EINVAL;
	}

	return 0;
}

int32_t NepModemDeviceToHostPathSetup(void)
{
	int32_t ret;
	int i;
	struct {
		enum NoaModemDeviceToHostRing ring_id;
		uint8_t cache_desc_task_id;
		uint8_t fetch_header_task_id;
		uint8_t tethering_task_id;
		uint8_t route_data_task_id;
		bool use_vendor_ring;
		const char *name;
	} rings[kNoaModemRingRxDataEnd] = {
		{ kNoaModemRingRxData, kNepCacheModemDeviceToHostPathTask,
		  kNepFetchHeaderModemDeviceToHostPathTask, kNepAnalyzeModemPacketTask,
		  kNepRouteModemDeviceToHostPathTask, false, "rx_data" },
		{ kNoaModemRingRxq0, kNepCacheModemDeviceToHostRxq0PathTask,
		  kNepFetchHeaderModemDeviceToHostRxq0PathTask, kNepAnalyzeModemRxq0PacketTask,
		  kNepRouteModemDeviceToHostRxq0PathTask, true, "rxq0" },
		{ kNoaModemRingRxq1, kNepCacheModemDeviceToHostRxq1PathTask,
		  kNepFetchHeaderModemDeviceToHostRxq1PathTask, kNepAnalyzeModemRxq1PacketTask,
		  kNepRouteModemDeviceToHostRxq1PathTask, true, "rxq1" },
		{ kNoaModemRingRxq2, kNepCacheModemDeviceToHostRxq2PathTask,
		  kNepFetchHeaderModemDeviceToHostRxq2PathTask, kNepAnalyzeModemRxq2PacketTask,
		  kNepRouteModemDeviceToHostRxq2PathTask, true, "rxq2" },
	};

	// Each ring gets its own set of resources (tasks, stages) to operate independently.
	for (i = 0; i < kNoaModemRingRxDataEnd; i++) {
		ret = SetupSingleModemDeviceToHostPath(
			rings[i].ring_id, rings[i].cache_desc_task_id,
			rings[i].fetch_header_task_id, rings[i].tethering_task_id,
			rings[i].route_data_task_id, rings[i].use_vendor_ring, rings[i].name,
			&g_d2h_path_resources[i]);
		if (ret) {
			pr_err("Failed to setup modem D2H path for %s\n", rings[i].name);
			return ret;
		}
	}
	return 0;
}

#define H2D_CACHED_RING_SIZE (16U)
#define H2D_CACHED_RING_ITEM_LEN (NOA_DESC_MODEM_TX_MTK_BYTE)
#define H2D_SHADOW_RING_ITEM_LEN (32U)
#define H2D_SHADOW_RING_BYTES (H2D_SHADOW_RING_ITEM_LEN * H2D_CACHED_RING_SIZE)

int32_t NepModemHostToDevicePathSetup(void)
{
	const uint8_t src_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
	SEC_FAST_DATA static char cached_ring_buf[H2D_CACHED_RING_ITEM_LEN * H2D_CACHED_RING_SIZE];
	__attribute__((aligned(H2D_SHADOW_RING_BYTES))) SEC_FAST_DATA static char
		shadow_ring_buf[H2D_SHADOW_RING_BYTES];
	SEC_FAST_DATA static NestedRingCacheContext cache_context;
	SEC_FAST_DATA static NestedRingRouteContext route_context;
	SEC_FAST_DATA static uint16_t cached_ring_head;
	SEC_FAST_DATA static uint16_t cached_ring_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_head;
	SEC_FAST_DATA static uintptr_t shadow_ring_tail;
	SEC_FAST_DATA static NestedRingStage cache_stage;
	SEC_FAST_DATA static NestedRingStage route_stage;
	int32_t ret;
	NestedCachedRing cached_ring;
	NestedShadowRing shadow_ring;
	NestedRingCacheStageSetupParams cache_stage_params = {};
	NestedRingRouteStageSetupParams route_stage_params = {};
	NestedRingTask *task;
	struct ring_manager_instance *src_ring =
		NoaRingManagerInfoInstanceGetById(src_path_id, kNoaRingNepInput);
	const struct DataPathOptions options = GetDataPathOptions();

	NepRingRegisterResetFunction(src_ring);

	/*************************** Cache Descriptor Stage ***************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], H2D_CACHED_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &cached_ring_head, &cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], H2D_SHADOW_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &shadow_ring_head, &shadow_ring_tail);

	task = NepTaskGet(kNepCacheDataTaskGroup, kNepCacheModemHostToDevicePathTask);
	if (!task) {
		pr_err("Failed to get modem host to device cache stage task\n");
		return -EINVAL;
	}
	cache_stage_params.name = "modem_h2d_cache";
	cache_stage_params.is_dram_ring = true;
	if (options.is_ci_testing) {
		cache_stage_params.is_dram_ring = true;
	}
	cache_stage_params.task = task;
	cache_stage_params.stage = &cache_stage;
	cache_stage_params.shadow_ring_info = &shadow_ring;
	cache_stage_params.cached_ring_info = &cached_ring;
	if (cache_stage_params.is_dram_ring) {
		// If this is a DRAM ring, we use the DMA330's cache engine to read the data from DRAM.
		cache_stage_params.engine = NestedRingCacheEngineSingletonGet();
	} else {
		WARN_ON(1);
	}
	cache_stage_params.next_stage = &route_stage;
	cache_stage_params.cache_context = &cache_context;
	cache_stage_params.path_id = src_path_id;
	cache_stage_params.fill_entry = NepFillHostEntry;
#ifdef linux
	cache_stage_params.dma = NULL;
#else /* linux */
	cache_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingCacheStageSetup(&cache_stage_params);
	if (ret) {
		pr_err("Failed to setup cache stage on modem host to device path, ret %" PRId32
		       "\n",
		       ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &cache_stage);

	/****************************** Route Data Stage ******************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], H2D_CACHED_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &cached_ring_head, &cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], H2D_SHADOW_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &shadow_ring_head, &shadow_ring_tail);

	task = NepTaskGet(kNepRouteDataTaskGroup, kNepRouteModemHostToDevicePathTask);
	if (!task) {
		pr_err("Failed to get modem host to device route stage task\n");
		return -EINVAL;
	}
	route_stage_params.name = "modem_h2d_route";
	route_stage_params.network_type = kNoaNetworkInterfaceModem;
	route_stage_params.from_device = false;
	route_stage_params.support_forward_wlan = false;
	route_stage_params.support_forward_modem = false;
	route_stage_params.task = task;
	route_stage_params.stage = &route_stage;
	route_stage_params.shadow_ring_info = &shadow_ring;
	route_stage_params.cached_ring_info = &cached_ring;
	route_stage_params.engine = NestedRingRouteEngineSingletonGet();
	route_stage_params.next_stage = NULL;
	route_stage_params.route_context = &route_context;
	route_stage_params.src_path_id = route_stage_params.feedthrough_path_id = src_path_id;
	// The feedback is sent back to its origin for buffer recycling, so the direction is reversed.
	route_stage_params.feedback_path_id =
		NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				     NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT);
	route_stage_params.feedthrough_path_notifier =
		RingServiceDoorbellNotifierGet(options.modem_h2d_feedthrough_notifier_port);
	route_stage_params.feedback_path_notifier =
		RingServiceDoorbellNotifierGet(options.modem_h2d_feedback_notifier_port);
#ifdef linux
	route_stage_params.dma = NULL;
#else /* linux */
	route_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingRouteStageSetup(&route_stage_params);
	if (ret) {
		pr_err("Failed to setup route stage on modem host to device path, ret %" PRId32
		       "\n",
		       ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &route_stage);

	ret = RingServiceRegisterIsr(options.modem_h2d_isr_port, options.modem_h2d_isr_ring_id,
				     cache_stage.task);
	if (ret) {
		pr_err("Failed to register ISR for modem host to device path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}

	return 0;
}
