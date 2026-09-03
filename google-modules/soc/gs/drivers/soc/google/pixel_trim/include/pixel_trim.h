/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PIXEL_TRIM_H__
#define __PIXEL_TRIM_H__

#include <linux/types.h>

struct pixel_trim {
	struct list_head list;

	unsigned long (*trim)(void *private);
	void *private;

	char *name;
};

#if IS_ENABLED(CONFIG_PIXEL_TRIM)
int register_trim(struct pixel_trim *trim);
void unregister_trim(struct pixel_trim *trim);
#else
static inline int register_trim(struct pixel_trim *trim) { return 0; }
static inline void unregister_trim(struct pixel_trim *trim) { }
#endif
#endif
