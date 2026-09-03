/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vb2
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VB2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VB2_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct vb2_plane;
struct v4l2_plane;

DECLARE_HOOK(android_vh_vb2_plane_fill_v4l2_plane,
	TP_PROTO(struct vb2_plane *vb2_plane, struct v4l2_plane *v4l2_plane),
	TP_ARGS(vb2_plane, v4l2_plane));

#endif /* _TRACE_HOOK_VB2_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
