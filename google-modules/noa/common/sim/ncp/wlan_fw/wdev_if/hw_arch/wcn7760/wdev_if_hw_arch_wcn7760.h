#ifndef WDEV_IF_HW_ARCH_WCN7760_WDEV_IF_HW_ARCH_WCN7760_H
#define WDEV_IF_HW_ARCH_WCN7760_WDEV_IF_HW_ARCH_WCN7760_H

#include "wdev_if/wdev_if.h"
#include "noa_desc.h"
#include "sys_if/types/types.h"
#include "modules/sta_table/sta_table.h"

/**
 * WdevArchGetTxFlowRingIdWcn7760() - Gets theTX flow ring ID.
 * @wdev_if: The Wdev interface.
 * @wlan_ext_txd: The WLAN extension Tx descriptor.
 * @ring_id: The ring ID to be used for transmission.
 *
 * Return: 0 on success, negative error code otherwise.
 */
extern int32_t WdevArchGetTxFlowRingIdWcn7760(WdevIf *const wdev_if, const void *const wlan_ext_txd,
					   uint32_t *ring_id);

/**
 * WdevIfPrepareNoaWlanExtendTxDWcn7760() - Prepare the WLAN extended TX
 * descriptor.
 * @wdev_if: The Wdev interface.
 * @noa_desc: The NOA descriptor.
 * @sta_info: Station information.
 * @ext_txd: The composed NOA WLAN extend Tx descriptor.
 *
 * Return: 0 on success, negative error code on failure.
 */
extern int32_t WdevIfPrepareNoaWlanExtendTxDWcn7760(struct WdevIf *const wdev_if,
						 const NoaDesc *const noa_desc,
						 const StaInfo *const sta_info,
						 NoaWlanExtendTxD *ext_txd);

/**
 * WdevArchPrepareTxPostDescWcn7760() - Prepares a native TCL Tx descriptor.
 * @wdev_if: The Wdev interface.
 * @noa_desc: The  NOA descriptor.
 * @wlan_ext_txd: The WLAN extension Tx descriptor.
 * @wdev_desc_buf_size: Size of the wdev_desc buffer.
 * @coherence_info: Coherence information (unused for WCN7760).
 * @wdev_desc: The Wdev descriptor buffer to be filled.
 *
 * Return: 0 on success, negative error code otherwise.
 */
extern int32_t WdevArchPrepareTxPostDescWcn7760(WdevIf *const wdev_if, const NoaDesc *const noa_desc,
					     const void *const wlan_ext_txd,
					     uint32_t wdev_desc_buf_size,
					     WdevPostDescCoherenceInfo *const coherence_info,
					     void *const wdev_desc);

/**
 * WdevArchPrepareRxPostDescWcn7760() - Prepares Rx post descriptor for WCN7760.
 * @wdev_if: The Wdev interface.
 * @tkid: The packet identifier.
 * @wdev_desc_buf_size: Size of the wdev_desc buffer.
 * @coherence_info: Coherence information (unused for WCN7760).
 * @wdev_desc: The Wdev descriptor buffer to be filled.
 *
 * Return: 0 on success, negative error code otherwise.
 */
extern int32_t WdevArchPrepareRxPostDescWcn7760(WdevIf *const wdev_if, uint16_t tkid,
					     uint32_t wdev_desc_buf_size,
					     WdevPostDescCoherenceInfo *const coherence_info,
					     void *const wdev_desc);

/**
 * WdevArchProcessTxCplDescWcn7760() - Prepares Tx completion  descriptor
 * for WCN7760.
 * @wdev_if: The Wdev interface.
 * @wdev_desc: The Wdev descriptor containing the completion information.
 * @coherence_info: Coherence information (unused for WCN7760).
 * @desc_info: Structure to store extracted completion info.
 *
 * Return: 0 on success, negative error code otherwise.
 */
extern int32_t WdevArchProcessTxCplDescWcn7760(WdevIf *const wdev_if, const void *const wdev_desc,
					    WdevCmplDescCoherenceInfo *const coherence_info,
					    WdevTxCmplDescriptorInfo *desc_info);

/**
 * WdevArchProcessRxCplDescWcn7760() - Processes an RX completion descriptor.
 * @wdev_if: The Wdev interface.
 * @wdev_desc: The Wdev descriptor containing the completion information.
 * @coherence_info: Coherence information (unused for WCN7760).
 * @desc_info: Structure to store extracted receive info.
 *
 * Return: 0 on success, negative error code otherwise.
 */
extern int32_t WdevArchProcessRxCplDescWcn7760(WdevIf *const wdev_if, const void *const wdev_desc,
					    WdevCmplDescCoherenceInfo *const coherence_info,
					    WdevRxCmplDescriptorInfo *const desc_info);

#endif /* WDEV_IF_HW_ARCH_WCN7760_WDEV_IF_HW_ARCH_WCN7760_H */
