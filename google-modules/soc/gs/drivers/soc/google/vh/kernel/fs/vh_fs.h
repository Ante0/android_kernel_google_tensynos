/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __VH_FS_H__
#define __VH_FS_H__

void vh_ep_create_wakeup_source_mod(void *data, char *name, int len);
void vh_timerfd_create_mod(void *data, char *name, int len);

#endif
