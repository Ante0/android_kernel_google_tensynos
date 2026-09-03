#ifndef __NOA_COMMON_COMPILER_H__
#define __NOA_COMMON_COMPILER_H__

#define SEC_INIT
#define SEC_INIT_DATA
#define SEC_INIT_CONST

// Codes in ITCM
#define SEC_FAST

// Data in DTCM
#define SEC_FAST_DATA
#define SEC_FAST_CONST

// Data in public SRAM SHARED region
#define SEC_PUBLIC
#define SEC_PUBLIC_BUFFER
#define SEC_PUBLIC_HEADER

// Data in DRAM
#define SEC_EXRAM_DATA
#define SEC_FAST_CONST

#endif /* __NOA_COMMON_COMPILER_H__ */
