// SPDX-License-Identifier: GPL-2.0-only
/*
 * Trace module for NOA WLAN
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>>
 */

#include <common/core.h>

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "wlan_trace.h"
