// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */
#include <linux/types.h>

#include "g2d_registers.h"
#include "g2d_sc_hw.h"
#include "g2d_writeback_hw.h"
#include "g2d_pvric_hw.h"

static u32 wb_get_y_bpp(enum sc_hw_wb_format format)
{
	switch (format) {
	case WB_FORMAT_ARGB8888:
	case WB_FORMAT_XRGB8888:
	case WB_FORMAT_A2RGB101010:
	case WB_FORMAT_X2RGB101010:
		return 32;
	case WB_FORMAT_NV12:
		return 8;
	case WB_FORMAT_P010:
		return 16;
	default:
		return 0;
	}
}

static u32 wb_get_uv_bpp(enum sc_hw_wb_format format)
{
	switch (format) {
	case WB_FORMAT_NV12:
		return 8;
	case WB_FORMAT_P010:
		return 16;
	default:
		return 0;
	}
}

static void wb_set_fb(struct sc_hw *hw, u8 hw_id, struct drm_rect *dst)
{
	u32 config = 0;
	struct sc_hw_fb *fb = &hw->wb[hw_id].fb;
	struct pvric_hw_config *pvric_config = &hw->wb[hw_id].pvric;
	bool use_wdma_to_pos = !pvric_config->enable;
	u32 y_bpp;
	u32 uv_bpp;
	u32 y_offset = 0;
	u32 u_offset = 0;
	u32 v_offset = 0;

	if (!fb->enable)
		return;

	if (!dst) {
		dev_err(hw->dev, "%s null dst rect\n", __func__);
		return;
	}

	dev_dbg(hw->dev, "%s hw_id %d dst x1 %d y1 %d x2 %d y2 %d, compressed %d\n", __func__,
		hw_id, dst->x1, dst->y1, dst->x2, dst->y2, pvric_config->enable);

	if (use_wdma_to_pos) {
		y_bpp = wb_get_y_bpp(fb->format);
		if (!y_bpp) {
			dev_info(hw->dev, "%s unknown format 0x%x", __func__, fb->format);
		} else {
			y_offset = (dst->y1 * fb->stride) + (dst->x1 * (y_bpp >> 3));

			uv_bpp = wb_get_uv_bpp(fb->format);
			if (uv_bpp) {
				u_offset = (dst->y1 / 2 * fb->u_stride) + (dst->x1 * (uv_bpp >> 3));
				// V plane is interleaved with U for NV12/P010
			}
		}
	}

	config = VS_SET_FIELD(config, SCREG_LAYER0_WB_CONFIG, FORMAT, fb->format);
	config = VS_SET_FIELD(config, SCREG_LAYER0_WB_CONFIG, SWIZZLE, fb->swizzle);
	config = VS_SET_FIELD(config, SCREG_LAYER0_WB_CONFIG, TILE_MODE, fb->tile_mode);
	config = VS_SET_FIELD(config, SCREG_LAYER0_WB_CONFIG, UV_SWIZZLE, fb->uv_swizzle);
	sc_write(hw, SCREG_LAYER0_WB_CONFIG_Address, config);

	config = 0;
	config = VS_SET_FIELD(config, SCREG_LAYER0_WB_SIZE, WIDTH, drm_rect_width(dst));
	config = VS_SET_FIELD(config, SCREG_LAYER0_WB_SIZE, HEIGHT, drm_rect_height(dst));
	sc_write(hw, SCREG_LAYER0_WB_SIZE_Address, config);

	if (!use_wdma_to_pos) {
		config = 0;
		config = VS_SET_FIELD(config, SCREG_LAYER0_BLD_WB_LOC, LOCX, dst->x1);
		config = VS_SET_FIELD(config, SCREG_LAYER0_BLD_WB_LOC, LOCY, dst->y1);
		sc_write(hw, SCREG_LAYER0_BLD_WB_LOC_Address, config);
	}
	sc_write(hw, SCREG_LAYER0_WDMA_ADDRESS_Address, (u32)fb->address + y_offset);
	sc_write(hw, SCREG_LAYER0_WDMA_HADDRESS_Address, (fb->address >> 32) & 0xFF);
	sc_write(hw, SCREG_LAYER0_WDMA_UPLANE_ADDRESS_Address, (u32)fb->u_address + u_offset);
	sc_write(hw, SCREG_LAYER0_WDMA_UPLANE_HADDRESS_Address, (fb->u_address >> 32) & 0xFF);
	sc_write(hw, SCREG_LAYER0_WDMA_VPLANE_ADDRESS_Address, (u32)fb->v_address + v_offset);
	sc_write(hw, SCREG_LAYER0_WDMA_VPLANE_HADDRESS_Address, (fb->v_address >> 32) & 0xFF);
	sc_write(hw, SCREG_LAYER0_WDMA_STRIDE_Address, fb->stride);
	sc_write(hw, SCREG_LAYER0_WDMA_UPLANE_STRIDE_Address, fb->u_stride);
	sc_write(hw, SCREG_LAYER0_WDMA_VPLANE_STRIDE_Address, fb->v_stride);
}

void wb_hw_commit(struct sc_hw *hw, u8 hw_id, struct drm_rect *dst)
{
	wb_set_fb(hw, hw_id, dst);
	pvric_hw_wb_commit(hw, hw_id);
}
