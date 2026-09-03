/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent CSRs.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/sizes.h>

/* Offsets from sswrp_tpu > tpu_top_csr */

/* funcApbSlaves_cpuSecure_cpuSecure */
#define EDGETPU_REG_RESET_CONTROL                       0x190018 /* ResetControl */
#define EDGETPU_REG_AXIUSER_CORE0                       0x190050 /* AxiUser */
#define EDGETPU_REG_INSTRUCTION_REMAP_CONTROL           0x190070 /* InstructionRemapControl */
#define EDGETPU_REG_INSTRUCTION_REMAP_LIMIT             0x190090 /* InstructionRemapLimit */
#define EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE          0x1900a0 /* InstructionRemapNewbase */

#define EDGETPU_REG_INSTRUCTION_REMAP_AXIUSER_CORE0     0x1900b0 /* InstructionRemapAxiUser */

/* funcApbSlaves_tpuTop_tpuTop */
#define EDGETPU_REG_LPM_CONTROL                         0x1e0028 /* LpmControlCsr */
/* LpmControlCsr bits */
#define LPM_CTRL_LPMCTLPWRSTATE		BIT(0) /* lpmCtlPwrState */

#define EDGETPU_LPM_CORE_CSR				0x1e0030 /* LpmCoreCsr */
#define EDGETPU_LPM_CLUSTER_CSR0			0x1e0038 /* LpmClusterCsr0 */
#define EDGETPU_LPM_CLUSTER_CSR1			0x1e0040 /* LpmClusterCsr1 */

/* funcApbSlaves_wdt0_wdt0 */
#define EDGETPU_REG_WDT0_CONTROL			0x18d008 /* wdt_control */
#define EDGETPU_REG_WDT0_KEY				0x18d010 /* wdt_key */
/* funcApbSlaves_wdt1_wdt1 */
#define EDGETPU_REG_WDT1_CONTROL			0x18e008 /* wdt_control */
#define EDGETPU_REG_WDT1_KEY				0x18e010 /* wdt_key */

/* funcApbSlaves_cpuNonSecure_cpuNonSecure */
#define EDGETPU_REG_CPUNS_TIMESTAMP			0x1a01c0 /* Timestamp */

/* funcApbSlaves_debugApbSlaves_apbaddr_dbg_0, 1 base addresses */
#define EDGETPU_REG_EXTERNAL_DEBUG_0_BASE		0x410000
#define EDGETPU_REG_EXTERNAL_DEBUG_1_BASE		0x510000

/* CSR offsets within external debug 0,1 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROGRAM_COUNTER      0x00a0 /* edpcsrlo */
#define EDGETPU_REG_EXTERNAL_DEBUG_OS_LOCK_ACCESS       0x0300 /* oslar_el1 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROCESSOR_STATUS     0x0314 /* edprsr */
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_ACCESS          0x0fb0 /* edlar */
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_STATUS          0x0fb4 /* edlsr */
#define EDGETPU_REG_EXTERNAL_DEBUG_AUTHSTATUS           0x0fb8 /* dbgauthstatus */

/*
 * Laguna PA and size of sswrp_cpm > cpm_top > cpm_periph_csrs > cpm_lpb > LPB_SSWRP_TPU_CSRS
 * (aka LPB_SSWRP_CSRS[32]).
 */
#define LPB_SSWRP_DEFAULT_TPU_CSRS	0x5330800
#define LPB_SSWRP_DEFAULT_TPU_CSRS_SIZE	0x18
#define LPB_TPU_INT_STATUS		0x04	/* internal status */
#define LPB_TPU_RAIL_STATUS		0x08

/* LPCM_LPM_TPU_CSRS register offsets. */
#define LPCM_LPM_TPU_PSM0_STATUS	0x2244
#define LPCM_LPM_TPU_PSM1_STATUS	0x3294
#define LPCM_LPM_TPU_PSM2_STATUS	0x4264
