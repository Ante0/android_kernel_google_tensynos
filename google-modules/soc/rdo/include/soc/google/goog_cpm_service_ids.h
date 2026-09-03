/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2024, Google LLC
 */

#ifndef __GOOG_CPM_SERVICE_ID_H
#define __GOOG_CPM_SERVICE_ID_H

enum apc_common_mba_service_id {
	APC_COMMON_SERVICE_FIRST = 0x0,
	APC_COMMON_SERVICE_ID_RESERVED = 0x0,
	APC_COMMON_SERVICE_ID_PING = 0x1,
	APC_COMMON_SERVICE_ID_CPMLOG = 0x2,
	APC_COMMON_SERVICE_ID_GMC = 0x3,
	APC_COMMON_SERVICE_ID_GSLC = 0x4,
	APC_COMMON_SERVICE_ID_CPM_TRACEPOINT = 0x5,
	APC_COMMON_SERVICE_ID_PF_STATE_CPM = 0x6,
	APC_COMMON_SERVICE_ID_POWER_CONTROLLER = 0x7,
	APC_COMMON_SERVICE_ID_MIPM = 0x8,
	APC_COMMON_SERVICE_ID_SSR = 0x9,
	APC_COMMON_SERVICE_LAST = 0x37,
};

#define MBA_COMMON_MIN_SERVICE_ID 0x38
#define MBA_COMMON_MAX_SERVICE_ID 0x70

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
	// CPM command line service. See go/cpm-cli-over-mba
	CPM_COMMON_COMMAND_LINE_SERVICE = 0xF,
	// GDMC->CPM special message
	CPM_GDMC_PMIC_SERVICE = 0x10,
	// SMMU initial register configuration service
	CPM_SMMU_SERVICE = 0x11,
	// BCL service
	CPM_COMMON_BCL_SERVICE = 0x12,
	CPM_COMMON_THERMAL_V2_SERVICE = 0x15,
	// Pixel firmware tracepoint service
	CPM_COMMON_FWTP_SERVICE = 0x17,
	// Subsystem Reset service
	CPM_COMMON_SSR_SERVICE = 0x18,
	// Pixel firmware metrics service
	CPM_COMMON_FWMT_SERVICE = 0x1A,
	CPM_COMMON_SERVICE_LAST = 0x37,
};

enum cpm_ap_ns_mba_service_id {
	CPM_AP_NS_MAILBOX_SERVICE_LAST = 0x38,
	CPM_AP_NS_SSRAM_SERVICE = 0x66,
	CPM_AP_NS_SPMI_SERVICE = 0x67,
	CPM_AP_NS_POWER_DASH_MB_SERVICE = 0x68,
	CPM_AP_NS_ODPM_MB_SERVICE = 0x69,
	CPM_AP_NS_IRQ_SERVICE = 0x6E,
	// Control latency library CPM functions
	CPM_AP_NS_LTC_MB_SERVICE = 0x70,
	CPM_AP_NS_MAILBOX_SERVICE_FIRST = 0x70,
};

#endif /* __GOOG_CPM_SERVICE_ID_H */
