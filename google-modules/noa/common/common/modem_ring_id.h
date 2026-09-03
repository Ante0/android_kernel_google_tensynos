/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Sam Chao <samchao@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_MODEM_RING_ID_H__
#define __NOA_MODEM_RING_ID_H__

#ifdef linux
#include <linux/kernel.h>

#include <common/ring_id.h>
#else /* linux */
#include <common/ring_id.h>
#endif /* linux */

enum NoaModemHostToDeviceRingType {
	kNoaModemRingTxData = NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT,
	kNoaModemHostToDeviceRingMax
};
static_assert(kNoaModemHostToDeviceRingMax <= NOA_RING_CATEGORY_MASK + 1);

enum NoaModemDeviceToHostRing {
	kNoaModemRingRxData = NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT,
	kNoaModemRingRxq0,
	kNoaModemRingRxq1,
	kNoaModemRingRxq2,
	kNoaModemRingRxDataEnd,
	kNoaModemNepBufferPool = kNoaModemRingRxDataEnd,
	kNoaModemDeviceToHostRingMax
};
static_assert(kNoaModemDeviceToHostRingMax <= NOA_RING_CATEGORY_MASK + 1);

enum NoaModemApcToNcpRingType {
	kNoaModemRingTxDrb0 = NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT,
	kNoaModemRingTxDrb1,
	kNoaModemRingTxDrb2,
	kNoaModemRingTxDrb3,
	kNoaModemRingTxDrb4,
	kNoaModemRingRxRefillNormalBat0,
	kNoaModemRingRxRefillFragBat0,
	kNoaModemRingRxRefillNormalBat1,
	kNoaModemRingRxRefillFragBat1,
	kNoaModemApcToNcpRingMax
};
static_assert(kNoaModemApcToNcpRingMax <= NOA_RING_CATEGORY_MASK + 1);

#endif /* __NOA_MODEM_RING_ID_H__ */
// NOLINTEND
