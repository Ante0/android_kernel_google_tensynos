#ifndef NOA_WLAN_MEMORY_MAP_H
#define NOA_WLAN_MEMORY_MAP_H

#include "common/ring.h"

#define MAX_RINGS_NUM 83
#define MAX_TX_RINGS_NUM 80
#define MAX_RX_RINGS_NUM 1
#define MAX_RX_POST_RINGS_NUM 1
#define MAX_TX_CPL_RINGS_NUM 1
#define MAX_RING_TYPE_NUM 4
#define MAX_RING_NAME_SIZE 32
#define PRIORITY_CLASS 8
#define NOA_MAX_STA_SUPPORT 12
#define RING_MAX_NAME 32
#define MAC_ADDR_LEN 6
#define APC_NCP_MEMORY_OFFSET 0x20000000
#define WIFI_DBG_LOG_SYS_OFFSET 20000
#define WIFI_DBG_PACKET_OFFSET 60000
#define WIFI_DBG_LOG_SYS_DRAM_SIZE 40000
#define WIFI_DBG_PACKET_DRAM_SIZE 40000
#define BUFFER_MANAGEMENT_OFFSET 100000
#define BUFFER_MANAGEMENT_ENTRY_NUM 49152
#define BUFFER_MANAGEMENT_TABLE_NUM 4
#define PCIE_STORED_STATE_BUF_WORD_SIZE (512UL / sizeof(uint32_t))
#define NUM_SHM_WDEV_TX_POST_RING (80U)
#define NUM_SHM_WDEV_RX_POST_RING (1U)
#define NUM_SHM_WDEV_TX_CMPL_RING (1U)
#define NUM_SHM_WDEV_RX_CMPL_RING (1U)
#define NUM_SHM_WDEV_CTRL_POST_RING (1U)
#define NUM_SHM_WDEV_CTRL_CMPL_RING (1U)
#define NUM_SHM_NEP_TX_POST_RING (1U)
#define NUM_SHM_NEP_TX_CMPL_RING (1U)
#define NUM_SHM_NEP_RX_CMPL_RING (1U)
#define NUM_SHM_NEP_FEEDBACK_RING (1U)
#define NUM_SHM_APC_DIRECT_TX_POST_RING (1U)
#define NUM_SHM_APC_DIRECT_RX_CMPL_RING (1U)
#define NUM_SHM_APC_TX_CMPL_RING (1U)
#define NUM_SHM_APC_VENDOR_RX_BUF_REPLN_RING (1U)
#define NUM_SHM_APC_NOA_TX_BUF_REPLN_RING (1U)
#define NUM_SHM_APC_FEEDBACK_RING (1U)
#define NUM_SHM_APC_FALLBACK_RX_CMPL_RING (1U)

#ifndef UINT32_WIDTH
#define UINT32_WIDTH 32
#endif
#define BITS_TO_UINT32S(nbits) (((nbits) + UINT32_WIDTH - 1) / UINT32_WIDTH)
#define SET_BIT(bitmap, bit)                                                                       \
	(((uint32_t *)(bitmap))[(bit) / UINT32_WIDTH] |= (1U << ((bit) % UINT32_WIDTH)))
#define CLEAR_BIT(bitmap, bit)                                                                     \
	(((uint32_t *)(bitmap))[(bit) / UINT32_WIDTH] &= ~(1U << ((bit) % UINT32_WIDTH)))
#define TEST_BIT(bitmap, bit)                                                                      \
	(((uint32_t *)(bitmap))[(bit) / UINT32_WIDTH] & (1U << ((bit) % UINT32_WIDTH)))

#define NOA_WLAN_SHM_INIT_RING_GROUP(LAYOUT, RING_TYPE)                                            \
	LAYOUT->group_pool[kShm##RING_TYPE##RingGroup].initialized_count = 0;                      \
	LAYOUT->group_pool[kShm##RING_TYPE##RingGroup].capacity =                                  \
		kShm##RING_TYPE##RingEnd - kShm##RING_TYPE##RingStart;                             \
	LAYOUT->group_pool[kShm##RING_TYPE##RingGroup].start_ring_index = kShm##RING_TYPE##RingStart

enum NoaWlanConfigSection {
	kSectionStart = 0,
	kGlobalConfig = kSectionStart,
	kRingConfig,
	kWdevRingConfig,
	kNepRingConfig,
	kStaInfo,
	kPmStateInfo,
	kIntrStateInfo,
	kMibInfo,
	kWitLogSys,
	kWitPacketSniffer,
	kBufferMgmt,
	kSectionEnd,
	kSectionNum = kSectionEnd,
};

typedef enum BufferOwnership {
	kBufferOwnershipStart = 0,
	kBufferOwnershipWlanSw = kBufferOwnershipStart,
	kBufferOwnershipWlanFw,
	kBufferOwnershipWdev,
	kBufferOwnershipNep,
	kBufferOwnershipEnd,
	kBufferOwnershipNum = kBufferOwnershipEnd,
} BufferOwnership;

typedef enum BufferTableType {
	kBufferTableTypeStart = 0,
	kBufferTableTypeNoaRx = kBufferTableTypeStart,
	kBufferTableTypeApcRx,
	kBufferTableTypeNoaTx,
	kBufferTableTypeApcTx,
	kBufferTableTypeEnd,
	kBufferTableTypeNum = kBufferTableTypeEnd,
} BufferTableType;

enum {
	kTxPostRingPool = 0,
	kRxCmplRingPool,
	kTxCmplRingPool,
	kRxPostRingPool,
};

enum {
	kShmRingGroupTypeStart = 0,
	// WiFi device <-> NCP rings
	kShmWdevTxPostRingGroup = kShmRingGroupTypeStart,
	kShmWdevRxCmplRingGroup,
	kShmWdevTxCmplRingGroup,
	kShmWdevRxPostRingGroup,
	kShmWdevCtrlPostRingGroup,
	kShmWdevCtrlCmplRingGroup,
	// NEP <-> NCP rings
	kShmNepTxPostRingGroup,
	kShmNepRxCmplRingGroup,
	kShmNepTxCmplRingGroup,
	kShmNepFeedbackRingGroup,
	// APC <-> NCP rings
	kShmApcDirectTxPostRingGroup,
	kShmApcDirectRxCmplRingGroup,
	kShmApcTxCmplRingGroup,
	kShmApcVendorRxBufReplnRingGroup,
	kShmApcNoaTxBufReplnRingGroup,
	kShmApcFeedbackRingGroup,
	kShmApcFallbackRxCmplRingGroup,
	kShmRingGroupTypeEnd,
	kShmRingGroupTypeNum = kShmRingGroupTypeEnd,
};

enum {
	kShmRingStart = 0,
	// WiFi device <-> NCP rings
	kShmWdevTxPostRingStart = kShmRingStart,
	kShmWdevTxPostRingEnd = kShmWdevTxPostRingStart + NUM_SHM_WDEV_TX_POST_RING,
	kShmWdevTxCmplRingStart = kShmWdevTxPostRingEnd,
	kShmWdevTxCmplRingEnd = kShmWdevTxCmplRingStart + NUM_SHM_WDEV_TX_CMPL_RING,
	kShmWdevRxPostRingStart = kShmWdevTxCmplRingEnd,
	kShmWdevRxPostRingEnd = kShmWdevRxPostRingStart + NUM_SHM_WDEV_RX_POST_RING,
	kShmWdevRxCmplRingStart = kShmWdevRxPostRingEnd,
	kShmWdevRxCmplRingEnd = kShmWdevRxCmplRingStart + NUM_SHM_WDEV_RX_CMPL_RING,
	kShmWdevCtrlPostRingStart = kShmWdevRxCmplRingEnd,
	kShmWdevCtrlPostRingEnd = kShmWdevCtrlPostRingStart + NUM_SHM_WDEV_CTRL_POST_RING,
	kShmWdevCtrlCmplRingStart = kShmWdevCtrlPostRingEnd,
	kShmWdevCtrlCmplRingEnd = kShmWdevCtrlCmplRingStart + NUM_SHM_WDEV_CTRL_CMPL_RING,
	// NEP <-> NCP rings
	kShmNepTxPostRingStart = kShmWdevCtrlCmplRingEnd,
	kShmNepTxPostRingEnd = kShmNepTxPostRingStart + NUM_SHM_NEP_TX_POST_RING,
	kShmNepRxCmplRingStart = kShmNepTxPostRingEnd,
	kShmNepRxCmplRingEnd = kShmNepRxCmplRingStart + NUM_SHM_NEP_RX_CMPL_RING,
	kShmNepTxCmplRingStart = kShmNepRxCmplRingEnd,
	kShmNepTxCmplRingEnd = kShmNepTxCmplRingStart + NUM_SHM_NEP_TX_CMPL_RING,
	kShmNepFeedbackRingStart = kShmNepTxCmplRingEnd,
	kShmNepFeedbackRingEnd = kShmNepFeedbackRingStart + NUM_SHM_NEP_FEEDBACK_RING,
	// APC <-> NCP rings
	kShmApcDirectTxPostRingStart = kShmNepFeedbackRingEnd,
	kShmApcDirectTxPostRingEnd = kShmApcDirectTxPostRingStart + NUM_SHM_APC_DIRECT_TX_POST_RING,
	kShmApcDirectRxCmplRingStart = kShmApcDirectTxPostRingEnd,
	kShmApcDirectRxCmplRingEnd = kShmApcDirectRxCmplRingStart + NUM_SHM_APC_DIRECT_RX_CMPL_RING,
	kShmApcTxCmplRingStart = kShmApcDirectRxCmplRingEnd,
	kShmApcTxCmplRingEnd = kShmApcTxCmplRingStart + NUM_SHM_APC_TX_CMPL_RING,
	kShmApcVendorRxBufReplnRingStart = kShmApcTxCmplRingEnd,
	kShmApcVendorRxBufReplnRingEnd =
		kShmApcVendorRxBufReplnRingStart + NUM_SHM_APC_VENDOR_RX_BUF_REPLN_RING,
	kShmApcNoaTxBufReplnRingStart = kShmApcVendorRxBufReplnRingEnd,
	kShmApcNoaTxBufReplnRingEnd =
		kShmApcNoaTxBufReplnRingStart + NUM_SHM_APC_NOA_TX_BUF_REPLN_RING,
	kShmApcFeedbackRingStart = kShmApcNoaTxBufReplnRingEnd,
	kShmApcFeedbackRingEnd = kShmApcFeedbackRingStart + NUM_SHM_APC_FEEDBACK_RING,
	kShmApcFallbackRxCmplRingStart = kShmApcFeedbackRingEnd,
	kShmApcFallbackRxCmplRingEnd =
		kShmApcFallbackRxCmplRingStart + NUM_SHM_APC_FALLBACK_RX_CMPL_RING,
	kShmRingEnd = kShmApcFallbackRxCmplRingEnd,
	kShmRingNum = kShmRingEnd,
};

enum {
	kRingStateBitStart = 0,
	kRingStateBitInitialized = kRingStateBitStart,
	kRingStateBitActive,
	kRingStateBitEnd = 32,
	kRingStateBitNum = kRingStateBitEnd,
};

enum {
	kRingStateMaskInitialized = (1U << kRingStateBitInitialized),
	kRingStateMaskActive = (1U << kRingStateBitActive),
};

typedef struct NoaGlobalConfig {
	uint64_t chip_type;
	uint32_t rx_pkt_max;
	uint32_t rx_buf_size;
	uint32_t tx_pkt_max;
	uint32_t tx_bm_size;
	uint64_t share_addr;
	uint32_t reg_addr;
	uint32_t share_size;
	uint32_t reg_size;
	uint32_t ints_addr;
	uint32_t intm_addr;
	uint32_t rx_pkt_tlv_size;
	uint64_t doorbell_addr;
	uint64_t fw_trap_addr;
} __attribute__((packed)) NoaGlobalConfig;

typedef struct NoaWlanRingInfo {
	struct noa_ring_regs regs;
	uint32_t ndesc;
	uint32_t desc_sz;
	uint32_t offset;
	uint32_t sn;
	uint8_t hw_idx;
	uint8_t stride;
	char name[RING_MAX_NAME];
	/* driver mode simulator only */
	uint64_t dma_va;
	uint64_t dma_pa;
	uint8_t state;
	uint8_t is_active;
} __attribute__((packed)) NoaWlanRingInfo;

typedef struct NoaWlanShmRingEntry {
	// Static configuration
	char name[RING_MAX_NAME];
	struct noa_ring_regs regs;
	uint32_t ndesc;
	uint32_t desc_sz;
	uint8_t hw_idx;
	uint8_t stride;
	uint64_t dma_va;
	uint64_t dma_pa;
	// Dynamic state
	uint32_t state;
	uint16_t read;
	uint16_t write;
	uint32_t sn;
} __attribute__((packed, aligned(4))) NoaWlanShmRingEntry;

typedef struct NoaWlanShmRingGroup {
	/// @brief The total number of ring slots allocated to this group
	uint32_t capacity;
	/// @brief The number of rings within this group that are initialized.
	uint32_t initialized_count;
	/// @brief The starting index into the main `ring_pool` array.
	uint32_t start_ring_index;
} __attribute__((packed, aligned(4))) NoaWlanShmRingGroup;

typedef struct NoaWlanShmRingConfig {
	/// @brief An array of all logical ring groups.
	NoaWlanShmRingGroup group_pool[kShmRingGroupTypeNum];
	/// @brief A flat memory pool containing the metadata for all rings.
	NoaWlanShmRingEntry entry_pool[kShmRingNum];
} __attribute__((packed, aligned(4))) NoaWlanShmRingConfig;

typedef struct NoaWlanRingPoolConfig {
	uint32_t ring_type;
	uint32_t count;
	uint64_t ring_pool; // the pointer of each ring pool type
} __attribute__((packed)) NoaWlanRingPoolConfig;

typedef struct NoaWlanWiFiRingConfig {
	struct NoaWlanRingInfo wifi_tx_rings_info[MAX_TX_RINGS_NUM];
	struct NoaWlanRingInfo wifi_rx_rings_info[MAX_RX_RINGS_NUM];
	struct NoaWlanRingInfo wifi_tx_cpl_rings_info[MAX_TX_CPL_RINGS_NUM];
	struct NoaWlanRingInfo wifi_rx_post_rings_info[MAX_RX_POST_RINGS_NUM];
	struct NoaWlanRingPoolConfig wifi_ring_pool_config[MAX_RING_TYPE_NUM];
} __attribute__((packed)) NoaWlanWiFiRingConfig;

typedef struct NoaWlanNepRingConfig {
	struct NoaWlanRingInfo nep_tx_rings_info[MAX_TX_RINGS_NUM];
	struct NoaWlanRingInfo nep_rx_rings_info[MAX_RX_RINGS_NUM];
	struct NoaWlanRingInfo nep_txcpl_rings_info[MAX_TX_CPL_RINGS_NUM];
	struct NoaWlanRingInfo nep_rxpost_rings_info[MAX_RX_POST_RINGS_NUM];
	struct NoaWlanRingPoolConfig nep_ring_pool_config[MAX_RING_TYPE_NUM];
} __attribute__((packed)) NoaWlanNepRingConfig;

typedef struct NoaWlanStaInfo {
	/* DW 0 */
	uint32_t oif;
	/* DW 1 */
	uint16_t bss_idx;
	uint16_t qos_txq_map[PRIORITY_CLASS];
	/* DW 2/3 */
	uint8_t mac_addr[MAC_ADDR_LEN];
	uint8_t encrypt_type : 4;
	uint8_t encap_type : 2;
	uint8_t lmac_id : 2;
	uint8_t bmid;
	uint16_t fw_metadata;
	/* DW 4 */
	uint32_t search_idx : 20;
	uint32_t search_type : 2;
	uint32_t dscp_tid_map_id : 6;
	uint32_t addry_en : 1;
	uint32_t addrx_en : 1;
	uint32_t reserved2 : 2;
	/* DW 5 */
	uint8_t enable;
	uint8_t sta_id;
	uint8_t reserved3[2];
} __attribute__((packed)) NoaWlanStaInfo;

typedef struct NoaPmStateInfo {
	uint8_t PCIe_link_owner;
	uint8_t PCIe_is_busmaster;
	int8_t PCIe_enable_cnt;
	uint8_t PCIe_current_state;
	int32_t PCIe_pm_state;
	uint32_t runtime_suspend_cnt1;
	uint32_t runtime_resume_cnt1;
	uint32_t system_suspend_cnt1;
	uint32_t system_resume_cnt1;
	uint32_t runtime_suspend_cnt2;
	uint32_t runtime_resume_cnt2;
	uint32_t system_suspend_cnt2;
	uint32_t system_resume_cnt2;
	uint32_t pcie_stored_state[PCIE_STORED_STATE_BUF_WORD_SIZE];
} __attribute__((packed)) NoaPmStateInfo;

typedef enum NoaInterruptType {
	INTERRUPT_TYPE_START = 0,
	PCIE_MSI_APC_INTR = INTERRUPT_TYPE_START,
	PCIE_MSI_NCP_INTR,
	NEP_APC_INTR,
	NEP_NCP_INTR,
	NCP_APC_INTR,
	APC_NCP_INTR,
	INTERRUPT_TYPE_END,
	INTERRUPT_TYPE_NUM = INTERRUPT_TYPE_END,
} __attribute__((packed)) NoaInterruptType;

typedef struct NoaInterruptStateInfo {
	uint8_t interrupt_owner;
	uint8_t rsv[3];
	uint32_t interrupt_proxy_status;
	/// Total interrupt count for each type. Each type of interrupt might have
	/// multiple IRQs, adding all IRQs into this interrupt_cnt.
	uint32_t interrupt_cnt[INTERRUPT_TYPE_NUM];
	uint32_t interrupt_proxy_cnt;
} __attribute__((packed)) NoaInterruptStateInfo;

typedef struct NoaMib {
	uint32_t total_rx_pkt_cnt;
	uint32_t total_rx_forward_pkt_cnt;
	uint32_t total_rx_pkt_err_cnt;
	uint64_t total_rx_pkt_byte;
	uint32_t total_tx_pkt_cnt;
	uint32_t total_tx_forward_pkt_cnt;
	uint32_t total_tx_pkt_err_cnt;
	uint64_t total_tx_pkt_byte;
	uint32_t total_tx_cpl_cnt;
	uint32_t total_tx_cpl_err_cnt;
	uint32_t total_rx_replenish_cnt;
	uint32_t total_rx_replenish_err_cnt;
	uint32_t total_rxbm_sync_cnt;
	uint32_t total_txbm_sync_cnt;
	uint32_t nep_sw_input_ring_pkt_cnt[MAX_TX_RINGS_NUM];
	uint32_t nep_sw_output_ring_pkt_cnt;
	uint32_t nep_sw_txcpl_ring_pkt_cnt;
	uint32_t nep_sw_rxrefill_ring_pkt_cnt;
	uint32_t nep_fw_input_ring_pkt_cnt;
	uint32_t nep_fw_output_ring_pkt_cnt[MAX_TX_RINGS_NUM];
	uint32_t nep_fw_txcpl_ring_pkt_cnt;
	uint32_t nep_fw_rxrefill_ring_pkt_cnt;
	uint32_t device_rx_ring_pkt_cnt;
	uint32_t device_tx_ring_pkt_cnt[MAX_TX_RINGS_NUM];
	uint32_t device_txcpl_ring_pkt_cnt;
	uint32_t device_rxrefill_ring_pkt_cnt;
} __attribute__((packed)) NoaMib;

typedef struct BufferManagementTableInfo {
	// DW 0
	uint8_t version;
	uint8_t table_type;
	uint16_t pktid_offset;
	// DW 1
	uint16_t entry_num;
	bool wlan_sw_sync_request;
	uint8_t rsv;
	// DW 2
	uint32_t table_size;
	// These addresses are physical addresses within the RTOS, whereas
	// in the driver mode, it would be a virtual addresses.
	// DW 3 - DW 4
	uint64_t refill_bitmap_addr;
	// DW 5
	uint32_t refill_bitmap_size;
	// DW 6 - DW 7
	uint64_t bmes_addr;
	// DW 8
	uint32_t bmes_size;
} __attribute__((packed)) BufferManagementTableInfo;

typedef struct BufferManagementEntry {
	// DW 0
	uint16_t pktid;
	uint16_t ownership : 2;
	uint16_t buffer_size : 14;
	// In the driver mode, buffer_addr_cpu provides a virtual address
	// accessible to the CPU, while buffer_addr_dma is the physical
	// address used by the DMA controller. In RTOS, where the CPU can
	// directly access physical addresses, only buffer_addr_dma is
	// needed.
	// DW 1 - DW 2
	uint64_t buffer_addr_phy;
	// DW 3 - DW 4
	// should be removed when the driver mode is no longer in use.
	uint64_t buffer_addr_cpu; // driver mode only
} __attribute__((packed)) BufferManagementEntry;

/// @brief Structure containing information about the buffer manager.
typedef struct BufferManagerInfo {
	/// @brief Version of the buffer manager.
	uint8_t version;
	/// @brief Reserved bytes for future use.
	uint8_t rsv[3];
	/// @brief Address of NOA RX buffer table info.
	uint64_t noa_rx_buffer_table_info_addr;
	/// @brief Address of APC RX buffer table info.
	uint64_t apc_rx_buffer_table_info_addr;
	/// @brief Address of NOA TX buffer table info.
	uint64_t noa_tx_buffer_table_info_addr;
	/// @brief Address of APC TX buffer table info.
	uint64_t apc_tx_buffer_table_info_addr;
} __attribute__((packed)) BufferManagerInfo;

typedef struct NoaBufferManagementSharedMemory {
	BufferManagerInfo bm_info;
	BufferManagementTableInfo bm_table_info[BUFFER_MANAGEMENT_TABLE_NUM];
	uint8_t refill_bitmap[BUFFER_MANAGEMENT_ENTRY_NUM / 8];
	BufferManagementEntry bm_entry[BUFFER_MANAGEMENT_ENTRY_NUM];
} __attribute__((packed)) NoaBufferManagementSharedMemory;

typedef struct NoaWlanSharedInfo {
	struct NoaGlobalConfig global_config;
	struct NoaWlanShmRingConfig ring_config;
	struct NoaWlanWiFiRingConfig wifi_ring_config;
	struct NoaWlanNepRingConfig nep_ring_config;
	struct NoaWlanStaInfo sta_info[NOA_MAX_STA_SUPPORT];
	struct NoaPmStateInfo pm_state_info;
	struct NoaInterruptStateInfo interrupt_state_info;
	struct NoaMib mib;
	uint8_t wit_shared_log_sys_addr[WIFI_DBG_LOG_SYS_DRAM_SIZE];
	uint8_t wit_shared_packet_addr[WIFI_DBG_PACKET_DRAM_SIZE];
	struct NoaBufferManagementSharedMemory buffer_management_shared_memory;
} __attribute__((packed)) NoaWlanSharedInfo;

#endif /* CORE_WLAN_SHARED_MEM_H */
