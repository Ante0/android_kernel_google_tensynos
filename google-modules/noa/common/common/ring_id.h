/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NOA Ring ID
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_RING_ID_H__
#define __NOA_RING_ID_H__
#ifdef linux
#include <linux/types.h>
#include <linux/kernel.h>

#include <common/core.h>
#else /* linux */
#include <cstdint>

#include "common/core.h"
#endif /* linux */

#define RING_ID_BIT_MASK(n) ((uint32_t)((1U << (n)) - 1))

#define NOA_RING_CATEGORY_BITS (4U)
#define NOA_RING_CATEGORY_MASK RING_ID_BIT_MASK(NOA_RING_CATEGORY_BITS)
#define NOA_RING_CATEGORY_OFFSET (0U)

#define NOA_NETWORK_FLOW_BITS (1U)
#define NOA_NETWORK_FLOW_MASK RING_ID_BIT_MASK(NOA_NETWORK_FLOW_BITS)
#define NOA_NETWORK_FLOW_OFFSET (NOA_RING_CATEGORY_OFFSET + NOA_RING_CATEGORY_BITS)

#define NOA_NETWORK_INTERFACE_BITS (3U)
#define NOA_NETWORK_INTERFACE_MASK RING_ID_BIT_MASK(NOA_NETWORK_INTERFACE_BITS)
#define NOA_NETWORK_INTERFACE_OFFSET (NOA_NETWORK_FLOW_OFFSET + NOA_NETWORK_FLOW_BITS)

#define NOA_RING_ID_TOTAL_BITS (NOA_NETWORK_INTERFACE_OFFSET + NOA_NETWORK_INTERFACE_BITS)

static_assert(NOA_RING_ID_TOTAL_BITS <= 8U);

#define NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT (0U)

static inline uint8_t NoaFeedbckPathIdFromDataPath(uint8_t path_id)
{
	return ((path_id & ~(NOA_RING_CATEGORY_MASK)) | NOA_DEFAULT_RING_ID_FOR_FEEDBACK_EVENT) ^
	       (NOA_NETWORK_FLOW_MASK << NOA_NETWORK_FLOW_OFFSET);
}

enum NoaNetworkInterface {
	kNoaNetworkInterfaceWlan = 0,
	kNoaNetworkInterfaceWlanDirect,
	kNoaNetworkInterfaceModem,
	kNoaNetworkInterfaceModemApcNcp,
	kNoaNetworkInterfaceNetengine,
	kNoaNetworkInterfaceMax
};
static_assert(kNoaNetworkInterfaceMax <= NOA_NETWORK_INTERFACE_MASK + 1);

enum NoaNetworkFlow {
	kNoaNetworkFlowHostToDevice = 0,
	kNoaNetworkFlowDeviceToHost,
	kNoaNetworkFlowMax
};
static_assert(kNoaNetworkFlowMax <= NOA_NETWORK_FLOW_MASK + 1);

enum NoaNetengineFlow { kNoaNetengineTunnel = 0, kNoaNetengineFlowMax };
static_assert(kNoaNetengineFlowMax <= NOA_NETWORK_FLOW_MASK + 1);

enum NoaNetengineRingType {
	kNoaNetengineRingData = 0,
	kNoaNetengineBufferPool,
	kNoaNetworkStackRing,
	kNoaNetengineRingTypeMax
};
static_assert(kNoaNetengineRingTypeMax <= NOA_RING_CATEGORY_MASK + 1);

#ifdef __cplusplus
constexpr static inline uint8_t NoaRingPathIdConvert(uint8_t interface, uint8_t direction,
						     uint8_t category)
{
	return ((interface & NOA_NETWORK_INTERFACE_MASK) << NOA_NETWORK_INTERFACE_OFFSET) |
	       ((direction & NOA_NETWORK_FLOW_MASK) << NOA_NETWORK_FLOW_OFFSET) |
	       ((category & NOA_RING_CATEGORY_MASK));
}
#else /* __cplusplus */
#define NoaRingPathIdConvert(interface, direction, category)                                       \
	((uint8_t)(((interface & NOA_NETWORK_INTERFACE_MASK) << NOA_NETWORK_INTERFACE_OFFSET) |    \
		   ((direction & NOA_NETWORK_FLOW_MASK) << NOA_NETWORK_FLOW_OFFSET) |              \
		   ((category & NOA_RING_CATEGORY_MASK))))
#endif /* __cplusplus */

static inline bool NoaIsHostToDevicePath(uint8_t path_id)
{
	return !(path_id & (kNoaNetworkFlowDeviceToHost << NOA_NETWORK_FLOW_OFFSET));
}

static inline void NoaRingPathIdParse(const uint8_t id, uint8_t *interface, uint8_t *flow,
				      uint8_t *category)
{
	*category = (id & NOA_RING_CATEGORY_MASK);
	*flow = ((id >> NOA_NETWORK_FLOW_OFFSET) & NOA_NETWORK_FLOW_MASK);
	*interface = ((id >> NOA_NETWORK_INTERFACE_OFFSET) & NOA_NETWORK_INTERFACE_MASK);
}

enum NoaRingNepDirection {
	kNoaRingNepInput = 0,
	kNoaRingNepOutput = 1,
	// Represents a non-specific direction. This is typically used
	// for the rings directly operated by AP and NCP regardless of
	// their configured direction.
	kNoaNepRingAnyDirection = 2,
	kNoaRingNepDirectionMax
};

static inline uint8_t NoaRingOutputPathToPort(uint8_t interface, uint8_t flow)
{
	switch (interface) {
	case kNoaNetworkInterfaceWlan:
		return (flow == kNoaNetworkFlowHostToDevice) ? NOA_PORT_WLAN_FW : NOA_PORT_WLAN_SW;
	case kNoaNetworkInterfaceModem:
		return (flow == kNoaNetworkFlowHostToDevice) ? NOA_PORT_MODEM_FW :
							       NOA_PORT_MODEM_SW;
	case kNoaNetworkInterfaceNetengine:
		return NOA_PORT_NETENGINE;
	default:
		break;
	}
	return NOA_PORT_MAX;
}

static inline uint8_t NoaRingOutputPathIdToPort(uint8_t id)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;
	NoaRingPathIdParse(id, &interface, &flow, &category);
	return NoaRingOutputPathToPort(interface, flow);
}

static inline uint8_t NoaRingInputPathToPort(uint8_t interface, uint8_t flow)
{
	switch (interface) {
	case kNoaNetworkInterfaceWlan:
		return (flow == kNoaNetworkFlowHostToDevice) ? NOA_PORT_WLAN_SW : NOA_PORT_WLAN_FW;
	case kNoaNetworkInterfaceModem:
		return (flow == kNoaNetworkFlowHostToDevice) ? NOA_PORT_MODEM_SW :
							       NOA_PORT_MODEM_FW;
	case kNoaNetworkInterfaceNetengine:
		return NOA_PORT_NETENGINE;
	default:
		break;
	}
	return NOA_PORT_MAX;
}

static inline uint8_t NoaRingInputPathIdToPort(uint8_t id)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;
	NoaRingPathIdParse(id, &interface, &flow, &category);
	return NoaRingInputPathToPort(interface, flow);
}

#endif /* __NOA_RING_ID_H__ */
// NOLINTEND
