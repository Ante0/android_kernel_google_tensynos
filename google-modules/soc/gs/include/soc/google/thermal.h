/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2025 Google LLC
 */

#ifndef __GOOGLE_THERMAL_H__
#define __GOOGLE_THERMAL_H__

#if IS_ENABLED(CONFIG_VH_THERMAL)
void register_tz_id_ignore_genl(int tz_id);
void register_tz_id_irq_wakeable(int tz_id);
#else
static inline void register_tz_id_ignore_genl(int tz_id) {}
static inline void register_tz_id_irq_wakeable(int tz_id)
{
}
#endif /* CONFIG_VH_THERMAL */

#endif /* __GOOGLE_THERMAL_H__ */
