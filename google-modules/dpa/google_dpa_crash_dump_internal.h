/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_CRASH_DUMP_INTERNAL_H
#define _GOOGLE_DPA_CRASH_DUMP_INTERNAL_H

#include "google_dpa_internal.h"

#if IS_ENABLED(CONFIG_SUBSYSTEM_COREDUMP)

int google_dpa_init_crashdump(struct google_dpa *dpa);

void google_dpa_deinit_crashdump(struct google_dpa *dpa);

void google_dpa_collect_crashdump_locked(struct google_dpa *dpa, void *regdump, int reg_dump_len);

#else

static inline int google_dpa_init_crashdump(struct google_dpa *dpa)
{
	return 0;
}

static inline void google_dpa_deinit_crashdump(struct google_dpa *dpa)
{
}

static inline void google_dpa_collect_crashdump_locked(struct google_dpa *dpa, void *regdump,
						       int reg_dump_len)
{
}

#endif

#endif /* _GOOGLE_DPA_CRASH_DUMP_INTERNAL_H */
