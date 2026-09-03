// SPDX-License-Identifier: GPL-2.0-only
/*
 * Testing for Google GEM module.
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <kunit/test.h>

#include <linux/device.h>

#include "google_gem_utils.h"

struct gem_kstrntou8_test {
	const char *data;
	size_t len;
	int ret;
	u8 value;
};

struct gem_split_args_test {
	const char *data;
	size_t len;
	char delimiter;
	int ret;
	size_t argc;
	off_t offsets[10]; // Large enough for all the test cases.
};

struct gem_parse_substr_test {
	const char *data;
	size_t len;
	char delimiter;
	int ret;
	u32 sum;
};

struct gem_consume_ip_info_test {
	const char *data;
	size_t len;
	int ret;
	u8 id;
	u8 cntrs_num;
	const char *name;
};

struct gem_ip_info_parser_test {
	const char *ip_info;
	const void *grp_desc;
	const char *out;
	int ret;
};

static void gem_test_kstrntou8(struct kunit *test)
{
	const struct gem_kstrntou8_test *params = test->param_value;
	int ret;
	u8 value;

	ret = kstrntou8(params->data, params->len, 0, &value);
	KUNIT_ASSERT_EQ(test, ret, params->ret);
	if (ret)
		return;

	KUNIT_EXPECT_EQ(test, value, params->value);
}

static void gem_test_split_args(struct kunit *test)
{
	const struct gem_split_args_test *params = test->param_value;
	int ret;
	const char **argv;

	argv = kunit_kzalloc(test, sizeof(*argv) * params->argc, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, argv);

	ret = split_args(params->data, params->len, params->delimiter, argv, params->argc);
	KUNIT_ASSERT_EQ(test, ret, params->ret);
	if (params->ret)
		return;

	for (int i = 0; i < params->argc; i++) {
		off_t expected = params->offsets[i];
		off_t actual = argv[i] - params->data;

		KUNIT_EXPECT_EQ_MSG(test, expected, actual,
				    "argv[%d] doesn't match: expected %ld vs. actual %ld",
				    i, expected, actual);
	}
}

static int consume_u8_sum(struct device *dev, const char *pos, size_t size, void *data)
{
	u32 *sum = data;
	u8 value;
	int ret;

	ret = kstrntou8(pos, size, 0, &value);
	if (ret)
		return ret;

	*sum += value;
	return 0;
}

static void gem_test_parse_substr(struct kunit *test)
{
	const struct gem_parse_substr_test *params = test->param_value;
	u32 sum = 0;
	const char *pos;
	struct device *dev = test->priv;
	int ret;

	pos = parse_substr(dev, params->data, params->data + params->len, params->delimiter,
			   consume_u8_sum, &sum);

	ret = PTR_ERR_OR_ZERO(pos);
	KUNIT_ASSERT_EQ(test, ret, params->ret);
	if (params->ret)
		return;
	KUNIT_ASSERT_EQ(test, params->len, pos - params->data);
	KUNIT_ASSERT_EQ(test, sum, params->sum);
}

static void gem_test_consume_ip_info(struct kunit *test)
{
	const struct gem_consume_ip_info_test *params = test->param_value;
	struct device *dev = test->priv;
	int ret;
	struct list_head out;
	struct ip_info_entry *ip_info;

	INIT_LIST_HEAD(&out);

	ret = consume_ip_info(dev, params->data, params->len, &out);
	KUNIT_ASSERT_EQ(test, ret, params->ret);
	if (params->ret)
		return;

	ip_info = list_entry(out.next, struct ip_info_entry, list_node);

	KUNIT_EXPECT_EQ(test, ip_info->id, params->id);
	KUNIT_EXPECT_EQ(test, ip_info->cntrs_num, params->cntrs_num);
	KUNIT_EXPECT_STREQ(test, ip_info->name, params->name);
}

static char *close_trailing(char *pos, char trailing, char closer)
{
	if (*(pos - 1) == trailing)
		*(pos - 1) = closer;
	else
		*pos++ = closer;

	return pos;
}

static const struct eventgrp_desc eventgrp_desc_empty[] = {
	{ NULL },
};

static const struct event_entry evgrp_0_events[] = {
	{ 0, "event_0", GEM_EVENT_TYPE_HISTORICAL_HIGH },
	{ 1, "event_1", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 2, "event_2", GEM_EVENT_TYPE_UNKNOWN },
	{ 0, NULL },
};

static const struct filter_entry evgrp_0_filters[] = {
	{ 0, "Valid" },
	{ 1, "Addr" },
	{ 2, "User" },
	{ 3, "Cache" },
	{ 0, NULL },
};

static const struct eventgrp_desc eventgrp_desc_0[] = {
	{ "evgrp_0", evgrp_0_events, evgrp_0_filters },
};

static const struct event_entry test_cntrs1_events[] = {
	{ 70, "u99", GEM_EVENT_TYPE_UNKNOWN },
	{ 80, "v98", GEM_EVENT_TYPE_HISTORICAL_HIGH },
	{ 0, NULL },
};

static const struct filter_entry test_cntrs1_filters[] = {
	{ 20, "day" },
	{ 21, "second" },
	{ 22, "year" },
	{ 0, NULL },
};

static const struct event_entry test_cntrs2_events[] = {
	{ 99, "humming", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 0, NULL },
};

static const struct filter_entry test_cntrs2_filters[] = {
	{ 1, "VOL" },
	{ 0, NULL },
};

static const struct eventgrp_desc eventgrp_desc_1[] = {
	{ "test-counters-X01", test_cntrs1_events, test_cntrs1_filters },
	{ "test-counters-X02", test_cntrs2_events, test_cntrs2_filters },
};

static void gem_test_ip_info_parser(struct kunit *test)
{
	const struct gem_ip_info_parser_test *params = test->param_value;
	struct device *dev = test->priv;
	struct list_head eventgrp_list;
	struct eventgrp *eventgrp;
	int ret;
	const struct eventgrp_desc *grp_desc = params->grp_desc ?
		params->grp_desc : eventgrp_desc_empty;
	const char *expected_out = params->out ? params->out : "";
	size_t out_sz = strlen(expected_out) + 1;
	char *out = kunit_kzalloc(test, out_sz, GFP_KERNEL);
	char *pos = out;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, out);

	INIT_LIST_HEAD(&eventgrp_list);
	ret = gem_ip_info_parser(dev, params->ip_info, grp_desc, &eventgrp_list);

	KUNIT_ASSERT_EQ(test, ret, params->ret);
	if (params->ret)
		return;

	pos += scnprintf(pos, out_sz - (pos - out), IP_LIST_MAGIC);
	list_for_each_entry(eventgrp, &eventgrp_list, list_node) {
		struct ip_info_entry *info;
		const struct event_entry *event;
		const struct filter_entry *filter;

		eventgrp_for_each_event(event, eventgrp)
			pos += scnprintf(pos, out_sz - (pos - out), "%d:%s:%d,",
					 event->id, event->name, event->type);
		pos = close_trailing(pos, ',', '|');

		eventgrp_for_each_filter(filter, eventgrp)
			pos += scnprintf(pos, out_sz - (pos - out), "%d:%s,",
					 filter->id, filter->name);
		pos = close_trailing(pos, ',', '|');

		list_for_each_entry(info, &eventgrp->ip_info_list, list_node)
			pos += scnprintf(pos, out_sz - (pos - out), "%d:%s:%d,",
					 info->id, info->name, info->cntrs_num);
		pos = close_trailing(pos, ',', '|');
	}

	close_trailing(pos, '|', '\0');

	KUNIT_ASSERT_STREQ(test, out, expected_out);
}

static void fake_gem_dev_release(struct device *dev)
{
}

static int google_gem_test_init(struct kunit *test)
{
	struct device *dev;
	int ret;

	dev = kunit_kzalloc(test, sizeof(*dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	ret = dev_set_name(dev, "fake_gem_dev.kunit");
	KUNIT_ASSERT_EQ_MSG(test, ret, 0, "failed on dev_set_name");

	dev->release = fake_gem_dev_release;

	ret = device_register(dev);
	KUNIT_ASSERT_EQ_MSG(test, ret, 0, "failed on device_register");

	test->priv = dev;
	return 0;
}

static void google_gem_test_exit(struct kunit *test)
{
	device_unregister((struct device *)test->priv);
}

static const struct gem_kstrntou8_test gem_test_kstrntou8_tests[] = {
	{ .data = "0", .len = 1, .ret = 0, .value = 0 },
	{ .data = "@", .len = 1, .ret = -EINVAL, .value = 0 },
	{ .data = "1", .len = 1, .ret = 0, .value = 1 },
	{ .data = "103", .len = 0, .ret = -EINVAL, .value = 0 },
	{ .data = "103", .len = 1, .ret = 0, .value = 1 },
	{ .data = "103", .len = 2, .ret = 0, .value = 10 },
	{ .data = "103", .len = 3, .ret = 0, .value = 103 },
	{ .data = "103", .len = 4, .ret = 0, .value = 103 },
	{ .data = "0xdede", .len = 2, .ret = -EINVAL, .value = 0 },
	{ .data = "0xdede", .len = 4, .ret = 0, .value = 0xde },
	{ .data = " 247", .len = 1, .ret = -EINVAL, .value = 0 },
	{ .data = " 247", .len = 2, .ret = -EINVAL, .value = 0 },
};

static void gem_test_kstrntou8_desc(const struct gem_kstrntou8_test *t, char *desc)
{
	scnprintf(desc, KUNIT_PARAM_DESC_SIZE, "Expected result: data=%s len=%zu -> %d, ret=%d\n",
		  t->data, t->len, t->value, t->ret);
}

static const struct gem_split_args_test gem_test_split_args_tests[] = {
	{ .data = "4:5:6", .len = 5, .delimiter = ':', .argc = 3, .ret = 0,
		.offsets = { 0, 2, 4 } },
	{ .data = "4:5:6", .len = 5, .delimiter = ':', .argc = 2, .ret = -EINVAL },
	{ .data = "4:5:6", .len = 5, .delimiter = ':', .argc = 2, .ret = -EINVAL },
	{ .data = "4:5:6", .len = 3, .delimiter = ':', .argc = 2, .ret = 0,
		.offsets = { 0, 2 } },
	{ .data = "4:5:6", .len = 2, .delimiter = ':', .argc = 2, .ret = -EINVAL },
	{ .data = "zz@__te", .len = 7, .delimiter = '@', .argc = 2, .ret = 0,
		.offsets = { 0, 3 } },
	{ .data = "@__te", .len = 5, .delimiter = '@', .argc = 2, .ret = -EINVAL },
	{ .data = "", .len = 0, .delimiter = '@', .argc = 0, .ret = -EINVAL },
	{ .data = "@", .len = 1, .delimiter = '@', .argc = 1, .ret = -EINVAL },
	{ .data = "@", .len = 1, .delimiter = '@', .argc = 2, .ret = -EINVAL },
	{ .data = "data,:|value", .len = 12, .delimiter = ',', .argc = 2, .ret = 0,
		.offsets = { 0, 5 } },
};

static void gem_test_split_args_desc(const struct gem_split_args_test *t, char *desc)
{
	scnprintf(desc, KUNIT_PARAM_DESC_SIZE,
		  "Expected result: data=%s len=%zu delimiter=%c argc=%zu -> ret=%d\n",
		  t->data, t->len, t->delimiter, t->argc, t->ret);
}

static const struct gem_parse_substr_test gem_test_parse_substr_tests[] = {
	{ .data = "68|20|6", .len = 7, .delimiter = '|', .ret = 0, .sum = 68 + 20 + 6 },
	{ .data = "68|20|6", .len = 5, .delimiter = '|', .ret = 0, .sum = 68 + 20 },
	{ .data = "68|20|6", .len = 4, .delimiter = '|', .ret = 0, .sum = 68 + 2 },
	{ .data = "", .len = 0, .delimiter = '|', .ret = -EINVAL, .sum = 0 },
	{ .data = ",", .len = 1, .delimiter = ',', .ret = -EINVAL, .sum = 0 },
	{ .data = "5,", .len = 2, .delimiter = ',', .ret = -EINVAL, .sum = 0 },
	{ .data = "5,", .len = 1, .delimiter = ',', .ret = 0, .sum = 5 },
};

static void gem_test_parse_substr_desc(const struct gem_parse_substr_test *t, char *desc)
{
	scnprintf(desc, KUNIT_PARAM_DESC_SIZE,
		  "Expected result: data=%s len=%zu delimiter=%c sum=%u -> ret=%d\n",
		  t->data, t->len, t->delimiter, t->sum, t->ret);
}

static const struct gem_consume_ip_info_test gem_test_consume_ip_info_tests[] = {
	{ .data = "0:IP-Z:60", .len = 9, .ret = 0, .id = 0, .cntrs_num = 60,
		.name = "IP-Z" },
	{ .data = "0:IP-Z:60", .len = 6, .ret = -EINVAL },
	{ .data = "2:I:0", .len = 5, .ret = 0, .id = 2, .cntrs_num = 0, .name = "I" },
	{ .data = "560:I:0", .len = 7, .ret = -EINVAL },
	{ .data = "61:ip@,name*nr_id:9", .len = 19, .ret = 0, .id = 61, .cntrs_num = 9,
		.name = "ip@,name*nr_id" },
	{ .data = "0", .len = 1, .ret = -EINVAL },
	{ .data = "0:", .len = 2, .ret = -EINVAL },
	{ .data = "tt", .len = 2, .ret = -EINVAL },
	{ .data = ":", .len = 1, .ret = -EINVAL },
};

static void gem_test_consume_ip_info_desc(const struct gem_consume_ip_info_test *t, char *desc)
{
	scnprintf(desc, KUNIT_PARAM_DESC_SIZE,
		  "Expected result: data=%s id=%u cntrs_num=%u name=%s -> ret=%d\n",
		  t->data, t->id, t->cntrs_num, t->name, t->ret);
}

#define FDT_BLOB_0_OUT IP_LIST_MAGIC \
	"0:event_0:2,1:event_1:1,2:event_2:0|0:Valid,1:Addr,2:User,3:Cache|0:i1:5,0:i2:6"
#define FDT_BLOB_1_DATA IP_LIST_MAGIC \
	"test-counters-X02|60:BUS-MON:1,7:DATA-MON:12,2:reader:79|test-counters-X01|0:b009:0"
#define FDT_BLOB_1_OUT_1 IP_LIST_MAGIC "99:humming:1|1:VOL|60:BUS-MON:1,7:DATA-MON:12,2:reader:79|"
#define FDT_BLOB_1_OUT_2 "70:u99:0,80:v98:2|20:day,21:second,22:year|0:b009:0"

static const struct gem_ip_info_parser_test gem_test_ip_info_parser_tests[] = {
	// Valid string.
	{ .ip_info = IP_LIST_MAGIC "evgrp_0|0:i1:5,0:i2:6",
		.grp_desc = eventgrp_desc_0,
		.out = FDT_BLOB_0_OUT,
		.ret = 0 },
	{ .ip_info = FDT_BLOB_1_DATA,
		.grp_desc = eventgrp_desc_1,
		.out = FDT_BLOB_1_OUT_1 FDT_BLOB_1_OUT_2,
		.ret = 0 },

	// Invalid magic string.
	{ .ip_info = "123:456", .ret = -EINVAL },
	// Only magic.
	{ .ip_info = IP_LIST_MAGIC, .ret = -EFAULT },
};

static void gem_test_ip_info_parser_desc(const struct gem_ip_info_parser_test *t, char *desc)
{
	scnprintf(desc, KUNIT_PARAM_DESC_SIZE,
		  "Expected result: ip_info=%s -> ret=%d out=%s\n",
		  t->ip_info, t->ret, t->out ? t->out : "<null>");
}

KUNIT_ARRAY_PARAM(gem_test_kstrntou8,
		  gem_test_kstrntou8_tests,
		  gem_test_kstrntou8_desc);

KUNIT_ARRAY_PARAM(gem_test_split_args,
		  gem_test_split_args_tests,
		  gem_test_split_args_desc);

KUNIT_ARRAY_PARAM(gem_test_parse_substr,
		  gem_test_parse_substr_tests,
		  gem_test_parse_substr_desc);

KUNIT_ARRAY_PARAM(gem_test_consume_ip_info,
		  gem_test_consume_ip_info_tests,
		  gem_test_consume_ip_info_desc);

KUNIT_ARRAY_PARAM(gem_test_ip_info_parser,
		  gem_test_ip_info_parser_tests,
		  gem_test_ip_info_parser_desc);

static struct kunit_case google_gem_test_cases[] = {
	KUNIT_CASE_PARAM(gem_test_kstrntou8, gem_test_kstrntou8_gen_params),
	KUNIT_CASE_PARAM(gem_test_split_args, gem_test_split_args_gen_params),
	KUNIT_CASE_PARAM(gem_test_parse_substr, gem_test_parse_substr_gen_params),
	KUNIT_CASE_PARAM(gem_test_consume_ip_info, gem_test_consume_ip_info_gen_params),
	KUNIT_CASE_PARAM(gem_test_ip_info_parser, gem_test_ip_info_parser_gen_params),
	{}
};

static struct kunit_suite google_gem_test_suite = {
	.init = google_gem_test_init,
	.exit = google_gem_test_exit,
	.name = "google_gem_test",
	.test_cases = google_gem_test_cases,
};

kunit_test_suite(google_gem_test_suite);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GEM KUnit test");
MODULE_LICENSE("GPL");
