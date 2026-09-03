/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_preprocess.h"

#include "vs_dc_hdr.h"
#include "vs_dc_hw.h"
#include "vs_dc_info.h"
#include "vs_dc_reg_fe0.h"
#include "vs_dc_reg_fe1.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_property.h"
#include "vs_dc.h"
#include "vs_dc_pre.h"
#include "vs_trace.h"

static bool data_extend_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_data_extend *data_extend = data;
	u32 config = 0;

	trace_config_hw_layer_feature_en("DATA_EXTEND", hw_id, enable);
	config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, EXTEND_BITS_ALPHA_MODE, enable);

	if (enable) {
		trace_config_hw_layer_feature("DATA_EXTEND_DATA", hw_id, "mode:%d value:%d",
					      data_extend->data_extend_mode,
					      data_extend->alpha_data_extend.alpha_extend_value);

		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, EXTEND_BITS_MODE,
				      data_extend->data_extend_mode);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, ALPHA_BIT_EXTEND_Address),
			 data_extend->alpha_data_extend.alpha_extend_value);
	} else {
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, EXTEND_BITS_MODE, 0x0);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, ALPHA_BIT_EXTEND_Address), 0x0);
	}
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_Address), config);
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(data_extend_proto, "DATA_EXTEND", struct drm_vs_data_extend, NULL, NULL,
			  data_extend_config_hw);

static bool uv_upsample_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			      const void *obj_state)
{
	const struct dc_hw_plane *hw_plane = vs_dc_hw_get_plane(hw, hw_id);
	const struct vs_plane_info *plane_info = hw_plane->info;
	const u64 upsample = *(u64 *)data;
	const u32 h_phase = upsample & 0xFF;
	const u32 v_phase = (upsample >> 8) & 0xFF;

	if (h_phase > plane_info->max_uv_phase || v_phase > plane_info->max_uv_phase) {
		dev_err(hw->dev, "%s has invalid up-sample phase %#x, %#x\n", __func__, h_phase,
			v_phase);
		return false;
	}
	return true;
}

static bool uv_upsample_update(const struct dc_hw *hw, u8 hw_id, struct vs_dc_property_state *state,
			       const void *data, u32 size, const void *obj_state)
{
	const struct dc_hw_plane *plane = vs_dc_hw_get_plane(hw, hw_id);

	if (plane->fb.enable && vs_dc_is_yuv_format(plane->fb.format)) {
		*(u64 *)state->data = VS_DC_PROPERTY_VAL(data, u64);
		state->enable = true;
		state->dirty = true;
	} else {
		state->enable = false;
		state->dirty = false;
	}

	trace_update_hw_layer_feature_en_dirty("UV_UPSAMPLE", hw_id, state->enable, state->dirty);

	return true;
}

static bool uv_upsample_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const u64 upsample = *(u64 *)data;
	const u32 h_phase = upsample & 0xFF;
	const u32 v_phase = (upsample >> 8) & 0xFF;
	const u32 reg = VS_SH_LAYER_FIELD(hw_id, UV_UP_SAMPLE_Address);
	const u32 config = VS_SET_FIELD(0, DCREG_SH_LAYER0_UV_UP_SAMPLE, HPHASE, h_phase) |
			   VS_SET_FIELD(0, DCREG_SH_LAYER0_UV_UP_SAMPLE, VPHASE, v_phase);

	trace_config_hw_layer_feature_en("UP_SAMPLE", hw_id, enable);

	if (enable)
		dc_write(hw, reg, config);
	return true;
}

VS_DC_RANGE_PROPERTY_PROTO(uv_upsample_proto, "UP_SAMPLE", 0, 0xFFFF, uv_upsample_check,
			   uv_upsample_update, uv_upsample_config_hw);

static bool dma_config_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct dc_hw_plane *hw_plane = vs_dc_hw_get_plane(hw, hw_id);
	const struct vs_plane_info *plane_info = hw_plane->info;
	const struct vs_plane_state *vs_plane_state = obj_state;
	const struct drm_plane_state *state = &vs_plane_state->base;
	const u32 one_roi = plane_info->roi;
	const u32 two_roi = plane_info->roi_two;
	const u32 skip_roi = plane_info->roi_skip;
	const u32 ext_layer = plane_info->layer_ext;
	const u32 roi_y_gap = hw->info->roi_y_gap;
	const u32 ext_layer_ex = plane_info->layer_ext_ex;
	const u32 fb_w = vs_plane_state->base.fb->width;
	const u32 fb_h = vs_plane_state->base.fb->height;
	const uint32_t src_w = state->src_w >> 16;
	const uint32_t src_h = state->src_h >> 16;
	const int32_t src_x = state->src_x >> 16;
	const int32_t src_y = state->src_y >> 16;

	const struct drm_vs_dma *dma = data;
	const struct vs_dc_info *info = hw->info;
	struct drm_framebuffer *fb = vs_plane_state->base.fb;

	int ret = 0;

	if ((dma->mode == VS_DMA_NORMAL) || (dma->mode == VS_DMA_ONE_ROI)) {
		dev_err(hw->dev,
			"%s DMA_CONFIG blob property doesn't support VS_DMA_NORMAL and VS_DMA_ONE_ROI!\n",
			__func__);
		return false;
	}

	// This must be enabled for all dma modes other than NORMAL
	if (!one_roi) {
		dev_err(hw->dev, "%s not support layer ROI.\n", __func__);
		return false;
	}

	// Rotation is supported for NORMAL & ONE_ROI, which are not handled here
	if (vs_plane_state->base.rotation != DRM_MODE_ROTATE_0) {
		dev_err(hw->dev, "%s Current ROI mode does not support rotation.\n", __func__);
		return false;
	}

	/* check in rect whether valid */
	switch (dma->mode) {
	case VS_DMA_TWO_ROI:
		if (!two_roi) {
			dev_err(hw->dev, "%s not support layer two ROI.\n", __func__);
			return false;
		}
		if (src_w != state->crtc_w || src_h != state->crtc_h ||
		    dma->in_rect.w != dma->out_rect.w || dma->in_rect.h != dma->out_rect.h) {
			dev_err(hw->dev, "%s Two ROI mode does not support scale.\n", __func__);
			return false;
		}
		if (((dma->in_rect.w + dma->in_rect.x) > fb_w) ||
		    ((dma->in_rect.h + dma->in_rect.y) > fb_h)) {
			dev_err(hw->dev, "%s ROI 1 area is out of layer area range.\n", __func__);
			return false;
		}
		if (!((dma->in_rect.y + dma->in_rect.h) > (src_y) ||
		      (src_y + src_h) > (dma->in_rect.y))) {
			dev_err(hw->dev,
				"%s the gap between the layer two ROIs is less than zero.\n",
				__func__);
			return false;
		}
		break;
	case VS_DMA_SKIP_ROI:
		if (!skip_roi) {
			dev_err(hw->dev, "%s not support layer skip ROI.\n", __func__);
			return false;
		}
		if (src_w != fb_w || src_h != fb_h) {
			dev_err(hw->dev, "%s Skip ROI mode requires plane dims to match fb.\n",
				__func__);
			return false;
		}
		if (fb_w != state->crtc_w || fb_h != state->crtc_h) {
			dev_err(hw->dev, "%s Skip ROI mode does not support scale.\n", __func__);
			return false;
		}
		if (((dma->in_rect.w + dma->in_rect.x) > fb_w) ||
		    ((dma->in_rect.h + dma->in_rect.y) > fb_h)) {
			dev_err(hw->dev, "%s skip ROI area is out of layer area range.\n",
				__func__);
			return false;
		}
		break;
	case VS_DMA_EXT_LAYER:
		if (!ext_layer) {
			dev_err(hw->dev, "%s not support extend layer.\n", __func__);
			return false;
		}
		if (!vs_plane_state->fb_ext) {
			dev_err(hw->dev, "%s Extend layer DMA mode has no framebuffer.\n",
				__func__);
			return false;
		}
		if (vs_plane_state->base.fb->width != state->crtc_w ||
		    vs_plane_state->base.fb->height != state->crtc_h ||
		    vs_plane_state->fb_ext->width != dma->out_rect.w ||
		    vs_plane_state->fb_ext->height != dma->out_rect.h) {
			dev_err(hw->dev, "%s Extend layer DMA mode does not support scale.\n",
				__func__);
			return false;
		}
		break;
	case VS_DMA_EXT_LAYER_EX:
		if (!ext_layer_ex) {
			dev_err(hw->dev, "%s not support extend layer ROI.\n", __func__);
			return false;
		}
		if (!vs_plane_state->fb_ext) {
			dev_err(hw->dev, "%s Extend layer DMA mode has no framebuffer.\n",
				__func__);
			return false;
		}
		if (src_w != state->crtc_w || src_h != state->crtc_h ||
		    dma->in_rect.w != dma->out_rect.w || dma->in_rect.h != dma->out_rect.h) {
			dev_err(hw->dev, "%s layer extend DMA mode does not support scale\n",
				__func__);
			return false;
		}
		if (((src_w + src_x) > fb_w) || ((src_h + src_y) > fb_h)) {
			dev_err(hw->dev, "%s ROI 0 area is out of layer area range.\n", __func__);
			return false;
		}
		if (((dma->in_rect.w + dma->in_rect.x) > vs_plane_state->fb_ext->width) ||
		    ((dma->in_rect.h + dma->in_rect.y) > vs_plane_state->fb_ext->height)) {
			dev_err(hw->dev, "%s ROI 1 area is out of layer area range.\n", __func__);
			return false;
		}
		break;
	default:
		dev_err(hw->dev, "%s Invalid drm_vs_dma_mode set: %d.\n", __func__, dma->mode);
		break;
	}

	/* check out rect whether valid */
	if (dma->mode == VS_DMA_TWO_ROI || dma->mode == VS_DMA_EXT_LAYER ||
	    dma->mode == VS_DMA_EXT_LAYER_EX) {
		if (roi_y_gap == 16) {
			if ((dma->out_rect.x <= (state->crtc_x + state->crtc_w) &&
			     dma->out_rect.y <= (state->crtc_y + state->crtc_h)) &&
			    (state->crtc_x <= dma->out_rect.x &&
			     state->crtc_y <= (dma->out_rect.y + dma->out_rect.h))) {
				dev_err(hw->dev, "%s The two display rect is overlap.\n", __func__);
				return false;
			}

			if (abs((state->crtc_y + state->crtc_h) - dma->out_rect.y) <= 16 ||
			    abs((dma->out_rect.y + dma->out_rect.h) - state->crtc_y) <= 16) {
				dev_err(hw->dev, "%s The vertical gap < 16.\n", __func__);
				return false;
			}
		} else {
			if (!(dma->out_rect.y > (state->crtc_y + state->crtc_h) ||
			      state->crtc_y > (dma->out_rect.y + dma->out_rect.h))) {
				dev_err(hw->dev, "%s The layer ROI gap < 0\n", __func__);
				return false;
			}
		}
	}

	/* check roi format constraint*/
	if (info->format_limit) {
		if (dma->mode == VS_DMA_TWO_ROI || dma->mode == VS_DMA_EXT_LAYER_EX ||
		    dma->mode == VS_DMA_SKIP_ROI) {
			ret = check_roi_offset_align(fb, info, dma->in_rect.x,
						     dma->in_rect.y);
			ret = ret | check_format_limit(fb, info, dma->in_rect.w,
						       dma->in_rect.h, ROI);

			if (ret)
				return false;
		}
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(dma_config_proto, "DMA_CONFIG", struct drm_vs_dma, dma_config_check, NULL,
			  NULL);

VS_DC_BLOB_PROPERTY_PROTO(scale_proto, "SCALER", struct drm_vs_preprocess_scale_config, NULL, NULL,
			  NULL);

static bool sbs_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
		      const void *obj_state)
{
	const struct dc_hw_plane *hw_plane = vs_dc_hw_get_plane(hw, hw_id);
	const struct vs_plane_info *plane_info = hw_plane->info;
	const struct vs_plane_state *vs_plane_state = obj_state;
	const struct drm_vs_dma *dma = NULL;
	enum drm_vs_dma_mode dma_mode = VS_DMA_NORMAL;

	const struct drm_vs_sbs *sbs = data;

	if (!plane_info->sbs) {
		dev_err(hw->dev, "%s The plane is not support side by side.\n", __func__);
		return false;
	}

	if (plane_info->roi)
		dma = vs_dc_drm_plane_property_get(vs_plane_state, "DMA_CONFIG", NULL);

	if (dma != NULL)
		dma_mode = dma->mode;

	if ((sbs->mode == VS_SBS_SPLIT) &&
	    (dma_mode != VS_DMA_NORMAL && dma_mode != VS_DMA_ONE_ROI)) {
		dev_err(hw->dev, "%s Invalid layer split sbs DMA mode.\n", __func__);
		return false;
	}

	return true;
}

static bool sbs_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_sbs *sbs = data;
	const struct dc_hw_plane *hw_plane = vs_dc_hw_get_plane(hw, hw_id);
	u32 config = 0;

	trace_config_hw_layer_feature_en("SBS", hw_id, enable);

	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SBSBUF_MODE_Address), (u32)sbs->mode);

	if (sbs->mode == VS_SBS_SPLIT) {
		trace_config_hw_layer_feature("SBS_DATA", hw_id, "mode:%d right_x:%d right_w:%d",
					      sbs->mode, sbs->right_x, sbs->right_w);

		/* RIGHT_START_X */
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SBSBUF_RIGHT_STARTX_Address), sbs->right_x);
		/* left width */
		config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_Address));
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_Address),
			 VS_SET_FIELD(config, DCREG_SH_LAYER0_OUT_ROI_SIZE, WIDTH, sbs->left_w));
		/* right width */
		config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_EX_Address));
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_EX_Address),
			 VS_SET_FIELD(config, DCREG_SH_LAYER0_OUT_ROI_SIZE_EX, WIDTH, sbs->left_w));

		if (hw_plane->fb.display_id < HW_DISPLAY_2) {
			vs_dpu_link_node_config(hw, VS_DPU_LINK_POST, HW_DISPLAY_0,
						hw_plane->fb.display_id, true);
			vs_dpu_link_node_config(hw, VS_DPU_LINK_POST, HW_DISPLAY_1,
						hw_plane->fb.display_id, true);
		} else {
			vs_dpu_link_node_config(hw, VS_DPU_LINK_POST, HW_DISPLAY_2,
						hw_plane->fb.display_id, true);
			vs_dpu_link_node_config(hw, VS_DPU_LINK_POST, HW_DISPLAY_3,
						hw_plane->fb.display_id, true);
		}
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(sbs_proto, "SBS", struct drm_vs_sbs, sbs_check, NULL, sbs_config_hw);

static bool line_padding_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			       const void *obj_state)
{
	const struct dc_hw_plane *hw_plane = vs_dc_hw_get_plane(hw, hw_id);
	const struct vs_plane_info *plane_info = hw_plane->info;
	const struct vs_plane_state *vs_plane_state = obj_state;
	const struct drm_vs_dma *dma = NULL;
	enum drm_vs_dma_mode dma_mode = VS_DMA_NORMAL;
	const struct drm_vs_scale_config *scale = NULL;

	if (!plane_info->line_padding) {
		dev_err(hw->dev, "%s The layer is not support line padding.\n", __func__);
		return false;
	}

	if (plane_info->roi)
		dma = vs_dc_drm_plane_property_get(vs_plane_state, "DMA_CONFIG", NULL);

	if (dma != NULL)
		dma_mode = dma->mode;

	if (dma_mode != VS_DMA_NORMAL && dma_mode != VS_DMA_ONE_ROI) {
		dev_err(hw->dev, "%s Invalid layer line padding dma mode.\n", __func__);
		return false;
	}

	if ((plane_info->min_scale != VS_PLANE_NO_SCALING ||
	     plane_info->max_scale != VS_PLANE_NO_SCALING) &&
	    (plane_info->min_scale != 0 || plane_info->max_scale != 0)) {
		scale = vs_dc_drm_plane_property_get(vs_plane_state, "SCALER", NULL);
		if (scale != NULL) {
			if (scale->enable) {
				dev_err(hw->dev, "%s Line padding not supported with scaling.\n",
					__func__);
				return false;
			}
		}
	}

	return true;
}

static bool line_padding_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_line_padding *line_padding = data;
	u32 config = 0;

	trace_config_hw_layer_feature_en("LINE_PADDING", hw_id, enable);

	config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_EX_Address));
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_EX_Address),
		 VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG_EX, LINE_PADDING, enable));

	if (enable) {
		trace_config_hw_layer_feature("LINE_PADDING_DATA", hw_id,
					      "mode:%d ARGB: %x %x %x %x", line_padding->mode,
					      line_padding->color.a, line_padding->color.r,
					      line_padding->color.g, line_padding->color.b);

		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, LINE_PADDING_MODE_Address),
			 line_padding->mode);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, LINE_PADDING_VALUE_A_Address),
			 line_padding->color.a);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, LINE_PADDING_VALUE_R_Address),
			 line_padding->color.r);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, LINE_PADDING_VALUE_G_Address),
			 line_padding->color.g);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, LINE_PADDING_VALUE_B_Address),
			 line_padding->color.b);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(line_padding_proto, "LINE_PADDING", struct drm_vs_line_padding,
			  line_padding_check, NULL, line_padding_config_hw);

VS_DC_BOOL_PROPERTY_PROTO(secure_buffer_proto, "SECURE_BUFFER", NULL, NULL, NULL);

bool vs_dc_register_preprocess_states(struct vs_dc_property_state_group *states,
				      const struct vs_plane_info *info)
{
	if (info->color_encoding)
		if (info->max_uv_phase)
			__ERR_CHECK(vs_dc_property_register_state(states, &uv_upsample_proto),
				    on_error);
	if (info->hdr)
		__ERR_CHECK(vs_dc_register_hdr_states(states, info), on_error);
	if (info->roi)
		__ERR_CHECK(vs_dc_property_register_state(states, &dma_config_proto), on_error);
	if ((info->min_scale != VS_PLANE_NO_SCALING || info->max_scale != VS_PLANE_NO_SCALING) &&
	    (info->min_scale != 0 || info->max_scale != 0))
		__ERR_CHECK(vs_dc_property_register_state(states, &scale_proto), on_error);
	if (info->sbs)
		__ERR_CHECK(vs_dc_property_register_state(states, &sbs_proto), on_error);
	if (info->line_padding)
		__ERR_CHECK(vs_dc_property_register_state(states, &line_padding_proto), on_error);
	if (info->sid != HW_PLANE_NOT_SUPPORTED_SID)
		__ERR_CHECK(vs_dc_property_register_state(states, &secure_buffer_proto), on_error);
	if (info->data_extend)
		__ERR_CHECK(vs_dc_property_register_state(states, &data_extend_proto), on_error);

	return true;
on_error:
	return false;
}
