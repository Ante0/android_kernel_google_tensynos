// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <perf/mbfs.h>

static void mbfs_api_get_handle_test(struct kunit *test)
{
	union mbfs_client_handle handle;
	union mbfs_client_handle child_handle;
	static const char correct_path[] = "cpm/cpmfs/test/fibo_gen/0";
	static const char too_long_a_node_path[] = "cpm/0123456789abcdefg";
	static const char incorrect_path[] = "cpm/cpmfs/test/0";
	static const char incorrect_backend_path[] = "cpm0/cpmfs/test/0";
	static const char empty_path[] = "";
	static const char trusted_end_node_path[] = "cpm/cpmfs/test/fibo_gen/11";

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(correct_path, &handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_handle(too_long_a_node_path, &handle), MBFS_INVALID_ARG);
	KUNIT_EXPECT_EQ(test, mbfs_get_handle(incorrect_path, &handle), MBFS_NOT_FOUND);
	KUNIT_EXPECT_EQ(test, mbfs_get_handle(incorrect_backend_path, &handle), MBFS_PROBE_DEFER);
	KUNIT_EXPECT_EQ(test, mbfs_get_handle(empty_path, &handle), MBFS_INVALID_ARG);

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(trusted_end_node_path, &handle),
			MBFS_NOT_FOUND); // not found since this file is secured
	// it's no possible to revert trusting for now. might need improving.

	memset(&handle, 0xff, sizeof(union mbfs_client_handle));
	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, "0", &child_handle), MBFS_NOT_FOUND);
}

static void mbfs_api_get_node_desc_test(struct kunit *test)
{
	union mbfs_client_handle handle;
	struct mbfs_client_node_desc desc;
	static const char end_node_path[] = "cpm/cpmfs/test/fibo_gen/0";
	static const char folder_node_path[] = "cpm/cpmfs/test/fibo_gen";

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(end_node_path, &handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_node_desc(handle, &desc), MBFS_OK);

	KUNIT_EXPECT_STREQ(test, desc.name, "0");
	KUNIT_EXPECT_EQ(test, desc.read_permission, true);
	KUNIT_EXPECT_EQ(test, desc.write_permission, false);
	KUNIT_EXPECT_EQ(test, desc.node_type, MBFS_FILE);
	KUNIT_EXPECT_EQ(test, desc.value_type, MBFS_UINT64);
	KUNIT_EXPECT_EQ(test, desc.num_subfolders, 0);
	KUNIT_EXPECT_EQ(test, desc.num_files, 0);

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_node_desc(handle, &desc), MBFS_OK);

	KUNIT_EXPECT_STREQ(test, desc.name, "fibo_gen");
	KUNIT_EXPECT_EQ(test, desc.read_permission, true);
	KUNIT_EXPECT_EQ(test, desc.write_permission, false);
	KUNIT_EXPECT_EQ(test, desc.node_type, MBFS_FOLDER);
	KUNIT_EXPECT_EQ(test, desc.num_subfolders, 0);
	KUNIT_EXPECT_EQ(test, desc.num_files, 20);

	//get description of node with invalid handle
	memset(&handle, 0xff, sizeof(union mbfs_client_handle));
	KUNIT_EXPECT_EQ(test, mbfs_get_node_desc(handle, &desc), MBFS_NOT_FOUND);
}

static void mbfs_api_get_child_by_name_test(struct kunit *test)
{
	static const char folder_node_path[] = "cpm/cpmfs/test/fibo_gen";
	static const char typetest_folder_node_path[] = "cpm/cpmfs/test/typetest";
	static const char typetest_uint64[] = "big_name_uint64";
	union mbfs_client_handle handle;
	union mbfs_client_handle child_handle;

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);

	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, "0", &child_handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, "11", &child_handle), MBFS_NOT_FOUND);
	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, "21", &child_handle), MBFS_NOT_FOUND);

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(typetest_folder_node_path, &handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, typetest_uint64, &child_handle),
			MBFS_OK);
}

static void mbfs_api_get_nth_child_test(struct kunit *test)
{
	static const char folder_node_path[] = "cpm/cpmfs/test/fibo_gen";
	union mbfs_client_handle handle;
	union mbfs_client_handle child_handle;

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 0, &child_handle), MBFS_OK);

	/* Below we shouldn't be able to obtain handles to files.
	 * File number 11 is restricted for not authorised use.
	 * File number 21 doesn't exist.
	 * Obtaining those is possible since the handle is actually calculated not
	 * queried from device. This is accepted as a implementation/optimisation decision.
	 */
	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 11, &child_handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 21, &child_handle), MBFS_OK);

	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FOLDER, 0, &child_handle),
			MBFS_NOT_FOUND);

	memset(&handle, 0xff, sizeof(union mbfs_client_handle));
	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 0, &child_handle),
			MBFS_NOT_FOUND);
}

static void mbfs_api_read_permissions_file_test(struct kunit *test)
{
	static const char folder_node_path[] = "cpm/cpmfs/test/fibo_gen";
	union mbfs_client_handle handle;
	union mbfs_client_handle child_handle;
	union val64 value;

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);

	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 0, &child_handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_read_file(child_handle, &value), MBFS_OK);
	KUNIT_EXPECT_EQ(test, value.number, 0);

	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 11, &child_handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_read_file(child_handle, &value), MBFS_FORBIDDEN);
}

static void mbfs_api_read_non_existing_file_test(struct kunit *test)
{
	static const char folder_node_path[] = "cpm/cpmfs/test/fibo_gen";
	union mbfs_client_handle handle;
	union mbfs_client_handle child_handle;
	union val64 value;

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);

	KUNIT_EXPECT_EQ(test, mbfs_get_nth_child(handle, MBFS_FILE, 21, &child_handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_read_file(child_handle, &value), MBFS_NOT_FOUND);
}

static void mbfs_api_read_write_file_test(struct kunit *test)
{
	static const char folder_node_path[] = "cpm/cpmfs/test/typetest";
	static const char string_name[] = "string";
	static const char uint64_name[] = "uint64";
	union mbfs_client_handle handle;
	union mbfs_client_handle child_handle;
	union val64 read;
	union val64 write;

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);

	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, string_name, &child_handle), MBFS_OK);
	strscpy(write.text, "test", sizeof(write.text));
	KUNIT_EXPECT_EQ(test, mbfs_write_file(child_handle, write), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_read_file(child_handle, &read), MBFS_OK);
	KUNIT_EXPECT_STREQ(test, read.text, write.text);

	KUNIT_EXPECT_EQ(test, mbfs_get_handle(folder_node_path, &handle), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_get_child_by_name(handle, uint64_name, &child_handle), MBFS_OK);
	write.number = 2;
	KUNIT_EXPECT_EQ(test, mbfs_write_file(child_handle, write), MBFS_OK);
	KUNIT_EXPECT_EQ(test, mbfs_read_file(child_handle, &read), MBFS_OK);
	KUNIT_EXPECT_EQ(test, read.number, write.number);

	read.number = 0;
	KUNIT_EXPECT_EQ(test, mbfs_read_child_by_name(handle, uint64_name, &read), MBFS_OK);
	KUNIT_EXPECT_EQ(test, read.number, write.number);

	KUNIT_EXPECT_EQ(test, mbfs_read_child_by_name(handle, "non_existing", &read),
			MBFS_NOT_FOUND);
}

static struct kunit_case mbfs_api_test[] = { KUNIT_CASE(mbfs_api_get_handle_test),
					     KUNIT_CASE(mbfs_api_get_node_desc_test),
					     KUNIT_CASE(mbfs_api_get_child_by_name_test),
					     KUNIT_CASE(mbfs_api_get_nth_child_test),
					     KUNIT_CASE(mbfs_api_read_permissions_file_test),
					     KUNIT_CASE(mbfs_api_read_non_existing_file_test),
					     KUNIT_CASE(mbfs_api_read_write_file_test),
					     {} };

static int mbfs_api_test_init(struct kunit *test)
{
	return 0;
}

static void mbfs_api_test_exit(struct kunit *test)
{
}

static struct kunit_suite mbfs_api_kunit_tests_suite = { .name = "mbfs_api_kunit_tests",
							 .test_cases = mbfs_api_test,
							 .init = mbfs_api_test_init,
							 .exit = mbfs_api_test_exit };

kunit_test_suite(mbfs_api_kunit_tests_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Przemyslaw Bida <przemyslawbida@google.com>");
