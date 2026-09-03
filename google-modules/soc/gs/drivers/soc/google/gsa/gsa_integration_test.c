// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */
#include <kunit/test.h>
#include <linux/gfp_types.h>
#include <linux/gsa/gsa_core.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "gsa_log.h"
#include "gsa_mbox.h"
#include "gsa_priv.h"

#if IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
#define GSA_NODE_PATH "/gsa-ns@e010000"
#else
#define GSA_NODE_PATH "/gsa-ns"
#endif

static int gsa_integration_test_init(struct kunit *test)
{
	struct platform_device *pdev;
	struct device_node *np = of_find_node_by_path(GSA_NODE_PATH);

	if (!np)
		return -1;
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -2;

	test->priv = &pdev->dev;
	return 0;
}

static void gsa_integration_test_exit(struct kunit *test)
{
	put_device(test->priv);
}

/* Test reading the GSA version from the real GSA
 *
 * This test covers GSA writing to the bounce-buffer and the mailbox stack.
 */
static void gsa_read_version(struct kunit *test)
{
	char project[32], build[128], built[32];
	ssize_t rc;
	char *buf = kunit_kmalloc(test, GSA_PAGE_SIZE, GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	rc = gsa_get_gsa_version(test->priv, buf);
	KUNIT_EXPECT_GT(test, rc, 0);

	rc = sscanf(buf, "Project: %31[^,], Build: %127[^,], Built: %31s", project, build, built);
	KUNIT_EXPECT_EQ(test, rc, 3);
}

/* Test reading the GSA log from the real GSA
 *
 * This test ensures the GSA log is readable.
 */
static void gsa_read_log(struct kunit *test)
{
	ssize_t rc;
	struct device *gsa = test->priv;
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);
	char *buf = kunit_kmalloc(test, GSA_LOG_SIZE, GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	rc = gsa_log_read(s->log, false, buf, PAGE_SIZE);
	KUNIT_EXPECT_GT(test, rc, 0);
}

static struct kunit_case gsa_integration_test_cases[] = { KUNIT_CASE(gsa_read_version),
							  KUNIT_CASE(gsa_read_log),
							  {} };

static struct kunit_suite gsa_integration_test_suite = {
	.name = "gsa-integration-kunit-test",
	.init = gsa_integration_test_init,
	.exit = gsa_integration_test_exit,
	.test_cases = gsa_integration_test_cases,
};

kunit_test_suites(&gsa_integration_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
