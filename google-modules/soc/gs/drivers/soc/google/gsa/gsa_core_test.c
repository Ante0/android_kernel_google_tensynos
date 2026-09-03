// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */
#include <kunit/test.h>
#include <linux/platform_device.h>
#include <linux/gfp_types.h>
#include "mock_gsa_mbox.h"
#include "gsa_mbox.h"
#include "gsa_priv.h"

/*
 * Caution: If a different CMD is used, make sure it is not a data xfer cmd in gsa_mbox.c
 * Data xfer cmds require a s2mpu that is not mocked.
 */
#define TEST_CMD 999U

static char test_buf[PAGE_SIZE];

static int gsa_core_test_init(struct kunit *test)
{
	test->priv = mock_gsa_mbox_init(test);
	return 0;
}

/*
 * This is a simple happy-path test that confirms gsa will receive
 * the expected arguments and gsa_mbox reacts normally when it receives
 * an OK response.
 */
static void gsa_send_cmd_receives_correct_args(struct kunit *test)
{
	struct mock_gsa_mbox *mbox = test->priv;
	const struct mock_gsa_recvd *recvd;
	u32 req[2] = { 456, 789 };
	u32 rsp[1];
	int rc;
	u32 mock_rsp[1] = { 123 };

	struct mock_gsa_resp *set_cmd_resp = gsa_mock_resp(mbox);

	set_cmd_resp->cmd = GSA_MB_CMD_RSP | TEST_CMD;
	set_cmd_resp->args = mock_rsp;
	set_cmd_resp->err = GSA_MB_OK;
	set_cmd_resp->argc = ARRAY_SIZE(mock_rsp);
	rc = gsa_send_cmd(mock_gsa_get_device(mbox), TEST_CMD, req, ARRAY_SIZE(req), rsp,
			  ARRAY_SIZE(rsp));

	KUNIT_ASSERT_TRUE(test, gsa_mock_was_called(mbox));

	KUNIT_EXPECT_EQ(test, rc, ARRAY_SIZE(mock_rsp));

	recvd = gsa_mock_args_recvd(mbox);
	KUNIT_EXPECT_EQ(test, recvd->cmd, TEST_CMD);
	KUNIT_EXPECT_EQ(test, recvd->argc, ARRAY_SIZE(req));
	KUNIT_EXPECT_EQ(test, recvd->args[0], req[0]);
	KUNIT_EXPECT_EQ(test, recvd->args[1], req[1]);

	KUNIT_EXPECT_EQ(test, rsp[0], mock_rsp[0]);
}

extern ssize_t log_main_show(struct device *gsa, struct device_attribute *attr,
			     char *buf);

/*
 * Test to confirm that a misconfigured log doesn't cause an unhandled failure
 */
static void gsa_main_log_read_no_config(struct kunit *test)
{
	struct device_attribute attr;
	struct device *mock_gsa = mock_gsa_get_device(mock_gsa_mbox_init(test));
	struct gsa_dev_state *s =
		platform_get_drvdata(to_platform_device(mock_gsa));

	s->log = NULL;
	KUNIT_EXPECT_EQ(test, log_main_show(mock_gsa, &attr, test_buf),
			-ENODEV);
}

/*
 * Test to confirm that a misconfigured log doesn't cause an unhandled failure
 */
static void gsa_main_log_read_misconfig(struct kunit *test)
{
	struct device_attribute attr;
	struct device *mock_gsa = mock_gsa_get_device(mock_gsa_mbox_init(test));
	struct gsa_dev_state *s =
		platform_get_drvdata(to_platform_device(mock_gsa));

	s->log = ERR_PTR(-ENOMEM);
	KUNIT_EXPECT_EQ(test, log_main_show(mock_gsa, &attr, test_buf), -ENOMEM);
}

extern ssize_t log_intermediate_show(struct device *gsa,
				     struct device_attribute *attr,
				     char *buf);

/*
 * Test to confirm that a misconfigured log doesn't cause an unhandled failure
 */
static void gsa_intermediate_log_read_no_config(struct kunit *test)
{
	struct device_attribute attr;
	struct device *mock_gsa = mock_gsa_get_device(mock_gsa_mbox_init(test));
	struct gsa_dev_state *s =
		platform_get_drvdata(to_platform_device(mock_gsa));

	s->log = NULL;
	KUNIT_EXPECT_EQ(test,
			log_intermediate_show(mock_gsa, &attr, test_buf),
			-ENODEV);
}

/*
 * Test to confirm that a misconfigured log doesn't cause an unhandled failure
 */
static void gsa_intermediate_log_read_misconfig(struct kunit *test)
{
	struct device_attribute attr;
	struct device *mock_gsa = mock_gsa_get_device(mock_gsa_mbox_init(test));
	struct gsa_dev_state *s = platform_get_drvdata(to_platform_device(mock_gsa));

	s->log = ERR_PTR(-ENOMEM);
	KUNIT_EXPECT_EQ(test,
			log_intermediate_show(mock_gsa, &attr, test_buf),
			-ENOMEM);
}

static struct kunit_case gsa_core_test_cases[] = { KUNIT_CASE(gsa_send_cmd_receives_correct_args),
						   KUNIT_CASE(gsa_main_log_read_no_config),
						   KUNIT_CASE(gsa_main_log_read_misconfig),
						   KUNIT_CASE(gsa_intermediate_log_read_no_config),
						   KUNIT_CASE(gsa_intermediate_log_read_misconfig),
						   {} };

static struct kunit_suite gsa_core_test_suite = {
	.name = "gsa-core-kunit-test",
	.init = gsa_core_test_init,
	.test_cases = gsa_core_test_cases,
};

kunit_test_suites(&gsa_core_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL v2");
