/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google MailBox Array (MBA) Doorbell Interface
 *
 * Copyright (c) 2025 Google LLC
 */

#ifndef _GOOG_MBA_DOORBELL_IFACE_H_
#define _GOOG_MBA_DOORBELL_IFACE_H_

/*
 * mbox_doorbell_grp - structure to manage a doorbell group.
 *
 * Doorbell group management provide APIs to allocate, ring doorbell and instrument base on Linux
 * common mailbox framework system.
 * The low level mailbox controller needs to support features to integrate with APIs of doorbell
 * group:
 *  1. Enumerate doorbell bits to mbox channels respectively.
 *     (e.g. 32 doorbell bits map to 32 mbox channels; The mbox channel whose index is zero map to
 *      first doorbell bit)
 *  1. IRQ status delegation: when receiving doorbell interrupt with doorbell bits, low-level
 *     mailbox call rx_callback with reference to variable of IRQ status. Client of rx_callback
 *     can set bit to zero to clear IRQ status bits.
 */
struct mbox_doorbell_grp;

/*
 * mbox_request_doorbell_grp - request all channels of mailbox specifier in 'mboxes' property.
 * @dev:                client's device structure.
 * @client:             identity of the client requesting the channel.
 *
 * This API is used for doorbell grouping. It will help to request all specified channels in
 * 'mboxes' property and instrument when notified from the host.
 * When host ringing the client in multiple channels, the instrument function forward
 * the notification for all valid channels at one time and clear pending notification.
 * (Note this feature requires mailbox controller to support clearing pending notifications of
 * channels)
 *
 * Return: pointer of structure of mbox_doorbell_grp if successful.
 *         ERR_PTR for request failure.
 */
struct mbox_doorbell_grp *mbox_request_doorbell_grp(struct device *dev, struct mbox_client *client);

/*
 * mbox_ring_doorbell_grp - ring the doorbell in a doorbell group
 * @db_grp:		structure of @mbox_doorbell_grp.
 * @mboxes_idx:		Index of mboxes to send doorbell notification.
 *
 * Return: 0 or greater values for success, otherwise negative values.
 */
int mbox_ring_doorbell_grp(struct mbox_doorbell_grp *db_grp, int mboxes_idx);

/*
 * mbox_ring_doorbell - ring the doorbell by given channel structure
 * @chan:		mbox_chan structure for ringing host's doorbell
 *
 * Return: 0 or greater values for success, otherwise negative values.
 */
int mbox_ring_doorbell(struct mbox_chan *chan);

#endif /* _GOOG_MBA_DOORBELL_IFACE_H_ */
