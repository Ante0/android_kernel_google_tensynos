// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google MailBox Array (MBA) CPM Interface
 *
 * Copyright (c) 2023-2024 Google LLC
 */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include <soc/google/goog_mba_q_xport.h>
#include <soc/google/goog_mba_cpm_iface.h>

#include "goog-mba-cpm-iface.h"

#define CREATE_TRACE_POINTS
#include "goog-mba-cpm-iface-trace.h"

#define MAX_REQ_Q_SIZE              32
#define ENQUEUE_REQ_WAIT_MS         10
#define MAX_ENQUEUE_REQ_TIMEOUT_MS  10000 // Max timeout 10s for waiting for space in the req queue

/*
 * MAX_CLIENT_SESSION - The maximum number of concurrent client sessions.
 *
 * This is calculated as the sum of:
 *   - The maximum request queue size (MAX_REQ_Q_SIZE)
 *   - Twice the maximum number of valid transaction tokens (GOOG_MBA_Q_XPORT_MAX_TOKEN_VAL)
 *
 * Rationale:
 *   - Each client session can be in one of three states:
 *     1. Processing a two-way CPM transaction (with a valid token)
 *     2. Processing a one-way CPM transaction (without a token)
 *     3. Waiting in the queue
 *   - For state 3, the session occupies space in the request queue.
 *   - For state 1, the session holds a valid token.
 *   - In typical usage, the number of one-way requests is lower than two-way requests,
 *     but for simplicity, we assume they are equal.
 *   - Therefore, the maximum number of sessions is:
 *       (Max queue size) + 2 * (Max number of tokens)
 */
#define MAX_CLIENT_SESSION (MAX_REQ_Q_SIZE + GOOG_MBA_Q_XPORT_MAX_TOKEN_VAL * 2)

/* Session status bits */
#define SESSION_CLIENT_DONE BIT(0) /* client finished its job and leave */
#define SESSION_CPM_IFACE_DONE BIT(1) /* CPM interface job done and would not access this session */

/* Combined session status */
#define SESSION_RESERVE_STATUS (0x0) /* session has been reserved */
#define SESSION_FREE_STATUS (SESSION_CLIENT_DONE | SESSION_CPM_IFACE_DONE) /* session is free */

/*
 * struct cpm_iface_q - Represents a circular queue for CPM interface data
 *
 * @capacity:			The maximum number of elements the queue can hold.
 * @wr_idx:			Index for the next element to be enqueued (written).
 * @rd_idx:			Index for the next element to be dequeued (read).
 * @size:			The current number of elements in the queue.
 *
 * Data Storage (Flexible Type):
 *   This queue uses a union to store different data types.  Ensure you
 *   use the correct member of the union based on the queue's purpose:
 *     * @mbox_chan_info:	For storing `struct cpm_iface_mbox_chan_info` elements.
 *     * @req:			For storing `struct cpm_iface_req` elements.
 */
struct cpm_iface_q {
	int capacity;
	int wr_idx;
	int rd_idx;
	int size;
	union {
		DECLARE_FLEX_ARRAY(struct cpm_iface_mbox_chan_info, mbox_chan_info);
		DECLARE_FLEX_ARRAY(struct cpm_iface_req *, req);
	};
};

/*
 * struct cpm_iface_data -	Core data for the CPM interface module
 * @dev:			The device structure associated with this CPM interface.
 * @client_list_head:		List of registered client handles.
 * @msg_buf_size:		Maximum size in words(32-bit) of the payload that can be serviced.
 * @session_pool:		Shared client session pool with size of MAX_CLIENT_SESSION.
 * @lock:			Spinlock for protecting critical sections.
 * @initialized:		Status shared with client to wait for driver ready.
 *
 * Client Request Management:
 * @seq_num:			Sequence number for debugging
 * @req_q:			Queue for managing client request buffers.
 * @resp_pending:		Array for requests awaiting responses from the CPM host.
 * @resp_pending_size:		Number of @resp_pending entries in use.
 *
 * Mailbox Channel Management:
 * @req_chans_q:		Queue for available request mailbox channels.
 * @resp_chans_q:		Queue for available response mailbox channels.
 */
struct cpm_iface_data {
	struct device *dev;
	struct list_head client_list_head;
	int msg_buf_size;
	bool initialized;
	struct cpm_iface_session *session_pool;

	/*
	 * protect queue operation including token, req_q, resp_pending and req_chans_q.
	 * and resp_chans_q.
	 */
	spinlock_t lock;
	int seq_num;
	struct cpm_iface_q *req_q;
	struct cpm_iface_req *resp_pending[GOOG_MBA_Q_XPORT_MAX_TOKEN_VAL];
	int resp_pending_size;
	struct cpm_iface_q *req_chans_q;
	struct cpm_iface_q *resp_chans_q;
};

struct cpm_iface_data *cpm_iface_data;

static inline int cpm_iface_get_session_idx(struct cpm_iface_session *session)
{
	return session - cpm_iface_data->session_pool;
}

static inline int cpm_iface_get_mbox_chan_info_idx(struct cpm_iface_mbox_chan_info *minfo)
{
	return minfo - cpm_iface_data->req_chans_q->mbox_chan_info;
}

static inline bool cpm_iface_is_request_msg(struct cpm_iface_req *req)
{
	return (req->msg_type == REQUEST_MSG);
}

static inline bool cpm_iface_is_oneway_msg(struct cpm_iface_req *req)
{
	return (req->msg_type == ONEWAY_MSG);
}

static inline bool cpm_iface_is_response_msg(struct cpm_iface_req *req)
{
	return (req->msg_type == RESPONSE_MSG);
}

static inline bool cpm_iface_is_request_header(void *msg)
{
	return (goog_mba_q_xport_get_type(msg) == GOOG_MBA_Q_XPORT_TYPE_REQUEST);
}

static inline bool cpm_iface_is_oneway_header(void *msg)
{
	return (goog_mba_q_xport_get_type(msg) == GOOG_MBA_Q_XPORT_TYPE_ONEWAY);
}

static inline bool cpm_iface_is_response_header(void *msg)
{
	return (goog_mba_q_xport_get_type(msg) == GOOG_MBA_Q_XPORT_TYPE_RESPONSE);
}

static inline bool cpm_iface_is_init_header(void *msg)
{
	return (*(u32 *)msg == GOOG_MBA_Q_XPORT_INIT_PROTO_VAL);
}

static inline bool cpm_iface_is_q_empty(struct cpm_iface_q *q)
{
	return (!q->size);
}

static inline bool cpm_iface_is_q_full(struct cpm_iface_q *q)
{
	return (q->size == q->capacity);
}

static struct cpm_iface_q *_cpm_iface_q_create(struct device *dev, size_t capacity,
					       size_t elem_size)
{
	struct cpm_iface_q *q;
	unsigned long q_size = sizeof(*q) + capacity * elem_size;

	q = devm_kzalloc(dev, q_size, GFP_KERNEL);
	if (!q)
		return ERR_PTR(-ENOMEM);

	q->capacity = capacity;

	return q;
}

/*
 * cpm_iface_pending_resp_available - if any available entries from @resp_pending
 *
 * Returns: true for available entries.
 * false for out of available entry.
 */
static inline bool cpm_iface_pending_resp_available(void)
{
	return cpm_iface_data->resp_pending_size < GOOG_MBA_Q_XPORT_MAX_TOKEN_VAL;
}

/*
 * cpm_iface_add_pending_resp - add request to pending table to wait for host response
 * @req:			Client's request wait for response.
 *
 * Returns: 0 if add success.
 * -EBUSY for no available token.
 */
static int cpm_iface_add_pending_resp(struct cpm_iface_req *req)
{
	struct device *dev = cpm_iface_data->dev;
	int token;

	for (token = 0; token < GOOG_MBA_Q_XPORT_MAX_TOKEN_VAL; token++) {
		if (!cpm_iface_data->resp_pending[token]) {
			goog_mba_q_xport_set_token(&req->req_msg->header, token);
			cpm_iface_data->resp_pending[token] = req;
			cpm_iface_data->resp_pending_size++;
			return 0;
		}
	}

	dev_err(dev, "Failed to find available entries in resp_pending (size:%d)\n",
		cpm_iface_data->resp_pending_size);

	return -EBUSY;
}

/*
 * cpm_iface_remove_pending_resp - remove request entry from pending table after received response
 * @msg_payload:		Used to resolve embedded token value to remove corresponding entry
 *				of pending table.
 */
static struct cpm_iface_req *cpm_iface_remove_pending_resp(struct cpm_iface_payload *msg_payload)
{
	u32 token = goog_mba_q_xport_get_token(&msg_payload->header);
	struct cpm_iface_req *req;

	if (WARN_ON_ONCE(token >= ARRAY_SIZE(cpm_iface_data->resp_pending)))
		return NULL;

	req = cpm_iface_data->resp_pending[token];
	if (WARN_ON_ONCE(!req))
		return NULL;

	cpm_iface_data->resp_pending[token] = NULL;
	cpm_iface_data->resp_pending_size--;

	return req;
}

/*
 * cpm_iface_set_seq_number - assign sequential number in valid range
 * @req_msg:			Request payload to be wrote sequence number.
 */
static inline void cpm_iface_set_seq_number(struct cpm_iface_payload *req_msg)
{
	int seq_num;

	seq_num = cpm_iface_data->seq_num;
	goog_mba_q_xport_set_seq(&req_msg->header, seq_num);
	if (seq_num == GOOG_MBA_Q_XPORT_MAX_SEQ_VAL)
		cpm_iface_data->seq_num = GOOG_MBA_Q_XPORT_MIN_SEQ_VAL;
	else
		cpm_iface_data->seq_num++;
}

/* APIs for circular queue of requests */
static struct cpm_iface_q *cpm_iface_create_req_q(struct device *dev, size_t capacity)
{
	return _cpm_iface_q_create(dev, capacity, sizeof(struct cpm_iface_req *));
}

/*
 * cpm_iface_create_session_pool - initialize shared session pool
 * @dev:			Driver device structure
 *
 * Returns: valid pointer to initialized cpm_iface_session array.
 * Errors if initialization failed.
 */
static struct cpm_iface_session *cpm_iface_create_session_pool(struct device *dev)
{
	struct cpm_iface_session *session_pool;
	int i;

	session_pool = devm_kcalloc(dev, MAX_CLIENT_SESSION, sizeof(*session_pool),
				    GFP_KERNEL);
	if (!session_pool)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < MAX_CLIENT_SESSION; i++) {
		session_pool[i].status = SESSION_FREE_STATUS;
		spin_lock_init(&session_pool[i].lock);
		init_completion(&session_pool[i].complete);
	}

	return session_pool;
}

/*
 * cpm_iface_get_session - search and get available session structure from the pool
 *
 * Returns: valid pointer for reserved cpm_iface_session.
 * Null if no available cpm_iface_session.
 */
static struct cpm_iface_session *cpm_iface_get_session(void)
{
	struct cpm_iface_session *session;
	int i;

	for (i = 0; i < MAX_CLIENT_SESSION; i++) {
		session = &cpm_iface_data->session_pool[i];

		scoped_guard(spinlock_irqsave_try, &session->lock) {
			if (session->status ==
			    (SESSION_CLIENT_DONE | SESSION_CPM_IFACE_DONE)) {
				session->status = SESSION_RESERVE_STATUS;
				reinit_completion(&session->complete);

				return session;
			}
		}
	}

	return NULL;
}

/*
 * cpm_iface_cpm_put_session - CPM release its reference to given session
 * @req:			Request contained by session.
 * @msg_payload:		Host returned payload.
 *				Valid value will be copied to client's response message payload.
 *				Null will skip this step.
 */
static void cpm_iface_cpm_put_session(struct cpm_iface_req *req,
				      struct cpm_iface_payload *msg_payload)
{
	struct cpm_iface_session *session =
		container_of(req, struct cpm_iface_session, req);

	guard(spinlock_irqsave)(&session->lock);
	if (!(session->status & SESSION_CLIENT_DONE) && msg_payload)
		memcpy(req->resp_msg, msg_payload, sizeof(*req->resp_msg));

	complete(&session->complete);
	session->status |= SESSION_CPM_IFACE_DONE;
}

/*
 * cpm_iface_client_put_session - client release its reference to given session
 * @session:			Client's session to be put.
 */
static void cpm_iface_client_put_session(struct cpm_iface_session *session)
{
	scoped_guard(spinlock_irqsave, &session->lock)
		session->status |= SESSION_CLIENT_DONE;
}

static struct cpm_iface_session *cpm_iface_req_enqueue(struct cpm_iface_req *req)
{
	struct cpm_iface_q *q = cpm_iface_data->req_q;
	struct device *dev = cpm_iface_data->dev;
	struct cpm_iface_session *session;

	guard(spinlock_irqsave)(&cpm_iface_data->lock);

	if (cpm_iface_is_q_full(q)) {
		dev_dbg(dev, "Try increasing MAX_REQ_Q_SIZE\n");
		return ERR_PTR(-EBUSY);
	}

	session = cpm_iface_get_session();
	if (!session) {
		dev_dbg(dev, "Failed to get session\n");
		return ERR_PTR(-EBUSY);
	}

	/*
	 * if client leave due to timeout, accessing to @q and @req potentially corrupt the memory,
	 * copy client's request and request payload to avoid this.
	 */
	session->req = *req;
	session->req_msg = *req->req_msg;
	memset(&session->timestamps, 0, sizeof(session->timestamps));
	session->req.req_msg = &session->req_msg;
	session->req.timestamps = &session->timestamps;

	trace_cpm_iface_req_enqueue(cpm_iface_get_session_idx(session), session,
				    q->wr_idx, q->size + 1);

	q->req[q->wr_idx] = &session->req;
	q->wr_idx = (q->wr_idx + 1) % q->capacity;
	q->size++;

	cpm_iface_record_timestamp(session->req.timestamps,
				   CPM_IFACE_REQ_ENQUEUE);
	return session;
}

static struct cpm_iface_req *cpm_iface_req_dequeue(struct cpm_iface_q *q)
{
	struct cpm_iface_req *req;

	if (cpm_iface_is_q_empty(q))
		return ERR_PTR(-ENOENT);

	req = q->req[q->rd_idx];
	q->rd_idx = (q->rd_idx + 1) % q->capacity;
	q->size--;

	return req;
}

/*
 * cpm_iface_create_mbox_chan_info_q - Creates a circular queue for mailbox channel information.
 * @capacity: The desired capacity of the queue.
 *
 * Returns: A pointer to the newly created `cpm_iface_q` structure, or an ERR_PTR()
 *          on failure.
 */
static struct cpm_iface_q *cpm_iface_create_mbox_chan_info_q(struct device *dev, size_t capacity)
{
	return _cpm_iface_q_create(dev, capacity, sizeof(struct cpm_iface_mbox_chan_info));
}

static void cpm_iface_reset_mbox_chan_info_idx(struct cpm_iface_q *q)
{
	q->rd_idx = 0;
	q->wr_idx = 0;
	q->size = 0;
}

/*
 * cpm_iface_get_mbox_chan_info - Acquires the next available mailbox channel from the queue.
 * @q:				Pointer to the `cpm_iface_q` containing the mailbox channels.
 *
 * Returns: A pointer to the `cpm_iface_mbox_chan_info` structure for the available
 *          channel, or an ERR_PTR() on failure.
 *
 * Clients should use this function to obtain a mailbox channel before sending messages.
 */
static struct cpm_iface_mbox_chan_info *cpm_iface_get_mbox_chan_info(struct cpm_iface_q *q)
{
	struct cpm_iface_mbox_chan_info *mbox_chan = NULL;

	if (cpm_iface_is_q_full(q))
		return NULL;

	mbox_chan = &q->mbox_chan_info[q->wr_idx];

	q->wr_idx = (q->wr_idx + 1) % q->capacity;
	q->size++;

	return mbox_chan;
}

/*
 * cpm_iface_free_mbox_chan -	Releases a previously acquired mailbox channel.
 * @q:				Pointer to the `cpm_iface_q` containing the mailbox channels.
 *
 * Returns: 0 for success, negative value for failure.
 *
 * Clients should call this function after completing message transactions on a channel.
 */
static int cpm_iface_free_mbox_chan(struct cpm_iface_q *q)
{
	if (cpm_iface_is_q_empty(q))
		return -ENOENT;

	if (q->rd_idx < 0)
		return q->rd_idx;

	q->rd_idx = (q->rd_idx + 1) % q->capacity;
	q->size--;

	return 0;
}

/*
 * cpm_iface_mbox_chan_get_front -	Gets a pointer to the next mailbox channel scheduled
 *					to be released.
 * @q:					Pointer to the `cpm_iface_q` containing the mailbox
 *					channels.
 *
 * Returns: A pointer to the next `cpm_iface_mbox_chan_info` to be released, or
 *          an ERR_PTR(-ENOENT) if there are no channels pending release.
 *
 * This function can be used to identify the mailbox channel that will be made available
 * after the next call to `cpm_iface_free_mbox_chan`.
 */
static struct cpm_iface_mbox_chan_info *cpm_iface_mbox_chan_get_front(struct cpm_iface_q *q)
{
	if (cpm_iface_is_q_empty(q))
		return ERR_PTR(-ENOENT);

	return &q->mbox_chan_info[q->rd_idx];
}

static struct cpm_iface_client *cpm_iface_search_client_by_id(int id)
{
	struct cpm_iface_client *client;

	if (id < 0 || id > GOOG_MBA_Q_XPORT_MAX_SRC_ID_VAL)
		return ERR_PTR(-EINVAL);

	list_for_each_entry(client, &cpm_iface_data->client_list_head, entry) {
		if (client->src_id == id)
			return client;
	}

	return NULL;
}

static void cpm_iface_kfree_client(void *res)
{
	struct cpm_iface_client *client = res;

	if (WARN_ONCE(!list_empty(&client->entry), "dev %s\n",
		      dev_name(client->dev)))
		cpm_iface_free_client(client);
	kfree(client);
}

struct cpm_iface_client *cpm_iface_request_client(struct device *dev,
						  int src_id,
						  cpm_iface_cb_t cpm_iface_cb,
						  void *prv_data)
{
	struct cpm_iface_client *client;
	struct platform_device *pdev;
	struct device_node *node = NULL;
	struct of_phandle_iterator mboxes_iter;
	int ret;

	if (!dev || !dev->of_node) {
		pr_err("%s: No owner device node\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	if (!cpm_iface_data || !cpm_iface_data->initialized)
		return ERR_PTR(-EPROBE_DEFER);

	if (src_id) {
		scoped_guard(spinlock_irqsave, &cpm_iface_data->lock)
			client = cpm_iface_search_client_by_id(src_id);
		if (IS_ERR(client)) {
			dev_err(dev, "src_id(%d) out of valid range\n", src_id);
			return client;
		}

		if (client)
			return ERR_PTR(-EBUSY);
	}

	of_for_each_phandle(&mboxes_iter, ret, dev->of_node, "mboxes", "mbox-cells", 0) {
		if (cpm_iface_data->dev->of_node == mboxes_iter.node) {
			node = mboxes_iter.node;
			break;
		}
	}

	if (!node) {
		if (ret)
			dev_err(dev, "Err:%d when iterating 'mboxes'\n", ret);

		dev_err(dev, "'mboxes' doesn't contain CPM interface(%s)\n",
			cpm_iface_data->dev->of_node->name);
		return ERR_PTR(-EINVAL);
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		ret = -ENODEV;
		goto exit_put_node;
	}

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		ret = -ENOMEM;
		goto exit_put_pdev;
	}

	ret = devm_add_action(dev, cpm_iface_kfree_client, client);
	if (ret < 0)
		goto exit_free_client;

	client->dev = dev;
	client->src_id = src_id;
	client->host_tx_cb = cpm_iface_cb;
	client->prv_data = prv_data;

	scoped_guard(spinlock_irqsave, &cpm_iface_data->lock)
		list_add(&client->entry, &cpm_iface_data->client_list_head);

	of_node_put(node);

	trace_cpm_iface_request_client(client);

	return client;

exit_free_client:
	kfree(client);
exit_put_pdev:
	platform_device_put(pdev);
exit_put_node:
	of_node_put(node);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(cpm_iface_request_client);

void cpm_iface_free_client(struct cpm_iface_client *client)
{
	scoped_guard(spinlock_irqsave, &cpm_iface_data->lock) {
		if (!WARN_ON_ONCE(list_empty(&client->entry)))
			list_del_init(&client->entry);
	}

	trace_cpm_iface_free_client(client);
}
EXPORT_SYMBOL_GPL(cpm_iface_free_client);

/*
 * cpm_iface_try_to_submit() - Attempts to submit the next queued request to the mailbox system.
 *
 * This function dequeues the next pending request and attempts to submit it
 * to the Linux mailbox framework.
 *
 * Returns:
 *   * 0: Request successfully submitted.
 *   * < 0: Error originating from the Linux mailbox framework.
 *          (See mailbox documentation for specific error codes.)
 */
static int cpm_iface_try_to_submit(void)
{
	struct device *dev = cpm_iface_data->dev;
	struct cpm_iface_mbox_chan_info *mbox_chan_info;
	struct cpm_iface_req *req;
	struct cpm_iface_payload *req_msg;
	struct cpm_iface_session *session;
	int ret;

	guard(spinlock_irqsave)(&cpm_iface_data->lock);

	/*
	 * We need to satify all conditions to send a message.
	 *  - Request in queue
	 *  - Available mbox_chan_info
	 *  - Available entries for @resp_pending
	 */
	if (cpm_iface_is_q_empty(cpm_iface_data->req_q) ||
	    cpm_iface_is_q_full(cpm_iface_data->req_chans_q) ||
	    !cpm_iface_pending_resp_available())
		return 0;

	req = cpm_iface_req_dequeue(cpm_iface_data->req_q);
	session = container_of(req, struct cpm_iface_session, req);

	guard(spinlock)(&session->lock);

	req_msg = req->req_msg;
	if (!cpm_iface_is_init_header(req_msg)) {
		if (cpm_iface_is_request_header(req_msg)) {
			ret = cpm_iface_add_pending_resp(req);
			/*
			 * `cpm_iface_try_to_submit` doesn't expect to fail here since it checked
			 * availability above in critical section. If that happened, call BUG() to
			 * stop the system.
			 */
			if (ret < 0)
				BUG();
		}
		cpm_iface_set_seq_number(req_msg);
	}

	mbox_chan_info = cpm_iface_get_mbox_chan_info(cpm_iface_data->req_chans_q);
	mbox_chan_info->req = req;

	ret = mbox_send_message(mbox_chan_info->chan, mbox_chan_info->req->req_msg);
	if (ret < 0) {
		dev_err(dev, "mbox_send_message failed to send (ret:%d)\n", ret);
		dev_err(dev, "header: %#34x, payload: [%#34x, %#34x, %#34x]\n",
			req_msg->header, req_msg->payload[0],
			req_msg->payload[1], req_msg->payload[2]);
		dev_err(dev, "[mbox_chan] msg_count:%u, msg_free:%u\n",
			mbox_chan_info->chan->msg_count,
			mbox_chan_info->chan->msg_free);
		return ret;
	}

	cpm_iface_record_timestamp(req->timestamps, CPM_IFACE_SUBMIT_MSG);
	trace_cpm_iface_submit_msg(cpm_iface_get_session_idx(session),
				      cpm_iface_get_mbox_chan_info_idx(mbox_chan_info),
				      req);

	return 0;
}

static u32 cpm_iface_req_get_header(struct cpm_iface_client *client,
				    struct cpm_iface_req *cpm_iface_req)
{
	u32 header = 0;

	if (cpm_iface_is_oneway_msg(cpm_iface_req)) {
		header = goog_mba_q_xport_create_hdr(client->src_id, cpm_iface_req->dst_id,
						     0, 0, GOOG_MBA_Q_XPORT_TYPE_ONEWAY);
	} else if (cpm_iface_is_request_msg(cpm_iface_req)) {
		header = goog_mba_q_xport_create_hdr(client->src_id, cpm_iface_req->dst_id,
						     0, 0, GOOG_MBA_Q_XPORT_TYPE_REQUEST);
	} else if (cpm_iface_is_response_msg(cpm_iface_req)) {
		goog_mba_q_xport_set_type(&cpm_iface_req->resp_context,
					  GOOG_MBA_Q_XPORT_TYPE_RESPONSE);
		header = cpm_iface_req->resp_context;
	}

	return header;
}

static int __cpm_send_message(struct cpm_iface_req *cpm_iface_req, u32 header)
{
	unsigned long wait = MAX_SCHEDULE_TIMEOUT;
	struct cpm_iface_session *session;
	int ret;
	/* Set a max timeout value, if client request does not specify a timeout */
	unsigned long req_tout_ms = cpm_iface_req->tout_ms ?
				    cpm_iface_req->tout_ms : MAX_ENQUEUE_REQ_TIMEOUT_MS;
	unsigned long timeout = jiffies + msecs_to_jiffies(req_tout_ms);

	cpm_iface_req->req_msg->header = header;

	while (IS_ERR(session = cpm_iface_req_enqueue(cpm_iface_req))) {
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

	ret = cpm_iface_try_to_submit();
	if (ret < 0)
		goto exit;

	if (cpm_iface_req->tout_ms)
		wait = msecs_to_jiffies(cpm_iface_req->tout_ms);

	if (wait_for_completion_timeout(&session->complete, wait) == 0)
		ret = -ETIME;

	cpm_iface_record_timestamp(session->req.timestamps, CPM_IFACE_SEND_MESSAGE);
	if (cpm_iface_req->timestamps)
		*cpm_iface_req->timestamps = *session->req.timestamps;

	trace_cpm_send_message(cpm_iface_get_session_idx(session), ret);

exit:
	cpm_iface_client_put_session(session);

	return ret;
}

static bool cpm_iface_is_valid_req(struct cpm_iface_req *cpm_iface_req)
{
	struct device *dev = cpm_iface_data->dev;

	if (!cpm_iface_req) {
		dev_err(dev, "cpm_iface_req is NULL\n");
		return false;
	}

	if (!cpm_iface_req->req_msg) {
		dev_err(dev, "cpm_iface_req->reg_msg is NULL\n");
		return false;
	}

	if (cpm_iface_req->msg_type > RESPONSE_MSG || cpm_iface_req->msg_type < ONEWAY_MSG) {
		dev_err(dev, "Invalid msg_type: %d\n", cpm_iface_req->msg_type);
		return false;
	}

	if (cpm_iface_is_request_msg(cpm_iface_req) && !cpm_iface_req->resp_msg) {
		dev_err(dev, "cpm_iface_req->resp_msg is NULL when msg_type is REQUEST_MSG\n");
		return false;
	}

	if (cpm_iface_is_response_msg(cpm_iface_req)) {
		if (!goog_mba_q_xport_get_valid(&cpm_iface_req->resp_context)) {
			dev_err(dev, "Invalid valid bit in resp_context: %#x\n",
				cpm_iface_req->resp_context);
			return false;
		}

		if (!cpm_iface_is_response_header(&cpm_iface_req->resp_context)) {
			dev_err(dev, "Invalid response type in resp_context: %#x\n",
				cpm_iface_req->resp_context);
			return false;
		}
	}

	return true;
}

int cpm_send_message(struct cpm_iface_client *client, struct cpm_iface_req *cpm_iface_req)
{
	u32 header;

	if (!client || !cpm_iface_is_valid_req(cpm_iface_req))
		return -EINVAL;

	header = cpm_iface_req_get_header(client, cpm_iface_req);
	if (!header)
		return -EINVAL;

	return __cpm_send_message(cpm_iface_req, header);
}
EXPORT_SYMBOL_GPL(cpm_send_message);

static int
__cpm_iface_req_tx_done(struct cpm_iface_mbox_chan_info *mbox_chan_info,
			struct cpm_iface_req *req)
{
	struct device *dev = cpm_iface_data->dev;
	struct cpm_iface_session *session;

	lockdep_assert_held(&cpm_iface_data->lock);

	if (mbox_chan_info != cpm_iface_mbox_chan_get_front(cpm_iface_data->req_chans_q)) {
		struct cpm_iface_q *q = cpm_iface_data->req_chans_q;
		int idx = mbox_chan_info - q->mbox_chan_info;

		dev_err(dev, "Received tx_done in-between req_chans_q (idx: %d)\n", idx);
		dev_err(dev, "[req_chans_q] capacity:%d wr_idx:%d rd_idx:%d size:%d\n",
			q->capacity, q->wr_idx, q->rd_idx, q->size);
		return -EINVAL;
	}

	session = container_of(req, struct cpm_iface_session, req);
	trace_cpm_iface_req_tx_done(cpm_iface_get_session_idx(session),
				    cpm_iface_get_mbox_chan_info_idx(mbox_chan_info));

	if (cpm_iface_is_oneway_msg(req) || cpm_iface_is_response_msg(req)) {
		cpm_iface_record_timestamp(req->timestamps, CPM_IFACE_TX_DONE);
		cpm_iface_cpm_put_session(req, NULL);
	}

	cpm_iface_free_mbox_chan(cpm_iface_data->req_chans_q);

	return 0;
}

/*
 * cpm_iface_req_tx_done - callback function from request channels
 */
static void cpm_iface_req_tx_done(struct mbox_client *client, void *msg, int r)
{
	struct cpm_iface_mbox_chan_info *mbox_chan_info =
		container_of(client, struct cpm_iface_mbox_chan_info, client);
	struct cpm_iface_req *req = mbox_chan_info->req;

	scoped_guard(spinlock_irqsave, &cpm_iface_data->lock)
		if (__cpm_iface_req_tx_done(mbox_chan_info, req) < 0)
			return;

	cpm_iface_try_to_submit();
}

static inline u32 cpm_iface_prepare_resp_context(u32 header)
{
	u32 src_id = goog_mba_q_xport_get_dst_id(&header);
	u32 dst_id = goog_mba_q_xport_get_src_id(&header);
	u32 token = goog_mba_q_xport_get_token(&header);

	return goog_mba_q_xport_create_hdr(src_id, dst_id, 0, token, RESPONSE_MSG);
}

static void cpm_iface_handle_host_tx(void *msg)
{
	struct device *dev = cpm_iface_data->dev;
	struct cpm_iface_client *client;
	int dst_id = goog_mba_q_xport_get_dst_id(msg);
	struct cpm_iface_payload *msg_payload = msg;
	u32 resp_context = 0;

	scoped_guard(spinlock_irqsave, &cpm_iface_data->lock)
		client = cpm_iface_search_client_by_id(dst_id);

	if (IS_ERR_OR_NULL(client) || !client->host_tx_cb) {
		dev_err(dev, "No available client callback (id:%d).\n Payload Info:\n", dst_id);
		print_hex_dump(KERN_ERR, dev_name(dev), DUMP_PREFIX_NONE, 16, 4,
				msg_payload, sizeof(*msg_payload), false);
		return;
	}

	if (cpm_iface_is_request_header(msg))
		resp_context = cpm_iface_prepare_resp_context(msg_payload->header);

	client->host_tx_cb(resp_context, msg_payload, client->prv_data);

	trace_cpm_iface_handle_host_tx(dst_id, resp_context, msg_payload);
}

/*
 * cpm_iface_resp_cb -	Handles incoming messages on response channels
 * @mbox_client:	Pointer to the mailbox client structure.
 * @msg:		Pointer to the received message payload.
 *
 * This callback is invoked when a message arrives on a response channel. It handles
 * the following scenarios:
 *
 * 1. CPM Response to Client Request:	Processes a response from the CPM to a
 *					previously issued client request.
 * 2. CPM-Initiated Request:		Processes a new request initiated by the CPM
 *					and dispatches it to the appropriate client handler.
 * 3. Invalid Message Type:		Handles unexpected message types.
 */
static void cpm_iface_resp_cb(struct mbox_client *mbox_client, void *msg)
{
	struct device *dev = cpm_iface_data->dev;

	if (*(u32 *)msg == GOOG_MBA_Q_XPORT_INIT_PROTO_VAL) {
		dev_err(dev, "Reset message received from the remote\n");
		/* TODO: call cpm_iface_init_q_mode() in thread context */
		return;
	}

	if (cpm_iface_is_response_header(msg)) {
		struct cpm_iface_payload *msg_payload = msg;
		struct cpm_iface_req *req;
		struct cpm_iface_session *session;

		scoped_guard(spinlock_irqsave, &cpm_iface_data->lock) {
			req = cpm_iface_remove_pending_resp(msg_payload);
			if (!req) {
				dev_err(dev,
					"No token:%u found for response payload: %#10x %#10x %#10x %#10x\n",
					goog_mba_q_xport_get_token(
						&msg_payload->header),
					msg_payload->header,
					msg_payload->payload[0],
					msg_payload->payload[1],
					msg_payload->payload[2]);
				return;
			}

			session = container_of(req, struct cpm_iface_session,
					       req);

			cpm_iface_record_timestamp(req->timestamps,
						   CPM_IFACE_RESP_CB);
			trace_cpm_iface_resp_cb(cpm_iface_get_session_idx(session),
						msg_payload);
			cpm_iface_cpm_put_session(req, msg_payload);
		}

		cpm_iface_try_to_submit();
	} else if (cpm_iface_is_request_header(msg) || cpm_iface_is_oneway_header(msg)) {
		cpm_iface_handle_host_tx(msg);
	} else {
		dev_err(dev, "Unknown message type: %u\n", goog_mba_q_xport_get_type(msg));
	}
}

#define for_each_mbox_chan_in_q(chan, q) \
	for (chan = (q)->mbox_chan_info; chan < &(q)->mbox_chan_info[(q)->capacity]; chan++)

static struct cpm_iface_q
*cpm_iface_init_channel_dt(struct device *dev,
			   const char *mbox_idx_prop_name,
			   void (*txdone_callback)(struct mbox_client *client, void *msg, int r),
			   void (*rx_callback)(struct mbox_client *client, void *msg))
{
	int mbox_q_capacity;
	struct cpm_iface_q *q;
	struct mbox_client *client;
	struct cpm_iface_mbox_chan_info *mbox_chan_info;
	int ret;
	int i;
	u32 idx;

	mbox_q_capacity = of_property_count_u32_elems(dev->of_node, mbox_idx_prop_name);
	if (mbox_q_capacity < 0) {
		dev_err(dev, "Property %s not presented\n", mbox_idx_prop_name);
		return ERR_PTR(-ENOENT);
	}

	q = cpm_iface_create_mbox_chan_info_q(dev, mbox_q_capacity);
	if (IS_ERR(q))
		return q;

	i = 0;
	for_each_mbox_chan_in_q(mbox_chan_info, q) {
		ret = of_property_read_u32_index(dev->of_node, mbox_idx_prop_name, i, &idx);
		if (ret != 0) {
			dev_err(dev, "Failed to read #%d in %s\n", i, mbox_idx_prop_name);
			return ERR_PTR(-ENOENT);
		}
		client = &mbox_chan_info->client;
		client->dev = dev;
		client->rx_callback = rx_callback;
		client->tx_done = txdone_callback;

		mbox_chan_info->chan = mbox_request_channel(client, idx);
		if (IS_ERR(mbox_chan_info->chan)) {
			ret = PTR_ERR(mbox_chan_info->chan);
			dev_err(dev, "Failed to request mailbox channel(index: %d, err %d)\n",
				idx, ret);
			return ERR_PTR(ret);
		}
		i++;
	}
	dev_dbg(dev, "[%s] setup #%d mboxes\n", mbox_idx_prop_name, mbox_q_capacity);

	return q;
}

static int cpm_iface_init_q_mode_proto(void)
{
	struct cpm_iface_payload msg_payload = {0};
	struct cpm_iface_req req = {
		.msg_type = ONEWAY_MSG,
		.req_msg = &msg_payload,
	};

	return __cpm_send_message(&req, GOOG_MBA_Q_XPORT_INIT_PROTO_VAL);
}

static int cpm_iface_init_q_mode(struct cpm_iface_data *ci_data)
{
	int ret;

	ret = cpm_iface_init_q_mode_proto();
	if (ret < 0)
		return ret;

	/*
	 * After CPM acknowledges (tx done) in the initialization protocol, the next channel index
	 * resets to zero.
	 */
	cpm_iface_reset_mbox_chan_info_idx(ci_data->req_chans_q);
	cpm_iface_reset_mbox_chan_info_idx(ci_data->resp_chans_q);
	ci_data->seq_num = GOOG_MBA_Q_XPORT_MIN_SEQ_VAL;

	return 0;
}

static const char * const cpm_iface_default_events[] = {
	"cpm_iface_request_client",
	"cpm_iface_free_client",
	"cpm_iface_req_enqueue",
	"cpm_iface_submit_msg",
	"cpm_iface_req_tx_done",
	"cpm_iface_resp_cb",
	"cpm_send_message",
	"cpm_iface_handle_host_tx",
	"goog_mba_ctrl_process_q_rx",
	"goog_mba_ctrl_process_q_txdone",
	"goog_mba_ctrl_send_data_q",
};

static void cpm_iface_create_ftrace_instance(struct cpm_iface_data *ci_data)
{
	struct device *dev = ci_data->dev;
	struct trace_array *ta;
	int i;
	int ret;

	ta = trace_array_get_by_name("goog_cpm_mailbox", "goog_mba_ctrl,goog_mba_cpm_iface");
	if (!ta) {
		dev_err(dev, "Failed to create trace array\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(cpm_iface_default_events); i++) {
		ret = trace_array_set_clr_event(ta, NULL, cpm_iface_default_events[i], true);
		if (ret < 0)
			dev_warn(dev, "Event:%s not present\n", cpm_iface_default_events[i]);
	}

	trace_array_put(ta);
}

static int cpm_iface_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	u32 msg_buf_size;
	struct cpm_iface_data *ci_data;

	if (of_count_phandle_with_args(dev->of_node, "mboxes", "#mbox-cells") < 0) {
		dev_err(dev, "Property mboxes or #mbox-cells not found\n");
		return -ENOENT;
	}

	ret = of_property_read_u32(dev->of_node, "payload-size", &msg_buf_size);
	if (ret < 0) {
		dev_err(dev, "Failed to read msg-buf-size(ret:%d)\n", ret);
		return ret;
	}

	ci_data = devm_kzalloc(dev, sizeof(*cpm_iface_data), GFP_KERNEL);
	if (!ci_data)
		return -ENOMEM;

	ci_data->dev = dev;
	ci_data->msg_buf_size = msg_buf_size;

	ci_data->req_q = cpm_iface_create_req_q(dev, MAX_REQ_Q_SIZE);
	if (IS_ERR(ci_data->req_q))
		return -ENOMEM;

	spin_lock_init(&ci_data->lock);
	INIT_LIST_HEAD(&ci_data->client_list_head);

	ci_data->session_pool = cpm_iface_create_session_pool(dev);
	if (IS_ERR(ci_data->session_pool))
		return PTR_ERR(ci_data->session_pool);

	ci_data->req_chans_q = cpm_iface_init_channel_dt(dev, "req-mboxes-idx",
							 cpm_iface_req_tx_done, NULL);
	if (IS_ERR(ci_data->req_chans_q))
		return PTR_ERR(ci_data->req_chans_q);

	ci_data->resp_chans_q = cpm_iface_init_channel_dt(dev, "resp-mboxes-idx",
							  NULL, cpm_iface_resp_cb);
	if (IS_ERR(ci_data->resp_chans_q))
		return PTR_ERR(ci_data->resp_chans_q);

	cpm_iface_create_ftrace_instance(ci_data);

	cpm_iface_data = ci_data;

	ret = cpm_iface_init_q_mode(ci_data);
	if (ret < 0) {
		cpm_iface_data = NULL;
		return ret;
	}

	cpm_iface_data->initialized = true;

	dev_dbg(dev, "%s init done\n", __func__);

	return 0;
}

static void cpm_iface_remove(struct platform_device *pdev)
{
	struct cpm_iface_mbox_chan_info *minfo;

	for_each_mbox_chan_in_q(minfo, cpm_iface_data->req_chans_q)
		mbox_free_channel(minfo->chan);

	for_each_mbox_chan_in_q(minfo, cpm_iface_data->resp_chans_q)
		mbox_free_channel(minfo->chan);
}

static const struct of_device_id cpm_iface_of_match_table[] = {
	{ .compatible = "google,mba-cpm-iface" },
	{},
};
MODULE_DEVICE_TABLE(of, cpm_iface_of_match_table);

struct platform_driver cpm_iface_driver = {
	.probe = cpm_iface_probe,
	.remove = cpm_iface_remove,
	.driver = {
		.name = "goog-mba-cpm-iface",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cpm_iface_of_match_table),
	},
};

module_platform_driver(cpm_iface_driver);

MODULE_DESCRIPTION("Google MBA CPM interface");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
