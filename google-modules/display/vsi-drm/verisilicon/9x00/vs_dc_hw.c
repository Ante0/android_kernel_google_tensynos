// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#if IS_ENABLED(CONFIG_TZ_PROT)
#include <linux/gsa/tzprot.h>
#endif // CONFIG_TZ_PROT
#include <linux/media-bus-format.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_vblank.h>
#include <drm/vs_drm.h>
#include <drm/vs_drm_fourcc.h>
#include <gs_drm/gs_reg_dump.h>
#include <trace/dpu_trace.h>
#include "vs_dc.h"
#include "vs_dc_hw.h"
#include "vs_dc_info.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_reg_fe0.h"
#include "vs_dc_reg_fe1.h"
#include "vs_dc_sram.h"
#include "vs_module_params.h"
#include "display_compress/vs_dc_dsc.h"
#include "postprocess/vs_dc_display_blender.h"
#include "postprocess/vs_dc_postprocess.h"
#include "postprocess/vs_dc_histogram.h"
#include "preprocess/vs_dc_plane_blender.h"
#include "preprocess/vs_dc_preprocess.h"
#include "preprocess/vs_dc_pvric.h"
#include "preprocess/vs_dc_display_rcd.h"
#include "writeback/vs_dc_writeback.h"
#include "vs_trace.h"
#include "vs_trace_9x00.h"

#define DC_HW_INVALID 0xFFFFFFFF
/* Underrun linetime margin percentage */
#define UNDERRUN_MARGIN_PCT 99
/* Reference clock in MHz*/
#define DPU_REF_CLK_MHZ 100
#define PRINT_NAME_WIDTH "%-25s"

#define BE_IDLE_DBG_STATUS_MASK BIT(14)
#define BE_IDLE_DBG_STATUS_SEL_VALUE 0x2
#define BE_IDLE_DBG_POLL_US 100
#define BE_IDLE_DBG_TIMEOUT_US 100000

static u8 display_wb_pos[HW_DISPLAY_NUM] = { VS_WB_POS_CNT, VS_WB_POS_CNT, VS_WB_POS_CNT,
					     VS_WB_POS_CNT, VS_WB_POS_CNT };
static bool wb_busy[HW_WB_NUM] = { 0 };
static u32 cb8to2_store_value = DCREG_WB_CROSSBAR8_TO2_ResetValue;
u8 wb_split_id;
extern u32 dec_pos;
static u32 fe0_qos_value[MAX_QOS_LEVLE];
static u32 fe1_qos_value[MAX_QOS_LEVLE];
static u32 be_qos_value[MAX_QOS_LEVLE];

/* The default horizontal scale coefficient data
 * with the filter tap of 9.
 */
static const u32 default_scaling_coeff_horizontal[] = {
	0x03d3ff32, 0x279af561, 0xf561279a, 0xff3203d3, 0xff420000, 0xf5c103ac, 0x2a2e24f9,
	0x03f1f517, 0x0000ff22, 0x037eff54, 0x224bf637, 0xf4e62cac, 0xff150405, 0xff670000,
	0xf6be0349, 0x2f121f95, 0x040ff4d1, 0x0000ff0b, 0x030fff79, 0x1cdbf756, 0xf4d9315d,
	0xff03040e, 0xff8c0000, 0xf7fb02d0, 0x33881a21, 0x0401f500, 0x0000feff, 0x028eff9e,
	0x176af8ab, 0xf5483592, 0xfefe03e7, 0xffaf0000, 0xf9640249, 0x377414bb, 0x03c0f5b3,
	0x0000ff02, 0x0203ffbf, 0x1217fa23, 0xf641392d, 0xff0a038c, 0xffce0000, 0xfae601bc,
	0x3aba0f81, 0x034af6f4, 0x0000ff17, 0x0176ffdb, 0x0cfefbab, 0xf7cd3c18, 0xff2802f9,
	0xffe70000, 0xfc6f0130, 0x3d450a8f, 0x029bf8cc, 0x0000ff3f, 0x00edfff0, 0x0839fd31,
	0xf9f13e3e, 0xff5b022f, 0xfff70000, 0xfdf000ac, 0x3f0105fc, 0x01b6fb3d, 0x0000ff7d,
	0x006ffffc, 0x03ddfea8, 0xfcae3f8e, 0xffa40130, 0xffff0000, 0xff590035, 0x3fe301de,
	0x009dfe45, 0x0000ffd0, 0x00000000, 0x00000000, 0x00004000, 0x00000000, 0x00000000,
};
#define H_COEF_SIZE (sizeof(default_scaling_coeff_horizontal) / sizeof(u32))

/* The default vertical scale coefficient data
 * with the filter tap of 5.
 */
static const u32 default_scaling_coeff_vertical[] = {
	0x23fffc01, 0xfc0123ff, 0xfc600000, 0x26b72142, 0x0000fba7, 0x1e84fcc2, 0xfb542966,
	0xfd240000, 0x2c061bca, 0x0000fb0c, 0x1917fd86, 0xfad12e92, 0xfde40000, 0x31061670,
	0x0000faa6, 0x13d9fe3e, 0xfa8e335b, 0xfe910000, 0x358d1156, 0x0000fa8c, 0x0ee9fede,
	0xfaa23797, 0xff230000, 0x39730c96, 0x0000fad4, 0x0a60ff5f, 0xfb253b1c, 0xff910000,
	0x3c8f084a, 0x0000fb96, 0x0656ffba, 0xfc2b3dc5, 0xffda0000, 0x3ebb0486, 0x0000fce5,
	0x02dcfff0, 0xfdc63f6e, 0xfffc0000, 0x3fdb015a, 0x0000fecf, 0x00000000, 0x00004000,
	0x00000000,
};

#define V_COEF_SIZE (sizeof(default_scaling_coeff_vertical) / sizeof(u32))

#define FRAC_16_16(mult, div) (((mult) << 16) / (div))

#define SW_RESET_INTERVAL_US 1000
#define SW_RESET_TIMEOUT_US 100000

#define FIND_HIGHEST_BIT(n) (31 - __builtin_clz(n))

static const struct dc_hw_funcs hw_func;
static const struct dc_hw_sub_funcs hw_sub_func[];
static void wb_enable_shadow(struct dc_hw *hw, u8 hw_id, bool enable);
static void wb_ex_enable_shadow(struct dc_hw *hw, u8 hw_id, bool enable);
static void print_display_state(struct seq_file *s, const struct dc_hw_display *display,
				const u8 indent);
static void print_plane_state(struct seq_file *s, const struct dc_hw_plane *plane, const u8 indent);
static void print_wb_state(struct seq_file *s, const struct dc_hw_wb *wb, const u8 indent);

u8 dc_hw_get_plane_id(u8 layer, struct dc_hw *hw)
{
	const struct vs_plane_info *plane_info = get_plane_info(layer, hw->info);

	return plane_info->id;
}

void vs_dpu_query_module_range(u32 moduleid, u32 *reg_start, u32 *reg_end)
{
	u32 start = 0;
	u32 end = 0;

	/*get the range of specfic reg: [Start, end). */
	switch (moduleid) {
	case 0:
		start = DCREG_LAYER0_CONFIG_Address;
		end = DCREG_FE0_LAYER0_END_Address;
		break;
	case 1:
		start = DCREG_LAYER1_CONFIG_Address;
		end = DCREG_FE0_LAYER1_END_Address;
		break;
	case 2:
		start = DCREG_LAYER2_CONFIG_Address;
		end = DCREG_FE0_LAYER2_END_Address;
		break;
	case 3:
		start = DCREG_LAYER3_CONFIG_Address;
		end = DCREG_FE0_LAYER3_END_Address;
		break;
	case 4:
		start = DCREG_LAYER4_CONFIG_Address;
		end = DCREG_FE0_LAYER4_END_Address;
		break;
	case 5:
		start = DCREG_LAYER5_CONFIG_Address;
		end = DCREG_FE0_LAYER5_END_Address;
		break;
	case 7:
		start = DCREG_LAYER8_CONFIG_Address;
		end = DCREG_FE1_LAYER8_END_Address;
		break;
	case 8:
		start = DCREG_LAYER9_CONFIG_Address;
		end = DCREG_FE1_LAYER9_END_Address;
		break;
	case 9:
		start = DCREG_LAYER10_CONFIG_Address;
		end = DCREG_FE1_LAYER10_END_Address;
		break;
	case 10:
		start = DCREG_LAYER11_CONFIG_Address;
		end = DCREG_FE1_LAYER11_END_Address;
		break;
	case 11:
		start = DCREG_LAYER12_CONFIG_Address;
		end = DCREG_FE1_LAYER12_END_Address;
		break;
	case 12:
		start = DCREG_LAYER13_CONFIG_Address;
		end = DCREG_FE1_LAYER13_END_Address;
		break;
	case 14:
		start = DCREG_PANEL0_CONFIG_Address;
		end = DCREG_BE_POST_PROCESSOR_PANEL0_END_Address;
		break;
	case 15:
		start = DCREG_PANEL1_CONFIG_Address;
		end = DCREG_BE_POST_PROCESSOR_PANEL1_END_Address;
		break;
	case 16:
		start = DCREG_PANEL2_CONFIG_Address;
		end = DCREG_BE_POST_PROCESSOR_PANEL2_END_Address;
		break;
	case 17:
		start = DCREG_PANEL3_CONFIG_Address;
		end = DCREG_BE_POST_PROCESSOR_PANEL3_END_Address;
		break;
	case 18:
		start = DCREG_SH_WB0_SPLITER_COORD_X_Address;
		end = DCREG_BE_POST_PROCESSOR_WB0_END_Address;
		break;
	case 19:
		start = DCREG_SH_WB1_SPLITER_COORD_X_Address;
		end = DCREG_BE_POST_PROCESSOR_WB1_END_Address;
		break;
	}
	*reg_start = start;
	*reg_end = end;
}

void vs_dpu_link_node_config(struct dc_hw *hw, enum vs_dpu_link_node_type type, u32 moduleid,
			     u32 link_node_id, bool enable)
{
	u32 config = 0;
	/* At most 4 linknodes */
	if (link_node_id > 4)
		link_node_id = 4;

	switch (type) {
	case VS_DPU_LINK_LAYER:
		switch (moduleid) {
		case 0:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER0);
			break;
		case 1:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER1);
			break;
		case 2:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER2);
			break;
		case 3:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER3);
			break;
		case 4:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER4);
			break;
		case 5:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER5);
			break;
		case 8:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER8);
			break;
		case 9:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER9);
			break;
		case 10:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER10);
			break;
		case 11:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER11);
			break;
		case 12:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER12);
			break;
		case 13:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_LAYER13);
			break;

		default:
			break;
		}
		break;

	case VS_DPU_LINK_POST:
		switch (moduleid) {
		case 0:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_POST0);
			break;
		case 1:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_POST1);
			break;
		case 2:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_POST2);
			break;
		case 3:
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_POST3);
			break;

		default:
			break;
		}
		break;

	case VS_DPU_LINK_WB:
		if (moduleid == 0)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_WB0);
		else if (moduleid == 1)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_WB1);
		break;

	case VS_DPU_LINK_WB_SPILTER:
		if (moduleid == 0)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_WB_SPLITER0);
		else if (moduleid == 1)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_WB_SPLITER1);
		break;

	case VS_DPU_LINK_DSC:
		if (moduleid == 0)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_DSC0);
		else if (moduleid == 1)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_DSC1);
		else if (moduleid == 2)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_DSC2);
		break;

	case VS_DPU_LINK_DBUFFER:
		if (moduleid == 0)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_DBUFFER0);
		else if (moduleid == 1)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_DBUFFER1);
		else if (moduleid == 2)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_DBUFFER2);
		break;

	case VS_DPU_LINK_TM:
		if (moduleid == 0)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_TM0);
		else if (moduleid == 1)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_TM1);
		else if (moduleid == 2)
			config |= 1 << VS_GET_START(DCREG_SH_LINK_NODE0_RESOURCE_TM2);
		break;

	default:
		return;
	}

	if (enable)
		hw->link_node_resource[link_node_id] |= config;
	else
		hw->link_node_resource[link_node_id] &= ~config;
}

u32 vs_dpu_link_node_config_get(struct dc_hw *hw, u32 link_node_id)
{
	if (link_node_id < DC_DISPLAY_NUM)
		return hw->link_node_resource[link_node_id];
	else
		return 0;
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
void dc_hw_save_plane_status(struct dc_hw_plane *plane)
{
	u32 i = 0;

	if (plane->fb.enable) {
		plane->fb.dirty = true;
		plane->fb.secure_dirty = true;
	}

	if (plane->fb_ext.enable)
		plane->fb_ext.dirty = true;

	if (plane->pos.enable)
		plane->pos.dirty = true;

	if (plane->std_bld.enable)
		plane->std_bld.dirty = true;

	if (plane->y2r.enable)
		plane->y2r.dirty = true;

	if (plane->roi.enable)
		plane->roi.dirty = true;

	if (plane->scale.enable)
		plane->scale.dirty = true;

	if (plane->lut_3d.enable)
		plane->lut_3d.dirty = true;

	if (plane->rcd_mask.enable)
		plane->rcd_mask.dirty = true;

	if (plane->clear.enable)
		plane->clear.dirty = true;

	plane->sram.dirty = true; /* sram pool set recover always. */
	plane->scale.coefficients_dirty = true; /* always reprogram default scale coefficients */

	for (i = 0; i < plane->states.num; i++) {
		struct vs_dc_property_state *state = &plane->states.items[i];

		if (state->enable)
			state->dirty = true;
	}
}

void dc_hw_save_display_status(struct dc_hw_display *display)
{
	u32 i = 0;

	if (display->bld_size.enable)
		display->bld_size.dirty = true;

	if (display->gamma.enable[0] || display->gamma.enable[1] || display->gamma.enable[2])
		display->gamma.dirty = true;

	if (display->wb.enable)
		display->wb.dirty = true;

	if (display->blur_mask.enable)
		display->blur_mask.dirty = true;

	if (display->brightness_mask.enable)
		display->brightness_mask.dirty = true;

	if (display->ltm_enable.enable)
		display->ltm_enable.dirty = true;

	if (display->ltm_degamma.enable)
		display->ltm_degamma.dirty = true;

	if (display->ltm_gamma.enable)
		display->ltm_gamma.dirty = true;

	if (display->ltm_luma.enable)
		display->ltm_luma.dirty = true;

	if (display->freq_decomp.enable)
		display->freq_decomp.dirty = true;

	if (display->luma_adj.enable)
		display->luma_adj.dirty = true;

	if (display->grid_size.enable)
		display->grid_size.dirty = true;

	if (display->af_filter.enable)
		display->af_filter.dirty = true;

	if (display->af_slice.enable)
		display->af_slice.dirty = true;

	if (display->af_trans.enable)
		display->af_trans.dirty = true;

	if (display->tone_adj.enable)
		display->tone_adj.dirty = true;

	if (display->ltm_color.enable)
		display->ltm_color.dirty = true;

	if (display->ltm_dither.enable)
		display->ltm_dither.dirty = true;

	if (display->ltm_luma_set.enable)
		display->ltm_luma_set.dirty = true;

	if (display->ltm_luma_get.enable)
		display->ltm_luma_get.dirty = true;

	if (display->ltm_cd_set.enable)
		display->ltm_cd_set.dirty = true;

	if (display->ltm_cd_get.enable)
		display->ltm_cd_get.dirty = true;

	if (display->ltm_hist_set.enable)
		display->ltm_hist_set.dirty = true;

	if (display->ltm_hist_get.enable)
		display->ltm_hist_get.dirty = true;

	if (display->ltm_ds.enable)
		display->ltm_ds.dirty = true;

	for (int i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++)
		if (display->hw_hist_chan[i].enable)
			display->hw_hist_chan[i].dirty = true;

	if (display->hw_hist_rgb.enable)
		display->hw_hist_rgb.dirty = true;

	for (i = 0; i < display->states.num; i++) {
		struct vs_dc_property_state *state = &display->states.items[i];

		if (state->enable)
			state->dirty = true;
	}
}

void dc_hw_save_wb_status(struct dc_hw_wb *wb)
{
	u32 i = 0;

	if (wb->fb.enable)
		wb->fb.dirty = true;

	for (i = 0; i < wb->states.num; i++) {
		struct vs_dc_property_state *state = &wb->states.items[i];

		if (state->enable)
			state->dirty = true;
	}
}

void dc_hw_fe0_save_status(struct dc_hw *hw)
{
	u8 i = 0;

	if (!hw->info) {
		dev_err(hw->dev, "uninitialized vs_dc_info object\n");
		return;
	}

	for (i = 0; i < hw->info->plane_fe0_num; ++i) {
		if (!hw->plane[i].config_status)
			continue;

		dc_hw_save_plane_status(&hw->plane[i]);
	}
}

void dc_hw_fe1_save_status(struct dc_hw *hw)
{
	u8 i = 0;

	if (!hw->info) {
		dev_err(hw->dev, "uninitialized vs_dc_info object\n");
		return;
	}

	for (i = hw->info->plane_fe0_num; i < hw->info->plane_num; ++i) {
		if (!hw->plane[i].config_status)
			continue;

		dc_hw_save_plane_status(&hw->plane[i]);
	}
}

static void dc_hw_fe_save_status(struct dc_hw *hw)
{
	dc_hw_fe0_save_status(hw);
	dc_hw_fe1_save_status(hw);
}

void dc_hw_be_save_status(struct dc_hw *hw)
{
	u8 i = 0;

	if (!hw->info) {
		dev_err(hw->dev, "uninitialized vs_dc_info object\n");
		return;
	}

	for (i = 0; i < hw->info->display_num; i++)
		dc_hw_save_display_status(&hw->display[i]);

	for (i = 0; i < hw->info->wb_num; i++) {
		if (!hw->wb[i].config_status)
			continue;

		dc_hw_save_wb_status(&hw->wb[i]);
	}
}

void dc_hw_save_status(struct dc_hw *hw)
{
	dc_hw_fe_save_status(hw);
	dc_hw_be_save_status(hw);
}
#endif

/* Get the plane address offset based on HW_PLANE_0 */
static u32 _get_plane_offset(u32 hw_id)
{
	u32 offset = 0;
	u32 module_offset = 0;
	u32 bsae_offset = DCREG_SH_LAYER1_CONFIG_Address - DCREG_SH_LAYER0_CONFIG_Address;

	if (hw_id > HW_PLANE_7)
		module_offset = DCREG_SH_LAYER8_CONFIG_Address - DCREG_SH_LAYER0_CONFIG_Address;

	offset = ((u32)hw_id % 8) * bsae_offset + module_offset;

	return offset;
}

/* Get the plane test pattern address offset based on HW_PLANE_0 */
static u32 _get_plane_tp_offset(u32 hw_id)
{
	u32 offset = 0;
	u32 module_offset = 0;
	u32 bsae_offset = DCREG_SH_LAYER1_TP_ENABLE_Address - DCREG_SH_LAYER0_TP_ENABLE_Address;

	if (hw_id > HW_PLANE_7)
		module_offset =
			DCREG_SH_LAYER8_TP_ENABLE_Address - DCREG_SH_LAYER0_TP_ENABLE_Address;

	offset = ((u32)hw_id % 8) * bsae_offset + module_offset;

	return offset;
}

/* Get the plane output path addr offset based on HW_PLANE_0 */
static u32 _get_plane_out_offset(u32 hw_id)
{
	u32 offset = 0;
	u32 bsae_offset = DCREG_LAYER1_OUTPUT_PATH_ID_Address - DCREG_LAYER0_OUTPUT_PATH_ID_Address;

	offset = hw_id * bsae_offset;

	return offset;
}

static u32 _get_plane_zorder_offset(u32 hw_id)
{
	u32 offset = 0x0;
	u32 base_offset =
		DCREG_SH_LAYER1_BLEND_STACK_ID_Address - DCREG_SH_LAYER0_BLEND_STACK_ID_Address;

	offset = hw_id * base_offset;

	return offset;
}

/* Get the display test pattern addr offset based on HW_DISPLAY_0 */
static u32 _get_display_tp_offset(u32 hw_id, u32 output_id, enum drm_vs_disp_tp_pos pos)
{
	u32 offset = 0;
	u32 base_offset =
		DCREG_SH_PANEL1_BLEND_TP_ENABLE_Address - DCREG_SH_PANEL0_BLEND_TP_ENABLE_Address;
	u32 pos_offset[3] = { 0 };

	pos_offset[1] = DCREG_SH_PANEL0_POST_PRO_TP_ENABLE_Address -
			DCREG_SH_PANEL0_BLEND_TP_ENABLE_Address;
	pos_offset[2] =
		DCREG_SH_PANEL0_OFIFO_TP_ENABLE_Address - DCREG_SH_PANEL0_BLEND_TP_ENABLE_Address;

	if (pos == VS_DISP_TP_OFIFO)
		offset = output_id * base_offset + pos_offset[pos];
	else
		offset = hw_id * base_offset + pos_offset[pos];

	return offset;
}

/************************************************************************
 * Below are the main hw interfaces.
 *
 * TBD !!
 * Developers should add or modify the following interfaces
 * and corresponding implementations according to the actual
 * requirements during developing !
 *
 ************************************************************************/
void dc_hw_config_load_filter(struct dc_hw *hw, u8 hw_id, const u32 *coef_v, const u32 *coef_h)
{
	const u32 *ch = default_scaling_coeff_horizontal;
	const u32 *cv = default_scaling_coeff_vertical;

	// Use the user coefficients if they were both provided
	if (coef_v && coef_h) {
		ch = coef_h;
		cv = coef_v;
	}

	dc_write_u32_blob(hw, VS_SH_LAYER_FIELD(hw_id, HSCALE_COEF_DATA_Address), ch, H_COEF_SIZE);

	dc_write_u32_blob(hw, VS_SH_LAYER_FIELD(hw_id, VSCALE_COEF_DATA_Address), cv, V_COEF_SIZE);
}

static void load_be_default_filter(struct dc_hw *hw, u8 hw_id)
{
	/* TBD
	 * Lack of filter tap registers.
	 */

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALE_INITIAL_OFFSET_X_Address),
		 0x8000);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALE_INITIAL_OFFSET_Y_Address),
		 0x8000);

	dc_write_u32_blob(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, HSCALE_COEF_DATA_Address),
			  default_scaling_coeff_horizontal, H_COEF_SIZE);

	dc_write_u32_blob(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, VSCALE_COEF_DATA_Address),
			  default_scaling_coeff_vertical, V_COEF_SIZE);
}

int dc_fe0_hw_init(struct dc_hw *hw)
{
	int ret = 0;
	u8 i;
	const struct vs_plane_info *plane_info;

	for (i = 0; i < hw->info->plane_fe0_num; i++) {
		plane_info = &hw->info->planes_fe0[i];
		hw->plane[i].info = plane_info;
		hw->plane[i].func.print_state = print_plane_state;

		/* Write scale coeffs the first time a plane uses scaling */
		if (plane_info->min_scale != VS_PLANE_NO_SCALING ||
		    plane_info->max_scale != VS_PLANE_NO_SCALING)
			hw->plane[i].scale.coefficients_dirty = true;

		/* Initialize property states */
		if (!vs_dc_register_plane_blender_states(&hw->plane[i].states, plane_info)) {
			dev_err(hw->dev, "%s: Failed to register plane blender states.\n",
				__func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		if (!vs_dc_register_preprocess_states(&hw->plane[i].states, plane_info)) {
			dev_err(hw->dev, "%s: Failed to register preprocess.\n", __func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}

		if (!vs_dc_initialize_property_states(&hw->plane[i].states)) {
			dev_err(hw->dev, "%s: Failed to initialize plane property states.\n",
				__func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		dev_dbg(hw->dev, "%s: Alloc states mem %lu for plane %u\n", __func__,
			hw->plane[i].states.mem.total_size, i);
	}

	return ret;
err_cleanup:
	return ret;
}

int dc_fe1_hw_init(struct dc_hw *hw)
{
	int ret = 0;
	u8 i, j;
	const struct vs_plane_info *plane_info;

	for (i = 0; i < hw->info->plane_fe1_num; i++) {
		plane_info = &hw->info->planes_fe1[i];
		j = hw->info->plane_fe0_num + i;
		hw->plane[j].info = plane_info;
		hw->plane[j].func.print_state = print_plane_state;

		/* Write scale coeffs the first time a plane uses scaling */
		if (plane_info->min_scale != VS_PLANE_NO_SCALING ||
		    plane_info->max_scale != VS_PLANE_NO_SCALING)
			hw->plane[j].scale.coefficients_dirty = true;

		/* Initialize property states */
		if (!vs_dc_register_plane_blender_states(&hw->plane[j].states, plane_info)) {
			dev_err(hw->dev, "%s: Failed to register plane blender states.\n",
				__func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		if (!vs_dc_register_preprocess_states(&hw->plane[j].states, plane_info)) {
			dev_err(hw->dev, "%s: Failed to register preprocess.\n", __func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}

		if (!vs_dc_initialize_property_states(&hw->plane[j].states)) {
			dev_err(hw->dev, "%s: Failed to initialize plane property states.\n",
				__func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		dev_dbg(hw->dev, "%s: Alloc states mem %lu for plane %u\n", __func__,
			hw->plane[j].states.mem.total_size, j);
	}

	return ret;
err_cleanup:
	return ret;
}

static void dc_hw_display_urgent_init(struct dc_hw *hw, u8 hw_id)
{
	const struct vs_dc_info *dc_info = hw->info;

	if (dc_info->urgent_cmd_config) {
		memcpy(&hw->display[hw_id].urgent_cmd_config, dc_info->urgent_cmd_config,
		       sizeof(*dc_info->urgent_cmd_config));
	}

	if (dc_info->urgent_vid_config) {
		memcpy(&hw->display[hw_id].urgent_vid_config, dc_info->urgent_vid_config,
		       sizeof(*dc_info->urgent_vid_config));
	}
}

int dc_be_hw_init(struct dc_hw *hw)
{
	int ret = 0;
	u8 i, hw_id;
	const struct vs_display_info *display_info;

	for (i = 0; i < hw->info->display_num; i++) {
		display_info = &hw->info->displays[i];
		hw->display[i].info = display_info;
		hw->display[i].func.print_state = print_display_state;
		hw_id = display_info->id;

		if (display_info->min_scale != FRAC_16_16(1, 1) ||
		    display_info->max_scale != FRAC_16_16(1, 1))
			load_be_default_filter(hw, hw_id);

		/* Initialize property states */
		if (!vs_dc_register_display_blender_states(&hw->display[i].states, display_info)) {
			dev_err(hw->dev, "%s: Failed to register blender.\n", __func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		if (!vs_dc_register_postprocess_states(&hw->display[i].states, display_info)) {
			dev_err(hw->dev, "%s: Failed to register postprocess.\n", __func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}

		if (!vs_dc_initialize_property_states(&hw->display[i].states)) {
			dev_err(hw->dev, "%s: Failed to initialize display property states.\n",
				__func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		dev_dbg(hw->dev, "%s: Alloc states mem %lu for display %u\n", __func__,
			hw->display[i].states.mem.total_size, i);

		/* initialize internal tunable hardware configuration */
		dc_hw_display_urgent_init(hw, hw_id);
	}

	return ret;

err_cleanup:
	return ret;
}

int dc_wb_hw_init(struct dc_hw *hw)
{
	int ret = 0;
	u8 i;
	const struct vs_wb_info *wb_info;

	for (i = 0; i < hw->info->wb_num; i++) {
		wb_info = &hw->info->write_back[i];
		hw->wb[i].info = wb_info;
		hw->wb[i].func.print_state = print_wb_state;

		/* Initialize property states */
		if (!vs_dc_register_writeback_states(&hw->wb[i].states, wb_info)) {
			dev_err(hw->dev, "%s: Failed to register writeback states.\n", __func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}

		if (!vs_dc_initialize_property_states(&hw->wb[i].states)) {
			dev_err(hw->dev, "%s: Failed to initialize writeback property states.\n",
				__func__);
			ret = -ENOMEM;
			goto err_cleanup;
		}
		dev_dbg(hw->dev, "%s: Alloc states mem %lu for writeback %u\n", __func__,
			hw->wb[i].states.mem.total_size, i);
		hw->wb_irq_refcnt[i] = 0;
	}

	return ret;

err_cleanup:
	return ret;
}

int dc_hw_init(struct dc_hw *hw)
{
	int ret = 0;
	u32 chip_id, revision, pid, cid;

	chip_id = dc_read_immediate(hw, DCREG_CHIP_ID_Address);
	revision = dc_read_immediate(hw, DCREG_CHIP_REV_Address);
	pid = dc_read_immediate(hw, DCREG_PRODUCT_ID_Address);
	cid = dc_read_immediate(hw, DCREG_CHIP_CUSTOMER_Address);

	hw->info = vs_dc_get_chip_info(chip_id, revision, pid, cid);
	if (hw->info == NULL) {
		dev_err(hw->dev, "Could not find matched hw.\n");
		dev_err(hw->dev, "chip_id = %x\n", chip_id);
		dev_err(hw->dev, "revision = %x\n", revision);
		dev_err(hw->dev, "pid = %x\n", pid);
		dev_err(hw->dev, "cid = %x\n", cid);

		ret = -EPERM;
		goto err_cleanup;
	}

	/* default to NS interrupt destination */
	hw->intr_dest = BIT(0);

	if (chip_id == VS_CHIP_ID_9400)
		hw->rev = DC_REV_0;

	hw->output_mux_value = DCREG_POST_PROCESS_OUT_ResetValue;

	hw->func = (struct dc_hw_funcs *)&hw_func;
	hw->sub_func = (struct dc_hw_sub_funcs *)&hw_sub_func[hw->rev];

	return ret;
err_cleanup:
	return ret;
}

void dc_fe0_hw_deinit(struct dc_hw *hw)
{
	u8 i;

	for (i = 0; i < hw->info->layer_fe0_num; i++) {
		vs_dc_deinitialize_property_states(&hw->plane[i].states);
		memset(&hw->plane[i], 0, sizeof(struct dc_hw_plane));
	}
}

void dc_fe1_hw_deinit(struct dc_hw *hw)
{
	u8 i;

	for (i = hw->info->layer_fe0_num; i < hw->info->layer_num; i++) {
		vs_dc_deinitialize_property_states(&hw->plane[i].states);
		memset(&hw->plane[i], 0, sizeof(struct dc_hw_plane));
	}
}

void dc_be_hw_deinit(struct dc_hw *hw)
{
	u8 i;

	for (i = 0; i < hw->info->display_num; i++) {
		vs_dc_deinitialize_property_states(&hw->display[i].states);
		vs_dc_hist_chans_reset(hw, i);
		vs_dc_hist_rgb_reset(hw, i);
		memset(&hw->display[i], 0, sizeof(struct dc_hw_display));
	}
}

void dc_wb_hw_deinit(struct dc_hw *hw)
{
	u8 i;

	for (i = 0; i < hw->info->wb_num; i++) {
		vs_dc_deinitialize_property_states(&hw->wb[i].states);
		memset(&hw->wb[i], 0, sizeof(struct dc_hw_wb));
	}
}

void dc_hw_deinit(struct dc_hw *hw)
{
	dc_fe0_hw_deinit(hw);
	dc_fe1_hw_deinit(hw);
	dc_be_hw_deinit(hw);
	dc_wb_hw_deinit(hw);
}

void dc_hw_reinit(struct dc_hw *hw)
{
	int ret = 0;

	dc_hw_deinit(hw);

	ret = dc_hw_init(hw);
	if (ret < 0)
		dev_err(hw->dev, "dc_hw_init failed (%d)", ret);
	ret = dc_be_hw_init(hw);
	if (ret < 0)
		dev_err(hw->dev, "dc_be_hw_init failed (%d)", ret);
	ret = dc_fe0_hw_init(hw);
	if (ret < 0)
		dev_err(hw->dev, "dc_fe0_hw_init failed (%d)", ret);
	ret = dc_fe1_hw_init(hw);
	if (ret < 0)
		dev_err(hw->dev, "dc_fe1_hw_init failed (%d)", ret);
	ret = dc_wb_hw_init(hw);
	if (ret < 0)
		dev_err(hw->dev, "dc_wb_hw_init failed (%d)", ret);
}

void dc_hw_update_plane(struct dc_hw *hw, u8 id, struct dc_hw_fb *fb)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && fb) {
		bool original_secure = plane->fb.secure;

		if (!fb->enable) {
			plane->fb.enable = false;
			plane->fb.secure = false;
		} else {
			memcpy(&plane->fb, fb, offsetof(struct dc_hw_fb, secure_dirty));
		}

		plane->fb.dirty = true;
		plane->fb.secure_dirty |= (plane->fb.secure != original_secure);
	}

	trace_update_hw_layer_feature_en_dirty("FB", hw_id, plane->fb.enable, plane->fb.dirty);
	trace_update_hw_layer_feature_en_dirty("SECURE_FB", hw_id, plane->fb.secure,
					       plane->fb.secure_dirty);
}

void dc_hw_update_plane_sram(struct dc_hw *hw, u8 id, struct dc_hw_sram_pool *sram)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && sram) {
		memcpy(&plane->sram, sram, sizeof(*sram) - sizeof(sram->dirty));
		plane->sram.dirty = true;
	}

	trace_update_hw_layer_feature_en_dirty("SRAM DMA", hw_id,
					       plane->sram.sp_handle ? true : false,
					       plane->sram.dirty);

	if (plane->info->scl_sram_max_size_kb)
		trace_update_hw_layer_feature_en_dirty("SRAM SCL", hw_id,
						       plane->sram.scl_sp_handle ? true : false,
						       plane->sram.dirty);
}

void dc_hw_update_plane_position(struct dc_hw *hw, u8 id, struct dc_hw_position *pos)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && pos) {
		memcpy(&plane->pos, pos, sizeof(*pos) - sizeof(pos->dirty));
		plane->pos.dirty = true;
		plane->pos.enable = true;
	}

	trace_update_hw_layer_feature_en_dirty("POS", hw_id, plane->pos.enable, plane->pos.dirty);
}

void dc_hw_update_ext_fb(struct dc_hw *hw, u8 id, struct dc_hw_fb *fb)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && fb) {
		memcpy(&plane->fb_ext, fb, sizeof(*fb) - sizeof(fb->dirty));
		plane->fb_ext.enable = fb->enable;
		plane->fb_ext.dirty = true;
	}

	trace_update_hw_layer_feature_en_dirty("EXT_FB", hw_id, plane->fb_ext.enable,
					       plane->fb_ext.dirty);
}

void dc_hw_update_plane_y2r(struct dc_hw *hw, u8 id, struct dc_hw_y2r *y2r_conf)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && y2r_conf) {
		memcpy(&plane->y2r, y2r_conf, sizeof(*y2r_conf) - sizeof(y2r_conf->dirty));
		plane->y2r.dirty = true;
		plane->y2r.enable = y2r_conf->enable;
	}

	trace_update_hw_layer_feature_en_dirty("Y2R", hw_id, plane->y2r.enable, plane->y2r.dirty);
}

void dc_hw_update_plane_scale(struct dc_hw *hw, u8 id, struct dc_hw_scale *scale)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (!plane || !scale) {
		plane->scale.enable = false;
		goto end;
	}

	if (scale->dirty || scale->coefficients_dirty) {
		memcpy(&plane->scale, scale, offsetof(struct dc_hw_scale, coefficients_dirty));

		/* set to dirty if configured above but don't unset in case runtime pm set it */
		plane->scale.dirty |= scale->dirty;
		plane->scale.coefficients_dirty |= scale->coefficients_dirty;
	}

end:
	trace_update_hw_layer_feature_en_dirty("SCALE", hw_id, plane->scale.enable,
					       plane->scale.dirty);
	trace_update_hw_layer_feature_en_dirty("SCALE_COEFF", hw_id,
					       plane->scale.coefficients_enable,
					       plane->scale.coefficients_dirty);
}

void dc_hw_update_plane_roi(struct dc_hw *hw, u8 id, struct dc_hw_roi *roi)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && roi) {
		memcpy(&plane->roi, roi, sizeof(struct dc_hw_roi));

		plane->roi.dirty = true;
		plane->roi.enable = true;
	}

	trace_update_hw_layer_feature_en_dirty("ROI", hw_id, plane->roi.enable, plane->roi.dirty);
}

void dc_hw_update_plane_clear(struct dc_hw *hw, u8 id, struct dc_hw_clear *clear)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && clear) {
		memcpy(&plane->clear, clear, sizeof(struct dc_hw_clear) - sizeof(clear->dirty));
		plane->clear.dirty = true;
	}

	trace_update_hw_layer_feature_en_dirty("CLEAR", hw_id, plane->clear.enable,
					       plane->clear.dirty);
}

void dc_hw_update_plane_3d_lut(struct dc_hw *hw, u8 id, struct dc_hw_block *lut_3d)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && lut_3d) {
		memcpy(&plane->lut_3d, lut_3d, sizeof(*lut_3d) - sizeof(lut_3d->dirty));
		plane->lut_3d.dirty = true;
	}

	trace_update_hw_layer_feature_en_dirty("3D_LUT", hw_id, plane->lut_3d.enable,
					       plane->lut_3d.dirty);
}

void dc_hw_update_plane_std_bld(struct dc_hw *hw, u8 id, struct dc_hw_std_bld *std_bld)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && std_bld) {
		memcpy(&plane->std_bld, std_bld, sizeof(*std_bld) - sizeof(std_bld->dirty));
		plane->std_bld.dirty = true;
		plane->std_bld.enable = true;
	}

	trace_update_hw_layer_feature_en_dirty("STD_BLEND", hw_id, plane->std_bld.enable,
					       plane->std_bld.dirty);
}

void dc_hw_update_plane_rcd_mask(struct dc_hw *hw, u8 id, struct dc_hw_rcd_mask *rcd_mask)
{
	struct dc_hw_plane *plane = NULL;
	u8 hw_id = dc_hw_get_plane_id(id, hw);

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane && rcd_mask) {
		memcpy(&plane->rcd_mask, rcd_mask, sizeof(*rcd_mask) - sizeof(rcd_mask->dirty));
		plane->rcd_mask.dirty = true;
		plane->rcd_mask.enable = !!(rcd_mask->address);
	}

	trace_update_hw_layer_feature_en_dirty("RCD", hw_id, plane->rcd_mask.enable,
					       plane->rcd_mask.dirty);
}

void dc_hw_update_display_bld_size(struct dc_hw *hw, u8 id, bool user_conf,
				   struct dc_hw_size *bld_size)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display) {
		if (user_conf && bld_size) {
			memcpy(&display->bld_size, bld_size,
			       sizeof(*bld_size) - sizeof(bld_size->dirty));
			display->bld_size.dirty = true;
			display->bld_size.enable = true;
		} else if (!user_conf && display->mode.h_active && display->mode.v_active) {
			display->bld_size.width = display->mode.h_active;
			display->bld_size.height = display->mode.v_active;
			display->bld_size.dirty = true;
			display->bld_size.enable = true;
		}
	}

	trace_update_hw_display_feature_en_dirty("BLD_SIZE", id, display->bld_size.enable,
						 display->bld_size.dirty);
}

void dc_hw_update_display_ltm_luma_get(struct dc_hw *hw, u8 id,
				       struct dc_hw_ltm_luma_get *ltm_luma_get)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && ltm_luma_get) {
		memcpy(&display->ltm_luma_get, ltm_luma_get,
		       sizeof(*ltm_luma_get) - sizeof(ltm_luma_get->dirty));
		display->ltm_luma_get.dirty = true;
	}

	trace_update_hw_display_feature_en_dirty("LTM_LUMA_GET", id, display->ltm_luma_get.enable,
						 display->ltm_luma_get.dirty);
}

void dc_hw_update_display_ltm_cd_get(struct dc_hw *hw, u8 id, struct dc_hw_ltm_cd_get *ltm_cd_get)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && ltm_cd_get) {
		memcpy(&display->ltm_cd_get, ltm_cd_get,
		       sizeof(*ltm_cd_get) - sizeof(ltm_cd_get->dirty));
		display->ltm_cd_get.dirty = true;
	}

	trace_update_hw_display_feature_en_dirty("LTM_CD_GET", id, display->ltm_cd_get.enable,
						 display->ltm_cd_get.dirty);
}

void dc_hw_update_display_ltm_hist_get(struct dc_hw *hw, u8 id,
				       struct dc_hw_ltm_hist_get *ltm_hist_get)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && ltm_hist_get) {
		memcpy(&display->ltm_hist_get, ltm_hist_get,
		       sizeof(*ltm_hist_get) - sizeof(ltm_hist_get->dirty));
		display->ltm_hist_get.dirty = true;
	}

	trace_update_hw_display_feature_en_dirty("LTM_HIST_GET", id, display->ltm_hist_get.enable,
						 display->ltm_hist_get.dirty);
}

void dc_hw_update_display_blur_mask(struct dc_hw *hw, u8 id, struct dc_hw_fb *fb)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && fb) {
		memcpy(&display->blur_mask, fb, sizeof(*fb) - sizeof(fb->dirty));
		display->blur_mask.dirty = true;
		display->blur_mask.enable = true;
	}

	trace_update_hw_display_feature_en_dirty("BLUR", id, display->blur_mask.enable,
						 display->blur_mask.dirty);
}

void dc_hw_update_display_brightness_mask(struct dc_hw *hw, u8 id, struct dc_hw_fb *fb)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && fb) {
		memcpy(&display->brightness_mask, fb, sizeof(*fb) - sizeof(fb->dirty));
		display->brightness_mask.dirty = true;
		display->brightness_mask.enable = true;
	}

	trace_update_hw_display_feature_en_dirty("BRIGHTNESS", id, display->brightness_mask.enable,
						 display->brightness_mask.dirty);
}

void dc_hw_update_r2y(struct dc_hw *hw, u8 id, struct dc_hw_r2y *r2y_conf)
{
	struct dc_hw_wb *wb = NULL;

	if (!hw || !hw->info || id >= hw->info->wb_num)
		return;

	wb = &hw->wb[id];

	if (wb && r2y_conf) {
		memcpy(&wb->r2y, r2y_conf, sizeof(*r2y_conf));
		wb->r2y.dirty = true;
	}

	trace_update_hw_layer_feature_en_dirty("R2Y", id, wb->r2y.enable, wb->r2y.dirty);
}

void dc_hw_update_display_wb(struct dc_hw *hw, u8 id, struct dc_hw_display_wb *wb)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && wb) {
		memcpy(&display->wb, wb, sizeof(*wb) - sizeof(wb->dirty));
		display->wb.dirty = true;
	}

	trace_update_hw_wb_feature_en_dirty("WB", display->wb.wb_id, display->wb.enable,
					    display->wb.dirty);
}

void dc_hw_setup_display_mode(struct dc_hw *hw, u8 id, struct dc_hw_display_mode *mode)
{
	struct dc_hw_display *display = NULL;
	u8 hw_id = 0, output_id = 0;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display && mode) {
		if (!mode->enable)
			display->mode.enable = false;
		else
			memcpy(&display->mode, mode, sizeof(*mode));

		hw_id = hw->info->displays[id].id;
		output_id = display->output_id;
		hw->func->set_mode(hw, hw_id, output_id, &display->mode);
	}
}

void dc_hw_update_wb_fb(struct dc_hw *hw, u8 id, struct dc_hw_fb *fb)
{
	struct dc_hw_wb *wb = NULL;

	if (!hw || !hw->info || id >= hw->info->wb_num)
		return;

	wb = &hw->wb[id];

	if (wb && fb) {
		if (!fb->enable)
			wb->fb.enable = false;
		else
			memcpy(&wb->fb, fb, sizeof(*fb) - sizeof(fb->dirty));
		wb->fb.dirty = true;
	}

	trace_update_hw_wb_feature_en_dirty("WB_FB", wb->info->id, wb->fb.enable, wb->fb.dirty);
}

void dc_hw_setup_wb(struct dc_hw *hw, u8 id)
{
	struct dc_hw_wb *wb = NULL;
	u8 hw_id = 0;

	if (!hw || !hw->info || id >= hw->info->wb_num)
		return;

	wb = &hw->wb[id];

	if (wb) {
		hw_id = hw->info->write_back[id].id;
		hw->func->wb_commit(hw, hw_id);
	}
}

void dc_hw_enable_wb_irqs(struct dc_hw *hw, u8 wb_hw_id, bool enable)
{
	u32 i = 0;
	unsigned long flags;
	u32 config = 0;

	DPU_ATRACE_BEGIN("%s id %u, en:%u", __func__, wb_hw_id, enable);
	/* DCREG_BE_INTR_ENABLE/1 is accessed from multiple context */
	spin_lock_irqsave(&hw->be_irq_slock, flags);
	for (i = 0; i < 4; i++) {
		if (!(hw->intr_dest & BIT(i)))
			continue;

		config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1));
		switch (wb_hw_id) {
		case HW_WB_0:
			hw->wb_irq_refcnt[HW_WB_0] += (enable) ? 1 : -1;
			/* only disable IRQ when irq refcnt is 0 */
			if (!enable && hw->wb_irq_refcnt[HW_WB_0])
				break;

			config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_POST_WB0, FRM_DONE,
					      enable);
			config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_WB0, DATALOST_INTR,
					      enable);

			break;
		case HW_WB_1:
			hw->wb_irq_refcnt[HW_WB_1] += (enable) ? 1 : -1;
			/* only disable IRQ when irq refcnt is 0 */
			if (!enable && hw->wb_irq_refcnt[HW_WB_1])
				break;

			config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_POST_WB1, FRM_DONE,
					      enable);
			config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_WB1, DATALOST_INTR,
					      enable);

			break;
		case HW_BLEND_WB:
			hw->wb_irq_refcnt[HW_BLEND_WB] += (enable) ? 1 : -1;
			/* only disable IRQ when irq refcnt is 0 */
			if (!enable && hw->wb_irq_refcnt[HW_BLEND_WB])
				break;

			config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_BLD_WB, FRM_DONE,
					      enable);

			break;
		default:
			dev_err(hw->dev, "%s: Invalid wb_hw_id %u", __func__, wb_hw_id);
			break;
		}
		dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1), config);
	}
	spin_unlock_irqrestore(&hw->be_irq_slock, flags);
	DPU_ATRACE_END("%s id %u, en:%u", __func__, wb_hw_id, enable);
}

bool dc_hw_writeback_is_active(struct dc_hw *hw)
{
	int i;
	unsigned long flags;
	bool is_wb_active = false;

	spin_lock_irqsave(&hw->be_irq_slock, flags);
	for (i = 0; i < HW_WB_NUM; i++) {
		if (hw->wb_irq_refcnt[i] > 0) {
			is_wb_active = true;
			break;
		}
	}
	spin_unlock_irqrestore(&hw->be_irq_slock, flags);

	return is_wb_active;
}

void dc_hw_config_plane_status(struct dc_hw *hw, u8 id, bool config)
{
	struct dc_hw_plane *plane = NULL;

	if (!hw || !hw->info || id >= hw->info->plane_num)
		return;

	plane = &hw->plane[id];

	if (plane)
		plane->config_status = config;
}

void dc_hw_config_display_status(struct dc_hw *hw, u8 id, bool config)
{
	struct dc_hw_display *display = NULL;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	display = &hw->display[id];

	if (display)
		display->config_status = config;
}

void dc_hw_config_wb_status(struct dc_hw *hw, u8 id, bool config)
{
	struct dc_hw_wb *wb = NULL;

	if (!hw || !hw->info || id >= hw->info->wb_num)
		return;

	wb = &hw->wb[id];

	if (wb)
		wb->config_status = config;
}

#define FORCE_BE_CSR_RD_CNT 30
void dc_hw_force_be_csr_read(struct dc_hw *hw)
{
	/* see: b/459367742#comment11 */
	static const u32 read_addr = DCREG_POST_PROCESS_OUT_Address;
	int i;

	for (i = 0; i < FORCE_BE_CSR_RD_CNT; ++i)
		dc_read_immediate(hw, read_addr);
}

void dc_hw_enable_clock_domain_iso(struct dc_hw *hw, bool enable)
{
	u32 config = 0;

	config = VS_SET_FIELD(config, DCREG_CLOCK_DOMAIN_ISOLATION, DSI0_ISOLATE, enable);
	config = VS_SET_FIELD(config, DCREG_CLOCK_DOMAIN_ISOLATION, DSI1_ISOLATE, enable);
	config = VS_SET_FIELD(config, DCREG_CLOCK_DOMAIN_ISOLATION, DP0_ISOLATE, enable);
	config = VS_SET_FIELD(config, DCREG_CLOCK_DOMAIN_ISOLATION, DP1_ISOLATE, enable);

	dc_write_immediate(hw, DCREG_CLOCK_DOMAIN_ISOLATION_Address, config);
}

void dc_hw_enable_frame_irqs(struct dc_hw *hw, u8 id, bool enable)
{
	u32 config = 0, output_id = 0;
	u32 i = 0;
	unsigned long flags;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;

	/* DCREG_BE_INTR_ENABLE/1 is accessed from multiple context */
	spin_lock_irqsave(&hw->be_irq_slock, flags);
	for (i = 0; i < 4; i++) {
		if (hw->intr_dest & BIT(i)) {
			switch (output_id) {
			case HW_OUTIF_0:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH0,
						      FRM_START, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH0,
						      FRM_DONE, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE), config);
				break;
			case HW_OUTIF_1:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH1,
						      FRM_START, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH1,
						      FRM_DONE, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE), config);
				break;
			case HW_OUTIF_2:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH2,
						      FRM_START, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH2,
						      FRM_DONE, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH2,
						      UNDERRUN, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1), config);
				break;
			case HW_OUTIF_3:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH3,
						      FRM_START, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH3,
						      FRM_DONE, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH3,
						      UNDERRUN, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1), config);
				break;
			case HW_OUTIF_4:
				/* interrupts will get enabled/disabled during wb fb update */
				break;
			default:
				break;
			}
		}
	}

	trace_disp_frame_irq_enable(output_id, enable);
	trace_disp_be_intr_enabled(id, output_id, config);
	spin_unlock_irqrestore(&hw->be_irq_slock, flags);
}

void dc_hw_enable_vblank_irqs(struct dc_hw *hw, u8 id, bool enable)
{
	u32 config = 0, output_id = 0;
	struct dc_hw_display *display = NULL;
	bool set_output_start = false;
	u32 i = 0;
	unsigned long flags;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;
	display = &hw->display[id];
	set_output_start = enable && is_display_cmd_sw_trigger(display);

	/* DCREG_BE_INTR_ENABLE/1 is accessed from multiple context */
	spin_lock_irqsave(&hw->be_irq_slock, flags);
	for (i = 0; i < 4; i++) {
		if (hw->intr_dest & BIT(i)) {
			switch (output_id) {
			case HW_OUTIF_0:
				trace_disp_vblank_irq_enable(id, output_id, enable);
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE));
				// TODO(b/298512663) listen for falling edge only when needed
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH0,
						      TE_RISING_EDGE, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH0,
						      TE_FALLING_EDGE, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE), config);

				if (enable) {
					if (set_output_start) {
						dc_write(hw,
							 VS_SET_OUTPUT_FIELD(DCREG_OUTPUT,
									     output_id, Address),
							 VS_SET_FIELD(0, DCREG_OUTPUT0, WORK_MODE,
								      1));

						dc_hw_set_output_start(hw, output_id, true);
					}

					dc_write_immediate(hw,
							   VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT,
									      output_id,
									      CLK_EN_Address),
							   true);
					/* clear status to avoid false rising edge */
					dc_write_immediate(hw, DCREG_BE_INTR_STATUS_Address,
							   VS_SET_FIELD(0, DCREG_BE_INTR_STATUS,
									OUTPATH0_TE_RISING_EDGE,
									1));
				}

				trace_disp_be_intr_enabled(id, output_id, config);
				break;
			case HW_OUTIF_1:
				trace_disp_vblank_irq_enable(id, output_id, enable);
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE));
				// TODO(b/298512663) listen for falling edge only when needed
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH1,
						      TE_RISING_EDGE, enable);
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH1,
						      TE_FALLING_EDGE, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE), config);

				if (enable) {
					if (set_output_start) {
						dc_write(hw,
							 VS_SET_OUTPUT_FIELD(DCREG_OUTPUT,
									     output_id, Address),
							 VS_SET_FIELD(0, DCREG_OUTPUT0, WORK_MODE,
								      1));

						dc_hw_set_output_start(hw, output_id, true);
					}

					dc_write_immediate(hw,
							   VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT,
									      output_id,
									      CLK_EN_Address),
							   true);
					/* clear status to avoid false rising edge */
					dc_write_immediate(hw, DCREG_BE_INTR_STATUS_Address,
							   VS_SET_FIELD(0, DCREG_BE_INTR_STATUS,
									OUTPATH1_TE_RISING_EDGE,
									1));
				}

				trace_disp_be_intr_enabled(id, output_id, config);
				break;
			case HW_OUTIF_2:
			case HW_OUTIF_3:
			case HW_OUTIF_4:
				break;
			default:
				break;
			}
		}
	}

	spin_unlock_irqrestore(&hw->be_irq_slock, flags);
}

u32 dc_hw_get_vblank_count(struct dc_hw *hw, u8 id)
{
	return hw->display[id].vblank_count;
}

void dc_hw_enable_underrun_interrupt(struct dc_hw *hw, u8 id, bool enable)
{
	u32 config = 0;
	u32 output_id = 0;
	u32 i = 0;
	unsigned long flags;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;

	/* DCREG_BE_INTR_ENABLE/1 is access from multiple context */
	spin_lock_irqsave(&hw->be_irq_slock, flags);
	for (i = 0; i < 4; i++) {
		if (hw->intr_dest & BIT(i)) {
			switch (output_id) {
			case HW_OUTIF_0:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH0,
						      UNDERRUN, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE), config);
				break;
			case HW_OUTIF_1:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE_OUTPATH1,
						      UNDERRUN, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE), config);
				break;
			case HW_OUTIF_2:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH2,
						      UNDERRUN, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1), config);
				break;
			case HW_OUTIF_3:
				config = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1));
				config = VS_SET_FIELD(config, DCREG_BE_INTR_ENABLE1_OUTPATH3,
						      UNDERRUN, enable);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, ENABLE1), config);
				break;
			default:
				break;
			}
		}
	}

	trace_disp_be_intr_enabled(id, output_id, config);
	spin_unlock_irqrestore(&hw->be_irq_slock, flags);
}

void dc_hw_clear_underrun_interrupt(struct dc_hw *hw, u8 id)
{
	u32 config = 0, i = 0;
	u8 output_id = 0;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;

	for (i = 0; i < 4; i++) {
		if (hw->intr_dest & BIT(i)) {
			switch (output_id) {
			case HW_OUTIF_0:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS,
						      OUTPATH0_UNDERRUN, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS), config);
				break;
			case HW_OUTIF_1:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS,
						      OUTPATH1_UNDERRUN, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS), config);
				break;
			case HW_OUTIF_2:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS1,
						      OUTPATH2_UNDERRUN, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS1), config);
				break;
			case HW_OUTIF_3:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS1,
						      OUTPATH3_UNDERRUN, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS1), config);
				break;
			default:
				break;
			}
		}
	}
}

static void dc_hw_clear_start_interrupt(struct dc_hw *hw, u8 id)
{
	u32 config = 0, i = 0;
	u8 output_id = 0;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;

	for (i = 0; i < 4; i++) {
		if (hw->intr_dest & BIT(i)) {
			switch (output_id) {
			case HW_OUTIF_0:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS,
						      OUTPATH0_FRM_START, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS), config);
				break;
			case HW_OUTIF_1:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS,
						      OUTPATH1_FRM_START, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS), config);
				break;
			case HW_OUTIF_2:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS1,
						      OUTPATH2_FRM_START, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS1), config);
				break;
			case HW_OUTIF_3:
				config = VS_SET_FIELD(config, DCREG_BE_INTR_STATUS1,
						      OUTPATH3_FRM_START, 1);
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS1), config);
				break;
			}
		}
	}
}

void dc_hw_get_fe0_interrupt(struct dc_hw *hw, struct dc_hw_interrupt_status *status)
{
	u32 fe0_status = 0;

	fe0_status = dc_read_immediate(hw, DCREG_FE0_INTR_STATUS_Address);
	if (fe0_status) {
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, SW_RST_DONE))
			status->reset_status[FE0_SW_RESET] = 1;

		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER_FRM_DONE0))
			status->layer_frm_done |= BIT(HW_PLANE_0);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER_FRM_DONE1))
			status->layer_frm_done |= BIT(HW_PLANE_1);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER_FRM_DONE2))
			status->layer_frm_done |= BIT(HW_PLANE_2);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER_FRM_DONE3))
			status->layer_frm_done |= BIT(HW_PLANE_3);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER_FRM_DONE4))
			status->layer_frm_done |= BIT(HW_PLANE_4);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER_FRM_DONE5))
			status->layer_frm_done |= BIT(HW_PLANE_5);

		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, PVRIC_DECODE_ERROR0))
			status->pvric_decode_err |= BIT(HW_PLANE_0);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, PVRIC_DECODE_ERROR1))
			status->pvric_decode_err |= BIT(HW_PLANE_1);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, PVRIC_DECODE_ERROR2))
			status->pvric_decode_err |= BIT(HW_PLANE_2);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, PVRIC_DECODE_ERROR3))
			status->pvric_decode_err |= BIT(HW_PLANE_3);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, PVRIC_DECODE_ERROR4))
			status->pvric_decode_err |= BIT(HW_PLANE_4);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, PVRIC_DECODE_ERROR5))
			status->pvric_decode_err |= BIT(HW_PLANE_5);

		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, AXI_HANG0))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_HANG0, status->fe0_bus_errors);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, AXI_HANG1))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_HANG1, status->fe0_bus_errors);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, AXI_BUS_ERROR0))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_BUS_ERROR0, status->fe0_bus_errors);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, AXI_BUS_ERROR1))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_BUS_ERROR1, status->fe0_bus_errors);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, APB_HANG))
			set_bit(DC_HW_FE_BUS_ERROR_APB_HANG, status->fe0_bus_errors);

		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER0_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_0);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER1_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_1);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER2_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_2);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER3_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_3);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER4_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_4);
		if (VS_GET_FIELD(fe0_status, DCREG_FE0_INTR_STATUS, LAYER5_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_5);

		dc_write_immediate(hw, DCREG_FE0_INTR_STATUS_Address, fe0_status);
	}
}

void dc_hw_get_fe1_interrupt(struct dc_hw *hw, struct dc_hw_interrupt_status *status)
{
	u32 fe1_status = 0;

	fe1_status = dc_read_immediate(hw, DCREG_FE1_INTR_STATUS_Address);
	if (fe1_status) {
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, SW_RST_DONE))
			status->reset_status[FE1_SW_RESET] = 1;

		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER_FRM_DONE0))
			status->layer_frm_done |= BIT(HW_PLANE_8);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER_FRM_DONE1))
			status->layer_frm_done |= BIT(HW_PLANE_9);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER_FRM_DONE2))
			status->layer_frm_done |= BIT(HW_PLANE_10);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER_FRM_DONE3))
			status->layer_frm_done |= BIT(HW_PLANE_11);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER_FRM_DONE4))
			status->layer_frm_done |= BIT(HW_PLANE_12);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER_FRM_DONE5))
			status->layer_frm_done |= BIT(HW_PLANE_13);

		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, PVRIC_DECODE_ERROR0))
			status->pvric_decode_err |= BIT(HW_PLANE_8);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, PVRIC_DECODE_ERROR1))
			status->pvric_decode_err |= BIT(HW_PLANE_9);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, PVRIC_DECODE_ERROR2))
			status->pvric_decode_err |= BIT(HW_PLANE_10);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, PVRIC_DECODE_ERROR3))
			status->pvric_decode_err |= BIT(HW_PLANE_11);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, PVRIC_DECODE_ERROR4))
			status->pvric_decode_err |= BIT(HW_PLANE_12);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, PVRIC_DECODE_ERROR5))
			status->pvric_decode_err |= BIT(HW_PLANE_13);

		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, AXI_HANG0))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_HANG0, status->fe1_bus_errors);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, AXI_HANG1))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_HANG1, status->fe1_bus_errors);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, AXI_BUS_ERROR0))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_BUS_ERROR0, status->fe1_bus_errors);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, AXI_BUS_ERROR1))
			set_bit(DC_HW_FE_BUS_ERROR_AXI_BUS_ERROR1, status->fe1_bus_errors);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, APB_HANG))
			set_bit(DC_HW_FE_BUS_ERROR_APB_HANG, status->fe1_bus_errors);

		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER8_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_8);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER9_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_9);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER10_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_10);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER11_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_11);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER12_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_12);
		if (VS_GET_FIELD(fe1_status, DCREG_FE1_INTR_STATUS, LAYER13_PANEL_RST_DONE))
			status->layer_reset_done |= BIT(HW_PLANE_13);

		dc_write_immediate(hw, DCREG_FE1_INTR_STATUS_Address, fe1_status);
	}
}

void dc_hw_get_be_interrupt(struct dc_hw *hw, struct dc_hw_interrupt_status *status)
{
	u32 be_status0 = 0, be_status1 = 0;
	u32 be_overflow0 = 0, be_overflow1 = 0;
	u32 i = 0;

	for (i = 0; i < 4; i++) {
		if (hw->intr_dest & BIT(i)) {
			be_status0 = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS));
			if (be_status0)
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS), be_status0);
			be_overflow0 = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, OVERFLOW));
			if (be_overflow0)
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, OVERFLOW),
						   be_overflow0);

			be_status1 = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS1));
			if (be_status1)
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, STATUS1),
						   be_status1);
			be_overflow1 = dc_read_immediate(hw, VS_SET_INTR_ADDR(BE, i, OVERFLOW1));
			if (be_overflow1)
				dc_write_immediate(hw, VS_SET_INTR_ADDR(BE, i, OVERFLOW1),
						   be_overflow1);

			/* stop at first interrupt destination */
			break;
		}
	}

	if (be_status0) {
		/* frame and TE irqs are based on output interface id */
		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH0_FRM_START))
			status->output_frm_start |= BIT(HW_OUTIF_0);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH0_FRM_START))
			status->of_output_frm_start |= BIT(HW_OUTIF_0);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH1_FRM_START))
			status->output_frm_start |= BIT(HW_OUTIF_1);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH1_FRM_START))
			status->of_output_frm_start |= BIT(HW_OUTIF_1);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH0_FRM_DONE))
			status->output_frm_done |= BIT(HW_OUTIF_0);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH0_FRM_DONE))
			status->of_output_frm_done |= BIT(HW_OUTIF_0);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH1_FRM_DONE))
			status->output_frm_done |= BIT(HW_OUTIF_1);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH1_FRM_DONE))
			status->of_output_frm_done |= BIT(HW_OUTIF_1);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH0_UNDERRUN))
			status->output_underrun |= BIT(HW_OUTIF_0);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH0_UNDERRUN))
			status->of_output_underrun |= BIT(HW_OUTIF_0);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH1_UNDERRUN))
			status->output_underrun |= BIT(HW_OUTIF_1);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH1_UNDERRUN))
			status->of_output_underrun |= BIT(HW_OUTIF_1);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH0_TE_RISING_EDGE))
			status->output_te_rising |= BIT(HW_OUTIF_0);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH0_TE_RISING_EDGE))
			status->of_output_te_rising |= BIT(HW_OUTIF_0);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH1_TE_RISING_EDGE))
			status->output_te_rising |= BIT(HW_OUTIF_1);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH1_TE_RISING_EDGE))
			status->of_output_te_rising |= BIT(HW_OUTIF_1);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH0_TE_FALLING_EDGE))
			status->output_te_falling |= BIT(HW_OUTIF_0);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH0_TE_FALLING_EDGE))
			status->of_output_te_falling |= BIT(HW_OUTIF_0);

		if (VS_GET_FIELD(be_status0, DCREG_BE_INTR_STATUS, OUTPATH1_TE_FALLING_EDGE))
			status->output_te_falling |= BIT(HW_OUTIF_1);
		if (VS_GET_FIELD(be_overflow0, DCREG_BE_INTR_OVERFLOW, OUTPATH1_TE_FALLING_EDGE))
			status->of_output_te_falling |= BIT(HW_OUTIF_1);
	}

	if (be_status1) {
		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, SW_RST_DONE))
			status->reset_status[BE_SW_RESET] = 1;

		/* frame and TE irqs are based on output interface id */
		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, OUTPATH2_FRM_START))
			status->output_frm_start |= BIT(HW_OUTIF_2);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, OUTPATH2_FRM_START))
			status->of_output_frm_start |= BIT(HW_OUTIF_2);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, OUTPATH3_FRM_START))
			status->output_frm_start |= BIT(HW_OUTIF_3);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, OUTPATH3_FRM_START))
			status->of_output_frm_start |= BIT(HW_OUTIF_3);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, OUTPATH2_FRM_DONE))
			status->output_frm_done |= BIT(HW_OUTIF_2);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, OUTPATH2_FRM_DONE))
			status->of_output_frm_done |= BIT(HW_OUTIF_2);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, OUTPATH3_FRM_DONE))
			status->output_frm_done |= BIT(HW_OUTIF_3);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, OUTPATH3_FRM_DONE))
			status->of_output_frm_done |= BIT(HW_OUTIF_3);

		/* blender WB uses an artificial output frame done */
		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, BLD_WB_FRM_DONE)) {
			status->output_frm_done |= BIT(HW_OUTIF_4);
			status->wb_frm_done |= BIT(HW_BLEND_WB);
			hw->wb[HW_BLEND_WB].wb_frm_done = true;
		}
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, BLD_WB_FRM_DONE)) {
			status->of_output_frm_done |= BIT(HW_OUTIF_4);
			status->of_wb_frm_done |= BIT(HW_BLEND_WB);
		}

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, OUTPATH2_UNDERRUN))
			status->output_underrun |= BIT(HW_OUTIF_2);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, OUTPATH2_UNDERRUN))
			status->of_output_underrun |= BIT(HW_OUTIF_2);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, OUTPATH3_UNDERRUN))
			status->output_underrun |= BIT(HW_OUTIF_3);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, OUTPATH3_UNDERRUN))
			status->of_output_underrun |= BIT(HW_OUTIF_3);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, POST_WB0_FRM_DONE))
		{
			status->wb_frm_done |= BIT(HW_WB_0);
			hw->wb[HW_WB_0].wb_frm_done = true;
		}
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, POST_WB0_FRM_DONE))
			status->of_wb_frm_done |= BIT(HW_WB_0);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, POST_WB1_FRM_DONE))
		{
			status->wb_frm_done |= BIT(HW_WB_1);
			hw->wb[HW_WB_1].wb_frm_done = true;
		}
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, POST_WB1_FRM_DONE))
			status->of_wb_frm_done |= BIT(HW_WB_1);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, WB0_DATALOST_INTR))
			status->wb_datalost |= BIT(HW_WB_0);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, WB0_DATALOST_INTR))
			status->of_wb_datalost |= BIT(HW_WB_0);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, WB1_DATALOST_INTR))
			status->wb_datalost |= BIT(HW_WB_1);
		if (VS_GET_FIELD(be_overflow1, DCREG_BE_INTR_OVERFLOW1, WB1_DATALOST_INTR))
			status->of_wb_datalost |= BIT(HW_WB_1);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, APB_HANG))
			set_bit(DC_HW_BE_BUS_ERROR_APB_HANG, status->be_bus_errors);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, AXI_BUS_ERROR))
			set_bit(DC_HW_BE_BUS_ERROR_AXI_BUS_ERROR, status->be_bus_errors);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, AXI_RD_BUS_HANG))
			set_bit(DC_HW_BE_BUS_ERROR_AXI_RD_BUS_HANG, status->be_bus_errors);

		if (VS_GET_FIELD(be_status1, DCREG_BE_INTR_STATUS1, AXI_WR_BUS_HANG))
			set_bit(DC_HW_BE_BUS_ERROR_AXI_WR_BUS_HANG, status->be_bus_errors);
	}
}

int dc_hw_get_interrupt(struct dc_hw *hw, struct dc_hw_interrupt_status *status)
{
	dc_hw_get_fe0_interrupt(hw, status);
	dc_hw_get_fe1_interrupt(hw, status);
	dc_hw_get_be_interrupt(hw, status);

	return 0;
}

int dc_hw_clear_be_interrupt_overflows(struct dc_hw *hw)
{
	dc_write_immediate(hw, DCREG_BE_INTR_OVERFLOW_Address, DCREG_BE_INTR_OVERFLOW_WriteMask);
	dc_write_immediate(hw, DCREG_BE_INTR_OVERFLOW1_Address, DCREG_BE_INTR_OVERFLOW1_WriteMask);

	return 0;
}

int dc_hw_clear_be_interrupt_statuses(struct dc_hw *hw)
{
	dc_write_immediate(hw, DCREG_BE_INTR_STATUS_Address, DCREG_BE_INTR_STATUS_WriteMask);
	dc_write_immediate(hw, DCREG_BE_INTR_STATUS1_Address, DCREG_BE_INTR_STATUS1_WriteMask);

	return 0;
}

int dc_hw_disable_all_be_interrupts(struct dc_hw *hw)
{
	unsigned long flags;
	u32 prev_intr_en_val, prev_intr_en1_val;

	/* DCREG_BE_INTR_ENABLE/1 is accessible from multiple contexts */
	spin_lock_irqsave(&hw->be_irq_slock, flags);

	/* Flag BE interrupts that have not been previously disabled */
	prev_intr_en_val = dc_read_immediate(hw, DCREG_BE_INTR_ENABLE_Address);
	if (prev_intr_en_val & DCREG_BE_INTR_ENABLE_WriteMask)
		dev_warn(hw->dev, "BE Interrupts still enabled during suspend; INTR_EN %#x\n",
			 prev_intr_en_val);

	prev_intr_en1_val = dc_read_immediate(hw, DCREG_BE_INTR_ENABLE1_Address);
	if (prev_intr_en1_val & DCREG_BE_INTR_ENABLE1_WriteMask)
		dev_warn(hw->dev, "BE Interrupts still enabled during suspend; INTR_EN1 %#x\n",
			 prev_intr_en1_val);

	/* Disable all BE interrupts */
	dc_write_immediate(hw, DCREG_BE_INTR_ENABLE_Address, DCREG_BE_INTR_ENABLE_ResetValue);
	dc_write_immediate(hw, DCREG_BE_INTR_ENABLE1_Address, DCREG_BE_INTR_ENABLE1_ResetValue);

	/* Disable clocks associated with vblank interrupts */
	dc_write_immediate(hw, DCREG_SH_OUTPUT0_CLK_EN_Address, 0);
	dc_write_immediate(hw, DCREG_SH_OUTPUT1_CLK_EN_Address, 0);

	trace_disp_be_intr_disabled(-1, -1, 0x0);
	spin_unlock_irqrestore(&hw->be_irq_slock, flags);

	return 0;
}

int dc_hw_reset_all_be_interrupts(struct dc_hw *hw)
{
	dc_hw_disable_all_be_interrupts(hw);
	dc_hw_clear_be_interrupt_statuses(hw);
	dc_hw_clear_be_interrupt_overflows(hw);

	return 0;
}

int dc_hw_enable_fe_interrupts(struct dc_hw *hw)
{
	dc_write_immediate(hw, DCREG_FE0_INTR_ENABLE_Address, DCREG_FE0_INTR_ENABLE_WriteMask);
	dc_write_immediate(hw, DCREG_FE1_INTR_ENABLE_Address, DCREG_FE1_INTR_ENABLE_WriteMask);

	return 0;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
void dc_hw_set_plane_pattern(struct dc_hw *hw, u8 hw_id, struct dc_hw_pattern *pattern)
{
	u32 offset = _get_plane_tp_offset(hw_id);

	dc_write(hw, DCREG_SH_LAYER0_TP_ENABLE_Address + offset, pattern->enable);

	if (!pattern->enable)
		return;

	dc_write(hw, DCREG_SH_LAYER0_TP_MODE_Address + offset, pattern->mode);

	switch (pattern->mode) {
	case VS_PURE_COLOR:
	case VS_BORDER_PATRN:
	case VS_CURSOR_PATRN:
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_ALPHA_Address + offset, pattern->color >> 48);
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_RED_Address + offset,
			 0xFFFF & (pattern->color >> 32));
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_GREEN_Address + offset,
			 0xFFFF & (pattern->color >> 16));
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_BLUE_Address + offset,
			 0xFFFF & pattern->color);

		if (pattern->mode == VS_CURSOR_PATRN)
			dc_write(hw, DCREG_SH_LAYER0_TP_CURSOR_COORD_Address + offset,
				 pattern->rect.x | pattern->rect.y << 16);
		break;
	case VS_COLOR_BAR_H:
	case VS_RMAP_H:
	case VS_BLACK_WHITE_H:
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG, HEIGHT,
				      pattern->rect.h));

		if (pattern->mode == VS_RMAP_H)
			dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_RAMP_STEP_Address + offset,
				 pattern->ramp_step);
		break;
	case VS_COLOR_BAR_V:
	case VS_RMAP_V:
	case VS_BLACK_WHITE_V:
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG, WIDTH,
				      pattern->rect.w));

		if (pattern->mode == VS_RMAP_V)
			dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_RAMP_STEP_Address + offset,
				 pattern->ramp_step);
		break;
	case VS_BLACK_WHITE_SQR:
		dc_write(hw, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG, HEIGHT,
				      pattern->rect.h) |
				 VS_SET_FIELD(0, DCREG_SH_LAYER0_TP_COLOR_BAR_CONFIG, WIDTH,
					      pattern->rect.h));
		break;
	default:
		break;
	}
}

void dc_hw_clear_display_pattern(struct dc_hw *hw, u8 id, struct dc_hw_pattern *pattern)
{
	u32 hw_id = hw->info->displays[id].id;
	u32 output_id = hw->display[id].output_id;
	u32 offset = _get_display_tp_offset(hw_id, output_id, pattern->pos);

	dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_ENABLE_Address + offset, 0);
}

void dc_hw_set_display_pattern(struct dc_hw *hw, u8 id, struct dc_hw_pattern *pattern)
{
	u32 hw_id = hw->info->displays[id].id;
	u32 output_id = hw->display[id].output_id;
	u32 offset = _get_display_tp_offset(hw_id, output_id, pattern->pos);

	dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_ENABLE_Address + offset, pattern->enable);

	if (!pattern->enable)
		return;

	dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_MODE_Address + offset, pattern->mode);

	switch (pattern->mode) {
	case VS_PURE_COLOR:
	case VS_BORDER_PATRN:
	case VS_CURSOR_PATRN:
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_ALPHA_Address + offset,
			 pattern->color >> 48);
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_RED_Address + offset,
			 0xFFFF & (pattern->color >> 32));
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_GREEN_Address + offset,
			 0xFFFF & (pattern->color >> 16));
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_BLUE_Address + offset,
			 0xFFFF & pattern->color);

		if (pattern->mode == VS_CURSOR_PATRN)
			dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_CURSOR_COORD_Address + offset,
				 pattern->rect.x | pattern->rect.y << 16);

		break;
	case VS_COLOR_BAR_H:
	case VS_RMAP_H:
	case VS_BLACK_WHITE_H:
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG, HEIGHT,
				      pattern->rect.h));

		if (pattern->mode == VS_RMAP_H)
			dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_RAMP_STEP_Address + offset,
				 pattern->ramp_step);
		break;
	case VS_COLOR_BAR_V:
	case VS_RMAP_V:
	case VS_BLACK_WHITE_V:
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG, WIDTH,
				      pattern->rect.w));

		if (pattern->mode == VS_RMAP_V)
			dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_RAMP_STEP_Address + offset,
				 pattern->ramp_step);
		break;
	case VS_BLACK_WHITE_SQR:
		dc_write(hw, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG, HEIGHT,
				      pattern->rect.h) |
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_BLEND_TP_COLOR_BAR_CONFIG, WIDTH,
					      pattern->rect.h));
		break;
	default:
		break;
	}
}

void dc_hw_get_fe_qos_value(struct dc_hw_plane_qos *qos)
{
	u8 i;

	for (i = 0; i < MAX_QOS_LEVLE; i++) {
		qos->fe0_qos_value[i] = fe0_qos_value[i];
		qos->fe1_qos_value[i] = fe1_qos_value[i];
	}
}

void dc_hw_get_be_qos_value(struct dc_hw_disp_qos *qos)
{
	u8 i;

	for (i = 0; i < MAX_QOS_LEVLE; i++)
		qos->qos_value[i] = be_qos_value[i];
}

#endif /* CONFIG_DEBUG_FS */

void dc_hw_get_crtc_scanout_position(struct dc_hw *hw, u8 display_id, u32 *position)
{
	u8 output_id = hw->display[display_id].output_id;
	*position =
		dc_read(hw, VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, CURRENT_COORD_Address));
}

static void _dc_hw_ltm_enable_shadow_register(struct dc_hw *hw, u8 display_id, bool enable)
{
	u32 config = 0, hw_id = 0;
	bool ltm_enable = false, gtm_enable = false;

	hw_id = hw->info->displays[display_id].id;

	if (hw->display[display_id].info->ltm)
		vs_dc_hw_get_display_property(hw, hw_id, "LTM_HISTOGRAM_CONFIG", &ltm_enable);
	if (hw->display[display_id].info->gtm)
		vs_dc_hw_get_display_property(hw, hw_id, "GTM_LUMA_AVE_CONFIG", &gtm_enable);

	if (ltm_enable || gtm_enable) {
		trace_hw_ltm_reg_switch(hw_id, enable);
		config = dc_read_immediate(hw, VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id,
								    LTM_CONFIG_Address));
		dc_write_immediate(hw, VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id, LTM_CONFIG_Address),
				   VS_SET_FIELD(config, DCREG_PANEL0_LTM_CONFIG, REG_SWITCH,
						enable));
	}
}

void dc_hw_enable_shadow_register(struct dc_hw *hw, u8 display_id, bool enable)
{
	u32 i, hw_id = 0;
	u32 config = 0;

	/* for layer */
	for (i = 0; i < hw->info->layer_num; i++) {
		if (!hw->plane[i].config_status || (hw->plane[i].fb.display_id != display_id))
			continue;

		hw_id = dc_hw_get_plane_id(i, hw);
		trace_hw_layer_reg_switch(hw_id, enable);

		config = dc_read_immediate(hw, VS_SET_FE_FIELD(DCREG_LAYER, hw_id, CONFIG_Address));
		dc_write_immediate(hw, VS_SET_FE_FIELD(DCREG_LAYER, hw_id, CONFIG_Address),
				   VS_SET_FIELD(config, DCREG_LAYER0_CONFIG, REG_SWITCH, enable));
	}

	/* for display & interface */
	for (i = 0; i < hw->info->display_num; i++) {
		if (!hw->display[i].config_status || (hw->info->displays[i].id != display_id))
			continue;

		hw_id = hw->info->displays[i].id;
		if (hw_id >= HW_DISPLAY_4)
			continue;

		trace_hw_panel_reg_switch(hw_id, enable);
		config = dc_read_immediate(hw,
					   VS_SET_PANEL_FIELD(DCREG_PANEL, hw_id, CONFIG_Address));
		dc_write_immediate(hw, VS_SET_PANEL_FIELD(DCREG_PANEL, hw_id, CONFIG_Address),
				   VS_SET_FIELD(config, DCREG_PANEL0_CONFIG, REG_SWITCH, enable));

		/* LTM/GTM shadow register switch */
		_dc_hw_ltm_enable_shadow_register(hw, i, enable);

		trace_hw_display_reg_switch(hw->display[i].output_id, enable);
		config = dc_read_immediate(hw, VS_SET_PANEL_FIELD(DCREG_OUTPUT,
								  hw->display[i].output_id,
								  CONFIG_Address));
		dc_write_immediate(hw,
				   VS_SET_PANEL_FIELD(DCREG_OUTPUT, hw->display[i].output_id,
						      CONFIG_Address),
				   VS_SET_FIELD(config, DCREG_OUTPUT0_CONFIG, REG_SWITCH, enable));
	}

	/* avoid wb programming when secure is enabled */
	if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM))
		return;

	/* for writeback interfaces */
	for (i = 0; i < hw->info->wb_num; i++) {
		if (!hw->wb[i].config_status || hw->wb[i].fb.display_id != display_id)
			continue;

		hw_id = hw->info->write_back[i].id;
		trace_hw_wb_reg_switch(hw_id, enable);
		hw->sub_func->wb_reg_switch(hw, hw_id, enable);
	}
}

void dc_hw_sw_sof_trigger(struct dc_hw *hw, u8 output_id, bool trig_enable)
{
	dc_write_immediate(hw, VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id, SW_CONFIG_Address),
			   trig_enable);
}

static u32 set_panel_output_mux(u32 mux_value, u8 src_panel, u8 output_id)
{
	u32 config = mux_value;

	switch (output_id) {
	case HW_OUTIF_0:
		config = VS_SET_FIELD(config, DCREG_POST_PROCESS_OUT, MUX_OUT0_SEL, src_panel);
		break;
	case HW_OUTIF_1:
		config = VS_SET_FIELD(config, DCREG_POST_PROCESS_OUT, MUX_OUT1_SEL, src_panel);
		break;
	case HW_OUTIF_2:
		config = VS_SET_FIELD(config, DCREG_POST_PROCESS_OUT, MUX_OUT2_SEL, src_panel);
		break;
	case HW_OUTIF_3:
		config = VS_SET_FIELD(config, DCREG_POST_PROCESS_OUT, MUX_OUT3_SEL, src_panel);
		break;
	default:
		break;
	}

	return config;
}

static u8 get_panel_output_mux(u32 mux_value, u8 output_id)
{
	switch (output_id) {
	case HW_OUTIF_0:
		return VS_GET_FIELD(mux_value, DCREG_POST_PROCESS_OUT, MUX_OUT0_SEL);
	case HW_OUTIF_1:
		return VS_GET_FIELD(mux_value, DCREG_POST_PROCESS_OUT, MUX_OUT1_SEL);
	case HW_OUTIF_2:
		return VS_GET_FIELD(mux_value, DCREG_POST_PROCESS_OUT, MUX_OUT2_SEL);
	case HW_OUTIF_3:
		return VS_GET_FIELD(mux_value, DCREG_POST_PROCESS_OUT, MUX_OUT3_SEL);
	default:
		return U8_MAX; /* invalid */
	}
}

static u32 panel_output_mux(u32 mux_value, u8 src_panel, u8 output_id)
{
	u32 config = mux_value;
	u8 replaced_panel;
	u8 i;

	replaced_panel = get_panel_output_mux(config, output_id);
	if (replaced_panel == src_panel)
		return config;

	/* swap src_panel and replaced_panel to avoid two outputs having the same source
	 * to ensure src_panel disconnects from the old output which should be inactive
	 */
	for (i = 0; i < 4; ++i) {
		if (i == output_id) {
			config = set_panel_output_mux(config, src_panel, i);
		} else {
			u8 current_panel = get_panel_output_mux(config, i);

			if (current_panel == src_panel)
				config = set_panel_output_mux(config, replaced_panel, i);
		}
	}

	return config;
}

static void spliter_trigger(struct dc_hw *hw, u8 src_panel, u8 output_id, bool trig_enable)
{
	u32 config = 0;
	unsigned long flags;

	if (src_panel > HW_DISPLAY_0) {
		spin_lock_irqsave(&hw->output_mux_slock, flags);
		config = hw->output_mux_value;
		config = panel_output_mux(config, src_panel - 1,
					  hw->display[src_panel - 1].output_id);
		config = panel_output_mux(config, src_panel, output_id);
		dc_write_immediate(hw, DCREG_POST_PROCESS_OUT_Address, config);
		trace_disp_output_mux_sel(src_panel, output_id, config);
		hw->output_mux_value = config;
		spin_unlock_irqrestore(&hw->output_mux_slock, flags);
		dc_write_immediate(hw,
				   VS_SET_PANEL_FIELD(DCREG_OUTPUT,
						      hw->display[src_panel - 1].output_id,
						      START_Address),
				   trig_enable);
		dc_write_immediate(hw, VS_SET_PANEL_FIELD(DCREG_OUTPUT, output_id, START_Address),
				   trig_enable);
		return;
	}
}

void online_trigger(struct dc_hw *hw, u8 src_panel, u8 output_id, bool trig_enable)
{
	u32 config = 0;
	bool spliter_enable = false;
	bool src_from_spliter = false;
	unsigned long flags;

	if (hw->display[src_panel].info->spliter)
		vs_dc_hw_get_display_property(hw, src_panel, "SPLITER", &spliter_enable);

	if (src_panel > HW_DISPLAY_0) {
		if (hw->display[src_panel - 1].info->spliter)
			vs_dc_hw_get_display_property(hw, src_panel - 1, "SPLITER",
						      &src_from_spliter);
	}

	if (spliter_enable)
		return;

	if (src_from_spliter) {
		/* For one panel split to two output trigger. */
		spliter_trigger(hw, src_panel, output_id, trig_enable);
		return;
	}

	if (src_panel < HW_DISPLAY_4) {
		/* post process output FIFO mux configuration */
		src_panel = src_panel > HW_DISPLAY_0 ? src_panel + 1 : src_panel;

		spin_lock_irqsave(&hw->output_mux_slock, flags);
		config = hw->output_mux_value;
		config = panel_output_mux(config, src_panel, output_id);
		dc_write_immediate(hw, DCREG_POST_PROCESS_OUT_Address, config);
		trace_disp_output_mux_sel(src_panel, output_id, config);
		hw->output_mux_value = config;
		spin_unlock_irqrestore(&hw->output_mux_slock, flags);
		dc_write_immediate(hw, VS_SET_PANEL_FIELD(DCREG_OUTPUT, output_id, START_Address),
				   trig_enable);
	}
}

void link_node_online_trigger(struct dc_hw *hw, u8 src_panel, bool trig_enable, bool wb_enable,
			      u32 wb_point, u32 intf_id)
{
	u32 link_node_config = 0;
	u32 config = 0;
	u32 link_node_id = hw->display[src_panel].sbs_split_dirty ? ((src_panel >> 1) ? 2 : 0) :
								    src_panel;

	if (src_panel < HW_DISPLAY_4) {
		if (trig_enable) {
			config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, src_panel,
								CONFIG_Address));

			if (wb_enable && wb_point == VS_WB_DISP_IN)
				dc_write(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, src_panel,
							    CONFIG_Address),
					 VS_SET_FIELD(config, DCREG_SH_PANEL0, CONFIG_OUTPUT_PATH,
						      false));
			else
				dc_write(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, src_panel,
							    CONFIG_Address),
					 VS_SET_FIELD(config, DCREG_SH_PANEL0, CONFIG_OUTPUT_PATH,
						      true));

			/*cannot support both postprocess writeback and display*/
			if (wb_enable && wb_point == VS_WB_DISP_OUT)
				dc_write_immediate(hw,
						   VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, intf_id,
								       CROSSBAR4_TO4_Address),
						   VS_SET_FIELD(0x0, DCREG_OUTPUT0,
								CROSSBAR4_TO4_MUX_OUT, src_panel) |
							   VS_SET_FIELD(0x0, DCREG_OUTPUT0,
									CROSSBAR4_TO4_PATH, false));
			else
				dc_write_immediate(hw,
						   VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, intf_id,
								       CROSSBAR4_TO4_Address),
						   VS_SET_FIELD(0x0, DCREG_OUTPUT0,
								CROSSBAR4_TO4_MUX_OUT, src_panel) |
							   VS_SET_FIELD(0x0, DCREG_OUTPUT0,
									CROSSBAR4_TO4_PATH, true));
		}

		vs_dpu_link_node_config(hw, VS_DPU_LINK_POST, src_panel, link_node_id, trig_enable);
		/* linkNode DBUFFER and TM would connet while writeback disabled. */
		if (!wb_enable) {
			vs_dpu_link_node_config(hw, VS_DPU_LINK_DBUFFER, intf_id, link_node_id,
						trig_enable);
			vs_dpu_link_node_config(hw, VS_DPU_LINK_TM, intf_id, link_node_id,
						trig_enable);
		}
	}

	link_node_config = vs_dpu_link_node_config_get(hw, link_node_id);

	/* while side by side split enable, should trigger after each display have set up. */
	if (!hw->display[src_panel + 1].sbs_split_dirty) {
		dc_write(hw,
			 VS_SET_LINKNODE_FIELD(DCREG_SH_LINK_NODE, link_node_id, RESOURCE_Address),
			 link_node_config);
		dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_LINK_NODE, link_node_id, CONFIG_Address),
			 0x7);
		dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_LINK_NODE, link_node_id, SW_DONE_Address),
			 0x1);
		dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_LINK_NODE, link_node_id, START_Address),
			 0x1);
	}
}

void link_node_offline_trigger(struct dc_hw *hw, u8 src_panel, bool trig_enable)
{
	u32 link_node_config = 0;
	u32 link_node_id = src_panel;

	/* At most 4 linknodes */
	if (link_node_id > 4)
		link_node_id = 4;

	/* Offline trigger DBUFFER no need to link. */
	if (src_panel < HW_DISPLAY_4)
		vs_dpu_link_node_config(hw, VS_DPU_LINK_POST, src_panel, link_node_id,
					trig_enable);
	link_node_config = vs_dpu_link_node_config_get(hw, link_node_id);

	dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_SH_LINK_NODE, link_node_id, RESOURCE_Address),
		 link_node_config);
	dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_LINK_NODE, link_node_id, CONFIG_Address), 0x7);
	dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_LINK_NODE, link_node_id, SW_DONE_Address), 0x1);
	dc_write(hw, VS_SET_LINKNODE_FIELD(DCREG_LINK_NODE, link_node_id, START_Address), 0x1);
}

static bool dp_sync_trigger(struct dc_hw *hw, u8 display_id)
{
	u32 dp_sync_config = 0;
	u8 dp_sync_enable = 0, display_id_ex;

	dp_sync_config = dc_read_immediate(hw, DCREG_OUTPUT_SYNC_CONFIG_Address);
	dp_sync_enable = VS_GET_FIELD(dp_sync_config, DCREG_OUTPUT_SYNC_CONFIG, ENABLE);

	if (dp_sync_enable & BIT(display_id)) {
		display_id_ex = FIND_HIGHEST_BIT(dp_sync_enable);
		if (display_id_ex == display_id) {
			/* start all synced DP interface in first frame */
			dc_write_immediate(hw, DCREG_OUTPUT_SYNC_START_Address,
					   VS_SET_FIELD_PREDEF(0, DCREG_OUTPUT_SYNC_START, VALUE,
							       START));
		}
		return true;
	}

	return false;
}

static bool ofifo_splice_trigger(struct dc_hw *hw, u8 display_id, struct drm_crtc *crtc)
{
	u8 splice_mode0 = 0, splice_mode1 = 0;
	u32 splice_mode_config = 0;
	u8 output_id = hw->display[display_id].output_id;

	splice_mode_config = dc_read_immediate(hw, DCREG_OUTIF_SPLICE_MODE_Address);
	splice_mode0 = VS_GET_FIELD(splice_mode_config, DCREG_OUTIF_SPLICE_MODE, SPLICER0);
	splice_mode1 = VS_GET_FIELD(splice_mode_config, DCREG_OUTIF_SPLICE_MODE, SPLICER1);

	/* the configurable connection of dcregOutputStart and dcregOutifSpliceMode */
	switch (splice_mode0) {
	case 0:
		switch (splice_mode1) {
		case 0:
			return false;
		case 1:
			if (output_id == HW_OUTIF_0 || output_id == HW_OUTIF_1) {
				return false;
			} else if (output_id == HW_OUTIF_2) {
				return true;
			} else if (output_id == HW_OUTIF_3) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT2_START_Address, 0x1);
				return true;
			}
			break;
		case 2:
			if (output_id == HW_OUTIF_0 || output_id == HW_OUTIF_1) {
				return false;
			} else if (output_id == HW_OUTIF_2) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				dc_write_immediate(hw, DCREG_OUTPUT3_START_Address, 0x1);
				return true;
			}
			break;
		case 3:
			pr_err("Unvalid ofifo splice mode [splicer0: %d, splicer1: %d], please check.\n",
			       splice_mode0, splice_mode1);
			return false;
		default:
			return false;
		}
		break;
	case 1:
		switch (splice_mode1) {
		case 0:
			if (output_id == HW_OUTIF_0) {
				return true;
			} else if (output_id == HW_OUTIF_1) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT0_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_2 || output_id == HW_OUTIF_3) {
				return false;
			}
			break;
		case 1:
			if (output_id == HW_OUTIF_0 || output_id == HW_OUTIF_2) {
				return true;
			} else if (output_id == HW_OUTIF_1) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT0_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT2_START_Address, 0x1);
				return true;
			}
			break;
		case 2:
			if (output_id == HW_OUTIF_0) {
				return true;
			} else if (output_id == HW_OUTIF_2) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_1) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT0_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				dc_write_immediate(hw, DCREG_OUTPUT3_START_Address, 0x1);
				return true;
			}
			break;
		case 3:
			if (output_id == HW_OUTIF_0) {
				return true;
			} else if (output_id == HW_OUTIF_2) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_1) {
				dc_write_immediate(hw, DCREG_OUTPUT0_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT1_START_Address, 0x1);
				return true;
			}
			break;
		default:
			return false;
		}
		break;
	case 2:
		switch (splice_mode1) {
		case 0:
			if (output_id == HW_OUTIF_0) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_1) {
				dc_write_immediate(hw, DCREG_OUTPUT1_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_2 || output_id == HW_OUTIF_3) {
				return false;
			}
			break;
		case 1:
			if (output_id == HW_OUTIF_0) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_2) {
				return true;
			} else if (output_id == HW_OUTIF_1) {
				dc_write_immediate(hw, DCREG_OUTPUT1_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT2_START_Address, 0x1);
				return true;
			}
			break;
		case 2:
			if (output_id == HW_OUTIF_0 || output_id == HW_OUTIF_2) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_1) {
				dc_write_immediate(hw, DCREG_OUTPUT1_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				dc_write_immediate(hw, DCREG_OUTPUT3_START_Address, 0x1);
				return true;
			}
			break;
		case 3:
			if (output_id == HW_OUTIF_0) {
				return true;
			} else if (output_id == HW_OUTIF_2) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_1) {
				dc_write_immediate(hw, DCREG_OUTPUT1_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT0_START_Address, 0x1);
				return true;
			}
			break;
		default:
			return false;
		}
		break;
	case 3:
		switch (splice_mode1) {
		case 0:
		case 3:
			pr_err("Unvalid ofifo splice mode [splicer0: %d, splicer1: %d], please check.\n",
			       splice_mode0, splice_mode1);
			return false;
		case 1:
			if (output_id == HW_OUTIF_0) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_2) {
				return true;
			} else if (output_id == HW_OUTIF_1) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT3_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				dc_write_immediate(hw, DCREG_OUTPUT2_START_Address, 0x1);
				return true;
			}
			break;
		case 2:
			if (output_id == HW_OUTIF_0) {
				drm_crtc_vblank_off(crtc);
				return true;
			} else if (output_id == HW_OUTIF_2) {
				return true;
			} else if (output_id == HW_OUTIF_1) {
				drm_crtc_vblank_off(crtc);
				dc_write_immediate(hw, DCREG_OUTPUT2_START_Address, 0x1);
				return true;
			} else if (output_id == HW_OUTIF_3) {
				dc_write_immediate(hw, DCREG_OUTPUT3_START_Address, 0x1);
				return true;
			}
			break;
		default:
			return false;
		}
		break;
	default:
		return false;
	}

	return false;
}

void dc_hw_start_trigger(struct dc_hw *hw, u8 display_id, struct drm_crtc *crtc)
{
	u32 i, hw_id = 0;
	struct dc_hw_display_wb *wb = NULL;
	u32 output_id = 0;

	DPU_ATRACE_INSTANT(__func__);

	/* for dp sync trigger */
	if (hw->info->displays[display_id].dp_sync && dp_sync_trigger(hw, display_id))
		return;

	/* for ofifo splice trigger */
	if (hw->info->displays[display_id].ofifo_splice &&
	    ofifo_splice_trigger(hw, display_id, crtc))
		return;

	/* for online trigger */
	for (i = 0; i < hw->info->display_num; i++) {
		if (!hw->display[i].config_status || (hw->info->displays[i].id != display_id))
			continue;

		hw_id = hw->info->displays[i].id;
		output_id = hw->display[i].output_id;
		if (hw->display[i].wb.enable || hw->display[i].wb.dirty)
			wb = &hw->display[i].wb;

		/* clear pending frm start if any to ensure next frame start reflects new update */
		dc_hw_clear_start_interrupt(hw, i);

		if (hw->rev == DC_REV_0) {
			online_trigger(hw, hw_id, output_id, hw->display[i].mode.enable);

			/* SW to start of frame trigger while output command mode and
			 * not auto command mode
			 */
			if (is_display_cmd_sw_trigger(&hw->display[i]) && hw_id < HW_DISPLAY_2) {
				trace_disp_sof(hw_id, output_id, hw->display[i].mode.enable);
				dc_hw_sw_sof_trigger(hw, output_id, hw->display[i].mode.enable);
			}
		}
	}

	/* for offline trigger */
	if (wb && (wb->wb_id < hw->info->wb_num) && hw->wb[wb->wb_id].config_status) {
		hw_id = hw->info->write_back[wb->wb_id].id;
		if (hw_id == HW_BLEND_WB) {
			if (hw->rev == DC_REV_0)
				dc_write_immediate(hw, DCREG_BLD_WB_START_Address,
						   hw->wb[wb->wb_id].fb.enable);
		}
	}
}

void dc_hw_disable_trigger(struct dc_hw *hw, u8 id)
{
	u8 hw_id = 0, output_id = 0;

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;
	hw_id = hw->info->displays[id].id;

	if (hw_id >= HW_DISPLAY_4)
		dc_write_immediate(hw, DCREG_BLD_WB_START_Address, false);
	else
		online_trigger(hw, hw_id, output_id, false);
}

void dc_hw_hard_reset_output_regs(struct dc_hw *hw, u8 id)
{
	u8 output_id = 0;
	u32 config;
	u32 addr = VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, CONFIG_Address);

	if (!hw || !hw->info || id >= hw->info->display_num)
		return;

	output_id = hw->display[id].output_id;
	config = dc_read_immediate(hw, addr);
	dc_write_immediate(hw, addr,
			   VS_SET_FIELD_PREDEF(config, DCREG_OUTPUT0_CONFIG, RESET, RESET));
}

void dc_hw_do_fe0_reset(struct dc_hw *hw)
{
	int ret;
	u8 fe0_rst_status = 0;
	u8 i = 0;

	if (hw->rev == DC_REV_0) {
		hw->reset_status[FE0_SW_RESET] = 0;
		dev_info(hw->dev, "%s: trigger FE0 software reset\n", __func__);
		dc_write_immediate(hw, DCREG_FE0_INTR_ENABLE_Address,
				   VS_SET_FIELD(0, DCREG_FE0_INTR_ENABLE, SW_RST_DONE, 0x1));
		dc_write_immediate(hw, DCREG_FE0_SW_RESET_Address,
				   VS_SET_FIELD_PREDEF(0, DCREG_FE0_SW_RESET, RESET, RESET));

		ret = readb_poll_timeout(&hw->reset_status[FE0_SW_RESET], fe0_rst_status,
					 fe0_rst_status, SW_RESET_INTERVAL_US, SW_RESET_TIMEOUT_US);
		if (ret)
			dev_warn(hw->dev, "%s: FE0 software reset timedout\n", __func__);
		else
			dev_dbg(hw->dev, "%s: FE0 software reset completed\n", __func__);

		dc_write_immediate(hw, DCREG_FE0_INTR_ENABLE_Address,
				   VS_SET_FIELD(0, DCREG_FE0_INTR_ENABLE, SW_RST_DONE, 0));
		hw->reset_status[FE0_SW_RESET] = 0;
	}

	for (i = 0; i < hw->info->plane_fe0_num; i++)
		dc_hw_config_plane_status(hw, i, false);
}

void dc_hw_do_fe1_reset(struct dc_hw *hw)
{
	int ret;
	u8 fe1_rst_status = 0;
	u8 i = 0, j = 0;

	if (hw->rev == DC_REV_0) {
		hw->reset_status[FE1_SW_RESET] = 0;
		dev_info(hw->dev, "%s: trigger FE1 software reset\n", __func__);
		dc_write_immediate(hw, DCREG_FE1_INTR_ENABLE_Address,
				   VS_SET_FIELD(0, DCREG_FE1_INTR_ENABLE, SW_RST_DONE, 0x1));
		dc_write_immediate(hw, DCREG_FE1_SW_RESET_Address,
				   VS_SET_FIELD_PREDEF(0, DCREG_FE1_SW_RESET, RESET, RESET));

		ret = readb_poll_timeout(&hw->reset_status[FE1_SW_RESET], fe1_rst_status,
					 fe1_rst_status, SW_RESET_INTERVAL_US, SW_RESET_TIMEOUT_US);
		if (ret)
			dev_warn(hw->dev, "%s: FE1 software reset timedout\n", __func__);
		else
			dev_dbg(hw->dev, "%s: FE1 software reset completed\n", __func__);

		dc_write_immediate(hw, DCREG_FE1_INTR_ENABLE_Address,
				   VS_SET_FIELD(0, DCREG_FE1_INTR_ENABLE, SW_RST_DONE, 0));
		hw->reset_status[FE1_SW_RESET] = 0;
	}

	for (i = 0; i < hw->info->plane_fe1_num; i++) {
		j = hw->info->plane_fe0_num + i;
		dc_hw_config_plane_status(hw, j, false);
	}
}

static void dc_hw_do_fe_reset(struct dc_hw *hw)
{
	dc_hw_do_fe0_reset(hw);
	dc_hw_do_fe1_reset(hw);
}

void dc_hw_do_be_reset(struct dc_hw *hw)
{
	int ret;
	u8 i, j, be_rst_status = 0;

	if (hw->rev == DC_REV_0) {
		hw->reset_status[BE_SW_RESET] = 0;
		dev_info(hw->dev, "%s: trigger BE software reset\n", __func__);
		dc_write_immediate(hw, DCREG_BE_INTR_ENABLE1_Address,
				   VS_SET_FIELD(0, DCREG_BE_INTR_ENABLE1, SW_RST_DONE, 0x1));
		dc_write_immediate(hw, DCREG_BE_SW_RESET_Address,
				   VS_SET_FIELD_PREDEF(0, DCREG_BE_SW_RESET, RESET, RESET));

		ret = readb_poll_timeout(&hw->reset_status[BE_SW_RESET], be_rst_status,
					 be_rst_status, SW_RESET_INTERVAL_US, SW_RESET_TIMEOUT_US);
		if (ret)
			dev_warn(hw->dev, "%s: BE software reset timedout\n", __func__);
		else
			dev_dbg(hw->dev, "%s: BE software reset completed\n", __func__);

		dc_write_immediate(hw, DCREG_BE_INTR_ENABLE1_Address,
				   VS_SET_FIELD(0, DCREG_BE_INTR_ENABLE1, SW_RST_DONE, 0));
		hw->reset_status[BE_SW_RESET] = 0;
	}

	for (i = 0; i <= HW_DISPLAY_1; i++)
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, i, CONFIG_Address), 0x00000000);

	for (i = 0; i <= HW_DISPLAY_3; i++) {
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_PANEL, i, CONFIG_Address), 0x00);
		display_wb_pos[i] = VS_WB_POS_CNT;
	}

	if (hw->rev == DC_REV_0) {
		for (i = 0; i < HW_WB_NUM; i++)
			wb_busy[i] = false;

		dc_write_immediate(hw, DCREG_BLD_WB_CONFIG_Address, 0x00);
		dc_write_immediate(hw, DCREG_POST_PRO_WB0_CONFIG_Address, 0x00);
		dc_write_immediate(hw, DCREG_POST_PRO_WB1_CONFIG_Address, 0x00);

		for (i = 0; i < hw->info->display_num; i++) {
			if (hw->display[i].info->bld_dth) {
				for (j = 0; j < hw->display[i].states.num; j++) {
					const struct vs_dc_property_proto *proto =
						hw->display[i].states.items[j].proto;

					if (!proto)
						continue;

					/* SW disabled the blend random dither by default */
					if (strcmp("BLD_DITHER", proto->name) == 0) {
						hw->display[i].states.items[j].enable = false;
						hw->display[i].states.items[j].dirty = true;
					}
				}
			}
		}
	}
}

void dc_hw_do_be_reset_cleanup(struct dc_hw *hw)
{
	u8 i = 0;
	unsigned long flags;

	for (i = 0; i < hw->info->wb_num; i++)
		dc_hw_config_wb_status(hw, i, false);

	for (i = 0; i < hw->info->display_num; i++)
		hw->link_node_resource[i] = 0;

	cb8to2_store_value = DCREG_WB_CROSSBAR8_TO2_ResetValue;
	spin_lock_irqsave(&hw->output_mux_slock, flags);
	hw->output_mux_value = DCREG_POST_PROCESS_OUT_ResetValue;
	spin_unlock_irqrestore(&hw->output_mux_slock, flags);
}

void dc_hw_do_reset(struct dc_hw *hw)
{
	dc_hw_do_fe_reset(hw);
	dc_hw_do_be_reset(hw);
	dc_hw_do_be_reset_cleanup(hw);
}

static bool dc_hw_is_layer_idle(struct dc_hw *hw, u8 hw_id)
{
	struct device *dev = hw->dev;
	u32 dbg_dma_st, dbg_dma_out_coord;
	u32 st_mask = 0, st_idle = 0;
	bool is_layer_enable, is_layer_idle;

	dbg_dma_st = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, DEBUG_DMA_ST_Address));
	dbg_dma_out_coord = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, DEBUG_DMA_OUT_COORD_Address));
	is_layer_enable = (dbg_dma_out_coord || dbg_dma_st);

	/* check only the STATE, REQ_DONE and DMA_DONE bits */
	st_mask = VS_SET_FIELD(st_mask, DCREG_SH_LAYER0_DEBUG_DMA_ST, STATE, 0x7);
	st_mask = VS_SET_FIELD(st_mask, DCREG_SH_LAYER0_DEBUG_DMA_ST, REQ_DONE, 0x1);
	st_mask = VS_SET_FIELD(st_mask, DCREG_SH_LAYER0_DEBUG_DMA_ST, DMA_DONE, 0x1);

	/* layer is considered idle when STATE = 0, REQ_DONE = 1, DMA_DONE = 1 */
	st_idle = VS_SET_FIELD(st_idle, DCREG_SH_LAYER0_DEBUG_DMA_ST, REQ_DONE, 0x1);
	st_idle = VS_SET_FIELD(st_idle, DCREG_SH_LAYER0_DEBUG_DMA_ST, DMA_DONE, 0x1);

	is_layer_idle = ((dbg_dma_st & st_mask) == st_idle);
	dev_dbg(dev, "%s layer-%d st:%#x, out_coord:%#x\n", __func__, hw_id, dbg_dma_st,
		dbg_dma_out_coord);

	if (is_layer_enable && !is_layer_idle) {
		dev_warn(dev, "%s layer-%d is not idle st:%#x, out_coord:%#x\n", __func__,
			 hw_id, dbg_dma_st, dbg_dma_out_coord);
		return false;
	}

	return true;
}

bool dc_hw_fe0_is_all_layers_idle(struct dc_hw *hw)
{
	u8 i, hw_id;

	if (!hw->info)
		return true;

	for (i = 0; i < hw->info->plane_fe0_num; ++i) {
		const struct vs_plane_info *plane_info = get_plane_info(i, hw->info);

		if (plane_info->rcd_plane)
			continue;

		hw_id = plane_info->id;
		if (!dc_hw_is_layer_idle(hw, hw_id))
			return false;
	}

	return true;
}

bool dc_hw_fe1_is_all_layers_idle(struct dc_hw *hw)
{
	u8 i, hw_id;

	if (!hw->info)
		return true;

	for (i = hw->info->plane_fe0_num; i < hw->info->plane_num; ++i) {
		const struct vs_plane_info *plane_info = get_plane_info(i, hw->info);

		if (plane_info->rcd_plane)
			continue;

		hw_id = plane_info->id;
		if (!dc_hw_is_layer_idle(hw, hw_id))
			return false;
	}

	return true;
}

bool dc_hw_fe_is_all_layers_idle(struct dc_hw *hw)
{
	return (dc_hw_fe0_is_all_layers_idle(hw) && dc_hw_fe1_is_all_layers_idle(hw));
}

#if IS_ENABLED(CONFIG_TZ_PROT)
static void toggle_secure(struct dc_hw *hw, u8 layer, bool secure)
{
	struct device *dev = hw->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	const struct vs_plane_info *plane_info = get_plane_info(layer, hw->info);
	u8 sid = plane_info->sid;
	int ret;

	if (!dc->tzprot_pdev) {
		dev_err(dev,
			"%s: secure plane requested but tzprot_pdev not populated correctly in dts.",
			__func__);
		return;
	}

	mutex_lock(&hw->secure_lock);
	if (test_bit(plane_info->id, hw->secured_layers_mask) == secure) {
		dev_warn(dev, "layer %d already has secure %d, double enable or disable",
			 plane_info->id, secure);
		goto exit;
	}

	DPU_ATRACE_BEGIN("trusty_protect_ip");
	ret = trusty_protect_ip(&(dc->tzprot_pdev->dev), sid, secure);
	if (ret)
		dev_err(dev,
			"%s: trusty_protect_ip call failed for sid %d, fb->secure: %d. error %d",
			__func__, sid, secure, ret);
	DPU_ATRACE_END("trusty_protect_ip");

	if (secure)
		set_bit(plane_info->id, hw->secured_layers_mask);
	else
		clear_bit(plane_info->id, hw->secured_layers_mask);

	trace_disp_trusty_protect_ip(plane_info->id, sid, hw->secured_layers_mask, secure);
exit:
	mutex_unlock(&hw->secure_lock);
}

static void plane_set_secure(struct dc_hw *hw, u8 layer, struct dc_hw_fb *fb)
{
	trace_config_hw_layer_feature_en("SECURE_FB", dc_hw_get_plane_id(layer, hw), fb->secure);

	fb->secure_dirty = false;
	toggle_secure(hw, layer, fb->secure);
}
#else
static inline void plane_set_secure(struct dc_hw *hw, u8 layer, struct dc_hw_fb *fb) {}
#endif // CONFIG_TZ_PROT

static void dc_hw_plane_ot_init(struct dc_hw *hw, u8 hw_id, u32 offset)
{
	const struct dc_hw_plane *hw_plane = vs_dc_hw_get_plane(hw, hw_id);

	if (hw_plane->info->outstanding_number)
		dc_write(hw, DCREG_SH_LAYER0_OT_NUMBER_Address + offset,
			 hw_plane->info->outstanding_number);
}

void dc_hw_set_fe_qos(struct dc_hw *hw, u32 value)
{
	dc_write(hw, DCREG_FE0_QOS_Address, value);
	dc_write(hw, DCREG_FE1_QOS_Address, value);
}

static void plane_set_fb(struct dc_hw *hw, u8 hw_id, struct dc_hw_fb *fb)
{
	u32 offset = _get_plane_offset(hw_id);
	u32 config = 0;

	trace_config_hw_layer_fb("FB", hw_id, fb);

	/* commit frame buffer width/height */

	if (fb->enable) {
		/* address configuration */
		dc_write(hw, DCREG_SH_LAYER0_ADDRESS_Address + offset,
			 (u32)(fb->address & 0xFFFFFFFF));
		dc_write(hw, DCREG_SH_LAYER0_HIGH_ADDRESS_Address + offset, fb->address >> 32);
		dc_write(hw, DCREG_SH_LAYER0_UADDRESS_Address + offset,
			 (u32)(fb->u_address & 0xFFFFFFFF));
		dc_write(hw, DCREG_SH_LAYER0_HIGH_UADDRESS_Address + offset, fb->u_address >> 32);
		dc_write(hw, DCREG_SH_LAYER0_VADDRESS_Address + offset,
			 (u32)(fb->v_address & 0xFFFFFFFF));
		dc_write(hw, DCREG_SH_LAYER0_HIGH_VADDRESS_Address + offset, fb->v_address >> 32);

		/* stride/size configuration */
		dc_write(hw, DCREG_SH_LAYER0_STRIDE_Address + offset, fb->stride);
		dc_write(hw, DCREG_SH_LAYER0_USTRIDE_Address + offset, fb->u_stride);
		dc_write(hw, DCREG_SH_LAYER0_VSTRIDE_Address + offset, fb->v_stride);
		dc_write(hw, DCREG_SH_LAYER0_SIZE_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_SIZE, WIDTH, fb->width) |
				 VS_SET_FIELD(0, DCREG_SH_LAYER0_SIZE, HEIGHT, fb->height));
		dc_hw_plane_ot_init(hw, hw_id, offset);
	}

	/* enable/swizzle/tile/rotation/format configuration */
	config = dc_read(hw, DCREG_SH_LAYER0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, ENABLE, fb->enable);
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, UV_SWIZZLE, fb->uv_swizzle);
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, SWIZZLE, fb->swizzle);
	if (hw->info->chip_id == VS_CHIP_ID_9200 && fb->tile_mode == TILE_MODE_32X8)
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, TILE_MODE, 0x8);
	else
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, TILE_MODE, fb->tile_mode);
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, ROT_ANGLE, fb->rotation);
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, FORMAT, fb->format);

	dc_write(hw, DCREG_SH_LAYER0_CONFIG_Address + offset, config);

	offset = _get_plane_out_offset(hw_id);

	config = 0;
	/* zorder configuration */
	if (hw->rev == DC_REV_0) {
		u32 layer_offset = _get_plane_zorder_offset(hw_id);

		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_BLEND_STACK_ID, ID0, fb->zpos & 0xF) |
			 VS_SET_FIELD(config, DCREG_SH_LAYER0_BLEND_STACK_ID, ID1, 0);

		dc_write(hw, DCREG_SH_LAYER0_BLEND_STACK_ID_Address + layer_offset, config);
	}

	fb->dirty = false;
}

/**
 * plane_set_outpath() - Configure the output path for a plane.
 * @hw: Pointer to the hardware structure.
 * @hw_id: Hardware ID of the plane (layer).
 * @fb: Pointer to the framebuffer structure containing routing info.
 *
 * This function configures the routing of a hardware plane to a specific
 * display output path (display_id).
 */
static void plane_set_outpath(struct dc_hw *hw, u8 hw_id, struct dc_hw_fb *fb)
{
	trace_config_hw_layer_feature("OUTPATH", hw_id, "en:%d id:%d", fb->enable, fb->display_id);

	if (hw->rev == DC_REV_0)
		dc_write(hw, VS_SET_FE_FIELD(DCREG_LAYER, hw_id, OUTPUT_PATH_ID_Address),
			 VS_SET_FIELD(0, DCREG_LAYER0_OUTPUT_PATH_ID, ID, fb->display_id));
}

static void plane_set_ext_fb(struct dc_hw *hw, u8 hw_id, struct dc_hw_fb *fb)
{
	trace_config_hw_layer_fb("FB_EXT", hw_id, fb);

	/* commit frame buffer width/height */
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_SIZE_Address),
		 VS_SET_FIELD(0, DCREG_SH_LAYER0_EX_SIZE, WIDTH, fb->width) |
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_EX_SIZE, HEIGHT, fb->height));

	/* commit address */
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_ADDRESS_Address), (u32)(fb->address & 0xFFFFFFFF));
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_HIGH_ADDRESS_Address), fb->address >> 32);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_UADDRESS_Address),
		 (u32)(fb->u_address & 0xFFFFFFFF));
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_HIGH_UADDRESS_Address), fb->u_address >> 32);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_VADDRESS_Address),
		 (u32)(fb->v_address & 0xFFFFFFFF));
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_HIGH_VADDRESS_Address), fb->v_address >> 32);

	/* commit stride */
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_STRIDE_Address), fb->stride);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_USTRIDE_Address), fb->u_stride);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, EX_VSTRIDE_Address), fb->v_stride);

	fb->dirty = false;
}

static void plane_set_pos(struct dc_hw *hw, u8 hw_id, struct dc_hw_position *pos)
{
	trace_config_hw_layer_feature("POS", hw_id, "x:%d y:%d w:%d h:%d", pos->rect[0].x,
				      pos->rect[0].y, pos->rect[0].w, pos->rect[0].h);

	/* TODO: need to refine. */
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_ORIGIN_Address),
		 VS_SET_FIELD(0, DCREG_SH_LAYER0_OUT_ROI_ORIGIN, X, pos->rect[0].x) |
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_OUT_ROI_ORIGIN, Y, pos->rect[0].y));

	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_Address),
		 VS_SET_FIELD(0, DCREG_SH_LAYER0_OUT_ROI_SIZE, WIDTH, pos->rect[0].w) |
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_OUT_ROI_SIZE, HEIGHT, pos->rect[0].h));

	pos->dirty = false;
}

static void plane_set_y2r(struct dc_hw *hw, u8 hw_id, struct dc_hw_y2r *y2r_conf)
{
	u32 csc_mode = 0, csc_gamut = 0;
	u32 config = 0;

	trace_config_hw_layer_feature("Y2R", hw_id, "en:%d gamut:%d mode:%d", y2r_conf->enable,
				      y2r_conf->gamut, y2r_conf->mode);

	if (!y2r_conf->enable) {
		config = dc_read(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, CONFIG_Address));
		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, CONFIG_Address),
			 VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_CONFIG, Y2R, DISABLED));
		y2r_conf->dirty = false;
		return;
	}

	switch (y2r_conf->mode) {
	case CSC_MODE_USER_DEF:
		csc_mode = VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, MODE, PROGRAMMABLE);
		break;
	case CSC_MODE_L2L:
		csc_mode = VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, MODE,
					       LIMIT_YUV_2_LIMIT_RGB);
		break;
	case CSC_MODE_L2F:
		csc_mode = VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, MODE,
					       LIMIT_YUV_2_FULL_RGB);
		break;
	case CSC_MODE_F2L:
		csc_mode = VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, MODE,
					       FULL_YUV_2_LIMIT_RGB);
		break;
	case CSC_MODE_F2F:
		csc_mode = VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, MODE,
					       FULL_YUV_2_FULL_RGB);
		break;
	default:
		break;
	}

	if (y2r_conf->mode != CSC_MODE_USER_DEF) {
		switch (y2r_conf->gamut) {
		case CSC_GAMUT_601:
			csc_gamut =
				VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, GAMUT, BT601);
			break;
		case CSC_GAMUT_709:
			csc_gamut =
				VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, GAMUT, BT709);
			break;
		case CSC_GAMUT_2020:
			csc_gamut =
				VS_SET_FIELD_PREDEF(0, DCREG_SH_LAYER0_Y2R_CONFIG, GAMUT, BT2020);
			break;
		default:
			break;
		}
	} else {
		dc_write_u32_blob(hw,
				  VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, YUV_TO_RGB_COEF0_Address),
				  y2r_conf->coef, VS_MAX_Y2R_COEF_NUM);
	}

	dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, Y2R_CONFIG_Address),
		 csc_mode | csc_gamut);

	config = dc_read(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, CONFIG_Address));
	dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, CONFIG_Address),
		 VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_CONFIG, Y2R, ENABLED));

	y2r_conf->dirty = false;
}

static void plane_set_scale(struct dc_hw *hw, u8 hw_id, struct dc_hw_scale *scale)
{
	u32 config = 0;
	u32 offset_x = 0x0;
	u32 offset_y = 0x0;

	trace_config_hw_layer_feature_en("SCALE", hw_id, scale->enable);
	trace_config_hw_layer_feature_en("SCALE_COEFF", hw_id, scale->coefficients_enable);

	config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_Address));
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_Address),
		 VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, SCALE, scale->enable));

	scale->dirty = false;

	if (!scale->enable)
		return;

	if (scale->coefficients_dirty) {
		if (scale->coefficients_enable)
			dc_hw_config_load_filter(hw, hw_id, scale->coefficients.coef_v,
						 scale->coefficients.coef_h);
		else
			dc_hw_config_load_filter(hw, hw_id, NULL, NULL);

		scale->coefficients_dirty = false;
	}

	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SCALE_INITIAL_OFFSET_X_Address), offset_x);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SCALE_INITIAL_OFFSET_Y_Address), offset_y);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, HSCALE_FACTOR_Address), scale->factor_x);
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, VSCALE_FACTOR_Address), scale->factor_y);

	trace_config_hw_layer_feature(
		"SCALE_DATA", hw_id,
		"en:%d factor_[x y]: %u %u offset_[x y]: %u %u", scale->enable,
		scale->factor_x, scale->factor_y, offset_x, offset_y);
}

static void plane_set_roi(struct dc_hw *hw, u8 hw_id, struct dc_hw_roi *roi_hw)
{
	u32 config_rect0 = 0;
	u32 config_rect1 = 0;

	if (!roi_hw || !hw) {
		dev_err(hw->dev, "%s: Invalid inputs", __func__);
		return;
	}

	trace_config_hw_layer_feature("ROI", hw_id, "mode:%d", roi_hw->mode);

	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, DMA_MODE_Address), (u32)roi_hw->mode);

	if (roi_hw->mode == VS_DMA_ONE_ROI || roi_hw->mode == VS_DMA_TWO_ROI ||
	    roi_hw->mode == VS_DMA_EXT_LAYER || roi_hw->mode == VS_DMA_EXT_LAYER_EX) {
		trace_config_hw_layer_feature(
			"ROI[0]", hw_id,
			"IN_RECT[0]_[x y w h]: %d %d %d %d OUT_RECT[0]_[x y w h]: %d %d %d %d",
			roi_hw->in_rect[0].x, roi_hw->in_rect[0].y,
			roi_hw->in_rect[0].w, roi_hw->in_rect[0].h,
			roi_hw->out_rect[0].x, roi_hw->out_rect[0].y,
			roi_hw->out_rect[0].w, roi_hw->out_rect[0].h);

		if (roi_hw->mode != VS_DMA_EXT_LAYER) {
			/* config in ROI 0 region. */
			config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_IN_ROI_ORIGIN, X,
						    roi_hw->in_rect[0].x) |
				       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_IN_ROI_ORIGIN, Y,
						    roi_hw->in_rect[0].y);
			dc_write(hw, VS_SH_LAYER_FIELD(hw_id, IN_ROI_ORIGIN_Address), config_rect0);

			config_rect0 = 0;
			config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_IN_ROI_SIZE,
						    WIDTH, roi_hw->in_rect[0].w) |
				       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_IN_ROI_SIZE,
						    HEIGHT, roi_hw->in_rect[0].h);
			dc_write(hw, VS_SH_LAYER_FIELD(hw_id, IN_ROI_SIZE_Address), config_rect0);
		}

		/* config out ROI 0 region */
		config_rect0 = 0;
		config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_ORIGIN, X,
					    roi_hw->out_rect[0].x) |
			       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_ORIGIN, Y,
					    roi_hw->out_rect[0].y);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_ORIGIN_Address), config_rect0);

		config_rect0 = 0;
		config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_SIZE, WIDTH,
					    roi_hw->out_rect[0].w) |
			       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_SIZE, HEIGHT,
					    roi_hw->out_rect[0].h);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_Address), config_rect0);
	}

	if (roi_hw->mode == VS_DMA_TWO_ROI || roi_hw->mode == VS_DMA_EXT_LAYER_EX ||
	    roi_hw->mode == VS_DMA_EXT_LAYER) {
		trace_config_hw_layer_feature(
			"ROI[1]", hw_id,
			"IN_RECT[1]_[x y w h]: %d %d %d %d OUT_RECT[1]_[x y w h]: %d %d %d %d",
			roi_hw->in_rect[1].x, roi_hw->in_rect[1].y,
			roi_hw->in_rect[1].w, roi_hw->in_rect[1].h,
			roi_hw->out_rect[1].x, roi_hw->out_rect[1].y,
			roi_hw->out_rect[1].w, roi_hw->out_rect[1].h);

		if (roi_hw->mode != VS_DMA_EXT_LAYER) {
			/* config in ROI 1 region */
			config_rect1 = VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_IN_ROI_ORIGIN_EX,
						    X, roi_hw->in_rect[1].x) |
				       VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_IN_ROI_ORIGIN_EX,
						    Y, roi_hw->in_rect[1].y);
			dc_write(hw, VS_SH_LAYER_FIELD(hw_id, IN_ROI_ORIGIN_EX_Address),
				 config_rect1);

			config_rect1 = 0;
			config_rect1 = VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_IN_ROI_SIZE_EX,
						    WIDTH, roi_hw->in_rect[1].w) |
				       VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_IN_ROI_SIZE_EX,
						    HEIGHT, roi_hw->in_rect[1].h);
			dc_write(hw, VS_SH_LAYER_FIELD(hw_id, IN_ROI_SIZE_EX_Address),
				 config_rect1);
		}

		/* config out rect 1 region */
		config_rect1 = 0;
		config_rect1 = VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_OUT_ROI_ORIGIN_EX, X,
					    roi_hw->out_rect[1].x) |
			       VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_OUT_ROI_ORIGIN_EX, Y,
					    roi_hw->out_rect[1].y);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_ORIGIN_EX_Address), config_rect1);

		config_rect1 = 0;
		config_rect1 = VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_OUT_ROI_SIZE_EX, WIDTH,
					    roi_hw->out_rect[1].w) |
			       VS_SET_FIELD(config_rect1, DCREG_SH_LAYER0_OUT_ROI_SIZE_EX, HEIGHT,
					    roi_hw->out_rect[1].h);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_EX_Address), config_rect1);
	}

	if (roi_hw->mode == VS_DMA_SKIP_ROI) {
		trace_config_hw_layer_feature(
			"ROI[0]", hw_id,
			"IN_RECT[0]_[x y w h]: %d %d %d %d OUT_RECT[0]_[x y w h]: %d %d %d %d",
			roi_hw->in_rect[0].x, roi_hw->in_rect[0].y,
			roi_hw->in_rect[0].w, roi_hw->in_rect[0].h,
			roi_hw->out_rect[0].x, roi_hw->out_rect[0].y,
			roi_hw->out_rect[0].w, roi_hw->out_rect[0].h);

		/* config skip ROI region. */
		config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_SKIP_ROI_ORIGIN, X,
					    roi_hw->in_rect[0].x) |
			       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_SKIP_ROI_ORIGIN, Y,
					    roi_hw->in_rect[0].y);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SKIP_ROI_ORIGIN_Address), config_rect0);

		config_rect0 = 0;
		config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_SKIP_ROI_SIZE, WIDTH,
					    roi_hw->in_rect[0].w) |
			       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_SKIP_ROI_SIZE, HEIGHT,
					    roi_hw->in_rect[0].h);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SKIP_ROI_SIZE_Address), config_rect0);

		/* config out region */
		config_rect0 = 0;
		config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_ORIGIN, X,
					    roi_hw->out_rect[0].x) |
			       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_ORIGIN, Y,
					    roi_hw->out_rect[0].y);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_ORIGIN_Address), config_rect0);

		config_rect0 = 0;
		config_rect0 = VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_SIZE, WIDTH,
					    roi_hw->out_rect[0].w) |
			       VS_SET_FIELD(config_rect0, DCREG_SH_LAYER0_OUT_ROI_SIZE, HEIGHT,
					    roi_hw->out_rect[0].h);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, OUT_ROI_SIZE_Address), config_rect0);
	}

	roi_hw->dirty = false;
}

static void plane_set_3d_lut(struct dc_hw *hw, u8 hw_id, struct dc_hw_block *lut_3d)
{
	u32 offset = _get_plane_offset(hw_id);
	u32 *block_addr = NULL;
	u32 config = 0, entry_cnt = 0, i = 0;

	trace_config_hw_layer_feature("3D_LUT", hw_id, "%d", lut_3d->enable);

	config = dc_read(hw, DCREG_SH_LAYER0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, LUT3D, lut_3d->enable);
	dc_write(hw, DCREG_SH_LAYER0_CONFIG_Address + offset, config);

	if (lut_3d->enable) {
		block_addr = (u32 *)lut_3d->vaddr;
		entry_cnt = *block_addr;
		trace_config_hw_layer_feature("3D_LUT_DATA", hw_id, "block_addr:%pK entry_cnt:%d",
					      block_addr, entry_cnt);

		block_addr++;
		trace_hw_layer_feature_data("3D_LUT_DATA", hw_id, "RED", (const u8 *)block_addr,
					    entry_cnt * sizeof(*block_addr));
		/* TODO:reg may change. Wait for final version */
		dc_write(hw, DCREG_LAYER0_LUT3D_RED_ACCESS_Address + offset, true);
		/* coef data of RED channel*/
		for (i = 0; i < entry_cnt; i++) {
			dc_write(hw, DCREG_SH_LAYER0_LUT3D_RED_DATA_Address + offset, *block_addr);
			block_addr++;
		}

		trace_hw_layer_feature_data("3D_LUT_DATA", hw_id, "GREEN", (const u8 *)block_addr,
					    entry_cnt * sizeof(*block_addr));
		dc_write(hw, DCREG_LAYER0_LUT3D_GREEN_ACCESS_Address + offset, true);
		/* coef data of GREEN channel*/
		for (i = 0; i < entry_cnt; i++) {
			dc_write(hw, DCREG_SH_LAYER0_LUT3D_GREEN_DATA_Address + offset,
				 *block_addr);
			block_addr++;
		}

		trace_hw_layer_feature_data("3D_LUT_DATA", hw_id, "BLUE", (const u8 *)block_addr,
					    entry_cnt * sizeof(*block_addr));
		dc_write(hw, DCREG_LAYER0_LUT3D_BLUE_ACCESS_Address + offset, true);
		/* coef data of BLUE channel*/
		for (i = 0; i < entry_cnt; i++) {
			dc_write(hw, DCREG_SH_LAYER0_LUT3D_BLUE_DATA_Address + offset, *block_addr);
			block_addr++;
		}

		kfree(lut_3d->vaddr);
	}

	lut_3d->dirty = false;
}

static void plane_set_std_bld(struct dc_hw *hw, u8 hw_id, struct dc_hw_std_bld *std_bld)
{
	u32 config = 0;

	trace_config_hw_layer_feature("BLEND_MODE", hw_id, "alpha:%d blend_mode:%d", std_bld->alpha,
				      std_bld->blend_mode);

	if (std_bld->blend_mode == DRM_MODE_BLEND_PIXEL_NONE &&
	    std_bld->alpha == VS_BLEND_ALPHA_OPAQUE) {
		config = dc_read(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id,
						     ALPHA_BLEND_CONFIG_Address));
		config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
					     ALPHA_BLEND, DISABLED);
		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, ALPHA_BLEND_CONFIG_Address),
			 config);

	} else {
		config = dc_read(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id,
						     ALPHA_BLEND_CONFIG_Address));
		config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
					     ALPHA_BLEND, ENABLED);
		config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
					     ALPHA_BLEND_FAST, DISABLED);
		config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
					     SRC_ALPHA_MODE, NORMAL);
		config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
					     DST_ALPHA_MODE, NORMAL);

		switch (std_bld->blend_mode) {
		case DRM_MODE_BLEND_PREMULTI:
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_GLOBAL_ALPHA_MODE, SCALED);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_GLOBAL_ALPHA_MODE, GLOBAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_FACTOR_MODE, DEST);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_ALPHA_FACTOR_MODE, DEST);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_ALPHA_FACTOR_MODE, DEST);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_COLOR_BLEND_MODE, FACTOR_MODE);
			dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, SRC_ALPHA_Address),
				 std_bld->alpha);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_COLOR_BLEND_MODE, INVERSED);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_ALPHA_BLEND_MODE, FACTOR_MODE);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_ALPHA_BLEND_MODE, INVERSED);
			break;
		case DRM_MODE_BLEND_COVERAGE:
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_GLOBAL_ALPHA_MODE, SCALED);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_GLOBAL_ALPHA_MODE, SCALED);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_ALPHA_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_ALPHA_FACTOR_MODE, DEST);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_COLOR_BLEND_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_COLOR_BLEND_MODE, INVERSED);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_ALPHA_BLEND_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_ALPHA_BLEND_MODE, INVERSED);
			break;
		case DRM_MODE_BLEND_PIXEL_NONE:
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_GLOBAL_ALPHA_MODE, GLOBAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_GLOBAL_ALPHA_MODE, GLOBAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_ALPHA_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_ALPHA_FACTOR_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_COLOR_BLEND_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_COLOR_BLEND_MODE, INVERSED);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     SRC_ALPHA_BLEND_MODE, NORMAL);
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     DST_ALPHA_BLEND_MODE, INVERSED);
			break;
		default:
			break;
		}

		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, ALPHA_BLEND_CONFIG_Address),
			 config);

		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, SRC_GLOBAL_ALPHA_Address),
			 std_bld->alpha);

		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, DST_GLOBAL_ALPHA_Address),
			 std_bld->alpha);
	}

	std_bld->dirty = false;
}

static bool plane_set_clear(struct dc_hw *hw, u8 hw_id, struct dc_hw_clear *clear)
{
	u32 config = 0;

	trace_config_hw_layer_feature("CLEAR", hw_id, "en: %d", clear->enable);

	config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_Address));
	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, CONFIG_Address),
		 VS_SET_FIELD(config, DCREG_SH_LAYER0_CONFIG, CLEAR, clear->enable));

	if (clear->enable) {
		trace_config_hw_layer_feature("CLEAR_DATA", hw_id, "ARGB: %x %x %x %x",
					      clear->color.a, clear->color.r, clear->color.g,
					      clear->color.b);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SOLID_COLOR_ALPHA_Address), clear->color.a);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SOLID_COLOR_RED_Address), clear->color.r);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SOLID_COLOR_GREEN_Address), clear->color.g);
		dc_write(hw, VS_SH_LAYER_FIELD(hw_id, SOLID_COLOR_BLUE_Address), clear->color.b);
	}

	clear->dirty = false;

	return true;
}

static void plane_set_sram(struct dc_hw *hw, u8 hw_id, struct dc_hw_sram_pool *sram)
{
	u8 hw_size = 0;

	trace_config_hw_layer_feature(
		"SRAM", hw_id,
		"sp_handle:%#x sp_size:%d sp_unit_sz:%d scl_sp_handle:%#x scl_sp_size:%d",
		sram->sp_handle, sram->sp_size, sram->sp_unit_size, sram->scl_sp_handle,
		sram->scl_sp_size);

	if (sram->sp_unit_size == SRAM_UNIT_SIZE_64KB) {
		if (sram->sp_size == (ALIGN64KB * 2))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_KBYTE128;
		else if (sram->sp_size == (ALIGN64KB * 3))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_KBYTE192;
		else if (sram->sp_size == (ALIGN64KB * 4))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_KBYTE256;
		else if (sram->sp_size == (ALIGN64KB * 5))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_KBYTE320;
		else if (sram->sp_size >= (ALIGN64KB * 6))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_KBYTE384;
		else
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_KBYTE64;

		/* TODO(b/332951297) disallow 320 and 384 KB SRAM allocations */
		if (sram->sp_size >= (ALIGN64KB * 5))
			dev_err_ratelimited(hw->dev, "invalid DMA SRAM alloc:%uK",
					    sram->sp_size >> 10);
		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, DMA_SRAM_SIZE_Address),
			 hw_size);
	} else if (sram->sp_unit_size == SRAM_UNIT_SIZE_32KB) {
		if (sram->sp_size == (ALIGN32KB * 2))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_EX_KBYTE64;
		else if (sram->sp_size == (ALIGN32KB * 3))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_EX_KBYTE96;
		else if (sram->sp_size == (ALIGN32KB * 4))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_EX_KBYTE128;
		else if (sram->sp_size == (ALIGN32KB * 5))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_EX_KBYTE160;
		else if (sram->sp_size >= (ALIGN32KB * 6))
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_EX_KBYTE192;
		else
			hw_size = DCREG_SH_LAYER0_DMA_SRAM_SIZE_VALUE_EX_KBYTE32;

		dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, DMA_SRAM_SIZE_Address),
			 hw_size << 8);
	}

	if (sram->scl_sp_size == (ALIGN36KB * 2))
		hw_size = DCREG_SH_LAYER0_SCALER_SRAM_SIZE_VALUE_KBYTE72;
	else if (sram->scl_sp_size == (ALIGN36KB * 3))
		hw_size = DCREG_SH_LAYER0_SCALER_SRAM_SIZE_VALUE_KBYTE108;
	else if (sram->scl_sp_size == (ALIGN36KB * 4))
		hw_size = DCREG_SH_LAYER0_SCALER_SRAM_SIZE_VALUE_KBYTE144;
	else if (sram->scl_sp_size >= (ALIGN36KB * 5))
		hw_size = DCREG_SH_LAYER0_SCALER_SRAM_SIZE_VALUE_KBYTE180;
	else
		hw_size = DCREG_SH_LAYER0_SCALER_SRAM_SIZE_VALUE_KBYTE36;
	dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, SCALER_SRAM_SIZE_Address), hw_size);

	sram->dirty = false;
}

static void plane_set_qos(struct dc_hw *hw, u8 hw_id, struct dc_hw_plane_qos *qos)
{
	u32 config = 0, config1 = 0, config2 = 0;
	u8 i;

	for (i = 0; i < MAX_QOS_LEVLE; i++) {
		config =
			(i == 0) ?
				VS_SET_FIELD(config, DCREG_FE0_QOS, LEVEL0, qos->fe0_qos_value[i]) :
			(i == 1) ?
				VS_SET_FIELD(config, DCREG_FE0_QOS, LEVEL1, qos->fe0_qos_value[i]) :
			(i == 2) ?
				VS_SET_FIELD(config, DCREG_FE0_QOS, LEVEL2, qos->fe0_qos_value[i]) :
			(i == 3) ?
				VS_SET_FIELD(config, DCREG_FE0_QOS, LEVEL3, qos->fe0_qos_value[i]) :
				config;
		config1 = (i == 0) ? VS_SET_FIELD(config1, DCREG_FE1_QOS, LEVEL0,
						  qos->fe1_qos_value[i]) :
			  (i == 1) ? VS_SET_FIELD(config1, DCREG_FE1_QOS, LEVEL1,
						  qos->fe1_qos_value[i]) :
			  (i == 2) ? VS_SET_FIELD(config1, DCREG_FE1_QOS, LEVEL2,
						  qos->fe1_qos_value[i]) :
			  (i == 3) ? VS_SET_FIELD(config1, DCREG_FE1_QOS, LEVEL3,
						  qos->fe1_qos_value[i]) :
					   config1;
		config2 = (i == 0) ? VS_SET_FIELD(config2, DCREG_LAYER0_QOS, LEVEL0,
						  qos->plane_qos_value[i]) :
			  (i == 1) ? VS_SET_FIELD(config2, DCREG_LAYER0_QOS, LEVEL1,
						  qos->plane_qos_value[i]) :
			  (i == 2) ? VS_SET_FIELD(config2, DCREG_LAYER0_QOS, LEVEL2,
						  qos->plane_qos_value[i]) :
			  (i == 3) ? VS_SET_FIELD(config2, DCREG_LAYER0_QOS, LEVEL3,
						  qos->plane_qos_value[i]) :
					   config2;

		fe0_qos_value[i] = qos->fe0_qos_value[i];
		fe1_qos_value[i] = qos->fe1_qos_value[i];
	}

	dc_write_immediate(hw, DCREG_FE0_QOS_Address, config);
	dc_write_immediate(hw, DCREG_FE1_QOS_Address, config1);
	dc_write_immediate(hw, VS_SET_FE_FIELD(DCREG_LAYER, hw_id, QOS_Address), config2);
	qos->dirty = false;
}

inline void dc_hw_set_output_start(struct dc_hw *hw, u8 output_id, bool trig_enable)
{
	dc_write_immediate(hw, VS_SET_PANEL_FIELD(DCREG_OUTPUT, output_id, START_Address),
			   trig_enable);
}

static void plane_set_rcd_mask(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask)
{
	rcd_fb_config_hw(hw, rcd_mask);
	rcd_enable(hw, rcd_mask);
	rcd_roi_config_hw(hw, rcd_mask);

	rcd_mask->dirty = false;
}

static void dc_hw_configure_cmd_underrun(struct dc_hw *hw, u8 hw_id, u8 output_id,
					 struct dc_hw_display_mode *mode)
{
	u32 config = 0;
	u32 te_width_us = mode->te_usec;
	int fps = mode->fps;
	/* scan_frame_time = 1/fps - te_width
	 * linetime + 1% margin = (scan_frame_time / (height * UNDERRUN_MARGIN)) / DPU_REF_CLK
	 */
	u32 active_time_us = (USEC_PER_SEC / fps) - te_width_us;
	u32 line_cycles = mult_frac(active_time_us, DPU_REF_CLK_MHZ * UNDERRUN_MARGIN_PCT,
				    100 * mode->v_active);

	config = VS_SET_FIELD(0, DCREG_SH_OUTPUT0_SCANOUT_COUNTER, WIDTH, line_cycles) |
		 VS_SET_FIELD(0, DCREG_SH_OUTPUT0_SCANOUT_COUNTER, HEIGHT, mode->v_active);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_OUTPUT, output_id, SCANOUT_COUNTER_Address),
		 config);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id, SCANOUT_DELAY_COUNTER_Address),
		 0);

	dev_dbg(hw->dev, "%s: h_active = %u, v_active = %u, te_width_us = %u, fps = %d\n", __func__,
		mode->h_active, mode->v_active, te_width_us, fps);
	dev_dbg(hw->dev, "%s: config = 0x%x\n", __func__, config);
	trace_disp_underrun_config(hw_id, output_id, mode, te_width_us, config);
}

static void dc_hw_configure_cmd_urgent(struct dc_hw *hw, u8 hw_id, u8 output_id,
				       struct dc_hw_display_mode *mode, bool force_disable)
{
	struct dc_hw_display *display = &hw->display[hw_id];
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_dc_urgent_cmd_config *urgent_cmd_override;
	u32 urgent_value;
	u32 sys_counter;
	u32 total_frame_time_us;
	u32 v_active;
	u32 line_cycles;
	u32 sys_delay_counter;

	urgent_value =
		dc_read(hw, VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_VALUE_Address));

	if (dc->urgent_override[hw_id].override_urgent) {
		urgent_cmd_override = &dc->urgent_override[hw_id].urgent_cmd_config;
		memcpy(&display->urgent_cmd_config, urgent_cmd_override,
		       sizeof(*urgent_cmd_override));
	}

	if (force_disable || !display->urgent_cmd_config.enable) {
		urgent_value = VS_SET_FIELD(urgent_value, DCREG_SH_OUTPUT0_URGENT_VALUE, VALUE, 0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_VALUE_Address),
			 urgent_value);
		return;
	}

	/* system_frame_time = 1/fps
	 * linetime + 10% margin = (system_frame_time / (height * URGENT_MARGIN)) / DPU_REF_CLK
	 */
	total_frame_time_us = USEC_PER_SEC / mode->fps;
	v_active = mult_frac(mode->v_active, display->urgent_cmd_config.v_margin_pct, 100);
	line_cycles = mult_frac(total_frame_time_us,
				DPU_REF_CLK_MHZ * display->urgent_cmd_config.h_margin_pct,
				100 * v_active);
	sys_delay_counter = display->urgent_cmd_config.delay_counter_usec * DPU_REF_CLK_MHZ;

	sys_counter = VS_SET_FIELD(0, DCREG_SH_OUTPUT0_SYSTEM_COUNTER, WIDTH, line_cycles) |
		      VS_SET_FIELD(0, DCREG_SH_OUTPUT0_SYSTEM_COUNTER, HEIGHT, v_active);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_OUTPUT, output_id, SYSTEM_COUNTER_Address),
		 sys_counter);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id, SYSTEM_DELAY_COUNTER_Address),
		 sys_delay_counter);

	urgent_value = VS_SET_FIELD(urgent_value, DCREG_SH_OUTPUT0_URGENT_VALUE, VALUE,
				    display->urgent_cmd_config.urgent_value);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_VALUE_Address),
		 urgent_value);

	dev_dbg(hw->dev, "%s: h_active = %u, v_active = %u, fps = %d\n", __func__, mode->h_active,
		mode->v_active, mode->fps);
	dev_dbg(hw->dev,
		"%s: h_margin_pct = %u, v_margin_pct = %u, delay_counter_usec = %u urgent_value = %#x\n",
		__func__, display->urgent_cmd_config.h_margin_pct,
		display->urgent_cmd_config.v_margin_pct,
		display->urgent_cmd_config.delay_counter_usec,
		display->urgent_cmd_config.urgent_value);
	dev_dbg(hw->dev, "%s: sys_counter = %#x, sys_delay_counter = %#x, urgent_value = %#x\n",
		__func__, sys_counter, sys_delay_counter, urgent_value);
	trace_disp_urgent_cmd_config(hw_id, output_id, mode, sys_counter, sys_delay_counter,
				     urgent_value);
}

static void dc_hw_configure_vid_urgent(struct dc_hw *hw, u8 hw_id, u8 output_id,
				       struct dc_hw_display_mode *mode, bool force_disable)
{
	struct dc_hw_display *display = &hw->display[hw_id];
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_dc_urgent_vid_config *urgent_vid_override;
	u32 urgent_value;
	u32 qos_thresh_0, qos_thresh_1, qos_thresh_2;
	u32 urgent_thresh_0, urgent_thresh_1, urgent_thresh_2;
	u32 urgent_low_thresh, urgent_high_thresh;
	u32 healthy_thresh;

	urgent_value =
		dc_read(hw, VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_VALUE_Address));

	if (dc->urgent_override[hw_id].override_urgent) {
		urgent_vid_override = &dc->urgent_override[hw_id].urgent_vid_config;
		memcpy(&display->urgent_vid_config, urgent_vid_override,
		       sizeof(*urgent_vid_override));
	}

	if (force_disable || !display->urgent_vid_config.enable) {
		urgent_value = VS_SET_FIELD(urgent_value, DCREG_SH_OUTPUT0_URGENT_VALUE, ENABLE,
					    DCREG_SH_OUTPUT0_URGENT_VALUE_ENABLE_DISABLED);
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_VALUE_Address),
			 urgent_value);
		return;
	}

	qos_thresh_0 = display->urgent_vid_config.qos_thresh_0;
	qos_thresh_1 = display->urgent_vid_config.qos_thresh_1;
	qos_thresh_2 = display->urgent_vid_config.qos_thresh_2;
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, QOS_THRESHOLD0_Address),
		 qos_thresh_0);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, QOS_THRESHOLD1_Address),
		 qos_thresh_1);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, QOS_THRESHOLD2_Address),
		 qos_thresh_2);

	urgent_thresh_0 = display->urgent_vid_config.urgent_thresh_0;
	urgent_thresh_1 = display->urgent_vid_config.urgent_thresh_1;
	urgent_thresh_2 = display->urgent_vid_config.urgent_thresh_2;
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_THRESHOLD0_Address),
		 urgent_thresh_0);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_THRESHOLD1_Address),
		 urgent_thresh_1);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_THRESHOLD2_Address),
		 urgent_thresh_2);

	urgent_low_thresh = display->urgent_vid_config.urgent_low_thresh;
	urgent_high_thresh = display->urgent_vid_config.urgent_high_thresh;
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_LOW_Address),
		 urgent_low_thresh);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_HIGH_Address),
		 urgent_high_thresh);

	healthy_thresh = display->urgent_vid_config.healthy_thresh;
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, HEALTHY_THRESHOLD_Address),
		 healthy_thresh);

	urgent_value = VS_SET_FIELD(urgent_value, DCREG_SH_OUTPUT0_URGENT_VALUE, ENABLE,
				    DCREG_SH_OUTPUT0_URGENT_VALUE_ENABLE_ENABLED);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_VALUE_Address),
		 urgent_value);

	dev_dbg(hw->dev, "qos threshold = %#x, %#x, %#x\n", qos_thresh_0, qos_thresh_1,
		qos_thresh_2);
	dev_dbg(hw->dev,
		"urgent threshold = %#x %#x %#x, lo = %#x hi = %#x, healthy threshold = %#x\n",
		urgent_thresh_0, urgent_thresh_1, urgent_thresh_2, urgent_low_thresh,
		urgent_high_thresh, healthy_thresh);
	trace_disp_urgent_vid_config(hw_id, output_id, qos_thresh_0, qos_thresh_1, qos_thresh_2,
				     urgent_thresh_0, urgent_thresh_1, urgent_thresh_2,
				     urgent_low_thresh, urgent_high_thresh, healthy_thresh);
}

static void display_set_mode(struct dc_hw *hw, u8 hw_id, u8 output_id,
			     struct dc_hw_display_mode *mode)
{
	u32 config = 0;
	u32 vfp_height = 0;
	u32 ipi_format;
	u32 ipi_colordepth;

	vfp_height = mode->v_sync_start - mode->v_active;

	if (hw_id >= HW_DISPLAY_4)
		return;

	trace_disp_set_mode(hw_id, mode);

	/* output path clock configuration*/
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_OUTPUT, output_id, CLK_EN_Address),
		 mode->enable);

	mode->is_yuv = false;
	if (mode->enable) {
		switch (mode->bus_format) {
		case MEDIA_BUS_FMT_RGB666_1X18:
		case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
			/* PANELx_FORMAT format is the same for hw ids 0-3 */
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     RGB666);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS6;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_RGB;
			break;
		case MEDIA_BUS_FMT_RGB888_1X24:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     RGB888);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS8;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_RGB;
			break;
		case MEDIA_BUS_FMT_RGB101010_1X30:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     RGB101010);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS10;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_RGB;
			break;
		case MEDIA_BUS_FMT_RGB121212_1X36:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     RGB121212);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS12;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_RGB;
			break;
		case MEDIA_BUS_FMT_UYVY8_1X16:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     YUV422_8BIT);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS8;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_YCBCR422;
			mode->is_yuv = true;
			break;
		case MEDIA_BUS_FMT_UYVY10_1X20:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     YUV422_10BIT);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS10;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_YCBCR422;
			mode->is_yuv = true;
			break;
		case MEDIA_BUS_FMT_YUV8_1X24:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     YUV444_8BIT);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS8;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_YCBCR444;
			mode->is_yuv = true;
			break;
		case MEDIA_BUS_FMT_YUV10_1X30:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     YUV444_10BIT);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS10;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_YCBCR444;
			mode->is_yuv = true;
			break;
		default:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_PANEL2_FORMAT, OUTPUT_FORMAT,
						     RGB888);
			ipi_colordepth = DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_VALUE_BITS8;
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_RGB;
			break;
		}

		/* output format configuration */
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, FORMAT_Address), config);

		if (mode->dsc_enable)
			ipi_format = DCREG_SH_OUTPUT0_IPI_FORMAT_VALUE_COMPRESS_DATA;

		if (output_id == HW_OUTIF_0) {
			dc_write(hw, DCREG_SH_OUTPUT0_IPI_FORMAT_Address, ipi_format);
			dc_write(hw, DCREG_SH_OUTPUT0_IPI_COLOR_DEPTH_Address, ipi_colordepth);
		} else if (output_id == HW_OUTIF_1) {
			dc_write(hw, DCREG_SH_OUTPUT1_IPI_FORMAT_Address, ipi_format);
			dc_write(hw, DCREG_SH_OUTPUT1_IPI_COLOR_DEPTH_Address, ipi_colordepth);
		}

		/* If Variable Refresh Rate is enabled, just need to set the VFP height(next frame work), Others will not change */
		if (mode->vrr_enable) {
			dc_write(hw,
				 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id,
						     TIMING_VFP_HEIGHT_Address),
				 vfp_height);
		}

		/* horizontal sync pulse polarity & sync width configuration */
		dc_write(hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, TIMING_HSYNC_Address),
			 mode->h_sync_polarity ? 0 : 1);
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_HS_WIDTH_Address),
			 mode->h_sync_end - mode->h_sync_start);
		/* horizontal back porch configuration */
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_HBP_WIDTH_Address),
			 mode->h_total - mode->h_sync_end);
		/* horizontal active width configuration */
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_HA_WIDTH_Address),
			 mode->h_active);
		/* horizontal front porch configuration */
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_HFP_WIDTH_Address),
			 mode->h_sync_start - mode->h_active);

		/* vertical sync pulse polarity & sync width configuration */
		dc_write(hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, TIMING_VSYNC_Address),
			 mode->v_sync_polarity ? 0 : 1);
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_VS_HEIGHT_Address),
			 mode->v_sync_end - mode->v_sync_start);
		/* vertical back porch configuration */
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_VBP_HEIGHT_Address),
			 mode->v_total - mode->v_sync_end);
		/* vertical active height configuration */
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_VA_HEIGHT_Address),
			 mode->v_active);
		/* vertical front porch configuration */
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, TIMING_VFP_HEIGHT_Address),
			 vfp_height);

		/* set the output mode
		 * For now, just set the video modo, command trigger mode or command auto mode, command sync.
		 * TODO: Other settings
		 */
		if (mode->output_mode & VS_OUTPUT_MODE_CMD) {
			config = dc_read(hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, Address));
			dc_write(hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, Address),
				 VS_SET_FIELD_VALUE(config, DCREG_OUTPUT0, WORK_MODE, COMMAND));

			config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id,
								  CONFIG_COMMAND_OPT_Address));

			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTPUT0, CONFIG_COMMAND_OPT_SYNC,
						     DISABLED);

			if (mode->output_mode & VS_OUTPUT_MODE_CMD_AUTO)
				config = VS_SET_FIELD_PREDEF(config, DCREG_OUTPUT0,
							     CONFIG_COMMAND_OPT_OPTION, AUTO_MODE);
			else
				config = VS_SET_FIELD_PREDEF(config, DCREG_OUTPUT0,
							     CONFIG_COMMAND_OPT_OPTION,
							     TRIGGER_MODE);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id,
						      CONFIG_COMMAND_OPT_Address),
				 config);

			if (mode->output_mode & VS_OUTPUT_MODE_CMD_DE_SYNC)
				dc_write(hw,
					 VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id,
							      DE_SYNC_MODE_Address),
					 true);
			else
				dc_write(hw,
					 VS_SET_PANEL01_FIELD(DCREG_OUTPUT, output_id,
							      DE_SYNC_MODE_Address),
					 false);

			/* Adding underrun configuration for command mode only */
			dc_hw_configure_cmd_underrun(hw, hw_id, output_id, mode);
			dc_hw_configure_cmd_urgent(hw, hw_id, output_id, mode,
						   !is_urgent_enabled());
		} else {
			config = dc_read(hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, Address));
			dc_write(hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, Address),
				 VS_SET_FIELD_VALUE(config, DCREG_OUTPUT0, WORK_MODE, VIDEO));
			dc_hw_configure_vid_urgent(hw, hw_id, output_id, mode,
						   !is_urgent_enabled());
		}

		/* set panel image size */
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, WIDTH_Address),
			 mode->h_active);
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, HEIGHT_Address),
			 mode->v_active);
	}

	return;
}

static void display_set_bld_size(struct dc_hw *hw, u8 hw_id, struct dc_hw_size *bld_size)
{
	trace_config_hw_display_feature_en("BLD_SIZE", hw_id, bld_size->enable);

	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, IMAGE_WIDTH_Address),
		 bld_size->width);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, IMAGE_HEIGHT_Address),
		 bld_size->height);

	bld_size->dirty = false;
}

static void display_set_dither_size(struct dc_hw *hw, u8 hw_id, struct dc_hw_size *size)
{
	trace_config_hw_display_feature_en("DITHER_SIZE", hw_id, size->enable);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, DITHER_WIDTH_Address),
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_DITHER_WIDTH, VALUE, size->width));
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, DITHER_HEIGHT_Address),
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_DITHER_HEIGHT, VALUE, size->height));

	size->dirty = false;
}

static void display_set_gamma(struct dc_hw *hw, u8 hw_id, struct dc_hw_gamma *gamma)
{
	u32 config = 0, reg_offset = 0x04;
	u32 i, j, seg_offset[3] = { 0 };

	seg_offset[1] = DCREG_SH_PANEL0_GAMMA_EX_SEGMENT_COUNT_Address -
			DCREG_SH_PANEL0_GAMMA_SEGMENT_COUNT_Address;
	seg_offset[2] = DCREG_SH_PANEL0_GAMMA_EX1_SEGMENT_COUNT_Address -
			DCREG_SH_PANEL0_GAMMA_SEGMENT_COUNT_Address;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, GAMMA, gamma->enable[0]);
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, GAMMA_EX, gamma->enable[1]);
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, GAMMA_EX1, gamma->enable[2]);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address), config);

	trace_config_hw_display_feature("GAMMA", hw_id, "en:[%d %d %d]", gamma->enable[0],
					gamma->enable[1], gamma->enable[2]);

	for (i = 0; i < 3; i++) {
		if (!gamma->enable[i])
			continue;

		trace_config_hw_display_feature("GAMMA_DATA", hw_id,
						"lut:%d seg_cnt:%d entry_cnt:%d", i,
						gamma->lut[i].seg_cnt, gamma->lut[i].entry_cnt);
		trace_hw_display_feature_data("GAMMA_DATA", hw_id, "seg_point",
					      (const u8 *)gamma->lut[i].seg_point,
					      sizeof(gamma->lut[i].seg_point));
		trace_hw_display_feature_data("GAMMA_DATA", hw_id, "seg_step",
					      (const u8 *)gamma->lut[i].seg_step,
					      sizeof(gamma->lut[i].seg_step));
		/* segment count start from 0, so the configured value minus 1*/
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, GAMMA_SEGMENT_COUNT_Address) +
				 seg_offset[i],
			 gamma->lut[i].seg_cnt - 1);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      GAMMA_GREEN_SEGMENT_COUNT_Address) +
				 seg_offset[i],
			 gamma->lut[i].seg_cnt - 1);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      GAMMA_BLUE_SEGMENT_COUNT_Address) +
				 seg_offset[i],
			 gamma->lut[i].seg_cnt - 1);

		/* commit the segment points */
		dc_write_u32_blob(hw,
				  VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						       GAMMA_SEGMENT_POINT1_Address) +
					  seg_offset[i],
				  gamma->lut[i].seg_point, gamma->lut[i].seg_cnt - 1);
		dc_write_u32_blob(hw,
				  VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						       GAMMA_GREEN_SEGMENT_POINT1_Address) +
					  seg_offset[i],
				  gamma->lut[i].seg_point, gamma->lut[i].seg_cnt - 1);
		dc_write_u32_blob(hw,
				  VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						       GAMMA_BLUE_SEGMENT_POINT1_Address) +
					  seg_offset[i],
				  gamma->lut[i].seg_point, gamma->lut[i].seg_cnt - 1);

		/* commit the segment steps */
		dc_write_u32_blob(hw,
				  VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						       GAMMA_SEGMENT_STEP_Address) +
					  seg_offset[i],
				  gamma->lut[i].seg_step, gamma->lut[i].seg_cnt);
		dc_write_u32_blob(hw,
				  VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						       GAMMA_GREEN_SEGMENT_STEP_Address) +
					  seg_offset[i],
				  gamma->lut[i].seg_step, gamma->lut[i].seg_cnt);
		dc_write_u32_blob(hw,
				  VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						       GAMMA_BLUE_SEGMENT_STEP_Address) +
					  seg_offset[i],
				  gamma->lut[i].seg_step, gamma->lut[i].seg_cnt);

		/* commit the coef data of RED/GREEN/BULE channel */
		for (j = 0; j < gamma->lut[i].entry_cnt; j++) {
			dc_write_relaxed(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      GAMMA_RED_DATA_Address) +
					 seg_offset[i] + j * reg_offset,
				 gamma->lut[i].data[j].r);
			dc_write_relaxed(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      GAMMA_GREEN_DATA_Address) +
					 seg_offset[i] + j * reg_offset,
				 gamma->lut[i].data[j].g);
			dc_write_relaxed(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      GAMMA_BLUE_DATA_Address) +
					 seg_offset[i] + j * reg_offset,
				 gamma->lut[i].data[j].b);
		}
	}

	gamma->dirty = false;
}

static void display_ex_set_gamma(struct dc_hw *hw, u8 hw_id, struct dc_hw_gamma *gamma)
{
	u32 config = 0, reg_offset = 0x04;
	u32 i, sub_offset;

	switch (hw_id) {
	case HW_DISPLAY_0:
		sub_offset = 0;
		break;
	case HW_DISPLAY_1:
		sub_offset = DCREG_SH_PANEL1_OETF_SEGMENT_COUNT_Address -
			     DCREG_SH_PANEL0_OETF_SEGMENT_COUNT_Address;
		break;
	case HW_DISPLAY_2:
		sub_offset = DCREG_SH_PANEL2_OETF_SEGMENT_COUNT_Address -
			     DCREG_SH_PANEL0_OETF_SEGMENT_COUNT_Address;
		break;
	case HW_DISPLAY_3:
		sub_offset = DCREG_SH_PANEL3_OETF_SEGMENT_COUNT_Address -
			     DCREG_SH_PANEL0_OETF_SEGMENT_COUNT_Address;
		break;
	default:
		gamma->dirty = false;
		return;
	}

	trace_config_hw_display_feature_en("EX_GAMMA", hw_id, gamma->enable[0]);

	config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
	config = (hw_id > HW_DISPLAY_1) ?
			 (VS_SET_FIELD(config, DCREG_SH_PANEL2_CONFIG, OETF, gamma->enable[0])) :
			 VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, OETF, gamma->enable[0]);

	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address), config);

	if (gamma->enable[0]) {
		trace_config_hw_display_feature("EX_GAMMA_DATA", hw_id, "seg_cnt:%d entry_cnt:%d",
						gamma->lut[0].seg_cnt, gamma->lut[0].entry_cnt);
		trace_hw_display_feature_data("EX_GAMMA_DATA", hw_id, "seg_point",
					      (const u8 *)gamma->lut[0].seg_point,
					      sizeof(gamma->lut[0].seg_point));
		trace_hw_display_feature_data("EX_GAMMA_DATA", hw_id, "seg_step",
					      (const u8 *)gamma->lut[0].seg_step,
					      sizeof(gamma->lut[0].seg_step));
		/* segment count start from 0, so the configured value minus 1*/
		dc_write(hw, DCREG_SH_PANEL0_OETF_SEGMENT_COUNT_Address + sub_offset,
			 gamma->lut[0].seg_cnt - 1);

		/* commit the segment points */
		dc_write_u32_blob(hw, DCREG_SH_PANEL0_OETF_SEGMENT_POINT1_Address + sub_offset,
				  gamma->lut[0].seg_point, gamma->lut[0].seg_cnt - 1);

		/* commit the segment steps */
		dc_write_u32_blob(hw, DCREG_SH_PANEL0_OETF_SEGMENT_STEP_Address + sub_offset,
				  gamma->lut[0].seg_step, gamma->lut[0].seg_cnt);

		/* commit the coef data of R/G/B channel */
		for (i = 0; i < gamma->lut[0].entry_cnt; i++) {
			dc_write(hw,
				 DCREG_SH_PANEL0_OETF_DATA_Address + sub_offset + (i * reg_offset),
				 gamma->lut[0].data[i].r);
		}
	}

	gamma->dirty = false;
}

static void display_set_ltm_luma_get(struct dc_hw *hw, u8 hw_id,
				     struct dc_hw_ltm_luma_get *ltm_luma_get)
{
	ltm_luma_get->ltm_luma_get->ave = dc_read(
		hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_LUMA_AVE_LEVEL_Address));

	ltm_luma_get->dirty = false;
}

static void display_set_ltm_cd_get(struct dc_hw *hw, u8 hw_id, struct dc_hw_ltm_cd_get *ltm_cd_get)
{
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;

	for (entry_cnt = 0; entry_cnt < VS_LTM_CD_RESULT_NUM; entry_cnt++) {
		ltm_cd_get->ltm_cd_get->result[entry_cnt] =
			dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id,
							 LTM_CD_HIST_BIN_RESULT_Address) +
					    (entry_cnt * base_offset));
	}

	ltm_cd_get->dirty = false;
}

static void display_set_ltm_hist_get(struct dc_hw *hw, u8 hw_id,
				     struct dc_hw_ltm_hist_get *ltm_hist_get)
{
	u32 entry_cnt = 0;

	for (entry_cnt = 0; entry_cnt < VS_LTM_HIST_RESULT_NUM; entry_cnt++) {
		ltm_hist_get->ltm_hist_get->result[entry_cnt] = dc_read(
			hw, VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id, LTM_HIST_LUT_DATA_Address));
	}

	ltm_hist_get->dirty = false;
}

static void display_set_blur_mask(struct dc_hw *hw, u8 hw_id, struct dc_hw_fb *fb)
{
	trace_config_hw_display_fb_mask("BLUR_MASK", hw_id, fb);
	/* commit blur Mask data address/stride */
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BLUR_MASK_DATA_ADDRESS_Address),
		 (u32)(fb->address & 0xFFFFFFFF));

	dc_write(hw,
		 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BLUR_MASK_DATA_HIGH_ADDRESS_Address),
		 fb->address >> 32);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BLUR_MASK_DATA_STRIDE_Address),
		 fb->stride);

	fb->dirty = false;
}

static void display_set_brightness_mask(struct dc_hw *hw, u8 hw_id, struct dc_hw_fb *fb)
{
	trace_config_hw_display_fb_mask("BRIGHTNESS_MASK", hw_id, fb);
	/* commit brightness Mask data address/stride */
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BRIGHT_MASK_DATA_ADDRESS_Address),
		 (u32)(fb->address & 0xFFFFFFFF));

	dc_write(hw,
		 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BRIGHT_MASK_DATA_HIGH_ADDRESS_Address),
		 fb->address >> 32);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BRIGHT_MASK_DATA_STRIDE_Address),
		 fb->stride);

	fb->dirty = false;
}

static void display_set_qos(struct dc_hw *hw, u8 hw_id, u8 output_id, struct dc_hw_disp_qos *qos)
{
	u8 i;
	u32 config = 0;

	for (i = 0; i < MAX_QOS_LEVLE - 1; i++) {
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, QOS_THRESHOLD0_Address) +
				 0x4 * i,
			 qos->qos_threshold[i]);
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id,
					     URGENT_THRESHOLD0_Address) +
				 0x4 * i,
			 qos->urgent_threshold[i]);
		config = (i == 0) ? VS_SET_FIELD(config, DCREG_BE_QOS, LEVEL0, qos->qos_value[i]) :
			 (i == 1) ? VS_SET_FIELD(config, DCREG_BE_QOS, LEVEL1, qos->qos_value[i]) :
			 (i == 2) ? VS_SET_FIELD(config, DCREG_BE_QOS, LEVEL2, qos->qos_value[i]) :
					  config;
		be_qos_value[i] = qos->qos_value[i];
	}

	config = (i == 3) ? VS_SET_FIELD(config, DCREG_BE_QOS, LEVEL3, qos->qos_value[i]) : config;
	be_qos_value[i] = qos->qos_value[i];
	dc_write_immediate(hw, DCREG_BE_QOS_Address, config);
	dc_write(hw, VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_HIGH_Address),
		 qos->urgent_high);
	dc_write(hw, VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, output_id, URGENT_LOW_Address),
		 qos->urgent_low);
	/* Histogram QOS value set. */
	if (qos->hist_qos_value) {
		dc_write_immediate(hw,
				   VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id, HIST_WB_AW_QOS_Address),
				   VS_SET_FIELD(0, DCREG_PANEL0_HIST_WB_AW_QOS, VALUE,
						qos->hist_qos_value));
		qos->hist_qos_changed = false;
	}

	qos->dirty = false;
}

static void display_set_wb_pos(struct dc_hw *hw, u8 hw_id, struct dc_hw_display_wb *wb)
{
	u32 wb_pos = 0;
	u8 wb_hw_id = hw->info->write_back[wb->wb_id].id;
	/* TODO: hw_id map to output_id, for now the hw_id has no gap, so it can be used fllow */
	u8 output_id = hw->display[hw_id].output_id;

	if (wb_hw_id >= HW_BLEND_WB)
		return;

	trace_config_hw_wb_feature("WB_POS", wb->wb_id, "en:%d wb_point:%d", wb->enable,
				   wb->wb_point);

	if (wb->enable) {
		wb_busy[wb_hw_id] = true;

		switch (wb->wb_point) {
		case VS_WB_DISP_IN:
			wb_pos = VS_SET_WB_PANEL_FIELD(DCREG_POST_PRO_WB, wb_hw_id,
						       TAPPING_POINT_VALUE_PANEL, hw_id, INPUT);
			break;
		case VS_WB_DISP_CC:
			wb_pos = VS_SET_WB_PANEL01_FIELD(DCREG_POST_PRO_WB, wb_hw_id,
							 TAPPING_POINT_VALUE_PANEL, hw_id, CC_OUT);
			break;
		case VS_WB_DISP_OUT:
			wb_pos = VS_SET_WB_PANEL_FIELD(DCREG_POST_PRO_WB, wb_hw_id,
						       TAPPING_POINT_VALUE_PANEL, hw_id, OUTPUT);
			break;
		case VS_WB_OFIFO_IN:
			wb_pos = VS_SET_WB_PANEL_FIELD(DCREG_POST_PRO_WB, wb_hw_id,
						       TAPPING_POINT_VALUE_OFIFO, output_id, INPUT);
			break;
		case VS_WB_OFIFO_OUT:
			wb_pos = VS_SET_WB_PANEL_FIELD(DCREG_POST_PRO_WB, wb_hw_id,
						       TAPPING_POINT_VALUE_OFIFO, output_id,
						       OUTPUT);
			break;
		default:
			break;
		}
		dc_write_immediate(
			hw, VS_SET_POSTWB_FIELD(DCREG_POST_PRO_WB, wb_hw_id, TAPPING_POINT_Address),
			wb_pos);
		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, wb_hw_id, CONTROL_Address),
			 true);
	} else {
		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, wb_hw_id, CONTROL_Address),
			 false);
		wb_busy[wb_hw_id] = false;
	}

	wb->dirty = false;
}

static void display_ex_set_wb_pos(struct dc_hw *hw, u8 hw_id, struct dc_hw_display_wb *wb)
{
	u8 wb_hw_id = wb->wb_id;
	u32 cb8to2_config = 0;

	trace_config_hw_wb_feature("EX_WB_POS", wb->wb_id, "en:%d wb_point:%d", wb->enable,
				   wb->wb_point);

	/* display0 ~ display2 support two writeback position: blend and interface.
	 * display3 support one writeback position: interface.
	 * display4 only support offline write back.
	 */

	if (hw_id == HW_DISPLAY_0 || hw_id == HW_DISPLAY_1 || hw_id == HW_DISPLAY_2) {
		if (wb->wb_point == VS_WB_DISP_IN || display_wb_pos[hw_id] == VS_WB_DISP_IN) {
			if (wb->enable) {
				/* Occupied target free crossbar and wb path */
				wb_busy[wb_hw_id] = true;

				/* Record the wb position of each display */
				display_wb_pos[hw_id] = VS_WB_DISP_IN;

				/* config 8to2 crossbar */
				cb8to2_config = cb8to2_store_value;
				cb8to2_config =
					wb_hw_id == HW_WB_0 ?
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX0_OUT, 4 + hw_id) :
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX1_OUT, 4 + hw_id);
				dc_write(hw, DCREG_WB_CROSSBAR8_TO2_Address, cb8to2_config);
				cb8to2_store_value = cb8to2_config;

				dc_write(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
							    WB_CONFIG_Address),
					 true);
			} else {
				/* Free occupied crossbar and wb path */
				wb_busy[wb_hw_id] = false;
				display_wb_pos[hw_id] = VS_WB_POS_CNT;
				dc_write(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
							    WB_CONFIG_Address),
					 false);
			}
		} else if (wb->wb_point == VS_WB_DISP_OUT ||
			   display_wb_pos[hw_id] == VS_WB_DISP_OUT) {
			if (wb->enable) {
				wb_busy[wb_hw_id] = true;
				display_wb_pos[hw_id] = VS_WB_DISP_OUT;

				/* config 8to2 crossbar */
				/* mux0 and mux1 can not equal 0 togather. */
				cb8to2_store_value =
					(wb_hw_id == HW_WB_0) ?
						VS_SET_FIELD(cb8to2_store_value,
							     DCREG_WB_CROSSBAR8_TO2, MUX1_OUT, 6) :
						VS_SET_FIELD(cb8to2_store_value,
							     DCREG_WB_CROSSBAR8_TO2, MUX0_OUT, 6);

				cb8to2_config = cb8to2_store_value;
				cb8to2_config =
					wb_hw_id == HW_WB_0 ?
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX0_OUT, 0) :
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX1_OUT, 0);
				dc_write(hw, DCREG_WB_CROSSBAR8_TO2_Address, cb8to2_config);
				cb8to2_store_value = cb8to2_config;

				/* config 4to4_crossbar[3] */
				dc_write(hw, DCREG_OUTPUT3_CROSSBAR4_TO4_Address,
					 VS_SET_FIELD(0x0, DCREG_OUTPUT3, CROSSBAR4_TO4_MUX_OUT,
						      hw_id) |
						 VS_SET_FIELD(0x0, DCREG_OUTPUT3,
							      CROSSBAR4_TO4_PATH, true));
				dc_write(hw, DCREG_SH_OUTPUT3_WRITE_BACK_Address,
					 VS_SET_FIELD(0, DCREG_SH_OUTPUT3, WRITE_BACK_ENABLE,
						      true));
			} else {
				wb_busy[wb_hw_id] = false;
				display_wb_pos[hw_id] = VS_WB_POS_CNT;

				dc_write(hw, DCREG_SH_OUTPUT3_WRITE_BACK_Address,
					 VS_SET_FIELD(0, DCREG_SH_OUTPUT3, WRITE_BACK_ENABLE,
						      false));
			}
		}
	} else if (hw_id == HW_DISPLAY_3) {
		if (wb->enable) {
			wb_busy[wb_hw_id] = true;

			/* config 8to2 crossbar */
			cb8to2_store_value =
				(wb_hw_id == HW_WB_0) ?
					VS_SET_FIELD(cb8to2_store_value, DCREG_WB_CROSSBAR8_TO2,
						     MUX1_OUT, 6) :
					VS_SET_FIELD(cb8to2_store_value, DCREG_WB_CROSSBAR8_TO2,
						     MUX0_OUT, 6);

			cb8to2_config = cb8to2_store_value;
			cb8to2_config = wb_hw_id == HW_WB_0 ?
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX0_OUT, 0) :
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX1_OUT, 0);
			dc_write(hw, DCREG_WB_CROSSBAR8_TO2_Address, cb8to2_config);
			cb8to2_store_value = cb8to2_config;

			/* config 4to4_crossbar[3] */
			dc_write(hw, DCREG_OUTPUT3_CROSSBAR4_TO4_Address,
				 VS_SET_FIELD(0x0, DCREG_OUTPUT3, CROSSBAR4_TO4_MUX_OUT, hw_id) |
					 VS_SET_FIELD(0x0, DCREG_OUTPUT3, CROSSBAR4_TO4_PATH,
						      true));

			dc_write(hw, DCREG_SH_OUTPUT3_WRITE_BACK_Address,
				 VS_SET_FIELD(0, DCREG_SH_OUTPUT3, WRITE_BACK_ENABLE, true));
		} else {
			wb_busy[wb_hw_id] = false;
			dc_write(hw, DCREG_SH_OUTPUT3_WRITE_BACK_Address,
				 VS_SET_FIELD(0, DCREG_SH_OUTPUT3, WRITE_BACK_ENABLE, false));
		}
	} else {
		if (wb->enable) {
			wb_busy[wb_hw_id] = true;

			/* config 8to2 crossbar */
			cb8to2_config = cb8to2_store_value;
			cb8to2_config = wb_hw_id == HW_WB_0 ?
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX0_OUT, 0x7) :
						VS_SET_FIELD(cb8to2_config, DCREG_WB_CROSSBAR8_TO2,
							     MUX1_OUT, 0x7);
			dc_write(hw, DCREG_WB_CROSSBAR8_TO2_Address, cb8to2_config);
			cb8to2_store_value = cb8to2_config;
		} else {
			wb_busy[wb_hw_id] = false;
		}
	}

	wb->dirty = false;
}

static void wb_enable_shadow(struct dc_hw *hw, u8 hw_id, bool enable)
{
	u32 config = 0;

	if (hw_id < HW_BLEND_WB) {
		config = dc_read_immediate(hw, VS_SET_POSTWB_FIELD(DCREG_POST_PRO_WB, hw_id,
								   CONFIG_Address));
		dc_write_immediate(
			hw, VS_SET_POSTWB_FIELD(DCREG_POST_PRO_WB, hw_id, CONFIG_Address),
			VS_SET_FIELD(config, DCREG_POST_PRO_WB0_CONFIG, REG_SWITCH, enable));
	} else {
		config = dc_read_immediate(hw, DCREG_BLD_WB_CONFIG_Address);
		dc_write_immediate(hw, DCREG_BLD_WB_CONFIG_Address,
				   VS_SET_FIELD(config, DCREG_BLD_WB_CONFIG, REG_SWITCH, enable));
	}
}

static void wb_ex_enable_shadow(struct dc_hw *hw, u8 hw_id, bool enable)
{
	u32 offset = hw_id * (DCREG_WB1_CONFIG_Address - DCREG_WB0_CONFIG_Address);
	u32 config = 0;

	config = dc_read_immediate(hw, DCREG_WB0_CONFIG_Address + offset);
	dc_write_immediate(hw, DCREG_WB0_CONFIG_Address + offset,
			   VS_SET_FIELD(config, DCREG_WB0_CONFIG, REG_SWITCH, enable));
}

static void wb_set_fb(struct dc_hw *hw, u8 hw_id, struct dc_hw_wb *wb)
{
	u32 config = 0;
	struct dc_hw_fb *fb = &wb->fb;

	trace_config_hw_wb_fb("WB_FB", hw_id, fb);

	/* writeback module irq disable will be pending to job cleanup callback */
	if (fb->enable)
		dc_hw_enable_wb_irqs(hw, hw_id, fb->enable);

	if (!fb->enable) {
		if (hw_id < HW_BLEND_WB) {
			dc_write(hw,
				 VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, hw_id, CONTROL_Address),
				 false);
		} else {
			config = dc_read(hw, DCREG_SH_BLD_WB_CONFIG_Address);
			config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_CONFIG, ENABLE, false);
			dc_write(hw, DCREG_SH_BLD_WB_CONFIG_Address, config);
		}
		fb->dirty = false;
		return;
	}

	if (hw_id < HW_BLEND_WB) {
		/* TODO (b/436294486) Always program the WB in two path mode */
		dc_write_immediate(hw, DCREG_POST_PRO_WB_PATH_NUM_Address,
				   DCREG_POST_PRO_WB_PATH_NUM_VALUE_WB_TWO_PATH);

		if (wb->info->wb_stall)
			dc_write(hw, DCREG_POST_PRO_WB_STALL_Address,
				 DCREG_POST_PRO_WB_STALL_VALUE_ENABLED);

		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, hw_id, DEST_STRIDE_Address),
			 fb->stride);
		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, hw_id, DEST_ADDRESS_Address),
			 (u32)fb->address);
		dc_write(hw,
			 VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, hw_id, DEST_HADDRESS_Address),
			 fb->address >> 32);

		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_POST_PRO_WB, hw_id, CONTROL_Address),
			 true);
	} else {
		config = dc_read(hw, DCREG_SH_BLD_WB_CONFIG_Address);
		config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_CONFIG, ENABLE, true);
		config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_CONFIG, TILE_MODE, fb->tile_mode);
		config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_CONFIG, UV_SWIZZLE, fb->uv_swizzle);
		config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_CONFIG, SWIZZLE, fb->swizzle);
		dc_write(hw, DCREG_SH_BLD_WB_CONFIG_Address, config);

		dc_write(hw, DCREG_SH_BLD_WB_FORMAT_Address, fb->format);

		dc_write(hw, DCREG_SH_BLD_WB_WIDTH_Address, fb->width);
		dc_write(hw, DCREG_SH_BLD_WB_HEIGHT_Address, fb->height);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_ADDRESS_Address, (u32)fb->address);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_HADDRESS_Address, (fb->address >> 32) & 0xFF);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_UPLANE_ADDRESS_Address, (u32)fb->u_address);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_UPLANE_HADDRESS_Address,
			 (fb->u_address >> 32) & 0xFF);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_VPLANE_ADDRESS_Address, (u32)fb->v_address);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_VPLANE_HADDRESS_Address,
			 (fb->v_address >> 32) & 0xFF);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_STRIDE_Address, fb->stride);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_UPLANE_STRIDE_Address, fb->u_stride);
		dc_write(hw, DCREG_SH_BLD_WB_DEST_VPLANE_STRIDE_Address, fb->v_stride);

		config = VS_SET_FIELD(0, DCREG_BE_INTR_STATUS1_BLD_WB, FRM_DONE, 1);
		dc_write(hw, DCREG_BE_INTR_STATUS1_Address, config);
	}

	fb->dirty = false;
}

static void wb_set_r2y(struct dc_hw *hw, u8 hw_id, struct dc_hw_r2y *r2y_conf)
{
	u32 wb_config;
	u32 r2y_config = 0;

	wb_config = dc_read(hw, DCREG_SH_BLD_WB_CONFIG_Address);

	if (r2y_conf->enable) {
		wb_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_BLD_WB_CONFIG, R2Y, ENABLED);


		switch (r2y_conf->mode) {
		case VS_CSC_CM_USR:
			r2y_config = VS_SET_FIELD_PREDEF(
				r2y_config, DCREG_SH_BLD_WB_R2Y_CONFIG, MODE, PROGRAMMABLE);
			break;
		case VS_CSC_CM_L2L:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config,
							    DCREG_SH_BLD_WB_R2Y_CONFIG, MODE,
							    LIMIT_RGB_2_LIMIT_YUV);
			break;
		case VS_CSC_CM_L2F:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config,
							    DCREG_SH_BLD_WB_R2Y_CONFIG, MODE,
							    LIMIT_RGB_2_FULL_YUV);
			break;
		case VS_CSC_CM_F2L:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config,
							    DCREG_SH_BLD_WB_R2Y_CONFIG, MODE,
							    FULL_RGB_2_LIMIT_YUV);
			break;
		case VS_CSC_CM_F2F:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config,
							    DCREG_SH_BLD_WB_R2Y_CONFIG, MODE,
							    FULL_RGB_2_FULL_YUV);
			break;
		default:
			break;
		}

		switch (r2y_conf->gamut) {
		case VS_CSC_CG_601:
			r2y_config = VS_SET_FIELD_PREDEF(
				r2y_config, DCREG_SH_BLD_WB_R2Y_CONFIG, GAMUT, BT601);
			break;
		case VS_CSC_CG_709:
			r2y_config = VS_SET_FIELD_PREDEF(
				r2y_config, DCREG_SH_BLD_WB_R2Y_CONFIG, GAMUT, BT709);
			break;
		case VS_CSC_CG_2020:
			r2y_config = VS_SET_FIELD_PREDEF(
				r2y_config, DCREG_SH_BLD_WB_R2Y_CONFIG, GAMUT, BT2020);
			break;
		case VS_CSC_CG_P3:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config,
							    DCREG_SH_BLD_WB_R2Y_CONFIG, GAMUT, P3);
			break;
		case VS_CSC_CG_SRGB:
			r2y_config = VS_SET_FIELD_PREDEF(
				r2y_config, DCREG_SH_BLD_WB_R2Y_CONFIG, GAMUT, SRGB);
			break;
		default:
			break;
		}
		dc_write(hw, DCREG_SH_BLD_WB_R2Y_CONFIG_Address, r2y_config);

		if (r2y_conf->mode == VS_CSC_CM_USR)
			dc_write_u32_blob(hw, DCREG_SH_BLD_WB_RGB_TO_YUV_COEF0_Address,
					  r2y_conf->coef, VS_MAX_R2Y_COEF_NUM);
	} else {
		wb_config = VS_SET_FIELD_PREDEF(wb_config, DCREG_SH_BLD_WB_CONFIG, R2Y, DISABLED);
	}
	dc_write(hw, DCREG_SH_BLD_WB_CONFIG_Address, wb_config);
}

static void wb_ex_set_fb(struct dc_hw *hw, u8 hw_id, struct dc_hw_wb *wb)
{
	u32 offset = hw_id * (DCREG_WB1_CONFIG_Address - DCREG_WB0_CONFIG_Address);
	u32 config = 0;
	u8 addr_ext[3] = { 0 };
	struct dc_hw_fb *fb = &wb->fb;

	trace_config_hw_wb_fb("EX_WB_FB", hw_id, fb);

	if (fb->enable) {
		switch (fb->format) {
		case WB_FORMAT_RGB888:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT, RGB888);
			break;
		case WB_FORMAT_ARGB8888:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT,
						     ARGB8888);
			break;
		case WB_FORMAT_XRGB8888:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT,
						     XRGB8888);
			break;
		case WB_FORMAT_A2RGB101010:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT,
						     A2RGB101010);
			break;
		case WB_FORMAT_X2RGB101010:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT,
						     X2RGB101010);
			break;
		case WB_FORMAT_NV12:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT, NV12);
			break;
		case WB_FORMAT_P010:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT, P010);
			break;
		default:
			config = VS_SET_FIELD_PREDEF(0, DCREG_SH_WB0_FORMAT, OUTPUT_FORMAT,
						     ARGB8888);
			break;
		}

		/* write-back format configuration */
		dc_write(hw, DCREG_SH_WB0_FORMAT_Address + offset, config);

		/* address configuration */
		dc_write(hw, DCREG_SH_WB0_DEST_ADDRESS_Address + offset,
			 (u32)(fb->address & 0xFFFFFFFF));
		dc_write(hw, DCREG_SH_WB0_DEST_UPLANE_ADDRESS_Address + offset,
			 (u32)(fb->u_address & 0xFFFFFFFF));
		dc_write(hw, DCREG_SH_WB0_DEST_VPLANE_ADDRESS_Address + offset,
			 (u32)(fb->v_address & 0xFFFFFFFF));

		addr_ext[0] = (u8)((fb->address >> 32) & 0xFF);
		addr_ext[1] = (u8)((fb->u_address >> 32) & 0xFF);
		addr_ext[2] = (u8)((fb->v_address >> 32) & 0xFF);

		if (addr_ext[0])
			dc_write(hw, DCREG_SH_WB0_DEST_HADDRESS_Address + offset, addr_ext[0]);
		if (addr_ext[1])
			dc_write(hw, DCREG_SH_WB0_DEST_UPLANE_HADDRESS_Address + offset,
				 addr_ext[1]);
		if (addr_ext[2])
			dc_write(hw, DCREG_SH_WB0_DEST_VPLANE_HADDRESS_Address + offset,
				 addr_ext[2]);

		/* stride/size configuration */
		dc_write(hw, DCREG_SH_WB0_DEST_STRIDE_Address + offset, fb->stride);
		dc_write(hw, DCREG_SH_WB0_DEST_UPLANE_STRIDE_Address + offset, fb->u_stride);
		dc_write(hw, DCREG_SH_WB0_DEST_VPLANE_STRIDE_Address + offset, fb->v_stride);

		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_WB, hw_id, IMAGE_WIDTH_Address),
			 fb->width & 0xFFFF);
		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_WB, hw_id, IMAGE_HEIGHT_Address),
			 fb->height & 0xFFFF);
		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_WB, hw_id, SCALER_DEST_WIDTH_Address),
			 fb->width & 0xFFFF);
		dc_write(hw, VS_SET_POSTWB_FIELD(DCREG_SH_WB, hw_id, SCALER_DEST_HEIGHT_Address),
			 fb->height & 0xFFFF);

		/* Output3 image width height default setting.  */
		dc_write(hw, DCREG_SH_OUTPUT3_IMAGE_WIDTH_Address, fb->width & 0xFFFF);
		dc_write(hw, DCREG_SH_OUTPUT3_IMAGE_HEIGHT_Address, fb->height & 0xFFFF);

		/* enable/swizzle configuration */
		dc_write(hw, DCREG_WB0_START_Address + offset,
			 VS_SET_FIELD(0, DCREG_WB0_START, START, fb->enable));

		config = dc_read(hw, DCREG_SH_WB0_CONFIG_Address + offset);
		config = VS_SET_FIELD(config, DCREG_SH_WB0_CONFIG, UV_SWIZZLE, fb->uv_swizzle);
		config = VS_SET_FIELD(config, DCREG_SH_WB0_CONFIG, SWIZZLE, fb->swizzle);
		config = VS_SET_FIELD(config, DCREG_SH_WB0_CONFIG, TILE_MODE, fb->tile_mode);
		dc_write(hw, DCREG_SH_WB0_CONFIG_Address + offset, config);
	} else {
		dc_write(hw, DCREG_WB0_START_Address + offset,
			 VS_SET_FIELD(0, DCREG_WB0_START, START, 0));
	}

	fb->dirty = false;
}

void dc_hw_plane_set_secure_bits(struct dc_hw *hw, u8 display_id, bool secure_bit_en)
{
	struct dc_hw_plane *plane;
	u8 layer_num = hw->info->layer_num;
	u32 i;

	for (i = 0; i < layer_num; i++) {
		plane = &hw->plane[i];

		if (plane->fb.display_id != display_id)
			continue;

		if (!plane->config_status)
			continue;

		/**
		 * If secure_bit_en is true, only modify secure bit to enable
		 * If secure_bit_en is false, only modify secure bit to disable
		 */

		if (plane->fb.secure_dirty && plane->fb.secure == secure_bit_en)
			plane_set_secure(hw, i, &plane->fb);
	}
}

static void wb_set_qos(struct dc_hw *hw, u8 hw_id, struct dc_hw_wb_qos *qos)
{
	if (hw_id < HW_BLEND_WB)
		dc_write_immediate(hw,
				   VS_SET_POSTWB_FIELD(DCREG_POST_PRO_WB, hw_id, AW_QOS_Address),
				   VS_SET_FIELD(0, DCREG_POST_PRO_WB0_AW_QOS, VALUE,
						qos->qos_value));
	else
		dc_write_immediate(hw, DCREG_BLD_WB_AW_QOS_Address,
				   VS_SET_FIELD(0, DCREG_BLD_WB_AW_QOS, VALUE, qos->qos_value));

	qos->dirty = false;
}

static void plane_commit(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_plane *plane;
	u8 hw_id, layer_num = hw->info->layer_num;
	u32 i, j;

	for (i = 0; i < layer_num; i++) {
		plane = &hw->plane[i];
		hw_id = dc_hw_get_plane_id(i, hw);
		if (plane->fb.display_id != display_id)
			continue;

		if (!plane->config_status)
			continue;

		if (plane->info->rcd_plane) {
			if (plane->rcd_mask.dirty)
				plane_set_rcd_mask(hw, &plane->rcd_mask);
			continue;
		}

		if (plane->fb.dirty)
			plane_set_fb(hw, hw_id, &plane->fb);
		if (plane->fb_ext.dirty)
			plane_set_ext_fb(hw, hw_id, &plane->fb_ext);
		if (plane->pos.dirty)
			plane_set_pos(hw, hw_id, &plane->pos);
		if (plane->y2r.dirty)
			plane_set_y2r(hw, hw_id, &plane->y2r);
		if (plane->roi.dirty)
			plane_set_roi(hw, hw_id, &plane->roi);
		if (plane->scale.dirty)
			plane_set_scale(hw, hw_id, &plane->scale);
		if (plane->lut_3d.dirty)
			plane_set_3d_lut(hw, hw_id, &plane->lut_3d);
		if (plane->std_bld.dirty)
			plane_set_std_bld(hw, hw_id, &plane->std_bld);
		if (plane->clear.dirty)
			plane_set_clear(hw, hw_id, &plane->clear);
		if (plane->sram.dirty)
			plane_set_sram(hw, hw_id, &plane->sram);
		if (plane->qos.dirty)
			plane_set_qos(hw, hw_id, &plane->qos);

		for (j = 0; j < plane->states.num; j++) {
			struct vs_dc_property_state *state = &plane->states.items[j];

			if (!state->dirty)
				continue;

			if (!state->proto->config_hw)
				continue;

			vs_dc_property_config_hw(hw, hw_id, state);
		}
		/* TBD */
	}
}

/**
 * plane_commit_non_shadow - Commit non-shadow plane configurations.
 * @hw: Pointer to the hardware structure.
 * @display_id: Display ID.
 *
 * This function commits plane configurations that are non-shadow.
 */
static void plane_commit_non_shadow(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_plane *plane;
	u8 hw_id, layer_num = hw->info->layer_num;
	u32 i;

	for (i = 0; i < layer_num; i++) {
		plane = &hw->plane[i];
		hw_id = dc_hw_get_plane_id(i, hw);
		if (plane->fb.display_id != display_id)
			continue;

		if (!plane->config_status)
			continue;

		if (plane->info->rcd_plane)
			continue;

		plane_set_outpath(hw, hw_id, &plane->fb);
	}
}

void dc_hw_disable_plane_features(struct dc_hw *hw, u8 display_id)
{
	dc_hw_plane_set_secure_bits(hw, display_id, false);
}

static void display_commit(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display;
	u8 hw_id, display_num = hw->info->display_num;
	u32 i, j;

	for (i = 0; i < display_num; i++) {
		display = &hw->display[i];
		hw_id = hw->info->displays[i].id;
		if (hw_id != display_id)
			continue;

		if (display->bld_size.dirty) {
			display_set_bld_size(hw, hw_id, &display->bld_size);
			display_set_dither_size(hw, hw_id, &display->bld_size);
		}
		if (display->gamma.dirty)
			hw->sub_func->display_gamma(hw, hw_id, &display->gamma);
		if (display->blur_mask.dirty)
			display_set_blur_mask(hw, hw_id, &display->blur_mask);
		if (display->brightness_mask.dirty)
			display_set_brightness_mask(hw, hw_id, &display->brightness_mask);
		if (display->wb.dirty) {
			if (hw->display[display_id].wb_split_dirty)
				display->wb.wb_id = wb_split_id;
			hw->sub_func->display_wb_pos(hw, hw_id, &display->wb);
		}
		if (display->qos.dirty)
			display_set_qos(hw, hw_id, display->output_id, &display->qos);

		// dc property
		for (j = 0; j < display->states.num; j++) {
			struct vs_dc_property_state *state = &display->states.items[j];

			if (!state->dirty)
				continue;
			if (!state->proto->config_hw)
				continue;
			vs_dc_property_config_hw(hw, hw_id, state);
		}

		if (display->ltm_luma_get.dirty)
			display_set_ltm_luma_get(hw, hw_id, &display->ltm_luma_get);
		if (display->ltm_cd_get.dirty)
			display_set_ltm_cd_get(hw, hw_id, &display->ltm_cd_get);
		if (display->ltm_hist_get.dirty)
			display_set_ltm_hist_get(hw, hw_id, &display->ltm_hist_get);

		/* handle histogram channels */
		vs_dc_hist_chans_commit(hw, display_id);
		vs_dc_hist_rgb_commit(hw, display_id);

		/* TBD */
	}
}

static void wb_commit(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_wb *wb;
	u8 hw_id, wb_num = hw->info->wb_num;
	u32 i, j;

	/* avoid wb programming when secure is enabled */
	if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM))
		return;

	for (i = 0; i < wb_num; i++) {
		wb = &hw->wb[i];
		hw_id = hw->info->write_back[i].id;

		if (wb->fb.display_id != display_id)
			continue;

		if (!wb->config_status)
			continue;

		/* program dirty properties */
		if (wb->fb.dirty)
			hw->sub_func->wb_fb(hw, hw_id, wb);

		if (wb->r2y.dirty)
			wb_set_r2y(hw, hw_id, &wb->r2y);

		if (wb->qos.dirty)
			wb_set_qos(hw, hw_id, &wb->qos);

		/* program DC properties */
		for (j = 0; j < wb->states.num; j++) {
			struct vs_dc_property_state *state = &wb->states.items[j];

			if (!state->dirty)
				continue;
			if (!state->proto->config_hw)
				continue;

			vs_dc_property_config_hw(hw, hw_id, state);
		}
	}
}

static const struct dc_hw_funcs hw_func = {
	.set_mode = display_set_mode,
	.wb_commit = wb_commit,
	.plane_commit = plane_commit,
	.plane_commit_non_shadow = plane_commit_non_shadow,
	.display_commit = display_commit,
};

static const struct dc_hw_sub_funcs hw_sub_func[] = {
	{
		.display_gamma = display_set_gamma,
		.display_wb_pos = display_set_wb_pos,
		.wb_fb = wb_set_fb,
		.wb_reg_switch = wb_enable_shadow,
		/* TBD */
	},
	{
		.display_gamma = display_ex_set_gamma,
		.display_wb_pos = display_ex_set_wb_pos,
		.wb_fb = wb_ex_set_fb,
		.wb_reg_switch = wb_ex_enable_shadow,
		/* TBD */
	},
};

void dc_hw_plane_commit(struct dc_hw *hw, u8 display_id)
{
	DPU_ATRACE_BEGIN(__func__);
	hw->func->plane_commit(hw, display_id);
	DPU_ATRACE_END(__func__);
}

void dc_hw_plane_commit_non_shadow(struct dc_hw *hw, u8 display_id)
{
	DPU_ATRACE_BEGIN(__func__);
	hw->func->plane_commit_non_shadow(hw, display_id);
	DPU_ATRACE_END(__func__);
}

void dc_hw_display_commit(struct dc_hw *hw, u8 display_id)
{
	u8 i;

	DPU_ATRACE_BEGIN(__func__);
	hw->func->display_commit(hw, display_id);
	DPU_ATRACE_END(__func__);

	for (i = 0; i < SW_RESET_NUM; i++)
		hw->reset_status[i] = false;
}

void dc_hw_wb_commit(struct dc_hw *hw, u8 display_id)
{
	DPU_ATRACE_BEGIN(__func__);
	hw->func->wb_commit(hw, display_id);
	DPU_ATRACE_END(__func__);
}

/*
 * Handle frame done
 */
void dc_hw_display_frame_done(struct dc_hw *hw, u8 display_id)
{
	dc_hw_read_dsc_status(hw, display_id);
}

/*
 * Handle flip done
 */
void dc_hw_display_flip_done(struct dc_hw *hw, u8 display_id)
{
	/* Disable secure bit after frame transfer is done to prevent b/415715428 */
	dc_hw_plane_set_secure_bits(hw, display_id, false);
	/* histogram channels + rgb */
	vs_dc_hist_flip_done(hw, display_id);
}

/* Get the display address offset based on HW_DISPLAY_0 */
u32 vs_dc_get_display_offset(u32 hw_id)
{
	u32 offset;

	switch (hw_id) {
	case HW_DISPLAY_0:
		offset = DCREG_SH_PANEL0_CONFIG_Address - DCREG_SH_PANEL0_CONFIG_Address;
		break;
	case HW_DISPLAY_1:
		offset = DCREG_SH_PANEL1_CONFIG_Address - DCREG_SH_PANEL0_CONFIG_Address;
		break;
	case HW_DISPLAY_2:
		offset = DCREG_SH_PANEL2_CONFIG_Address - DCREG_SH_PANEL0_CONFIG_Address;
		break;
	case HW_DISPLAY_3:
		offset = DCREG_SH_PANEL3_CONFIG_Address - DCREG_SH_PANEL0_CONFIG_Address;
		break;
	case HW_DISPLAY_4:
		offset = DCREG_SH_BLD_WB_CONFIG_Address - DCREG_SH_PANEL0_CONFIG_Address;
		break;
	default:
		offset = DC_HW_INVALID;
		break;
	}

	return offset;
}

/* Get the panel dither table address offset based on HW_DISPLAY_0 */
u32 vs_dc_get_panel_dither_table_offset(u32 hw_id)
{
	u32 offset;

	switch (hw_id) {
	case HW_DISPLAY_0:
		offset = DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address -
			 DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address;
		break;
	case HW_DISPLAY_1:
		offset = DCREG_SH_PANEL1_DTH_RTABLE_LOW_Address -
			 DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address;
		break;
	case HW_DISPLAY_2:
		offset = DCREG_SH_PANEL2_DTH_RTABLE_LOW_Address -
			 DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address;
		break;
	case HW_DISPLAY_3:
		offset = DCREG_SH_PANEL3_DTH_RTABLE_LOW_Address -
			 DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address;
		break;
	case HW_DISPLAY_4:
		offset = DCREG_SH_BLD_WB_DTH_RTABLE_LOW_Address -
			 DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address;
		break;
	default:
		offset = DC_HW_INVALID;
		break;
	}

	return offset;
}

u32 vs_dc_get_wb_offset(u32 hw_id)
{
	u32 offset = 0x0;

	if (hw_id == HW_WB_1)
		offset = DCREG_SH_WB1_CONFIG_Address - DCREG_SH_WB0_CONFIG_Address;

	return offset;
}

const struct dc_hw_plane *vs_dc_hw_get_plane(const struct dc_hw *hw, u32 hw_id)
{
	u32 i;

	if (!hw || !hw->info)
		return NULL;

	for (i = 0; i < hw->info->plane_num; i++)
		if (hw->plane[i].info->id == hw_id)
			return &hw->plane[i];
	return NULL;
}

const void *vs_dc_hw_get_plane_property(const struct dc_hw *hw, u32 hw_id, const char *prop_name,
					bool *out_enabled)
{
	const struct dc_hw_plane *plane = vs_dc_hw_get_plane(hw, hw_id);

	if (!plane) {
		dev_err(hw->dev, "%s: not found plane %u\n", __func__, hw_id);
		return NULL;
	}
	return vs_dc_property_get_by_name(&plane->states, prop_name, out_enabled);
}

const struct dc_hw_display *vs_dc_hw_get_display(const struct dc_hw *hw, u32 hw_id)
{
	u32 i;

	for (i = 0; i < hw->info->display_num; i++)
		if (hw->display[i].info->id == hw_id)
			return &hw->display[i];
	return NULL;
}

int vs_dc_hw_get_display_id(const struct dc_hw *hw, u32 hw_id)
{
	for (int i = 0; i < hw->info->display_num; i++)
		if (hw->display[i].info->id == hw_id)
			return i;

	/* unlikely */
	return -1;
}

const void *vs_dc_hw_get_display_property(const struct dc_hw *hw, u32 hw_id, const char *prop_name,
					  bool *out_enabled)
{
	const struct dc_hw_display *display = vs_dc_hw_get_display(hw, hw_id);

	if (!display) {
		dev_err(hw->dev, "%s: not found display %u\n", __func__, hw_id);
		return NULL;
	}
	return vs_dc_property_get_by_name(&display->states, prop_name, out_enabled);
}

const struct dc_hw_wb *vs_dc_hw_get_wb(const struct dc_hw *hw, u32 hw_id)
{
	u32 i;

	if (!hw || !hw->info)
		return NULL;

	for (i = 0; i < hw->info->wb_num; i++)
		if (hw->wb[i].info->id == hw_id)
			return &hw->wb[i];
	return NULL;
}

const void *vs_dc_hw_get_wb_property(const struct dc_hw *hw, u32 hw_id, const char *prop_name,
				     bool *out_enabled)
{
	const struct dc_hw_wb *wb = vs_dc_hw_get_wb(hw, hw_id);

	if (!wb) {
		dev_err(hw->dev, "%s: not found writeback %u\n", __func__, hw_id);
		return NULL;
	}
	return vs_dc_property_get_by_name(&wb->states, prop_name, out_enabled);
}

static const struct regmap_access_table *get_allowed_regmap_access_table(struct dc_hw *hw)
{
	if (!hw->info->dump_reg_access_table_secure)
		return hw->info->dump_reg_access_table;
	if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM))
		return hw->info->dump_reg_access_table_secure;
	return hw->info->dump_reg_access_table;
}

int dc_hw_reg_dump_custom(struct dc_hw *hw, struct drm_printer *p, const char *desc, u32 offset,
			  u32 size)
{
	return gs_reg_dump_with_skips(desc, hw->reg_base, offset, size, p,
				      get_allowed_regmap_access_table(hw));
}

int dc_hw_reg_dump(struct dc_hw *hw, struct drm_printer *p, enum dc_hw_reg_bank_type reg_type)
{
	u32 fe0_src, fe1_src, be_src;
	int ret;

	if (reg_type != DC_HW_REG_BANK_ACTIVE && reg_type != DC_HW_REG_BANK_SHADOW)
		return -EINVAL;

	fe0_src = dc_read(hw, DCREG_FE0_REG_READ_SRC_Address);
	fe1_src = dc_read(hw, DCREG_FE1_REG_READ_SRC_Address);
	be_src = dc_read(hw, DCREG_BE_REG_READ_SRC_Address);

	dc_write(hw, DCREG_FE0_REG_READ_SRC_Address,
		 reg_type == DC_HW_REG_BANK_ACTIVE ?
			 VS_SET_FIELD_PREDEF(fe0_src, DCREG_FE0_REG_READ_SRC, SEL, ACTIVE) :
			 VS_SET_FIELD_PREDEF(fe0_src, DCREG_FE0_REG_READ_SRC, SEL, SHADOW));
	dc_write(hw, DCREG_FE1_REG_READ_SRC_Address,
		 reg_type == DC_HW_REG_BANK_ACTIVE ?
			 VS_SET_FIELD_PREDEF(fe1_src, DCREG_FE1_REG_READ_SRC, SEL, ACTIVE) :
			 VS_SET_FIELD_PREDEF(fe1_src, DCREG_FE1_REG_READ_SRC, SEL, SHADOW));
	dc_write(hw, DCREG_BE_REG_READ_SRC_Address,
		 reg_type == DC_HW_REG_BANK_ACTIVE ?
			 VS_SET_FIELD_PREDEF(be_src, DCREG_BE_REG_READ_SRC, SEL, ACTIVE) :
			 VS_SET_FIELD_PREDEF(be_src, DCREG_BE_REG_READ_SRC, SEL, SHADOW));

	ret = dc_hw_reg_dump_custom(hw, p, reg_type == DC_HW_REG_BANK_ACTIVE ?
		"DPU active reg" : "DPU shadow reg", hw->reg_dump_offset, hw->reg_dump_size);

	dc_write(hw, DCREG_FE0_REG_READ_SRC_Address, fe0_src);
	dc_write(hw, DCREG_FE1_REG_READ_SRC_Address, fe1_src);
	dc_write(hw, DCREG_BE_REG_READ_SRC_Address, be_src);

	return ret;
}

void dc_hw_collect_reg_dump(struct device *dev, enum dc_hw_reg_dump_options option,
			    enum dc_hw_reg_bank_type reg_type)
{
	struct drm_printer p = drm_info_printer(dev);
	struct vs_dc *dc = dev_get_drvdata(dev);

	if (option == DC_HW_REG_DUMP_IN_NONE)
		return;

	if (option == DC_HW_REG_DUMP_IN_CONSOLE)
		dc_hw_reg_dump(&dc->hw, &p, DC_HW_REG_BANK_ACTIVE);

	if (option == DC_HW_REG_DUMP_IN_TRACE)
		dc_hw_reg_dump(&dc->hw, NULL, DC_HW_REG_BANK_ACTIVE);
}

void dc_write(struct dc_hw *hw, u32 reg, u32 value)
{
	dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg, value);
	trace_dc_write(reg, value);
	writel(value, hw->reg_base + reg);
}

u32 dc_read(struct dc_hw *hw, u32 reg)
{
	u32 value = readl(hw->reg_base + reg);

	dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg, value);
	trace_dc_read(reg, value);

	return value;
}

void dc_write_u32_blob(struct dc_hw *hw, u32 reg, const u32 *data, u32 size)
{
	const u32 u32_stride = sizeof(u32);
	u32 i;

	for (i = 0; i < size; i++) {
		dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg + u32_stride * i, data[i]);
		trace_dc_write_u32_blob(reg + u32_stride * i, data[i]);
		writel(data[i], hw->reg_base + reg + u32_stride * i);
	}
}

void dc_write_relaxed(struct dc_hw *hw, u32 reg, u32 value)
{
	dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg, value);
	trace_dc_write_relaxed(reg, value);
	writel_relaxed(value, hw->reg_base + reg);
}

u32 dc_read_relaxed(struct dc_hw *hw, u32 reg)
{
	u32 value = readl_relaxed(hw->reg_base + reg);

	dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg, value);
	trace_dc_read_relaxed(reg, value);

	return value;
}

void dc_write_relaxed_u32_blob(struct dc_hw *hw, u32 reg, const u32 *data, u32 size)
{
	const u32 u32_stride = sizeof(u32);
	u32 i;

	for (i = 0; i < size; i++) {
		dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg + u32_stride * i, data[i]);
		trace_dc_write_relaxed_u32_blob(reg + u32_stride * i, data[i]);
		writel_relaxed(data[i], hw->reg_base + reg + u32_stride * i);
	}
}

inline bool vs_dc_display_is_wb(u32 hw_id)
{
	return hw_id == HW_DISPLAY_4;
}

#define LTM_HIST_READ_BUF_SIZE 256

static int dc_hwc_ltm_read_hist_csr(struct dc_hw *hw, u8 hw_id,
				    struct drm_vs_ltm_histogram_data *data)
{
	int rc = 0;
	u16 __user *out_hist;
	u32 hist_read_idx;
	u16 buf[LTM_HIST_READ_BUF_SIZE];
	u32 hist_buf_idx;

	DPU_ATRACE_BEGIN(__func__);
	out_hist = u64_to_user_ptr(data->hist_ptr);
	for (hist_read_idx = 0, hist_buf_idx = 0; hist_read_idx < VS_LTM_HIST_RESULT_NUM;
	     hist_read_idx++) {
		dc_write_immediate(hw, VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id,
					LTM_HIST_LUT_INDEX_Address), hist_read_idx);
		buf[hist_buf_idx++] = dc_read_immediate(hw, VS_SET_PANEL01_FIELD(DCREG_PANEL,
						hw_id, LTM_HIST_LUT_DATA_Address));
		if (hist_buf_idx >= LTM_HIST_READ_BUF_SIZE) {
			if (copy_to_user(out_hist, buf, sizeof(buf))) {
				rc = -EIO;
				dev_err(hw->dev, "%s failed to copy data to user space\n",
					__func__);
				break;
			}
			hist_buf_idx = 0;
			out_hist += LTM_HIST_READ_BUF_SIZE;
		}
	}
	if (!rc && hist_buf_idx > 0 && !copy_to_user(out_hist, buf, hist_buf_idx)) {
		dev_err(hw->dev, "%s failed to copy remain data to user space\n", __func__);
		rc = -EIO;
	}

	DPU_ATRACE_END(__func__);

	return rc;
}

int dc_hw_get_ltm_hist(struct dc_hw *hw, u8 hw_id, struct drm_vs_ltm_histogram_data *data,
		       dma_addr_t (*vs_crtc_get_ltm_hist_dma_addr)(struct device *, u8))
{
	int rc = 0;
	u32 config = 0;

	if (!data)
		return -ENOMEM;

	DPU_ATRACE_BEGIN(__func__);
	/* disable LTM hist update */
	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONFIG, UPDATE, 0);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address), config);

	if (data->read_mode == VS_LTM_HIST_READ_WDMA) {
		if (vs_crtc_get_ltm_hist_dma_addr) {
			data->hist_dma_addr = vs_crtc_get_ltm_hist_dma_addr(hw->dev, hw_id);
		} else {
			dev_err(hw->dev, "%s missing callback to get hist dma addr\n", __func__);
			data->hist_dma_addr = 0;
		}
	} else if (data->read_mode == VS_LTM_HIST_READ_CSR && data->hist_ptr) {
		rc = dc_hwc_ltm_read_hist_csr(hw, hw_id, data);
	} else if (data->read_mode != VS_LTM_HIST_READ_NONE) {
		dev_err(hw->dev, "%s invalid read mode %d\n", __func__, data->read_mode);
	}

	if (!rc && data->read_cd) {
		u32 entry_cnt = 0;
		u32 base_offset = 0x4;

		DPU_ATRACE_BEGIN("ltm_cd_read_csr");
		for (entry_cnt = 0; entry_cnt < VS_LTM_CD_RESULT_NUM; entry_cnt++) {
			data->cd.result[entry_cnt] =
				dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id,
								 LTM_CD_HIST_BIN_RESULT_Address) +
						    (entry_cnt * base_offset));
		}
		DPU_ATRACE_END("ltm_cd_read_csr");
	}

	if (!rc && data->read_luma_ave) {
		data->luma_ave.ave = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
								      LTM_LUMA_AVE_LEVEL_Address));
	}

	/* allow LTM hist update */
	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONFIG, UPDATE, 1);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address), config);

	DPU_ATRACE_END(__func__);

	return rc;
}

static void print_tab(struct seq_file *s, u8 indent)
{
	while (indent--)
		seq_puts(s, "\t");
}

static void print_hw_states(struct seq_file *s, const struct vs_dc_property_state_group *states,
			    u8 indent)
{
	u8 j;

	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d items\n", "states", states->num);
	for (j = 0; j < states->num; j++) {
		const struct vs_dc_property_state *item = &states->items[j];

		print_tab(s, indent + 1);
		seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", item->proto->name,
			   item->enable, item->dirty);
	}
}

static void print_hw_display_mode(struct seq_file *s, const struct dc_hw_display_mode *mode,
				  u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d\n", "mode", mode->enable);
}

static void print_hw_display_bld_size(struct seq_file *s, const struct dc_hw_size *bld_size,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "bld_size", bld_size->enable,
		   bld_size->dirty);
}

static void print_hw_display_data_ext(struct seq_file *s, const struct dc_hw_data_extend *data_ext,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "data_ext", data_ext->enable,
		   data_ext->dirty);
}

static void print_hw_display_gamma(struct seq_file *s, const struct dc_hw_gamma *gamma, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=[%d %d %d], dirty=%d\n", "gamma", gamma->enable[0],
		   gamma->enable[1], gamma->enable[2], gamma->dirty);
}

static void print_hw_display_wb(struct seq_file *s, const struct dc_hw_display_wb *wb, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "wb", wb->enable, wb->dirty);
}

static void print_hw_display_blur_mask(struct seq_file *s, const struct dc_hw_fb *blur_mask,
				       u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "blur_mask", blur_mask->enable,
		   blur_mask->dirty);
}

static void print_hw_display_brightness_mask(struct seq_file *s,
					     const struct dc_hw_fb *brightness_mask,
					     u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "brightness_mask",
		   brightness_mask->enable, brightness_mask->dirty);
}

static void print_hw_display_ltm_enable(struct seq_file *s,
					const struct dc_hw_ltm_enable *ltm_enable,
					u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_enable", ltm_enable->enable,
		   ltm_enable->dirty);
}

static void print_hw_display_ltm_degamma(struct seq_file *s,
					 const struct dc_hw_ltm_xgamma *ltm_degamma,
					 u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_degamma",
		   ltm_degamma->enable, ltm_degamma->dirty);
}

static void print_hw_display_ltm_gamma(struct seq_file *s, const struct dc_hw_ltm_xgamma *ltm_gamma,
				       u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_gamma", ltm_gamma->enable,
		   ltm_gamma->dirty);
}

static void print_hw_display_ltm_luma(struct seq_file *s, const struct dc_hw_ltm_luma *ltm_luma,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_luma", ltm_luma->enable,
		   ltm_luma->dirty);
}

static void print_hw_display_freq_decomp(struct seq_file *s,
					 const struct dc_hw_ltm_freq_decomp *freq_decomp, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "freq_decomp",
		   freq_decomp->enable, freq_decomp->dirty);
}

static void print_hw_display_luma_adj(struct seq_file *s, const struct dc_hw_ltm_luma_adj *luma_adj,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "luma_adj", luma_adj->enable,
		   luma_adj->dirty);
}

static void print_hw_display_grid_size(struct seq_file *s, const struct dc_hw_ltm_grid *grid_size,
				       u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "grid_size", grid_size->enable,
		   grid_size->dirty);
}

static void print_hw_display_af_filter(struct seq_file *s,
				       const struct dc_hw_ltm_af_filter *af_filter,
				       u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "af_filter", af_filter->enable,
		   af_filter->dirty);
}

static void print_hw_display_af_slice(struct seq_file *s, const struct dc_hw_ltm_af_slice *af_slice,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "af_slice", af_slice->enable,
		   af_slice->dirty);
}

static void print_hw_display_af_trans(struct seq_file *s, const struct dc_hw_ltm_af_trans *af_trans,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "af_trans", af_trans->enable,
		   af_trans->dirty);
}

static void print_hw_display_tone_adj(struct seq_file *s, const struct dc_hw_ltm_tone_adj *tone_adj,
				      u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "tone_adj", tone_adj->enable,
		   tone_adj->dirty);
}

static void print_hw_display_ltm_color(struct seq_file *s, const struct dc_hw_ltm_color *ltm_color,
				       u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_color", ltm_color->enable,
		   ltm_color->dirty);
}

static void print_hw_display_ltm_dither(struct seq_file *s,
					const struct dc_hw_ltm_dither *ltm_dither,
					u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_dither", ltm_dither->enable,
		   ltm_dither->dirty);
}

static void print_hw_display_ltm_luma_set(struct seq_file *s,
					  const struct dc_hw_ltm_luma_set *ltm_luma_set, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_luma_set",
		   ltm_luma_set->enable, ltm_luma_set->dirty);
}

static void print_hw_display_ltm_luma_get(struct seq_file *s,
					  const struct dc_hw_ltm_luma_get *ltm_luma_get, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_luma_get",
		   ltm_luma_get->enable, ltm_luma_get->dirty);
}

static void print_hw_display_ltm_cd_set(struct seq_file *s,
					const struct dc_hw_ltm_cd_set *ltm_cd_set,
					u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_cd_set", ltm_cd_set->enable,
		   ltm_cd_set->dirty);
}

static void print_hw_display_ltm_cd_get(struct seq_file *s,
					const struct dc_hw_ltm_cd_get *ltm_cd_get,
					u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_cd_get", ltm_cd_get->enable,
		   ltm_cd_get->dirty);
}

static void print_hw_display_ltm_hist_set(struct seq_file *s,
					  const struct dc_hw_ltm_hist_set *ltm_hist_set, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_hist_set",
		   ltm_hist_set->enable, ltm_hist_set->dirty);
}

static void print_hw_display_ltm_hist_get(struct seq_file *s,
					  const struct dc_hw_ltm_hist_get *ltm_hist_get, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_hist_get",
		   ltm_hist_get->enable, ltm_hist_get->dirty);
}

static void print_hw_display_ltm_ds(struct seq_file *s, const struct dc_hw_ltm_ds *ltm_ds,
				    u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "ltm_ds", ltm_ds->enable,
		   ltm_ds->dirty);
}

static void print_hw_display_hw_hist_chan(struct seq_file *s,
					  const struct dc_hw_hist_chan *hw_hist_chan,
					  u8 indent)
{
	u8 j;
	char name[40] = {};

	for (j = 0; j < VS_HIST_CHAN_IDX_COUNT; j++) {
		snprintf(name, sizeof(name), "hw_hist_chan[%d]", j);
		print_tab(s, indent);
		seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", name,
			   hw_hist_chan[j].enable, hw_hist_chan[j].dirty);
	}
}

static void print_hw_display_hw_hist_rgb(struct seq_file *s,
					 const struct dc_hw_hist_rgb *hw_hist_rgb,
					 u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "hw_hist_rgb",
		   hw_hist_rgb->enable, hw_hist_rgb->dirty);
}

static void print_hw_display_sram_pool(struct seq_file *s, const struct dc_hw_sram_pool *sram_pool,
				       u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": dirty=%d\n", "sram_pool", sram_pool->dirty);
}

static void print_hw_plane_fb(struct seq_file *s, const struct dc_hw_fb *fb, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "fb", fb->enable, fb->dirty);
}

static void print_hw_plane_fb_ext(struct seq_file *s, const struct dc_hw_fb *fb_ext, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "fb_ext", fb_ext->enable,
		   fb_ext->dirty);
}

static void print_hw_plane_pos(struct seq_file *s, const struct dc_hw_position *pos, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "pos", pos->enable, pos->dirty);
}

static void print_hw_plane_rcd_mask(struct seq_file *s, const struct dc_hw_rcd_mask *rcd_mask,
				    u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "rcd_mask", rcd_mask->enable,
		   rcd_mask->dirty);
}

static void print_hw_plane_std_bld(struct seq_file *s, const struct dc_hw_std_bld *std_bld,
				   u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "std_bld", std_bld->enable,
		   std_bld->dirty);
}

static void print_hw_plane_y2r(struct seq_file *s, const struct dc_hw_y2r *y2r, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "y2r", y2r->enable, y2r->dirty);
}

static void print_hw_plane_lut_3d(struct seq_file *s, const struct dc_hw_block *lut_3d, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "lut_3d", lut_3d->enable,
		   lut_3d->dirty);
}

static void print_hw_plane_scale(struct seq_file *s, const struct dc_hw_scale *scale, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "scale", scale->enable,
		   scale->dirty);
}

static void print_hw_plane_roi(struct seq_file *s, const struct dc_hw_roi *roi, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "roi", roi->enable, roi->dirty);
}

static void print_hw_plane_clear(struct seq_file *s, const struct dc_hw_clear *clear, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "clear", clear->enable,
		   clear->dirty);
}

static void print_hw_plane_sram(struct seq_file *s, const struct dc_hw_sram_pool *sram, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": dirty=%d\n", "sram", sram->dirty);
}

static void print_hw_wb_fb(struct seq_file *s, const struct dc_hw_fb *fb, u8 indent)
{
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": enable=%d, dirty=%d\n", "fb", fb->enable, fb->dirty);
}

static void print_hw_display_urgent(struct seq_file *s, const struct dc_hw_display *display,
				    u8 indent)
{
	const struct dc_hw_display_mode *mode = &display->mode;

	if (!mode->enable)
		return;

	print_tab(s, indent);
	if (mode->output_mode & VS_OUTPUT_MODE_CMD) {
		seq_printf(s,
			   PRINT_NAME_WIDTH
			   ": en=%d, h_margin=%u, v_margin=%u, delay=%u, value=%u\n",
			   "cmd_urgent", display->urgent_cmd_config.enable,
			   display->urgent_cmd_config.h_margin_pct,
			   display->urgent_cmd_config.v_margin_pct,
			   display->urgent_cmd_config.delay_counter_usec,
			   display->urgent_cmd_config.urgent_value);
	} else {
		seq_printf(s,
			   PRINT_NAME_WIDTH
			   ": en=%d, qos=(%u %u %u), urgent=(%u %u %u), lv=(%u %u), healthy=%u\n",
			   "vid_urgent", display->urgent_vid_config.enable,
			   display->urgent_vid_config.qos_thresh_0,
			   display->urgent_vid_config.qos_thresh_1,
			   display->urgent_vid_config.qos_thresh_2,
			   display->urgent_vid_config.urgent_thresh_0,
			   display->urgent_vid_config.urgent_thresh_1,
			   display->urgent_vid_config.urgent_thresh_2,
			   display->urgent_vid_config.urgent_high_thresh,
			   display->urgent_vid_config.urgent_low_thresh,
			   display->urgent_vid_config.healthy_thresh);
	}
}

static void print_display_state(struct seq_file *s, const struct dc_hw_display *display, u8 indent)
{
	print_hw_display_mode(s, &display->mode, indent);
	print_hw_display_bld_size(s, &display->bld_size, indent);
	print_hw_display_data_ext(s, &display->data_ext, indent);
	print_hw_display_gamma(s, &display->gamma, indent);
	print_hw_display_wb(s, &display->wb, indent);
	print_hw_display_blur_mask(s, &display->blur_mask, indent);
	print_hw_display_brightness_mask(s, &display->brightness_mask, indent);
	print_hw_display_ltm_enable(s, &display->ltm_enable, indent);
	print_hw_display_ltm_degamma(s, &display->ltm_degamma, indent);
	print_hw_display_ltm_gamma(s, &display->ltm_gamma, indent);
	print_hw_display_ltm_luma(s, &display->ltm_luma, indent);
	print_hw_display_freq_decomp(s, &display->freq_decomp, indent);
	print_hw_display_luma_adj(s, &display->luma_adj, indent);
	print_hw_display_grid_size(s, &display->grid_size, indent);
	print_hw_display_af_filter(s, &display->af_filter, indent);
	print_hw_display_af_slice(s, &display->af_slice, indent);
	print_hw_display_af_trans(s, &display->af_trans, indent);
	print_hw_display_tone_adj(s, &display->tone_adj, indent);
	print_hw_display_ltm_color(s, &display->ltm_color, indent);
	print_hw_display_ltm_dither(s, &display->ltm_dither, indent);
	print_hw_display_ltm_luma_set(s, &display->ltm_luma_set, indent);
	print_hw_display_ltm_luma_get(s, &display->ltm_luma_get, indent);
	print_hw_display_ltm_cd_set(s, &display->ltm_cd_set, indent);
	print_hw_display_ltm_cd_get(s, &display->ltm_cd_get, indent);
	print_hw_display_ltm_hist_set(s, &display->ltm_hist_set, indent);
	print_hw_display_ltm_hist_get(s, &display->ltm_hist_get, indent);
	print_hw_display_ltm_ds(s, &display->ltm_ds, indent);
	print_hw_display_hw_hist_chan(s, display->hw_hist_chan, indent);
	print_hw_display_hw_hist_rgb(s, &display->hw_hist_rgb, indent);
	print_hw_states(s, &display->states, indent);
	print_hw_display_sram_pool(s, &display->sram_pool, indent);
	print_hw_display_urgent(s, display, indent);

	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "output_id", display->output_id);
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "sbs_split_dirty", display->sbs_split_dirty);
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "wb_split_dirty", display->wb_split_dirty);
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "config_status", display->config_status);
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "vblank_count", display->vblank_count);
}

static void print_plane_state(struct seq_file *s, const struct dc_hw_plane *plane, u8 indent)
{
	print_hw_plane_fb(s, &plane->fb, indent);
	print_hw_plane_fb_ext(s, &plane->fb_ext, indent);
	print_hw_plane_pos(s, &plane->pos, indent);
	print_hw_plane_rcd_mask(s, &plane->rcd_mask, indent);
	print_hw_plane_std_bld(s, &plane->std_bld, indent);
	print_hw_plane_y2r(s, &plane->y2r, indent);
	print_hw_plane_lut_3d(s, &plane->lut_3d, indent);
	print_hw_plane_scale(s, &plane->scale, indent);
	print_hw_plane_roi(s, &plane->roi, indent);
	print_hw_plane_clear(s, &plane->clear, indent);
	print_hw_states(s, &plane->states, indent);
	print_hw_plane_sram(s, &plane->sram, indent);
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "config_status", plane->config_status);
}

static void print_wb_state(struct seq_file *s, const struct dc_hw_wb *wb, u8 indent)
{
	print_hw_wb_fb(s, &wb->fb, indent);
	print_hw_states(s, &wb->states, indent);
	print_tab(s, indent);
	seq_printf(s, PRINT_NAME_WIDTH ": %d\n", "config_status", wb->config_status);
}

#if IS_ENABLED(CONFIG_VERISILICON_DC9400_0x32a) || IS_ENABLED(CONFIG_VERISILICON_DC9400_0x20000005)
bool dc_hw_is_be_idle(struct dc_hw *hw)
{
	int ret;
	u32 config;
	u32 dbg_status_ctrl_reg =
		SSWRP_DPU_GPCSR_SEC_MOD0_RW__DPU_BE_QCHCTL_DBG_STATUS_CTL_adjusted_address;
	u32 dbg_status_reg = SSWRP_DPU_GPCSR_SEC_MOD0_RW__DPU_BE_QCHCTL_DBG_STATUS_adjusted_address;

	if (IS_ERR_OR_NULL(hw->gpcsr_reg_base))
		return true;

	DPU_ATRACE_BEGIN("dc_hw_is_be_idle");

	/* Enable debug and set DBG_STATUS_SEL to 0x2 */
	config =
		(1 << SSWRP_DPU_GPCSR_SEC_MOD0_RW__DPU_BE_QCHCTL_DBG_STATUS_CTL__DBG_EN_offset) |
		(BE_IDLE_DBG_STATUS_SEL_VALUE
		 << SSWRP_DPU_GPCSR_SEC_MOD0_RW__DPU_BE_QCHCTL_DBG_STATUS_CTL__DBG_STATUS_SEL_offset);
	writel(config, hw->gpcsr_reg_base + dbg_status_ctrl_reg);

	config = 0;
	ret = readl_poll_timeout(hw->gpcsr_reg_base + dbg_status_reg, config,
				 !(config & BE_IDLE_DBG_STATUS_MASK), BE_IDLE_DBG_POLL_US,
				 BE_IDLE_DBG_TIMEOUT_US);
	DPU_ATRACE_END("dc_hw_is_be_idle");
	if (ret == -ETIMEDOUT) {
		trace_disp_be_idle_timeout(config);
		dev_err(hw->dev, "Backend idle check timed out! Status: 0x%x\n", config);
		return false;
	}

	return true;
}
#else
bool dc_hw_is_be_idle(struct dc_hw *hw)
{
	return true;
}
#endif
