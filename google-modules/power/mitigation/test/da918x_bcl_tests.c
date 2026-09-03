// SPDX-License-Identifier: GPL-2.0 only
/*
 * da918x_bcl_tests.c Google BCL DA918x tests
 *
 * Copyright (c) 2025 Google LLC.
 *
 */

#include <linux/err.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include "bcl.h"
#include "core_pmic_defs.h"
#include "da918x/da9188_limits.h"

static void verify_convert_pre_uvlo_lvl(struct kunit *test, int input,
					int rise_th, int fall_th)
{
	int converted = convert_pre_uvlo_lvl(input);

	KUNIT_EXPECT_EQ_MSG(test, (converted >> 4) & 0xF, rise_th,
			    "convert_pre_uvlo_lvl(%d) rise_th", input);
	KUNIT_EXPECT_EQ_MSG(test, converted & 0xF, fall_th,
			    "convert_pre_uvlo_lvl(%d) fall_th", input);
}

static void convert_pre_uvlo_lvl_test(struct kunit *test)
{
	verify_convert_pre_uvlo_lvl(test, 0, 0, 0);
	verify_convert_pre_uvlo_lvl(test, DA9188_PRE_UVLO_MIN - 1, 0x0, 0x0);
	verify_convert_pre_uvlo_lvl(test, DA9188_PRE_UVLO_MAX + 1, 0x0, 0x0);
	verify_convert_pre_uvlo_lvl(test, DA9188_PRE_UVLO_MIN, 0x0, 0x0);
	verify_convert_pre_uvlo_lvl(test, 2600, 0x0, 0x1);
	verify_convert_pre_uvlo_lvl(test, 2650, 0x0, 0x2);
	verify_convert_pre_uvlo_lvl(test, 2700, 0x1, 0x3);
	verify_convert_pre_uvlo_lvl(test, 3000, 0x7, 0x9);
	verify_convert_pre_uvlo_lvl(test, DA9188_PRE_UVLO_MAX, 0xD, 0xF);
}

static void convert_pre_ocp_lvl_test_value(struct kunit *test, int value,
					   int idx, u32 expected_output)
{
	/* Tests convert_pre_ocp_lvl with given parameters for the correct output */
	u32 output;

	convert_pre_ocp_lvl(value, idx, &output);
	KUNIT_EXPECT_EQ_MSG(test, output, expected_output,
			    "convert_pre_ocp_lvl(value=%d, idx=%d) output",
			    value, idx);
}

static void convert_pre_ocp_lvl_test_ret(struct kunit *test, int value, int idx,
					 int expected_ret)
{
	/* Tests convert_pre_ocp_lvl with given parameters for the correct return value */
	u32 output;
	int ret = convert_pre_ocp_lvl(value, idx, &output);

	KUNIT_EXPECT_EQ_MSG(test, ret, expected_ret,
			    "convert_pre_ocp_lvl(value=%d, idx=%d) ret", value,
			    idx);
}

static void convert_pre_ocp_lvl_full_test_idx(struct kunit *test, int idx,
					      int min, int max, int step)
{
	/* Test correct range */
	convert_pre_ocp_lvl_test_ret(test, 0, idx, -EINVAL);
	convert_pre_ocp_lvl_test_ret(test, -1, idx, -EINVAL);
	convert_pre_ocp_lvl_test_ret(test, min - 1, idx, -EINVAL);
	convert_pre_ocp_lvl_test_ret(test, max + 1, idx, -EINVAL);
	convert_pre_ocp_lvl_test_ret(test, min, idx, 0);
	convert_pre_ocp_lvl_test_ret(test, max, idx, 0);

	/* Test conversion accuracy */
	convert_pre_ocp_lvl_test_value(test, min, idx, 0xFF);
	convert_pre_ocp_lvl_test_value(test, min + step - 1, idx,
				       0xFF);
	convert_pre_ocp_lvl_test_value(test, min + step, idx, 0xEE);
	convert_pre_ocp_lvl_test_value(test, min + 15 * step - 1, idx,
				       0x11);
	convert_pre_ocp_lvl_test_value(test, min + 15 * step, idx,
				       0x00);
	convert_pre_ocp_lvl_test_value(test, max, idx, 0x00);
}

static void convert_pre_ocp_lvl_test(struct kunit *test)
{
	convert_pre_ocp_lvl_full_test_idx(test, PRE_OCP_CPU1, PRE_OCP_MIN,
					  DA9188_PRE_OCP_B3M_LIMIT,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, PRE_OCP_CPU2, PRE_OCP_MIN,
					  DA9188_PRE_OCP_B2M_LIMIT,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, PRE_OCP_GPU, PRE_OCP_MIN,
					  DA9188_PRE_OCP_B2M_LIMIT,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, PRE_OCP_AUR, PRE_OCP_MIN,
					  DA9188_PRE_OCP_B2M_LIMIT,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, PRE_OCP_TPU, PRE_OCP_TPU_MIN,
					  DA9188_PRE_OCP_B7M_LIMIT,
					  PRE_OCP_TPU_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, SOFT_PRE_OCP_CPU1,
					  SOFT_PRE_OCP_MIN, SOFT_PRE_OCP_MAX,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, SOFT_PRE_OCP_CPU2,
					  SOFT_PRE_OCP_MIN, SOFT_PRE_OCP_MAX,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, SOFT_PRE_OCP_GPU,
					  SOFT_PRE_OCP_MIN, SOFT_PRE_OCP_MAX,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, SOFT_PRE_OCP_AUR,
					  SOFT_PRE_OCP_MIN, SOFT_PRE_OCP_MAX,
					  PRE_OCP_STEP);
	convert_pre_ocp_lvl_full_test_idx(test, SOFT_PRE_OCP_TPU,
					  SOFT_PRE_OCP_TPU_MIN,
					  SOFT_PRE_OCP_TPU_MAX,
					  PRE_OCP_TPU_STEP);
}

static int da918x_bcl_test_init(struct kunit *test)
{
	return 0;
}

static void da918x_bcl_test_exit(struct kunit *test)
{
}

static struct kunit_case da918x_bcl_tests[] = {
	KUNIT_CASE(convert_pre_uvlo_lvl_test),
	KUNIT_CASE(convert_pre_ocp_lvl_test),
	{},
};

static struct kunit_suite da918x_bcl_test_suite = {
	.name = "da918x_bcl_test",
	.test_cases = da918x_bcl_tests,
	.init = da918x_bcl_test_init,
	.exit = da918x_bcl_test_exit,
};

kunit_test_suite(da918x_bcl_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Allen Jiang <alljiang@google.com>");
MODULE_AUTHOR("Hiroshi Akiyama <hiroshiakiyama@google.com>");
MODULE_AUTHOR("Jasmine Cha <chajasmine@google.com>");
MODULE_AUTHOR("Sam Ou <samou@google.com>");
MODULE_AUTHOR("Maggie Cheng <maggiecheng@google.com>");
MODULE_DESCRIPTION("Google LLC DA918x BCL Tests");
