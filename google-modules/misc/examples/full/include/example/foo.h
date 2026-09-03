/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EXAMPLE_FOO_H
#define EXAMPLE_FOO_H

#include <linux/kconfig.h>

#define FOO_EXPORTED_VAL 42

#if IS_ENABLED(CONFIG_EXAMPLE_FOO)
void foo_api_func(void);
#else
static inline void foo_api_func(void) {}
#endif

#endif /* EXAMPLE_FOO_H */
