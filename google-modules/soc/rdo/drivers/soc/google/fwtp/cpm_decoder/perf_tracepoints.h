/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024 Google LLC */

#ifndef _PERF_TRACEPOINTS_H
#define _PERF_TRACEPOINTS_H

#include "cpm_tracepoint_decoder.h"

#if IS_ENABLED(CONFIG_SOC_LGA)
extern struct client_tracepoint domain_freq_tp;
#endif

extern struct client_tracepoint freq_agg_update_tp;

extern struct client_tracepoint dvfsmon_tp;

#endif /* _PERF_TRACEPOINTS_H */
