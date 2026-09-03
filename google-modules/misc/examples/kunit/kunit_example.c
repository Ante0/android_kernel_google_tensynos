// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Google LLC
 */

#include <kunit/static_stub.h>
#include <kunit/visibility.h>
#include <linux/module.h>
#include "kunit_example.h"

/**
 * kunit_example_process_data() - Example public API.
 * @a: First input.
 * @b: Second input.
 *
 * This function represents a public API that calls an internal function.
 * In a real scenario, this might perform some high-level logic.
 *
 * Return: The result of the internal raw_add operation.
 */
int kunit_example_process_data(int a, int b)
{
	return kunit_example_raw_add(a, b);
}
EXPORT_SYMBOL_GPL(kunit_example_process_data);

/**
 * kunit_example_raw_add() - Internal helper function.
 * @a: First integer.
 * @b: Second integer.
 *
 * This function represents an internal operation (e.g., hardware access)
 * that we want to mock during testing.
 *
 * Return: The sum of a and b.
 */
VISIBLE_IF_KUNIT int kunit_example_raw_add(int a, int b)
{
	KUNIT_STATIC_STUB_REDIRECT(kunit_example_raw_add, a, b);
	return a + b;
}
EXPORT_SYMBOL_IF_KUNIT(kunit_example_raw_add);

static int __init kunit_example_init(void)
{
	return 0;
}

static void __exit kunit_example_exit(void)
{
}

module_init(kunit_example_init);
module_exit(kunit_example_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit Example Module");
