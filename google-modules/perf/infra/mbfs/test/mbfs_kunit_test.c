// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/array_size.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <perf/mbfs.h>

#include "../mbfs_base.h"
#include "../mbfs_protocol.h"

#define REQUIRED_MAJOR_MBFS_VERSION 0
#define REQUIRED_MIN_MINOR_MBFS_VERSION 1

#define MAX_RESPONSE_CHAIN 3

struct mbfs_mock_response {
	struct mbfs_response response;
	enum mbfs_error_code ret_code;
	struct mbfs_request request;
};

static struct mbfs_mock_response response_chain[MAX_RESPONSE_CHAIN];
static size_t current_response_index;
static size_t test_expected_response_cnt;
static struct kunit *cur_test;

static int mbfs_test_submit_request_mock(const struct mbfs_request *req_para,
					 struct mbfs_response *res_para)
{
	int ret = 0;
	const struct mbfs_request *req = req_para;
	struct mbfs_response *res = res_para;

	if (res == NULL || req == NULL) {
		ret = -EPERM;
		goto exit;
	}

	if (current_response_index >= MAX_RESPONSE_CHAIN ||
	    current_response_index >= test_expected_response_cnt) {
		KUNIT_FAIL(cur_test, "Current response index out of range.");
		ret = -EPERM;
		goto exit;
	}

	if (response_chain[current_response_index].request.command_idx != req->command_idx) {
		KUNIT_FAIL(cur_test,
			   "Requested command id: %d and expected id: %d one do not match.",
			   req->command_idx,
			   response_chain[current_response_index].request.command_idx);
		ret = -EPERM;
		goto increment_response_chain;
	}

	ret = mbfs_error2linux(response_chain[current_response_index].ret_code);

	if (!ret)
		memcpy(res, &response_chain[current_response_index].response,
		       sizeof(struct mbfs_response));

increment_response_chain:
	current_response_index++;
exit:
	return ret;
}

static struct mbfs_client_backend backend = {
	.name = "test_backend",
	.submit_request = mbfs_test_submit_request_mock,
};

static struct mbfs_mock_response *current_response(void)
{
	KUNIT_ASSERT_LT_MSG(cur_test, test_expected_response_cnt, MAX_RESPONSE_CHAIN,
			    "Exceeded MAX_RESPONSE_CHAIN in %s", __func__);

	return &response_chain[test_expected_response_cnt];
}

static void finish_response(void)
{
	test_expected_response_cnt++;
}

static void add_get_protocol_version_expectation(void)
{
	struct mbfs_protocol_version_response *proto_version;

	current_response()->request.command_idx = MBFS_GET_PROTOCOL_VERSION;
	current_response()->ret_code = MBFS_OK;
	proto_version = (struct mbfs_protocol_version_response *)&current_response()->response;
	proto_version->major_version = REQUIRED_MAJOR_MBFS_VERSION;
	proto_version->minor_version = REQUIRED_MIN_MINOR_MBFS_VERSION;
	proto_version->file_index_len = 0;

	finish_response();
}

static void add_root_handle_expectation(void)
{
	struct mbfs_get_root_handle_response *root_handle;

	current_response()->request.command_idx = MBFS_GET_ROOT_HANDLE;
	current_response()->ret_code = MBFS_OK;
	root_handle = (struct mbfs_get_root_handle_response *)&current_response()->response;
	root_handle->handle = 0;

	finish_response();
}

static void mbfs_register_test(struct kunit *test)
{
	add_get_protocol_version_expectation();
	add_root_handle_expectation();

	KUNIT_EXPECT_EQ(test, register_mbfs_backend(&backend), 0);

	KUNIT_EXPECT_EQ(test, test_expected_response_cnt, current_response_index);

	unregister_mbfs_backend(&backend);
}

static void mbfs_double_register_test(struct kunit *test)
{
	add_get_protocol_version_expectation();
	add_root_handle_expectation();

	KUNIT_EXPECT_EQ(test, register_mbfs_backend(&backend), 0);
	KUNIT_EXPECT_EQ(test, register_mbfs_backend(&backend), -EEXIST);

	KUNIT_EXPECT_EQ(test, test_expected_response_cnt, current_response_index);

	unregister_mbfs_backend(&backend);
}

static void mbfs_invalid_root_test(struct kunit *test)
{
	add_get_protocol_version_expectation();

	current_response()->request.command_idx = MBFS_GET_ROOT_HANDLE;
	current_response()->ret_code = MBFS_TRY_AGAIN;

	finish_response();

	KUNIT_EXPECT_EQ(test, register_mbfs_backend(&backend), -EAGAIN);
	KUNIT_EXPECT_EQ(test, test_expected_response_cnt, current_response_index);
}

static void mbfs_submit_incorrect_minor_test(struct kunit *test)
{
	struct mbfs_protocol_version_response *proto_version;

	current_response()->request.command_idx = MBFS_GET_PROTOCOL_VERSION;
	current_response()->ret_code = MBFS_OK;
	proto_version =
		(struct mbfs_protocol_version_response *)&response_chain[test_expected_response_cnt]
			.response;
	proto_version->major_version = REQUIRED_MAJOR_MBFS_VERSION;
	proto_version->minor_version = 0;

	finish_response();

	KUNIT_EXPECT_EQ(test, register_mbfs_backend(&backend), -EPERM);
	KUNIT_EXPECT_EQ(test, test_expected_response_cnt, current_response_index);
}

static void mbfs_register_failure_test(struct kunit *test)
{
	current_response()->request.command_idx = MBFS_GET_PROTOCOL_VERSION;
	current_response()->ret_code = MBFS_UNRECOGNIZED_COMMAND;

	finish_response();

	KUNIT_EXPECT_EQ(test, register_mbfs_backend(&backend), -EBADMSG);
	KUNIT_EXPECT_EQ(test, test_expected_response_cnt, current_response_index);
}

static struct kunit_case mbfs_test[] = {
	KUNIT_CASE(mbfs_register_test),		KUNIT_CASE(mbfs_double_register_test),
	KUNIT_CASE(mbfs_invalid_root_test),	KUNIT_CASE(mbfs_submit_incorrect_minor_test),
	KUNIT_CASE(mbfs_register_failure_test), {},
};

static int mbfs_test_init(struct kunit *test)
{
	cur_test = test;
	current_response_index = 0;
	test_expected_response_cnt = 0;
	backend.initialized = false;

	return 0;
}

static void mbfs_test_exit(struct kunit *test)
{
	cur_test = NULL;
	current_response_index = 0;
	test_expected_response_cnt = 0;
	backend.initialized = false;
}

static struct kunit_suite mbfs_kunit_tests_suite = { .name = "mbfs_kunit_tests",
						     .test_cases = mbfs_test,
						     .init = mbfs_test_init,
						     .exit = mbfs_test_exit };

kunit_test_suite(mbfs_kunit_tests_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Przemyslaw Bida <przemyslawbida@google.com>");
