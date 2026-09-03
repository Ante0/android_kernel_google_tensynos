#ifndef CORE_WLAN_SHARED_MEM_MEMORY_MAP_HELPER_H
#define CORE_WLAN_SHARED_MEM_MEMORY_MAP_HELPER_H

#include <common/wlan/noa_wlan.h>
#include "core/dp/wlan_dp.h"
#include "wlan_log/wlan_log.h"
#include "wlan_cast.h"
#include "sys_if/memory/sys_if_memory.h"
#include "modules/noa_ring_svc/noa_ring_svc.h"
#include "modules/memory_map/memory_map.h"
#include "modules/wlan_ring_manager/wlan_nep_ring_manager.h"
#include "modules/wlan_buffer_management/buffer_management_table.h"

#define MEMORY_MAP_SECTION_VALUE(struct_type, struct_ptr, field_name, field_type_fmt)              \
	do {                                                                                       \
		WLAN_LOG_INFO(Shell, "(0x%08lx) %s value: %" field_type_fmt "\n",                  \
			      offsetof(struct_type, field_name), #field_name,                      \
			      (struct_ptr)->field_name);                                           \
	} while (0)

#define MEMORY_MAP_SECTION_HEX64_VALUE(struct_type, struct_ptr, field_name)                        \
	do {                                                                                       \
		WLAN_LOG_INFO(Shell, "(0x%08lx) %s value upper: %x lower: %x\n",                   \
			      offsetof(struct_type, field_name), #field_name,                      \
			      (u32)((struct_ptr)->field_name >> 32),                               \
			      (u32)((struct_ptr)->field_name & 0xFFFFFFFF));                       \
	} while (0)

#define MEMORY_MAP_SECTION_U32_ARRAY_VALUE(struct_type, struct_ptr, field_name, idx)               \
	do {                                                                                       \
		WLAN_LOG_INFO(Shell, "(0x%08lx) %s[%d] value: %" PRIu32 "\n",                      \
			      offsetof(struct_type, field_name) + sizeof(uint32_t) * idx,          \
			      #field_name, idx, (struct_ptr)->field_name[idx]);                    \
	} while (0)

#define MEMORY_MAP_SECTION_RING_CONFIG_VALUE(ring_pool, i)                                         \
	do {                                                                                       \
		MEMORY_MAP_SECTION_HEX64_VALUE(struct noa_ring_regs, &(ring_pool[i].regs), base);  \
		MEMORY_MAP_SECTION_HEX64_VALUE(struct noa_ring_regs, &(ring_pool[i].regs), len);   \
		MEMORY_MAP_SECTION_HEX64_VALUE(struct noa_ring_regs, &(ring_pool[i].regs),         \
					       max_item);                                          \
		MEMORY_MAP_SECTION_HEX64_VALUE(struct noa_ring_regs, &(ring_pool[i].regs), read);  \
		MEMORY_MAP_SECTION_HEX64_VALUE(struct noa_ring_regs, &(ring_pool[i].regs), write); \
		MEMORY_MAP_SECTION_HEX64_VALUE(struct noa_ring_regs, &(ring_pool[i].regs),         \
					       dpa_base);                                          \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), ndesc, PRIu32);         \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), desc_sz, PRIu32);       \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), offset, PRIu32);        \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), sn, PRIu32);            \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), hw_idx, PRIu32);        \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), stride, PRIu32);        \
		MEMORY_MAP_SECTION_VALUE(NoaWlanRingInfo, &(ring_pool[i]), name, "s");             \
	} while (0)

typedef enum GlobalConfigField {
	kChipType = 0,
	kRxPacketNum,
	kRxBufferSize,
	kVendorTxPacketIdMax,
	kNoaTxPacketNum,
	kShareAddress,
	kWdevRegAddress,
	kShareSize,
	kWdevRegSize,
	kIntsAddr,
	kIntmAddr,
	kRxPktTlvSize,
	kDoorbellAddr,
	kFwTrapAddr,
} GlobalConfigField;

typedef enum PciDevReconcileState {
	kPciDeReconileStateNone = 0,
	kIsBusmaster,
	kEnableCnt,
	kCurrentState,
	kPmState,
} PciDevReconcileState;

typedef enum PcieOwnership {
	kPcieOwnershipAP,
	kPcieOwnershipDPA,
	kPcieOwnershipNum,
} PcieOwnership;

typedef struct MemoryMapHelper {
	/// @brief NOA WLAN shared memory address
	uint64_t base_addr;
	/// @brief NOA WLAN shared memory size
	uint32_t size;
} MemoryMapHelper;

typedef struct MemoryMapHelperInitParams {
	/// @brief NOA WLAN shared memory address
	uint64_t base_addr;
	/// @brief NOA WLAN shared memory size
	uint32_t size;
} MemoryMapHelperInitParams;

/// @brief Initializes the memory map helper.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] params Pointer to the initialization parameters.
///
/// @return 0 on success, negative error code on failure.

int32_t MemoryMapHelperInit(MemoryMapHelper *helper, const MemoryMapHelperInitParams *params);

/// @brief Deinitializes the memory map helper.
///
/// @param[in] helper Pointer to the memory map helper structure.
void MemoryMapHelperDeinit(MemoryMapHelper *helper);

/// @brief Get shared memory section address.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] section The subsection of the shared memory
/// @return Pointer to the subsection
void *GetNoaWlanConfigSectionAddr(MemoryMapHelper *helper, enum NoaWlanConfigSection section);

/// @brief Get shared memory section size.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] section The subsection of the shared memory
/// @return Size of the subsection
uint32_t GetNoaWlanConfigSectionSize(MemoryMapHelper *helper, enum NoaWlanConfigSection section);

/// @brief Reads a value from the NOA WLAN shared memory.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] offset Offset in bytes from the base address of the shared
/// memory.
/// @param[in] size Size of the output buffer in bytes.
/// @param[out] out Pointer to the output buffer to store the read value.
///
/// @return 0 on success, negative error code on failure.
int32_t MemoryMapHelperRead(const MemoryMapHelper *helper, uint32_t offset, size_t size, void *out);

/// @brief Reads a field from the global configuration section.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] field Enum value representing the field to read.
/// @param[in] buffer_size Size of the output buffer in bytes.
/// @param[out] output_buffer Pointer to the output buffer.
///
/// @return 0 on success, negative error code on failure.
int32_t WlanGlobalConfigRead(const MemoryMapHelper *helper, GlobalConfigField field,
			     uint32_t buffer_size, void *output_buffer);

/// @brief Update the NEP ring info subsection in shared memory
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] nep_ring input NEP ring to be updated
/// @param[in] idx input NEP ring index
/// @param[in] ring_type NEP ring type
struct NoaWlanNepRingConfig *UpdateNoaWlanNepRingConfig(MemoryMapHelper *helper,
							const WlanRing *nep_ring, uint32_t idx,
							uint32_t ring_type);

/// @brief Update the WiFi MIB subsection in shared memory
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] dp_stats the pointer of the WlanDpStats
/// @return WiFi MIB subsection in shared memory
struct NoaMib *UpdateNoaWlanMIBInfo(MemoryMapHelper *helper, WlanDpStats *const dp_stats);

/// @brief Update the WiFi Interrupt Counter in shared memory
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] int_cnt the interrupt count number
/// @param[in] type the interrupt type related to APC and NCP.
void UpdateNoaWlanInterruptCounter(MemoryMapHelper *helper, uint32_t int_cnt,
				   NoaInterruptType type);

/// @brief Get the active STA info subsection in shared memory
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @return STA info subsection in shared memory
const struct NoaWlanStaInfo *GetNoaWlanStaInfoAddr(MemoryMapHelper *helper);

/// @brief Gets the address where the PCIe state is stored within the shared memory.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @return The address of the PCIe state storage location, or 0 if the
///         shared memory base address is invalid.
void *GetNoaWlanPcieStateStoredAddress(MemoryMapHelper *helper);

/// @brief Retrieves a pointer to a specific WLAN device ring information pool.
///
/// @param[in] helper A pointer to the MemoryMapHelper instance used to locate
/// the WLAN configuration section.
/// @param[in] ring_type The type of ring pool to retrieve, such as
/// `RING_TYPE_TX_DATA` or `RING_TYPE_RX_DATA`.
///
/// @return A pointer to the `NoaWlanRingInfo` structure for the requested
/// ring type on success.
/// @return NULL if the ring_type is invalid or not supported.
struct NoaWlanRingInfo *GetNoaWLanWdevRingPool(MemoryMapHelper *helper, uint32_t ring_type);

/// @brief Update the PCIe link PM state in shared memory.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] type The type of PCIe device state to update.
/// @param[in] value The value to set for the specified PCIe device state.
/// @return value read from the shared memory section on success.
int32_t UpdateNoaWlanPciDevState(MemoryMapHelper *helper, PciDevReconcileState type, int32_t value);

/// @brief Get the PCIe ownership state from shared memory.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @return The current PCIe ownership state, or an error code on failure.
PcieOwnership GetNoaWlanPcieOwnership(MemoryMapHelper *helper);

static inline void MemoryMapoffsetDump(MemoryMapHelper *helper)
{
	if (helper == NULL) {
		return;
	}

	WLAN_LOG_INFO(Shell,
		      "=============== (NCP) NOA WLAN Shared Memory Map Offset ===============");
	WLAN_LOG_INFO(Shell, "base address pa: %lx", helper->base_addr);
	WLAN_LOG_INFO(Shell, "(%08lx) GLOBAL_CONFIG size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, global_config),
		      GetNoaWlanConfigSectionSize(helper, kGlobalConfig));
	WLAN_LOG_INFO(Shell, "(%08lx) WDEV_RING_CONFIG size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, wifi_ring_config),
		      GetNoaWlanConfigSectionSize(helper, kWdevRingConfig));
	WLAN_LOG_INFO(Shell, "(%08lx) NEP_RING_CONFIG size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, nep_ring_config),
		      GetNoaWlanConfigSectionSize(helper, kNepRingConfig));
	WLAN_LOG_INFO(Shell, "(%08lx) STA_INFO size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, sta_info),
		      GetNoaWlanConfigSectionSize(helper, kStaInfo));
	WLAN_LOG_INFO(Shell, "(%08lx) PM_STATE_INFO size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, pm_state_info),
		      GetNoaWlanConfigSectionSize(helper, kPmStateInfo));
	WLAN_LOG_INFO(Shell, "(%08lx) INTR_STATE_INFO size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, interrupt_state_info),
		      GetNoaWlanConfigSectionSize(helper, kIntrStateInfo));
	WLAN_LOG_INFO(Shell, "(%08lx) MIB size: %" PRIu32, offsetof(NoaWlanSharedInfo, mib),
		      GetNoaWlanConfigSectionSize(helper, kMibInfo));
	WLAN_LOG_INFO(Shell, "(%08lx) WIT_LOG_SYS size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, wit_shared_log_sys_addr),
		      GetNoaWlanConfigSectionSize(helper, kWitLogSys));
	WLAN_LOG_INFO(Shell, "(%08lx) WIT_PACKET_SNIFFER size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, wit_shared_packet_addr),
		      GetNoaWlanConfigSectionSize(helper, kWitPacketSniffer));
	WLAN_LOG_INFO(Shell, "(%08lx) BUFFER_MGMT size: %" PRIu32,
		      offsetof(NoaWlanSharedInfo, buffer_management_shared_memory),
		      GetNoaWlanConfigSectionSize(helper, kBufferMgmt));
}

/// @brief Dump memory map value by section
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in]
static inline void DumpMemoryMapGlobalConfigSection(MemoryMapHelper *helper)
{
	NoaGlobalConfig *global_config = WLAN_REINTERPRET_CAST(
		NoaGlobalConfig *,
		(helper->base_addr + offsetof(NoaWlanSharedInfo, global_config)));

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, global_config),
			   sizeof(NoaGlobalConfig));

	WLAN_LOG_INFO(Shell, "(0x%08lx) %s value: %u\n", offsetof(NoaGlobalConfig, chip_type),
		      "chip_type", (u32)(global_config->chip_type & 0xFFFFFFFF));
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, rx_pkt_max, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, rx_buf_size, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, tx_pkt_max, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, tx_bm_size, PRIu32);
	MEMORY_MAP_SECTION_HEX64_VALUE(NoaGlobalConfig, global_config, share_addr);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, reg_addr, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, share_size, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, reg_size, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, ints_addr, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, intm_addr, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaGlobalConfig, global_config, rx_pkt_tlv_size, PRIu32);
}

static inline void DumpMemoryMapNepRingConfigSection(MemoryMapHelper *helper)
{
	int i = 0;
	NoaWlanNepRingConfig *nep_ring_config = WLAN_REINTERPRET_CAST(
		NoaWlanNepRingConfig *,
		(helper->base_addr + offsetof(NoaWlanSharedInfo, nep_ring_config)));
	NoaWlanRingInfo *nep_tx_rings = nep_ring_config->nep_tx_rings_info;
	NoaWlanRingInfo *nep_rxcpl_rings = nep_ring_config->nep_rx_rings_info;

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, nep_ring_config),
			   sizeof(NoaWlanNepRingConfig));

	for (i = 0; i < NUM_NEP_TX_POST_RING; i++) {
		WLAN_LOG_INFO(Shell, "NEP Ring TX %d value", i);
		MEMORY_MAP_SECTION_RING_CONFIG_VALUE(nep_tx_rings, i);
	}

	for (i = 0; i < NUM_NEP_RX_CMPL_RING; i++) {
		WLAN_LOG_INFO(Shell, "NEP Ring RX %d value", i);
		MEMORY_MAP_SECTION_RING_CONFIG_VALUE(nep_rxcpl_rings, i);
	}
}

static inline void DumpMemoryMapStaInfoSection(MemoryMapHelper *helper)
{
	int i = 0;
	uint32_t bit_field_offset =
		offsetof(NoaWlanStaInfo, mac_addr) + sizeof(uint8_t) * MAC_ADDR_LEN;

	NoaWlanStaInfo *sta_infos = WLAN_REINTERPRET_CAST(
		NoaWlanStaInfo *, (helper->base_addr + offsetof(NoaWlanSharedInfo, sta_info)));

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, sta_infos),
			   sizeof(NoaWlanStaInfo) * NOA_MAX_STA_SUPPORT);

	for (i = 0; i < NOA_MAX_STA_SUPPORT; i++) {
		if (!sta_infos[i].enable) {
			continue;
		}
		WLAN_LOG_INFO(Shell, "Station Info %d pointer address: %p\n", i, &sta_infos[i]);
		MEMORY_MAP_SECTION_VALUE(NoaWlanStaInfo, &sta_infos[i], oif, PRIu32);
		MEMORY_MAP_SECTION_VALUE(NoaWlanStaInfo, &sta_infos[i], bss_idx, PRIu16);
		WLAN_LOG_INFO(Shell, "(0x%08lx) qos_map (%d, %d, %d, %d, %d, %d, %d, %d)\n",
			      offsetof(NoaWlanStaInfo, qos_txq_map), sta_infos[i].qos_txq_map[0],
			      sta_infos[i].qos_txq_map[1], sta_infos[i].qos_txq_map[2],
			      sta_infos[i].qos_txq_map[3], sta_infos[i].qos_txq_map[4],
			      sta_infos[i].qos_txq_map[5], sta_infos[i].qos_txq_map[6],
			      sta_infos[i].qos_txq_map[7]);
		WLAN_LOG_INFO(Shell, "(0x%08lx) mac_addr %02x:%02x:%02x:%02x:%02x:%02x \n",
			      offsetof(NoaWlanStaInfo, mac_addr), sta_infos[i].mac_addr[0],
			      sta_infos[i].mac_addr[1], sta_infos[i].mac_addr[2],
			      sta_infos[i].mac_addr[3], sta_infos[i].mac_addr[4],
			      sta_infos[i].mac_addr[5]);
		WLAN_LOG_INFO(
			Shell,
			"(0x%08x) encrypt_type %d, (0x%08x) encap_type %d, (0x%08x)lmac_id %d\n",
			bit_field_offset, sta_infos[i].encrypt_type, bit_field_offset + 4,
			sta_infos[i].encap_type, bit_field_offset + 4 + 2, sta_infos[i].lmac_id);
		MEMORY_MAP_SECTION_VALUE(NoaWlanStaInfo, &sta_infos[i], bmid, PRIu8);
		bit_field_offset = offsetof(NoaWlanStaInfo, fw_metadata) + sizeof(uint8_t) * 2;
		WLAN_LOG_INFO(Shell, "(0x%08x) search_idx %d, (0x%08x) search_type %d\n",
			      bit_field_offset, sta_infos[i].search_idx, bit_field_offset + 20,
			      sta_infos[i].search_type);
		WLAN_LOG_INFO(
			Shell,
			"(0x%08x) dscp_tid_map_id %d, (0x%08x) addry_en %d, (0x%08x) addrx_en %d\n",
			bit_field_offset + 20 + 2, sta_infos[i].dscp_tid_map_id,
			bit_field_offset + 20 + 2 + 6, sta_infos[i].addry_en,
			bit_field_offset + 20 + 2 + 6 + 1, sta_infos[i].addrx_en);
	}
}

static inline void
DumpMemoryMapBufferMgmtSection(MemoryMapHelper *helper, struct BufferInfo *buf,
			       const struct BufferManagementTableInfo *table_info, bool dump_entry)
{
	int32_t offset;

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, table_info),
			   sizeof(struct BufferManagementTableInfo));

	WLAN_LOG_INFO(Shell, "Shared BM Table Info NOA View Addr: %p\n", table_info);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, version, PRIu16);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, table_type, PRIu16);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, pktid_offset,
				 PRIu16);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, entry_num, PRIu16);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, wlan_sw_sync_request,
				 PRIu16);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, table_size, PRIu32);
	MEMORY_MAP_SECTION_HEX64_VALUE(struct BufferManagementTableInfo, table_info,
				       refill_bitmap_addr);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, refill_bitmap_size,
				 PRIu32);
	MEMORY_MAP_SECTION_HEX64_VALUE(struct BufferManagementTableInfo, table_info, bmes_addr);
	MEMORY_MAP_SECTION_VALUE(struct BufferManagementTableInfo, table_info, bmes_size, PRIu32);

	if (dump_entry) {
		WLAN_LOG_INFO(Shell, "Shared BM Entry NOA View Addr: %p\n", buf);
		MEMORY_MAP_SECTION_VALUE(struct BufferInfo, buf, pktid, PRIu16);
		offset = offsetof(struct BufferInfo, ownership) + sizeof(buf->pktid);
		WLAN_LOG_INFO(Shell, "(0x%08x) ownership value: %" PRIu16 "\n", offset,
			      buf->ownership);
		WLAN_LOG_INFO(Shell, "(0x%08x) buffer_size value: %" PRIu16 "\n", offset + 2,
			      buf->buffer_size);
		MEMORY_MAP_SECTION_HEX64_VALUE(struct BufferInfo, buf, buffer_addr_phy);
		MEMORY_MAP_SECTION_HEX64_VALUE(struct BufferInfo, buf, buffer_addr_cpu);
	}
}

static inline uint32_t NoaWlanGetMaxRingNum(uint32_t ring_type)
{
	switch (ring_type) {
	case RING_TYPE_TX_DATA:
		return MAX_TX_RINGS_NUM;
	case RING_TYPE_RX_DATA:
		return MAX_RX_RINGS_NUM;
	case RING_TYPE_RX_POST:
		return MAX_RX_POST_RINGS_NUM;
	case RING_TYPE_TX_CPL:
		return MAX_TX_CPL_RINGS_NUM;
	default:
		return 0;
	}
}

static inline NoaWlanRingInfo *NoaWlanGetRingInfo(NoaWlanWiFiRingConfig *ring_config,
						  uint32_t ring_type)
{
	if (!ring_config) {
		return NULL;
	}

	switch (ring_type) {
	case RING_TYPE_TX_DATA:
		return ring_config->wifi_tx_rings_info;
	case RING_TYPE_RX_DATA:
		return ring_config->wifi_rx_rings_info;
	case RING_TYPE_TX_CPL:
		return ring_config->wifi_tx_cpl_rings_info;
	case RING_TYPE_RX_POST:
		return ring_config->wifi_rx_post_rings_info;
	default:
		return NULL;
	}
}

static inline void DumpMemoryMapWdevRingConfigSection(MemoryMapHelper *helper, int32_t ring_type,
						      int32_t ring_id)
{
	NoaWlanWiFiRingConfig *wifi_ring_config = WLAN_REINTERPRET_CAST(
		NoaWlanWiFiRingConfig *,
		(helper->base_addr + offsetof(NoaWlanSharedInfo, wifi_ring_config)));
	NoaWlanRingInfo *ring_info = NoaWlanGetRingInfo(wifi_ring_config, ring_type);
	uint32_t max_ring_num = NoaWlanGetMaxRingNum(ring_type);

	if (ring_info == NULL || ring_id >= max_ring_num) {
		return;
	}

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, wifi_ring_config),
			   sizeof(NoaWlanWiFiRingConfig));

	MEMORY_MAP_SECTION_RING_CONFIG_VALUE(ring_info, ring_id);
}

/// @brief Dump PM state info section in shared memory
///
/// Since for now the PM state info section does not have a copy in wlan_svc_, like
/// we have done in the station info section, it only need to read from the shared
/// memory directly. However, there is still a possibility that we will have the copy
/// in wlan_svc_ in the future, so here might be a sync back.
///
/// @param[in] helper Pointer to the memory map helper structure.
static inline void DumpMemoryMapPmStateInfoSection(MemoryMapHelper *helper)
{
	struct PciCapSavedData {
		uint16_t cap_nr;
		bool cap_extended;
		uint32_t size;
		uint32_t data[];
	};
	struct PciSavedState {
		uint32_t config_space[16];
		struct PciCapSavedData cap[];
	};

	struct PciCapSavedData *cap;
	size_t block_size;
	NoaPmStateInfo *pm_state_info = WLAN_REINTERPRET_CAST(
		NoaPmStateInfo *, (helper->base_addr + offsetof(NoaWlanSharedInfo, pm_state_info)));
	struct PciSavedState *pcie_stored_state =
		WLAN_STATIC_CAST(struct PciSavedState *, GetNoaWlanPcieStateStoredAddress(helper));
	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(const PhyAddr, pm_state_info),
			   sizeof(NoaPmStateInfo));

	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, PCIe_link_owner, PRIu16);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, PCIe_is_busmaster, PRIu16);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, PCIe_enable_cnt, PRId16);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, PCIe_current_state, PRIu16);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, PCIe_pm_state, PRId32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, runtime_suspend_cnt1, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, runtime_resume_cnt1, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, system_suspend_cnt1, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, system_resume_cnt1, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, runtime_suspend_cnt2, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, runtime_resume_cnt2, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, system_suspend_cnt2, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaPmStateInfo, pm_state_info, system_resume_cnt2, PRIu32);

	WLAN_LOG_INFO(Shell, "  Config Space Header (64 bytes):\n");
	for (int i = 0; i < 16; i += 4) {
		WLAN_LOG_INFO(Shell, "    0x%02X: %08X %08X %08X %08X\n", i * 4,
			      pcie_stored_state->config_space[i],
			      pcie_stored_state->config_space[i + 1],
			      pcie_stored_state->config_space[i + 2],
			      pcie_stored_state->config_space[i + 3]);
	}

	WLAN_LOG_INFO(Shell, "  Extended Capabilities:\n");
	cap = pcie_stored_state->cap;

	while (cap->size > 0) {
		WLAN_LOG_INFO(Shell, "    %sCap ID: 0x%02X, Data Size: %u bytes\n",
			      cap->cap_extended ? "Ext " : "", cap->cap_nr, cap->size);

		block_size = sizeof(struct PciCapSavedData) + cap->size;
		cap = (struct PciCapSavedData *)((uint8_t *)cap + block_size);
	}
}

static inline void DumpMemoryMapMibSection(MemoryMapHelper *helper)
{
	int i = 0;
	NoaMib *mib_info = WLAN_REINTERPRET_CAST(
		NoaMib *, (helper->base_addr + offsetof(NoaWlanSharedInfo, mib)));

	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rx_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rx_forward_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rx_pkt_err_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rx_pkt_byte, PRIu64);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_tx_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_tx_forward_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_tx_pkt_err_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_tx_pkt_byte, PRIu64);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_tx_cpl_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_tx_cpl_err_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rx_replenish_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rx_replenish_err_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_rxbm_sync_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, total_txbm_sync_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, nep_sw_output_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, nep_sw_txcpl_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, nep_sw_rxrefill_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, nep_fw_input_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, nep_fw_txcpl_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, nep_fw_rxrefill_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, device_rx_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, device_txcpl_ring_pkt_cnt, PRIu32);
	MEMORY_MAP_SECTION_VALUE(NoaMib, mib_info, device_rxrefill_ring_pkt_cnt, PRIu32);

	for (i = 0; i < NUM_NEP_TX_POST_RING; i++) {
		MEMORY_MAP_SECTION_U32_ARRAY_VALUE(NoaMib, mib_info, nep_sw_input_ring_pkt_cnt, i);
	}

	for (i = 0; i < NUM_NEP_TX_POST_RING; i++) {
		MEMORY_MAP_SECTION_U32_ARRAY_VALUE(NoaMib, mib_info, nep_fw_output_ring_pkt_cnt, i);
	}

	for (i = 0; i < MAX_TX_RINGS_NUM; i++) {
		MEMORY_MAP_SECTION_U32_ARRAY_VALUE(NoaMib, mib_info, device_tx_ring_pkt_cnt, i);
	}
}

#endif // CORE_WLAN_SHARED_MEM_MEMORY_MAP_HELPER_H
