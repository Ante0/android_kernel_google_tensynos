/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent CSRs.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/sizes.h>

/* Offsets from sswrp_tpu > tpu_top_csr */

/* funcApbSlaves_cpuSecure_cpuSecure */
#define EDGETPU_REG_RESET_CONTROL                       0x190010 /* ResetControl */
#define EDGETPU_REG_AXIUSER_CORE0                       0x190040 /* AxiUser */
#define EDGETPU_REG_INSTRUCTION_REMAP_CONTROL           0x190050 /* InstructionRemapControl */
#define EDGETPU_REG_INSTRUCTION_REMAP_LIMIT             0x190060 /* InstructionRemapLimit */
#define EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE          0x190068 /* InstructionRemapNewbase */

#define EDGETPU_REG_INSTRUCTION_REMAP_AXIUSER_CORE0     0x190070 /* InstructionRemapAxiUser */

/* funcApbSlaves_tpuTop_tpuTop */
#define EDGETPU_REG_LPM_CONTROL                         0x1E0020 /* LpmControlCsr */
/* LpmControlCsr bits */
#define LPM_CTRL_LPMCTLPWRSTATE		BIT(0) /* lpmCtlPwrState */

#define EDGETPU_LPM_CORE_CSR				0x1e0028 /* LpmCoreCsr */
#define EDGETPU_LPM_CLUSTER_CSR0			0x1e0030 /* LpmClusterCsr0 */

/* funcApbSlaves_wdt0_wdt0 */
#define EDGETPU_REG_WDT0_CONTROL			0x18d008 /* wdt_control */
#define EDGETPU_REG_WDT0_KEY				0x18d010 /* wdt_key */

/* funcApbSlaves_cpuNonSecure_cpuNonSecure */
#define EDGETPU_REG_CPUNS_TIMESTAMP			0x1a00e0 /* Timestamp */

/* funcApbSlaves_debugApbSlaves_apbaddr_dbg_0 base address */
#define EDGETPU_REG_EXTERNAL_DEBUG_0_BASE		0x410000

/* CSR offsets within external debug 0 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROGRAM_COUNTER      0x00a0 /* edpcsrlo */
#define EDGETPU_REG_EXTERNAL_DEBUG_OS_LOCK_ACCESS       0x0300 /* oslar_el1 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROCESSOR_STATUS     0x0314 /* edprsr */
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_ACCESS          0x0fb0 /* edlar */
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_STATUS          0x0fb4 /* edlsr */
#define EDGETPU_REG_EXTERNAL_DEBUG_AUTHSTATUS           0x0fb8 /* dbgauthstatus */

/*
 * Malibu PA and size of mbu > gsm_top > CSR > gsm_top > cpm_top > cpm_periph_csrs > cpm_lpb >
 * cpm_lpb_csrs > LPB_SSWRP_CSRS > LPB_SSWRP_CSRS[33].
 */
#define LPB_SSWRP_DEFAULT_TPU_CSRS	0xeb40840
#define LPB_SSWRP_DEFAULT_TPU_CSRS_SIZE	0x30
#define LPB_TPU_INT_STATUS		0x00	/* internal status */
#define LPB_TPU_RAIL_STATUS		0x04

/* LPCM_LPM_TPU_CSRS register offsets. */
#define LPCM_LPM_TPU_PSM0_STATUS	0x2244
#define LPCM_LPM_TPU_PSM1_STATUS	0x3230
#define LPCM_LPM_TPU_PSM2_STATUS	0x41f0
