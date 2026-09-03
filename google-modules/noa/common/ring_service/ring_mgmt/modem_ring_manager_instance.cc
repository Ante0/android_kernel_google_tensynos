// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Sam Chao <samchao@google.com>
 */

#ifdef linux
#include "modem_ring_manager_instance.h"

#include <linux/kernel.h>

#include <common/ring_id.h>
#include <common/modem_ring_id.h>
#else /* linux */
#include "ring_mgmt/modem_ring_manager_instance.h"

#include <cstdint>

#include "common/ring_id.h"
#include "common/modem_ring_id.h"
#endif /* linux */

static struct NoaRingManagerInfoFlow *ModemHostToDeviceInstance(void)
{
	int32_t i;
	static struct NoaRingManagerInfo types[kNoaModemHostToDeviceRingMax];
	static struct NoaRingManagerInfoFlow h2d = {
		.num = kNoaModemHostToDeviceRingMax,
		.entries = &types[0],
	};
	for (i = 0; i < kNoaModemHostToDeviceRingMax; ++i) {
		NoaRingManagerInfoInit(&types[i]);
	}
	types[kNoaModemRingTxData].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
	types[kNoaModemRingTxData].entries[kNoaRingNepInput].name = "ModemTxDataInNep";
	types[kNoaModemRingTxData].entries[kNoaRingNepOutput].name = "ModemTxDataOutNep";
	return &h2d;
}

static struct NoaRingManagerInfoFlow *ModemDeviceToHostInstance(void)
{
	int32_t i;
	static struct NoaRingManagerInfo types[kNoaModemDeviceToHostRingMax];
	static struct NoaRingManagerInfoFlow d2h = {
		.num = kNoaModemDeviceToHostRingMax,
		.entries = &types[0],
	};
	for (i = 0; i < kNoaModemDeviceToHostRingMax; ++i) {
		NoaRingManagerInfoInit(&types[i]);
	}
	types[kNoaModemRingRxData].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemRingRxData);
	types[kNoaModemRingRxData].entries[kNoaRingNepInput].name = "ModemRxDataInNep";
	types[kNoaModemRingRxData].entries[kNoaRingNepOutput].name = "ModemRxDataOutNep";
	types[kNoaModemRingRxq0].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemRingRxq0);
	types[kNoaModemRingRxq0].entries[kNoaRingNepInput].name = "ModemRxq0InNep";
	types[kNoaModemRingRxq0].entries[kNoaRingNepOutput].name = "ModemRxq0OutNep";
	types[kNoaModemRingRxq1].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemRingRxq1);
	types[kNoaModemRingRxq1].entries[kNoaRingNepInput].name = "ModemRxq1InNep";
	types[kNoaModemRingRxq1].entries[kNoaRingNepOutput].name = "ModemRxq1OutNep";
	types[kNoaModemRingRxq2].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemRingRxq2);
	types[kNoaModemRingRxq2].entries[kNoaRingNepInput].name = "ModemRxq2InNep";
	types[kNoaModemRingRxq2].entries[kNoaRingNepOutput].name = "ModemRxq2OutNep";
	types[kNoaModemNepBufferPool].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool);
	types[kNoaModemNepBufferPool].entries[kNoaRingNepInput].name = "ModemBufferPoolIn";
	types[kNoaModemNepBufferPool].entries[kNoaRingNepOutput].name = "ModemBufferPoolOut";
	return &d2h;
}

struct NoaRingManagerInfoNetwork *NoaRingManagerInfoModemInstance(void)
{
	static struct NoaRingManagerInfoFlow *flows[kNoaNetworkFlowMax] = { 0 };
	static struct NoaRingManagerInfoNetwork modem = {
		.num = kNoaNetworkFlowMax,
		.entries = &flows[0],
	};
	flows[kNoaNetworkFlowHostToDevice] = ModemHostToDeviceInstance();
	flows[kNoaNetworkFlowDeviceToHost] = ModemDeviceToHostInstance();
	return &modem;
}
