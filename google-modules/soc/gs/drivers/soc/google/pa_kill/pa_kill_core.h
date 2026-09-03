/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PA_KILL_CORE_H_
#define __PA_KILL_CORE_H_

void reclaim_memory(unsigned long nr_demand_pages);
void destroy_kill_threads(void);
int create_kill_threads(unsigned int nr_thread);
void pa_set_cpu_affinity(void);

#endif
