// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google MailBox Array (MBA) Aggregator
 *
 * Copyright (c) 2023 Google LLC
 */
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>

#include <soc/google/goog-mba-aggr.h>

#include "goog-mba-aggr.h"

#define CREATE_TRACE_POINTS
#include "goog-mba-aggr-trace.h"


#define ENQUEUE_REQ_WAIT_MS         10
#define MAX_ENQUEUE_REQ_TIMEOUT_MS  10000 // Max timeout 10s for waiting for space in the req queue

static struct goog_mba_aggr_data goog_mba_aggr_gdata = {
	.head = LIST_HEAD_INIT(goog_mba_aggr_gdata.head),
	.nr_services = 0,
};

/*
 * goog_mba_aggr_create_session_pool - initialize shared session pool
 * @dev:			Device driver structure.
 * @gservice:			Service assigned to this client.
 *
 * Returns: 0, if session pool initialized successfully.
 * otherwise, negative value for error code.
 */
static int goog_mba_aggr_create_session_pool(struct device *dev,
					     struct goog_mba_aggr_service *gservice)
{
	/*
	 * This is calculated as the sum of:
	 *   - The maximum request queue size (MAX_REQ_Q_SIZE)
	 *   - Twice the maximum number of tx channels
	 */
	const int max_client_sessions = MAX_REQ_QUEUE_LENGTH + gservice->nr_client_tx_chans * 2;
	struct goog_mba_aggr_session *session_pool;
	u32 *req_msgs;
	int i;

	session_pool = devm_kcalloc(dev, max_client_sessions, sizeof(*session_pool), GFP_KERNEL);
	if (!session_pool)
		return -ENOMEM;

	req_msgs = devm_kcalloc(dev, max_client_sessions * gservice->payload_size,
				sizeof(*req_msgs), GFP_KERNEL);
	if (!req_msgs)
		return -ENOMEM;

	for (i = 0; i < max_client_sessions; i++) {
		session_pool[i].req_msg = &req_msgs[i * gservice->payload_size];
		session_pool[i].status = SESSION_FREE_STATUS;
		spin_lock_init(&session_pool[i].lock);
		init_completion(&session_pool[i].complete);
	}

	gservice->session_pool = session_pool;
	gservice->nr_sessions = max_client_sessions;

	return 0;
}

/*
 * goog_mba_aggr_get_session - search and get available session structure from the pool
 *
 * Returns: valid pointer for reserved goog_mba_aggr_session.
 * Null if no available goog_mba_aggr_session.
 */
static struct goog_mba_aggr_session *
goog_mba_aggr_get_session(struct goog_mba_aggr_service *gservice)
{
	struct goog_mba_aggr_session *session;
	int i;

	for (i = 0; i < gservice->nr_sessions; i++) {
		session = &gservice->session_pool[i];

		scoped_guard(spinlock_irqsave_try, &session->lock) {
			if (session->status == SESSION_FREE_STATUS) {
				session->status = SESSION_RESERVE_STATUS;
				reinit_completion(&session->complete);

				return session;
			}
		}
	}

	return NULL;
}

/*
 * goog_mba_aggr_host_put_session - host release its reference to given session
 * @session:			Host's session to be put.
 */
static void goog_mba_aggr_host_put_session(struct goog_mba_aggr_session *session)
{
	scoped_guard(spinlock_irqsave, &session->lock)
		session->status |= SESSION_HOST_DONE;
}

/*
 * goog_mba_aggr_client_put_session - client release its reference to given session
 * @session:			Client's session to be put.
 */
static void goog_mba_aggr_client_put_session(struct goog_mba_aggr_session *session)
{
	scoped_guard(spinlock_irqsave, &session->lock)
		session->status |= SESSION_CLIENT_DONE;
}

struct goog_mba_aggr_service *goog_mba_request_gservice(struct device *dev, int index,
							goog_mbox_aggr_resp_cb_t host_tx_cb,
							void *prv_data)
{
	struct goog_mba_aggr_service *gservice;
	struct device_node *np;

	np = of_parse_phandle(dev->of_node, "goog-mba-aggrs", index);
	if (!np) {
		dev_err(dev, "Failed to parse goog-mba-aggrs property\n");
		return ERR_PTR(-ENODEV);
	}

	list_for_each_entry(gservice, &goog_mba_aggr_gdata.head, entry)
		if (gservice->dev->of_node == np)
			break;
	of_node_put(np);

	if (list_entry_is_head(gservice, &goog_mba_aggr_gdata.head, entry))
		return ERR_PTR(-EPROBE_DEFER);

	gservice->host_tx_cb = host_tx_cb;
	gservice->host_tx_cb_prv_data = prv_data;

	return gservice;
}
EXPORT_SYMBOL_GPL(goog_mba_request_gservice);

/*
 * Return: Non-negative integer for successful submission
 */
static int goog_mba_aggr_try_to_submit(struct goog_mba_aggr_service *gservice)
{
	struct device *dev = gservice->dev;
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;
	unsigned long flags;
	int ret = -EBUSY;
	int i;

	spin_lock_irqsave(&gservice->lock, flags);

	if (gservice->req_queue_size == 0) {
		ret = -ENOENT;
		goto exit;
	}

	for (i = 0; i < gservice->nr_client_tx_chans; i++) {
		mbox_chan_info = &gservice->client_tx_chans[i];
		if (!mbox_chan_info->req)
			break;
	}
	if (mbox_chan_info->req)
		goto exit;

	mbox_chan_info->req = gservice->req_queue[gservice->req_queue_tail];

	gservice->req_queue_tail = (gservice->req_queue_tail + 1) % MAX_REQ_QUEUE_LENGTH;
	gservice->req_queue_size--;

	ret = mbox_send_message(mbox_chan_info->chan, mbox_chan_info->req->req_buf);
	trace_goog_mba_aggr_submit(mbox_chan_info, gservice);
exit:
	spin_unlock_irqrestore(&gservice->lock, flags);

	dev_dbg(dev, "gservice[%s]: submit message done(ret=%d)\n", gservice->name, ret);

	return ret;
}

static struct goog_mba_aggr_session *
goog_mba_aggr_queue_req(struct goog_mba_aggr_service *gservice,
			struct goog_mba_aggr_request *client_req)
{
	struct device *dev = gservice->dev;
	unsigned long flags;
	struct goog_mba_aggr_session *session;

	spin_lock_irqsave(&gservice->lock, flags);

	if (gservice->req_queue_size == MAX_REQ_QUEUE_LENGTH) {
		spin_unlock_irqrestore(&gservice->lock, flags);
		return ERR_PTR(-EBUSY);
	}

	session = goog_mba_aggr_get_session(gservice);
	if (!session) {
		spin_unlock_irqrestore(&gservice->lock, flags);
		dev_dbg(dev, "Failed to get session\n");
		return ERR_PTR(-EBUSY);
	}

	session->req = *client_req;
	session->req.req_buf = session->req_msg;
	memcpy(session->req_msg, client_req->req_buf,
	       sizeof(*session->req_msg) * gservice->payload_size);

	gservice->req_queue[gservice->req_queue_head] = &session->req;

	gservice->req_queue_head = (gservice->req_queue_head + 1) % MAX_REQ_QUEUE_LENGTH;
	gservice->req_queue_size++;
	trace_goog_mba_aggr_queue_req(gservice, session);

	spin_unlock_irqrestore(&gservice->lock, flags);

	return session;
}

int goog_mba_aggr_send_message(struct goog_mba_aggr_service *gservice,
			       struct goog_mba_aggr_request *client_req)
{
	struct device *dev = gservice->dev;
	unsigned long wait = MAX_SCHEDULE_TIMEOUT;
	/* Set a max timeout value, if client request does not specify a timeout */
	unsigned long req_tout_ms = client_req->tout_ms ?
				    client_req->tout_ms : MAX_ENQUEUE_REQ_TIMEOUT_MS;
	unsigned long timeout = jiffies + msecs_to_jiffies(req_tout_ms);
	struct goog_mba_aggr_session *session;
	int ret = 0;

	dev_dbg(dev, "gservice[%s]: Start to send message\n", gservice->name);

	while (IS_ERR(session = goog_mba_aggr_queue_req(gservice, client_req))) {
		int err = PTR_ERR(session);

		/*
		 * For the case of -EBUSY, resources may be busy and not able to service right now;
		 * We can try again later if we are still in the valid timeout duration.
		 */
		if (err == -EBUSY && time_before(jiffies, timeout)) {
			msleep(ENQUEUE_REQ_WAIT_MS);
			continue;
		}

		return err;
	}

	goog_mba_aggr_try_to_submit(gservice);
	if (!client_req->async) {
		if (client_req->tout_ms)
			wait = msecs_to_jiffies(client_req->tout_ms);
		if (wait_for_completion_timeout(&session->complete, wait) == 0)
			ret = -ETIME;
	}

	goog_mba_aggr_client_put_session(session);
	trace_goog_mba_aggr_send_message(ret);

	return ret;
}
EXPORT_SYMBOL_GPL(goog_mba_aggr_send_message);

static inline void
goog_mba_aggr_mark_mbox_chan_free(struct goog_mba_aggr_service *gservice,
				  struct mbox_client *client)
{
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;
	unsigned long flags;

	mbox_chan_info = container_of(client, struct goog_mba_aggr_mbox_chan_info, client);
	spin_lock_irqsave(&gservice->lock, flags);
	mbox_chan_info->req = NULL;
	spin_unlock_irqrestore(&gservice->lock, flags);
}

/*
 * goog_mba_aggr_client_tx_done - Callback function for client initiated transaction tx done.
 */
static void goog_mba_aggr_client_tx_done(struct mbox_client *client, void *msg, int r)
{
	struct goog_mba_aggr_service *gservice;
	struct goog_mba_aggr_request *req;
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;
	struct goog_mba_aggr_session *session;

	mbox_chan_info = container_of(client, struct goog_mba_aggr_mbox_chan_info, client);
	gservice = mbox_chan_info->gservice;
	req = mbox_chan_info->req;
	session = container_of(req, struct goog_mba_aggr_session, req);

	trace_goog_mba_aggr_client_tx_done(mbox_chan_info);

	if (req->oneway) {
		if (!req->async)
			complete(&session->complete);

		goog_mba_aggr_mark_mbox_chan_free(gservice, client);
		goog_mba_aggr_host_put_session(session);
		goog_mba_aggr_try_to_submit(gservice);
	}
}

/*
 * goog_mba_aggr_client_rx_cb - Callback function for host response to client initiated
 *				two-way request.
 */
static void goog_mba_aggr_client_rx_cb(struct mbox_client *client, void *msg)
{
	struct goog_mba_aggr_service *gservice;
	struct goog_mba_aggr_request *req;
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;
	struct goog_mba_aggr_session *session;

	mbox_chan_info = container_of(client, struct goog_mba_aggr_mbox_chan_info, client);
	gservice = mbox_chan_info->gservice;
	req = mbox_chan_info->req;
	session = container_of(req, struct goog_mba_aggr_session, req);

	trace_goog_mba_aggr_client_rx_cb(mbox_chan_info, msg);

	if (req->async) {
		req->async_resp_cb(msg, req->prv_data);
	} else {
		memcpy(req->resp_buf, msg, gservice->payload_size * sizeof(u32));
		complete(&session->complete);
	}
	goog_mba_aggr_mark_mbox_chan_free(gservice, client);
	goog_mba_aggr_host_put_session(session);
	goog_mba_aggr_try_to_submit(gservice);
}

/*
 * goog_mba_aggr_host_tx_cb - Callback function for host initiated messages.
 */
static void goog_mba_aggr_host_tx_cb(struct mbox_client *client, void *msg)
{
	struct goog_mba_aggr_service *gservice;
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;

	mbox_chan_info = container_of(client, struct goog_mba_aggr_mbox_chan_info, client);
	gservice = mbox_chan_info->gservice;

	trace_goog_mba_aggr_host_tx_cb(mbox_chan_info, msg, !!(gservice->host_tx_cb));

	if (gservice->host_tx_cb)
		gservice->host_tx_cb(msg, gservice->host_tx_cb_prv_data);
	else
		dev_warn(gservice->dev, "No host tx callback in service#%s", gservice->name);
}

static void goog_mba_aggr_host_rx_done(struct mbox_client *client, void *msg, int r)
{
	struct goog_mba_aggr_service *gservice;
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;

	mbox_chan_info = container_of(client, struct goog_mba_aggr_mbox_chan_info, client);
	gservice = mbox_chan_info->gservice;

	trace_goog_mba_aggr_host_rx_done(mbox_chan_info - gservice->host_tx_chans);
	goog_mba_aggr_mark_mbox_chan_free(gservice, client);
}

static int goog_mba_aggr_init_channel_dt(struct goog_mba_aggr_service *gservice,
					 enum goog_mba_aggr_chan_type channel_type)
{
	const char *mbox_idx_prop_name;
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_infos;
	int nr_mboxes;
	void (*rx_callback)(struct mbox_client *client, void *msg);
	void (*txdone_callback)(struct mbox_client *client, void *msg, int r);
	struct goog_mba_aggr_mbox_chan_info *mbox_chan_info;
	struct mbox_client *client;
	struct device *dev = gservice->dev;
	int ret;
	int i = 0;
	u32 idx;

	if (channel_type == CLIENT_TX_CHAN) {
		mbox_idx_prop_name = "req-mboxes-idx";
		rx_callback        = goog_mba_aggr_client_rx_cb;
		txdone_callback    = goog_mba_aggr_client_tx_done;
	} else {
		mbox_idx_prop_name = "resp-mboxes-idx";
		rx_callback        = goog_mba_aggr_host_tx_cb;
		txdone_callback    = goog_mba_aggr_host_rx_done;
	}

	nr_mboxes = of_property_count_u32_elems(dev->of_node, mbox_idx_prop_name);
	if (nr_mboxes < 0) {
		dev_err(dev, "Property %s not presented\n", mbox_idx_prop_name);
		return -ENOENT;
	}
	mbox_chan_infos = devm_kcalloc(dev, nr_mboxes, sizeof(*mbox_chan_infos), GFP_KERNEL);

	for (i = 0; i < nr_mboxes; i++) {
		mbox_chan_info = &mbox_chan_infos[i];
		mbox_chan_info->gservice = gservice;

		ret = of_property_read_u32_index(dev->of_node, mbox_idx_prop_name, i, &idx);
		if (ret != 0) {
			dev_err(dev, "Failed to read #%d in %s\n", i, mbox_idx_prop_name);
			return -ENOENT;
		}
		client = &mbox_chan_info->client;
		client->dev = dev;
		client->rx_callback = rx_callback;
		client->tx_done = txdone_callback;

		mbox_chan_info->chan = mbox_request_channel(client, idx);
		if (IS_ERR(mbox_chan_info->chan)) {
			dev_err(dev, "Failed to request mailbox channel in service#%s(index: %d, err %ld)\n",
				gservice->name, idx, PTR_ERR(mbox_chan_info->chan));
			return PTR_ERR(mbox_chan_info->chan);
		}
	}

	if (channel_type == CLIENT_TX_CHAN) {
		gservice->client_tx_chans = mbox_chan_infos;
		gservice->nr_client_tx_chans = nr_mboxes;
	} else {
		gservice->host_tx_chans = mbox_chan_infos;
		gservice->nr_host_tx_chans = nr_mboxes;
	}

	dev_dbg(dev, "[%s] setup #%d mboxes\n", mbox_idx_prop_name, nr_mboxes);

	return 0;
}

static int goog_mba_aggr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	int total_mboxes;
	struct goog_mba_aggr_service *gservice;
	const char *service_name;

	total_mboxes = of_count_phandle_with_args(dev->of_node, "mboxes", "#mbox-cells");
	if (total_mboxes < 0) {
		dev_err(dev, "Property mboxes or #gma_mbox-cells not found\n");
		return -ENOENT;
	}

	gservice = devm_kzalloc(dev, sizeof(*gservice), GFP_KERNEL);
	if (!gservice)
		return -ENOMEM;

	platform_set_drvdata(pdev, gservice);
	ret = of_property_read_string(dev->of_node, "service-name", &service_name);
	if (ret) {
		dev_err(dev, "Failed to read service-name(ret:%d)\n", ret);
		return ret;
	}
	strscpy(gservice->name, service_name, MAX_SERVICE_NAME_LEN);

	ret = of_property_read_u32(dev->of_node, "payload-size", &gservice->payload_size);
	if (ret < 0) {
		dev_err(dev, "Failed to read payload-size(ret:%d)\n", ret);
		return ret;
	}

	gservice->dev = dev;
	spin_lock_init(&gservice->lock);

	ret = goog_mba_aggr_init_channel_dt(gservice, CLIENT_TX_CHAN);
	if (ret)
		return ret;

	ret = goog_mba_aggr_init_channel_dt(gservice, HOST_TX_CHAN);
	if (ret)
		return ret;

	ret = goog_mba_aggr_create_session_pool(dev, gservice);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&gservice->entry);
	list_add(&gservice->entry, &goog_mba_aggr_gdata.head);
	goog_mba_aggr_gdata.nr_services++;

	dev_dbg(dev, "Service(%s) init done\n", gservice->name);

	return 0;
}

static void goog_mba_aggr_remove(struct platform_device *pdev)
{
	struct goog_mba_aggr_service *gservice;
	int i;

	gservice = platform_get_drvdata(pdev);
	for (i = 0; i < gservice->nr_client_tx_chans; i++)
		mbox_free_channel(gservice->client_tx_chans[i].chan);
	for (i = 0; i < gservice->nr_host_tx_chans; i++)
		mbox_free_channel(gservice->host_tx_chans[i].chan);
}

static const struct of_device_id goog_mba_aggr_of_match_table[] = {
	{ .compatible = "google,mba-aggr" },
	{},
};
MODULE_DEVICE_TABLE(of, goog_mba_aggr_of_match_table);

struct platform_driver goog_mba_aggr_driver = {
	.probe = goog_mba_aggr_probe,
	.remove = goog_mba_aggr_remove,
	.driver = {
		.name = "goog-mba-aggr",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_mba_aggr_of_match_table),
	},
};

module_platform_driver(goog_mba_aggr_driver);

MODULE_DESCRIPTION("Google MBA Aggregator");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
