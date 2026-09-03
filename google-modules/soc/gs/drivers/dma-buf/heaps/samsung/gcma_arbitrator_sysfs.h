/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GCMA_ARBITRATOR_SYSFS_H
#define __GCMA_ARBITRATOR_SYSFS_H

#include <linux/types.h>

struct gcma_region;

#ifdef CONFIG_SYSFS
int gcma_arbitrator_sysfs_init_region(struct gcma_region *region);
void gcma_arbitrator_sysfs_exit_region(struct gcma_region *region);
void gcma_arbitrator_add_stat(struct gcma_region *region, size_t size);
void gcma_arbitrator_sub_stat(struct gcma_region *region, size_t size);
#else
static inline int gcma_arbitrator_sysfs_init_region(struct gcma_region *region) { return 0; }
static inline void gcma_arbitrator_sysfs_exit_region(struct gcma_region *region) {}
static inline void gcma_arbitrator_add_stat(struct gcma_region *region, size_t size) {}
static inline void gcma_arbitrator_sub_stat(struct gcma_region *region, size_t size) {}
#endif

#endif /* __GCMA_ARBITRATOR_SYSFS_H */
