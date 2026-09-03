/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef __DT_BINDINGS_LPCM_PF_STATE_MBU_H
#define __DT_BINDINGS_LPCM_PF_STATE_MBU_H

/*
 * define LPCM's ID of different SSWRPs, should match values defined in
 * protocols/lpcm/include/interfaces/protocols/lpcm/lpcm_protocol.h
 * TODO(b/283348013): create shared C header files and replace the include path
 */
#define LPCM_GSM 0
#define LPCM_AOSS_AONSS 1
#define LPCM_AOSS_AMBSS 2
#define LPCM_AOSS_PG 3
#define LPCM_AOSS_AON 4
#define LPCM_LSIO_E 5
#define LPCM_LSIO_S 6
#define LPCM_HSIO_N 7
#define LPCM_HSIO_S 8
#define LPCM_PCIE 9
#define LPCM_GPCA 10
#define LPCM_FABDISP 11
#define LPCM_FABHBW 12
#define LPCM_MEMSS 13
#define LPCM_FABMED 14
#define LPCM_FABSTBY 15
#define LPCM_FABSYSS 16
#define LPCM_CPU 17
#define LPCM_CPU_MAIN 18
#define LPCM_CPU_CCM1A 19
#define LPCM_CPU_CCM1B 20
#define LPCM_CCM2 21
#define LPCM_AURORA 22
#define LPCM_CODEC_3P 23
#define LPCM_G2D 24
#define LPCM_GCV 25
#define LPCM_ISPBE 26
#define LPCM_ISPFE 27
#define LPCM_GPU 28
#define LPCM_GPU_REMOTE_FLL 29
#define LPCM_GPU_REMOTE_SENSOR 30
#define LPCM_TPU 31
#define LPCM_TPU_REMOTE 32
#define LPCM_BMSM 33
#define LPCM_CPUACC 34
#define LPCM_DPU 35
#define LPCM_GDMC 36
#define LPCM_GMC0 37
#define LPCM_GMC1 38
#define LPCM_GMC2 39
#define LPCM_GMC3 40
#define LPCM_GPCM_AMB 41
#define LPCM_GPCM_INFRA 42
#define LPCM_GSA 43

#endif // __DT_BINDINGS_LPCM_PF_STATE_MBU_H
