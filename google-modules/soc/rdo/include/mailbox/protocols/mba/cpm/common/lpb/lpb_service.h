/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Google LLC
 */
#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_LPB_LPB_SERVICE_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_LPB_LPB_SERVICE_H

// Contains all definitions needed to use the LPB mailbox service

/* SERVICE_ID: CPM_COMMON_LPB_SERVICE
 *
 * Disabled for production
 * CLI to test LPB
 *
 * CMD: LPB_CMD_DEBUG_SET, LPB_CMD_SSWRP_STATE_SET
 * Queue Mode:
 * Word 0: Queue mode header
 * Word 1: Cmd: LPB_CMD_DEBUG_SET | LPB_CMD_SSWRP_STATE_SET
 *  bit [ 31 - 0 ]
 *      | cmd    |
 * Word 2: LPB SERVICE SSWRP ID
 *  bit [ 31 - 0 ]
 *      | SERVICE SSWRP ID |
 * Word 3: Set on or off (1 is on, anything else is off)
 *  bit [ 31 - 0 ]
 *      | state  |
 *
 * Response:
 * Word 0: Queue mode header
 * Word 1: CPM_COMMON_PROT_NO_ERR on success, else error
 *  bit [ 31 - 0                 ]
 *      | cpm_common_mba_error_t |
 * Word 2:
 *  bit [ 31 - 0                   ]
 *      | lpb_service_cmd_status_t |
 * Word 3: LPB Service ID that was changed
 *  bit [ 31 - 0           ]
 *      | Service SSWRP ID |
 *
 * Polling Mode:
 * Word 0: Header and data (1 to set on, 0 to set off)
 *         cmd: LPB_CMD_DEBUG_SET | LPB_CMD_SSWRP_STATE_SET
 *  bit [ 31 - 16             | 15-0 ]
 *      | polling mode header | cmd  |
 * Word 1: LPB SERVICE SSWRP ID
 *  bit [ 31 - 0 ]
 *      | SERVICE SSWRP ID |
 * Word 2: state (1 on, 0 off)
 *  bit [ 31 - 0 ]
 *      | state  |
 *
 * Response:
 * Word 0: Header
 *  bit [ 31 - 16             | 15 - 0 ]
 *      | polling mode header | unused |
 * Word 1: CPM_COMMON_PROT_NO_ERR on success, else error
 *  bit [ 31 - 0                 ]
 *      | cpm_common_mba_error_t |
 * Word 2:
 *  bit [ 31 - 0                   ]
 *      | lpb_service_cmd_status_t |
 * Word 3: LPB Service ID that was changed
 *  bit [ 31 - 0           ]
 *      | Service SSWRP ID |
 *
 * CMD: LPB_CMD_DEBUG_SET, LPB_CMD_SSWRP_STATE_SET
 *
 * CMD: LPB_CMD_SSWRP_STATE_GET
 * Queue Mode: Not supported at this time
 * Polling Mode:
 * Word 0: Header and data (1 to set on, 0 to set off)
 *         cmd: LPB_CMD_SSWRP_STATE_GET
 *  bit [ 31 - 16             | 15-0 ]
 *      | polling mode header | cmd  |
 * Word 1: LPB SERVICE SSWRP ID
 *  bit [ 31 - 0           ]
 *      | SERVICE SSWRP ID |
 *
 * Response:
 * Word 0: Header
 *  bit [ 31 - 16             | 15 - 0 ]
 *      | polling mode header | unused |
 * Word 1: lpb_service_state_t (LPB_SERVICE_STATE_ON | LPB_SERVICE_STATE_OFF)
 *  bit [ 31 - 0              ]
 *      | lpb_service_state_t |
 *
 * CMD: LPB_CMD_SSWRP_STATE_GET
 *
 * CMD: LPB_CMD_REQ_DRAM_STATE
 * Polling Mode:
 * Word 0: Header and data (1 to set on, 0 to set off)
 *         cmd: LPB_CMD_REQ_DRAM_STATE
 *  bit [ 31 - 16             | 15-0 ]
 *      | polling mode header | cmd  |
 * Word 1: state (1 on, all else off)
 *  bit [ 31 - 0 ]
 *      | state  |
 *
 * Response:
 * Word 0: Header
 *  bit [ 31 - 16             | 15 - 0 ]
 *      | polling mode header | unused |
 * Word 1: Error code (CPM_COMMON_PROT_NO_ERR for success, else an error code)
 *  bit [ 31 - 0     ]
 *      | error code |
 *
 * CMD: LPB_CMD_REQ_DRAM_STATE
 *
 * CMD: LPB_CMD_GET_DRAM_STATE
 * Polling Mode:
 * Word 0: Header and data (1 to set on, 0 to set off)
 *         cmd: LPB_CMD_GET_DRAM_STATE
 *  bit [ 31 - 16             | 15-0 ]
 *      | polling mode header | cmd  |
 *
 * Response:
 * Word 0: Header
 *  bit [ 31 - 16             | 15 - 0 ]
 *      | polling mode header | unused |
 * Word 1: 0 for OFF, 1 for ON, else CPM_COMMON_PROT_GENERIC_ERR
 *  bit [ 31 - 0     ]
 *      | state |
 *
 * CMD: LPB_CMD_GET_DRAM_STATE
 *
 * SERVICE_ID: CPM_COMMON_LPB_SERVICE
 */

// Add all LPB specific definitions here
// Note: CPM, BMSM, and GDMC are not on this list, as they don't have an LPB
// sequencer

// LGA
enum lpb_service_sswrp_id {
	LPB_SERVICE_AOC_ID,
	LPB_SERVICE_AURORA_ID,
	LPB_SERVICE_CODEC_3P_ID,
	LPB_SERVICE_CPU_ID,
	LPB_SERVICE_CPUACC_ID,
	LPB_SERVICE_DPU_ID,
	LPB_SERVICE_EH_ID,
	LPB_SERVICE_FABHBW_ID,
	LPB_SERVICE_FABMED_ID,
	LPB_SERVICE_FABRT_ID,
	LPB_SERVICE_FABSTBY_ID,
	LPB_SERVICE_FABSYSS_ID,
	LPB_SERVICE_G2D_ID,
	LPB_SERVICE_GMC0_ID,
	LPB_SERVICE_GMC1_ID,
	LPB_SERVICE_GMC2_ID,
	LPB_SERVICE_GMC3_ID,
	LPB_SERVICE_GPCA_ID,
	LPB_SERVICE_GPCM_ID,
	LPB_SERVICE_GPDMA_ID,
	LPB_SERVICE_GPU_ID,
	LPB_SERVICE_GSA_ID,
	LPB_SERVICE_GSW_ID,
	LPB_SERVICE_HSIO_N_ID,
	LPB_SERVICE_HSIO_S_ID,
	LPB_SERVICE_INF_TCU_ID,
	LPB_SERVICE_ISPBE_ID,
	LPB_SERVICE_ISPFE_ID,
	LPB_SERVICE_LSIO_E_ID,
	LPB_SERVICE_LSIO_N_ID,
	LPB_SERVICE_LSIO_S_ID,
	LPB_SERVICE_MEMSS_ID,
	LPB_SERVICE_TPU_ID,
	LPB_SERVICE_NUM_SSWRPS,
};

// MBU
enum lpb_service_mbu_sswrp_id {
	LPB_SERVICE_MBU_AOSS_AMBSS_ID,
	LPB_SERVICE_MBU_AOSS_AONSS_ID,
	LPB_SERVICE_MBU_AOSS_PG_ID,
	LPB_SERVICE_MBU_AURDSP_ID,
	LPB_SERVICE_MBU_BMSM_ID,
	LPB_SERVICE_MBU_CODEC_3P_ID,
	LPB_SERVICE_MBU_CPU_ID,
	LPB_SERVICE_MBU_CPUACC_ID,
	LPB_SERVICE_MBU_DPU_ID,
	LPB_SERVICE_MBU_FABDISP_ID,
	LPB_SERVICE_MBU_FABHBW_ID,
	LPB_SERVICE_MBU_FABMED_ID,
	LPB_SERVICE_MBU_FABSTBY_ID,
	LPB_SERVICE_MBU_FABSYSS_ID,
	LPB_SERVICE_MBU_G2D_ID,
	LPB_SERVICE_MBU_GCV_ID,
	LPB_SERVICE_MBU_GMC0_ID,
	LPB_SERVICE_MBU_GMC1_ID,
	LPB_SERVICE_MBU_GMC2_ID,
	LPB_SERVICE_MBU_GMC3_ID,
	LPB_SERVICE_MBU_GPCA_ID,
	LPB_SERVICE_MBU_GPCM_AMB_ID,
	LPB_SERVICE_MBU_GPCM_INFRA_ID,
	LPB_SERVICE_MBU_GPU_ID,
	LPB_SERVICE_MBU_GSA_ID,
	LPB_SERVICE_MBU_HSIO_N_ID,
	LPB_SERVICE_MBU_HSIO_S_ID,
	LPB_SERVICE_MBU_ISPBE_ID,
	LPB_SERVICE_MBU_ISPFE_ID,
	LPB_SERVICE_MBU_LSIO_E_ID,
	LPB_SERVICE_MBU_LSIO_S_ID,
	LPB_SERVICE_MBU_MEMSS_ID,
	LPB_SERVICE_MBU_PCIE_ID,
	LPB_SERVICE_MBU_TPU_ID,
	LPB_SERVICE_MBU_NUM_SSWRPS,
};

// Used to indicate what has just happened as a result of the below commands
// Can be a generic success/error, or something more meaningful
enum lpb_service_cmd_status {
	LPB_SERVICE_CMD_SUCCESS = 0xf,
	LPB_SERVICE_CMD_FAIL,
	LPB_SERVICE_PWRDN_STARTED,
	LPB_SERVICE_PWRUP_STARTED,
	LPB_SERVICE_NO_EFFECT,
};

enum lpb_service_state {
	LPB_SERVICE_STATE_OFF = 0x1,
	LPB_SERVICE_STATE_ON,
};

#define LPB_SVC_MSG_SIZE (4)
#define LPB_CMD_DEBUG_SET (1)
#define LPB_CMD_SSWRP_STATE_SET (2)
#define LPB_CMD_SSWRP_STATE_GET (3)
#define LPB_CMD_REQUEST_COMPUTE_SSWRP_OFF (4)
#define LPB_CMD_REQ_DRAM_STATE (5)
#define LPB_CMD_GET_DRAM_STATE (6)

#endif
