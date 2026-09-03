/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Include all configuration files for SantaFe.
 *
 * Copyright (C) 2024-2025 Google LLC
 */

#ifndef __SANTAFE_CONFIG_H__
#define __SANTAFE_CONFIG_H__

#define DRIVER_NAME "santafe"

#define EDGETPU_NUM_CORES 1

/* Maximum number of telemetry buffers, one for TPU CPU and one for DIVE. */
#define EDGETPU_MAX_TELEMETRY_BUFFERS 2

/* Max number of PASIDs that the IOMMU supports simultaneously */
#define EDGETPU_NUM_PASIDS 16
/* Max number of virtual context IDs that can be allocated for one device. */
#define EDGETPU_NUM_VCIDS 16

/* Does not detach IOMMU domains when no wakelock held, client keeps a constant PASID. */
#define HAS_DETACHABLE_IOMMU_DOMAINS 0

/* Page faults are always real faults, never speculative, report errors. */
#define EDGETPU_REPORT_PAGE_FAULT_ERRORS 1

/* Number of TPU clusters for metrics handling. */
#define EDGETPU_TPU_CLUSTER_COUNT 1

/* Number of power islands for metric handling. */
#define EDGETPU_POWER_ISLAND_COUNT 2

/*
 * TZ Mailbox ID for secure workloads.  Must match firmware kTzMailboxId value for the chip,
 * but note firmware uses a zero-based index vs. kernel passing a one-based value here.
 * For this chip the value is not an actual mailbox index, but just an otherwise unused value
 * agreed upon with firmware for this purpose.
 */
#define EDGETPU_TZ_MAILBOX_ID 31

/* A special client ID for secure workloads pre-agreed with firmware (kTzRealmId). */
#define EDGETPU_EXT_TZ_CONTEXT_ID 0x40000000

#define EDGETPU_HAS_GSA 1

/* Allow loading of non-secure firmware images. */
#define EDGETPU_ALLOW_NONSECURE_FW 1

#define EDGETPU_HAS_FW_DEBUG 1

#define EDGETPU_USE_LITEBUF_VII 1

/* BCL mitigation CSRs default values. */
#define MITIGATION_RESPONSE_EN_DEFAULT			0x4f
#define MITIGATION_RESPONSE_TYPE_DEFAULT		0x8d
#define MITIGATION_RESPONSE_HYST_DEFAULT		0xff
#define LIGHT_MITIGATION_DIV_RATIO_DEFAULT		0x2
#define HEAVY_MITIGATION_DIV_RATIO_DEFAULT		0x4
#define THERMAL_HEAVY_MITIGATION_DIV_RATIO_DEFAULT	0x4
#define MITIGATION_FLL_STEP_DOWN_DEFAULT		0x0


/* This platform supports post-quantum firmware image authentication */
#define EDGETPU_HAS_PQ_FW_AUTH 1

#include "config-csrs.h"
#include "config-mailbox.h"

#endif /* __SANTAFE_CONFIG_H__ */
