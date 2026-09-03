/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#define CREATE_TRACE_POINTS
#include <trace/panel_trace.h>
EXPORT_TRACEPOINT_SYMBOL(dsi_tx);
EXPORT_TRACEPOINT_SYMBOL(dsi_rx);
EXPORT_TRACEPOINT_SYMBOL(dsi_cmd_fifo_status);
EXPORT_TRACEPOINT_SYMBOL(dsi_label_scope);
EXPORT_TRACEPOINT_SYMBOL(panel_errors);
EXPORT_TRACEPOINT_SYMBOL(gram_collision);
EXPORT_TRACEPOINT_SYMBOL(panel_settings_full);
EXPORT_TRACEPOINT_SYMBOL(panel_settings_lite);
EXPORT_TRACEPOINT_SYMBOL(msleep);
EXPORT_TRACEPOINT_SYMBOL(te2_update_settings);
EXPORT_TRACEPOINT_SYMBOL(panel_write_generic);
