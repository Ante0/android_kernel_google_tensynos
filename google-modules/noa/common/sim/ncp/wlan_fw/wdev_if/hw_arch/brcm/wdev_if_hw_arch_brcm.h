#ifndef WDEV_IF_HW_ARCH_BRCM_WDEV_IF_HW_ARCH_BRCM_H
#define WDEV_IF_HW_ARCH_BRCM_WDEV_IF_HW_ARCH_BRCM_H

#include "wdev_if/wdev_if.h"
#include "noa_desc.h"
#include "sys_if/types/types.h"

/// @brief Gets the TX flow ring ID.
///
/// @param[in] wdev_if The Wdev interface.
/// @param[in] wlan_ext_txd The WLAN extension Tx descriptor.
/// @param[out] ring_id The ring ID to be used for transmission.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t WdevArchGetTxFlowRingIdBrcm(WdevIf *const wdev_if, const void *const wlan_ext_txd,
					   uint32_t *ring_id);

/// @brief Prepare a NOA WLAN extend TX descriptor.
///
/// @param[in] wdev_if The WLAN device interface.
/// @param[in] noa_desc The NOA descriptor.
/// @param[in] sta_info The station information.
/// @param[out] ext_txd The composed NOA WLAN extend TXD.
///
/// @return 0 on success, negative error code on failure.
extern int32_t WdevIfPrepareNoaWlanExtendTxDBrcm(struct WdevIf *const wdev_if,
						 const NoaDesc *const noa_desc,
						 const StaInfo *const sta_info,
						 NoaWlanExtendTxD *ext_txd);

/// @brief Prepares a Tx post descriptor for Broadcom devices.
///
/// @param[in] wdev_if The Wdev interface.
/// @param[in] noa_desc The NOA descriptor containing information about the
/// packet.
/// @param[in] wlan_ext_txd The WLAN extension Tx descriptor.
/// @param[in] wdev_desc_buf_size The size of the Wdev descriptor buffer.
/// @param[in, out] coherence_info Coherence information for synchronization.
/// @param[out] wdev_desc The Wdev descriptor buffer to be filled.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t WdevArchPrepareTxPostDescBrcm(WdevIf *const wdev_if, const NoaDesc *const noa_desc,
					     const void *const wlan_ext_txd,
					     uint32_t wdev_desc_buf_size,
					     WdevPostDescCoherenceInfo *const coherence_info,
					     void *const wdev_desc);

/// @brief Prepares an Rx post descriptor for Broadcom devices.
///
/// @param[in] wdev_if The Wdev interface.
/// @param[in] tkid The tkid.
/// @param[in] wdev_desc_buf_size The size of the Wdev descriptor buffer.
/// @param[in, out] coherence_info Coherence information for synchronization.
/// @param[out] wdev_desc The Wdev descriptor buffer to be filled.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t WdevArchPrepareRxPostDescBrcm(WdevIf *const wdev_if, uint16_t tkid,
					     uint32_t wdev_desc_buf_size,
					     WdevPostDescCoherenceInfo *const coherence_info,
					     void *const wdev_desc);

/// @brief Processes a Tx completion descriptor from Broadcom devices.
///
/// @param[in] wdev_if The Wdev interface.
/// @param[in] wdev_desc The Wdev descriptor containing the completion
/// information.
/// @param[in] coherence_info Coherence information for synchronization.
/// @param[out] desc_info The structure to be filled with extracted
/// information.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t WdevArchProcessTxCplDescBrcm(WdevIf *const wdev_if, const void *const wdev_desc,
					    WdevCmplDescCoherenceInfo *const coherence_info,
					    WdevTxCmplDescriptorInfo *desc_info);

/// @brief Processes an Rx completion descriptor from Broadcom devices.
///
/// @param[in] wdev_if The Wdev interface.
/// @param[in] wdev_desc The Wdev descriptor containing the completion
/// information.
/// @param[in] coherence_info Coherence information for synchronization.
/// @param[in] desc_info The structure to be filled with extracted information.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t WdevArchProcessRxCplDescBrcm(WdevIf *const wdev_if, const void *const wdev_desc,
					    WdevCmplDescCoherenceInfo *const coherence_info,
					    WdevRxCmplDescriptorInfo *const desc_info);

#endif /* WDEV_IF_HW_ARCH_BRCM_WDEV_IF_HW_ARCH_BRCM_H */
