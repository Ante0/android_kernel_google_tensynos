/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 */

#ifndef _GOOGLE_GEM_CTRL_UTILS_H
#define _GOOGLE_GEM_CTRL_UTILS_H

#include <linux/kstrtox.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "google_gem.h"

#define IP_LIST_MAGIC "g120"
#define IP_LIST_MAGIC_SZ strlen(IP_LIST_MAGIC)

#define __eventgrp_for_each(_fields, _i) \
	for (typeof(_i) __i = &(_fields)[0]; (_i) = __i, __i && __i->name; __i++)
#define eventgrp_for_each_event(_e, _egrp) __eventgrp_for_each((_egrp)->hw_desc->events, _e)
#define eventgrp_for_each_filter(_f, _egrp) __eventgrp_for_each((_egrp)->hw_desc->filters, _f)

struct ip_info_entry {
	struct list_head list_node;
	u8 id;
	u8 cntrs_num;
	char name[];
};

struct filter_entry {
	u8 id;
	const char *name;
};

struct event_entry {
	u8 id;
	const char *name;
	enum gem_event_type type;
};

struct eventgrp_desc {
	const char *name;
	const struct event_entry *events;
	const struct filter_entry *filters;
};

struct eventgrp {
	struct list_head list_node;
	const struct eventgrp_desc *hw_desc;
	struct list_head ip_info_list;
};

/*
 * Resolves tokens within `pos` to `pos + size` by using delimiter char `delimiter`, and writes
 * token references into `char *` array `argv`. Returns 0 if number of tokens located is exactly
 * `argc`, otherwise an error code.
 */
static inline int split_args(const char *pos, size_t size, char delimiter,
			     const char **argv, size_t argc)
{
	if (!argc)
		return -EINVAL;

	argv[0] = pos;

	for (int i = 1; i < argc; i++) {
		off_t delta;

		pos = strnchr(pos, size, delimiter);

		if (!pos)
			return -EINVAL;

		argv[i] = ++pos;

		delta = argv[i] - argv[i - 1];
		if (delta <= 1)
			return -EINVAL;

		size -= delta;
	}

	return !size || strnchr(pos, size, delimiter) ? -EINVAL : 0;
}

/* Similar to kstrtou8, but parses only chars between s ~ s + len */
static inline int kstrntou8(const char *s, size_t len, unsigned int base, u8 *res)
{
	int ret;
	char *buf = kzalloc(len + 1, irqs_disabled() ? GFP_ATOMIC : GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	strscpy(buf, s, len + 1);
	ret = kstrtou8(buf, base, res);
	kfree(buf);
	return ret;
}

/*
 * Resolves tokens within `pos` to `pos + size` by using delimiter char `delimiter`, and consumes
 * them by `consume_cb`. Returns `end` if every `consume_cb` invocation succeeds, otherwise ERR_PTR.
 */
static inline const char *parse_substr(struct device *dev, const char *pos, const char *end,
				       char delimiter,
				       int (*consume_cb)(struct device *dev,
							 const char *pos, size_t size, void *data),
				       void *data)
{
	if (*pos && pos == end)
		return end;

	while (true) {
		const char *tok_end = min_t(const char *, end, strchrnul(pos, delimiter));
		int ret = consume_cb(dev, pos, tok_end - pos, data);

		if (ret)
			return ERR_PTR(ret);
		if (!*tok_end || tok_end >= end)
			break;

		pos = tok_end + 1;
	}

	return end;
}

/*
 * Parses ip-info string within `pos` to `pos + size` into an `ip_info_entry`, and appends it to the
 * `data`, which is a `struct list_head *`.
 */
int consume_ip_info(struct device *dev, const char *pos, size_t size, void *data);

/* Parses ip-list string within `pos` to `pos + size`, and outputs it to `eventgrp_list`. */
int gem_ip_info_parser(struct device *dev, const char *pos,
		       const struct eventgrp_desc *eventgrp_desc,
		       struct list_head *eventgrp_list);

#endif // _GOOGLE_GEM_CTRL_UTILS_H
