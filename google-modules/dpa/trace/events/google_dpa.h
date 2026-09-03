// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 * This implements google_dpa_boot trace event class.
 */

#include <linux/string.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM google_dpa

#if !defined(_GOOGLE_DPA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GOOGLE_DPA_TRACE_H

#include <linux/tracepoint.h>

#include "google_dpa_internal.h"

DECLARE_EVENT_CLASS(google_dpa_boot_class, TP_PROTO(struct google_dpa *dpa), TP_ARGS(dpa),
		    TP_STRUCT__entry(), TP_fast_assign(), TP_printk("%s", ""));

DEFINE_EVENT(google_dpa_boot_class, google_dpa_boot_release, TP_PROTO(struct google_dpa *dpa),
	     TP_ARGS(dpa));
DEFINE_EVENT(google_dpa_boot_class, google_dpa_boot_secure_boot, TP_PROTO(struct google_dpa *dpa),
	     TP_ARGS(dpa));
DEFINE_EVENT(google_dpa_boot_class, google_dpa_boot_complete, TP_PROTO(struct google_dpa *dpa),
	     TP_ARGS(dpa));
DEFINE_EVENT(google_dpa_boot_class, google_dpa_boot_rpc_ready, TP_PROTO(struct google_dpa *dpa),
	     TP_ARGS(dpa));

#endif /* _GOOGLE_DPA_TRACE_H */

#include <trace/define_trace.h>