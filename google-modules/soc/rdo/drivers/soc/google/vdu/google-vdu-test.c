// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 */

#include <kunit/test.h>
#include <kunit/visibility.h>
#include <linux/base64.h>
#include <linux/byteorder/generic.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "google-vdu.h"
#include "vdu_service.h"

/* Error handling tests */
static void gvdu_test_handle_mailbox_error_no_error(struct kunit *test)
{
	int64_t ret;
	const int success_code = 0;
	uint32_t header = goog_mba_nq_xport_create_hdr(1, 1);

	ret = gvdu_handle_mailbox_error(NULL, success_code, &header);

	KUNIT_ASSERT_EQ(test, success_code, ret);
}

static void gvdu_test_handle_mailbox_error_with_gdmc_error(struct kunit *test)
{
	int64_t ret;
	const int16_t gdmc_error_code = -123;
	uint32_t header = goog_mba_nq_xport_create_hdr(1, 1);

	goog_mba_nq_xport_set_error(&header, true);
	goog_mba_nq_xport_set_data(&header, (uint16_t)gdmc_error_code);

	ret = gvdu_handle_mailbox_error(NULL, 0, &header);

	KUNIT_EXPECT_EQ(test, gdmc_error_code, ret);
}

/* Base64 and directive decoding tests */
struct gvdu_test_priv {
	struct gvdu_base base;
	struct device dev;
};

static int gvdu_decode_test_init(struct kunit *test)
{
	struct gvdu_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	dev_set_name(&priv->dev, "google-vdu-test");
	dev_set_drvdata(&priv->dev, &priv->base);
	priv->base.dev = &priv->dev;

	test->priv = priv;
	return 0;
}

static void gvdu_decode_test_exit(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;

	kfree(priv->base.grant_buffer);
	kfree(priv->base.delegate_buffer);
}

static void gvdu_test_decode_base64_buffer_success(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;
	char *decoded_buf = NULL;
	ssize_t len;
	const char *base64_source = "SGVsbG8gV29ybGQ=";
	const char *base64_decoded = "Hello World";

	/* Reasonably high limitation */
	priv->base.vdu_directive_max_size = 0xFF;

	len = gvdu_decode_base64_buffer(&priv->dev, base64_source,
					strlen(base64_source), &decoded_buf);

	KUNIT_ASSERT_LE(test, 0, len);
	KUNIT_ASSERT_NOT_NULL(test, decoded_buf);
	KUNIT_EXPECT_EQ(test, strlen(base64_decoded), len);
	KUNIT_EXPECT_MEMEQ(test, base64_decoded, decoded_buf, len);

	kfree(decoded_buf);
}

static void gvdu_test_decode_base64_buffer_too_large(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;
	char *decoded_buf = NULL;
	ssize_t ret;
	const char *base64_source = "SGVsbG8gV29ybGQ=";

	/* base64_source decodes to 11 bytes */
	priv->base.vdu_directive_max_size = 10;

	ret = gvdu_decode_base64_buffer(&priv->dev, base64_source,
					strlen(base64_source), &decoded_buf);

	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
	KUNIT_EXPECT_NULL(test, decoded_buf);

	if (decoded_buf != NULL)
		kfree(decoded_buf);
}

static void gvdu_test_decode_cdev_directive_success(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;
	struct gvdu_base *base = &priv->base;
	ssize_t ret;
	const char *b64_grant = "Z3JhbnQ=";
	const char *b64_grant_decoded = "grant";
	const char *b64_delegate = "ZGVsZWdhdGU=";
	const char *b64_delegate_decoded = "delegate";
	const size_t b64_grant_len = strlen(b64_grant);
	const size_t b64_delegate_len = strlen(b64_delegate);
	const size_t total_len =
		sizeof(uint32_t) * 2 + b64_grant_len + b64_delegate_len;
	char *buf = kunit_kmalloc(test, total_len, GFP_KERNEL);
	__le32 grant_len_le = cpu_to_le32(b64_grant_len);
	__le32 delegate_len_le = cpu_to_le32(b64_delegate_len);
	size_t current_size = 0;

	KUNIT_ASSERT_NOT_NULL(test, buf);

	/* Construct the buffer: [grant_len][grant_b64][delegate_len][delegate_b64] */
	memcpy(buf, &grant_len_le, sizeof(uint32_t));
	current_size += sizeof(uint32_t);
	memcpy(buf + current_size, b64_grant, b64_grant_len);
	current_size += b64_grant_len;
	memcpy(buf + current_size, &delegate_len_le, sizeof(uint32_t));
	current_size += sizeof(uint32_t);
	memcpy(buf + current_size, b64_delegate, b64_delegate_len);

	base->char_dev.data_buffer = buf;
	base->char_dev.buffer_length = total_len;
	base->vdu_directive_max_size = 0xFF;

	ret = gvdu_decode_base64_cdev_directive(&priv->dev);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_ASSERT_NOT_NULL(test, base->grant_buffer);
	KUNIT_EXPECT_EQ(test, strlen(b64_grant_decoded), base->grant_length);
	KUNIT_EXPECT_MEMEQ(test, b64_grant_decoded, base->grant_buffer,
			   strlen(b64_grant_decoded));

	KUNIT_ASSERT_NOT_NULL(test, base->delegate_buffer);
	KUNIT_EXPECT_EQ(test, base->delegate_length,
			strlen(b64_delegate_decoded));
	KUNIT_EXPECT_MEMEQ(test, b64_delegate_decoded, base->delegate_buffer,
			   strlen(b64_delegate_decoded));
}

static void gvdu_test_decode_buffer_too_short_for_len(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;
	struct gvdu_base *base = &priv->base;
	ssize_t ret;
	char *buf = kunit_kmalloc(test, 2, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, buf);

	base->char_dev.data_buffer = buf;
	base->char_dev.buffer_length = 2;
	base->vdu_directive_max_size = 0xFF;

	/* Buffer is 2 bytes, too short for the length field (uint32_t) */
	ret = gvdu_decode_base64_cdev_directive(&priv->dev);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
}

static void gvdu_test_decode_short_grant_data(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;
	struct gvdu_base *base = &priv->base;
	ssize_t ret;
	__le32 grant_len_le;
	char *buf = kunit_kmalloc(test, 8, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, buf);

	base->char_dev.data_buffer = buf;
	base->char_dev.buffer_length = 8;
	base->vdu_directive_max_size = 0xFF;

	/* Claimed grant len (5) > remaining buffer (8 - 4 = 4) */
	grant_len_le = cpu_to_le32(5);
	memcpy(buf, &grant_len_le, sizeof(uint32_t));

	ret = gvdu_decode_base64_cdev_directive(&priv->dev);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
}

static void gvdu_test_decode_delegate_len_mismatch(struct kunit *test)
{
	struct gvdu_test_priv *priv = test->priv;
	struct gvdu_base *base = &priv->base;
	ssize_t ret;
	__le32 grant_len_le;
	__le32 delegate_len_le;
	char *buf = kunit_kmalloc(test, 20, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, buf);

	base->char_dev.data_buffer = buf;
	base->char_dev.buffer_length = 20;
	base->vdu_directive_max_size = 0xFF;

	grant_len_le = cpu_to_le32(4);
	/* Claimed delegate len (9) > remaining buffer (20 - 4 - 4 - 4 = 8) */
	delegate_len_le = cpu_to_le32(9);

	memcpy(buf, &grant_len_le, sizeof(uint32_t));
	memcpy(buf + 4, "YWJj", 4);
	memcpy(buf + 8, &delegate_len_le, sizeof(uint32_t));

	ret = gvdu_decode_base64_cdev_directive(&priv->dev);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
}

/* Define test cases */
static struct kunit_case gvdu_error_handling_test_cases[] = {
	KUNIT_CASE(gvdu_test_handle_mailbox_error_no_error),
	KUNIT_CASE(gvdu_test_handle_mailbox_error_with_gdmc_error),
	{},
};

static struct kunit_case gvdu_decode_test_cases[] = {
	KUNIT_CASE(gvdu_test_decode_base64_buffer_success),
	KUNIT_CASE(gvdu_test_decode_base64_buffer_too_large),
	KUNIT_CASE(gvdu_test_decode_cdev_directive_success),
	KUNIT_CASE(gvdu_test_decode_buffer_too_short_for_len),
	KUNIT_CASE(gvdu_test_decode_short_grant_data),
	KUNIT_CASE(gvdu_test_decode_delegate_len_mismatch),
	{},
};

static struct kunit_suite gvdu_error_handling_suite = {
	.name = "gvdu-error-handling-test",
	.test_cases = gvdu_error_handling_test_cases,
};

static struct kunit_suite gvdu_decode_suite = {
	.name = "gvdu-decode-test",
	.init = gvdu_decode_test_init,
	.exit = gvdu_decode_test_exit,
	.test_cases = gvdu_decode_test_cases,
};

kunit_test_suites(&gvdu_error_handling_suite, &gvdu_decode_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_AUTHOR("Kanstantsin Yarmash <kyarmash@google.com>");
MODULE_DESCRIPTION("Google VDU KUnit test");
MODULE_LICENSE("GPL");
