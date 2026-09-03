/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PIXEL_TRIM_CORE_H_
#define __PIXEL_TRIM_CORE_H_

void run_trim(void);
void trim_set_cpu_affinity(const struct cpumask *new_mask);
const struct cpumask *trim_get_cpu_affinity(void);

#endif
