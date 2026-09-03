/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Google LLC
 */

#ifndef _GOOG_MBA_AGGR_PRIV_H_
#define _GOOG_MBA_AGGR_PRIV_H_

/*
 * struct goog_mba_aggr_mbox_chan_info - Information of mbox_chan.
 *					One goog_mba_aggr_mbox_chan_info per mbox_chan.
 * @gservice:			Pointer to gservice which requested this channel.
 * @client:			Instance of mbox_client contained configs to request mbox_chan.
 * @chan:			Pointer to mbox_chan.
 * @req:			Sender data collection; Also represent for @mbox_chan_info is busy.
 */
struct goog_mba_aggr_mbox_chan_info {
	struct goog_mba_aggr_service *gservice;
	struct mbox_client client;
	struct mbox_chan *chan;
	struct goog_mba_aggr_request *req;
};

/* Session status bits */
#define SESSION_CLIENT_DONE BIT(0) /* client finished its job and leave */
#define SESSION_HOST_DONE BIT(1) /* host job done and would not access this session */

/* Combined session status */
#define SESSION_RESERVE_STATUS (0x0) /* session has been reserved */
#define SESSION_FREE_STATUS (SESSION_CLIENT_DONE | SESSION_HOST_DONE) /* session is free */

/*
 * struct goog_mba_aggr_session - Manages the lifecycle of a client session.
 * @lock:			Spinlock to protect access to the session's data.
 * @status:			Indicates the completion status of the client and host request
 *				processing.
 * @complete:			Completion structure for synchronization between client and host
 *				threads.
 * @req:			The client's original request data.
 * @req_msg:			The client's original request payload.
 */
struct goog_mba_aggr_session {
	/*
	 * protect @goog_mba_aggr_session operation of @status for searching and acquiring.
	 */
	spinlock_t lock;
	unsigned long status;
	struct completion complete;
	struct goog_mba_aggr_request req;
	u32 *req_msg;
};

#define MAX_SERVICE_NAME_LEN 32
#define MAX_REQ_QUEUE_LENGTH 32
/*
 * struct goog_mba_aggr_service - Service to communicate group of mbox_chan.
 * @dev:			Device backing this service.
 * @name:			Name of this service.
 * @entry:			List entry to chain available services.
 * @req_queue:			Circular buffer with size MAX_REQ_QUEUE_LENGTH.
 * @req_queue_head:		Next enqueue index of @req_queue.
 * @req_queue_tail:		Next dequeue index of @req_queue.
 * @req_queue_size:		The size of @req_queue.
 * @payload_size:		Maximum size in words(32-bit) of the payload that can be serviced.
 * @lock:			Spinlock for critical section operation of service.
 * @nr_client_tx_chans:		Number of channel infos used for client initiated transaction.
 * @client_tx_chans:		Array pointer of client initiated transaction channel infos.
 * @nr_host_tx_chans:		Number of channel infos used for host initiated transaction.
 * @host_tx_chans:		Array pointer of host initiated transaction channel infos.
 * @host_tx_cb:			Client hooked callback for host-initiated transaction
 * @host_tx_cb_prv_data:	Client private data pointer. Will be passed in when calling
 *				@host_tx_cb.
 */
struct goog_mba_aggr_service {
	struct device *dev;
	char name[MAX_SERVICE_NAME_LEN];
	struct list_head entry;
	struct goog_mba_aggr_request *req_queue[MAX_REQ_QUEUE_LENGTH];
	struct goog_mba_aggr_session *session_pool;
	int nr_sessions;
	int req_queue_head;
	int req_queue_tail;
	int req_queue_size;
	int payload_size;
	/*
	 * protect queue operation including req_queue, req_queue_head, req_queue_tail and
	 * req_queue_size.
	 */
	spinlock_t lock;

	int nr_client_tx_chans;
	struct goog_mba_aggr_mbox_chan_info *client_tx_chans;
	int nr_host_tx_chans;
	struct goog_mba_aggr_mbox_chan_info *host_tx_chans;
	goog_mbox_aggr_resp_cb_t host_tx_cb;
	void *host_tx_cb_prv_data;
};

enum goog_mba_aggr_chan_type {
	CLIENT_TX_CHAN,
	HOST_TX_CHAN,
};

struct goog_mba_aggr_data {
	struct list_head head;
	int nr_services;
};

static inline struct goog_mba_aggr_mbox_chan_info *
	goog_mba_aggr_to_mbox_chan_info(struct mbox_client *client)
{
	return container_of(client, struct goog_mba_aggr_mbox_chan_info, client);
}

static inline int
	goog_mba_aggr_index_mbox_chan_info(struct goog_mba_aggr_mbox_chan_info *mbox_chan_info)
{
	struct goog_mba_aggr_service *gservice = mbox_chan_info->gservice;
	s64 index;

	index = mbox_chan_info - gservice->client_tx_chans;
	if (index < gservice->nr_client_tx_chans && index >= 0)
		return index;

	index = mbox_chan_info - gservice->host_tx_chans;
	if (index < gservice->nr_host_tx_chans && index >= 0)
		return index;

	return -EINVAL;
}

static inline int goog_mba_aggr_get_session_idx(struct goog_mba_aggr_service *gservice,
						struct goog_mba_aggr_session *session)
{
	return session - gservice->session_pool;
}

#endif /* _GOOG_MBA_AGGR_PRIV_H_ */
