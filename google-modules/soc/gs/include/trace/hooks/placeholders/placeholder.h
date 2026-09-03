/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Declare placeholder for vendor hooks which are not on android-mainline but used by Pixel.
 *
 * Copyright 2024 Google LLC
 *
 */

#ifndef _TRACE_HOOKS_PLACEHOLDERS_PLACEHOLDER_H
#define _TRACE_HOOKS_PLACEHOLDERS_PLACEHOLDER_H

#include <trace/hooks/vendor_hooks.h>

#define DECLARE_HOOK_PLACEHOLDER(name, proto)							\
	static inline int									\
	register_trace_##name(void (*probe)(void *__data, proto), void *data)			\
	{											\
		pr_err("Vendor hook %s does not exist, functions might not work properly.\n",	\
			#name);									\
		return -EOPNOTSUPP;								\
	}

#endif /* _TRACE_HOOKS_PLACEHOLDERS_PLACEHOLDER_H */
