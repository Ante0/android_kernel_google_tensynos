/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_ICC_H
#define _GOOGLE_ICC_H

#include <linux/types.h>

#include "google_icc_vote.h"

#define MAX_LINKS 8

enum {
	TYPE_NORMAL = 0,
	TYPE_SOURCE,
	TYPE_GMC,
	TYPE_GSLC,
	TYPE_QBOX,
};

struct google_icc_node {
	const char *name;
	u16 type;
	u16 links[MAX_LINKS];
	u16 id;
	u16 num_links;
	u32 attr;
	struct icc_vote vote;
};

#define DEFINE_GNODE(_name, _id, _type, _attr, ...)                \
	static struct google_icc_node _name = {                    \
		.id = _id,                                         \
		.name = #_name,                                    \
		.type = _type,                                     \
		.attr = _attr,                                     \
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })), \
		.links = { __VA_ARGS__ },                          \
	}

struct google_icc_desc {
	struct google_icc_node *const *nodes;
	size_t num_nodes;
};

#endif /* _GOOGLE_ICC_H */
