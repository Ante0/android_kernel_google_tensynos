// SPDX-License-Identifier: GPL-2.0-only
/*
 * Testing for Google SoC LPCM resets via CPM.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <kunit/test.h>

#include "rst-cpm.h"

#define HSIO_S_LPCM_ID 6
#define RST_ID_SAMPLE 0
#define UNKNOWN_PAYLOAD_DATA_ERROR -1000

struct rst_cpm_test {
	struct goog_cpm_rst cpm_rst;
	struct cpm_msg msg;
	struct mbox_chan chan;
};

struct rst_cpm_test_send_mba_test {
	int response_payload_data;
	int err_code;
};

struct rst_cpm_test_init_payload_test {
	u8 lpcm_id;
	u8 rst_id;
	u8 op_id;
	int expected_ans;
};

static int google_mba_mock_send_msg_and_block(struct cpm_iface_client *client,
					      struct cpm_iface_req *req)
{
	struct cpm_msg *msg = container_of(req, struct cpm_msg, cpm_req);
	struct rst_cpm_test *fake_rst_cpm = container_of(msg, struct rst_cpm_test, msg);

	req->resp_msg->payload[0] = fake_rst_cpm->msg.resp_msg.payload[0];
	return 0;
}

static inline void rst_cpm_test_send_mba_set_resp(struct cpm_msg *msg, int response_payload_data)
{
	msg->resp_msg.payload[0] = response_payload_data;
}

static void rst_cpm_test_send_mba(struct kunit *test)
{
	struct rst_cpm_test *fake_rst_cpm;
	const struct rst_cpm_test_send_mba_test *params = test->param_value;
	int err;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, params);
	fake_rst_cpm = test->priv;
	rst_cpm_test_send_mba_set_resp(&fake_rst_cpm->msg, params->response_payload_data);

	err = goog_cpm_rst_send_mba_mail(&fake_rst_cpm->cpm_rst, RST_ID_SAMPLE, DEASSERT_OP_ID);
	KUNIT_ASSERT_EQ(test, err, params->err_code);
}

static void rst_cpm_test_init_payload(struct kunit *test)
{
	struct rst_cpm_test *fake_rst_cpm = test->priv;
	struct cpm_msg msg;
	const struct rst_cpm_test_init_payload_test *params = test->param_value;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_rst_cpm);
	fake_rst_cpm->cpm_rst.lpcm_id = params->lpcm_id;
	msg.req_msg = fake_rst_cpm->msg.req_msg;
	goog_cpm_init_payload(&fake_rst_cpm->cpm_rst, &msg, params->rst_id, params->op_id);
	KUNIT_ASSERT_EQ(test, msg.cpm_req.msg_type, REQ_MSG);
	KUNIT_ASSERT_EQ(test, msg.cpm_req.dst_id, LCPM_REMOTE_CHANNEL);
	KUNIT_ASSERT_EQ(test, msg.cpm_req->req_msg.payload[0], params->expected_ans);
}

static int rst_cpm_test_init(struct kunit *test)
{
	struct rst_cpm_test *fake_rst_cpm;
	struct goog_cpm_rst *cpm_rst;

	cpm_rst = kunit_kzalloc(test, sizeof(*cpm_rst), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cpm_rst);
	cpm_rst->cpm_mbox = kunit_kzalloc(test, sizeof(*cpm_rst->cpm_mbox), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cpm_rst->cpm_mbox);
	fake_rst_cpm = container_of(cpm_rst, struct rst_cpm_test, cpm_rst);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fake_rst_cpm);

	cpm_rst->cpm_mbox->client = NULL; /* shouldn't be needed */

	cpm_rst->cpm_mbox->send_msg_and_block = google_mba_mock_send_msg_and_block;
	test->priv = fake_rst_cpm;
	return 0;
}

static const
struct rst_cpm_test_init_payload_test rst_cpm_test_init_payload_tests[] = {
	/*
	 * The last value is expected_ans.
	 * This number is the combination of LPCM_CMD_SET_RST, HSIO_S_LPCM_ID, RST_ID_SAMPLE,
	 * ASSERT_OP_ID (from low bit to high bit) in struct goog_cpm_lpcm_service_req.
	 */
	{HSIO_S_LPCM_ID, RST_ID_SAMPLE, ASSERT_OP_ID, 1540},
	{HSIO_S_LPCM_ID, RST_ID_SAMPLE, DEASSERT_OP_ID, 16778756}
};

static void
rst_cpm_test_init_payload_desc(const struct rst_cpm_test_init_payload_test *t, char *desc)
{
	sprintf(desc, "req_id = %d, lpcm id = %d, rst id = %d, op id = %d\n", LPCM_CMD_SET_RST,
		t->lpcm_id, t->rst_id, t->op_id);
}

KUNIT_ARRAY_PARAM(rst_cpm_test_init_payload,
		  rst_cpm_test_init_payload_tests,
		  rst_cpm_test_init_payload_desc);

static const
struct rst_cpm_test_send_mba_test rst_cpm_test_send_mba_tests[] = {
	{NO_ERROR, 0},
	{ERR_LPCM_NOT_SUPPORTED, -EPROTO},
	{ERR_LPCM_INVALID_ARGS, -EINVAL},
	{ERR_LPCM_TIMED_OUT, -ETIMEDOUT},
	{ERR_LPCM_GENERIC, -ESERVERFAULT},
	{UNKNOWN_PAYLOAD_DATA_ERROR, -EPROTO}
};

static void
rst_cpm_test_send_mba_desc(const struct rst_cpm_test_send_mba_test *t, char *desc)
{
	sprintf(desc, "receive payload %d, should get error num %d\n", t->response_payload_data,
		t->err_code);
}

KUNIT_ARRAY_PARAM(rst_cpm_test_send_mba,
		  rst_cpm_test_send_mba_tests,
		  rst_cpm_test_send_mba_desc);

static struct kunit_case rst_cpm_test_cases[] = {
	KUNIT_CASE_PARAM(rst_cpm_test_send_mba, rst_cpm_test_send_mba_gen_params),
	KUNIT_CASE_PARAM(rst_cpm_test_init_payload, rst_cpm_test_init_payload_gen_params),
	{}
};

static struct kunit_suite rst_cpm_test_suite = {
	.init = rst_cpm_test_init,
	.name = "rst_cpm_test",
	.test_cases = rst_cpm_test_cases,
};

kunit_test_suite(rst_cpm_test_suite);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google CPM Reset Driver KUnit test");
MODULE_LICENSE("GPL");
