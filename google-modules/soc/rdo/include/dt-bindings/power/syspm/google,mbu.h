/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef __DT_BINDINGS_POWER_SYSPM_GOOGLE_MBU_H
#define __DT_BINDINGS_POWER_SYSPM_GOOGLE_MBU_H

/*
 * File should mirror interfaces/protocols/syspm/syspm_resource_sid.h from
 * the source firmware repository.
 */

/*
 * These bindings define identifiers for different types of system resources
 * managed by the SysPM framework. They include identifiers for:
 * -   LPB (Logical Power Button) resources
 * -   Rail resources
 * -   SD (Sub-Domain) resources
 * -   Logical resources
 *
 * They are designed to provide a comprehensive list of system resources and are
 * used throughout the SysPM framework to reference specific resources.
 *
 * Important:
 * -   The resource service IDs defined by these bindings are considered stable
 * and shall not be changed. Gaps have been intentionally left in between
 * service IDs to allow for the introduction of other resources in each
 * category. This design ensures that adding new resources will not affect the
 * service IDs of existing resources further down the list.
 * -   It is strongly advised to avoid hardcoding these service IDs directly in
 * application logic. Hardcoding service IDs can introduce a significant risk of
 * incompatibility and unexpected behavior if the service IDs are modified
 * across SoCs. Instead, use the provided macros to reference resources.
 */

// LPB Resource SIDs - Reserved Range: 0x0 to 0xff (0 to 255)
#define SYSPM_RESOURCE_SID_LPB_AOSS_AMBSS 0x0		// 0
#define SYSPM_RESOURCE_SID_LPB_AOSS_AONSS 0x1		// 1
#define SYSPM_RESOURCE_SID_LPB_AOSS_PG 0x2		// 2
#define SYSPM_RESOURCE_SID_LPB_AURDSP 0x3		// 3
#define SYSPM_RESOURCE_SID_LPB_BMSM 0x4			// 4
#define SYSPM_RESOURCE_SID_LPB_CODEC_3P 0x5		// 5
#define SYSPM_RESOURCE_SID_LPB_CPU 0x6			// 6
#define SYSPM_RESOURCE_SID_LPB_CPUACC 0x7		// 7
#define SYSPM_RESOURCE_SID_LPB_DPU 0x8			// 8
#define SYSPM_RESOURCE_SID_LPB_FABDISP 0x9		// 9
#define SYSPM_RESOURCE_SID_LPB_FABHBW 0xA		// 10
#define SYSPM_RESOURCE_SID_LPB_FABMED 0xB		// 11
#define SYSPM_RESOURCE_SID_LPB_FABSTBY 0xC		// 12
#define SYSPM_RESOURCE_SID_LPB_FABSYSS 0xD		// 13
#define SYSPM_RESOURCE_SID_LPB_G2D 0xE			// 14
#define SYSPM_RESOURCE_SID_LPB_GCV 0xF			// 15
#define SYSPM_RESOURCE_SID_LPB_GMC0 0x10		// 16
#define SYSPM_RESOURCE_SID_LPB_GMC1 0x11		// 17
#define SYSPM_RESOURCE_SID_LPB_GMC2 0x12		// 18
#define SYSPM_RESOURCE_SID_LPB_GMC3 0x13		// 19
#define SYSPM_RESOURCE_SID_LPB_GPCA 0x14		// 20
#define SYSPM_RESOURCE_SID_LPB_GPCM_AMB 0x15		// 21
#define SYSPM_RESOURCE_SID_LPB_GPCM_INFRA 0x16		// 22
#define SYSPM_RESOURCE_SID_LPB_GPU 0x17			// 23
#define SYSPM_RESOURCE_SID_LPB_GSA 0x18			// 24
#define SYSPM_RESOURCE_SID_LPB_HSIO_N 0x19		// 25
#define SYSPM_RESOURCE_SID_LPB_HSIO_S 0x1A		// 26
#define SYSPM_RESOURCE_SID_LPB_ISPBE 0x1B		// 27
#define SYSPM_RESOURCE_SID_LPB_ISPFE 0x1C		// 28
#define SYSPM_RESOURCE_SID_LPB_LSIO_E 0x1D		// 29
#define SYSPM_RESOURCE_SID_LPB_LSIO_S 0x1E		// 30
#define SYSPM_RESOURCE_SID_LPB_MEMSS 0x1F		// 31
#define SYSPM_RESOURCE_SID_LPB_PCIE 0x20		// 32
#define SYSPM_RESOURCE_SID_LPB_TPU 0x21			// 33

// Rail Resource SIDs - Reserved Range: 0x100 to 0x1ff (256 to 511)
#define SYSPM_RESOURCE_SID_RAIL_VDD_AMB 0x105		// 261
#define SYSPM_RESOURCE_SID_RAIL_VDD_INFRA 0x106		// 262
#define SYSPM_RESOURCE_SID_RAIL_VDD_MM 0x107		// 263
#define SYSPM_RESOURCE_SID_RAIL_VDD_GMC 0x108		// 264
#define SYSPM_RESOURCE_SID_RAIL_VDD_SEC 0x112		// 274

// Sub-Domain Resource SIDs - Reserved Range: 0x200 to 0x3ff (512 to 1023)
// DPU SIDs - Reserved Range: 0x200 to 0x20f (512 to 527)
#define SYSPM_RESOURCE_SID_SD_DPU_BE 0x200		// 512
#define SYSPM_RESOURCE_SID_SD_DPU_FE0 0x201		// 513
#define SYSPM_RESOURCE_SID_SD_DPU_FE1 0x202		// 514
#define SYSPM_RESOURCE_SID_SD_DPU_DSI0 0x203		// 515
#define SYSPM_RESOURCE_SID_SD_DPU_DSI1 0x204		// 516
#define SYSPM_RESOURCE_SID_SD_DPU_DP0 0x205		// 517
// HSIO_N SIDs - Reserved Range: 0x210 to 0x21f (528 to 543)
#define SYSPM_RESOURCE_SID_SD_HSIO_N_USB 0x210		// 528
#define SYSPM_RESOURCE_SID_SD_HSIO_N_USB_PSW 0x211	// 529
#define SYSPM_RESOURCE_SID_SD_HSIO_N_EBU 0x212		// 530
#define SYSPM_RESOURCE_SID_SD_HSIO_N_DP 0x213		// 531
#define SYSPM_RESOURCE_SID_SD_HSIO_N_USB2AUX 0x214	// 532
#define SYSPM_RESOURCE_SID_SD_HSIO_N_USB2AUX_PSW 0x215	// 533
// HSIO_S SIDs - Reserved Range: 0x220 to 0x22f (544 to 559)
#define SYSPM_RESOURCE_SID_SD_HSIO_S_UFS 0x220		// 544
#define SYSPM_RESOURCE_SID_SD_HSIO_S_SD 0x221		// 545
#define SYSPM_RESOURCE_SID_SD_HSIO_S_UFS_PREP 0x222	// 546
// LSIO_E SIDs - Reserved Range: 0x230 to 0x23f (560 to 575)
#define SYSPM_RESOURCE_SID_SD_LSIO_E_CLI_GPIO 0x230	// 560
// LSIO_S SIDs - Reserved Range: 0x240 to 0x24f (576 to 591)
#define SYSPM_RESOURCE_SID_SD_LSIO_S_CLI_GPIO 0x240	// 576
// PCIE SIDs - Reserved Range: 0x250 to 0x25f (592 to 607)
#define SYSPM_RESOURCE_SID_SD_PCIE_CTRL0 0x250		// 592
#define SYSPM_RESOURCE_SID_SD_PCIE_CTRL1 0x251		// 593
#define SYSPM_RESOURCE_SID_SD_PCIE_TOP 0x252		// 594
#define SYSPM_RESOURCE_SID_SD_PCIE_DPA 0x253		// 595
// G2D SIDs - Reserved Range: 0x260 to 0x26f (608 to 623)
#define SYSPM_RESOURCE_SID_SD_G2D_CORE 0x260		// 608
// MEMSS SIDs - Reserved Range: 0x270 to 0x27f (624 to 639)
#define SYSPM_RESOURCE_SID_SD_MEMSS_DTA 0x270		// 624
// CPUSS SIDs - Reserved Range: 0x280 to 0x28f (640 to 655)
#define SYSPM_RESOURCE_SID_SD_CPUSS_BCI 0x280		// 640
#define SYSPM_RESOURCE_SID_SD_CPUSS_PLL 0x281		// 641
// CPUACC SIDs - Reserved Range: 0x290 to 0x29f (656 to 671)
#define SYSPM_RESOURCE_SID_SD_CPUACC_GPDMA 0x290	// 656
// ISPFE SIDs - Reserved Range: 0x2a0 to 0x2af (672 to 687)
#define SYSPM_RESOURCE_SID_SD_ISPFE_CORE0 0x2a0		// 672
#define SYSPM_RESOURCE_SID_SD_ISPFE_CORE1 0x2a1		// 673
#define SYSPM_RESOURCE_SID_SD_ISPFE_CORE2 0x2a2		// 674
#define SYSPM_RESOURCE_SID_SD_ISPFE_CSIS 0x2a3		// 675

// Logical Resource SIDs - Reserved Range: 0x400 to 0x5ff (1024 to 1535)
#define SYSPM_RESOURCE_SID_LOG_MC_ON 0x400		// 1024
#define SYSPM_RESOURCE_SID_LOG_FABSTBY_ON 0x401		// 1025
#define SYSPM_RESOURCE_SID_LOG_SEC_ON 0x402		// 1026

// Invalid SID
#define SYSPM_RESOURCE_SID_INVALID 0xffff		// 65535

#endif /* __DT_BINDINGS_POWER_SYSPM_GOOGLE_MBU_H */
