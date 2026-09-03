# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y := -I $(src)
subdir-ccflags-$(CONFIG_VERISILICON_CHIP_9x00) += -I $(src)/9x00
subdir-ccflags-$(CONFIG_VERISILICON_CHIP_9x00) += -I $(src)/9x00/info

vs_drm-objs := vs_crtc.o \
		vs_drv.o \
		vs_fb.o \
		vs_gem.o \
		vs_plane.o \
		vs_dc_drm_property.o \
		vs_simple_enc.o \
		vs_drm_atomic.o \
		vs_recovery.o \
		vs_trace.o \
		vs_writeback.o \
		vs_qos.o

ifeq ($(CONFIG_VERISILICON_CHIP_9x00), y)
# link to the 9x00 specify info
vs_drm-objs += 9x00/info/vs_dc_info.o
vs_drm-$(CONFIG_VERISILICON_DC9400_0x316) += 9x00/info/vs_dc_info_9400_0x316.o
vs_drm-$(CONFIG_VERISILICON_DC9400_0x32a) += 9x00/info/vs_dc_info_9400_0x32a.o
vs_drm-$(CONFIG_VERISILICON_DC9400_0x20000005) += 9x00/info/vs_dc_info_9400_0x20000005.o

# 9x00 other objs
vs_drm-objs += 9x00/vs_dc_hw.o
vs_drm-objs += 9x00/vs_dc.o
vs_drm-objs += 9x00/vs_dc_debugfs.o
vs_drm-objs += 9x00/vs_dc_lut.o
vs_drm-objs += 9x00/vs_dc_post.o
vs_drm-objs += 9x00/vs_dc_pre.o
vs_drm-objs += 9x00/vs_dc_property.o
vs_drm-objs += 9x00/vs_dc_sram.o
vs_drm-objs += 9x00/preprocess/vs_dc_pvric.o
vs_drm-objs += 9x00/preprocess/vs_dc_hdr.o
vs_drm-objs += 9x00/preprocess/vs_dc_preprocess.o
vs_drm-objs += 9x00/preprocess/vs_dc_plane_blender.o
vs_drm-objs += 9x00/postprocess/vs_dc_sharpness.o
vs_drm-objs += 9x00/postprocess/vs_dc_ltm.o
vs_drm-objs += 9x00/postprocess/vs_dc_display_blender.o
vs_drm-objs += 9x00/postprocess/vs_dc_postprocess.o
vs_drm-objs += 9x00/postprocess/vs_dc_scale.o
vs_drm-objs += 9x00/postprocess/vs_dc_display_3d_lut.o
vs_drm-objs += 9x00/postprocess/vs_dc_display_yuv.o
vs_drm-objs += 9x00/postprocess/vs_dc_display_rcd.o
vs_drm-objs += 9x00/postprocess/vs_dc_histogram.o
vs_drm-objs += 9x00/writeback/vs_dc_writeback.o
vs_drm-objs += 9x00/display_compress/vs_dc_dsc.o
vs_drm-objs += 9x00/display_compress/vs_dc_dsc_rc_tables.o
endif

vs_drm-$(CONFIG_VERISILICON_MIPI_DSI2H) += vs_mipi_dsi2h.o

obj-$(CONFIG_DRM_VERISILICON) += vs_drm.o
