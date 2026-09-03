// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NEP ring management instance
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "ring_manager_instance.h"

#include <linux/errno.h>

#include <common/ring_id.h>
#include "wlan_ring_manager_instance.h"
#include "modem_ring_manager_instance.h"
#else /* linux */
#include "ring_mgmt/ring_manager_instance.h"

#include <cstdint>
#include <cerrno>

#include "common/ring_id.h"
#include "ring_mgmt/wlan_ring_manager_instance.h"
#include "ring_mgmt/modem_ring_manager_instance.h"
#endif /* linux */

static struct NoaRingManagerInfoNetwork *NetengineInstance(void);

struct NoaRingManagerInfoRoot *NoaRingManagerRootInstance(void)
{
	static struct NoaRingManagerInfoNetwork *networks[kNoaNetworkInterfaceMax] = { 0 };
	static struct NoaRingManagerInfoRoot root = {
		.num = kNoaNetworkInterfaceMax,
		.entries = &networks[0],
	};

	root.entries[kNoaNetworkInterfaceWlan] = NoaRingManagerInfoWlanInstance();
	root.entries[kNoaNetworkInterfaceModem] = NoaRingManagerInfoModemInstance();
	root.entries[kNoaNetworkInterfaceNetengine] = NetengineInstance();
	return &root;
}

static struct NoaRingManagerInfoNetwork *NetengineInstance(void)
{
	int32_t i;
	static struct NoaRingManagerInfo types[kNoaNetengineRingTypeMax];
	static struct NoaRingManagerInfoFlow flow = {
		.num = kNoaNetengineRingTypeMax,
		.entries = &types[0],
	};
	static struct NoaRingManagerInfoFlow *tunnels[kNoaNetengineFlowMax] = { 0 };
	static struct NoaRingManagerInfoNetwork netengine = {
		.num = kNoaNetengineFlowMax,
		.entries = &tunnels[0],
	};
	for (i = 0; i < kNoaNetengineRingTypeMax; ++i) {
		NoaRingManagerInfoInit(&types[i]);
	}
	types[kNoaNetengineRingData].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
	types[kNoaNetengineRingData].entries[kNoaRingNepInput].name = "NetEngineDataInNep";
	types[kNoaNetengineRingData].entries[kNoaRingNepOutput].name = "NetEngineDataOutNep";
	types[kNoaNetengineBufferPool].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineBufferPool);
	types[kNoaNetengineBufferPool].entries[kNoaRingNepInput].name = "NetengineBufferIn";
	types[kNoaNetengineBufferPool].entries[kNoaRingNepOutput].name = "NetengineBufferOut";

	types[kNoaNetworkStackRing].path_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetworkStackRing);
	types[kNoaNetworkStackRing].entries[kNoaRingNepInput].name = "NetworkStackIn";
	types[kNoaNetworkStackRing].entries[kNoaRingNepOutput].name = "NetworkStackOut";
	tunnels[kNoaNetengineTunnel] = &flow;

	return &netengine;
}
