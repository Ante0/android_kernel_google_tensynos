// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Google LLC
 */
#include "vs_dc_info.h"

extern const struct vs_dc_info dc_info_9400_316;
extern const struct vs_dc_info dc_info_9400_32a;
extern const struct vs_dc_info dc_info_9400_20000005;

static const struct vs_dc_info *dc_infos[] = {
#if IS_ENABLED(CONFIG_VERISILICON_DC9400_0x316)
	&dc_info_9400_316,
#endif /* CONFIG_VERISILICON_DC9400_0x316 */
#if IS_ENABLED(CONFIG_VERISILICON_DC9400_0x32a)
	&dc_info_9400_32a,
#endif /* CONFIG_VERISILICON_DC9400_0x32a */
#if IS_ENABLED(CONFIG_VERISILICON_DC9400_0x20000005)
	&dc_info_9400_20000005,
#endif /* CONFIG_VERISILICON_DC9400_0x20000005 */
};

const struct vs_dc_info *vs_dc_get_chip_info(u32 chip_id, u32 revision, u32 pid, u32 cid)
{
	int i = 0;
	const struct vs_dc_info *info = NULL;

	for (i = 0; i < ARRAY_SIZE(dc_infos); i++) {
		info = dc_infos[i];
		if (info->chip_id == chip_id && info->pid == pid && info->cid == cid) {
			return info;
		}
	}
	return NULL;
}

int vs_dc_get_hw_cap(const struct vs_dc_info *info, enum drm_vs_hw_cap_type type, u64 *cap)
{
	if (!info || !cap || type >= VS_HW_CAP_COUNT)
		return -EINVAL;

	switch (type) {
	case VS_HW_CAP_CHIP_ID:
		*cap = info->chip_id;
		break;
	case VS_HW_CAP_CHIP_REV:
		*cap = info->revision;
		break;
	case VS_HW_CAP_CHIP_PID:
		*cap = info->pid;
		break;
	case VS_HW_CAP_CHIP_CID:
		*cap = info->cid;
		break;
	case VS_HW_CAP_PITCH_ALIGNEMENT:
		*cap = info->pitch_alignment;
		break;
	case VS_HW_CAP_ADDR_ALIGNEMENT:
		*cap = info->addr_alignment;
		break;
	case VS_HW_CAP_FE0_DMA_SRAM_SIZE:
		*cap = info->fe0_dma_sram_size;
		break;
	case VS_HW_CAP_FE1_DMA_SRAM_SIZE:
		*cap = info->fe1_dma_sram_size;
		break;
	case VS_HW_CAP_FE0_SCL_SRAM_SIZE:
		*cap = info->fe0_scl_sram_size;
		break;
	case VS_HW_CAP_FE1_SCL_SRAM_SIZE:
		*cap = info->fe1_scl_sram_size;
		break;
	case VS_HW_CAP_MAX_BLEND_LAYER:
		*cap = info->max_blend_layer;
		break;
	case VS_HW_CAP_MAX_EXT_LAYER:
		*cap = info->max_ext_layer;
		break;
	case VS_HW_CAP_MAX_SEG_NUM:
		*cap = info->max_seg_num;
		break;
	case VS_HW_CAP_MAX_EOTF_SIZE:
		*cap = info->max_eotf_size;
		break;
	case VS_HW_CAP_MAX_TONEMAP_SIZE:
		*cap = info->max_tonemap_size;
		break;
	case VS_HW_CAP_MAX_OETF_SIZE:
		*cap = info->max_oetf_size;
		break;
	case VS_HW_CAP_MAX_DEGAMMA_SIZE:
		*cap = info->max_degamma_size;
		break;
	case VS_HW_CAP_MAX_GAMMA_SIZE:
		*cap = info->max_gamma_size;
		break;
	case VS_HW_CAP_CGM_LUT_SIZE:
		*cap = info->cgm_lut_size;
		break;
	case VS_HW_CAP_CGM_EX_LUT_SIZE:
		*cap = info->cgm_ex_lut_size;
		break;
	case VS_HW_CAP_PRE_OETF_BITS:
		*cap = info->pre_eotf_bits;
		break;
	case VS_HW_CAP_HDR_BITS:
		*cap = info->hdr_bits;
		break;
	case VS_HW_CAP_OETF_BITS:
		*cap = info->oetf_bits;
		break;
	case VS_HW_CAP_BLD_CGM_BITS:
		*cap = info->bld_cgm_bits;
		break;
	case VS_HW_CAP_PRE_DEGAMMA_BITS:
		*cap = info->pre_degamma_bits;
		break;
	case VS_HW_CAP_DEGAMME_BITS:
		*cap = info->degamma_bits;
		break;
	case VS_HW_CAP_CGM_LUT_BITS:
		*cap = info->cgm_lut_bits;
		break;
	case VS_HW_CAP_GAMM_BITS:
		*cap = info->gamma_bits;
		break;
	case VS_HW_CAP_BLUR_COEF_BITS:
		*cap = info->blur_coef_bits;
		break;
	case VS_HW_CAP_PPC:
		*cap = info->ppc;
		break;
	case VS_HW_CAP_VBLANK_MARGIN_PCT:
		*cap = info->vblank_margin_pct;
		break;
	case VS_HW_CAP_H_BUBBLE_PCT:
		*cap = info->h_bubble_pct;
		break;
	case VS_HW_CAP_ROTATION_PREFETCH_LINE:
		*cap = info->rotation_prefetch_line;
		break;
	case VS_HW_CAP_ROTATION_PIPELINE_DELAY_US:
		*cap = info->rotation_pipeline_delay_us;
		break;
	case VS_HW_CAP_ROTATION_PIPELINE_LATENCY_US:
		*cap = info->rotation_pipeline_latency_us;
		break;
	case VS_HW_CAP_ROTATION_NOMINAL_CYCLE_NUM:
		*cap = info->rotation_nominal_cycle_num;
		break;
	case VS_HW_CAP_ROTATION_NOMINAL_CYCLE_DENOM:
		*cap = info->rotation_nominal_cycle_denom;
		break;
	case VS_HW_CAP_AXI_BUS_BIT_WIDTH:
		*cap = info->axi_bus_bit_width;
		break;
	case VS_HW_CAP_AXI_BUS_UTIL_PCT:
		*cap = info->axi_bus_util_pct;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct vs_plane_info *vs_dc_get_fe0_info(const struct vs_dc_info *info)
{
	return info->planes_fe0;
}

const struct vs_plane_info *vs_dc_get_fe1_info(const struct vs_dc_info *info)
{
	return info->planes_fe1;
}

const struct vs_display_info *vs_dc_get_be_info(const struct vs_dc_info *info)
{
	return info->displays;
}

struct vs_plane_info *get_plane_info(u8 plane_id, const struct vs_dc_info *info)
{
	u8 id = 0;
	u8 boundary = info->plane_fe0_num;

	id = plane_id >= boundary ? (plane_id - boundary) : (plane_id);
	if (plane_id >= boundary)
		return (struct vs_plane_info *)&info->planes_fe1[id];
	else
		return (struct vs_plane_info *)&info->planes_fe0[id];
}
