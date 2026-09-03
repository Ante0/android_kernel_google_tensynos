/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NOA Ring ID
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_FLATTEN_RING_ID_H__
#define __NOA_FLATTEN_RING_ID_H__

#ifdef linux
#include <linux/errno.h>

#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include <common/modem_ring_id.h>
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa_ring_service_proxy_defs.h>
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#else /* linux */
#include <errno.h>

#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#endif /* linux */

#define kNoaUnknownRing (0)
#define kNoaWlanTxCplMax (1)

/* Rings that send data into NEP */
enum NoaInToNepRingId {
	kNoaUnknownRingInToNep = kNoaUnknownRing,
	/* Rings for wlan from Host to Device */
	kNoaWlanH2DTxDataInToNep,
	kNoaWlanH2DRefillRxInToNep,
	/* Rings for wlan from Device to Host */
	kNoaWlanD2HRxDataInToNep,
	// TxCpl ring may expand, this accounts for the need
	// to have more than one TxCpl ring.
	// kNoaWlanD2HTxCplInToNepEnd is not inclusive
	kNoaWlanD2HTxCplInToNep,
	kNoaWlanD2HTxCplInToNepEnd = kNoaWlanD2HTxCplInToNep + kNoaWlanTxCplMax,
        kNoaWlanD2HNepBufferPoolInToNep = kNoaWlanD2HTxCplInToNepEnd,
	/* Rings for Modem from Host to Device */
	kNoaModemH2DTxDataInToNep,
	/* Rings for Modem from Device to Host */
	kNoaModemD2HRxDataInToNep,
	kNoaModemD2HRxq0InToNep,
	kNoaModemD2HRxq1InToNep,
	kNoaModemD2HRxq2InToNep,
	kNoaModemD2HNepBufferPoolInToNep,
	kNoaNetengineH2DTunnelInToNep,
	kNoaNetengineBufferPoolInToNep,
	kNoaNetworkStackInToNep,
	kNoaInToNepRingMax,
};

/* Rings that receive data from NEP */
enum NoaOutOfNepRingId {
	/* Rings for wlan from Host to Device */
	// Starting value at kNoaInToNepRingMax to
	// sequentially following NoaInToNepRingId
	kNoaWlanH2DTxDataOutOfNep = kNoaInToNepRingMax,
	kNoaWlanH2DRefillRxOutOfNep,
	/* Rings for wlan from Device to Host */
	kNoaWlanD2HRxDataOutOfNep,
	kNoaWlanD2HTxCplOutOfNep,
	kNoaWlanD2HTxCplOutOfNepEnd = kNoaWlanD2HTxCplOutOfNep + kNoaWlanTxCplMax,
	/* Rings for Modem from Host to Device */
	kNoaModemH2DTxDataOutOfNep = kNoaWlanD2HTxCplOutOfNepEnd,
	/* Rings for Modem from Device to Host */
	kNoaModemD2HRxDataOutOfNep,
	kNoaModemD2HRxq0OutOfNep,
	kNoaModemD2HRxq1OutOfNep,
	kNoaModemD2HRxq2OutOfNep,
	kNoaNetengineH2DTunnelOutOfNep,
	kNoaNetworkStackOutOfNep,
	kNoaOutOfNepRingMax,
};

/* Rings that are directly operated by AP and NCP */
enum NoaApNcpDirectRingId {
	// Starting value at kNoaOutOfNepRingMax to
	// sequentially following NoaOutOfNepRingId
	kNoaWlanH2DTxDataApNcpDirect = kNoaOutOfNepRingMax,
	kNoaWlanH2DVendorRxBufferReplenishApNcpDirect,
	kNoaWlanH2DNoaTxBufferReplenishApNcpDirect,
	kNoaWlanH2DFeedbackApNcpDirect,
	kNoaWlanD2HRxDataApNcpDirect,
	kNoaWlanD2HTxCplApNcpDirect,
	kNoaWlanD2HRxFallbackApNcpDirect,
	kNoaModemApcNcpTxDrb0ApNcpDirect,
	kNoaModemApcNcpTxDrb1ApNcpDirect,
	kNoaModemApcNcpTxDrb2ApNcpDirect,
	kNoaModemApcNcpTxDrb3ApNcpDirect,
	kNoaModemApcNcpTxDrb4ApNcpDirect,
	kNoaModemApcNcpRxRefillNormalBat0ApNcpDirect,
	kNoaModemApcNcpRxRefillFragBat0ApNcpDirect,
	kNoaModemApcNcpRxRefillNormalBat1ApNcpDirect,
	kNoaModemApcNcpRxRefillFragBat1ApNcpDirect,
	kNoaApNcpDirectRingMax,
};

/**
 * The max count of Noa rings
 * Must align with GOOGLE_DPA_RING_MAX in dpa
 * under include/../google_dpa_ring_service_proxy_defs.h
 */
#define NOA_NEP_RING_MAX kNoaApNcpDirectRingMax

#ifdef linux
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
static_assert(NOA_NEP_RING_MAX == GOOGLE_DPA_RING_MAX);
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#endif /* linux */

/**
 * @brief Converts a 4-level ring ID to a flattened ID.
 *
 * This function maps a hierarchical, 4-level ring identifier (composed of
 * interface, flow, category, and direction) into a single, linear/flattened
 * ring ID. The mapping logic is specifically designed to handle rings that
 * serve as either an input to or an output from the Network Engine Processor
 * (NEP).
 *
 * @param[in] interface The network interface type, such as WLAN or Modem.
 * @param[in] flow      The data flow direction, e.g., Host-to-Device or
 *                      Device-to-Host.
 * @param[in] category  The specific ring type or category within the interface
 *                      and flow.
 * @param[in] direction The direction relative to the NEP, i.e., input or
 *                      output.
 *
 * @return The corresponding flattened ring ID. Returns `kNoaUnknownRing` if no
 *         valid mapping is found.
 */
static inline int8_t NoaRingIdMapping(uint8_t interface, uint8_t flow, uint8_t category,
				       uint8_t direction)
{
	if (direction == kNoaRingNepInput) {
		if (flow == kNoaNetworkFlowHostToDevice) {
			switch (interface) {
			case kNoaNetworkInterfaceWlan:
				switch (category) {
				case kNoaWlanRingTxData:
					return kNoaWlanH2DTxDataInToNep;
				case kNoaWlanRingRefillRx:
					return kNoaWlanH2DRefillRxInToNep;
				}
				break;
			case kNoaNetworkInterfaceModem:
				if (category == kNoaModemRingTxData)
					return kNoaModemH2DTxDataInToNep;
				break;
			case kNoaNetworkInterfaceNetengine:
				switch (category) {
				case kNoaNetengineRingData:
					return kNoaNetengineH2DTunnelInToNep;
				case kNoaNetengineBufferPool:
					return kNoaNetengineBufferPoolInToNep;
				case kNoaNetworkStackRing:
					return kNoaNetworkStackInToNep;
				}
				break;
			}
		} else { /* kNoaNetworkFlowDeviceToHost */
			switch (interface) {
			case kNoaNetworkInterfaceWlan:
				switch (category) {
				case kNoaWlanRingRxData:
					return kNoaWlanD2HRxDataInToNep;
				case kNoaWlanRingTxCpl:
					return kNoaWlanD2HTxCplInToNep;
				case kNoaWlanNepBufferPool:
					return kNoaWlanD2HNepBufferPoolInToNep;
				}
				break;
			case kNoaNetworkInterfaceModem:
				switch (category) {
				case kNoaModemRingRxData:
					return kNoaModemD2HRxDataInToNep;
				case kNoaModemRingRxq0:
					return kNoaModemD2HRxq0InToNep;
				case kNoaModemRingRxq1:
					return kNoaModemD2HRxq1InToNep;
				case kNoaModemRingRxq2:
					return kNoaModemD2HRxq2InToNep;
				case kNoaModemNepBufferPool:
					return kNoaModemD2HNepBufferPoolInToNep;
				}
				break;
			}
		}
	} else if (direction == kNoaRingNepOutput) {
		if (flow == kNoaNetworkFlowHostToDevice) {
			switch (interface) {
			case kNoaNetworkInterfaceWlan:
				switch (category) {
				case kNoaWlanRingTxData:
					return kNoaWlanH2DTxDataOutOfNep;
				case kNoaWlanRingRefillRx:
					return kNoaWlanH2DRefillRxOutOfNep;
				}
				break;
			case kNoaNetworkInterfaceModem:
				if (category == kNoaModemRingTxData)
					return kNoaModemH2DTxDataOutOfNep;
				break;
			case kNoaNetworkInterfaceNetengine:
				switch (category) {
				case kNoaNetengineRingData:
					return kNoaNetengineH2DTunnelOutOfNep;
				case kNoaNetengineBufferPool:
					return kNoaNetengineBufferPoolInToNep;
				case kNoaNetworkStackRing:
					return kNoaNetworkStackOutOfNep;
				}
				break;
			}
		} else { /* kNoaNetworkFlowDeviceToHost */
			switch (interface) {
			case kNoaNetworkInterfaceWlan:
				switch (category) {
				case kNoaWlanRingRxData:
					return kNoaWlanD2HRxDataOutOfNep;
				case kNoaWlanRingTxCpl:
					return kNoaWlanD2HTxCplOutOfNep;
				case kNoaWlanNepBufferPool: // Won't use
					return kNoaWlanD2HNepBufferPoolInToNep;
				}
				break;
			case kNoaNetworkInterfaceModem:
				switch (category) {
				case kNoaModemRingRxData:
					return kNoaModemD2HRxDataOutOfNep;
				case kNoaModemRingRxq0:
					return kNoaModemD2HRxq0OutOfNep;
				case kNoaModemRingRxq1:
					return kNoaModemD2HRxq1OutOfNep;
				case kNoaModemRingRxq2:
					return kNoaModemD2HRxq2OutOfNep;
				case kNoaModemNepBufferPool: // Won't use
					return kNoaModemD2HNepBufferPoolInToNep;
				}
				break;
			}
		}
	} else { /* kNoaNepRingAnyDirection */
		// Interfaces kNoaNetworkInterfaceWlanDirect (id = 1) and
		// kNoaNetworkInterfaceModemApcNcp (id = 3) fall into this direction
		// Any other interfaces do not apply
		if (flow == kNoaNetworkFlowHostToDevice) {
			switch (interface) {
			case kNoaNetworkInterfaceWlanDirect:
				switch (category) {
				case kNoaWlanDirectH2DRingTxData:
					return kNoaWlanH2DTxDataApNcpDirect;
				case kNoaWlanDirectH2DRingVendorRxBufferReplenish:
					return kNoaWlanH2DVendorRxBufferReplenishApNcpDirect;
				case kNoaWlanDirectH2DRingNoaTxBufferReplenish:
					return kNoaWlanH2DNoaTxBufferReplenishApNcpDirect;
				case kNoaWlanDirectH2DRingFeedback:
					return kNoaWlanH2DFeedbackApNcpDirect;
				}
				break;
			case kNoaNetworkInterfaceModemApcNcp:
				switch (category) {
				case kNoaModemRingTxDrb0:
					return kNoaModemApcNcpTxDrb0ApNcpDirect;
				case kNoaModemRingTxDrb1:
					return kNoaModemApcNcpTxDrb1ApNcpDirect;
				case kNoaModemRingTxDrb2:
					return kNoaModemApcNcpTxDrb2ApNcpDirect;
				case kNoaModemRingTxDrb3:
					return kNoaModemApcNcpTxDrb3ApNcpDirect;
				case kNoaModemRingTxDrb4:
					return kNoaModemApcNcpTxDrb4ApNcpDirect;
				case kNoaModemRingRxRefillNormalBat0:
					return kNoaModemApcNcpRxRefillNormalBat0ApNcpDirect;
				case kNoaModemRingRxRefillFragBat0:
					return kNoaModemApcNcpRxRefillFragBat0ApNcpDirect;
				case kNoaModemRingRxRefillNormalBat1:
					return kNoaModemApcNcpRxRefillNormalBat1ApNcpDirect;
				case kNoaModemRingRxRefillFragBat1:
					return kNoaModemApcNcpRxRefillFragBat1ApNcpDirect;
				}
				break;
			default:
				// Skipping interfaces
				// kNoaNetworkInterfaceWlan (id = 0)
				// kNoaNetworkInterfaceModem (id = 2)
				// kNoaNetworkInterfaceNetengine (id = 4)
				// Rings for these interfaces are accounted
				// in the InToNep and OutOfNep directions
				break;
			}
		} else { /* kNoaNetworkFlowDeviceToHost */
			switch (interface) {
			case kNoaNetworkInterfaceWlanDirect:
				switch (category) {
				case kNoaWlanDirectD2HRingRxData:
					return kNoaWlanD2HRxDataApNcpDirect;
				case kNoaWlanDirectD2HRingTxCpl:
					return kNoaWlanD2HTxCplApNcpDirect;
				case kNoaWlanDirectD2HRingRxFallback:
					return kNoaWlanD2HRxFallbackApNcpDirect;
				}
				break;
			default:
				// Skipping interfaces
				// kNoaNetworkInterfaceWlan (id = 0)
				// kNoaNetworkInterfaceModem (id = 2)
				// kNoaNetworkInterfaceNetengine (id = 4)
				// Rings for these interfaces are accounted
				// in the InToNep and OutOfNep directions
				break;
			}
		}
	}
	pr_err("Failed to map 4D ring id to 1D: interface %d flow %d category %d direction %d",
	       interface, flow, category, direction);
	return -EINVAL;
}

#endif /* __NOA_FLATTEN_RING_ID_H__ */
// NOLINTEND
