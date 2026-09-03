#include "wlan_service.h"

#include <common/wlan/noa_wlan.h>
#include "core/wlan_pm/wlan_pm.h"
#include "wlan_service_rpc_protocol.h"
#include "wlan_log/wlan_log.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "sys_if/memory/sys_if_memory.h"
#include "modules/wlan_ring_manager/wlan_ring_manager.h"
#include "modules/sta_table/sta_table.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"
#include "modules/wlan_nep_buffer_pool/wlan_nep_buffer_pool.h"
#include "modules/flow_id_table/flow_id_table.h"
#include "core/dp/wlan_dp.h"
#include "ext_svc/ext_svc.h"
#include "wdev_if/wdev_if.h"
#include "core/wlan_shared_mem/memory_map_helper.h"
#include "wlan_debug_controller/wlan_packet_sniffer/wlan_debug_packet_sniffer_controller.h"
#include "wlan_debug_controller/wlan_log_sys/wlan_debug_log_sys_controller.h"
#include "wlan_debug_controller/wlan_packet_sniffer/wlan_debug_packet_sniffer.h"
#include "core/wlan_svc/wlan_service_fsm.h"
#include "sys_if/noa_net/noa_net.h"

static const uint32_t kDefaultDrainTimeoutMs = 100;

static void InitializeWlanRingParams(const NepRing *ring, uint32_t stride,
				     WlanRingInitParams *params_out)
{
	memset(params_out, 0, sizeof(*params_out));

	// Copy hardware register addresses for ring control.
	params_out->regs.dpa_base = ring->regs.dpa_base;
	params_out->regs.len = ring->regs.len;
	params_out->regs.max_item = ring->regs.max_item;
	params_out->regs.read = ring->regs.read;
	params_out->regs.write = ring->regs.write;
	// Copy descriptor ring configuration.
	params_out->stride = stride;
#if defined(HOST_SIMULATOR)
	SysIfIoWritel(*(WLAN_REINTERPRET_CAST(uint32_t *, ring->regs.max_item)),
		      WLAN_REINTERPRET_CAST(void *, ring->regs.max_item));
	SysIfIoWritel(*(WLAN_REINTERPRET_CAST(uint32_t *, ring->regs.len)),
		      WLAN_REINTERPRET_CAST(void *, ring->regs.len));
	SysIfIoWritel(*(WLAN_REINTERPRET_CAST(uint32_t *, ring->regs.read)),
		      WLAN_REINTERPRET_CAST(void *, ring->regs.read));
	SysIfIoWritel(*(WLAN_REINTERPRET_CAST(uint32_t *, ring->regs.write)),
		      WLAN_REINTERPRET_CAST(void *, ring->regs.write));
	SysIfIoWritel(WLAN_STATIC_CAST(uint64_t,
				       *(WLAN_REINTERPRET_CAST(uint64_t *, ring->regs.dpa_base))),
		      WLAN_REINTERPRET_CAST(void *, ring->regs.dpa_base));
#endif // defined(HOST_SIMULATOR)
	SysIfInvalidDCache(
		WLAN_REINTERPRET_CAST(const PhyAddr,
				      WLAN_REINTERPRET_CAST(void *, ring->regs.max_item)),
		sizeof(uint32_t));
	params_out->ndesc = SysIfIoReadl(WLAN_REINTERPRET_CAST(void *, ring->regs.max_item));
	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr,
						 WLAN_REINTERPRET_CAST(void *, ring->regs.len)),
			   sizeof(uint32_t));
	params_out->desc_sz = SysIfIoReadl(WLAN_REINTERPRET_CAST(void *, ring->regs.len));
	if (SysIfIsDriverMode()) {
		params_out->desc = WLAN_REINTERPRET_CAST(
			void *,
			WLAN_STATIC_CAST(uintptr_t, *(WLAN_REINTERPRET_CAST(uint64_t *,
									    ring->regs.dpa_base))));
	} else {
		SysIfInvalidDCache(
			WLAN_REINTERPRET_CAST(const PhyAddr,
					      WLAN_REINTERPRET_CAST(void *, ring->regs.dpa_base)),
			sizeof(uint32_t));
		params_out->desc = WLAN_REINTERPRET_CAST(
			void *, WLAN_STATIC_CAST(uintptr_t, SysIfIoReadl(WLAN_REINTERPRET_CAST(
								    void *, ring->regs.dpa_base))));
	}
	params_out->buf_size = params_out->desc_sz * params_out->ndesc;
	// Set the initial software read/write pointers.
	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr,
						 WLAN_REINTERPRET_CAST(void *, ring->regs.read)),
			   sizeof(uint32_t));
	params_out->read = SysIfIoReadl(WLAN_REINTERPRET_CAST(void *, ring->regs.read));
	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr,
						 WLAN_REINTERPRET_CAST(void *, ring->regs.write)),
			   sizeof(uint32_t));
	params_out->write = SysIfIoReadl(WLAN_REINTERPRET_CAST(void *, ring->regs.write));
	// Safely copy the ring name.
	strncpy(params_out->name, ring->name, sizeof(params_out->name));
	// Ensure null-termination, as strncpy doesn't guarantee it.
	params_out->name[sizeof(params_out->name) - 1] = '\0';

	WLAN_LOG_DEBUG(Cfg, "ring name: %s", params_out->name);
	WLAN_LOG_DEBUG(Cfg, "desc: %p", (void *)params_out->desc);
	WLAN_LOG_DEBUG(Cfg, "ndesc: %u", params_out->ndesc);
	WLAN_LOG_DEBUG(Cfg, "desc_sz: %u", params_out->desc_sz);
}

static int32_t NepRingManagerAttachRings(WlanService *const svc)
{
	WlanRingInitParams ring_init_params;
	uint32_t i = 0;
	NepRing *ring = NULL;

	for (i = 0; i < NUM_RING_SVC_RX_CMPL_RING; i++) {
		ring = &svc->ring_svc.ring_svc_rx_cmpl_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kNepRxCmplRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kNepRxCmplRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_TX_POST_RING; i++) {
		ring = &svc->ring_svc.ring_svc_tx_post_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kNepTxPostRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kNepTxPostRingGroup, i);
			return -ENODEV;
		}
	}

	return 0;
};

static int32_t ApcRingManagerAttachRings(WlanService *const svc)
{
	WlanRingInitParams ring_init_params;
	uint32_t i = 0;
	NepRing *ring = NULL;

	for (i = 0; i < NUM_RING_SVC_DIRECT_RX_CMPL_RING; i++) {
		ring = &svc->ring_svc.ring_svc_direct_rx_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcDirectRxCmplRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcDirectRxCmplRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_DIRECT_TX_POST_RING; i++) {
		ring = &svc->ring_svc.ring_svc_direct_tx_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcDirectTxPostRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcDirectTxPostRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_DIRECT_TX_CMPL_RING; i++) {
		ring = &svc->ring_svc.ring_svc_direct_tx_cpl_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcTxCmplRingGroup, i,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcTxCmplRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_DIRECT_RX_FALLBACK_RING; i++) {
		ring = &svc->ring_svc.ring_svc_direct_rx_fallback_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcFallbackRxCmplRingGroup, i,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcFallbackRxCmplRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_DIRECT_VENDOR_RX_REPLENISH_RING; i++) {
		ring = &svc->ring_svc.ring_svc_direct_vendor_rx_replenish_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcVendorRxBufReplnRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcVendorRxBufReplnRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_DIRECT_NOA_TX_REPLENISH_RING; i++) {
		ring = &svc->ring_svc.ring_svc_direct_noa_tx_replenish_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcNoaTxBufReplnRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcNoaTxBufReplnRingGroup, i);
			return -ENODEV;
		}
	}

	for (i = 0; i < NUM_RING_SVC_DIRECT_FEEDBACK_RING; i++) {
		ring = &svc->ring_svc.ring_svc_feedback_ring_pool[i];
		InitializeWlanRingParams(ring, 1, &ring_init_params);
		if (WlanRingManagerAddRing(&svc->ring_manager, kApcFeedbackRingGroup, 0,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, kApcFeedbackRingGroup, i);
			return -ENODEV;
		}
	}

	return 0;
}

static void PmBusPowerEventHandler(WlanPowerEvent UNUSED(event), void *UNUSED(context))
{
	// TODO: b/399291271 - Implement the wlan pm power event callback.
}

static int32_t WlanServicePmInitWrapper(WlanService *const svc)
{
	WlanPmInitParams pm_init_params;
	uint32_t power_on_bus = 1;

	memset(&pm_init_params, 0, sizeof(pm_init_params));
	pm_init_params.ext_svc = &svc->ext_svc;
	pm_init_params.client.on_power_evt_cb = PmBusPowerEventHandler;
	pm_init_params.client.context = NULL;
	if (WlanPmInit(&pm_init_params, &svc->pm_iface)) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanPmInit failed.", __func__);
		return -ENODEV;
	}

	svc->ext_svc.wlan_rpc_service.send_event_to_apc(
		kWlanEventTypePmBusPower, WLAN_STATIC_CAST(void *, &power_on_bus),
		sizeof(power_on_bus));

	return 0;
}

static int32_t WlanServiceInit(WlanService *const svc, const void *msg,
			       uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	const WlanCmdFwInit *params = WLAN_REINTERPRET_CAST(const WlanCmdFwInit *, msg);
	MemoryMapHelperInitParams memory_map_helper_init_params;

	if (!svc || !params) {
		return -EINVAL;
	}

	memset(&memory_map_helper_init_params, 0, sizeof(memory_map_helper_init_params));
	memory_map_helper_init_params.base_addr = params->noa_shared_mem_addr;
	memory_map_helper_init_params.size = params->noa_shared_mem_size;
	if (MemoryMapHelperInit(&svc->memory_map_helper, &memory_map_helper_init_params) != 0) {
		return -ENODEV;
	}

	return WlanServiceFsmEventHandler(&svc->fsm, kWlanServiceFsmEventInit);
}

static int32_t WlanServiceExit(WlanService *const svc, const void *UNUSED(msg),
			       uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	return WlanServiceFsmEventHandler(&svc->fsm, kWlanServiceFsmEventExit);
}

static int32_t WlanServiceRxHandoverSync(WlanService *const svc, const void *msg,
					 uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	const struct noa_wlan_cmd_rx_handover_sync *cmd =
		WLAN_REINTERPRET_CAST(const struct noa_wlan_cmd_rx_handover_sync *, msg);
	const struct noa_wlan_rx_handover_item *items;
	uint32_t i;
	int32_t ret = 0;

	if (!svc || !cmd) {
		return -EINVAL;
	}

	WLAN_LOG_INFO(Cfg, "%s(): received RX_HANDOVER_SYNC: count=%" PRIu32 ", table_dpa=0x%08" PRIx32 "%08" PRIx32 "\n",
		      __func__, cmd->count,
		      WLAN_STATIC_CAST(uint32_t, cmd->handover_table_dpa_addr >> 32),
		      WLAN_STATIC_CAST(uint32_t, cmd->handover_table_dpa_addr & 0xFFFFFFFF));

	if (cmd->count == 0 || cmd->handover_table_dpa_addr == 0) {
		WLAN_LOG_INFO(Cfg, "%s(): empty handover table or null DPA addr\n", __func__);
		return 0;
	}

	items = WLAN_REINTERPRET_CAST(const struct noa_wlan_rx_handover_item *,
				      WLAN_STATIC_CAST(uintptr_t, cmd->handover_table_dpa_addr));

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, items),
			   cmd->count * sizeof(struct noa_wlan_rx_handover_item));

	for (i = 0; i < cmd->count; i++) {
		const struct noa_wlan_rx_handover_item *src = &items[i];

		WLAN_LOG_DEBUG(Cfg, "%s(): importing TKID %" PRIu32 ": size=%" PRIu32 ", host_pa=0x%08" PRIx32 "%08" PRIx32 ", dpa_addr=0x%08" PRIx32 "%08" PRIx32 "\n",
			      __func__, WLAN_STATIC_CAST(uint32_t, src->tkid), WLAN_STATIC_CAST(uint32_t, src->buf_size),
			      WLAN_STATIC_CAST(uint32_t, src->host_pa >> 32), WLAN_STATIC_CAST(uint32_t, src->host_pa & 0xFFFFFFFF),
			      WLAN_STATIC_CAST(uint32_t, src->dpa_addr >> 32), WLAN_STATIC_CAST(uint32_t, src->dpa_addr & 0xFFFFFFFF));

		ret = BmAcquire(kVendorRxBufferManager, src->tkid, src->buf_size, src->host_pa,
				src->dpa_addr);
		if (ret) {
			WLAN_LOG_ERROR(Cfg, "%s(): Failed to acquire buffer for TKID %" PRIu32 ", err: %" PRId32 "\n",
				       __func__, WLAN_STATIC_CAST(uint32_t, src->tkid), ret);
		}
	}

	WLAN_LOG_INFO(Cfg, "%s(): successfully imported handover buffers\n", __func__);
	return 0;
}

static int32_t WlanServiceStationControl(WlanService *const svc, const void *msg,
					 uint32_t UNUSED(resp_buf_len),
					 void *const UNUSED(resp_buf))
{
	struct StaInfo sta_info;
	const struct NoaWlanStaInfo *sta_info_list = GetNoaWlanStaInfoAddr(&svc->memory_map_helper);
	const WlanCmdStaActivate *sync_cmd = WLAN_REINTERPRET_CAST(const WlanCmdStaActivate *, msg);

	if (!svc || !sync_cmd) {
		return -EINVAL;
	}

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, sta_info_list),
			   sizeof(struct NoaWlanStaInfo) * NOA_MAX_STA_SUPPORT);

	sta_info.oif = sta_info_list[sync_cmd->sta_table_idx].oif;
	sta_info.bss_idx = sta_info_list[sync_cmd->sta_table_idx].bss_idx;
	sta_info.encrypt_type = sta_info_list[sync_cmd->sta_table_idx].encrypt_type;
	sta_info.encap_type = sta_info_list[sync_cmd->sta_table_idx].encap_type;
	sta_info.lmac_id = sta_info_list[sync_cmd->sta_table_idx].lmac_id;
	sta_info.bmid = sta_info_list[sync_cmd->sta_table_idx].bmid;
	sta_info.search_idx = sta_info_list[sync_cmd->sta_table_idx].search_idx;
	sta_info.search_type = sta_info_list[sync_cmd->sta_table_idx].search_type;
	sta_info.dscp_tid_map_id = sta_info_list[sync_cmd->sta_table_idx].dscp_tid_map_id;
	sta_info.addry_en = sta_info_list[sync_cmd->sta_table_idx].addry_en;
	sta_info.addrx_en = sta_info_list[sync_cmd->sta_table_idx].addrx_en;
	sta_info.enable = sta_info_list[sync_cmd->sta_table_idx].enable;

	memcpy(sta_info.mac_addr, sta_info_list[sync_cmd->sta_table_idx].mac_addr,
	       sizeof(sta_info.mac_addr));
	memcpy(sta_info.qos_txq_map, sta_info_list[sync_cmd->sta_table_idx].qos_txq_map,
	       sizeof(sta_info.qos_txq_map));

	if (sync_cmd->enable) {
		return StaTableAddStation(&svc->sta_table, sync_cmd->sta_table_idx, &sta_info);
	} else {
		return StaTableRemoveStation(&svc->sta_table, sync_cmd->sta_table_idx);
	}
}

static int32_t WlanServiceWdevRingUpdate(WlanService *const svc, uint32_t ring_type)
{
	const struct NoaWlanWiFiRingConfig *ring_config = WLAN_REINTERPRET_CAST(
		const struct NoaWlanWiFiRingConfig *,
		GetNoaWlanConfigSectionAddr(&svc->memory_map_helper, kWdevRingConfig));
	uint32_t i = 0;
	uint32_t ring_count = 0;
	const struct NoaWlanRingInfo *ring_pool =
		GetNoaWLanWdevRingPool(&svc->memory_map_helper, ring_type);
	RingGroupType wdev_ring_group_type;

	if (!svc || !ring_pool) {
		return -EINVAL;
	}

	switch (ring_type) {
	case kTxPostRingPool:
		wdev_ring_group_type = kWdevTxPostRingGroup;
		break;
	case kRxPostRingPool:
		wdev_ring_group_type = kWdevRxPostRingGroup;
		break;
	case kTxCmplRingPool:
		wdev_ring_group_type = kWdevTxCmplRingGroup;
		break;
	case kRxCmplRingPool:
		wdev_ring_group_type = kWdevRxCmplRingGroup;
		break;
	default:
		return -EINVAL;
	}

	SysIfInvalidDCache(
		WLAN_REINTERPRET_CAST(PhyAddr, GetNoaWlanConfigSectionAddr(&svc->memory_map_helper,
									   kWdevRingConfig)),
		GetNoaWlanConfigSectionSize(&svc->memory_map_helper, kWdevRingConfig));

	ring_count = ring_config->wifi_ring_pool_config[ring_type].count;
	for (i = 0; i < ring_count; i++) {
		WlanRingInitParams ring_init_params;

		memset(&ring_init_params, 0, sizeof(WlanRingInitParams));
		ring_init_params.regs.base = (ring_pool[i]).regs.base;
		ring_init_params.regs.len = (ring_pool[i]).regs.len;
		ring_init_params.regs.max_item = (ring_pool[i]).regs.max_item;
		ring_init_params.regs.read = (ring_pool[i]).regs.read;
		ring_init_params.regs.write = (ring_pool[i]).regs.write;
		ring_init_params.desc_sz = (ring_pool[i]).desc_sz;
		ring_init_params.desc = WLAN_REINTERPRET_CAST(void *, (ring_pool[i]).dma_va);
		ring_init_params.desc_dma = WLAN_STATIC_CAST(dma_addr_t, (ring_pool[i]).dma_pa);
		ring_init_params.ndesc = (ring_pool[i]).ndesc;
		ring_init_params.hw_idx = (ring_pool[i]).hw_idx;
		ring_init_params.stride = (ring_pool[i]).stride;
		ring_init_params.sn = (ring_pool[i]).sn;
		strncpy(ring_init_params.name, (ring_pool[i]).name, sizeof(ring_init_params.name));
		if (0) {
			WLAN_LOG_DEBUG(Cfg, "ring_type: %u",
				       WLAN_STATIC_CAST(RingGroupType, ring_type));
			WLAN_LOG_DEBUG(Cfg, "regs.base: %p",
				       WLAN_REINTERPRET_CAST(void *, ring_init_params.regs.base));
			WLAN_LOG_DEBUG(Cfg, "regs.len: %p",
				       WLAN_REINTERPRET_CAST(void *, ring_init_params.regs.len));
			WLAN_LOG_DEBUG(Cfg, "regs.max_item: %p",
				       WLAN_REINTERPRET_CAST(void *,
							     ring_init_params.regs.max_item));
			WLAN_LOG_DEBUG(Cfg, "regs.read: %p",
				       WLAN_REINTERPRET_CAST(void *, ring_init_params.regs.read));
			WLAN_LOG_DEBUG(Cfg, "regs.write: %p",
				       WLAN_REINTERPRET_CAST(void *, ring_init_params.regs.write));
			WLAN_LOG_DEBUG(Cfg, "ring name: %s", ring_init_params.name);
		}
		if (WlanRingManagerAddRing(&svc->ring_manager, wdev_ring_group_type, i,
					   &ring_init_params) != 0) {
			WLAN_LOG_WARN(Cfg,
				      "%s(): add ring type: %" PRId32 " ring id: %" PRId32
				      " failed.",
				      __func__, ring_type, i);
			return -ENODEV;
		}
	}

	return 0;
}

static int32_t WlanServiceWdevRingSync(WlanService *const svc, uint32_t ring_type)
{
	const struct NoaWlanWiFiRingConfig *ring_config = WLAN_REINTERPRET_CAST(
		const struct NoaWlanWiFiRingConfig *,
		GetNoaWlanConfigSectionAddr(&svc->memory_map_helper, kWdevRingConfig));
	uint32_t i = 0;
	uint32_t ring_count = ring_config->wifi_ring_pool_config[ring_type].count;
	struct NoaWlanRingInfo *ring_pool =
		GetNoaWLanWdevRingPool(&svc->memory_map_helper, ring_type);
	RingGroupType wdev_ring_group_type;

	if (!svc) {
		return -EINVAL;
	}

	switch (ring_type) {
	case kTxPostRingPool:
		wdev_ring_group_type = kWdevTxPostRingGroup;
		break;
	case kRxPostRingPool:
		wdev_ring_group_type = kWdevRxPostRingGroup;
		break;
	case kTxCmplRingPool:
		wdev_ring_group_type = kWdevTxCmplRingGroup;
		break;
	case kRxCmplRingPool:
		wdev_ring_group_type = kWdevRxCmplRingGroup;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ring_count; i++) {
		struct NoaWlanRingInfo *ring_info = &(ring_pool[i]);
		WlanRing *ring;

		if (WlanRingManagerGetRing(&svc->ring_manager, wdev_ring_group_type, i, &ring) !=
		    0) {
			continue;
		}
		ring_info->sn = ring->sn;
	}
	SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, ring_pool),
			 sizeof(struct NoaWlanRingInfo) * ring_count);
	return 0;
}

static int32_t WlanServiceWdevTxRingControl(WlanService *const svc, const void *msg,
					    uint32_t UNUSED(resp_buf_len),
					    void *const UNUSED(resp_buf))
{
	const WlanCmdTxRingActivate *ring_activate =
		WLAN_REINTERPRET_CAST(const WlanCmdTxRingActivate *, msg);

	if (!svc || !ring_activate) {
		return -EINVAL;
	}

	if (ring_activate->enable) {
		WlanRingManagerActivateRing(&svc->ring_manager, kWdevTxPostRingGroup,
					    ring_activate->ring_id);
	} else {
		WlanRingManagerDeactivateRing(&svc->ring_manager, kWdevTxPostRingGroup,
					      ring_activate->ring_id);
	}

	return 0;
}

static int32_t WlanServiceIsrRegister(WlanService *const svc, const void *msg,
				      uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	const struct WlanCmdIrqReuest *irq_request =
		WLAN_REINTERPRET_CAST(const struct WlanCmdIrqReuest *, msg);
	int i = 0;

	WlanDpInitIrqInfo irq_info[MAX_NUM_WLAN_DEV_IRQ];
	memset(irq_info, 0, sizeof(WlanDpInitIrqInfo) * MAX_NUM_WLAN_DEV_IRQ);

	for (i = 0; i < irq_request->irq_nums; i++) {
		irq_info[i].irq_num = irq_request->irqs[i];
		irq_info[i].rx_data_ring_polling_mask = 0x1;
		irq_info[i].tx_cpl_ring_polling_mask = 0x1;
	}

	if (irq_request->msi_descs[0].nvec_used > 0) {
		SysIfSetupPcieMsiDescs(WLAN_REINTERPRET_CAST(const void *, irq_request->msi_descs),
				       MAX_WLAN_PCIE_MSI_NUM);
	}

	return WlanDpWdevRequestIrqs(&svc->dp, irq_request->irq_nums, irq_info);
}

static int32_t WlanServiceStart(WlanService *const svc, const void *UNUSED(msg),
				uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	return WlanServiceFsmEventHandler(&svc->fsm, kWlanServiceFsmEventStart);
}

static int32_t WlanServiceStop(WlanService *const svc, const void *UNUSED(msg),
			       uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	return WlanServiceFsmEventHandler(&svc->fsm, kWlanServiceFsmEventStop);
}

static int32_t WlanServiceMockBmSyncDoorbell(WlanService *const svc, const void *UNUSED(msg),
					     uint32_t UNUSED(resp_buf_len),
					     void *const UNUSED(resp_buf))
{
	TriggerWlanDpBufferRefill(&svc->dp);
	return 0;
}

static int32_t WlanServiceDpModeCtrl(WlanService *const svc, const void *msg,
				     uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	const WlanCmdDpModeCtrl *dp_mode_ctrl =
		WLAN_REINTERPRET_CAST(const WlanCmdDpModeCtrl *, msg);

	WlanDpSetMode(&svc->dp, WLAN_STATIC_CAST(const WlanDpMode, dp_mode_ctrl->mode));

	return 0;
}

static int32_t WlanServiceLogSystemCtrl(WlanService *const UNUSED(svc), const void *msg,
					uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	WlanSvcDebugLogConfig(WLAN_REINTERPRET_CAST(const WlanLogSystemControlCmd *, msg));
	return 0;
}

static int32_t WlanServicePacketSnifferCtrl(WlanService *const UNUSED(svc), const void *msg,
					    uint32_t UNUSED(resp_buf_len),
					    void *const UNUSED(resp_buf))
{
	WlanPacketSnifferConfig(WLAN_REINTERPRET_CAST(const WlanPacketSnifferControlParam *, msg));
	return 0;
}

static int32_t WlanServiceShellDumpStaInfo(WlanService *const svc, uint32_t UNUSED(argv_len),
					   const char *const UNUSED(argv))
{
	const struct NoaWlanStaInfo *sta_info;
	uint32_t i;

	if (!svc) {
		return -EINVAL;
	}

	sta_info = GetNoaWlanStaInfoAddr(&svc->memory_map_helper);

	if (!sta_info) {
		return -EINVAL;
	}

	WLAN_LOG_ERROR(Shell, "==== NOA Wlan STA Info ===");
	for (i = 0; i < svc->sta_table.num_active_sta; i++) {
		if (!sta_info[i].enable) {
			continue;
		}
		WLAN_LOG_ERROR(Shell, "%12s: %5" PRIu8, "sta_id", sta_info[i].sta_id);
		WLAN_LOG_ERROR(Shell, "%12s: %8" PRIu32 ",%12s: %8" PRIu16, "oif", sta_info[i].oif,
			       "bss_idx", sta_info[i].bss_idx);
		WLAN_LOG_ERROR(Shell,
			       "%12s: %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
			       " %" PRIu32 " %" PRIu32 " %" PRIu32,
			       "qos_txq_map", sta_info[i].qos_txq_map[0],
			       sta_info[i].qos_txq_map[1], sta_info[i].qos_txq_map[2],
			       sta_info[i].qos_txq_map[3], sta_info[i].qos_txq_map[4],
			       sta_info[i].qos_txq_map[5], sta_info[i].qos_txq_map[6],
			       sta_info[i].qos_txq_map[7]);
		WLAN_LOG_ERROR(Shell, "%12s: %02x:%02x:%02x:%02x:%02x:%02x", "mac_addr",
			       sta_info[i].mac_addr[0], sta_info[i].mac_addr[1],
			       sta_info[i].mac_addr[2], sta_info[i].mac_addr[3],
			       sta_info[i].mac_addr[4], sta_info[i].mac_addr[5]);
		WLAN_LOG_ERROR(Shell, "%12s: %5" PRIu8 ",%12s: %5" PRIu8 ",%12s: %5" PRIu8,
			       "encrypt_type", sta_info[i].encrypt_type, "encap_type",
			       sta_info[i].encap_type, "lmac_id", sta_info[i].lmac_id);

		WLAN_LOG_ERROR(Shell, "%12s: %5" PRIu8 ",%12s: %5" PRIu8 ",%12s: %5" PRIu8, "bmid",
			       sta_info[i].bmid, "search_idx", sta_info[i].search_idx,
			       "search_type", sta_info[i].search_type);

		WLAN_LOG_ERROR(Shell, "%12s: %5" PRIu8 ",%12s: %5" PRIu8 ",%12s: %5" PRIu8,
			       "dscp_tid_map_id", sta_info[i].dscp_tid_map_id, "addry_en",
			       sta_info[i].addry_en, "addrx_en", sta_info[i].addrx_en);
	}

	return 0;
}

static int32_t WlanServiceShellSyncIrqInfo(WlanService *const svc, uint32_t UNUSED(argv_len),
					   const char *const UNUSED(argv))
{
	int i = 0;
	const NoaInterruptStateInfo *intr_state;

	if (!svc) {
		return -EINVAL;
	}

	intr_state = WLAN_REINTERPRET_CAST(NoaInterruptStateInfo *,
					   GetNoaWlanConfigSectionAddr(&svc->memory_map_helper,
								       kIntrStateInfo));

	WLAN_LOG_INFO(Shell, "==== NOA Wlan Interrupt Statistic ===");
	for (i = 0; i < svc->dp.num_ring_svc_irq; i++) {
		UpdateNoaWlanInterruptCounter(
			&svc->memory_map_helper,
			svc->dp.ring_svc_intr_ctx_group[i].intr_stats.num_total_intr, NEP_NCP_INTR);
		WLAN_LOG_INFO(Shell,
			      "Ring Service NEP to NCP Interrupt IRQ Num: %" PRIu32
			      ", RXCPL Mask: 0x%08" PRIX32 ", TXCPL Mask: 0x%08" PRIX32,
			      svc->dp.ring_svc_intr_ctx_group[i].irq_num,
			      svc->dp.ring_svc_intr_ctx_group[i].rx_data_ring_polling_mask,
			      svc->dp.ring_svc_intr_ctx_group[i].tx_cpl_ring_polling_mask);
	}
	WLAN_LOG_INFO(Shell, "Ring Service NEP to NCP Total Interrupt Count: %" PRIu32,
		      intr_state->interrupt_cnt[NEP_NCP_INTR]);

	for (i = 0; i < svc->dp.num_wlan_dev_irq; i++) {
		UpdateNoaWlanInterruptCounter(
			&svc->memory_map_helper,
			svc->dp.wlan_dev_intr_ctx_group[i].intr_stats.num_total_intr,
			PCIE_MSI_NCP_INTR);
		WLAN_LOG_INFO(Shell,
			      "PCIe MSI NCP Interrupt IRQ Num: %" PRIu32
			      ", RXCPL Mask: 0x%08" PRIX32 ", TXCPL Mask: 0x%08" PRIX32,
			      svc->dp.wlan_dev_intr_ctx_group[i].irq_num,
			      svc->dp.wlan_dev_intr_ctx_group[i].rx_data_ring_polling_mask,
			      svc->dp.wlan_dev_intr_ctx_group[i].tx_cpl_ring_polling_mask);
	}
	WLAN_LOG_INFO(Shell, "PCIe MSI NCP Total Interrupt Count: %" PRIu32,
		      intr_state->interrupt_cnt[PCIE_MSI_NCP_INTR]);
	WLAN_LOG_INFO(Shell, "PCIe MSI APC Total Interrupt Count: %" PRIu32,
		      intr_state->interrupt_cnt[PCIE_MSI_APC_INTR]);
	WLAN_LOG_INFO(Shell, "Ring Service NEP to APC Total Interrupt Count: %" PRIu32,
		      intr_state->interrupt_cnt[NEP_APC_INTR]);

	return 0;
}

static int32_t WlanServiceShellSyncNepRingInfo(WlanService *const svc, uint32_t UNUSED(argv_len),
					       const char *const UNUSED(argv))
{
	int i = 0;
	WlanRing *ring_txpost;
	WlanRing *ring_rxcpl;
	WLAN_LOG_INFO(Shell, "==== NOA NEP Ring Info ===");

	for (i = 0; i < NUM_NEP_TX_POST_RING; i++) {
		WlanRingManagerGetRing(&svc->ring_manager,
				       WLAN_STATIC_CAST(RingGroupType, kNepTxPostRingGroup), i,
				       &ring_txpost);
		UpdateNoaWlanNepRingConfig(&svc->memory_map_helper, ring_txpost, i,
					   RING_TYPE_TX_DATA);
	}

	for (i = 0; i < NUM_NEP_RX_CMPL_RING; i++) {
		WlanRingManagerGetRing(&svc->ring_manager,
				       WLAN_STATIC_CAST(RingGroupType, kNepRxCmplRingGroup), i,
				       &ring_rxcpl);
		UpdateNoaWlanNepRingConfig(&svc->memory_map_helper, ring_rxcpl, i,
					   RING_TYPE_RX_DATA);
	}

	DumpMemoryMapNepRingConfigSection(&svc->memory_map_helper);
	return 0;
}

static int32_t WlanServiceShellDumpWdevTxRingInfo(WlanService *const svc, uint32_t UNUSED(argv_len),
                           const char *const UNUSED(argv))
{
	uint32_t num_rings = WlanRingManagerGetRingNum(&svc->ring_manager, kWdevTxPostRingGroup);

	WLAN_LOG_INFO(Shell, "==== Wdev TX Post Ring Info ===");
	WLAN_LOG_INFO(Shell, "Total rings: %" PRIu32, num_rings);

	for (uint32_t i = 0; i < num_rings; i++) {
		WlanRing *ring;
		if (WlanRingManagerGetRing(&svc->ring_manager, kWdevTxPostRingGroup, i, &ring) == 0) {
			bool active = WlanRingIsActive(ring);
			WLAN_LOG_INFO(Shell, "Ring %" PRIu32 ": active=%d, hw_idx=%" PRIu32 ", flags=%" PRIu32,
				      i, active, ring->hw_idx, ring->flags);
			if (active) {
				for (uint32_t j = 0; j < MAX_NUM_STA_SUPPORT; j++) {
					const StaInfo *sta = &svc->sta_table.sta_info_list[j];
					if (sta->enable) {
						for (int tid = 0; tid < kWlanTidNum; tid++) {
							if (sta->qos_txq_map[tid] == i) {
								WLAN_LOG_INFO(Shell, "  oif: %" PRIu32 ", MAC: %02X:%02X:%02X:%02X:%02X:%02X",
									      sta->oif,
									      sta->mac_addr[0], sta->mac_addr[1],
									      sta->mac_addr[2], sta->mac_addr[3],
									      sta->mac_addr[4], sta->mac_addr[5]);
								break;
							}
						}
					}
				}
			}
		}
	}

	return 0;
}

static int32_t WlanServiceShellSyncMibInfo(WlanService *const svc, uint32_t UNUSED(argv_len),
					   const char *const UNUSED(argv))
{
	WlanDpStats *dp_stats;
	struct NoaMib *mib;

	if (!svc) {
		return -EINVAL;
	}

	dp_stats = &svc->dp.dp_stats;

	// Sync to share memory MIB section.
	mib = UpdateNoaWlanMIBInfo(&svc->memory_map_helper, dp_stats);

	if (!mib) {
		return -EINVAL;
	}

	DumpMemoryMapMibSection(&svc->memory_map_helper);
	return 0;
}

static int32_t WlanServiceShellSetDpMode(WlanService *const svc, uint32_t argv_len,
					 const char *const argv)
{
	WlanCmdDpModeCtrl dp_mode_ctrl;

	memset(&dp_mode_ctrl, 0, sizeof(dp_mode_ctrl));

	if (strncmp(argv, "normal", argv_len) == 0) {
		dp_mode_ctrl.mode = kWlanCmdDpNormalMode;
	} else if (strncmp(argv, "feedthrough", argv_len) == 0) {
		dp_mode_ctrl.mode = kWlanCmdDpForceFeedthroughMode;
	} else if (strncmp(argv, "vpn", argv_len) == 0) {
		dp_mode_ctrl.mode = kWlanCmdDpVpnForceFwdToNetEngineMode;
	} else {
		WLAN_LOG_ERROR(Shell, "%s(): Unknown mode: %s", __func__, argv);
		return -EINVAL;
	}

	WLAN_LOG_ERROR(Shell, "%s(): Set mode: %s", __func__, argv);

	return WlanServiceCommand(svc, kWlanCmdDpModeCtrl, &dp_mode_ctrl, 0, NULL);
}

static int32_t WlanServiceShellDumpMemoryMapOffset(WlanService *const svc,
						   uint32_t UNUSED(argv_len),
						   const char *const UNUSED(argv))
{
	MemoryMapoffsetDump(&svc->memory_map_helper);
	return 0;
}

static int32_t WlanServiceShellDumpMemoryMapSection(WlanService *const svc, uint32_t argv_len,
						    const char *const argv)
{
	int32_t idx = -1, ring_type = -1;

	WLAN_LOG_INFO(Shell, "=============== (NCP) NOA WLAN %s Value ===============", argv);
	if (strncmp(argv, "GLOBAL_CONFIG", argv_len) == 0) {
		DumpMemoryMapGlobalConfigSection(&svc->memory_map_helper);
	} else if (strncmp(argv, "NEP_RING_CONFIG", argv_len) == 0) {
		WlanServiceShellSyncNepRingInfo(svc, argv_len, argv);
	} else if (strncmp(argv, "STA_INFO", argv_len) == 0) {
		DumpMemoryMapStaInfoSection(&svc->memory_map_helper);
	} else if (strncmp(argv, "WDEV_RING_CONFIG", strlen("WDEV_RING_CONFIG")) == 0) {
		sscanf(argv, "WDEV_RING_CONFIG %d %d", &ring_type, &idx);
		if (idx >= 0 && ring_type >= 0) {
			WlanServiceWdevRingSync(svc, ring_type);
			DumpMemoryMapWdevRingConfigSection(&svc->memory_map_helper, ring_type, idx);
		}
	} else if (strncmp(argv, "PM_STATE_INFO", argv_len) == 0) {
		DumpMemoryMapPmStateInfoSection(&svc->memory_map_helper);
	} else if (strncmp(argv, "MIB_INFO", argv_len) == 0) {
		WlanServiceShellSyncMibInfo(svc, argv_len, argv);
	} else if (strncmp(argv, "INTR_STATE_INFO", argv_len) == 0) {
		WlanServiceShellSyncIrqInfo(svc, argv_len, argv);
	} else {
		WLAN_LOG_ERROR(Shell, "%s(): Unknown section: %s", __func__, argv);
		return -EINVAL;
	}
	return 0;
}

static int32_t WlanServiceShellStartTputMonitor(WlanService *const svc, uint32_t UNUSED(argv_len),
						const char *const UNUSED(argv))
{
	WlanDpThroughputMonitorStart(&svc->dp);
	return 0;
}

static int32_t WlanServiceShellSwitchTputMonitor(WlanService *const svc, uint32_t UNUSED(argv_len),
						 const char *const UNUSED(argv))
{
	WlanDpThroughputMonitorStop(&svc->dp);
	return 0;
}

static int32_t WlanServiceShellDumpFlowIdTable(WlanService *const svc, uint32_t UNUSED(argv_len),
					       const char *const UNUSED(argv))
{
	if (!svc) {
		return -EINVAL;
	}

	PrintAllValidFlowIds(&svc->flow_id_table);
	return 0;
}

static int32_t WlanServiceShellWakeLock(WlanService *const svc, uint32_t argv_len,
					const char *const argv)
{
	if (strncmp(argv, "acquire", argv_len) == 0) {
		WLAN_LOG_INFO(Shell, "Acquire wake lock");
		VoteBusPower(svc->pm_iface);
	} else if (strncmp(argv, "release", argv_len) == 0) {
		WLAN_LOG_INFO(Shell, "Release wake lock");
		DevoteBusPower(svc->pm_iface);
	} else if (strncmp(argv, "state", argv_len) == 0) {
		WlanBusPowerState state = GetBusPowerState(svc->pm_iface);
		if (state == kWlanBusPowerStatePoweringOn) {
			WLAN_LOG_INFO(Shell, "Wake lock is acquired");
		} else {
			WLAN_LOG_INFO(Shell, "Wake lock is released");
		}
	} else {
		WLAN_LOG_ERROR(Shell,
			       "%s(): Unknown command: %s, only supports acquire, release, state",
			       __func__, argv);
		return -EINVAL;
	}
	return 0;
}

static int32_t WlanServiceShellSimulateRxDrop(WlanService *const svc, uint32_t argv_len,
					      const char *const argv)
{
	if (!svc) {
		return -EINVAL;
	}

	if (strncmp(argv, "on", argv_len) == 0) {
		WLAN_LOG_INFO(Shell, "Enable simulate RX packet drop");
		WlanDpSetSimulateRxDrop(&svc->dp, true);
	} else if (strncmp(argv, "off", argv_len) == 0) {
		WLAN_LOG_INFO(Shell, "Disable simulate RX packet drop");
		WlanDpSetSimulateRxDrop(&svc->dp, false);
	} else {
		WLAN_LOG_ERROR(Shell, "%s(): Unknown argument: %s, only supports on or off",
			       __func__, argv);
		return -EINVAL;
	}
	return 0;
}

static int32_t WlanServiceShellRequest(WlanService *const svc, const void *msg,
				       uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	static const WlanServiceShellRequestTableEntry kShellRequestTable[] = {
		{ "irq-info", WlanServiceShellSyncIrqInfo },
		{ "set-mode", WlanServiceShellSetDpMode },
		{ "mib-info", WlanServiceShellSyncMibInfo },
		{ "sta-info", WlanServiceShellDumpStaInfo },
		{ "nep-ring-info", WlanServiceShellSyncNepRingInfo },
		{ "wdev-tx-ring-info", WlanServiceShellDumpWdevTxRingInfo },
		{ "memory-map-offset", WlanServiceShellDumpMemoryMapOffset },
		{ "memory-map-section", WlanServiceShellDumpMemoryMapSection },
		{ "start-tput-monitor", WlanServiceShellStartTputMonitor },
		{ "stop-tput-monitor", WlanServiceShellSwitchTputMonitor },
		{ "flow-id-table", WlanServiceShellDumpFlowIdTable },
		{ "wake-lock", WlanServiceShellWakeLock },
		{ "rx-drop", WlanServiceShellSimulateRxDrop },
	};
	static const uint32_t kRequestTableSize =
		sizeof(kShellRequestTable) / sizeof(WlanServiceShellRequestTableEntry);
	const WlanCmdShellRequest *shell_req =
		WLAN_REINTERPRET_CAST(const WlanCmdShellRequest *, msg);
	uint32_t i;

	if (!svc || !shell_req) {
		return -EINVAL;
	}

	for (i = 0; i < kRequestTableSize; i++) {
		if (strncmp(shell_req->cmd, kShellRequestTable[i].cmd, MAX_SHELL_CMD_LEN) == 0) {
			if (kShellRequestTable[i].handler) {
				return kShellRequestTable[i].handler(
					svc, shell_req->argv_len,
					WLAN_REINTERPRET_CAST(const char *, shell_req->argv));
			} else {
				return -ENOEXEC;
			}
		}
	}

	return -EINVAL;
}

static int32_t WlanServicePmStateNotify(WlanService *const svc, const void *msg,
					uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	if (!svc) {
		return -EINVAL;
	}
	svc->pm_iface.notify_bus_on();
	return 0;
}

static int32_t WlanServicePacketSnifferReset(WlanService *const svc, const void *msg,
					     uint32_t UNUSED(resp_buf_len),
					     void *const UNUSED(resp_buf))
{
	if (!svc) {
		return -EINVAL;
	}
	WlanPacketSnifferReset();

	return 0;
}

static int32_t WlanServiceUpdateUp2Flow(WlanService *const svc, const void *msg,
					uint32_t UNUSED(resp_buf_len), void *const UNUSED(resp_buf))
{
	const WlanCmdUpdateUp2Flow *req = WLAN_REINTERPRET_CAST(const WlanCmdUpdateUp2Flow *, msg);
	WLAN_LOG_INFO(Shell, "[WLAN] type: %u, table [1]%u [2]%u", req->type, req->table[1],
		      req->table[2]);
	UpdateUp2FlowPriorityTable(&svc->flow_id_table, req->type, req->table);
	return 0;
}

static int32_t WlanServiceUpdateFlowIdLookUpEntry(WlanService *const svc, const void *msg,
						  uint32_t UNUSED(resp_buf_len),
						  void *const UNUSED(resp_buf))
{
	struct WlanCmdUpdateFlowIdEntry {
		uint16_t flowid;
		uint8_t prio;
		uint8_t da[ETH_MAC_LEN];
		uint8_t ifindex;
		uint32_t oif;
		uint8_t role;
		uint8_t is_add;
	} __attribute__((packed, aligned(4)));

	const struct WlanCmdUpdateFlowIdEntry *req =
		WLAN_REINTERPRET_CAST(const struct WlanCmdUpdateFlowIdEntry *, msg);
	WLAN_LOG_INFO(Shell, "message content: flowid %u, prio %u, ifindex %u, oif: %u role %u",
		      req->flowid, req->prio, req->ifindex, req->oif, req->role);

	if (req->is_add) {
		WLAN_LOG_INFO(Shell, "Add flow id lookup table entry");
		AddFlowIdLookUpTableEntry(&svc->flow_id_table, req->oif, req->ifindex, req->flowid,
					  req->prio, req->da, req->role);
	} else {
		WLAN_LOG_INFO(Shell, "Remove flow id lookup table entry");
		DeleteFlowIdLookUpTableEntry(&svc->flow_id_table, req->oif, req->ifindex,
					     req->flowid);
	}

	return 0;
}

static const char *GetWlanServiceCommandName(int32_t cmd)
{
#define CASE_WLAN_SERVICE_COMMAND_NAME(command)                                                    \
	case k##command:                                                                           \
		return #command

	switch (cmd) {
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdRxbmSync);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdFwInit);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdFwStart);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdFwExit);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdFwStop);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdRingUpdate);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdTxRingActivate);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdStaActivate);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdRegReceiver);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdIsrRegister);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdTxbmSync);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdRegClientDev);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdDpModeCtrl);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdPacketSnifferCtrl);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdShareInfoCtrl);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdReportCtrl);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdShellRequest);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdPmStateNotify);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdPacketSnifferReset);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdUpdateUp2Flow);
		CASE_WLAN_SERVICE_COMMAND_NAME(WlanCmdUpdateFlowIdLookUpEntry);
	default:
		return "Unknown Command";
	}
}

static void WlanServiceCommandResponse(WlanService *svc, int32_t cmd, int32_t result)
{
	WlanEventCmdCompletion completion;
	ExternalServices *const ext_svc = &svc->ext_svc;

	if (!svc) {
		return;
	}

	WLAN_LOG_DEBUG(Cfg, "%s: Sending command completion response: cmd=%s(%d), result=%d",
		       __func__, GetWlanServiceCommandName(cmd), cmd, result);

	completion.cmd = cmd;
	completion.result = result;
	if (ExtSvcSendEventToApc(ext_svc, kWlanEventTypeCmdCompletion,
				 WLAN_REINTERPRET_CAST(void *, &completion),
				 sizeof(WlanEventCmdCompletion)) != 0) {
		WLAN_LOG_ERROR(
			Cfg,
			"%s: Failed to send command completion response: cmd=%s(%d), result=%d",
			__func__, GetWlanServiceCommandName(cmd), cmd, result);
	}
}

int32_t WlanServiceCommand(WlanService *const svc, int32_t cmd, const void *msg,
			   uint32_t resp_buf_len, void *const resp_buf)
{
	int ret = -ENODEV;
	// TODO(b/377179085): Hook up the wlan service command handlers.
	static const WlanCmdHandler kWlanCmdTable[kWlanCmdIdNum] = {
		[kWlanCmdRxbmSync] = NULL,
		[kWlanCmdFwInit] = WlanServiceInit,
		[kWlanCmdFwStart] = WlanServiceStart,
		[kWlanCmdFwExit] = WlanServiceExit,
		[kWlanCmdFwStop] = WlanServiceStop,
		[kWlanCmdRingUpdate] = NULL,
		[kWlanCmdTxRingActivate] = WlanServiceWdevTxRingControl,
		[kWlanCmdStaActivate] = WlanServiceStationControl,
		[kWlanCmdRegReceiver] = NULL,
		[kWlanCmdIsrRegister] = WlanServiceIsrRegister,
		[kWlanCmdTxbmSync] = NULL,
		[kWlanCmdRegClientDev] = NULL,
		[kWlanCmdDpModeCtrl] = WlanServiceDpModeCtrl,
		[kWlanCmdMockBmSyncDoorbell] = WlanServiceMockBmSyncDoorbell,
		[kWlanCmdLogSysCtrl] = WlanServiceLogSystemCtrl,
		[kWlanCmdPacketSnifferCtrl] = WlanServicePacketSnifferCtrl,
		[kWlanCmdShareInfoCtrl] = NULL,
		[kWlanCmdReportCtrl] = NULL,
		[kWlanCmdShellRequest] = WlanServiceShellRequest,
		[kWlanCmdPmStateNotify] = WlanServicePmStateNotify,
		[kWlanCmdPacketSnifferReset] = WlanServicePacketSnifferReset,
		[kWlanCmdUpdateUp2Flow] = WlanServiceUpdateUp2Flow,
		[kWlanCmdUpdateFlowIdLookUpEntry] = WlanServiceUpdateFlowIdLookUpEntry,
		[kWlanCmdRxHandoverSync] = WlanServiceRxHandoverSync,
	};

	WLAN_LOG_DEBUG(Cfg, "%s: Received command: cmd=%s(%d)", __func__,
		       GetWlanServiceCommandName(cmd), cmd);

	if (!svc) {
		ret = -EINVAL;
		goto end;
	}

	if (cmd >= kWlanCmdIdStart && cmd < kWlanCmdIdEnd) {
		if (kWlanCmdTable[cmd]) {
			ret = kWlanCmdTable[cmd](svc, msg, resp_buf_len, resp_buf);
		} else {
			WLAN_LOG_WARN(Cfg, "%s: No handler for command: cmd=%s(%d)", __func__,
				      GetWlanServiceCommandName(cmd), cmd);
			ret = 0;
		}
	} else {
		WLAN_LOG_ERROR(Cfg, "%s: Unknown command ID: cmd=%d", __func__, cmd);
		ret = -EINVAL;
	}
end:
	WlanServiceCommandResponse(svc, cmd, ret);
	return ret;
}

static void WlanServiceIdleDetectedCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);

	// WLAN_LOG_INFO(Dp, "%s(): TX idle detected", __func__);

	WlanServiceFsmEventHandler(&svc->fsm, kWlanServiceFsmEventDeactivate);
}

static void WlanServiceTxPrepareCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);

	WlanServiceFsmEventHandler(&svc->fsm, kWlanServiceFsmEventActivate);
}

static MailboxReturn WlanServiceDoorbellHandlerBmUpdate(int32_t UNUSED(irq), void *context)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, context);

	TriggerWlanDpBufferRefill(&svc->dp);

	return kMailboxIrqHandled;
}

static void WlanServiceStationInfoSyncTask(unsigned long context)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, context);
	struct StaInfo sta_info;
	struct NoaWlanStaInfo *sta_info_list = WLAN_REINTERPRET_CAST(
		struct NoaWlanStaInfo *,
		GetNoaWlanConfigSectionAddr(&svc->memory_map_helper, kStaInfo));
	uint32_t idx = 0;
	static const uint8_t kZeroMacAddr[kMacAddressLen] = { 0, 0, 0, 0, 0, 0 };

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, sta_info_list),
			   sizeof(struct NoaWlanStaInfo) * NOA_MAX_STA_SUPPORT);

	for (idx = 0; idx < NOA_MAX_STA_SUPPORT; idx++) {
		if (memcmp(sta_info_list[idx].mac_addr, kZeroMacAddr, kMacAddressLen) == 0) {
			continue;
		}

		memset(&sta_info, 0, sizeof(sta_info));

		sta_info.oif = sta_info_list[idx].oif;
		sta_info.bss_idx = sta_info_list[idx].bss_idx;
		sta_info.encrypt_type = sta_info_list[idx].encrypt_type;
		sta_info.encap_type = sta_info_list[idx].encap_type;
		sta_info.lmac_id = sta_info_list[idx].lmac_id;
		sta_info.bmid = sta_info_list[idx].bmid;
		sta_info.search_idx = sta_info_list[idx].search_idx;
		sta_info.search_type = sta_info_list[idx].search_type;
		sta_info.dscp_tid_map_id = sta_info_list[idx].dscp_tid_map_id;
		sta_info.addry_en = sta_info_list[idx].addry_en;
		sta_info.addrx_en = sta_info_list[idx].addrx_en;
		sta_info.enable = sta_info_list[idx].enable;
		sta_info.fw_metadata = sta_info_list[idx].fw_metadata;

		memcpy(sta_info.mac_addr, sta_info_list[idx].mac_addr, sizeof(sta_info.mac_addr));
		memcpy(sta_info.qos_txq_map, sta_info_list[idx].qos_txq_map,
		       sizeof(sta_info.qos_txq_map));

		if (sta_info.enable) {
			StaTableAddStation(&svc->sta_table, idx, &sta_info);
		} else {
			StaTableRemoveStation(&svc->sta_table, idx);
			memset(&sta_info_list[idx], 0, sizeof(sta_info_list[idx]));
			SysIfFlushDCache(WLAN_REINTERPRET_CAST(const PhyAddr, &sta_info_list[idx]),
					 sizeof(struct NoaWlanStaInfo));
		}
	}
	WlanServiceCommandResponse(svc, kDoorbellStationInfoSync, 0);
}

static MailboxReturn WlanServiceDoorbellHandlerStationInfoSync(int32_t UNUSED(irq), void *context)
{
	// TODO - b/424014614: Handle this operation in the dedicated control path task.
	static struct tasklet_struct task;
	static bool init = false;
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, context);

	if (svc->state != kWlanServiceStateStart) {
		WLAN_LOG_WARN(Cfg, "%s(): WlanService is not starting.", __func__);
		return kMailboxIrqHandled;
	}

	if (!init) {
		tasklet_init(&task, WlanServiceStationInfoSyncTask,
			     WLAN_REINTERPRET_CAST(uintptr_t, context));
		init = true;
	}

	tasklet_schedule(&task);
	return kMailboxIrqHandled;
}

static void WlanServiceWdevTxRingControlTask(unsigned long context)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, context);
	const struct NoaWlanWiFiRingConfig *ring_config = WLAN_REINTERPRET_CAST(
		struct NoaWlanWiFiRingConfig *,
		GetNoaWlanConfigSectionAddr(&svc->memory_map_helper, kWdevRingConfig));
	const struct NoaWlanRingInfo *ring_pool =
		GetNoaWLanWdevRingPool(&svc->memory_map_helper, kTxPostRingPool);
	uint32_t ring_count = 0;
	uint32_t i = 0;

	SysIfInvalidDCache(
		WLAN_REINTERPRET_CAST(PhyAddr, GetNoaWlanConfigSectionAddr(&svc->memory_map_helper,
									   kWdevRingConfig)),
		GetNoaWlanConfigSectionSize(&svc->memory_map_helper, kWdevRingConfig));

	ring_count = ring_config->wifi_ring_pool_config[kTxPostRingPool].count;
	for (i = 0; i < ring_count; i++) {
		if (ring_pool[i].is_active) {
			WlanRingManagerActivateRing(&svc->ring_manager, kWdevTxPostRingGroup, i);
		} else {
			WlanRingManagerDeactivateRing(&svc->ring_manager, kWdevTxPostRingGroup, i);
		}
	}
	WlanServiceCommandResponse(svc, kDoorbellTxRingInfoSync, 0);
}

static void WlanPacketSnifferResetTask(unsigned long UNUSED(context))
{
	WlanPacketSnifferReset();
}

static void WlanServicePcieOwnershipSwitchTask(unsigned long context)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, context);
	PhyAddr saved_state = WLAN_REINTERPRET_CAST(
		PhyAddr, GetNoaWlanPcieStateStoredAddress(&svc->memory_map_helper));
	PcieOwnership owner = GetNoaWlanPcieOwnership(&svc->memory_map_helper);

	if (owner == kPcieOwnershipAP) {
		SysIfSyncPcieConfig(saved_state, PCIE_STORED_STATE_BUF_WORD_SIZE, true);
		// TODO - b/443189086: D3/DS handshake & control ring preparation
	} else if (owner == kPcieOwnershipDPA) {
		// TODO - b/443189086: D3/DS handshake & Control ring preparation
		SysIfSyncPcieConfig(saved_state, PCIE_STORED_STATE_BUF_WORD_SIZE, false);
		// TODO - b/439968187: Use PCIe exposed API to get link status
	}

	WlanServiceCommandResponse(svc, kDoorbellPcieOwnershipSwitch, 0);
}

static MailboxReturn WlanServiceDoorbellHandlerTxRingInfoSync(int32_t UNUSED(irq), void *context)
{
	// TODO - b/424014614: Handle this operation in the dedicated control path task.
	static struct tasklet_struct task;
	static bool init = false;
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, context);

	if (svc->state != kWlanServiceStateStart) {
		WLAN_LOG_WARN(Cfg, "%s(): WlanService is not starting.", __func__);
		return kMailboxIrqHandled;
	}

	if (!init) {
		tasklet_init(&task, WlanServiceWdevTxRingControlTask,
			     WLAN_REINTERPRET_CAST(uintptr_t, context));
		init = true;
	}

	tasklet_schedule(&task);
	return kMailboxIrqHandled;
}

static MailboxReturn WlanServiceDoorbellHandlerPacketSnifferReset(int32_t UNUSED(irq),
								  void *context)
{
	// TODO - b/424014614: Handle this operation in the dedicated control path task.
	static struct tasklet_struct task;
	static bool init = false;

	if (!init) {
		tasklet_init(&task, WlanPacketSnifferResetTask,
			     WLAN_REINTERPRET_CAST(uintptr_t, context));
		init = true;
	}

	tasklet_schedule(&task);

	return kMailboxIrqHandled;
}

static MailboxReturn WlanServiceDoorbellHandlerPcieOwnershipSwitch(int32_t UNUSED(irq),
								   void *context)
{
	// TODO - b/424014614: Handle this operation in the dedicated control path task.
	static struct tasklet_struct task;
	static bool init = false;

	if (!init) {
		tasklet_init(&task, WlanServicePcieOwnershipSwitchTask,
			     WLAN_REINTERPRET_CAST(uintptr_t, context));
		init = true;
	}

	tasklet_schedule(&task);
	return kMailboxIrqHandled;
}

static int32_t WlanServiceStateExitEventInitCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);
	NoaRingSvcInitParams ring_svc_init_params;
	WlanDpInitParams dp_init_params;
	uint64_t chip_type;
	uint32_t rx_buffer_size;
	uint32_t rx_pkt_tlv_size;
	void *pcie_stored_state;

	if (svc->state != kWlanServiceStatePlatInit) {
		WLAN_LOG_ERROR(Cfg, "%s(): Platform is not initialized.", __func__);
		return -ENODEV;
	}

	if (WlanGlobalConfigRead(&svc->memory_map_helper, kRxPacketNum, sizeof(rx_buffer_size),
				 &rx_buffer_size) != 0) {
		goto BM_INIT_FAILED;
	}

	pcie_stored_state = GetNoaWlanPcieStateStoredAddress(&svc->memory_map_helper);
	if (pcie_stored_state == NULL) {
		WLAN_LOG_ERROR(Cfg, "%s(): PCIE stored state is not set.", __func__);
		return -EINVAL;
	} else {
		SysIfSetPcieStoredState(pcie_stored_state);
	}

	if (WlanServicePmInitWrapper(svc) != 0) {
		goto PM_INIT_FAILED;
	}

	if (BmInit(kVendorRxBufferManager)) {
		WLAN_LOG_ERROR(Cfg, "%s(): kVendorRxBufferManager init failed.", __func__);
		goto BM_INIT_FAILED;
	}

	if (BmInit(kNoaTxBufferManager)) {
		WLAN_LOG_ERROR(Cfg, "%s(): kNoaTxBufferManager init failed.", __func__);
		BmDeinit(kVendorRxBufferManager);
		goto BM_INIT_FAILED;
	}

	if (StaTableInit(&svc->sta_table, &svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): StaTableInit failed.", __func__);
		goto STA_TABLE_INIT_FAILED;
	}

	if (WlanNepBufferPoolInit(&svc->nep_tx_buffer_pool.base, kNepTxBufferPool) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanNepBufferPoolInit failed.", __func__);
		goto WLAN_NEP_BUFFER_INIT_FAILED;
	}

	memset(&ring_svc_init_params, 0, sizeof(ring_svc_init_params));
	ring_svc_init_params.ext_svc = &svc->ext_svc;
	if (NoaRingSvcInit(&svc->ring_svc, &ring_svc_init_params) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingSvcInit init failed.", __func__);
		goto NOA_RING_SVC_INIT_FAILED;
	}

	if (WlanRingManagerInit(&svc->ring_manager) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanWdevRingManager init failed.", __func__);
		goto WLAN_RING_MANAGER_INIT_FAILED;
	}

	if (NepRingManagerAttachRings(svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NepRingManagerAttachRings init failed.", __func__);
		goto NEP_RING_ATTACH_INIT_FAILED;
		;
	}

	if (WlanGlobalConfigRead(&svc->memory_map_helper, kChipType, sizeof(chip_type),
				 &chip_type) != 0) {
		goto WDEV_IF_INIT_FAILED;
	}

	if (WlanGlobalConfigRead(&svc->memory_map_helper, kRxPktTlvSize, sizeof(rx_pkt_tlv_size),
				 &rx_pkt_tlv_size) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): rx_pkt_tlv_size read failed.", __func__);
		goto WDEV_IF_INIT_FAILED;
	}

	if (WdevIfInit(&svc->wdev_if, WLAN_STATIC_CAST(WlanDeviceChipId, chip_type),
		       rx_pkt_tlv_size, &svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): WdevIfInit failed.", __func__);
		goto WDEV_IF_INIT_FAILED;
	}

	FlowIdTableInit(&svc->flow_id_table, &svc->ext_svc);

	// Attach interrupt/mailbox
	memset(&dp_init_params, 0, sizeof(WlanDpInitParams));
	dp_init_params.num_ring_svc_irq = 1;
	dp_init_params.ring_svc_irq_info[0].rx_data_ring_polling_mask = 0x1;

	dp_init_params.ring_manager = &svc->ring_manager;
	dp_init_params.wdev_if = &svc->wdev_if;
	dp_init_params.sta_table = &svc->sta_table;
	dp_init_params.flow_id_table = &svc->flow_id_table;
	dp_init_params.nep_tx_buffer_pool = &svc->nep_tx_buffer_pool.base;
	dp_init_params.ext_svc = &svc->ext_svc;
	dp_init_params.mode = kWlanDpNormalMode;
	dp_init_params.idle_detected_callback = WlanServiceIdleDetectedCb;
	dp_init_params.idle_detected_ctx = svc;
	dp_init_params.tx_prepare_callback = WlanServiceTxPrepareCb;
	dp_init_params.tx_prepare_ctx = svc;

	if (WlanDpInit(&svc->dp, &dp_init_params) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanDpInit failed.", __func__);
		goto DP_INIT_FAILED;
	}

	WlanPacketSnifferInit(
		&svc->ext_svc,
		WLAN_REINTERPRET_CAST(uint64_t, GetNoaWlanConfigSectionAddr(&svc->memory_map_helper,
									    kWitPacketSniffer)),
		GetNoaWlanConfigSectionSize(&svc->memory_map_helper, kWitPacketSniffer));
	WlanLogSetDramCtrl(
		WLAN_REINTERPRET_CAST(uint64_t, GetNoaWlanConfigSectionAddr(&svc->memory_map_helper,
									    kWitLogSys)),
		GetNoaWlanConfigSectionSize(&svc->memory_map_helper, kWitLogSys));

	if (SysIfRegisterMailbox(kNcpWifiMailboxTypeAp, kDoorbellBmUpdate,
				 WlanServiceDoorbellHandlerBmUpdate, svc)) {
		WLAN_LOG_ERROR(Cfg, "%s(): register mailbox failed.", __func__);
		goto DP_INIT_FAILED;
	}

	if (SysIfRegisterMailbox(kNcpWifiMailboxTypeAp, kDoorbellStationInfoSync,
				 WlanServiceDoorbellHandlerStationInfoSync, svc)) {
		WLAN_LOG_ERROR(Cfg, "%s(): register mailbox failed.", __func__);
		goto DP_INIT_FAILED;
	}

	if (SysIfRegisterMailbox(kNcpWifiMailboxTypeAp, kDoorbellTxRingInfoSync,
				 WlanServiceDoorbellHandlerTxRingInfoSync, svc)) {
		WLAN_LOG_ERROR(Cfg, "%s(): register mailbox failed.", __func__);
		goto DP_INIT_FAILED;
	}

	if (SysIfRegisterMailbox(kNcpWifiMailboxTypeAp, kDoorbellPacketSnifferReset,
				 WlanServiceDoorbellHandlerPacketSnifferReset, svc)) {
		WLAN_LOG_ERROR(Cfg, "%s(): register mailbox failed.", __func__);
		goto DP_INIT_FAILED;
	}

	if (SysIfRegisterMailbox(kNcpWifiMailboxTypeAp, kDoorbellPcieOwnershipSwitch,
				 WlanServiceDoorbellHandlerPcieOwnershipSwitch, svc)) {
		WLAN_LOG_ERROR(Cfg, "%s(): register mailbox failed.", __func__);
		goto DP_INIT_FAILED;
	}

	svc->state = kWlanServiceStateReady;

	return 0;

DP_INIT_FAILED:
	WdevIfDeinit(&svc->wdev_if);
WDEV_IF_INIT_FAILED:
NEP_RING_ATTACH_INIT_FAILED:
	WlanRingManagerDeinit(&svc->ring_manager);
WLAN_RING_MANAGER_INIT_FAILED:
	NoaRingSvcDeinit(&svc->ring_svc);
NOA_RING_SVC_INIT_FAILED:
	WlanNepBufferPoolDeinit(&svc->nep_tx_buffer_pool.base);
WLAN_NEP_BUFFER_INIT_FAILED:
	StaTableDeinit(&svc->sta_table);
STA_TABLE_INIT_FAILED:
	WlanPmDeinit(&svc->pm_iface);
PM_INIT_FAILED:
	BmDeinit(kNoaTxBufferManager);
	BmDeinit(kVendorRxBufferManager);
BM_INIT_FAILED:
	return -ENODEV;
}

static int32_t WlanServiceStateStopEventExitCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);

	if (svc->state != kWlanServiceStateReady) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanService is not ready.", __func__);
		return -ENODEV;
	}
	svc->state = kWlanServiceStatePlatInit;
	WlanDpDeinit(&svc->dp);
	WdevIfDeinit(&svc->wdev_if);
	FlowIdTableDeinit(&svc->flow_id_table);
	WlanRingManagerDeinit(&svc->ring_manager);
	WlanNepBufferPoolDeinit(&svc->nep_tx_buffer_pool.base);
	StaTableDeinit(&svc->sta_table);
	WlanPmDeinit(&svc->pm_iface);
	MemoryMapHelperDeinit(&svc->memory_map_helper);
	BmDeinit(kNoaTxBufferManager);
	BmDeinit(kVendorRxBufferManager);
	return 0;
}

static int32_t WlanServiceStateStopEventStartCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);
	uint64_t doorbell_addr;
	uint64_t fw_trap_addr;

	if (svc->state != kWlanServiceStateReady) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanService is not ready.", __func__);
		return -ENODEV;
	}

	if (WlanGlobalConfigRead(&svc->memory_map_helper, kDoorbellAddr, sizeof(doorbell_addr),
				 &doorbell_addr) != 0) {
		return -ENODEV;
	}
	WdevIfSetDoorbellAddr(&svc->wdev_if, doorbell_addr);

	if (WlanGlobalConfigRead(&svc->memory_map_helper, kFwTrapAddr, sizeof(fw_trap_addr),
				 &fw_trap_addr) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): Failed to read kFwTrapAddr.", __func__);
		return -ENODEV;
	}
	WlanDpSetFwTrapAddr(&svc->dp, WLAN_STATIC_CAST(uint64_t, fw_trap_addr));
	if (svc->wdev_if.chip_id == kWlanDeviceChipIdWcn7760) {
		svc->wdev_if.cookie_base_addr = (uint32_t)fw_trap_addr;
	}

	// Update Wdev rings from APC
	WlanServiceWdevRingUpdate(svc, WLAN_STATIC_CAST(uint32_t, kTxPostRingPool));
	WlanServiceWdevRingUpdate(svc, WLAN_STATIC_CAST(uint32_t, kRxCmplRingPool));
	WlanServiceWdevRingUpdate(svc, WLAN_STATIC_CAST(uint32_t, kTxCmplRingPool));
	WlanServiceWdevRingUpdate(svc, WLAN_STATIC_CAST(uint32_t, kRxPostRingPool));
	ApcRingManagerAttachRings(svc);

	if (ExtSvcActivateNepTxBufferPool(&svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): ExtSvcActivateNepTxBufferPool failed.", __func__);
		return -ENODEV;
	}

	if (ExtSvcActivateWlanFwRing(&svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): ExtSvcActivateWlanFwRing failed.", __func__);
		return -ENODEV;
	}

	WlanDpStart(&svc->dp);
	WlanDpTriggerDataPathPoll(&svc->dp);
	svc->state = kWlanServiceStateStart;
	return 0;
}

static int32_t WlanServiceStatePassiveEventStopCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);

	if (svc->state != kWlanServiceStateStart) {
		WLAN_LOG_ERROR(Cfg, "%s(): WlanService is not starting.", __func__);
		return -ENODEV;
	}

	if (ExtSvcDeactivateWlanFwOutputRing(&svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): ExtSvcDeactivateWlanFwOutputRing failed.", __func__);
	}

	WlanDpDrainOutputRingAsync(&svc->dp, kDefaultDrainTimeoutMs);

	WlanDpStop(&svc->dp);

	if (ExtSvcDeactivateWlanFwInputRing(&svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): ExtSvcDeactivateWlanFwInputRing failed.", __func__);
	}

	WlanDpIrqDisable(&svc->dp);

	if (ExtSvcDeactivateNepTxBufferPool(&svc->ext_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): ExtSvcDeactivateNepTxBufferPool failed.", __func__);
		return -ENODEV;
	}

	WlanServiceWdevRingSync(svc, WLAN_STATIC_CAST(uint32_t, kTxPostRingPool));
	WlanServiceWdevRingSync(svc, WLAN_STATIC_CAST(uint32_t, kRxCmplRingPool));
	WlanServiceWdevRingSync(svc, WLAN_STATIC_CAST(uint32_t, kTxCmplRingPool));
	WlanServiceWdevRingSync(svc, WLAN_STATIC_CAST(uint32_t, kRxPostRingPool));
	svc->state = kWlanServiceStateReady;
	return 0;
}

static int32_t WlanServiceStatePassiveEventActivateCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);

	VoteBusPower(svc->pm_iface);

	WlanDpIdleDetectionStart(&svc->dp);

	return 0;
}

static int32_t WlanServiceStateActiveEventDeactivateCb(void *ctx)
{
	WlanService *svc = WLAN_REINTERPRET_CAST(WlanService *, ctx);

	WlanDpIdleDetectionStop(&svc->dp);

	DevoteBusPower(svc->pm_iface);

	return 0;
}

int32_t WlanServicePlatInit(WlanService *const svc,
			    const WlanServicePlatformInitParams *const params)
{
	ExternalServicesInitParams ext_svc_init_params;

	if (!svc || !params) {
		return -EINVAL;
	}

	WlanLogInit();
	WlanServiceFsmInit(&svc->fsm);
	WlanServiceFsmSetEventCallback(&svc->fsm, kWlanServiceFsmStateExit,
				       kWlanServiceFsmEventInit, WlanServiceStateExitEventInitCb,
				       WLAN_REINTERPRET_CAST(void *, svc));
	WlanServiceFsmSetEventCallback(&svc->fsm, kWlanServiceFsmStateStop,
				       kWlanServiceFsmEventExit, WlanServiceStateStopEventExitCb,
				       WLAN_REINTERPRET_CAST(void *, svc));
	WlanServiceFsmSetEventCallback(&svc->fsm, kWlanServiceFsmStateStop,
				       kWlanServiceFsmEventStart, WlanServiceStateStopEventStartCb,
				       WLAN_REINTERPRET_CAST(void *, svc));
	WlanServiceFsmSetEventCallback(&svc->fsm, kWlanServiceFsmStatePassive,
				       kWlanServiceFsmEventStop, WlanServiceStatePassiveEventStopCb,
				       WLAN_REINTERPRET_CAST(void *, svc));
	WlanServiceFsmSetEventCallback(&svc->fsm, kWlanServiceFsmStatePassive,
				       kWlanServiceFsmEventActivate,
				       WlanServiceStatePassiveEventActivateCb,
				       WLAN_REINTERPRET_CAST(void *, svc));
	WlanServiceFsmSetEventCallback(&svc->fsm, kWlanServiceFsmStateActive,
				       kWlanServiceFsmEventDeactivate,
				       WlanServiceStateActiveEventDeactivateCb,
				       WLAN_REINTERPRET_CAST(void *, svc));

	memset(&ext_svc_init_params, 0, sizeof(ext_svc_init_params));
	ext_svc_init_params.send_event_to_apc = params->send_event_to_apc;
	ext_svc_init_params.activate_wlan_fw_ring = params->nep_ring_activate;
	ext_svc_init_params.deactivate_wlan_fw_input_ring = params->nep_input_ring_deactivate;
	ext_svc_init_params.deactivate_wlan_fw_output_ring = params->nep_output_ring_deactivate;
	ext_svc_init_params.activate_nep_tx_buffer_pool = params->nep_ring_buffer_pool_activate;
	ext_svc_init_params.deactivate_nep_tx_buffer_pool = params->nep_ring_buffer_pool_deactivate;
	ext_svc_init_params.send_command_to_net_engine = params->send_command_to_net_engine;
	ext_svc_init_params.noa_power_vote = params->noa_power_vote;

	if (ExtSvcInit(&svc->ext_svc, &ext_svc_init_params) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): ExtSvcInit failed.", __func__);
		return -ENODEV;
	}

	svc->state = kWlanServiceStatePlatInit;

	return 0;
}
