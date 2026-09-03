/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for CPU idle metrics
 *
 * Copyright 2025 Google LLC
 */

#ifndef _CPUIDLE_METRICS_H_
#define _CPUIDLE_METRICS_H_

#if IS_ENABLED(CONFIG_CPUIDLE_METRICS)
int cpuidle_metrics_init(struct kobject *metrics_kobj);
#else
static inline int cpuidle_metrics_init(struct kobject *metrics_kobj) { return 0; }
#endif

#endif /* _CPUIDLE_METRICS_H_ */
