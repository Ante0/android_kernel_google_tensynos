// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Google LLC
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include "../kunit_example.h"

/**
 * mock_kunit_example_raw_add() - Mock function for kunit_example_raw_add.
 * @a: First integer.
 * @b: Second integer.
 *
 * This function is used to replace the internal logic during testing.
 *
 * Return: A constant value for testing.
 */
static int mock_kunit_example_raw_add(int a, int b)
{
	return 42;
}

/**
 * test_kunit_example_process_real() - Test the real implementation via public API.
 * @test: The KUnit test context.
 */
static void test_kunit_example_process_real(struct kunit *test)
{
	/* Calling the public API, which uses the real internal logic */
	KUNIT_EXPECT_EQ(test, kunit_example_process_data(1, 2), 3);
}

/**
 * test_kunit_example_process_mock() - Test the mocked implementation via public API.
 * @test: The KUnit test context.
 */
static void test_kunit_example_process_mock(struct kunit *test)
{
	/* Activate the static stub redirection for the internal helper */
	kunit_activate_static_stub(test, kunit_example_raw_add, mock_kunit_example_raw_add);

	/*
	 * Calling the public API should now return the mock value
	 * because its internal call is redirected.
	 */
	KUNIT_EXPECT_EQ(test, kunit_example_process_data(1, 2), 42);
}

static struct kunit_case kunit_example_test_cases[] = {
	KUNIT_CASE(test_kunit_example_process_real),
	KUNIT_CASE(test_kunit_example_process_mock),
	{}
};

static struct kunit_suite kunit_example_test_suite = {
	.name = "kunit_example_test",
	.test_cases = kunit_example_test_cases,
};

kunit_test_suite(kunit_example_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
