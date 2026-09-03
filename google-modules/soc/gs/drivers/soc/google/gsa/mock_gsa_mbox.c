// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */
#include <kunit/test.h>
#include <kunit/visibility.h>
#include <linux/platform_device.h>
#include <linux/gfp_types.h>
#include <linux/sched.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include "mock_gsa_mbox.h"
#include "gsa_mbox.h"
#include "gsa_priv.h"

/* Shared register semantics for outgoing messages */
#define MBOX_SR_SEND_CMD_IDX 0
#define MBOX_SR_SEND_ARGC_IDX 1
#define MBOX_SR_SEND_ARGV_IDX 2

/* Shared register semantics for incoming messages */
#define MBOX_SR_RECV_CMD_IDX 0
#define MBOX_SR_RECV_ERR_IDX 1
#define MBOX_SR_RECV_ARGC_IDX 2
#define MBOX_SR_RECV_ARGV_IDX 3

struct mock_gsa_mbox {
	struct platform_device *pdev;
	/** struct mock_send_mbox
	 * @called: indicates if send_mbox_msg was called.
	 *	Populated by mock send_mbox_msg.
	 *	Used to verify that send_mbox_msg was called.
	 * @received: holds the args that were received by send_mbox_msg.
	 *	Populated by mock_send_mbox_msg.
	 *	Used to verify send_mbox_msg received the expected args.
	 * @respond_with: set before send_mbox_msg is called, mock will return these args.
	 *	Populated by the caller before send msg cmd is sent.
	 *	Used to check how the tested function responds to send_mbox_msg response.
	 */
	struct mock_send_mbox {
		bool called;
		struct mock_gsa_recvd received;
		struct mock_gsa_resp respond_with;
	} mock_send_mbox;
};

/**
 * init_test_gsa_mbox() - Creates a gsa_mbox suitable for testing.
 * @test: kunit test context this is called from.
 * @pdev: platform_device belonging to kunit test.
 * @send_mbox_msg: mock function that will replace call to gsa.
 *
 * Return: mocked gsa_mbox
 */
struct gsa_mbox *init_test_gsa_mbox(struct kunit *test, struct platform_device *pdev,
				    int (*send_mbox_msg)(struct mbox_chan *chan, void *mssg))
{
	struct gsa_mbox *mbox = kunit_kzalloc(test, sizeof(*mbox), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mbox);

	mbox->send_mbox_msg = send_mbox_msg;
	mbox->dev = &pdev->dev;
	mbox->msg_slot.client = (struct mbox_client){
		.dev = &pdev->dev,
		.tx_block = true,
		.tx_tout = 100,
		.knows_txdone = false,
		.rx_callback = mbox_rx_callback,
		.tx_done = mbox_tx_done,
		.tx_prepare = mbox_tx_prepare,
	};
	mbox->msg_slot.rsp_buffer = NULL;
	/*
	 * The send_mbox_message function ptr needs to receive a pointer to channel.
	 * The mock send_mbox_message needs to call the rx_callback, which needs a client.
	 * Attach client to the channel so we can access it within the mock function.
	 */
	mbox->msg_slot.channel = kunit_kzalloc(test, sizeof(mbox->msg_slot.channel), GFP_KERNEL);
	mbox->msg_slot.channel->cl = &mbox->msg_slot.client;
	mutex_init(&mbox->share_reg_lock);
	init_completion(&mbox->msg_slot.mbox_cmd_completion);

	return mbox;
}

/* Allows mbox to be attached to pdev via the gsa_dev_state struct that is intnernal to this file.*/
void gsa_set_test_pdev_drvdata(struct kunit *test, struct platform_device *pdev,
			       struct gsa_mbox *mbox)
{
	struct gsa_dev_state *s = kunit_kzalloc(test, sizeof(*s), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, s);

	s->mb = mbox;
	platform_set_drvdata(pdev, s);
}

static int mock_send_mbox_cmd(struct mbox_chan *chan, void *mssg)
{
	struct kunit *test = current->kunit_test;
	struct mock_gsa_mbox *mock_mbox = test->priv;
	struct mock_gsa_recvd *mock_received = &mock_mbox->mock_send_mbox.received;
	struct mock_gsa_resp *mock_response = &mock_mbox->mock_send_mbox.respond_with;
	/* Allocate 3 + argc because we have idx for cmd, error, argc + n args. */
	u32 *resp_buffer = kunit_kcalloc(test, 3 + mock_response->argc, sizeof(u32), GFP_KERNEL);
	u32 *recv_buffer = (u32 *)mssg;
	size_t i = 0;

	mock_mbox->mock_send_mbox.called = true;

	/* Capture the received arguments so they can be checked against expectations. */
	mock_received->cmd = recv_buffer[MBOX_SR_SEND_CMD_IDX];
	mock_received->argc = recv_buffer[MBOX_SR_SEND_ARGC_IDX];
	mock_received->args = kunit_kcalloc(test, mock_received->argc, sizeof(u32), GFP_KERNEL);
	for (i = 0; i < mock_received->argc; i++)
		mock_received->args[i] = recv_buffer[MBOX_SR_SEND_ARGV_IDX + i];

	/* Respond with the mocked response to test how subject code responds. */
	resp_buffer[MBOX_SR_RECV_CMD_IDX] = mock_response->cmd;
	resp_buffer[MBOX_SR_RECV_ERR_IDX] = mock_response->err;
	resp_buffer[MBOX_SR_RECV_ARGC_IDX] = mock_response->argc;
	for (i = 0; i < mock_response->argc; i++)
		resp_buffer[MBOX_SR_RECV_ARGV_IDX + i] = mock_response->args[i];

	chan->cl->rx_callback(chan->cl, (void *)resp_buffer);
	return 0;
}

struct mock_gsa_mbox *mock_gsa_mbox_init(struct kunit *test)
{
	struct mock_gsa_mbox *mock_mbox;
	struct gsa_mbox *mbox;

	mock_mbox = kunit_kzalloc(test, sizeof(*mock_mbox), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mock_mbox);

	mock_mbox->pdev = platform_device_register_simple("mock_gsa", PLATFORM_DEVID_AUTO, NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, mock_mbox->pdev);

	mbox = init_test_gsa_mbox(test, mock_mbox->pdev, mock_send_mbox_cmd);

	/* Attach gsa mbox to the pdev via device state. */
	gsa_set_test_pdev_drvdata(test, mock_mbox->pdev, mbox);

	return mock_mbox;
}

struct device *mock_gsa_get_device(struct mock_gsa_mbox *mock_mbox)
{
	return &mock_mbox->pdev->dev;
}

bool gsa_mock_was_called(struct mock_gsa_mbox *mock_mbox)
{
	return mock_mbox->mock_send_mbox.called;
}

struct mock_gsa_resp *gsa_mock_resp(struct mock_gsa_mbox *mock_mbox)
{
	return &mock_mbox->mock_send_mbox.respond_with;
}

const struct mock_gsa_recvd *gsa_mock_args_recvd(struct mock_gsa_mbox *mock_mbox)
{
	return &mock_mbox->mock_send_mbox.received;
}

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL v2");
