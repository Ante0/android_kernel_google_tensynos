/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright 2022, Google LLC

#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SERVICE_IDS_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SERVICE_IDS_H

#define MBA_COMMON_MIN_SERVICE_ID 0x38
#define MBA_COMMON_MAX_SERVICE_ID 0x70

#define CPM_GDMC_PAYLOAD_SIZE (4)
#define CPM_GSA_PAYLOAD_SIZE (4)

// Common service IDs across all mailbox channels for CPM
enum cpm_common_mba_service_id {
	CPM_COMMON_SERVICE_FIRST = 0x0,
	// Test service
	CPM_COMMON_PING_SERVICE = 0x0,
	// Used for extracting logs
	CPM_AP_COMMON_LOG_SERVICE = 0x1,
	// Interact with tracepoint library
	CPM_AP_COMMON_TRACEPOINT_SERVICE = 0x2,
	// Control GSLC CPM functions
	CPM_AP_COMMON_GSLC_MB_SERVICE = 0x3,
	// Control GMCCPM functions
	CPM_AP_COMMON_GMC_MB_SERVICE = 0x4,
	// Control LPB peripheral
	CPM_COMMON_LPB_SERVICE = 0x5,
	// System reset service
	CPM_COMMON_RESET_SERVICE = 0x6,
	// ODPM API service for AP
	CPM_AP_COMMON_ODPM_MB_SERVICE = 0x7,
	// LPCM service.
	CPM_COMMON_LPCM_SERVICE = 0x8,
	// Control and modify SoC power states
	CPM_COMMON_POWER_STATE_SERVICE = 0x9,
	// PMIC related services
	CPM_COMMON_PMIC_SERVICE = 0xA,
	// MIPM interface
	CPM_COMMON_MIPM_SERVICE = 0xB,
	// Thermal services - emul temps, loops, config, etc
	CPM_COMMON_THERMAL_SERVICE = 0xC,
	// Pull PIF history status over LMF.
	CPM_COMMON_PIF_HISTORY_SERVICE = 0xD,
	// Pwrblk/SSWRP based service
	CPM_COMMON_PWRBLK_SERVICE = 0xE,
	// BCL based service
	CPM_COMMON_BCL_SERVICE = 0x12,
	// SSR Triggering service
	CPM_COMMON_SSR_SERVICE = 0x18,
	CPM_COMMON_SERVICE_LAST = 0x37,
};

enum cpm_common_mba_error {
	CPM_COMMON_PROT_NO_ERR = 0xf,
	CPM_COMMON_PROT_GENERIC_ERR,
};

/* SERVICE_ID: CPM_AP_COMMON_DEFAULT_SERVICE
 *
 * Test service for CPM mba
 * Queue mode:
 * Word 0: Queue mode header
 * Word 1: integer which will be incremented
 * Word 2-3: Ticks when CPM received message, according to SST
 *
 * Polling Mode:
 * Word 0: Header and data (whatever is sent in data is incremented by 1)
 *  bit [ 31 - 16             | 15-0  ]
 *      | polling mode header | value |
 *
 * RESP:
 * Word 0: Header and data (whatever is sent in data is incremented by 1)
 *  bit [ 31 - 16             | 15-0      ]
 *      | polling mode header | value + 1 |
 * RESP:
 *
 * SERVICE_ID: CPM_AP_COMMON_DEFAULT_SERVICE
 */

/* SERVICE_ID: CPM_AP_COMMON_LOG_SERVICE
 *
 * CPM Logging, dump logs to DRAM for APC
 *
 * Request: DUMP LOGS
 * Word 0: Queue mode header
 * Word 1: DRAM offset
 * Word 2: unused
 * Word 3: unused
 *
 * Response: DUMP LOGS
 * Word 0: Queue mode header
 * Word 1: Count of logs dumped
 * Word 2: unused
 * Word 3: unused
 *
 * SERVICE_ID: CPM_AP_COMMON_LOG_SERVICE
 */

/* SERVICE_ID: CPM_AP_COMMON_TRACEPOINT_SERVICE
 *
 * CPM tracepoint library, profile CPM
 *
 * Request: TRACEPOINT_STRING_REQUEST | TRACEPOINT_REQUEST
 * Word 0: Queue mode header
 * Word 1: Command ID
 * Word 2: DRAM addr for tracepoints
 * Word 3: size of buffer to store TPs
 *
 * Response: TRACEPOINT_STRING_REQUEST
 * Word 0: Queue mode header
 * Word 1: LK Error code
 * Word 2: actual TP size written
 * Word 3: Start of tracepoints, offset from CPM base
 *
 * Response: TRACEPOINT_REQUEST
 * Word 0: Queue mode header
 * Word 1: LK Error code
 * Word 2: # of tracepoints written
 * Word 3: overflow occurred
 *
 * SERVICE_ID: CPM_AP_COMMON_TRACEPOINT_SERVICE
 */

/* SERVICE_ID: CPM_COMMON_RESET_SERVICE
 *
 * Service related to warm/cold reset
 *
 * CMD: CMD_PWR_REQ_OPT
 *      CMD_PROGRAM_RESET_REASON
 *      CMD_READ_RESET_REASON
 *      CMD_TRIGGER_THERMAL_RESET
 * Queue Mode:
 * Word 0: Queue mode header
 * Word 1: Data | cmd
 *  bit [ 31 - 16 | 15 - 0 ]
 *      | data    | cmd    |
 * Word 2: Res
 * Word 3: Res
 *
 * Polling Mode:
 * Word 0: Header only
 *  bit [ 31 - 0 ]
 *      | header |
 * Word 1: Data | cmd
 *  bit [ 31 - 16 | 15 - 0 ]
 *      | data    | cmd    |
 * Word 2: Res
 * Word 3: Res
 *
 * Data field will serving following purpose for command:
 * config for CMD_PWR_REQ_OPT
 * reset reason for CMD_PROGRAM_RESET_REASON
 * ignored when sending CMD_READ_RESET_REASON
 * ignored when sending CMD_TRIGGER_THERMAL_RESET
 *
 * SERVICE_ID: CPM_COMMON_RESET_SERVICE
 */

#endif
