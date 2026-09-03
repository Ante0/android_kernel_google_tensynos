// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Network Pipeline Service With Nested Ring.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "network_pipeline_service/wlan_data_path.h"

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
#include "common/wlan/noa_wlan.h"
#include "network_pipeline_service/task_manager.h"
#include "network_pipeline_service/ring_utils.h"
#include "tethering_pipeline_service.h"
#include "vpn_pipeline_service.h"
#include "ppf_pipeline_service.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_data_cache.h"
#include "ring_pipeline_service/ring_data_route.h"
#include "ring_pipeline_service/ring_isr_handler.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#include "ring_pipeline_service/packet_header_fetch.h"
#include "network_pipeline_service/option.h"
#else /* linux */
#include "network_pipeline_service_private/wlan_data_path.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/core.h"
#include "common/compiler.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/wlan/noa_wlan.h"
#include "dma/templated_dma.h"
#include "device_mgmt/manager.h"
#include "linux_port/log.h"
#include "network_pipeline_service/task_manager.h"
#include "network_pipeline_service_private/ring_utils.h"
#include "network_pipeline_service_private/option.h"
#include "net/tethering_pipeline_service.h"
#include "net/vpn_pipeline_service.h"
#include "net/ppf_pipeline_service.h"
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

int32_t NepWlanBufferPoolSetup(void)
{
	const uint8_t src_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool);
	SEC_FAST_DATA static char cached_ring_buf[NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN *
						  NEP_BUFFER_POOL_CACHED_RING_SIZE];
	NepBufferPoolContext *context =
		NepBufferPoolContextSingletonGet(NOA_RING_SERVICE_BUFFER_POOL_WLAN);
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

	task = NepTaskGet(kNepCacheBufferPoolTaskGroup, kNepCacheWlanNepBufferPoolPathTask);
	if (!task) {
		pr_err("Failed to get wlan buffer pool cache stage task\n");
		return -EINVAL;
	}
	stage_params.name = "wlan_buffer_pool";
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
		pr_err("Failed to setup buffer pool stage on wlan path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &context->cache_stage);

	ret = RingServiceRegisterIsr(options.wlan_d2h_isr_port,
				     options.wlan_d2h_isr_buffer_pool_ring_id,
				     context->cache_stage.task);
	if (ret) {
		pr_err("Failed to register ISR for wlan buffer pool path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}

	return 0;
}

#define D2H_CACHED_RING_SIZE (32U)
// Set the cache ring size to the maximum byte size required
// by both brcm and qcm vendors to prevent buffer overflow.
#define D2H_CACHED_RING_ITEM_LEN (NOA_DESC_MAX_BYTE)
#define D2H_SHADOW_RING_ITEM_LEN (MAX_NEP_DEVICE_ENTRY_SIZE)
#define D2H_SHADOW_RING_BYTES (D2H_SHADOW_RING_ITEM_LEN * D2H_CACHED_RING_SIZE)
#define D2H_HEADER_RING_BYTES (NEP_HEADER_BUFFER_SIZE * D2H_CACHED_RING_SIZE)

static void FormatVpnRxDescriptor(const void *src_desc, const nep_device_entry *entry,
				  void *vpn_desc)
{
	const struct noa_desc *src = (const struct noa_desc *)src_desc;
	struct noa_desc *dst = (struct noa_desc *)vpn_desc;
	struct noa_rx_ipsec_metadata *ipsec_metadata =
		(struct noa_rx_ipsec_metadata *)&dst->ext_data[0];
	memcpy(dst, src, noa_desc_bytes(src->desc_type));
	dst->reason = FWD_REASON_VPN;
	dst->desc_type = NOA_DESC_VPN_RX;
	// Since VPN processes packets in-place, the packet length must include the original head_offset.
	dst->dl = entry->pkt_info.packet_length + src->head_offset;
	memcpy(ipsec_metadata, &entry->ipsec_metadata, sizeof(struct noa_rx_ipsec_metadata));
}

int32_t NepWlanDeviceToHostPathSetup(void)
{
	const uint8_t src_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);
	SEC_FAST_DATA static char cached_ring_buf[D2H_CACHED_RING_ITEM_LEN * D2H_CACHED_RING_SIZE];
	__attribute__((aligned(D2H_SHADOW_RING_BYTES))) SEC_FAST_DATA static char
		shadow_ring_buf[D2H_SHADOW_RING_BYTES];
	__attribute__((aligned(D2H_HEADER_RING_BYTES))) SEC_FAST_DATA static char
		header_ring_buf[D2H_HEADER_RING_BYTES];
	SEC_FAST_DATA static NestedRingCacheContext cache_context;
	SEC_FAST_DATA static NestedRingFetchHeaderContext fetch_header_context;
	SEC_FAST_DATA static NestedRingRouteContext route_context;
	SEC_FAST_DATA static uint16_t cached_ring_head;
	SEC_FAST_DATA static uint16_t cached_ring_tail;
	SEC_FAST_DATA static uint16_t cached_ring_fetch_header_stage_tail;
	SEC_FAST_DATA static uint16_t cached_ring_tethering_stage_tail;
	SEC_FAST_DATA static uint16_t cached_ring_vpn_stage_tail;
	SEC_FAST_DATA static uint16_t cached_ring_ppf_stage_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_head;
	SEC_FAST_DATA static uintptr_t shadow_ring_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_fetch_header_stage_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_tethering_stage_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_vpn_stage_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_ppf_stage_tail;
	SEC_FAST_DATA static NestedRingStage cache_stage;
	SEC_FAST_DATA static NestedRingEngine cache_engine;
	SEC_FAST_DATA static NestedRingStage fetch_header_stage;
	SEC_FAST_DATA static NestedRingStage tethering_stage;
	SEC_FAST_DATA static NestedRingEngine tethering_engine;
	SEC_FAST_DATA static NestedRingStage vpn_stage;
	SEC_FAST_DATA static NestedRingEngine vpn_engine;
	SEC_FAST_DATA static NestedRingStage ppf_stage;
	SEC_FAST_DATA static NestedRingEngine ppf_engine;
	SEC_FAST_DATA static NestedRingStage route_stage;
	int32_t ret;
	NestedCachedRing cached_ring;
	NestedShadowRing shadow_ring;
	NestedRingCacheStageSetupParams cache_stage_params = {};
	NestedRingFetchHeaderStageSetupParams fetch_header_stage_params = {};
	NestedRingTetheringStageSetupParams tethering_stage_params = {};
	NestedRingVpnStageSetupParams vpn_stage_params = {};
	NestedRingPpfStageSetupParams ppf_stage_params = {};
	NestedRingRouteStageSetupParams route_stage_params = {};
	NestedRingTask *task;
	struct ring_manager_instance *src_ring =
		NoaRingManagerInfoInstanceGetById(src_path_id, kNoaRingNepInput);
	const struct DataPathOptions options = GetDataPathOptions();

	NepRingRegisterResetFunction(src_ring);

	/*************************** Cache Descriptor Stage ***************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &cached_ring_head, &cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], D2H_SHADOW_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &shadow_ring_head, &shadow_ring_tail);

	task = NepTaskGet(kNepCacheDataTaskGroup, kNepCacheWlanDeviceToHostPathTask);
	if (!task) {
		pr_err("Failed to get wlan device to host cache stage task\n");
		return -EINVAL;
	}
	cache_stage_params.name = "wlan_d2h_cache";
	// The Wi-Fi device-to-host path currently uses an SRAM ring buffer.
	cache_stage_params.is_dram_ring = false;
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
		cache_stage_params.engine = &cache_engine;
	}
	cache_stage_params.next_stage = &fetch_header_stage;
	cache_stage_params.cache_context = &cache_context;
	cache_stage_params.path_id = src_path_id;
	cache_stage_params.fill_entry = NepFillWifiDeviceEntry;
#ifdef linux
	cache_stage_params.dma = NULL;
#else /* linux */
	cache_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingCacheStageSetup(&cache_stage_params);
	if (ret) {
		pr_err("Failed to setup cache stage on wlan device to host path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &cache_stage);

	/****************************** Fetching Header Stage *******************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &cached_ring_head,
			     &cached_ring_fetch_header_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], D2H_SHADOW_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &shadow_ring_head,
			     &shadow_ring_fetch_header_stage_tail);

	task = NepTaskGet(kNepFetchHeaderTaskGroup, kNepFetchHeaderWlanDeviceToHostPathTask);
	if (!task) {
		pr_err("Failed to get wlan fetch header stage task\n");
		return -EINVAL;
	}
	fetch_header_stage_params.name = "wlan_header";
	fetch_header_stage_params.task = task;
	fetch_header_stage_params.stage = &fetch_header_stage;
	fetch_header_stage_params.shadow_ring_info = &shadow_ring;
	fetch_header_stage_params.cached_ring_info = &cached_ring;
	fetch_header_stage_params.engine = NestedRingFetchHeaderEngineSingletonGet();
	fetch_header_stage_params.next_stage = &tethering_stage;
	fetch_header_stage_params.fetch_header_context = &fetch_header_context;
#ifdef linux
	fetch_header_stage_params.dma = NULL;
#else /* linux */
	fetch_header_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */
	fetch_header_stage_params.header_buffer_ring_base = (uintptr_t)&header_ring_buf[0];
	fetch_header_stage_params.header_buffer_ring_size = D2H_CACHED_RING_SIZE;

	ret = NestedRingFetchHeaderStageSetup(&fetch_header_stage_params);
	if (ret) {
		pr_err("Failed to setup fetch header stage on wlan path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &fetch_header_stage);

	/****************************** Tethering Stage *******************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &cached_ring_fetch_header_stage_tail,
			     &cached_ring_tethering_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], D2H_SHADOW_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &shadow_ring_fetch_header_stage_tail,
			     &shadow_ring_tethering_stage_tail);

	task = NepTaskGet(kNepAnalyzeDataTaskGroup, kNepAnalyzeWlanPacketTask);
	if (!task) {
		pr_err("Failed to get wlan tethering stage task\n");
		return -EINVAL;
	}
	tethering_stage_params.name = "wlan_tethering";
	tethering_stage_params.task = task;
	tethering_stage_params.stage = &tethering_stage;
	tethering_stage_params.shadow_ring_info = &shadow_ring;
	tethering_stage_params.cached_ring_info = &cached_ring;
	tethering_stage_params.engine = &tethering_engine;
	tethering_stage_params.next_stage = &vpn_stage;

	ret = NestedRingTetheringStageSetup(&tethering_stage_params, true);
	if (ret) {
		pr_err("Failed to setup tethering stage on wlan path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &tethering_stage);

	/********************************* VPN Stage **********************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &cached_ring_tethering_stage_tail,
			     &cached_ring_vpn_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], D2H_SHADOW_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &shadow_ring_tethering_stage_tail,
			     &shadow_ring_vpn_stage_tail);

	task = NepTaskGet(kNepAnalyzeDataTaskGroup, kNepVpnWlanRxPacketTask);
	if (!task) {
		pr_err("Failed to get wlan VPN RX stage task\n");
		return -EINVAL;
	}
	vpn_stage_params.name = "wlan_vpn_rx";
	vpn_stage_params.is_wlan = true;
	vpn_stage_params.is_tx_path = false;
	vpn_stage_params.task = task;
	vpn_stage_params.stage = &vpn_stage;
	vpn_stage_params.shadow_ring_info = &shadow_ring;
	vpn_stage_params.cached_ring_info = &cached_ring;
	vpn_stage_params.engine = &vpn_engine;
	vpn_stage_params.next_stage = &ppf_stage;

	ret = NestedRingVpnStageSetup(&vpn_stage_params);
	if (ret) {
		pr_err("Failed to setup VPN RX stage on wlan path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &vpn_stage);

	/********************************* PPF Stage **********************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &cached_ring_vpn_stage_tail,
			     &cached_ring_ppf_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], D2H_SHADOW_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &shadow_ring_vpn_stage_tail,
			     &shadow_ring_ppf_stage_tail);

	task = NepTaskGet(kNepAnalyzeDataTaskGroup, kNepInspectWlanPpfPacketTask);
	if (!task) {
		pr_err("Failed to get wlan PPF stage task\n");
		return -EINVAL;
	}
	ppf_stage_params.name = "wlan_ppf";
	ppf_stage_params.task = task;
	ppf_stage_params.stage = &ppf_stage;
	ppf_stage_params.shadow_ring_info = &shadow_ring;
	ppf_stage_params.cached_ring_info = &cached_ring;
	ppf_stage_params.engine = &ppf_engine;
	ppf_stage_params.next_stage = &route_stage;

	ret = NestedRingPpfStageSetup(&ppf_stage_params);
	if (ret) {
		pr_err("Failed to setup PPF stage on wlan path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &ppf_stage);

	/****************************** Route Data Stage ******************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], D2H_CACHED_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &cached_ring_ppf_stage_tail, &cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], D2H_SHADOW_RING_ITEM_LEN,
			     D2H_CACHED_RING_SIZE, &shadow_ring_ppf_stage_tail, &shadow_ring_tail);

	task = NepTaskGet(kNepRouteDataTaskGroup, kNepRouteWlanDeviceToHostPathTask);
	if (!task) {
		pr_err("Failed to get wlan device to host route stage task\n");
		return -EINVAL;
	}
	route_stage_params.name = "wlan_d2h_route";
	route_stage_params.network_type = kNoaNetworkInterfaceWlan;
	route_stage_params.from_device = true;
	route_stage_params.use_vendor_ring = false;
	route_stage_params.support_forward_wlan = true;
	route_stage_params.wlan_buffer_pool_context =
		NepBufferPoolContextSingletonGet(NOA_RING_SERVICE_BUFFER_POOL_WLAN);
	route_stage_params.support_forward_modem = true;
	route_stage_params.modem_buffer_pool_context =
		NepBufferPoolContextSingletonGet(NOA_RING_SERVICE_BUFFER_POOL_MODEM);
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
		NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
				     NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT);
	route_stage_params.forward_wlan_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);
	route_stage_params.forward_modem_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
	route_stage_params.feedthrough_path_notifier =
		RingServiceDoorbellNotifierGet(options.wlan_d2h_feedthrough_notifier_port);
	route_stage_params.feedback_path_notifier =
		RingServiceDoorbellNotifierGet(options.wlan_d2h_feedback_notifier_port);
	route_stage_params.forward_wlan_path_notifier =
		RingServiceDoorbellNotifierGet(options.wlan_d2h_forward_wlan_notifier_port);
	route_stage_params.forward_modem_path_notifier =
		RingServiceDoorbellNotifierGet(options.wlan_d2h_forward_modem_notifier_port);
	route_stage_params.support_vpn = true;
	route_stage_params.vpn_path_id = route_stage_params.feedthrough_path_id;
	route_stage_params.vpn_path_notifier = route_stage_params.feedthrough_path_notifier;
	route_stage_params.format_vpn_rx_desc = FormatVpnRxDescriptor;
#ifdef linux
	route_stage_params.dma = NULL;
#else /* linux */
	route_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingRouteStageSetup(&route_stage_params);
	if (ret) {
		pr_err("Failed to setup route stage on wlan device to host path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &route_stage);

	ret = RingServiceRegisterIsr(options.wlan_d2h_isr_port, options.wlan_d2h_isr_ring_id,
				     cache_stage.task);
	if (ret) {
		pr_err("Failed to register ISR for wlan device to host path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}

	return 0;
}

#define H2D_CACHED_RING_SIZE (32U)
#define H2D_CACHED_RING_ITEM_LEN (NOA_DESC_MAX_BYTE)
#define H2D_SHADOW_RING_ITEM_LEN (32U)
#define H2D_SHADOW_RING_BYTES (H2D_SHADOW_RING_ITEM_LEN * H2D_CACHED_RING_SIZE)

static void FillHostEntry(void *desc, void *entry)
{
	struct noa_desc *noa_desc = (struct noa_desc *)desc;
	if (noa_desc->desc_type == NOA_DESC_WLAN_TX_BRCM) {
		((nep_host_entry *)entry)->ipsec_metadata =
			((struct noa_bcm_txd *)noa_desc->ext_data)->ipsec_metadata;
	}
	NepFillHostEntry(desc, entry);
}

int32_t NepWlanHostToDevicePathSetup(void)
{
	const uint8_t src_path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);
	SEC_FAST_DATA static char cached_ring_buf[H2D_CACHED_RING_ITEM_LEN * H2D_CACHED_RING_SIZE];
	__attribute__((aligned(H2D_SHADOW_RING_BYTES))) SEC_FAST_DATA static char
		shadow_ring_buf[H2D_SHADOW_RING_BYTES];
	SEC_FAST_DATA static NestedRingCacheContext cache_context;
	SEC_FAST_DATA static NestedRingRouteContext route_context;
	SEC_FAST_DATA static uint16_t cached_ring_head;
	SEC_FAST_DATA static uint16_t cached_ring_vpn_stage_tail;
	SEC_FAST_DATA static uint16_t cached_ring_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_head;
	SEC_FAST_DATA static uintptr_t shadow_ring_vpn_stage_tail;
	SEC_FAST_DATA static uintptr_t shadow_ring_tail;
	SEC_FAST_DATA static NestedRingStage cache_stage;
	SEC_FAST_DATA static NestedRingStage vpn_stage;
	SEC_FAST_DATA static NestedRingEngine vpn_engine;
	SEC_FAST_DATA static NestedRingStage route_stage;
	int32_t ret;
	NestedCachedRing cached_ring;
	NestedShadowRing shadow_ring;
	NestedRingCacheStageSetupParams cache_stage_params = {};
	NestedRingVpnStageSetupParams vpn_stage_params = {};
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

	task = NepTaskGet(kNepCacheDataTaskGroup, kNepCacheWlanHostToDevicePathTask);
	if (!task) {
		pr_err("Failed to get wlan host to device cache stage task\n");
		return -EINVAL;
	}
	cache_stage_params.name = "wlan_h2d_cache";
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
	cache_stage_params.next_stage = &vpn_stage;
	cache_stage_params.cache_context = &cache_context;
	cache_stage_params.path_id = src_path_id;
	cache_stage_params.fill_entry = FillHostEntry;
#ifdef linux
	cache_stage_params.dma = NULL;
#else /* linux */
	cache_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingCacheStageSetup(&cache_stage_params);
	if (ret) {
		pr_err("Failed to setup cache stage on wlan host to device path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &cache_stage);

	/********************************* VPN Stage **********************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], H2D_CACHED_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &cached_ring_head, &cached_ring_vpn_stage_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], H2D_SHADOW_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &shadow_ring_head, &shadow_ring_vpn_stage_tail);

	task = NepTaskGet(kNepAnalyzeDataTaskGroup, kNepVpnWlanTxPacketTask);
	if (!task) {
		pr_err("Failed to get wlan VPN TX stage task\n");
		return -EINVAL;
	}
	vpn_stage_params.name = "wlan_vpn_tx";
	vpn_stage_params.is_wlan = true;
	vpn_stage_params.is_tx_path = true;
	vpn_stage_params.task = task;
	vpn_stage_params.stage = &vpn_stage;
	vpn_stage_params.shadow_ring_info = &shadow_ring;
	vpn_stage_params.cached_ring_info = &cached_ring;
	vpn_stage_params.engine = &vpn_engine;
	vpn_stage_params.next_stage = &route_stage;

	ret = NestedRingVpnStageSetup(&vpn_stage_params);
	if (ret) {
		pr_err("Failed to setup VPN TX stage on wlan path, ret %" PRId32 "\n", ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &vpn_stage);

	/****************************** Route Data Stage ******************************/
	NestedCachedRingInit(&cached_ring, &cached_ring_buf[0], H2D_CACHED_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &cached_ring_vpn_stage_tail, &cached_ring_tail);

	NestedShadowRingInit(&shadow_ring, (uintptr_t)&shadow_ring_buf[0], H2D_SHADOW_RING_ITEM_LEN,
			     H2D_CACHED_RING_SIZE, &shadow_ring_vpn_stage_tail, &shadow_ring_tail);

	task = NepTaskGet(kNepRouteDataTaskGroup, kNepRouteWlanHostToDevicePathTask);
	if (!task) {
		pr_err("Failed to get wlan host to device route stage task\n");
		return -EINVAL;
	}
	route_stage_params.name = "wlan_h2d_route";
	route_stage_params.network_type = kNoaNetworkInterfaceWlan;
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
		NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost,
				     NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT);
	route_stage_params.feedthrough_path_notifier =
		RingServiceDoorbellNotifierGet(options.wlan_h2d_feedthrough_notifier_port);
	route_stage_params.feedback_path_notifier =
		RingServiceDoorbellNotifierGet(options.wlan_h2d_feedback_notifier_port);
	route_stage_params.support_vpn = true;
	route_stage_params.vpn_path_id = route_stage_params.feedthrough_path_id;
	route_stage_params.vpn_path_notifier = route_stage_params.feedthrough_path_notifier;
#ifdef linux
	route_stage_params.dma = NULL;
#else /* linux */
	route_stage_params.dma =
		static_cast<void *>(DeviceManager::Instance()->GetDevice<TemplatedDma>());
#endif /* linux */

	ret = NestedRingRouteStageSetup(&route_stage_params);
	if (ret) {
		pr_err("Failed to setup route stage on wlan host to device path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}
	NepRingStageRegister(src_ring, &route_stage);

	ret = RingServiceRegisterIsr(options.wlan_h2d_isr_port, options.wlan_h2d_isr_ring_id,
				     cache_stage.task);
	if (ret) {
		pr_err("Failed to register ISR for wlan host to device path, ret %" PRId32 "\n",
		       ret);
		return -EINVAL;
	}

	return 0;
}
