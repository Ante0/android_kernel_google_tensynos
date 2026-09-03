/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */
#ifndef _UFS_LGA_PLATFORM_H_
#define _UFS_LGA_PLATFORM_H_

#include "../ufs-google.h"
#include "ufs-regs-gen.h"


/**
 * Driver platform defines
 */

#define UFS_DMA_MASK DMA_BIT_MASK(36)

/* HSIOS_UFS_PHYCLK_SEL bits */
#define REF_CLK_SEL (REF_CLK_SEL_DEFAULT)

/* HSIOS_UFS_MPHY_CFG bits */
#define UFS_MPHY_CFG (UFS_MPHY_CFG_DEFAULT)

/* HSIOS_UFS_CFG_CLKSEL bits */
#define CFG_CLK_SEL (CFG_CLK_SEL_DEFAULT)

/* MPHY Version Registers */
#define MPHY_FIRMWARE_VER_REG0 0x199
#define MPHY_FIRMWARE_VER_REG1 0x19a

/* HSIOS_UFS_AWUSER_VC fields */
#define UFS_AWUSER_VC GENMASK(2, 0)
#define UFS_ARUSER_VC GENMASK(10, 8)

#define HCLKDIV_VALUE 0xC8

/* Vendor specific card event interrupts: CIS(29) CRS(30) CTES(31) */
#define VS_INTERRUPT_MASK (CIS_MASK | CRS_MASK | CTES_MASK)

/* Multi Circular Queue related macros */
#define MCQ_SQDAO 0x4000
#define MCQ_SQINT 0x4200
#define MCQ_CQDAO 0x4400
#define MCQ_CQINT 0x4600
#define MAX_REG_SQDAO 0x14
#define MAX_REG_SQIS 0x8
#define MAX_REG_CQDAO 0x8
#define MAX_REG_CQIS 0xC
#define MCQ_QCFGPTR_MASK GENMASK(7, 0)
#define MCQ_QCFGPTR_UNIT 0x200
#define MCQ_SQATTR_OFFSET(c) \
	((((c) >> 16) & MCQ_QCFGPTR_MASK) * MCQ_QCFGPTR_UNIT)
#define MCQ_QCFG_SIZE 0x40
#define MAX_SUPP_MAC 64
#define REG_CQCFG 0x34
#define IAGVLD BIT(8)
#define IAG_MASK GENMASK(4, 0)
#define REG_MCQIACR 0x8
#define IACTH GENMASK(12, 8)
#define CTR BIT(16)
#define IAPWEN BIT(24)
#define IAEN BIT(31)
#define MAX_QUEUE_SUP GENMASK(7, 0)
#define MCQ_IAG_EVENT_STATUS 0x200000

/* Refer to go/pmic-regs */
#define SEQ_BUCK11S_8_ADDR 0x1857
#define SEQ_BUCK13S_8_ADDR 0x1867

#define REG_PMIC_VCC SEQ_BUCK11S_8_ADDR
#define REG_PMIC_VCCQ SEQ_BUCK13S_8_ADDR

enum IRQ_TYPE {
	CORE_MAIN_PMIC,
	CORE_SUB_PMIC,
	IF_PMIC,
};

extern const struct reg_info hc_registers[];
extern const struct reg_info top_registers[];

#endif /* _UFS_LGA_PLATFORM_H_ */
