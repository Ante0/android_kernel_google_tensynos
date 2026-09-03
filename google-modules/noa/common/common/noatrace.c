// SPDX-License-Identifier: GPL-2.0-only
/*
 * Trace module for NOA
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#include "common/core.h"

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "common/noatrace.h"
