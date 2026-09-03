/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */
#ifndef _GOOGLE_EDAC_H
#define _GOOGLE_EDAC_H

#define GOOGLE_EDAC "google-edac"

#define DEF_READ_SYSREG(sysreg, encode)						\
	static inline u64 read_##sysreg##_el1(void)				\
	{									\
		u64 val;							\
										\
		asm volatile("mrs %0, " #encode : "=r"(val) : : "memory");	\
		return val;							\
	}

#define DEF_WRITE_SYSREG(sysreg, encode)					\
	static inline void write_##sysreg##_el1(u64 val)			\
	{									\
		asm volatile("msr " #encode ", %0" : : "r"(val) : "memory");	\
	}

#define ERXSTATUS_VALID		BIT_ULL(30)
#define ERXSTATUS_UE		BIT_ULL(29)
#define ERXSTATUS_OF		BIT_ULL(27)
#define ERXSTATUS_MV		BIT_ULL(26)
#define ERXSTATUS_CE		GENMASK_ULL(25, 24)
#define ERXSTATUS_DE		BIT_ULL(23)

#define HAYES	0xD80
#define HUNTER	0xD81

#define HAYES_ERXMISC0_LVL	BIT_ULL(1)
#define HAYES_ERXMISC0_IND	BIT_ULL(0)
#define HAYES_ERXMISC0_SETMASK GENMASK_ULL(18, 6)
#define HAYES_ERXMISC0_WAYMASK GENMASK_ULL(31, 29)

#define HAYES_ERXMISC1_ARRAYMASK GENMASK_ULL(3, 0)
#define HAYES_ERXMISC1_BANKMASK GENMASK_ULL(12, 8)
#define HAYES_ERXMISC1_SUBBANKMASK GENMASK_ULL(17, 16)
#define HAYES_ERXMISC1_ENTRYMASK GENMASK_ULL(35, 20)

#define HAYES_L2_CACHE_DATA_RAM	0x8
#define HAYES_L2_CACHE_TAG_RAM	0x9
#define HAYES_L2_CACHE_L2DB_RAM	0xA
#define HAYES_L2_CACHE_DUP_L1_DCACHE_TAG_RAM	0xB
#define HAYES_L2_TLB_RAM	0xC

#define HUNTER_ERXMISC0_UNITMASK GENMASK_ULL(3, 0)
#define HUNTER_ERXMISC0_ARRAYMASK GENMASK_ULL(5, 4)
#define HUNTER_ERXMISC0_INDEXMASK GENMASK_ULL(18, 6)
#define HUNTER_ERXMISC0_SUBARRAYMASK GENMASK_ULL(22, 19)
#define HUNTER_ERXMISC0_BANKMASK GENMASK_ULL(24, 23)
#define HUNTER_ERXMISC0_SUBBANKMASK GENMASK_ULL(27, 25)
#define HUNTER_ERXMISC0_WAYMASK GENMASK_ULL(31, 28)

#define HUNTER_L1_INSTRUCTION_CACHE 0x1
#define HUNTER_L2_TLB	0x2
#define HUNTER_L1_DATA_CACHE	0x4
#define HUNTER_L2_CACHE	0x8

#define CLEAR_ERXSTATUSMASK GENMASK_ULL(63, 19)

#endif /* _GOOGLE_EDAC_H */
