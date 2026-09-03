/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for thermal metrics
 *
 * Copyright 2025 Google LLC
 */

#ifndef _THERMAL_METRICS_H_
#define _THERMAL_METRICS_H_

#if IS_ENABLED(CONFIG_THERMAL_METRICS)
int thermal_metrics_init(struct kobject *metrics_kobj);
#else
static inline int thermal_metrics_init(struct kobject *metrics_kobj) { return 0; }
#endif

#endif /* _THERMAL_METRICS_H_ */
