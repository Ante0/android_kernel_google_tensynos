// SPDX-License-Identifier: GPL
/*
 * Google Touch Interface - gti_pm.c KUnit Tests
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/device.h>
#include <linux/pm_runtime.h>

#include <kunit/test.h>
#include <kunit/static_stub.h>

#include "gti_pm.h"

// Mocked functions
static void mock_pm_stay_awake(struct device *dev)
{
}

static void mock_pm_relax(struct device *dev)
{
}

struct gti_pm_test_priv {
	struct gti_pm *pm;
	struct device *dev;
	u32 resume_count;
	u32 suspend_count;
};

static int mock_resume(void *private_data)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)private_data;
	pm_test->resume_count++;

	return 0;
}

static int mock_suspend(void *private_data)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)private_data;
	pm_test->suspend_count++;
	return 0;
}

static struct gti_pm_ops mock_pm_ops = {
	.resume = mock_resume,
	.suspend = mock_suspend,
};

static int gti_pm_test_init(struct kunit *test)
{
	struct gti_pm_test_priv *priv;
	int ret = 0;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	test->priv = priv;

	priv->dev = kunit_kzalloc(test, sizeof(*priv->dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->dev);

	priv->pm = kunit_kzalloc(test, sizeof(*priv->pm), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->pm);

	priv->pm->dev = priv->dev;
	dev_set_drvdata(priv->pm->dev, priv->pm);

	KUNIT_ASSERT_EQ(test, 0, gti_pm_probe(priv->pm, priv->dev, &mock_pm_ops, priv));

	kunit_activate_static_stub(test, pm_stay_awake, mock_pm_stay_awake);
	kunit_activate_static_stub(test, pm_relax, mock_pm_relax);

	// reset default status, lock = 0, suspend_count = 1, resume_count = 0
	ret = gti_pm_wake_unlock_internal(priv->pm, GTI_PM_WAKELOCK_TYPE_SCREEN_ON);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, 1, priv->suspend_count);
	KUNIT_EXPECT_EQ(test, 0, priv->resume_count);

	return 0;
}

static void gti_pm_test_exit(struct kunit *test)
{
	struct gti_pm_test_priv *priv = test->priv;
	gti_pm_remove(priv->pm);

	kunit_deactivate_static_stub(test, pm_stay_awake);
	kunit_deactivate_static_stub(test, pm_relax);
}

static void check_lock_status(struct kunit *test, enum gti_pm_wakelock_type type, bool lock)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)test->priv;
	bool ret = gti_pm_wake_check_locked_internal(pm_test->pm, type);

	KUNIT_EXPECT_EQ(test, ret, lock);
}

static void test_wake_lock_nosync(struct kunit *test)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)test->priv;
	int ret = 0;

	// default status, lock = 0, suspend_count = 1, resume_count = 0;

	// Flush workqueue to eliminate scheduling race conditions.
	// lock = FORCE_ACTIVE
	ret = gti_pm_wake_lock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE,
					       false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, true);

	flush_workqueue(pm_test->pm->event_wq);
	KUNIT_EXPECT_EQ(test, 1, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = FORCE_ACTIVE | BUGREPORT
	ret = gti_pm_wake_lock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_BUGREPORT, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_BUGREPORT, true);

	flush_workqueue(pm_test->pm->event_wq);
	KUNIT_EXPECT_EQ(test, 1, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = FORCE_ACTIVE
	ret = gti_pm_wake_unlock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_BUGREPORT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_BUGREPORT, false);

	flush_workqueue(pm_test->pm->event_wq);
	KUNIT_EXPECT_EQ(test, 1, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = 0
	ret = gti_pm_wake_unlock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);

	flush_workqueue(pm_test->pm->event_wq);
	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);
}

static void test_wake_lock_sync(struct kunit *test)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)test->priv;
	int ret = 0;

	// default status, lock = 0, suspend_count = 1, resume_count = 0;

	// lock = FORCE_ACTIVE
	ret = gti_pm_wake_lock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, true);

	KUNIT_EXPECT_EQ(test, 1, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = 0
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);

	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = FORCE_ACTIVE
	ret = gti_pm_wake_lock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, true);

	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 2, pm_test->resume_count);

	// lock = FORCE_ACTIVE | BUGREPORT
	ret = gti_pm_wake_lock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_BUGREPORT, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_BUGREPORT, true);

	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 2, pm_test->resume_count);

	// lock = BUGREPORT
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);

	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 2, pm_test->resume_count);

	// lock = 0;
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_BUGREPORT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_BUGREPORT, false);

	KUNIT_EXPECT_EQ(test, 3, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 2, pm_test->resume_count);
}

static void test_wake_lock_mixed(struct kunit *test)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)test->priv;
	int ret = 0;

	// default status, lock = 0, suspend_count = 1, resume_count = 0;

	// lock = SCREEN_ON;
	ret = gti_pm_wake_lock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, true);

	// lock = SCREEN_ON | FORCE_ACTIVE;
	ret = gti_pm_wake_lock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, true);

	KUNIT_EXPECT_EQ(test, 1, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = SCREEN_ON;
	ret = gti_pm_wake_unlock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);

	// lock = 0;
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_SCREEN_ON);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, false);

	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// lock = SCREEN_ON;
	ret = gti_pm_wake_lock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, true);

	// lock = 0;
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_SCREEN_ON);
	KUNIT_EXPECT_EQ(test, ret, 0);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, false);

	// The PM's worke_queue sees no state change due to a rapid lock/unlock toggle.
	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);
}

static void test_wake_lock_double_lock_unlock(struct kunit *test)
{
	struct gti_pm_test_priv *pm_test = (struct gti_pm_test_priv *)test->priv;
	int ret = 0;

	// default status, lock = 0, suspend_count = 1, resume_count = 0;

	// Nosync double lock
	ret = gti_pm_wake_lock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE,
					       false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gti_pm_wake_lock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE,
					       false);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, true);
	flush_workqueue(pm_test->pm->event_wq);
	KUNIT_EXPECT_EQ(test, 1, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// Nosync double unlock
	ret = gti_pm_wake_unlock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gti_pm_wake_unlock_nosync_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	flush_workqueue(pm_test->pm->event_wq);
	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 1, pm_test->resume_count);

	// Sync double lock
	ret = gti_pm_wake_lock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gti_pm_wake_lock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, true);
	KUNIT_EXPECT_EQ(test, 2, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 2, pm_test->resume_count);

	// Sync double unlock
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	ret = gti_pm_wake_unlock_internal(pm_test->pm, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	check_lock_status(test, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	KUNIT_EXPECT_EQ(test, 3, pm_test->suspend_count);
	KUNIT_EXPECT_EQ(test, 2, pm_test->resume_count);
}

// clang-format off
static struct kunit_case gti_pm_test_cases[] = {
	KUNIT_CASE(test_wake_lock_nosync),
	KUNIT_CASE(test_wake_lock_sync),
	KUNIT_CASE(test_wake_lock_mixed),
	KUNIT_CASE(test_wake_lock_double_lock_unlock),
	{}
};
// clang-format on

static struct kunit_suite gti_pm_test_suite = {
	.name = "gti_pm_test",
	.init = gti_pm_test_init,
	.exit = gti_pm_test_exit,
	.test_cases = gti_pm_test_cases,
};

kunit_test_suite(gti_pm_test_suite);

MODULE_DESCRIPTION("Google Touch Interface gti_pm_kunit_test");
MODULE_AUTHOR("Blackbear Chou<blackbearchou@google.com>");
MODULE_LICENSE("GPL");
