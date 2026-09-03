// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_SERVICE_H
#define _GOOGLE_DPA_SERVICE_H

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/types.h>

#include "pw_rpc_c/pw_rpc_client.h"

#include "google_dpa_internal.h"
#include "google_dpa_log_proxy.h"

struct google_dpa_rpc_context {
	struct device *dev;
	/* RPC status */
	PwStatus status;
	/* The RPC completion signal */
	struct completion complete;
	PwRpcClient *client;
};

/* init_google_dpa_rpc_context - init the common RPC context structure.
 * @res: RPC context.
 */
static inline void init_google_dpa_rpc_context(struct google_dpa_rpc_context *ctx,
					       struct device *dev, PwRpcClient *client)
{
	ctx->dev = dev;
	ctx->client = client;
	ctx->status = kPwStatusUnknown;
	init_completion(&ctx->complete);
}

/* Wait for the completion event and abandon the RPC if we time out waiting */
int dpa_wait_for_rpc_completion_timeout(struct google_dpa_rpc_context *ctx, uint32_t call_id,
					unsigned int msec);

/* Wait for the completion event and abandon the RPC if we time out waiting */
int dpa_wait_for_rpc_completion(struct google_dpa_rpc_context *ctx, uint32_t call_id);

/* dpa_rpc_echo_service_sync - Synchgronously call the echo method of the echo service.
 * @dev: struct device handle
 * @client: RPC client.
 * @message: The message string to be sent.
 *
 * Return: Echoed string, or err-encoded ptr on failure. Caller is responsible for freeing string
 */
char *dpa_rpc_echo_service_sync(struct device *dev, PwRpcClient *client, const char *msg);

/* dpa_rpc_check_health - Health check of DPA by RPC call.
 * @dev: struct device handle
 * @client: RPC client.
 *
 * Return: Error code or 0 when DPA RPC is healthy
 */
int dpa_rpc_check_health(struct device *dev, PwRpcClient *client);

/* dpa_rpc_dma_service_read_word_sync - Synchronously call the read word method of the dma service.
 * @dev: struct device handle
 * @client: RPC client.
 * @dma_handle: The dma address that the core should access.
 * @val: Caller passed pointer to u32 to hold the result of the RPC call.
 *
 * Return: Error code or 0 on success.
 */
int dpa_rpc_dma_service_read_word_sync(struct device *dev, PwRpcClient *client, u32 dma_handle,
				       u32 *val);

int dpa_rpc_dma_service_copy_data_sync(struct device *dev, PwRpcClient *client, u32 dma_dst_addr,
				       u32 dma_src_addr, u32 count);

int dpa_rpc_dma_service_dma_copy_data_sync(struct device *dev, PwRpcClient *client,
					   u32 dma_dst_addr, u32 dma_src_addr, u32 count,
					   u32 dma_channel, u32 dma_burst_size,
					   u32 dma_burst_length);

/* dpa_rpc_shell_cmd_sync - Synchgronously call the shell cmd method of the shell service.
 * @dev: struct device handle
 * @client: RPC client.
 * @cmd: The command string to be sent.
 * @result: Integer result of the shell cmd
 *
 * Return: 0 on success, negative error code on kernel error, or one of following results:
 *   1 if the remote command returned a failure
 *   2 if the remote couldn't find the command
 */
int dpa_rpc_shell_cmd_sync(struct device *dev, PwRpcClient *client, const char *cmd);

/* dpa_rpc_ipsec_service_execute - Synchronously call send method of ipsec service.
 * @client: RPC client.
 * @data: The data bytes to send.
 * @data_len: The length of data bytes to send.
 * @offset: The offset of the data bytes.
 * @total_len: The total byte length of the bytes  be sent.
 *
 * Return: 0 on success, negative error code on kernel error or positive RPC result.
 */
int dpa_rpc_ipsec_service_execute(struct device *dev, PwRpcClient *client, const char *data,
				  size_t data_len, size_t offset, size_t total_len);

/* dpa_rpc_pwlog_service_listen - call listen method of the pwlog service.
 * @client: RPC client.
 * @info: Log source information.
 */
int dpa_rpc_pwlog_service_listen(PwRpcClient *client, struct google_dpa_log_info *info);

/* dpa_rpc_pwlog_service_listen_cancel - cancel listen method of the pwlog service.
 * @client: RPC client.
 * @call_id: The call id.
 */
int dpa_rpc_pwlog_service_listen_cancel(PwRpcClient *client, struct google_dpa_log_info *info);

/* dpa_rpc_tea_service_listen - call push_events method of the tea service.
 * @client: RPC client.
 * @dpa: Google DPA.
 */
int dpa_rpc_tea_service_push_events(PwRpcClient *client, const struct google_dpa *dpa);

/* dpa_rpc_power_service_poweroff - call the poweroff method of the power service
 * DPA will start its poweroff sequence after sending ACK to AP.
 * DPA could still crash during the power-off sequence in the worst case and
 * AP might receive a crash notification.
 * @dev: struct device handle
 * @client: RPC client.
 */
int dpa_rpc_power_service_poweroff(struct device *dev, PwRpcClient *client);
#endif /* _GOOGLE_DPA_SERVICE_H */
