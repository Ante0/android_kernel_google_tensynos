/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef __GS_DRM_H__
#define __GS_DRM_H__

#include <drm/drm.h>
#include <drm/drm_mode.h>

/**
 * enum mode_usage_flags - flags to determine whether a mode is active
 *
 * Some build configurations or runtime flags will require different panel modes
 * to be available. These flags can be used to mark a given mode as having a
 * quality that would make it available in only a subset of those configurations
 *
 * @MODE_USAGE_PRIMARY: generally, the preferred mode. Used for "single display
 *                      mode" configurations (such as early bringup)
 * @MODE_USAGE_LP: Mode is a Low-Power Mode
 * @MODE_USAGE_VRR_ENABLED: Mode is a VRR mode, and so is disabled when VRR is
 *                          not available
 */
enum mode_usage_flags {
	MODE_USAGE_PRIMARY = 0,
	MODE_USAGE_LP,
	MODE_USAGE_VRR_ENABLED,
	MODE_USAGE_MAX,
};

#define MODE_USAGE_PRIMARY_BIT (1UL << MODE_USAGE_PRIMARY)
#define MODE_USAGE_LP_BIT (1UL << MODE_USAGE_LP)
#define MODE_USAGE_VRR_ENABLED_BIT (1UL << MODE_USAGE_VRR_ENABLED)

/**
 * struct gs_panel_mode_export - panel mode struct exported to hwc
 *
 * @modeinfo: Direct analog to drm_display_mode; see drm_mode_convert_to_umode()
 * @mode_usage_flags: Flags indicating special usage cases for the mode.
 *                    See enum mode_usage_flags
 * @video_mode: Whether panel is video-mode or command-mode
 * @clock_non_continuous: mode supports non-continuous clock behavior
 * @dsc_enabled: Compression with dsc is enabled for this mode
 * @sw_trigger: Frame transfer is triggered by sw instead of TE. Only relevant
 *              for DSI command modes; this is default for video modes.
 *
 * Specifically, this is the information packaged about the panel modes and made
 * viewable as a property on the gs_drm_connector.
 *
 * Designed to be expanded in the future; append all new members to the end to
 * avoid versioning issues
 */
struct gs_panel_mode_export {
	struct drm_mode_modeinfo modeinfo;
	__u64 mode_usage_flags;
	struct {
		__u8 video_mode : 1;
		__u8 clock_non_continuous : 1;
		__u8 dsc_enabled : 1;
		__u8 sw_trigger : 1;
	} mode_type;
};

/**
 * gs_panel_mode_export_header - description of panel mode layout in property blob
 * As we may expand the struct gs_panel_mode_export over time,
 * this header at the beginning of the blob allows for a description of the
 * currently-exported data structure
 */
struct gs_panel_mode_export_header {
	/** @header_size: sizeof(struct gs_panel_mode_export_header) */
	__u16 header_size;
	/** @mode_size: sizeof(struct gs_panel_mode_export) */
	__u16 mode_size;
	/** @num_modes: how many modes are in the rest of the blob */
	__u16 num_modes;
};

/**
 * struct gs_scaler_config - Used to override scaler properties.
 * @factor_x: horizontal scale factor, format 16.16
 * @factor_y: vertical scale factor, format 16.16
 * @init_offset_x: initial offset x, format 16.16
 * @init_offset_y: initial offset y, format 16.16
 */
struct gs_scaler_config {
	__u32 factor_x;
	__u32 factor_y;
	__u32 init_offset_x;
	__u32 init_offset_y;
};

#define EXPORTED_MODE_OFFSET(header, idx) ((header)->header_size + ((idx) * (header)->mode_size))
#define EXPORTED_MODE_BLOB_SIZE(num_modes)            \
	(sizeof(struct gs_panel_mode_export_header) + \
	 ((num_modes) * sizeof(struct gs_panel_mode_export)))

#endif /* __GS_DRM_H__ */
