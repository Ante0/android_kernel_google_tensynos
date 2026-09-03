/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GCMA_H__
#define __GCMA_H__

#include <linux/types.h>

extern void pixel_gcma_alloc_range(unsigned long start_pfn, unsigned long end_pfn);
extern void pixel_gcma_free_range(unsigned long start_pfn, unsigned long end_pfn);
extern int register_pixel_gcma_area(const char *name, phys_addr_t base,
				    phys_addr_t size);
#endif
