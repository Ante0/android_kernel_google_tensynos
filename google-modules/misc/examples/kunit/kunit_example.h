/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2026 Google LLC
 */

#ifndef _KUNIT_EXAMPLE_H_
#define _KUNIT_EXAMPLE_H_

#include <linux/kconfig.h>

/**
 * kunit_example_process_data() - Example public API.
 * @a: First input.
 * @b: Second input.
 *
 * Return: The processed result.
 */
int kunit_example_process_data(int a, int b);

/*
 * The function prototypes below are only made visible when KUnit is enabled.
 * This allows the KUnit test module to link against these internal functions.
 */
#if IS_ENABLED(CONFIG_KUNIT)
int kunit_example_raw_add(int a, int b);
#endif

#endif /* _KUNIT_EXAMPLE_H_ */
