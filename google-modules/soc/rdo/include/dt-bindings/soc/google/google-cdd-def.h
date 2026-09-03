/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (C) 2023 Google LLC.
 *
 */

#ifndef GOOGLE_CDD_DEF_H
#define GOOGLE_CDD_DEF_H

/* ACTION */
#define GO_DEFAULT		"default"
#define GO_DEFAULT_ID		0
#define GO_PANIC		"panic"
#define GO_PANIC_ID		1
#define GO_WATCHDOG		"watchdog"
#define GO_WATCHDOG_ID		2
#define GO_S2M			"s2m"
#define GO_S2M_ID		3
#define GO_CACHEDUMP		"cachedump"
#define GO_CACHEDUMP_ID	4
#define GO_SCANDUMP		"scandump"
#define GO_SCANDUMP_ID		5
#define GO_HALT		"halt"
#define GO_HALT_ID		6
#define GO_ACTION_MAX		7

/* DPM DUMP MODE */
#define NONE_DUMP		0
#define FULL_DUMP		1
#define QUICK_DUMP		2

/* Enable by DPM */
#define DPM_ENABLE			1
/* Enable by Privileged Debug */
#define PRIVILEGED_ENABLE		2

#endif /* GOOGLE_CDD_DEF_H */
