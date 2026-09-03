/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Wade Shih <wadeshih@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_WLAN_RING_ID_H__
#define __NOA_WLAN_RING_ID_H__

#ifdef linux
#include <linux/kernel.h>

#include <common/ring_id.h>
#else /* linux */
#include "common/ring_id.h"
#endif /* linux */

enum NoaWlanHostToDeviceRingType {
	kNoaWlanRingTxData = NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT,
	kNoaWlanRingRefillRx,
	kNoaWlanHostToDeviceRingMax
};
static_assert(kNoaWlanHostToDeviceRingMax <= NOA_RING_CATEGORY_MASK + 1);

enum NoaWlanDeviceToHostRing {
	kNoaWlanRingRxData = NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT,
	kNoaWlanRingTxCpl,
	kNoaWlanNepBufferPool,
	kNoaWlanDeviceToHostRingMax
};
static_assert(kNoaWlanDeviceToHostRingMax <= NOA_RING_CATEGORY_MASK + 1);

enum NoaWlanDirectHostToDeviceRingType {
	kNoaWlanDirectH2DRingTxData = 0,
	kNoaWlanDirectH2DRingVendorRxBufferReplenish,
	kNoaWlanDirectH2DRingNoaTxBufferReplenish,
	kNoaWlanDirectH2DRingFeedback,
	kNoaWlanDirectH2DRingMax,
};
static_assert(kNoaWlanDirectH2DRingMax <= NOA_RING_CATEGORY_MASK + 1);

enum NoaWlanDirectDeviceToHostRing {
	kNoaWlanDirectD2HRingRxData,
	kNoaWlanDirectD2HRingTxCpl,
	kNoaWlanDirectD2HRingRxFallback,
	kNoaWlanDirectD2HRingMax,
};
static_assert(kNoaWlanDirectD2HRingMax <= NOA_RING_CATEGORY_MASK + 1);

#endif /* __NOA_WLAN_RING_ID_H__ */
// NOLINTEND
