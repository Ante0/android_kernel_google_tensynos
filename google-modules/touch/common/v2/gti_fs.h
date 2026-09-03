/* SPDX-License-Identifier: GPL */
/*
 * GTI File System for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#ifndef __GTI_FS_H__
#define __GTI_FS_H__

#include "goog_touch_interface.h"

/*-----------------------------------------------------------------------------
 * Interactive calibration minimum and maximum state times.
 */

#define S_IN_NS 1000000000ULL /* 1s = 1e9ns */

/* Must wait at least 5s before beginning any operation */
#define MIN_DELAY_IDLE (5 * S_IN_NS)

/* Must wait at least 1s between screen-turning-off and calibration start, and
 * cannot stay in this state longer than 15s.
 */
#define MIN_DELAY_INIT_CAL (1 * S_IN_NS)
#define MAX_DELAY_INIT_CAL (15 * S_IN_NS)

/* Must wait at least 3s for calibration to complete and cannot stay in this
 * state longer than 15s.
 */
#define MIN_DELAY_RUN_CAL (3 * S_IN_NS)
#define MAX_DELAY_RUN_CAL (15 * S_IN_NS)

/* Must wait at least 1s before final return to idle and cannot stay in this
 * state longer than 15s.
 */
#define MIN_DELAY_END_CAL (1 * S_IN_NS)
#define MAX_DELAY_END_CAL (15 * S_IN_NS)

/* Must wait at least 1s between screen-turning-off and self-test start, and
 * cannot state in this state longer than 15s.
 */
#define MIN_DELAY_INIT_TEST (1 * S_IN_NS)
#define MAX_DELAY_INIT_TEST (15 * S_IN_NS)

/* Must wait at least 3s for self-test to complete and cannot stay in this state
 * longer than 15s.
 */
#define MIN_DELAY_RUN_TEST (3 * S_IN_NS)
#define MAX_DELAY_RUN_TEST (15 * S_IN_NS)

/* Must wait at least 1s before final return to idle and cannot stay in this
 * state longer than 15s.
 */
#define MIN_DELAY_END_TEST (1 * S_IN_NS)
#define MAX_DELAY_END_TEST (15 * S_IN_NS)

/* Must wait at least 1s before beginning reset operation and cannot stay in this state
 * longer than 15s.
 */
#define MIN_DELAY_INIT_RESET (1 * S_IN_NS)
#define MAX_DELAY_INIT_RESET (15 * S_IN_NS)
#define MIN_DELAY_RUN_RESET (1 * S_IN_NS)
#define MAX_DELAY_RUN_RESET (15 * S_IN_NS)
#define MIN_DELAY_END_RESET (1 * S_IN_NS)
#define MAX_DELAY_END_RESET (15 * S_IN_NS)

void gti_procfs_init(struct goog_touch_interface *gti);
int gti_sysfs_create_vendor_input_link(struct goog_touch_interface *gti);
int gti_sysfs_create_vendor_link(struct goog_touch_interface *gti);
int gti_sysfs_init(struct goog_touch_interface *gti);

#endif /* __GTI_FS_H__ */
