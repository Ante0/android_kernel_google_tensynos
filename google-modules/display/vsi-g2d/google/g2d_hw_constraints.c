// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include "g2d_hw_constraints.h"

static const struct g2d_layer_constraints layer_constraints = {
	.min_width = 8,
	.min_height = 8,
	/*
	 * Note that max height is only specified for rotation scenarios in HW documentation
	 * The non-rotation value could be increased if needed.
	 */

	.alignment = {
		[FORMAT_A8R8G8B8] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 15360,
			.max_height_rot = 1920,
		},
		[FORMAT_X8R8G8B8] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 15360,
			.max_height_rot = 1920,
		},
		[FORMAT_A2R10G10B10] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 15360,
			.max_height_rot = 1920,
		},
		[FORMAT_X2R10G10B10] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 15360,
			.max_height_rot = 1920,
		},
		[FORMAT_R5G6B5] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 30720,
			.max_height_rot = 1920,
		},
		[FORMAT_A1R5G5B5] = {
			.addr = 32,
			.stride = 32,
			.width = 1,
			.height = 1,
			.max_width = 30720,
			.max_height_rot = 0,
		},
		[FORMAT_X1R5G5B5] = {
			.addr = 32,
			.stride = 32,
			.width = 1,
			.height = 1,
			.max_width = 30720,
			.max_height_rot = 0,
		},
		[FORMAT_A4R4G4B4] = {
			.addr = 32,
			.stride = 32,
			.width = 1,
			.height = 1,
			.max_width = 30720,
			.max_height_rot = 0,
		},
		[FORMAT_X4R4G4B4] = {
			.addr = 32,
			.stride = 32,
			.width = 1,
			.height = 1,
			.max_width = 30720,
			.max_height_rot = 0,
		},
		[FORMAT_A16R16G16B16] = { /* AKA FP16 */
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 256,
			.width = 1,
			.height = 1,
			.max_width = 7680,
			.max_height_rot = 0,
		},
		[FORMAT_YV12] = {
			.addr = 32,
			.stride = 32,
			.width = 2,
			.height = 2,
			.max_width = 10240,
			.max_height_rot = 768,
		},
		[FORMAT_NV12] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 32,
			.width = 2,
			.height = 2,
			.max_width = 10240,
			.max_height_rot = 1024,
			.max_scaled_width = 1224,
		},
		[FORMAT_NV16] = {
			.addr = 32,
			.stride = 32,
			.width = 2,
			.height = 2,
			.max_width = 10240,
			.max_height_rot = 768,
		},
		[FORMAT_P010] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 32,
			.width = 2,
			.height = 2,
			.max_width = 5120,
			.max_height_rot = 1152
		},
		[FORMAT_P210] = {
			.addr = 32,
			.stride = 32,
			.width = 2,
			.height = 2,
			.max_width = 5120,
			.max_height_rot = 896,
		},
		[FORMAT_YUV420_PACKED] = {
			.addr = 32,
			.stride = 32,
			.width = 2,
			.height = 2,
			.max_width = 5120,
			.max_height_rot = 768,
		},
	},

	/* This ratio is src:dest, so 1:8 is an 8x upscale */
	.min_scale = FRAC_16_16(1, 8),
	/* This ratio is src:dest, so 4:1 is a 4x downscale */
	.max_scale = FRAC_16_16(4, 1),
};

static const struct g2d_wb_constraints wb_constraints = {
	.alignment = {
		[WB_FORMAT_ARGB8888] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 10240,
		},
		[WB_FORMAT_XRGB8888] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 10240,
		},
		[WB_FORMAT_A2RGB101010] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 10240,
		},
		[WB_FORMAT_X2RGB101010] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 64,
			.width = 1,
			.height = 1,
			.max_width = 10240,
		},
		[WB_FORMAT_NV12] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 32,
			.width = 2,
			.height = 2,
			.max_width = 13568,
		},
		[WB_FORMAT_P010] = {
			.addr = 32,
			.stride = 32,
			.addr_pvric = 256,
			.stride_pvric = 32,
			.width = 2,
			.height = 2,
			.max_width = 6784,
		},
	},
};

const struct g2d_layer_constraints *get_layer_dma_constraints(void)
{
	return &layer_constraints;
}

const struct g2d_wb_constraints *get_wb_dma_constraints(void)
{
	return &wb_constraints;
}

int g2d_get_max_scaling_width(u8 hw_format)
{
	return layer_constraints.alignment[hw_format].max_scaled_width;
}

