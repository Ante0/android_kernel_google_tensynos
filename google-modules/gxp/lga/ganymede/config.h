/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Include all configuration files for Ganymede.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __GANYMEDE_CONFIG_H__
#define __GANYMEDE_CONFIG_H__

#include <linux/sizes.h>

#define GXP_DRIVER_NAME "gxp_ganymede"
#define DSP_FIRMWARE_DEFAULT_PREFIX "gxp_ganymede_fw_core"
#define GXP_DEFAULT_MCU_FIRMWARE "google/gxp-ganymede.fw"

/* Maximum size of the DSP FW */
#define DSP_FIRMWARE_IMAGE_SIZE 0x00100000

/*
 * From soc/gs/include/dt-bindings/clock/zuma.h
 *   #define ACPM_DVFS_AUR 0x0B040013
 */
//TODO(b/249920074): Confirm correct value
#define AUR_DVFS_DOMAIN 19

#define GXP_NUM_CORES 3
/* Three for cores, one for KCI, one for UCI and one for IIF */
#define GXP_NUM_MAILBOXES (GXP_NUM_CORES + 3)
/* Indexes of the mailbox reg in device tree */
#define KCI_MAILBOX_ID (GXP_NUM_CORES)
#define UCI_MAILBOX_ID (GXP_NUM_CORES + 1)
#define IIF_MAILBOX_ID (GXP_NUM_CORES + 2)

/* three for cores, one for MCU */
#define GXP_NUM_WAKEUP_DOORBELLS (GXP_NUM_CORES + 1)

/* The total size of the configuration region. */
#define GXP_SHARED_BUFFER_SIZE SZ_512K
/* Size of slice per VD. */
#define GXP_SHARED_SLICE_SIZE SZ_32K

#define GXP_SEPARATE_LPM_OFFSET
/* PSM already initialized with required valid states. */
#define GXP_AUTO_PSM 1
/* Skip dumping LPM registers while taking the debug dump. */
#define GXP_SKIP_LPM_REGISTER_DUMP
/* Skip dumping interrupt polarity registers while taking debug dump. */
#define GXP_DUMP_INTERRUPT_POLARITY_REGISTER 0

/*
 * Indicates that RFW access policies have been enabled which makes access to certain registers
 * NS non accessible.
 */
#define GXP_RFW_AC_POLICY_ENABLED 1

/* 15 because the last slice is reserved for system config region. */
#define GXP_NUM_SHARED_SLICES 15

/*
 * Can be coherent with AP
 *
 * Linux IOMMU-DMA APIs optimise cache operations based on "dma-coherent" property in DT. Handle
 * "dma-coherent" property in driver itself instead of specifying in DT so as to support both
 * coherent and non-coherent buffers.
 */
#define GXP_IS_DMA_COHERENT

/* TODO(b/407813326): Remove this temporary WA once the correct flow is implemented. */
#define GXP_CLEAR_CARVEOUT_IIF_SIGNAL_REGION 1
#define GXP_CARVEOUT_IIF_SIGNAL_REGION_ADDRESS 0xA2140000
#define GXP_CARVEOUT_IIF_SIGNAL_REGION_SIZE 0x30000

/* HW watchdog */
#define GXP_WDG_DT_IRQ_INDEX 5
#define GXP_WDG_ENABLE_BIT 0
#define GXP_WDG_INT_CLEAR_BIT 5
#define GXP_WDG_KEY_VALUE 0xA55AA55A

/* arm-smmu-v3 requires domain finalization to do iommu map. */
#define GXP_MMU_REQUIRE_ATTACH 1

/* SMMU granularity depends on PAGE_SIZE. */
#define GXP_MMU_GRANULARITY_IS_PAGE 1

#define GXP_HAS_GSA 1

#define GXP_HAS_BPM 0
#define GXP_HAS_GEM 1

#define GXP_HAS_CMU 0

#include "config-pwr-state.h"
#include "context.h"
#include "iova.h"
#include "lpm.h"
#include "mailbox-regs.h"
#include "top-csrs.h"

#endif /* __GANYMEDE_CONFIG_H__ */
