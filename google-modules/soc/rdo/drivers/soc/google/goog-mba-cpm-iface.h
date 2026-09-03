/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Google LLC
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/mailbox_client.h>

#ifndef _GOOG_MBA_CPM_IFACE_PRIV_H_
#define _GOOG_MBA_CPM_IFACE_PRIV_H_

#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/google_gtc.h>

/*
 * cpm_iface_cb_t - Callback invoked for CPM-initiated transactions
 * @context:			Context value provided by the CPM host.
 *				0 for oneway request, otherwise client need to respond. (see below)
 * @msg:			Pointer to the message payload received from the CPM host.
 * @priv_data:			Client's private data (registered via `cpm_iface_request_client`).
 *
 * Client call `cpm_iface_request_client` to register callback. Client will be notified with CPM
 * initiated request with corresponding @src_id. @context indicate whether clients need to
 * respond to CPM's request - 0 for oneway request from CPM, otherwise client need to respond with
 * @msg_type assign to RESPONSE_MSG.
 */
typedef void (*cpm_iface_cb_t)(u32 context, void *msg, void *priv_data);

/*
 * cpm_iface_client - Represents a registered CPM interface client
 * @dev:			The client's associated device.
 * @src_id:			The client's source ID within the CPM system.
 * @host_tx_cb:			Callback function invoked for CPM-initiated transactions.
 * @entry:			Internal field for managing the list of active clients.
 * @prv_data:			Client-specific data passed to the `host_tx_cb` callback.
 */
struct cpm_iface_client {
	struct device *dev;
	int src_id;
	cpm_iface_cb_t host_tx_cb;
	struct list_head entry;
	void *prv_data;
};

/*
 * struct cpm_iface_mbox_chan_info - Represents a mailbox channel used for CPM communication
 * @client:			Mailbox client configuration used for channel requests.
 * @chan:			Pointer to the active mailbox channel.
 * @req:			Pointer to the request associated with the channel (if applicable).
 */
struct cpm_iface_mbox_chan_info {
	struct mbox_client client;
	struct mbox_chan *chan;
	struct cpm_iface_req *req;
};

/*
 * struct cpm_iface_session - Manages the lifecycle of a client session.
 * @lock:			Spinlock to protect access to the session's data.
 * @status:			Indicates the completion status of the client and CPM request
 *				processing.
 * @complete:			Completion structure for synchronization between client and CPM
 *				threads.
 * @req:			The client's original request data.
 * @req_msg:			The client's original request payload.
 */
struct cpm_iface_session {
	spinlock_t lock;
	unsigned long status;
	struct completion complete;
	struct cpm_iface_req req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_event_timestamps timestamps;
};

inline void cpm_iface_record_timestamp(struct cpm_iface_event_timestamps *ts,
				       enum cpm_iface_events event)
{
	if (ts && event < MAX_CPM_IFACE_EVENTS)
		ts->events[event] = goog_gtc_get_time_ns();
}
#endif /* _GOOG_MBA_CPM_IFACE_PRIV_H_ */
