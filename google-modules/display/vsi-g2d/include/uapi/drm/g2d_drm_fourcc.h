/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __G2D_DRM_FOURCC_H__
#define __G2D_DRM_FOURCC_H__

#include <drm/drm_fourcc.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Upstream fourcc codes used by GPU for compressed formats
#define DRM_FORMAT_MOD_VENDOR_PVR 0x92
#define DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14 fourcc_mod_code(PVR, 34)
#define DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14 fourcc_mod_code(PVR, 39)

#define DRM_FORMAT_MOD_VENDOR_VS 0x0b

/*
 * VeriSilicon Compressed Format
 *
 * DEC(Decompressor-Compressor) format consists of category (3 bits),
 * tile mode (6 bits) and align mode (2 bits).
 *
 * PVRIC(PowerVR Image Compression) format consists of category (3 bits),
 * tile mode (6 bits) and lossless/lossy mode (2 bits).
 */

#define DRM_FORMAT_MOD_VS_TYPE_NORMAL 0x00
#define DRM_FORMAT_MOD_VS_TYPE_COMPRESSED 0x01
#define DRM_FORMAT_MOD_VS_TYPE_PVRIC 0x02
#define DRM_FORMAT_MOD_VS_TYPE_CUSTOM_10BIT 0x03
#define DRM_FORMAT_MOD_VS_TYPE_MASK ((__u64)0x07 << 53)

#define fourcc_mod_vs_code(type, val) \
	fourcc_mod_code(VS, (((__u64)type) << 53) | ((val) & 0x001fffffffffffffULL))

#define fourcc_mod_vs_pvric_code(tile, lossy) \
	fourcc_mod_vs_code(DRM_FORMAT_MOD_VS_TYPE_PVRIC, ((tile) | (lossy)))

#define DRM_FORMAT_MOD_VS_NORM_MODE_MASK 0x2F
#define DRM_FORMAT_MOD_VS_LINEAR 0x00
#define DRM_FORMAT_MOD_VS_TILED4x4 0x01
#define DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR 0x02
#define DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR 0x03
#define DRM_FORMAT_MOD_VS_TILE_8X8 0x04
#define DRM_FORMAT_MOD_VS_TILE_8X4 0x07
#define DRM_FORMAT_MOD_VS_SUPER_TILED_XMAJOR_8X4 0x0B
#define DRM_FORMAT_MOD_VS_SUPER_TILED_YMAJOR_4X8 0x0C
#define DRM_FORMAT_MOD_VS_TILE_Y 0x0D
#define DRM_FORMAT_MOD_VS_TILE_128X1 0x0F
#define DRM_FORMAT_MOD_VS_TILE_256X1 0x10
#define DRM_FORMAT_MOD_VS_TILE_32X1 0x11
#define DRM_FORMAT_MOD_VS_TILE_64X1 0x12
#define DRM_FORMAT_MOD_VS_TILE_MODE4X4 0x15
#define DRM_FORMAT_MOD_VS_TILE_32X2 0x20
#define DRM_FORMAT_MOD_VS_TILE_16X4 0x21
#define DRM_FORMAT_MOD_VS_TILE_32X4 0x22
#define DRM_FORMAT_MOD_VS_TILE_32X8 0x23
#define DRM_FORMAT_MOD_VS_TILE_16X8 0x24
#define DRM_FORMAT_MOD_VS_TILE_16X16 0x25

#define fourcc_mod_vs_norm_code(tile) fourcc_mod_vs_code(DRM_FORMAT_MOD_VS_TYPE_NORMAL, (tile))

#define fourcc_mod_vs_custom_code(tile) \
	fourcc_mod_vs_code(DRM_FORMAT_MOD_VS_TYPE_CUSTOM_10BIT, (tile))

#if defined(__cplusplus)
}
#endif
#endif /* __G2D_DRM_FOURCC_H__ */
