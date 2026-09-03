/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright 2022, Google LLC

#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_AP_NS_MAILBOX_SERVICE_IDS_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_AP_NS_MAILBOX_SERVICE_IDS_H

#include <mailbox/protocols/mba/cpm/common/service_ids.h>

#define CPM_AP_NS_PAYLOAD_SIZE (4)
// Mailbox specific service IDs - each new value should be added with a value one less than the
// current lowest service ID value. If the service ID value becomes equal to SERVICE_LAST, adjust
// the highest common value for this mailbox array or update the protocol to include more services
enum cpm_ap_ns_mba_service_id {
	CPM_AP_NS_MAILBOX_SERVICE_LAST = 0x38,
	CPM_AP_NS_SSRAM_SERVICE = 0x66,
	CPM_AP_NS_POWER_DASH_MB_SERVICE = 0x68,
	CPM_AP_NS_ODPM_MB_SERVICE = 0x69,
	// Control latency library CPM functions
	CPM_AP_NS_LTC_MB_SERVICE = 0x70,
	CPM_AP_NS_MAILBOX_SERVICE_FIRST = 0x70,
};

static_assert((uint32_t)CPM_AP_NS_MAILBOX_SERVICE_LAST >
		       (uint32_t)CPM_COMMON_SERVICE_LAST,
	       "mba ap ns service count out of range");

// CPM_AP_COMMON_GSLC_MB_SERVICE
// Control GSLC

/* Mailbox command defines for arg1 of mailbox messages/requests */
#define GSLC_MBA_COMMAND_MASK  (0xFF000000U)
#define GSLC_MBA_COMMAND_SHIFT (24)

/* GSLC mailbox commands. (Using non zero values to be explicit) */
enum gslc_mba_cmds {
	CMD_ID_PARTITION_ENABLE = 1, /* Partition enable command */
	CMD_ID_PARTITION_DISABLE, /* Partition disable command */
	CMD_ID_PARTITION_MUTATE, /* Partition mutate command */
	CMD_ID_GET_CACHEDUMP_STATUS, /* Cache dump status command */
};

// CPM_AP_COMMON_GSLC_MB_SERVICE

// CPM_AP_COMMON_GMC_MB_SERVICE
// Manage GMC
//
// Request: GMC_MBA_MSG_ID_DVFS_REQ | GMC_MBA_MSG_ID_GET_CUR_PF_STATE
//
// Word 0: Queue mode header
// Word 1: DVFS arg or current PF state arg
//  bit [ 31 - 16 | 15 - 0 ]
//      | arg 1   | cmd ID |
// DVFS arg:
//  bit [ 31 - 16            | 15 - 0     ]
//      | Requested PF state | DVFS cmd ID |
//
// Get Current PF state arg:
//  bit [ 31 - 16 | 15 - 0            ]
//      | unused  | Curr state CMD ID |
//
// Word 2: Reserved
// Word 3: Reserved
//
// Response: GMC_MBA_MSG_ID_DVFS_REQ | GMC_MBA_MSG_ID_GET_CUR_PF_STATE
//
// Word 0: Queue mode header
// Word 1: GMC error
// GMC_MBA_MSG_ID_GET_CUR_PF_STATE Response:
// Word 2: Current PF state
// Word 3: Reserved
// CPM_AP_COMMON_GMC_MB_SERVICE

#endif
