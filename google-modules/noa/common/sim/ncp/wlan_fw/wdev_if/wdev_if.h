#ifndef WDEV_IF_WDEV_IF_H
#define WDEV_IF_WDEV_IF_H

#include "noa_desc.h"
#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "modules/sta_table/sta_table.h"
#include "ext_svc/ext_svc.h"

/// @brief Maximum number of IRQ information entries.
#define WDEV_MAX_NUM_IRQ_INFO (32U)

/// @brief Forward declaration.
struct WdevIf;

/// @brief WLAN device chip IDs.
typedef enum WlanDeviceChipId {
	kWlanDeviceChipIdStart = 0,
	kWlanDeviceChipIdBrcm4389 = kWlanDeviceChipIdStart,
	kWlanDeviceChipIdBrcm4390,
	kWlanDeviceChipIdGem5FakeBrcm4389,
	kWlanDeviceChipIdGem5FakeBrcm4390,
	kWlanDeviceChipIdWcn7760,
	kWlanDeviceChipIdEnd,
	kWlanDeviceChipIdNum = kWlanDeviceChipIdEnd,
} WlanDeviceChipId;

/// @brief WLAN device architecture types.
typedef enum WlanDeviceArchType {
	kWlanDeviceArchTypeStart = 0,
	kWlanDeviceArchTypeUnknown = kWlanDeviceArchTypeStart,
	kWlanDeviceArchTypeBrcmV0,
	kWlanDeviceArchTypeBrcmV1,
	kWlanDeviceArchTypeWcn7760,
	kWlanDeviceArchTypeEnd,
	kWlanDeviceArchTypeNum = kWlanDeviceArchTypeEnd,
} WlanDeviceArchType;

/// @brief Enumeration of completion descriptor coherence validation methods.
typedef enum WdevCmplDescCoherenceValidationMethod {
	kWdevCmplDescCoherenceValidationMethodStart = 0,
	kWdevCmplDescCoherenceValidationMethodNone = kWdevCmplDescCoherenceValidationMethodStart,
	kWdevCmplDescCoherenceValidationMethodBrcmSnCsum,
	kWdevCmplDescCoherenceValidationMethodEnd,
	kWdevCmplDescCoherenceValidationMethodNum = kWdevCmplDescCoherenceValidationMethodEnd,
} WdevCmplDescCoherenceValidationMethod;

/// @brief Enumeration of post descriptor coherence validation methods.
typedef enum WdevPostDescCoherenceValidationMethod {
	kWdevPostDescCoherenceValidationMethodStart = 0,
	kWdevPostDescCoherenceValidationMethodNone = kWdevPostDescCoherenceValidationMethodStart,
	kWdevPostDescCoherenceValidationMethodBrcmSn,
	kWdevPostDescCoherenceValidationMethodEnd,
	kWdevPostDescCoherenceValidationMethodNum = kWdevPostDescCoherenceValidationMethodEnd,
} WdevPostDescCoherenceValidationMethod;

/// @brief Flags for Wdev packets.
typedef enum WdevPacketFlag {
	kWdevPacketFlagStationMode = (1U << 0),
	kWdevPacketFlag802dot11 = (1U << 1),
} WdevPacketFlag;

/// @brief Structure containing information extracted from an Rx completion
/// descriptor.
typedef struct WdevRxCmplDescriptorInfo {
	uint16_t pktid;
	uint8_t bss_idx;
	int32_t flags;
	uint32_t head_offset;
	uint32_t data_len;
} WdevRxCmplDescriptorInfo;

/// @brief Structure containing information extracted from a Tx completion
/// descriptor.
typedef struct WdevTxCmplDescriptorInfo {
	uint16_t pktid;
} WdevTxCmplDescriptorInfo;

/// @brief Structure containing coherence information for completion
/// descriptors.
typedef struct WdevCmplDescCoherenceInfo {
	union {
		/// @brief Broadcom checksum validation input.
		struct {
			/// @brief Current sequence number.
			uint32_t current_sn;
			/// @brief Next sequence number.
			uint32_t next_sn;
			/// @brief Ring descriptor size.
			uint32_t ring_desc_size;
		} brcm_sn_cks;
	};
} WdevCmplDescCoherenceInfo;

/// @brief Structure containing coherence information for post descriptors.
typedef struct WdevPostDescCoherenceInfo {
	union {
		/// @brief Broadcom checksum validation input.
		struct {
			/// @brief Current sequence number.
			uint32_t current_sn;
			/// @brief Next sequence number.
			uint32_t next_sn;
			/// @brief Ring descriptor size.
			uint32_t ring_desc_size;
		} brcm_sn_cks;
	};
} WdevPostDescCoherenceInfo;

/// @brief Input structure for WLAN device ring TX post doorbell.
typedef struct WdevRingTxPostDoorbellInput {
	union {
		/// @brief Broadcom ring TX post doorbell input.
		struct {
			/// @brief Ring ID.
			uint32_t ring_id;
			/// @brief Write index.
			uint32_t write_idx;
		} brcm;
	};
} WdevRingTxPostDoorbellInput;

typedef struct WdevIrqInfo {
	/// @brief Number of IRQs.
	uint32_t num_irq;
	struct {
		/// @brief IRQ number.
		uint32_t irq_num;
		/// @brief Bitmask to control polling for TX completion rings.
		uint32_t tx_cpl_ring_polling_mask;
		/// @brief Bitmask to control polling for RX data rings.
		uint32_t rx_data_ring_polling_mask;
	} info[WDEV_MAX_NUM_IRQ_INFO];
} WdevIrqInfo;

/// @brief WLAN device chip operations.
typedef struct WdevIfChipOps {
	int32_t (*Init)(struct WdevIf *const wdev_if);
	void (*Deinit)(struct WdevIf *const wdev_if);
	void (*AcknowledgeInterrupt)(struct WdevIf *const wdev_if, int32_t irq_id);
	void (*RingTxPostDoorbell)(struct WdevIf *const wdev_if, void *priv);
	bool (*PcieCheckCmplTimeOut)(void);
	bool (*FwTrapCheck)(uint64_t fw_trap_addr);
} WdevIfChipOps;

/// @brief WLAN device hardware architecture operations.
typedef struct WdevIfHwArchOps {
	int32_t (*GetTxFlowRingId)(struct WdevIf *const wdev_if, const void *const wlan_ext_txd,
				   uint32_t *ring_id);
	int32_t (*PrepareNoaWlanExtendTxD)(struct WdevIf *const wdev_if,
					   const NoaDesc *const noa_desc,
					   const StaInfo *const sta_info,
					   NoaWlanExtendTxD *ext_txd);
	int32_t (*PrepareTxPostDesc)(struct WdevIf *const wdev_if, const NoaDesc *const noa_desc,
				     const void *const wlan_ext_txd, uint32_t wdev_desc_buf_size,
				     struct WdevPostDescCoherenceInfo *const coherence_info,
				     void *const wdev_desc);
	int32_t (*PrepareRxPostDesc)(struct WdevIf *const wdev_if, uint16_t tkid,
				     uint32_t wdev_desc_buf_size,
				     struct WdevPostDescCoherenceInfo *const coherence_info,
				     void *const wdev_desc);
	int32_t (*HandleTxCplDesc)(struct WdevIf *const wdev_if, const void *const wdev_desc,
				   WdevCmplDescCoherenceInfo *const coherence_info,
				   WdevTxCmplDescriptorInfo *const desc_info);
	int32_t (*HandleRxCplDesc)(struct WdevIf *const wdev_if, const void *const wdev_desc,
				   WdevCmplDescCoherenceInfo *const coherence_info,
				   WdevRxCmplDescriptorInfo *const desc_info);
} WdevIfHwArchOps;

/// @brief WLAN device statistics.
typedef struct WdevIfStats {
	/// @brief Number of ring TX post doorbell operations.
	uint32_t num_ring_tx_post_doorbell;
} WdevIfStats;

/// @brief WLAN device interface structure.
typedef struct WdevIf {
	/// @brief Chip ID.
	WlanDeviceChipId chip_id;
	/// @brief Architecture type.
	WlanDeviceArchType arch_type;
	/// @brief Complete descriptor validation method.
	WdevCmplDescCoherenceValidationMethod cmpl_desc_val_method;
	/// @brief Posting descriptor validation method.
	WdevPostDescCoherenceValidationMethod post_desc_val_method;
	/// @brief Chip operations.
	WdevIfChipOps chip_ops;
	/// @brief Hardware architecture operations.
	WdevIfHwArchOps hw_arch_ops;
	/// @brief Statistics.
	WdevIfStats stats;
	/// @brief RX packet TLV size.
	uint32_t rx_pkt_tlv_size;
	/// @brief IRQ information.
	WdevIrqInfo irq_info;
	ExternalServices *ext_svc;
	/// @brief WLAN device doorbell addr.
	uint64_t doorbell_addr;
	/// @brief Device private data.
	void *dev_priv_data;
	uint32_t cookie_base_addr;
} WdevIf;

/// @brief Deinitialize a WLAN device interface.
///
/// @param[in] wdev_if The WLAN device interface to deinitialize.
static inline void WdevIfDeinit(WdevIf *const wdev_if)
{
	if (wdev_if->chip_ops.Deinit) {
		wdev_if->chip_ops.Deinit(wdev_if);
	}

	memset(wdev_if, 0, sizeof(WdevIf));
}

/// @brief Acknowledge an interrupt to a WLAN device.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] irq_id The ID of the interrupt to acknowledge.
static inline void WdevIfAcknowledgeInterrupt(WdevIf *const wdev_if, const int32_t irq_id)
{
	if (wdev_if->chip_ops.AcknowledgeInterrupt) {
		wdev_if->chip_ops.AcknowledgeInterrupt(wdev_if, irq_id);
	}
}

/// @brief Ring the TX post doorbell.
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] priv Private data for the ring TX post doorbell
/// operation.
static inline void WdevIfRingTxPostDoorbell(WdevIf *const wdev_if, void *priv)
{
	if (wdev_if->chip_ops.RingTxPostDoorbell) {
		wdev_if->chip_ops.RingTxPostDoorbell(wdev_if, priv);
		wdev_if->stats.num_ring_tx_post_doorbell++;
	}
}

/// @brief Check if the configuration space for CTO.
/// @return true if CTO is found, false otherwise.
static inline bool WdevIfPcieCheckCmplTimeOut(WdevIf *const wdev_if)
{
	if (wdev_if->chip_ops.PcieCheckCmplTimeOut) {
		return wdev_if->chip_ops.PcieCheckCmplTimeOut();
	}

	return false;
}

/// @brief Check if fw trap data is zero
/// @return true if fw trap data is non zero, false otherwise.
static inline bool WdevIfFwTrapCheck(const WdevIf *const wdev_if, uint64_t fw_trap_addr)
{
	if (wdev_if->chip_ops.FwTrapCheck) {
		return wdev_if->chip_ops.FwTrapCheck(fw_trap_addr);
	}

	return false;
}

static inline const WdevIrqInfo *WdevIfGetIrqInfo(const WdevIf *const wdev_if)
{
	return &wdev_if->irq_info;
}

/// @brief Gets the TX flow ring ID.
///
/// @param[in] wdev_if The Wdev interface.
/// @param[in] wlan_ext_txd The WLAN extension Tx descriptor.
/// @param[out] ring_id The ring ID to be used for transmission.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t WdevIfGetTxFlowRingId(WdevIf *const wdev_if, const void *const wlan_ext_txd,
					    uint32_t *ring_id)
{
	if (wdev_if->hw_arch_ops.GetTxFlowRingId) {
		return wdev_if->hw_arch_ops.GetTxFlowRingId(wdev_if, wlan_ext_txd, ring_id);
	}

	return -ENODEV;
}

/// @brief Prepare a NOA WLAN extend TX descriptor.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] noa_desc The NOA descriptor.
/// @param[in] sta_info The station information.
/// @param[out] ext_txd The composed NOA WLAN extend TXD.
///
/// @return 0 on success, negative error code on failure.
static inline int32_t WdevIfPrepareNoaWlanExtendTxD(struct WdevIf *const wdev_if,
						    const NoaDesc *const noa_desc,
						    const StaInfo *const sta_info,
						    NoaWlanExtendTxD *ext_txd)
{
	if (wdev_if->hw_arch_ops.PrepareNoaWlanExtendTxD) {
		return wdev_if->hw_arch_ops.PrepareNoaWlanExtendTxD(wdev_if, noa_desc, sta_info,
								    ext_txd);
	}

	return -ENODEV;
}

/// @brief Prepare a TX post descriptor.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] noa_desc The NOA descriptor.
/// @param[in] wdev_desc_buf_size The size of the Wdev descriptor
/// buffer.
/// @param[in, out] coherence_info Coherence information for synchronization.
/// @param[out] wdev_desc The Wdev descriptor.
/// @return 0 on success, negative error code on failure.
static inline int32_t WdevIfPrepareTxPostDesc(WdevIf *const wdev_if, NoaDesc *const noa_desc,
					      const void *const wlan_ext_txd,
					      uint32_t wdev_desc_buf_size,
					      WdevPostDescCoherenceInfo *const coherence_info,
					      void *const wdev_desc)
{
	if (wdev_if->hw_arch_ops.PrepareTxPostDesc) {
		return wdev_if->hw_arch_ops.PrepareTxPostDesc(wdev_if, noa_desc, wlan_ext_txd,
							      wdev_desc_buf_size, coherence_info,
							      wdev_desc);
	}

	return -ENODEV;
}

/// @brief Prepare an RX post descriptor.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] tkid The tkid.
/// @param[in] wdev_desc_buf_size The size of the Wdev descriptor buffer.
/// @param[in, out] coherence_info Coherence information for synchronization.
/// @param[out] wdev_desc The Wdev descriptor.
/// @return 0 on success, negative error code on failure.
static inline int32_t WdevIfPrepareRxPostDesc(WdevIf *const wdev_if, uint16_t tkid,
					      uint32_t wdev_desc_buf_size,
					      WdevPostDescCoherenceInfo *const coherence_info,
					      void *const wdev_desc)
{
	if (wdev_if->hw_arch_ops.PrepareRxPostDesc) {
		return wdev_if->hw_arch_ops.PrepareRxPostDesc(wdev_if, tkid, wdev_desc_buf_size,
							      coherence_info, wdev_desc);
	}

	return -ENODEV;
}

/// @brief Handle a TX completion descriptor.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] wdev_desc The Wdev descriptor.
/// @param[in, out] coherence_info Coherence information for synchronization.
/// @param[out] desc_info The structure to be filled with extracted
/// information.
/// @return 0 on success, negative error code on failure.
static inline int32_t WdevIfHandleTxCplDesc(WdevIf *const wdev_if, void *const wdev_desc,
					    WdevCmplDescCoherenceInfo *const coherence_info,
					    WdevTxCmplDescriptorInfo *const desc_info)
{
	if (wdev_if->hw_arch_ops.HandleTxCplDesc) {
		return wdev_if->hw_arch_ops.HandleTxCplDesc(wdev_if, wdev_desc, coherence_info,
							    desc_info);
	}

	return -ENODEV;
}

/// @brief Handle an RX completion descriptor.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] wdev_desc The Wdev descriptor.
/// @param[in, out] coherence_info Coherence information for synchronization.
/// @param[out] desc_info The structure to be filled with extracted
/// information.
/// @return 0 on success, negative error code on failure.
static inline int32_t WdevIfHandleRxCplDesc(WdevIf *const wdev_if, void *const wdev_desc,
					    WdevCmplDescCoherenceInfo *const coherence_info,
					    WdevRxCmplDescriptorInfo *const desc_info)
{
	if (wdev_if->hw_arch_ops.HandleRxCplDesc) {
		return wdev_if->hw_arch_ops.HandleRxCplDesc(wdev_if, wdev_desc, coherence_info,
							    desc_info);
	}

	return -ENODEV;
}

/// @brief Get the chip ID of a WLAN device interface.
///
/// @param[in] wdev_if The WLAN device interface.
/// @return The chip ID.
static inline WlanDeviceChipId WdevIfGetChipId(WdevIf *const wdev_if)
{
	return wdev_if->chip_id;
}

/// @brief Get the architecture type of a WLAN device interface.
///
/// @param[in] wdev_if The WLAN device interface.
/// @return The architecture type.
static inline WlanDeviceArchType WdevIfGetArchType(WdevIf *const wdev_if)
{
	return wdev_if->arch_type;
}

/// @brief Gets the completion descriptor coherence validation method.
///
/// @param[in] wdev_if The WLAN device interface.
/// @return The descriptor validation method.
static inline WdevCmplDescCoherenceValidationMethod
WdevIfGetCmplValidateDescriptorMethod(WdevIf *const wdev_if)
{
	return wdev_if->cmpl_desc_val_method;
}

/// @brief Gets the posting descriptor coherence validation method.
///
/// @param[in] wdev_if The WLAN device interface.
/// @return The descriptor validation method.
static inline WdevPostDescCoherenceValidationMethod
WdevIfGetPostValidateDescriptorMethod(WdevIf *const wdev_if)
{
	return wdev_if->post_desc_val_method;
}

/// @brief Initialize a WLAN device interface.
///
/// @param[in] wdev_if The WLAN device interface to initialize.
/// @param[in] chip_id The chip ID.
/// @param[in] rx_pkt_tlv_size The RX packet TLV size.
/// @return 0 on success, negative error code on failure.
extern int32_t WdevIfInit(WdevIf *const wdev_if, WlanDeviceChipId chip_id,
			  uint32_t rx_pkt_tlv_size, ExternalServices *ext_svc);

/// @brief Set Wlan device doorbell address
///
/// @param[in] wdev_if The WLAN device interface to initialize.
/// @param[in] doorbell_addr Doorbell address.
/// @return 0 on success, negative error code on failure.
extern int32_t WdevIfSetDoorbellAddr(WdevIf *const wdev_if, uint64_t doorbell_addr);

#endif /* WDEV_IF_WDEV_IF_H */
