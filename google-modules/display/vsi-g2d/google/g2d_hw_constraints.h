/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_HW_CONSTRAINTS_H_
#define _G2D_HW_CONSTRAINTS_H_

#include "g2d_sc_hw.h"

/* DMA Alignment Requirements */
#define FRAC_16_16(mult, div) (((mult) << 16) / (div))

struct g2d_alignment_constraints {
	/* In Bytes */
	u16 addr;
	u16 stride;
	u16 addr_pvric;
	u16 stride_pvric;

	/* In Pixels */
	u16 width;
	u16 height;
	unsigned int max_width;
	unsigned int max_scaled_width;
	unsigned int max_height_rot;
};

struct g2d_layer_constraints {
	unsigned int min_width;
	unsigned int min_height;

	/*
	 * Some formats can have a weird 1-off difference (eg FP16 is *almost* the same as the other
	 * RGB formats). We store constraints per-format to ensure the greatest flexibility
	 */

	struct g2d_alignment_constraints alignment[NUM_LAYER_FORMATS];

	int min_scale; /* 16.16 fixed point */
	int max_scale; /* 16.16 fixed point */
};

struct g2d_wb_constraints {
	struct g2d_alignment_constraints alignment[NUM_WB_FORMATS];
};

const struct g2d_layer_constraints *get_layer_dma_constraints(void);
const struct g2d_wb_constraints *get_wb_dma_constraints(void);
int g2d_get_max_scaling_width(u8 hw_format);

#endif //_G2D_HW_CONSTRAINTS_H_
