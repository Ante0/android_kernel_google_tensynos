/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Vinay Kalia <vinaykalia@google.com>
 * Author: Wen Chang Liu <wenchangliu@google.com>
 */

#ifndef _VPU_SLC_H_
#define _VPU_SLC_H_

#include "vpu_priv.h"

#if IS_ENABLED(CONFIG_CODEC3P_SLC)
#include <soc/google/pt.h>
#include <ra/google_ra.h>

#define C3P_SID(SEQID, NS, SID_TYPE) (((SEQID) << 4) | ((NS) << 3) | (SID_TYPE))

/**
 * enum vpu_slc_option - The option of slc allocation hint
 * 1: SID_TYPE 0: VPU Instruction and data access by firmware running on VCPU.
 * 2: SID_TYPE 1: Bitstream access by PRE_ENT_DEC HW, VCPU via DMA.
 * 4: SID_TYPE 2,3: Metadata access by VCPU via DMA, PRE_ENT_DEC HW.
 * 8: SID_TYPE 4,5: Pixel access by core1 and core0
 * 16: SID_TYPE 6,7: Metadata access by core0 and core1
 */
enum vpu_slc_option {
	VPU_SLC_OPTION_FW_VCPU		= BIT(0),
	VPU_SLC_OPTION_BITSTREAM_VCPU	= BIT(1),
	VPU_SLC_OPTION_METADATA_VCPU	= BIT(2),
	VPU_SLC_OPTION_PIXEL_CORE	= BIT(3),
	VPU_SLC_OPTION_METADATA_CORE	= BIT(4),
};

#define DEFAULT_SLC_OPTION	(VPU_SLC_OPTION_FW_VCPU | \
				VPU_SLC_OPTION_METADATA_VCPU | \
				VPU_SLC_OPTION_METADATA_CORE)

int vpu_pt_client_register(struct device_node *node, struct vpu_core *core);
void vpu_pt_client_unregister(struct vpu_core *core);
int vpu_pt_client_enable(struct vpu_core *core);
void vpu_pt_client_disable(struct vpu_core *core);
int vpu_ra_sid_set_pid(struct vpu_core *core, int slc_option);
#else
static inline int vpu_pt_client_register(struct device_node *node,
						struct vpu_core *core) { return 0; }
static inline void vpu_pt_client_unregister(struct vpu_core *core) { }
static inline int vpu_pt_client_enable(struct vpu_core *core) { return 0; }
static inline void vpu_pt_client_disable(struct vpu_core *core) { }
static inline int vpu_ra_sid_set_pid(struct vpu_core *core, int slc_option) { return 0; }
#endif

#endif //_VPU_SLC_H_
