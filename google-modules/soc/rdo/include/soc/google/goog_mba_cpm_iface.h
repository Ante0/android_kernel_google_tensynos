/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Google LLC
 */

#ifndef _GOOG_MBA_CPM_IFACE_H_
#define _GOOG_MBA_CPM_IFACE_H_

#include <linux/types.h>

struct device;
struct cpm_iface_client;

/*
 * enum cpm_iface_msg_type - Specifies the type of message for CPM interface transactions
 *  `ONEWAY_MSG`:		A one-way message that does not expect a response. The @resp_msg
 *				field in `cpm_iface_req` is ignored.
 *  `REQUEST_MSG`:		A request message that expects a response.
 *  `RESPONSE_MSG`:		A response message to a previous request.
 */
enum cpm_iface_msg_type {
	ONEWAY_MSG,
	REQUEST_MSG,
	RESPONSE_MSG,
};

#define GOOG_MBA_PAYLOAD_SIZE 3

/*
 * struct cpm_iface_payload - Represents a payload for CPM messages
 * @header:			A header containing message metadata (e.g., type, destination).
 *				Client shouldn't fill the header.
 * @payload:			An array of 32-bit words representing the actual payload data.
 */
struct cpm_iface_payload {
	u32 header;
	u32 payload[GOOG_MBA_PAYLOAD_SIZE];
};

enum cpm_iface_events {
	CPM_IFACE_REQ_ENQUEUE,	/* event when client's request be put into software queue */
	CPM_IFACE_SUBMIT_MSG,	/* event when client's request be sent to MBA hardware queue slot */
	/*
	 * CPM_IFACE_TX_DONE: event when client get tx done notification.
	 *                    Only be captured for oneway message.
	 * CPM_IFACE_RESP_CB: event when CPM interface processing response message from CPM.
	 *                    Only be capture for two-way messages.
	 *
	 * Notice: these event definitions share the same enum values. For some cases, MSG and ACK
	 *         interrupts are pending at the same time before AP to handle them. The order to
	 *         handle MSG or ACK interrupt can't be guaranteed so we skip recording to event of
	 *         CPM_IFACE_TX_DONE when transaction message is two-way message to make timestamp
	 *         monotonic.
	 */
	CPM_IFACE_TX_DONE,
	CPM_IFACE_RESP_CB = CPM_IFACE_TX_DONE,
	CPM_IFACE_SEND_MESSAGE,	/* event when client finish message sending */
	MAX_CPM_IFACE_EVENTS,
};

struct cpm_iface_event_timestamps {
	ktime_t events[MAX_CPM_IFACE_EVENTS];
};

/*
 * struct cpm_iface_req		Represents a request within the CPM interface
 * @msg_type:			Specifies the message type (see `enum cpm_iface_msg_type`).
 * @req_msg:			The request message payload.
 * @resp_msg:			Buffer to receive a response.
 *				(Effective when @msg_type is REQUEST_MSG)
 * @timestamps:			Structure to record transaction timestamps. Client give valid
 *				pointer to get result of timestamps; Leave it NULL if don't need.
 * @dst_id:			Destination service ID within the CPM host.
 * @tout_ms:			Request timeout in milliseconds (0 indicates no timeout).
 * @resp_context:		Context for responding to CPM-initiated messages (used when
 *				@msg_type is RESPONSE_MSG).
 */
struct cpm_iface_req {
	enum cpm_iface_msg_type msg_type;
	struct cpm_iface_payload *req_msg;
	struct cpm_iface_payload *resp_msg;
	struct cpm_iface_event_timestamps *timestamps;
	int dst_id;
	unsigned long tout_ms;
	u32 resp_context;
};

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
 * cpm_iface_request_client - Registers a client with the CPM interface
 * @dev:			Client's device structure.
 * @src_id:			Client's service ID for identification by CPM (optional).
 *				Register client's service ID (1 to GOOG_MBA_Q_XPORT_MAX_SRC_ID_VAL)
 *				with callback in @cpm_iface_cb to handle CPM initiated request.
 *				Otherwise, specify to 0.
 * @cpm_iface_cb:		Callback function invoked for CPM-initiated requests (optional).
 * @prv_data:			Private data passed to the callback function (optional).
 *
 * Return:			A `cpm_iface_client` handle on success, or an ERR_PTR() on failure.
 *				-ENODEV: invalid device structure (see details in kernel log).
 *				-EBUSY: client has registered with the same @src_id.
 *				-EINVAL: client bind to wrong phandle that CPM interface don't
 *				support.
 *				-ENOMEM: memory allocation failed.
 *
 *
 * This function initializes a CPM interface handle for the client, allowing the client to interact
 * with the CPM. The handle encapsulates client-specific information for use by the CPM interface.
 *
 * Requirements:
 *  * The client's device tree node needs a 'mboxes' property pointing to the phandle of the
 *    'CPM mailbox interface module'.
 *  * Call `cpm_iface_free_client` when the handle is no longer needed.
 */
struct cpm_iface_client *cpm_iface_request_client(struct device *dev,
						  int src_id,
						  cpm_iface_cb_t cpm_iface_cb,
						  void *prv_data);

/*
 * cpm_iface_free_client - Free resources and refcount from CPM interface device.
 * @client:			Handle of `cpm_iface_client` used for CPM message operations.
 */
void cpm_iface_free_client(struct cpm_iface_client *client);

/*
 * cpm_send_message - send a payload to CPM host
 * @client:			Client handle obtained from `cpm_iface_request_client`.
 * @cpm_iface_req:		Request structure containing message parameters.
 *
 * Return:			0 on succeeded; Negative error code on failure.
 *
 * This function enables clients to transmit messages to the CPM host. It handles
 * the following scenarios:
 *
 * * **One-way Messages:**        Returns after Tx done or timeout.
 * * **Request Messages:**        Returns after receiving a response or timeout.
 * * **CPM-Initiated Responses:** Transmits a response using the provided `resp_context`.
 *
 * Important Notes:
 * * The first word of the request payload is used by the transport layer.
 * * For request messages, the `resp_msg` field will hold the CPM host's response.
 * * Do not call this function from atomic contexts due to potential blocking.
 */
int cpm_send_message(struct cpm_iface_client *client, struct cpm_iface_req *cpm_iface_req);

#endif /* _GOOG_MBA_CPM_IFACE_H_ */
