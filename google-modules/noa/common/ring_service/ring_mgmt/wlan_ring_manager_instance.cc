// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Wade Shih <wadeshih@google.com>
 */

#ifdef linux
#include "wlan_ring_manager_instance.h"

#include <linux/kernel.h>

#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#else /* linux */
#include "ring_mgmt/wlan_ring_manager_instance.h"

#include <cstdint>

#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#endif /* linux */

static struct NoaRingManagerInfoFlow *WlanHostToDeviceInstance(void)
{
	int32_t i;
	static struct NoaRingManagerInfo types[kNoaWlanHostToDeviceRingMax];
	static struct NoaRingManagerInfoFlow h2d = {
		.num = kNoaWlanHostToDeviceRingMax,
		.entries = &types[0],
	};
	for (i = 0; i < kNoaWlanHostToDeviceRingMax; ++i) {
		NoaRingManagerInfoInit(&types[i]);
	}
	types[kNoaWlanRingTxData].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);
	types[kNoaWlanRingTxData].entries[kNoaRingNepInput].name = "WlanTxDataInNep";
	types[kNoaWlanRingTxData].entries[kNoaRingNepOutput].name = "WlanTxDataOutNep";
	types[kNoaWlanRingRefillRx].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice, kNoaWlanRingRefillRx);
	types[kNoaWlanRingRefillRx].entries[kNoaRingNepInput].name = "WlanRxRefillInNep";
	types[kNoaWlanRingRefillRx].entries[kNoaRingNepOutput].name = "WlanRxRefillOutNep";
	return &h2d;
}

static struct NoaRingManagerInfoFlow *WlanDeviceToHostInstance(void)
{
	int32_t i;
	static struct NoaRingManagerInfo types[kNoaWlanDeviceToHostRingMax];
	static struct NoaRingManagerInfoFlow d2h = {
		.num = kNoaWlanDeviceToHostRingMax,
		.entries = &types[0],
	};
	for (i = 0; i < kNoaWlanDeviceToHostRingMax; ++i) {
		NoaRingManagerInfoInit(&types[i]);
	}
	types[kNoaWlanRingRxData].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);
	types[kNoaWlanRingRxData].entries[kNoaRingNepInput].name = "WlanRxDataInNep";
	types[kNoaWlanRingRxData].entries[kNoaRingNepOutput].name = "WlanRxDataOutNep";
	types[kNoaWlanRingTxCpl].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanRingTxCpl);
	types[kNoaWlanRingTxCpl].entries[kNoaRingNepInput].name = "WlanTxCplInNep";
	types[kNoaWlanRingTxCpl].entries[kNoaRingNepOutput].name = "WlanTxCplOutNep";
	types[kNoaWlanNepBufferPool].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool);
	types[kNoaWlanNepBufferPool].entries[kNoaRingNepInput].name = "WlanBufferPoolIn";
	types[kNoaWlanNepBufferPool].entries[kNoaRingNepOutput].name = "WlanBufferPoolOut";
	return &d2h;
}

struct NoaRingManagerInfoNetwork *NoaRingManagerInfoWlanInstance(void)
{
	static struct NoaRingManagerInfoFlow *flows[kNoaNetworkFlowMax] = { 0 };
	static struct NoaRingManagerInfoNetwork wlan = {
		.num = kNoaNetworkFlowMax,
		.entries = &flows[0],
	};
	flows[kNoaNetworkFlowHostToDevice] = WlanHostToDeviceInstance();
	flows[kNoaNetworkFlowDeviceToHost] = WlanDeviceToHostInstance();
	return &wlan;
}
