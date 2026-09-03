/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_FB_H__
#define __VS_FB_H__

struct vs_gem_object *vs_fb_get_gem_obj(struct drm_framebuffer *fb, unsigned char plane);
dma_addr_t vs_fb_get_dma_addr(struct drm_framebuffer *fb, unsigned char plane);
struct drm_framebuffer *vs_fb_create(struct drm_device *dev, struct drm_file *file_priv,
				     const struct drm_mode_fb_cmd2 *mode_cmd);
const struct drm_format_info *vs_get_format_info(const struct drm_mode_fb_cmd2 *cmd);
int vs_get_fbc_offset_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
inline uint64_t vs_fb_parse_fourcc_modifier(uint64_t mode_cmd_modifier);
bool vs_fb_is_shallow(uint64_t modifier);
#endif /* __VS_FB_H__ */
