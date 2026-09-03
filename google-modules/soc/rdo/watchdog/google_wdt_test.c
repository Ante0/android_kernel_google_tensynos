// SPDX-License-Identifier: GPL-2.0-only
/*
 * Testing for Watchdog Timer module.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <kunit/test.h>

#include "google_wdt_local.h"

#define GOOGLE_WDT_KEY_LOCK_VALUE 0
#define GOOGLE_WDT_KEY_UNLOCK_VALUE 12648430
#define GOOGLE_WDT_WDT_CONTROL_INIT_VALUE 0
#define EXPIRY_ACTION_1 1
#define EXPIRY_ACTION_0 0
#define TEST_PING_VALUE_10 10
#define TEST_PING_VALUE_0 0

/* Test cases for op_start function */
#define TEST_OPSTART(_expiry_action, _ping_value)	\
	{						\
		.expiry_action = _expiry_action,	\
		.ping_value = _ping_value,		\
	}

struct google_wdt_test {
	struct google_wdt parent;
	struct watchdog_device wdd;
	u32 control;
	u32 id_part_num_value;
	u32 ping_value;
	u32 wdt_key;
};

struct google_wdt_test_op_start_test {
	u8 expiry_action;
	u32 ping_value;
};

u32 google_wdt_test_mock_readl(struct google_wdt *wdt, ptrdiff_t offset)
{
	struct google_wdt_test *this = container_of(wdt, struct google_wdt_test, parent);
	switch (offset) {
	case GOOGLE_WDT_ID_PART_NUM:
		return this->id_part_num_value;
	case GOOGLE_WDT_WDT_KEY:
		return this->wdt_key;
	case GOOGLE_WDT_WDT_CONTROL:
		return this->control;
	case GOOGLE_WDT_WDT_VALUE:
		return this->ping_value;
	default:
		break;
	}
	return 0;
}

void google_wdt_test_mock_writel(struct google_wdt *wdt, u32 val,
				 ptrdiff_t offset)
{
	struct google_wdt_test *this = container_of(wdt, struct google_wdt_test, parent);
	switch (offset) {
	case GOOGLE_WDT_ID_PART_NUM:
		this->id_part_num_value = val;
		break;
	case GOOGLE_WDT_WDT_KEY:
		this->wdt_key = val;
		if (val == wdt->key_unlock)
			this->control |= GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE;
		else
			this->control &= ~GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE;
		break;
	case GOOGLE_WDT_WDT_CONTROL:
		this->control = val;
		break;
	case GOOGLE_WDT_WDT_VALUE:
		this->ping_value = val;
		break;
	default:
		break;
	}
}

static void validate_id_part_num_success(struct kunit *test)
{
	struct google_wdt_test *fake_wdt;
	int r;

	fake_wdt = test->priv;
	fake_wdt->id_part_num_value = GOOGLE_WDT_ID_PART_NUM_VALUE;

	r = google_wdt_validate_id_part_num(&fake_wdt->parent);
	KUNIT_EXPECT_EQ(test, r, 0);
}

static void validate_id_part_num_failed(struct kunit *test)
{
	struct google_wdt_test *fake_wdt;
	int r;

	fake_wdt = test->priv;
	fake_wdt->id_part_num_value = GOOGLE_WDT_ID_PART_NUM_VALUE + 1;

	r = google_wdt_validate_id_part_num(&fake_wdt->parent);
	KUNIT_EXPECT_EQ(test, r, -EINVAL);
}

static void interrupt_bit_clear(struct kunit *test)
{
	struct google_wdt_test *fake_wdt;
	bool interrupt;
	bool key_enable;

	fake_wdt = test->priv;

	google_wdt_isr(0, &fake_wdt->parent);
	interrupt = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_INT_CLEAR, fake_wdt->control);
	key_enable = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE, fake_wdt->control);
	KUNIT_EXPECT_TRUE(test, interrupt);
	KUNIT_EXPECT_FALSE(test, key_enable);
}

static void google_wdt_test_op_start(struct kunit *test)
{
	struct google_wdt *wdt;
	struct google_wdt_test *fake_wdt;
	const struct google_wdt_test_op_start_test *params = test->param_value;
	struct watchdog_device wdd;
	u8 expiry_action;
	bool control_enable;
	bool key_enable;

	fake_wdt = test->priv;
	wdd = fake_wdt->wdd;
	wdt = wdd.driver_data;
	wdt->expiry_action = params->expiry_action;
	wdt->ping_value = params->ping_value;
	wdd.driver_data = wdt;

	google_wdt_watchdog_op_start(&fake_wdt->wdd);
	expiry_action = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_EXPIRY_ACTION, fake_wdt->control);
	control_enable = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE, fake_wdt->control);
	key_enable = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE, fake_wdt->control);
	KUNIT_EXPECT_EQ(test, expiry_action, params->expiry_action);
	KUNIT_EXPECT_TRUE(test, control_enable);
	KUNIT_EXPECT_FALSE(test, key_enable);
	KUNIT_EXPECT_EQ(test, fake_wdt->ping_value, params->ping_value);
}

static void google_wdt_test_op_stop(struct kunit *test)
{
	struct google_wdt *wdt;
	struct google_wdt_test *fake_wdt;
	struct watchdog_device wdd;
	bool control_enable;
	bool key_enable;

	fake_wdt = test->priv;
	wdd = fake_wdt->wdd;
	wdt = wdd.driver_data;
	wdt->control |= GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE;
	wdt->ping_value = TEST_PING_VALUE_10;
	wdd.driver_data = wdt;

	google_wdt_watchdog_op_stop(&fake_wdt->wdd);
	control_enable = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE, fake_wdt->control);
	key_enable = FIELD_GET(GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE, fake_wdt->control);
	KUNIT_EXPECT_FALSE(test, control_enable);
	KUNIT_EXPECT_FALSE(test, key_enable);
	KUNIT_EXPECT_EQ(test, fake_wdt->ping_value, TEST_PING_VALUE_10);
}

static int google_wdt_test_init(struct kunit *test)
{
	struct google_wdt_test *fake_wdt;

	fake_wdt = kunit_kzalloc(test, sizeof(*fake_wdt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_wdt);

	fake_wdt->parent.read = google_wdt_test_mock_readl;
	fake_wdt->parent.write = google_wdt_test_mock_writel;
	fake_wdt->parent.key_unlock = GOOGLE_WDT_KEY_UNLOCK_VALUE;
	fake_wdt->parent.key_lock = GOOGLE_WDT_KEY_LOCK_VALUE;

	fake_wdt->control = GOOGLE_WDT_WDT_CONTROL_INIT_VALUE;
	fake_wdt->wdd.driver_data = &fake_wdt->parent;

	test->priv = fake_wdt;
	return 0;
}

static const
struct google_wdt_test_op_start_test google_wdt_test_op_start_tests[] = {
	TEST_OPSTART(EXPIRY_ACTION_1, TEST_PING_VALUE_10),
	TEST_OPSTART(EXPIRY_ACTION_0, TEST_PING_VALUE_0),
};

static void
google_wdt_test_op_start_desc(const struct google_wdt_test_op_start_test *t,
			      char *desc)
{
	sprintf(desc, "Expected expiry action num = %d, ping value = %d\n",
		t->expiry_action, t->ping_value);
}

KUNIT_ARRAY_PARAM(google_wdt_test_op_start,
		  google_wdt_test_op_start_tests,
		  google_wdt_test_op_start_desc);

static struct kunit_case google_wdt_test_cases[] = {
	KUNIT_CASE(validate_id_part_num_success),
	KUNIT_CASE(validate_id_part_num_failed),
	KUNIT_CASE(interrupt_bit_clear),
	KUNIT_CASE(google_wdt_test_op_stop),
	KUNIT_CASE_PARAM(google_wdt_test_op_start, google_wdt_test_op_start_gen_params),
	{}
};

static struct kunit_suite google_wdt_test_suite = {
	.init = google_wdt_test_init,
	.name = "google_wdt_test",
	.test_cases = google_wdt_test_cases,
};

kunit_test_suite(google_wdt_test_suite);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google WDT KUnit test");
MODULE_LICENSE("GPL");
