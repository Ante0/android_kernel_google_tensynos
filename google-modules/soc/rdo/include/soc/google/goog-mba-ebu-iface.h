/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOG_MBA_EBU_IFACE_H_
#define _GOOG_MBA_EBU_IFACE_H_

#include <linux/types.h>

#define GOOGLE_EBU_PAYLOAD_SIZE 3 /* Payload size in words */

struct ebu_iface;

/**
 * ebu_iface_payload - Payload type for request or response
 * @header:	Payload header
 * @data:	Payload data
 */
struct ebu_iface_payload {
	u32 header;
	u32 data[GOOGLE_EBU_PAYLOAD_SIZE];
} __packed;

/**
 * ebu_iface_message - Message type to communicate with EBU
 * @dst_service_id:	Destination service id
 * @request:		Pointer to the request message buffer to send
 * @response:		Pointer to the response message buffer to receive
 */
struct ebu_iface_message {
	int dst_service_id;
	struct ebu_iface_payload *request;
	struct ebu_iface_payload *response;
};

/*
 * ebu_iface_get - Get a ebu_iface handle for this device
 * @dev:			device that requests the EBU interface
 * Return:
 *	On success, a ebu interface handle.
 *	-ENODEV if dev doesn't have a device node entry, the device node doesn't have a
 *		"ebu-iface" property, or the property has an invalid phandle.
 *	-EPROBE_DEFER if the phandle is valid, but the ebu interface has not yet been probed.
 *
 * It is expected that this function is called once when a EBU client device is being probed
 * and the returned ebu_iface is stored in the client driver's context struct.  The EBU client
 * device driver should call ebu_iface_put() it its remove() callback.
 */
struct ebu_iface *ebu_iface_get(struct device *dev);

/*
 * ebu_iface_put - Put a ebu_iface handle for this device
 * @ebu_iface:			A EBU interface handle previously returned from ebu_iface_get().
 */
void ebu_iface_put(struct ebu_iface *dev);

/**
 * ebu_send_request - Send a request to DPA through EBU normal channels
 * @ebu_iface:	EBU interface handle
 * @message:	Request to send
 *
 * This function will block until a response arrives from the remote and
 * `message->response` will be filled with the response payload when it
 * arrives. It is caller's responsibility to ensure that `message->request` and
 * `message->response` are not invalidated until this function returns. Also,
 * callers should not touch the header part of `message->request` since it will
 * be handled by this function.
 *
 * This function should not be called in an atomic context.
 *
 * Return:
 *   0 on success, negative error code on failure.
 */
int ebu_send_request(struct ebu_iface *ebu_iface,
		     struct ebu_iface_message *message);

/*
 * ebu_ping - ping ebu service and get an incremental result
 * @ebu_iface:			A ebu interface handle.
 *
 * This function should not be called in an atomic context.
 *
 * Return:			0 for succeeded; Negative value for failure.
 */
int ebu_ping(struct ebu_iface *ebu_iface);

#endif /* _GOOG_MBA_EBU_IFACE_H_ */
