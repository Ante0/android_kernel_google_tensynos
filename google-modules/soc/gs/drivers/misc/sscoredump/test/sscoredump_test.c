// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include "../sscoredump.h"

extern u8 sscd_level;

struct sscd_test_priv {
	struct platform_device *pdev;
	struct sscd_platform_data pdata;
	struct sscd_device *sdev;
};

static void platform_device_unregister_action(void *pdev)
{
	platform_device_unregister(pdev);
}

static int sscoredump_test_init(struct kunit *test)
{
	struct sscd_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	test->priv = priv;

	priv->pdev = platform_device_alloc(SSCD_NAME, -1);
	KUNIT_ASSERT_NOT_NULL(test, priv->pdev);

	ret = platform_device_add_data(priv->pdev, &priv->pdata, sizeof(priv->pdata));
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = platform_device_add(priv->pdev);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, platform_device_unregister_action, priv->pdev);
	priv->sdev = platform_get_drvdata(priv->pdev);
	KUNIT_ASSERT_NOT_NULL(test, priv->sdev);

	return 0;
}

static void sscoredump_test_report_no_reader(struct kunit *test)
{
	struct sscd_test_priv *priv = test->priv;
	struct sscd_platform_data *pdata = dev_get_platdata(&priv->pdev->dev);
	int ret;
	char crash_info[] = "test crash";

	KUNIT_ASSERT_NOT_NULL(test, pdata);
	KUNIT_ASSERT_NOT_NULL(test, pdata->sscd_report);

	ret = pdata->sscd_report(priv->pdev, NULL, 0, 0, crash_info);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
}

static void sscoredump_test_create_report_simple(struct kunit *test)
{
	struct sscd_test_priv *priv = test->priv;
	struct sscd_device *sdev = priv->sdev;
	int ret;

	ret = create_report(sdev, NULL, 0, 0, "test crash");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, sdev->segs);
	KUNIT_ASSERT_EQ(test, sdev->nsegs, 1);
	KUNIT_ASSERT_EQ(test, sdev->segs[0].size, sizeof(sdev->crash_hdr));
	KUNIT_ASSERT_PTR_EQ(test, sdev->segs[0].addr, &sdev->crash_hdr);

	free_report(sdev);
	KUNIT_ASSERT_NULL(test, sdev->segs);
}

static void restore_sscd_level(void *old_level)
{
	sscd_level = *(u8 *)old_level;
}

static void sscoredump_test_create_report_with_elf(struct kunit *test)
{
	struct sscd_test_priv *priv = test->priv;
	struct sscd_device *sdev = priv->sdev;
	struct sscd_segment segs[1];
	int ret;
	u8 *old_level;

	old_level = kunit_kzalloc(test, sizeof(*old_level), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, old_level);
	*old_level = sscd_level;
	kunit_add_action(test, restore_sscd_level, old_level);

	sscd_level = REPORT_ALL;
	segs[0].addr = (void *)0x1234;
	segs[0].paddr = (void *)0x1234;
	segs[0].vaddr = (void *)0x1234;
	segs[0].size = 0x1000;

	ret = create_report(sdev, segs, 1, SSCD_FLAGS_ELFARM64HDR, "elf test");
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, sdev->segs);
	KUNIT_ASSERT_EQ(test, sdev->nsegs, 3);

	KUNIT_ASSERT_EQ(test, sdev->segs[0].size, sizeof(sdev->crash_hdr));
	KUNIT_ASSERT_PTR_EQ(test, sdev->segs[0].addr, &sdev->crash_hdr);

	KUNIT_ASSERT_NOT_NULL(test, sdev->segs[1].addr);
	KUNIT_ASSERT_GT(test, sdev->segs[1].size, 0);

	KUNIT_ASSERT_PTR_EQ(test, sdev->segs[2].addr, segs[0].addr);
	KUNIT_ASSERT_EQ(test, sdev->segs[2].size, segs[0].size);

	free_report(sdev);
	KUNIT_ASSERT_NULL(test, sdev->segs);
}

static void sscoredump_test_sysfs_enabled(struct kunit *test)
{
	struct sscd_test_priv *priv = test->priv;
	struct sscd_device *sdev = priv->sdev;
	char buf[10];
	ssize_t ret;

	ret = sdev_enabled_show(&sdev->dev, NULL, buf);
	KUNIT_ASSERT_GT(test, ret, 0);
	buf[ret - 1] = '\0';
	KUNIT_EXPECT_STREQ(test, "1", buf);

	ret = sdev_enabled_store(&sdev->dev, NULL, "0", 1);
	KUNIT_ASSERT_EQ(test, ret, 1);
	KUNIT_ASSERT_FALSE(test, sdev->enabled);

	ret = sdev_enabled_show(&sdev->dev, NULL, buf);
	KUNIT_ASSERT_GT(test, ret, 0);
	buf[ret - 1] = '\0';
	KUNIT_EXPECT_STREQ(test, "0", buf);
}

static struct kunit_case sscoredump_test_cases[] = {
	KUNIT_CASE(sscoredump_test_report_no_reader),
	KUNIT_CASE(sscoredump_test_create_report_simple),
	KUNIT_CASE(sscoredump_test_create_report_with_elf),
	KUNIT_CASE(sscoredump_test_sysfs_enabled),
	{}
};

static struct kunit_suite sscoredump_test_suite = {
	.name = "sscoredump_test",
	.init = sscoredump_test_init,
	.test_cases = sscoredump_test_cases,
};

kunit_test_suite(sscoredump_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
