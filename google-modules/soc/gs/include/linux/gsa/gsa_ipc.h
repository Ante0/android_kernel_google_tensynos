/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC
 */
#ifndef __LINUX_GSA_IPC_H
#define __LINUX_GSA_IPC_H

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/types.h>

struct gsa_ipc_chan;

/**
 * gsa_ipc_chan_create() - create GSA IPC channel
 * @ipc_dev: pointer to GSA IPC device this channel is associated with
 *
 * Return: pointer to @struct gsa_ipc_chan on success, or NULL otherwise
 */
struct gsa_ipc_chan *gsa_ipc_chan_create(struct device *ipc_dev);

/**
 * gsa_ipc_chan_connect() - connect to specified GSA service
 * @chan: pointer to @struct gsa_ipc_chan previously created by gsa_ipc_chan_create()
 * @srv_name: GSA IPC service (port name) to connect
 *
 * This routine establishes connection with specified GSA IPC service.
 * Only one GSA IPC service can be connected over particular IPC channel.
 *
 * Return: 0 on success or negative error otherwise
 */
int gsa_ipc_chan_connect(struct gsa_ipc_chan *chan, const char *srv_name);

/**
 * gsa_ipc_chan_close() - close connection over specified GSA IPC channel
 * @chan: pointer to @struct gsa_ipc_chan specifying GSA IPC channel to close
 *
 * This routine closes connection to GSA IPC service established over specified
 * GSA IPC channel. The &struct gsa_ipc_chan object can be reused to open new
 * connection if required.
 *
 * Return: 0 on success or negative error otherwise
 */
int gsa_ipc_chan_close(struct gsa_ipc_chan *chan);

/**
 * gsa_ipc_chan_destroy() - close and destroy GSA IPC channel
 * @chan: pointer to @struct gsa_ipc_chan specifying GSA IPC channel to destroy
 *
 * This routine closes connection to GSA IPC service (if any) and destroys
 * GSA IPC channel object.
 *
 * Return: 0 on success or negative error otherwise
 */
int gsa_ipc_chan_destroy(struct gsa_ipc_chan *chan);

/**
 * gsa_ipc_chan_send_msg() - Send message to specified IPC channel
 * @chan: pointer to @struct gsa_ipc_chan specifying GSA IPC channel
 * @msg: pointer to message to send
 * @msg_len: message size
 *
 * Return: Number of bytes sent on success or negative error otherwise
 */
int gsa_ipc_chan_send_msg(struct gsa_ipc_chan *chan, const void *msg, size_t msg_len);

/**
 * gsa_ipc_chan_recv_msg() - Receive message from specify IPC channel
 * @chan: pointer to @struct gsa_ipc_chan specifying GSA IPC channel
 * @msg_buf: pointer to buffer to place message to
 * @msg_buf_sz: size of the buffer specified by @msg_buf parameter
 *
 * Return: number of bytes received on success or negative error otherwise
 */
int gsa_ipc_chan_recv_msg(struct gsa_ipc_chan *chan, void *msg_buf, size_t msg_buf_sz);

/*
 * GSA IPC Secure HEAP integration interface
 */
struct scatterlist;

int gsa_transfer_memory(struct device *dev, u64 *id,
			struct scatterlist *sglist, unsigned int nents,
			pgprot_t pgprot, u64 tag, bool lend);

int gsa_reclaim_memory(struct device *dev, u64 id,
		       struct scatterlist *sglist, unsigned int nents);

struct dma_buf;
void gsa_register_func_for_dma_buf(
	u64 (*get_ffa_tag)(struct dma_buf *dma_buf),
	int (*get_shared_mem_id)(struct dma_buf *dma_buf, u64 *mem_id, u64 *poff));

#endif /* __LINUX_GSA_IPC_H */
