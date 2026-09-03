#include "memory_map_helper.h"

#include "sys_if/types/types.h"
#include "sys_if/memory/sys_if_memory.h"
#include <common/wlan/noa_wlan.h>
#include "wlan_log/wlan_log.h"
#include "modules/noa_ring_svc/noa_ring_svc.h"

struct NoaWlanRingInfo *GetNoaWLanWdevRingPool(MemoryMapHelper *helper, uint32_t ring_type)
{
	struct NoaWlanWiFiRingConfig *wifi_ring_config =
		WLAN_REINTERPRET_CAST(struct NoaWlanWiFiRingConfig *,
				      GetNoaWlanConfigSectionAddr(helper, kWdevRingConfig));

	switch (ring_type) {
	case RING_TYPE_TX_DATA:
		return wifi_ring_config->wifi_tx_rings_info;
	case RING_TYPE_RX_DATA:
		return wifi_ring_config->wifi_rx_rings_info;
	case RING_TYPE_TX_CPL:
		return wifi_ring_config->wifi_tx_cpl_rings_info;
	case RING_TYPE_RX_POST:
		return wifi_ring_config->wifi_rx_post_rings_info;
	default:
		return NULL;
	}
}

static inline struct NoaWlanRingInfo *GetNoaWlanNepRingPool(MemoryMapHelper *helper,
							    uint32_t ring_type)
{
	struct NoaWlanNepRingConfig *nep_ring_config = WLAN_REINTERPRET_CAST(
		struct NoaWlanNepRingConfig *, GetNoaWlanConfigSectionAddr(helper, kNepRingConfig));

	switch (ring_type) {
	case RING_TYPE_TX_DATA:
		return nep_ring_config->nep_tx_rings_info;
	case RING_TYPE_RX_DATA:
		return nep_ring_config->nep_rx_rings_info;
	case RING_TYPE_TX_CPL:
		return nep_ring_config->nep_txcpl_rings_info;
	case RING_TYPE_RX_POST:
		return nep_ring_config->nep_rxpost_rings_info;
	default:
		return NULL;
	}
}

static void NoaWlanRingPoolInit(MemoryMapHelper *helper)
{
	struct NoaWlanNepRingConfig *nep_ring_config = WLAN_REINTERPRET_CAST(
		struct NoaWlanNepRingConfig *, GetNoaWlanConfigSectionAddr(helper, kNepRingConfig));

	if (nep_ring_config == NULL) {
		return;
	}

	nep_ring_config->nep_ring_pool_config[RING_TYPE_TX_DATA].count = NUM_RING_SVC_TX_POST_RING;
	nep_ring_config->nep_ring_pool_config[RING_TYPE_RX_DATA].count = NUM_RING_SVC_RX_POST_RING;
	nep_ring_config->nep_ring_pool_config[RING_TYPE_TX_CPL].count = NUM_RING_SVC_TX_CMPL_RING;
	nep_ring_config->nep_ring_pool_config[RING_TYPE_RX_POST].count = NUM_RING_SVC_RX_POST_RING;
}

int32_t MemoryMapHelperInit(MemoryMapHelper *helper, const MemoryMapHelperInitParams *params)
{
	if (params->base_addr == 0 || params->size < sizeof(NoaWlanSharedInfo)) {
		return -EINVAL;
	}

	helper->base_addr = params->base_addr;
	helper->size = params->size;

	NoaWlanRingPoolInit(helper);
	return 0;
}

void MemoryMapHelperDeinit(MemoryMapHelper *helper)
{
	memset(helper, 0, sizeof(MemoryMapHelper));
}

void *GetNoaWlanConfigSectionAddr(MemoryMapHelper *helper, enum NoaWlanConfigSection section)
{
	switch (section) {
	case kGlobalConfig:
		return WLAN_REINTERPRET_CAST(void *, helper->base_addr + offsetof(NoaWlanSharedInfo,
										  global_config));
	case kWdevRingConfig:
		return WLAN_REINTERPRET_CAST(
			void *, helper->base_addr + offsetof(NoaWlanSharedInfo, wifi_ring_config));
	case kNepRingConfig:
		return WLAN_REINTERPRET_CAST(void *, helper->base_addr + offsetof(NoaWlanSharedInfo,
										  nep_ring_config));
	case kStaInfo:
		return WLAN_REINTERPRET_CAST(void *, helper->base_addr +
							     offsetof(NoaWlanSharedInfo, sta_info));
	case kPmStateInfo:
		return WLAN_REINTERPRET_CAST(void *, helper->base_addr + offsetof(NoaWlanSharedInfo,
										  pm_state_info));
	case kIntrStateInfo:
		return WLAN_REINTERPRET_CAST(void *,
					     helper->base_addr + offsetof(NoaWlanSharedInfo,
									  interrupt_state_info));
	case kMibInfo:
		return WLAN_REINTERPRET_CAST(void *,
					     helper->base_addr + offsetof(NoaWlanSharedInfo, mib));
	case kWitLogSys:
		return WLAN_REINTERPRET_CAST(void *,
					     helper->base_addr + offsetof(NoaWlanSharedInfo,
									  wit_shared_log_sys_addr));
	case kWitPacketSniffer:
		return WLAN_REINTERPRET_CAST(void *,
					     helper->base_addr + offsetof(NoaWlanSharedInfo,
									  wit_shared_packet_addr));
	case kBufferMgmt:
		return WLAN_REINTERPRET_CAST(
			void *, helper->base_addr + offsetof(NoaWlanSharedInfo,
							     buffer_management_shared_memory));
	default:
		return NULL;
	}
}

uint32_t GetNoaWlanConfigSectionSize(MemoryMapHelper *helper, enum NoaWlanConfigSection section)
{
	switch (section) {
	case kGlobalConfig:
		return sizeof(NoaGlobalConfig);
	case kWdevRingConfig:
		return sizeof(NoaWlanWiFiRingConfig);
	case kNepRingConfig:
		return sizeof(NoaWlanNepRingConfig);
	case kStaInfo:
		return sizeof(NoaWlanStaInfo);
	case kPmStateInfo:
		return sizeof(NoaPmStateInfo);
	case kIntrStateInfo:
		return sizeof(NoaInterruptStateInfo);
	case kMibInfo:
		return sizeof(NoaMib);
	case kWitLogSys:
		return WIFI_DBG_LOG_SYS_DRAM_SIZE;
	case kWitPacketSniffer:
		return WIFI_DBG_PACKET_DRAM_SIZE;
	case kBufferMgmt:
		return sizeof(NoaBufferManagementSharedMemory);
	default:
		return 0;
	}
}

int32_t MemoryMapHelperRead(const MemoryMapHelper *helper, uint32_t offset, size_t size, void *out)
{
	if (offset >= helper->size) {
		return -EINVAL;
	}

	if (helper->base_addr == 0) {
		return -ENODATA;
	}

	SysIfInvalidDCache(WLAN_STATIC_CAST(const PhyAddr, helper->base_addr + offset), size);
	memcpy(out, WLAN_REINTERPRET_CAST(const void *, helper->base_addr + offset), size);

	return 0;
}

#define MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, field, buffer_size, output_buffer)            \
	do {                                                                                       \
		uint32_t base = offsetof(NoaWlanSharedInfo, global_config);                        \
		size_t field_size = sizeof(((NoaGlobalConfig *)0)->field);                         \
		if (buffer_size < field_size) {                                                    \
			WLAN_LOG_ERROR(Cfg, "%s(): Invalid buffer size: %" PRIu32, __func__,       \
				       buffer_size);                                               \
			break;                                                                     \
		}                                                                                  \
		if (MemoryMapHelperRead(helper, base + offsetof(NoaGlobalConfig, field),           \
					field_size,                                                \
					WLAN_REINTERPRET_CAST(void *, output_buffer)) != 0) {      \
			return -ENODATA;                                                           \
		}                                                                                  \
	} while (0)

int32_t WlanGlobalConfigRead(const MemoryMapHelper *helper, GlobalConfigField field,
			     uint32_t buffer_size, void *output_buffer)
{
	switch (field) {
	case kChipType:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, chip_type, buffer_size, output_buffer);
		break;
	case kRxPacketNum:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, rx_pkt_max, buffer_size,
						     output_buffer);
		break;
	case kRxBufferSize:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, rx_buf_size, buffer_size,
						     output_buffer);
		break;
	case kVendorTxPacketIdMax:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, tx_pkt_max, buffer_size,
						     output_buffer);
		break;
	case kNoaTxPacketNum:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, tx_bm_size, buffer_size,
						     output_buffer);
		break;
	case kShareAddress:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, share_addr, buffer_size,
						     output_buffer);
		break;
	case kWdevRegAddress:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, reg_addr, buffer_size, output_buffer);
		break;
	case kShareSize:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, share_size, buffer_size,
						     output_buffer);
		break;
	case kWdevRegSize:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, reg_size, buffer_size, output_buffer);
		break;
	case kIntsAddr:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, ints_addr, buffer_size, output_buffer);
		break;
	case kIntmAddr:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, intm_addr, buffer_size, output_buffer);
		break;
	case kDoorbellAddr:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, doorbell_addr, buffer_size,
						     output_buffer);
		break;
	case kFwTrapAddr:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, fw_trap_addr, buffer_size,
						     output_buffer);
		break;
	case kRxPktTlvSize:
		MEMORY_MAP_HELPER_GLOBAL_CONFIG_READ(helper, rx_pkt_tlv_size, buffer_size,
						     output_buffer);
		break;
	default:
		return -ENODATA;
	}

	return 0;
}

static struct NoaMib *GetNoaWlanMIBAddr(MemoryMapHelper *helper)
{
	struct NoaWlanSharedInfo *shared_info =
		WLAN_REINTERPRET_CAST(struct NoaWlanSharedInfo *, helper->base_addr);

	if (shared_info == NULL) {
		return NULL;
	}
	return WLAN_REINTERPRET_CAST(struct NoaMib *, &shared_info->mib);
}

struct NoaWlanNepRingConfig *UpdateNoaWlanNepRingConfig(MemoryMapHelper *helper,
							const WlanRing *nep_ring, uint32_t ring_idx,
							uint32_t ring_type)
{
	struct NoaWlanNepRingConfig *nep_ring_config = WLAN_REINTERPRET_CAST(
		struct NoaWlanNepRingConfig *, GetNoaWlanConfigSectionAddr(helper, kNepRingConfig));
	struct NoaWlanRingInfo *ring_info_base;
	struct NoaWlanRingInfo *ring_info;

	if (nep_ring_config == NULL || ring_type >= MAX_RING_TYPE_NUM ||
	    ring_idx >= NoaWlanGetMaxRingNum(ring_type)) {
		return NULL;
	}

	ring_info_base = GetNoaWlanNepRingPool(helper, ring_type);
	ring_info = &ring_info_base[ring_idx];

	ring_info->regs.base = nep_ring->regs.base;
	ring_info->regs.len = nep_ring->regs.len;
	ring_info->regs.max_item = nep_ring->regs.max_item;
	ring_info->regs.read = nep_ring->regs.read;
	ring_info->regs.write = nep_ring->regs.write;
	ring_info->stride = 1;
	ring_info->ndesc = nep_ring->ndesc;
	ring_info->desc_sz = nep_ring->ndesc;
	ring_info->dma_va = WLAN_REINTERPRET_CAST(uint32_t, nep_ring->desc_dma);
	ring_info->dma_pa = WLAN_REINTERPRET_CAST(uint32_t, nep_ring->desc_dma);
	strncpy(ring_info->name, nep_ring->name, sizeof(ring_info->name));

	SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, nep_ring_config),
			 sizeof(NoaWlanNepRingConfig));

	return nep_ring_config;
}

struct NoaMib *UpdateNoaWlanMIBInfo(MemoryMapHelper *helper, WlanDpStats *const dp_stats)
{
	struct NoaMib *mib = GetNoaWlanMIBAddr(helper);
	uint32_t i;

	if (mib == NULL) {
		return NULL;
	}

	mib->total_rx_pkt_cnt = dp_stats->rx;
	mib->total_rx_forward_pkt_cnt = dp_stats->rx_forward;
	mib->total_rx_pkt_err_cnt = dp_stats->rx_err;
	mib->total_rx_pkt_byte = 0;
	mib->total_tx_pkt_cnt = dp_stats->tx;
	mib->total_tx_forward_pkt_cnt = dp_stats->tx_forward;
	mib->total_tx_pkt_err_cnt = dp_stats->tx_err;
	mib->total_tx_pkt_byte = 0;
	mib->total_tx_cpl_cnt = dp_stats->tx_cpl;
	mib->total_tx_cpl_err_cnt = dp_stats->tx_cpl_err;
	mib->total_rx_replenish_cnt = dp_stats->rx_replenish;
	mib->total_rx_replenish_err_cnt = dp_stats->rx_replenish_err;
	mib->total_rxbm_sync_cnt = dp_stats->rxbm_sync;
	mib->total_txbm_sync_cnt = dp_stats->txbm_sync;
	mib->nep_fw_input_ring_pkt_cnt = dp_stats->nep_fw_input;
	mib->device_rx_ring_pkt_cnt = dp_stats->dev_rx;
	mib->device_txcpl_ring_pkt_cnt = dp_stats->dev_txcpl;
	mib->device_rxrefill_ring_pkt_cnt = dp_stats->dev_rx_replenish;

	for (i = 0; i < MAX_TX_RINGS_NUM; i++) {
		mib->nep_fw_output_ring_pkt_cnt[i] = dp_stats->nep_fw_output[i];
		mib->device_tx_ring_pkt_cnt[i] = dp_stats->dev_tx[i];
	}
	SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, mib), sizeof(struct NoaMib));

	return mib;
}

void UpdateNoaWlanInterruptCounter(MemoryMapHelper *helper, uint32_t int_cnt, NoaInterruptType type)
{
	struct NoaInterruptStateInfo *intr_state_info =
		WLAN_REINTERPRET_CAST(struct NoaInterruptStateInfo *,
				      GetNoaWlanConfigSectionAddr(helper, kIntrStateInfo));

	if (intr_state_info == NULL) {
		return;
	}

	intr_state_info->interrupt_cnt[type] = int_cnt;
	SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, intr_state_info),
			 sizeof(NoaInterruptStateInfo));
}

const struct NoaWlanStaInfo *GetNoaWlanStaInfoAddr(MemoryMapHelper *helper)
{
	struct NoaWlanSharedInfo *shared_info =
		WLAN_REINTERPRET_CAST(struct NoaWlanSharedInfo *, helper->base_addr);

	if (shared_info == NULL) {
		return NULL;
	}
	return WLAN_REINTERPRET_CAST(struct NoaWlanStaInfo *, &shared_info->sta_info);
}

void *GetNoaWlanPcieStateStoredAddress(MemoryMapHelper *helper)
{
	struct NoaWlanSharedInfo *shared_info =
		WLAN_REINTERPRET_CAST(struct NoaWlanSharedInfo *, helper->base_addr);

	if (shared_info) {
		return WLAN_REINTERPRET_CAST(void *, shared_info->pm_state_info.pcie_stored_state);
	}

	return NULL;
}

int32_t UpdateNoaWlanPciDevState(MemoryMapHelper *helper, PciDevReconcileState type, int32_t value)
{
	struct NoaPmStateInfo *pm_state_info = WLAN_REINTERPRET_CAST(
		struct NoaPmStateInfo *, GetNoaWlanConfigSectionAddr(helper, kPmStateInfo));
	if (pm_state_info == NULL) {
		return -EINVAL;
	}

	switch (type) {
	case kIsBusmaster:
		pm_state_info->PCIe_is_busmaster = value;
		break;
	case kEnableCnt:
		pm_state_info->PCIe_enable_cnt = value;
		break;
	case kCurrentState:
		pm_state_info->PCIe_current_state = value;
		break;
	case kPmState:
		pm_state_info->PCIe_pm_state = value;
		break;
	default:
		WLAN_LOG_ERROR(Cfg, "Invalid PCIe state type: %d", type);
		return -EINVAL;
	}

	return 0;
}

PcieOwnership GetNoaWlanPcieOwnership(MemoryMapHelper *helper)
{
	struct NoaPmStateInfo *pm_state_info = WLAN_REINTERPRET_CAST(
		struct NoaPmStateInfo *, GetNoaWlanConfigSectionAddr(helper, kPmStateInfo));
	if (pm_state_info == NULL) {
		return kPcieOwnershipNum;
	}

	return WLAN_STATIC_CAST(PcieOwnership, pm_state_info->PCIe_link_owner);
}
