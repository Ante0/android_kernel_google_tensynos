/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */
#ifndef _GS_PANEL_FUNCS_DEFAULTS_H_
#define _GS_PANEL_FUNCS_DEFAULTS_H_

#include <linux/types.h>

struct gs_panel;
struct gs_panel_mode;
struct gs_panel_detect_fault_desc;

/*
 * DOC: gs_panel_funcs_defaults theory
 *
 * This file contains a number of default implementations of the functions
 * outlined in the `struct gs_panel_funcs` vtable in `gs_panel.h`
 *
 * These are meant to be used directly or extended in panel-specific driver code
 * as appropriate.
 *
 * In general, these functions should perform behavior that is common to a large
 * range of our panel code.
 */

/**
 * gs_panel_get_panel_rev - Callback for getting panel rev from extinfo block
 * Currently, this would not slot directly into the `get_panel_rev` entry in the
 * vtable, because it operates on the 8-bit build code rather than the entire
 * 32-bit extinfo data block.
 *
 * Deprecated; consider gs_panel_get_panel_rev_full()
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_rev_id` member variable of this struct.
 * @rev: Short-form build-code-based rev entry used to determine revision of
 *       panel
 */
void gs_panel_get_panel_rev(struct gs_panel *ctx, u8 rev);

/**
 * gs_panel_get_panel_rev_full - Callback for getting panel rev from extinfo block
 * This uses the 0xDB and 0xDC bytes to determine the revision of the panel,
 * and update ctx->panel_rev_id internally.
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_rev_id` member variable of this struct.
 * @id: 4-byte panel id. See &gs_panel.panel_id for format and byte layout.
 */
void gs_panel_get_panel_rev_full(struct gs_panel *ctx, u32 id);

/**
 * gs_panel_get_panel_rev_no_variant - Callback for getting panel rev from extinfo
 *
 * This operates the same as gs_panel_get_panel_rev_full(), except that it
 * clears the bits that would otherwise define a panel variant.
 * Useful if there's no distinction made on a panel between, for example,
 * `EVT 1.0.0` and `EVT 1.0.1`
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_rev_id` member variable of this struct.
 * @id: 4-byte panel id. See &gs_panel.panel_id for format and byte layout.
 */
void gs_panel_get_panel_rev_no_variant(struct gs_panel *ctx, u32 id);

/**
 * gs_panel_read_slsi_ddic_id - Callback for reading the panel serial.
 *
 * This will read the panel serial information from the SLSI_DDIC_ID
 * reg. It is meant to be used on SLSI ddic's.
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_serial_number` member variable of this struct.
 * Return: 0 on success, negative value on error.
 */
int gs_panel_read_slsi_ddic_id(struct gs_panel *ctx);

/**
 * gs_panel_read_serial - Callback for reading the panel serial.
 *
 * This will read the panel serial information from the register referred to by
 * the panel_id_reg member of the gs_panel_desc, or the PANEL_ID_REG_DEFAULT
 * if no data exists for that register.
 *
 * NOTE: this function is deprecated; for new work, prefer use of
 * gs_panel_read_slsi_ddic_id, or a more vendor-applicable method.
 *
 * @ctx: Handle for gs_panel private data. In particular, it will update the
 *       `panel_serial_number` member variable of this struct.
 * Return: 0 on success, negative value on error.
 */
int gs_panel_read_serial(struct gs_panel *ctx);

/**
 * gs_panel_model_init() - Helper function to construct panel_model string
 * @ctx: Reference to panel data
 * @project: Project code to write to panel_model string
 * @extra_info: Additional info to write into panel_model string
 *
 * Context: this may be called as part of an implementation of panel_config()
 */
void gs_panel_model_init(struct gs_panel *ctx, const char *project, u8 extra_info);

/**
 * gs_panel_is_mode_seamless_atomic_helper() - Default implementation for
 *                                             checking seamless transition.
 * @ctx: Reference to panel data
 * @old_pmode: Old (often: current) display mode
 * @new_pmode: Proposed display mode
 *
 * Checks whether the panel can transition between the two modes seamlessly without
 * having to turn the display off before the mode change.
 *
 * This implementation checks if resolution/timings and flags are the same.
 *
 * Return: true if seamless transition possible, false otherwise
 */
bool gs_panel_is_mode_seamless_atomic_helper(const struct gs_panel *ctx,
					     const struct gs_panel_mode *old_pmode,
					     const struct gs_panel_mode *new_pmode);

/**
 * gs_panel_is_mode_seamless_helper() - Default implementation for checking
 *                                      seamless transition.
 * @ctx: Reference to panel data
 * @new_pmode: Proposed display mode
 *
 * DEPRECATED; prefer use of is_mode_seamless_atomic()
 *
 * Checks whether the panel can transition to the new mode seamlessly without
 * having to turn the display off before the mode change.
 *
 * This implementation checks if resolution/timings and flags are the same.
 *
 * Return: true if seamless transition possible, false otherwise
 */
bool gs_panel_is_mode_seamless_helper(const struct gs_panel *ctx,
				      const struct gs_panel_mode *new_pmode);

/**
 * gs_panel_get_te2_edges_helper() - Get rising and falling edges of TE2 signal
 * @ctx: Reference to panel data
 * @buf: string buffer to which to write the TE2 timing data
 * @lp_mode: Whether this should display data about LP or non-LP TE2 timing
 *
 * Return: length of string written to buffer, or negative value on error
 */
ssize_t gs_panel_get_te2_edges_helper(struct gs_panel *ctx, char *buf, bool lp_mode);

/**
 * gs_panel_set_te2_edges_helper() - Configure rising/falling te2 edges
 * @ctx: Reference to panel data
 * @timings: Array of values to configure into the timings
 * @lp_mode: Whether we're configuring LP or non-LP timings
 *
 * Return: 0 on success, negative value on error.
 */
int gs_panel_set_te2_edges_helper(struct gs_panel *ctx, u32 *timings, bool lp_mode);

/**
 * gs_panel_detect_fault_helper - Common logic for DDIC fault detection
 * @ctx: handle for gs_panel
 * @desc: description of fault detection specific to each panel
 *
 * Reads panel fault registers (ERR_FG and/or ERR_DSI) based on configuration,
 * maps bits to panel errors.
 *
 * Return:
 *   0: Success
 *   -EIO: Read failure
 *   -EINVAL: Configuration error
 */
int gs_panel_detect_fault_helper(struct gs_panel *ctx,
				 const struct gs_panel_detect_fault_desc *desc, bool irq_triggered);

/**
 * gs_panel_set_binned_lp_helper() - Execute command sequences for LP modes
 * @ctx: Reference to panel data
 * @brightness: Brightness value to which the panel is being set
 *
 * This executes the correct commands for setting LP modes based on the binned
 * brightness value.
 */
void gs_panel_set_binned_lp_helper(struct gs_panel *ctx, const u16 brightness);

/**
 * gs_panel_set_lp_mode_helper() - Execute command sequence to enter LP mode
 * @ctx: Reference to panel data
 * @pmode: mode to enter
 *
 * This helper is a convenience function to execute the lp_cmdset commands.
 * If there is no cmdset defined in the gs_panel_desc, this function does nothing
 */
void gs_panel_set_lp_mode_helper(struct gs_panel *ctx, const struct gs_panel_mode *pmode);

/**
 * gs_panel_set_vddd_regulator_helper() - Set VDDD voltage
 * @ctx: Reference to panel data
 * @is_lp: Whether we're configuring LP or non-LP voltage
 *
 * This helper implements legacy VDDD voltage setting via Linux regulator.
 *
 * Return: 0 on success, negative value on error
 */
int gs_panel_set_vddd_regulator_helper(struct gs_panel *ctx, bool is_lp);

/**
 * gs_panel_set_vddd_optional_gpio_helper() - Set VDDD voltage
 * @ctx: Reference to panel data
 * @is_lp: Whether we're configuring LP or non-LP voltage
 *
 * This helper will set VDDD with a gpio if it exists, and if not,
 * will fall back to the above set_vddd_regulator_helper.
 *
 * Return: 0 on success, negative value on error
 */
int gs_panel_set_vddd_optional_gpio_helper(struct gs_panel *ctx, bool is_lp);

/**
 * gs_panel_refresh_ctrl_full_helper() - update full refresh_ctrl features
 * @ctx: Reference to panel data
 * @pmode: current display mode
 *
 * This helper will update the panel features based on the refresh_ctrl bits,
 * assuming a full refresh_ctrl feature set.
 *
 * Return: true if changes to the features were detected, false otherwise
 */
bool gs_panel_refresh_ctrl_full_helper(struct gs_panel *ctx, const struct gs_panel_mode *pmode);

/**
 * gs_panel_refresh_ctrl_lite_helper() - update lite refresh_ctrl features
 * @ctx: Reference to panel data
 * @pmode: current display mode
 * @min_rr_supported: minimum refresh rate supported by the panel
 *
 * This helper will update the panel features based on the refresh_ctrl bits,
 * assuming a lite refresh_ctrl feature set.
 *
 * Return: true if new changes nees to be applied, false if no changes were detected
 */
bool gs_panel_refresh_ctrl_lite_helper(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				       u8 min_rr_supported);

/**
 * gs_panel_trace_settings_full_helper() - trace settings on full panels
 * @ctx: Reference to panel data
 *
 * This helper will trace common settings on full panels in each commit.
 */
void gs_panel_trace_settings_full_helper(struct gs_panel *ctx);

/**
 * gs_panel_trace_settings_lite_helper() - trace settings on lite panels
 * @ctx: Reference to panel data
 *
 * This helper will trace common settings on lite panels in each commit.
 */
void gs_panel_trace_settings_lite_helper(struct gs_panel *ctx);

#endif // _GS_PANEL_FUNCS_DEFAULTS_H_
