#ifndef MODULE_WLAN_RING_MANAGER_WLAN_RING_MANAGER_H
#define MODULE_WLAN_RING_MANAGER_WLAN_RING_MANAGER_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "modules/wlan_ring/wlan_ring.h"

#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
#define NUM_WDEV_TX_POST_RING (80U)
#define NUM_WDEV_RX_POST_RING (1U)
#define NUM_WDEV_TX_CMPL_RING (1U)
#define NUM_WDEV_RX_CMPL_RING (1U)
#define NUM_WDEV_CTRL_POST_RING (1U)
#define NUM_WDEV_CTRL_CMPL_RING (1U)
#else
#define NUM_WDEV_TX_POST_RING (1U)
#define NUM_WDEV_RX_POST_RING (1U)
#define NUM_WDEV_TX_CMPL_RING (1U)
#define NUM_WDEV_RX_CMPL_RING (1U)
#define NUM_WDEV_CTRL_POST_RING (1U)
#define NUM_WDEV_CTRL_CMPL_RING (1U)
#endif
#define NUM_NEP_TX_POST_RING (1U)
#define NUM_NEP_TX_CMPL_RING (1U)
#define NUM_NEP_RX_CMPL_RING (1U)
#define NUM_NEP_FEEDBACK_RING (1U)
#define NUM_APC_DIRECT_TX_POST_RING (1U)
#define NUM_APC_DIRECT_RX_CMPL_RING (1U)
#define NUM_APC_TX_CMPL_RING (1U)
#define NUM_APC_VENDOR_RX_BUF_REPLN_RING (1U)
#define NUM_APC_NOA_TX_BUF_REPLN_RING (1U)
#define NUM_APC_FEEDBACK_RING (1U)
#define NUM_APC_FALLBACK_RX_CMPL_RING (1U)

typedef enum RingGroupType {
	kRingGroupTypeStart = 0,
	// WiFi device <-> NCP rings
	kWdevTxPostRingGroup = kRingGroupTypeStart,
	kWdevRxCmplRingGroup,
	kWdevTxCmplRingGroup,
	kWdevRxPostRingGroup,
	kWdevCtrlPostRingGroup,
	kWdevCtrlCmplRingGroup,
	// NEP <-> NCP rings
	kNepTxPostRingGroup,
	kNepRxCmplRingGroup,
	kNepTxCmplRingGroup,
	kNepFeedbackRingGroup,
	// APC <-> NCP rings
	kApcDirectTxPostRingGroup,
	kApcDirectRxCmplRingGroup,
	kApcTxCmplRingGroup,
	kApcVendorRxBufReplnRingGroup,
	kApcNoaTxBufReplnRingGroup,
	kApcFeedbackRingGroup,
	kApcFallbackRxCmplRingGroup,
	kRingGroupTypeEnd,
	kRingGroupTypeNum = kRingGroupTypeEnd,
} RingGroupType;

enum {
	kRingStart = 0,
	// WiFi device <-> NCP rings
	kWdevTxPostRingStart = kRingStart,
	kWdevTxPostRingEnd = kWdevTxPostRingStart + NUM_WDEV_TX_POST_RING,
	kWdevTxCmplRingStart = kWdevTxPostRingEnd,
	kWdevTxCmplRingEnd = kWdevTxCmplRingStart + NUM_WDEV_TX_CMPL_RING,
	kWdevRxPostRingStart = kWdevTxCmplRingEnd,
	kWdevRxPostRingEnd = kWdevRxPostRingStart + NUM_WDEV_RX_POST_RING,
	kWdevRxCmplRingStart = kWdevRxPostRingEnd,
	kWdevRxCmplRingEnd = kWdevRxCmplRingStart + NUM_WDEV_RX_CMPL_RING,
	kWdevCtrlPostRingStart = kWdevRxCmplRingEnd,
	kWdevCtrlPostRingEnd = kWdevCtrlPostRingStart + NUM_WDEV_CTRL_POST_RING,
	kWdevCtrlCmplRingStart = kWdevCtrlPostRingEnd,
	kWdevCtrlCmplRingEnd = kWdevCtrlCmplRingStart + NUM_WDEV_CTRL_CMPL_RING,
	// NEP <-> NCP rings
	kNepTxPostRingStart = kWdevCtrlCmplRingEnd,
	kNepTxPostRingEnd = kNepTxPostRingStart + NUM_NEP_TX_POST_RING,
	kNepRxCmplRingStart = kNepTxPostRingEnd,
	kNepRxCmplRingEnd = kNepRxCmplRingStart + NUM_NEP_RX_CMPL_RING,
	kNepTxCmplRingStart = kNepRxCmplRingEnd,
	kNepTxCmplRingEnd = kNepTxCmplRingStart + NUM_NEP_TX_CMPL_RING,
	kNepFeedbackRingStart = kNepTxCmplRingEnd,
	kNepFeedbackRingEnd = kNepFeedbackRingStart + NUM_NEP_FEEDBACK_RING,
	// APC <-> NCP rings
	kApcDirectTxPostRingStart = kNepFeedbackRingEnd,
	kApcDirectTxPostRingEnd = kApcDirectTxPostRingStart + NUM_APC_DIRECT_TX_POST_RING,
	kApcDirectRxCmplRingStart = kApcDirectTxPostRingEnd,
	kApcDirectRxCmplRingEnd = kApcDirectRxCmplRingStart + NUM_APC_DIRECT_RX_CMPL_RING,
	kApcTxCmplRingStart = kApcDirectRxCmplRingEnd,
	kApcTxCmplRingEnd = kApcTxCmplRingStart + NUM_APC_TX_CMPL_RING,
	kApcVendorRxBufReplnRingStart = kApcTxCmplRingEnd,
	kApcVendorRxBufReplnRingEnd =
		kApcVendorRxBufReplnRingStart + NUM_APC_VENDOR_RX_BUF_REPLN_RING,
	kApcNoaTxBufReplnRingStart = kApcVendorRxBufReplnRingEnd,
	kApcNoaTxBufReplnRingEnd = kApcNoaTxBufReplnRingStart + NUM_APC_NOA_TX_BUF_REPLN_RING,
	kApcFeedbackRingStart = kApcNoaTxBufReplnRingEnd,
	kApcFeedbackRingEnd = kApcFeedbackRingStart + NUM_APC_FEEDBACK_RING,
	kApcFallbackRxCmplRingStart = kApcFeedbackRingEnd,
	kApcFallbackRxCmplRingEnd = kApcFallbackRxCmplRingStart + NUM_APC_FALLBACK_RX_CMPL_RING,
	kRingEnd = kApcFallbackRxCmplRingEnd,
	kRingNum = kRingEnd,
};

typedef struct WlanRingGroup {
	uint32_t num_ring;
	WlanRing *ring_pool;
} WlanRingGroup;

typedef struct WlanRingManager {
	WlanRingGroup ring_groups[kRingGroupTypeNum];
	WlanRing ring_pool[kRingNum];
} WlanRingManager;

static inline uint32_t WlanRingManagerGetRingNum(WlanRingManager *const manager, RingGroupType type)
{
	if (manager && type < kRingGroupTypeNum) {
		return manager->ring_groups[type].num_ring;
	}

	return 0;
}

static inline WlanRing *WlanRingManagerGetRingPool(WlanRingManager *const manager,
						   RingGroupType type)
{
	if (manager && type < kRingGroupTypeNum) {
		return manager->ring_groups[type].ring_pool;
	}

	return NULL;
}

/// @brief Get a ring from the ring manager.
///
/// @param[in] manager The ring manager.
/// @param[in] type The type of ring group.
/// @param[in] ring_id The ID of the ring.
/// @param[out] ring Output ring.
/// @return 0 on success, -ENODEV on failure.
static inline int32_t WlanRingManagerGetRing(WlanRingManager *const manager, RingGroupType type,
					     uint32_t ring_id, WlanRing **ring)
{
	uint32_t ring_num = WlanRingManagerGetRingNum(manager, type);
	WlanRing *ring_pool = WlanRingManagerGetRingPool(manager, type);

	*ring = NULL;

	if (ring_pool && ring_id < ring_num) {
		*ring = &ring_pool[ring_id];
		return 0;
	}

	return -ENODEV;
}

/// @brief Initialize a WLAN ring manager.
///
/// @param[in] manager The ring manager.
/// @return 0 on success, a negative error code on failure.
extern int32_t WlanRingManagerInit(WlanRingManager *const manager);

/// @brief Deinitialize a WLAN ring manager.
///
/// @param[in] manager The ring manager.
extern void WlanRingManagerDeinit(WlanRingManager *const manager);

/// @brief Add a ring to the ring manager.
///
/// @param[in] manager The ring manager.
/// @param[in] type The type of ring group.
/// @param[in] ring_id The ID of the ring.
/// @param[in] params The initialization parameters for the ring.
///
/// @return 0 on success, a negative error code on failure.
extern int32_t WlanRingManagerAddRing(WlanRingManager *const manager, RingGroupType type,
				      uint32_t ring_id, const WlanRingInitParams *const params);

/// @brief Remove a ring from the ring manager.
///
/// @param[in] manager The ring manager.
/// @param[in] type The type of ring group.
/// @param[in] ring_id The ID of the ring.
///
/// @return 0 on success, a negative error code on failure.
extern int32_t WlanRingManagerRemoveRing(WlanRingManager *const manager, RingGroupType type,
					 uint32_t ring_id);

/// @brief Activate a ring in the ring manager.
///
/// @param[in] manager The ring manager.
/// @param[in] type The type of ring group.
/// @param[in] ring_id The ID of the ring.
///
/// @return 0 on success, a negative error code on failure.
extern int32_t WlanRingManagerActivateRing(WlanRingManager *const manager, RingGroupType type,
					   uint32_t ring_id);

/// @brief Deactivate a ring in the ring manager.
///
/// @param[in] manager The ring manager.
/// @param[in] type The type of ring group.
/// @param[in] ring_id The ID of the ring.
///
/// @return 0 on success, a negative error code on failure.
extern int32_t WlanRingManagerDeactivateRing(WlanRingManager *const manager, RingGroupType type,
					     uint32_t ring_id);

#endif /* MODULE_WLAN_RING_MANAGER_WLAN_RING_MANAGER_H */
