// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google, Inc.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/trusty/trusty.h>
#include <linux/trusty/trusty_ipc.h>
#include <linux/workqueue.h>

#include "trusty-wakelock.h"

/* Milliseconds to wait for a response buffer */
#define TZ_BUF_TIMEOUT 10000

/* Driver state */
struct trusty_wakelock_state {
	struct device *dev;
	struct tipc_chan *chan;
	struct workqueue_struct *wq;
};

/* Work queue item for sending responses */
struct trusty_wakelock_work {
	struct work_struct work;
	struct trusty_wakelock_state *s;
	struct suspendservice_rsp rsp;
};

static void trusty_wakelock_handle_event(void *cb_arg, int event)
{
	struct trusty_wakelock_state *s = cb_arg;

	switch (event) {
	case TIPC_CHANNEL_CONNECTED:
		dev_info(s->dev, "connected to trusty suspendservice\n");
		break;
	case TIPC_CHANNEL_DISCONNECTED:
		dev_err(s->dev, "connection lost: disconnected from trusty suspendservice\n");
		break;
	case TIPC_CHANNEL_SHUTDOWN:
		dev_err(s->dev, "connection lost: channel shutdown\n");
		break;
	}
}

static void trusty_wakelock_response_work(struct work_struct *work)
{
	struct trusty_wakelock_work *tw_work =
		container_of(work, struct trusty_wakelock_work, work);
	struct trusty_wakelock_state *s = tw_work->s;
	struct tipc_msg_buf *tx_buf;
	void *tx_data;
	int ret;

	/* Get a buffer - noting this can block, hence on a workqueue */
	tx_buf = tipc_chan_get_txbuf_timeout(s->chan, TZ_BUF_TIMEOUT);
	if (IS_ERR(tx_buf)) {
		dev_err(s->dev, "failed to allocate tx buffer for response: %ld\n",
				PTR_ERR(tx_buf));
		goto out;
	}

	tx_data = mb_put_data(tx_buf, sizeof(tw_work->rsp));
	memcpy(tx_data, &tw_work->rsp, sizeof(tw_work->rsp));

	ret = tipc_chan_queue_msg(s->chan, tx_buf);
	if (ret) {
		dev_err(s->dev, "failed to queue response message: %d\n", ret);
		tipc_chan_put_txbuf(s->chan, tx_buf);
	}

out:
	kfree(tw_work);
}

static struct tipc_msg_buf *trusty_wakelock_handle_msg(void *cb_arg,
						       struct tipc_msg_buf *rx_buf)
{
	struct trusty_wakelock_state *s = cb_arg;
	struct suspendservice_req *req;
	struct trusty_wakelock_work *tw_work;
	size_t msg_len = mb_avail_data(rx_buf);
	uint32_t error = SUSPENDSERVICE_ERR_OK;

	dev_dbg(s->dev, "received message, length: %zu\n", msg_len);

	if (msg_len < sizeof(*req)) {
		dev_err(s->dev, "message too short: %zu < %zu\n",
			msg_len, sizeof(*req));
		return rx_buf;
	}

	req = mb_get_data(rx_buf, sizeof(*req));
	if (!req) {
		dev_err(s->dev, "failed to get message data\n");
		return rx_buf;
	}

	/* Handle the command - noting no ref-counting as that happens in TZ */
	switch (req->cmd) {
	case SUSPENDSERVICE_CMD_ACQUIRE_BLOCK:
		dev_dbg(s->dev, "acquire suspend block requested\n");
		pm_stay_awake(s->dev);
		break;
	case SUSPENDSERVICE_CMD_RELEASE_BLOCK:
		dev_dbg(s->dev, "release suspend block requested\n");
		pm_relax(s->dev);
		break;
	default:
		dev_warn_once(s->dev, "unknown command: %u\n", req->cmd);
		error = SUSPENDSERVICE_ERR_UNKNOWN_CMD;
		break;
	}

	/* Defer response sending to avoid blocking the IPC Rx thread, which
	 * could otherwise cause a deadlock against tipc_chan_get_txbuf_timeout().
	 */
	tw_work = kzalloc(sizeof(*tw_work), GFP_KERNEL);
	if (!tw_work) {
		dev_err(s->dev, "failed to allocate work item for response\n");
		return rx_buf;
	}

	tw_work->s = s;
	tw_work->rsp.cmd = req->cmd | SUSPENDSERVICE_CMD_RESP;
	tw_work->rsp.error = error;
	INIT_WORK(&tw_work->work, trusty_wakelock_response_work);

	if (!queue_work(s->wq, &tw_work->work)) {
		dev_err(s->dev, "failed to queue response work\n");
		kfree(tw_work);
	}

	return rx_buf;
}

static void trusty_wakelock_handle_release(void *cb_arg)
{
	struct trusty_wakelock_state *s = cb_arg;

	dev_dbg(s->dev, "channel released, releasing wakelock\n");
	pm_relax(s->dev);
}

static const struct tipc_chan_ops trusty_wakelock_ops = {
	.handle_event = trusty_wakelock_handle_event,
	.handle_msg = trusty_wakelock_handle_msg,
	.handle_release = trusty_wakelock_handle_release,
};

static int trusty_wakelock_probe(struct platform_device *pdev)
{
	struct trusty_wakelock_state *s;
	int ret;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->dev = &pdev->dev;

	ret = device_init_wakeup(s->dev, true);
	if (ret) {
		dev_err(s->dev, "failed to initialize wakeup source: %d\n", ret);
		goto err_init_wakeup;
	}

	s->wq = alloc_ordered_workqueue("trusty-wakelock-wq", 0);
	if (!s->wq) {
		ret = -ENOMEM;
		dev_err(s->dev, "failed to allocate workqueue\n");
		goto err_alloc_wq;
	}

	/* Create TIPC channel. NULL dev means use default trusty device */
	s->chan = tipc_create_channel(NULL, &trusty_wakelock_ops, s);
	if (IS_ERR(s->chan)) {
		ret = PTR_ERR(s->chan);
		if (ret == -ENOENT) {
			dev_dbg(s->dev, "Trusty IPC device not ready, deferring probe\n");
			ret = -EPROBE_DEFER;
			goto err_create_channel;
		}
		dev_err(s->dev, "failed to create TIPC channel: %d\n", ret);
		goto err_create_channel;
	}

	platform_set_drvdata(pdev, s);

	/* Connect to the trusty service, noting this is asynchronous */
	ret = tipc_chan_connect(s->chan, TZ_SUSPEND_SERVICE_PORT);
	if (ret) {
		dev_err(s->dev, "failed to connect to %s: %d\n",
				TZ_SUSPEND_SERVICE_PORT, ret);
		goto err_connect;
	}

	return 0;

err_connect:
	tipc_chan_destroy(s->chan);
err_create_channel:
	destroy_workqueue(s->wq);
err_alloc_wq:
	device_init_wakeup(s->dev, false);
err_init_wakeup:
	kfree(s);
	return ret;
}

static void trusty_wakelock_remove(struct platform_device *pdev)
{
	struct trusty_wakelock_state *s = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "removing trusty-wakelock\n");

	/* Stop the channel so no more messages can be received */
	if (s->chan)
		tipc_chan_shutdown(s->chan);

	/* Drain and destroy workqueue - send any pending responses */
	if (s->wq)
		destroy_workqueue(s->wq);

	/* Finally completely destroy the channel */
	if (s->chan)
		tipc_chan_destroy(s->chan);

	device_init_wakeup(s->dev, false);

	kfree(s);
}

static const struct of_device_id trusty_wakelock_of_match[] = {
	{ .compatible = "android,trusty-wakelock", },
	{},
};
MODULE_DEVICE_TABLE(trusty, trusty_wakelock_of_match);

static struct platform_driver trusty_wakelock_driver = {
	.probe = trusty_wakelock_probe,
	.remove = trusty_wakelock_remove,
	.driver = {
		.name = "trusty-wakelock",
		.of_match_table = trusty_wakelock_of_match,
	},
};

module_platform_driver(trusty_wakelock_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Trusty wakelock driver");
