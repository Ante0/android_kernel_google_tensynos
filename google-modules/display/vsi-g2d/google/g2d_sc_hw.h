/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef G2D_SC_HW_H_
#define G2D_SC_HW_H_
#include <drm/g2d_drm.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>

#include "g2d_pvric_hw.h"

// Todo(b/390265640): Move to dts
#define NUM_PIPELINES (1)

#define __vsFIELDSTART(reg_field) \
		(0 ? reg_field)

#define __vsFIELDEND(reg_field) \
		(1 ? reg_field)

#define __vsFIELDSIZE(reg_field) (__vsFIELDEND(reg_field) - __vsFIELDSTART(reg_field) + 1)

#define __vsFIELDALIGN(data, reg_field) (((u32)(data)) << __vsFIELDSTART(reg_field))

#define __vsFIELDMASK(reg_field) \
	((u32)((__vsFIELDSIZE(reg_field) == 32) ? ~0U : (~(~0U << __vsFIELDSIZE(reg_field)))))

/**************************************************************************
 **
 **  VS_SET_FIELD
 **
 **  Set the value of a field within specified data.
 **
 **  ARGUMENTS:
 **
 **  data	Data value.
 **  reg	 Name of register.
 **  field   Name of field within register.
 **  value   Value for field.
 */
#define VS_SET_FIELD(data, reg, field, value)                                             \
	((((u32)(data)) & ~__vsFIELDALIGN(__vsFIELDMASK(reg##_##field), reg##_##field)) | \
	 __vsFIELDALIGN((u32)(value) & __vsFIELDMASK(reg##_##field), reg##_##field))

/*******************************************************************************
 **
 **  VS_GET_FIELD
 **
 **  Extract the value of a field from specified data.
 **
 **  ARGUMENTS:
 **
 **  data	Data value.
 **  reg	 Name of register.
 **  field   Name of field within register.
 */
#define VS_GET_FIELD(data, reg, field) \
	(((((u32)(data)) >> __vsFIELDSTART(reg##_##field)) & __vsFIELDMASK(reg##_##field)))

#define vsSETFIELD_FE(field0, id, field1) \
	(((u32)id == 0) ? (field0##0##_##field1) : (field0##1##_##field1))

/**************************************************************************
 **
 **  VS_SET_FIELD_PREDEF
 **
 **  Set the value of a field within specified data with a
 **  predefined value.
 **
 **  ARGUMENTS:
 **
 **  data	Data value.
 **  reg	 Name of register.
 **  field   Name of field within register.
 **  value   Name of the value within the field.
 */
#define VS_SET_FIELD_PREDEF(data, reg, field, value)                                      \
	((((u32)(data)) & ~__vsFIELDALIGN(__vsFIELDMASK(reg##_##field), reg##_##field)) | \
	 __vsFIELDALIGN(reg##_##field##_##value & __vsFIELDMASK(reg##_##field), reg##_##field))

/* TODO(b/391971453): Add support for all formats the HW can handle */
enum sc_hw_wb_format {
	WB_FORMAT_ARGB8888 = 0,
	WB_FORMAT_XRGB8888,
	WB_FORMAT_A2RGB101010,
	WB_FORMAT_X2RGB101010,
	/* b/332946613 Note that RGB888 is unsupported by HW */
	WB_FORMAT_RGB888,
	WB_FORMAT_NV12 = 14,
	WB_FORMAT_P010 = 16,
	NUM_WB_FORMATS,
};

enum sc_hw_pipe_id {
	HW_PIPE_0 = 0,
	HW_PIPE_1,
	NUM_HW_PIPES,
};

enum sc_hw_color_format {
	FORMAT_A8R8G8B8 = 0x00,
	FORMAT_X8R8G8B8,
	FORMAT_A2R10G10B10,
	FORMAT_X2R10G10B10,
	/* b/332946613 Note that R8G8B8 is unsupported by HW */
	FORMAT_R8G8B8,
	FORMAT_R5G6B5,
	FORMAT_A1R5G5B5,
	FORMAT_X1R5G5B5,
	FORMAT_A4R4G4B4,
	FORMAT_X4R4G4B4,
	FORMAT_A16R16G16B16 = 0x0A,
	/* b/332946613 Note that YUV422 formats like FORMAT_YUY2 are unsupported by HW */
	FORMAT_YUY2,
	/* b/332946613 Note that YUV422 formats like FORMAT_UYVY are unsupported by HW */
	FORMAT_UYVY,
	FORMAT_YV12,
	FORMAT_NV12,
	FORMAT_NV16,
	FORMAT_P010,
	FORMAT_P210,
	FORMAT_YUV420_PACKED,
	NUM_LAYER_FORMATS,
};

enum sc_hw_tile_mode {
	TILE_MODE_LINEAR = 0,
	TILE_MODE_32X2,
	TILE_MODE_16X4,
	TILE_MODE_32X4,
	TILE_MODE_32X8,
	TILE_MODE_16X8,
	TILE_MODE_8X8,
	TILE_MODE_16X16,
};

enum sc_hw_wb_tile_mode {
	WB_TILE_MODE_LINEAR = 0,
	WB_TILE_MODE_16X16,
};

enum sc_hw_csc_gamut {
	CSC_GAMUT_601 = 0,
	CSC_GAMUT_709 = 1,
	CSC_GAMUT_2020 = 2,
	CSC_GAMUT_P3 = 3,
	CSC_GAMUT_SRGB = 4,
};

enum sc_hw_csc_mode {
	CSC_MODE_USER_DEF = 0,
	CSC_MODE_L2L = 1,
	CSC_MODE_L2F = 2,
	CSC_MODE_F2L = 3,
	CSC_MODE_F2F = 4,
};

enum sc_hw_rotation {
	ROT_0,
	ROT_90,
	ROT_180,
	ROT_270,
	FLIP_X,
	FLIP_Y,
	FLIPX_90,
	FLIPY_90,
};

enum sc_hw_swizzle {
	SWIZZLE_ARGB = 0,
	SWIZZLE_RGBA,
	SWIZZLE_ABGR,
	SWIZZLE_BGRA,
};

struct drm_g2d_rect {
	__u16 x;
	__u16 y;
	__u16 w;
	__u16 h;
};

enum drm_g2d_dma_mode {
	/* read full image */
	G2D_DMA_NORMAL = 0,
	/* read one ROI region in the image */
	G2D_DMA_ONE_ROI = 1,
};

struct sc_hw_fb {
	u64 address;
	u64 u_address;
	u64 v_address;
	u32 stride;
	u32 u_stride;
	u32 v_stride;
	u16 width;
	u16 height;
	u8 format;
	u8 tile_mode;
	u8 rotation;
	u8 swizzle;
	u8 uv_swizzle;
	u8 zpos;
	u8 display_id;
	bool enable;
};

struct sc_hw_roi {
	// in the DPU driver this is an enum exposed in the UAPI.
	// Since G2D doesn't use a blob property for ROIs that isn't necessary here
	enum drm_g2d_dma_mode mode;
	struct drm_g2d_rect in_rect;
	bool enable;
};

struct sc_hw_scale {
	u32 src_w;
	u32 src_h;
	u32 dst_w;
	u32 dst_h;
	u32 factor_x;
	u32 factor_y;
	u32 offset_x;
	u32 offset_y;
	bool enable;
	bool coefficients_dirty;
};

struct sc_hw_y2r {
	u8 gamut;
	u8 mode;
	s32 coef[VS_MAX_Y2R_COEF_NUM];
	bool enable;
};

struct sc_hw_plane {
	struct sc_hw_fb fb;
	struct sc_hw_scale scale;
	struct sc_hw_roi roi;
	struct sc_hw_y2r y2r;
	struct pvric_hw_config pvric;
};

struct sc_hw_wb {
	struct sc_hw_fb fb;
	struct pvric_hw_config pvric;
};

struct sc_hw_interrupt_status {
	u8 pipe_frame_start;
	u8 pipe_frame_done;
	u8 apb_hang;
	u8 axi_rd_bus_hang;
	u8 axi_wr_bus_hang;
	u8 axi_bus_err;
	u8 pvric_decode_err;
	u8 pvric_encode_err;
	u8 reset_status;
};

struct sc_hw {
	void *reg_base;
	u32 reg_dump_offset;
	u32 reg_dump_size;
	u32 reg_size;
	struct sc_hw_plane plane[NUM_PIPELINES];
	struct sc_hw_wb wb[NUM_PIPELINES];
	struct sc_hw_sub_funcs *sub_func;
	/*for multiple interrupt destinations*/
	u8 intr_dest;
	u8 reset_status;
	struct device *dev;
};

void sc_hw_commit(struct sc_hw *hw, u8 display_id);
void sc_hw_wb_commit(struct sc_hw *hw, u8 display_id, struct drm_rect *dst);
void sc_hw_enable_shadow_register(struct sc_hw *hw, u8 display_id, bool enable);
void sc_hw_start_trigger(struct sc_hw *hw, u8 display_id);
void sc_hw_update_wb_fb(struct sc_hw *hw, u8 id, struct sc_hw_fb *fb);
inline void sc_write(struct sc_hw *hw, u32 reg, u32 value);
inline u32 sc_read(struct sc_hw *hw, u32 reg);
void sc_hw_restore_state(struct sc_hw *hw);
void sc_hw_update_plane(struct sc_hw *hw, u8 id, struct sc_hw_fb *fb);
void sc_hw_update_plane_roi(struct sc_hw *hw, u8 id, struct sc_hw_roi *roi);
void sc_hw_update_plane_scale(struct sc_hw *hw, u8 id, struct sc_hw_scale *scale);
void sc_hw_update_plane_y2r(struct sc_hw *hw, u8 id, struct sc_hw_y2r *y2r_conf);
void sc_hw_init(struct sc_hw *hw, struct device *dev);
int sc_hw_get_interrupt(struct sc_hw *hw, struct sc_hw_interrupt_status *status);
int sc_hw_enable_interrupts(struct sc_hw *hw);
int sc_hw_disable_interrupts(struct sc_hw *hw);
void sc_hw_print_id_regs(struct sc_hw *hw, struct device *dev);
#if IS_ENABLED(CONFIG_DEBUG_FS)
int sc_hw_reg_dump(struct seq_file *s, struct sc_hw *hw);
#endif /* CONFIG_DEBUG_FS */

#endif // G2D_SC_HW_H_
