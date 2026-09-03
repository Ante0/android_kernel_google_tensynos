/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Google LLC
 */

#ifndef _GOOG_MBA_AGGR_H_
#define _GOOG_MBA_AGGR_H_

typedef void (*goog_mbox_aggr_resp_cb_t)(void *resp_msg, void *priv_data);
struct goog_mba_aggr_service;

/*
 * goog_mba_aggr_request - Struct used to save parameters from client's request to send data
 * @req_buf:			Client specific message typecasted.
 * @resp_buf:			Pointer to place response message.
 * @oneway:			Specify the message sending is oneway.
 *				Host will not trigger RX callback.
 * @async:			Requests that the message is sent without blocking.
 * @async_resp_cb:		Atomic callback to call when the data is received.
 * @tout_ms:			Time to timeout in milliseconds.
 * @prv_data:			Client's data pointer to be passed in when calling async_resp_cb.
 */
struct goog_mba_aggr_request {
	void *req_buf;
	void *resp_buf;
	int oneway;
	bool async;
	goog_mbox_aggr_resp_cb_t async_resp_cb;
	unsigned long tout_ms;
	void *prv_data;
};

/*
 * goog_mba_request_gservice - Request a service for group of channels.
 * @dev:			Device backing to the request driver.
 * @index:			Index of service specifier in 'goog-mba-aggrs' property.
 * @host_tx_cb:			Callback from response channels.
 * @prv_data:			Client's data pointer to be passed in when calling @host_tx_cb.
 *
 * The Client specifies its required service node in dts file. It can't be
 * called from atomic context. Client need to pass pointer of service to
 * specify which service it want to communicate.
 *
 * Return: Pointer to the service assigned to the client if successful.
 *		ERR_PTR for request failure.
 */
struct goog_mba_aggr_service *goog_mba_request_gservice(struct device *dev, int index,
							goog_mbox_aggr_resp_cb_t host_tx_cb,
							void *prv_data);

/*
 * goog_mba_aggr_send_message - For client to submit a message to be
 *					sent to the remote.
 * @gservice:			Service assigned to this client.
 * @client_req:			Parameters of client's request to send data.
 *
 * For client to submit data to the service destined for a remote processor.
 *
 * Return: Non-negative integer for successful submission.
 *	Negative value denotes failure.
 */
int goog_mba_aggr_send_message(struct goog_mba_aggr_service *gservice,
			       struct goog_mba_aggr_request *client_req);
#endif
