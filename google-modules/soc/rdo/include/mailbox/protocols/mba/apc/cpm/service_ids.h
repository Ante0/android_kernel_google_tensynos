/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright 2022, Google LLC

#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_APC_CPM_SERVICE_IDS_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_APC_CPM_SERVICE_IDS_H

#include <interfaces/protocols/mba/apc/common/service_ids.h>

// Mailbox specific service IDs - service IDs unique to APC and CPM
enum ap_ns_cpm_mba_service_id {
	APC_CPM_MAILBOX_SERVICE_LAST = 0x38,
	// Thermal related service
	APC_CPM_THERMAL_SERVICE = 0x6F,
	APC_CPM_MAILBOX_SERVICE_FIRST = 0x70,
};

static_assert((uint32_t)APC_CPM_MAILBOX_SERVICE_LAST >
		       (uint32_t)APC_COMMON_SERVICE_LAST,
	       "APC CPM service count out of range");

#endif
