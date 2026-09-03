// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This Linux module implements Pigweed Remote Procedure Call (RPC)
 * functionality over NOA Inter-Process Communication (IPC). Pigweed
 * RPC provides a framework for handling RPC requests and responses,
 * while abstracting away the underlying transport mechanism. To
 * bridge this abstraction with NOA IPC, this module implements two
 * key components: a channel output handler and a channel input
 * receiver. The channel output handler takes encoded RPC data from
 * the Pigweed framework and transmits it over the NOA IPC channel.
 * The channel input receiver runs as a thread, continuously
 * receiving encoded RPC data from the NOA IPC channel. Upon
 * receiving data, it invokes the Pigweed RPC packet processing API,
 * which decodes the data and passes it to the appropriate RPC
 * handler. During module initialization, NOA IPC channel information
 * is retrieved from a shared memory region. This information is then
 * used by both the channel output handler and the channel input
 * receiver to establish communication over the NOA IPC transport.
 */

#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/types.h>

#include "google_dpa_boot.h"
#include "google_dpa_internal.h"
#include "google_dpa_rpc_internal.h"
#include "noa_ipc/ipc_manager.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

static struct IpcManagerData ipc_manager_data;

static PwRpcClient rpc_client_ncp;
static PwRpcClient rpc_client_nep;

/*
 * IPC Channel ID format is IPC_CHANNEL_<SOURCE>_<SINK>_<PURPOSE>.
 * 0 is kIpcInvalidChannelId.
 */
enum ipc_channel_id {
	IPC_CHANNEL_NCP_APC_RPC = 1,
	IPC_CHANNEL_APC_NCP_RPC,
	IPC_CHANNEL_NEP_APC_RPC,
	IPC_CHANNEL_APC_NEP_RPC,
	IPC_CHANNEL_NEP_NCP_RPC,
	IPC_CHANNEL_NCP_NEP_RPC,
};

/*
 * RPC Channel ID format is RPC_CHANNEL_<SERVER>_<CLIENT>_<PURPOSE>
 * 0 is Channel::kUnassignedChannelId.
 * 1 is default system channel ID.
 */
enum rpc_channel_id {
	RPC_CHANNEL_NCP_APC_DEFAULT = 2,
	RPC_CHANNEL_NEP_APC_DEFAULT,
	RPC_CHANNEL_NEP_NCP_DEFAULT,
};

static PwStatus rpc_channel_output_locked(PwRpcClient *client, u32 ipc_ch_id, const u8 *buf,
					  size_t len)
{
	if (!client)
		return kPwStatusInvalidArgument;

	if (!buf || !len)
		return kPwStatusOk;

	struct IpcManagerData *ipc_mgr_data = (struct IpcManagerData *)(client->transport_info);
	struct IpcChannelData *ipc_ch = IpcMgrGetChannel(ipc_mgr_data, ipc_ch_id);
	struct google_dpa *dpa = (struct google_dpa *)ipc_mgr_data->context;

	if (!ipc_ch) {
		dev_err(dpa->dev, "Channel %u is not found.", ipc_ch_id);
		return kPwStatusNotFound;
	}

	if (!IpcChIsReady(ipc_ch)) {
		dev_warn(dpa->dev, "Channel %u is not ready.", ipc_ch_id);
		return kPwStatusUnavailable;
	}

	if (IpcChWrite(ipc_ch, (const char *)buf, len) != 0) {
		dev_warn(dpa->dev, "Cannot write data to Channel %u.", ipc_ch_id);
		return kPwStatusAborted;
	}
	return kPwStatusOk;
}

static PwStatus rpc_channel_output(PwRpcClient *client, u32 ipc_ch_id, struct mutex *mutex,
				   const u8 *buf, size_t len)
{
	int ret = 0;

	ret = mutex_lock_interruptible(mutex);
	if (ret)
		return -ERESTARTSYS;

	ret = rpc_channel_output_locked(client, ipc_ch_id, buf, len);

	mutex_unlock(mutex);

	return ret;
}

/* Avoid writing data concurrently fo NCP */
DEFINE_MUTEX(ncp_rpc_channel_output_mutex);
static PwStatus ncp_rpc_channel_output(PwRpcClient *client, const u8 *buf, size_t len)
{
	return rpc_channel_output(client, IPC_CHANNEL_APC_NCP_RPC, &ncp_rpc_channel_output_mutex,
				  buf, len);
}

/* Avoid writing data concurrently fo NEP */
DEFINE_MUTEX(nep_rpc_channel_output_mutex);
static PwStatus nep_rpc_channel_output(PwRpcClient *client, const u8 *buf, size_t len)
{
	return rpc_channel_output(client, IPC_CHANNEL_APC_NEP_RPC, &nep_rpc_channel_output_mutex,
				  buf, len);
}

struct ipc_rx_task_info {
	const char *name;
	u8 channel_id;
	PwRpcClient *client;
	struct task_struct *task;
};

#define RX_BUFFER_SIZE (1024)
#define PB_DECODE_BUFFER_SIZE (768)
static int ipc_rx_task(void *data)
{
	struct ipc_rx_task_info *info = (struct ipc_rx_task_info *)data;
	PwRpcClient *client = info->client;
	struct IpcManagerData *ipc_mgr_data = (struct IpcManagerData *)(client->transport_info);
	struct google_dpa *dpa = (struct google_dpa *)ipc_mgr_data->context;
	struct IpcChannelData *ipc_ch = IpcMgrGetChannel(ipc_mgr_data, info->channel_id);

	if (!ipc_ch) {
		dev_err(dpa->dev, "IPC Channel %u does not exist", info->channel_id);
		return -EIO;
	}

	dev_dbg(dpa->dev, "IPC RX task (%s) is starting with channel %u", info->name,
		info->channel_id);

	/* Ensure shared information is set up by the sender. */
	while (!IpcChIsReady(ipc_ch)) {
		if (kthread_should_stop())
			return 0;

		cond_resched();
	};

	dev_dbg(dpa->dev, "IPC RX task is running (%s)", info->name);

	s32 ret = 0;
	u32 read_bytes = 0;
	u8 rx_buf[RX_BUFFER_SIZE];
	u8 decode_buf[PB_DECODE_BUFFER_SIZE];

	while (!kthread_should_stop()) {
		read_bytes = 0;
		/* IpcChRead is a blocking call. */
		ret = IpcChRead(ipc_ch, (char *)rx_buf, sizeof(rx_buf), &read_bytes);
		if (ret) {
			dev_err(dpa->dev, "%s: channel %u read error (%d)", info->name,
				info->channel_id, ret);
			break;
		}

		if (!read_bytes)
			continue;

		PwStatus status = PwRpcClientProcessPacket(info->client, rx_buf, read_bytes,
							   decode_buf, sizeof(decode_buf));
		if (status != kPwStatusOk) {
			dev_warn(dpa->dev, "%s: cannot process packet with error %d", info->name,
				 status);
		}
	};
	return ret;
}

static struct ipc_rx_task_info ncp_rx_task_info = {
	.name = "NCP IPC RX Task",
	.channel_id = IPC_CHANNEL_NCP_APC_RPC,
	.client = NULL,
	.task = NULL,
};

static int ncp_ipc_rx_task(void *data)
{
	return ipc_rx_task(data);
}

static PwStatus ncp_rpc_channel_input_setup(PwRpcClient *client)
{
	if (!client)
		return kPwStatusInvalidArgument;
	ncp_rx_task_info.client = client;
	ncp_rx_task_info.task =
		kthread_run(ncp_ipc_rx_task, &ncp_rx_task_info, "%s", ncp_rx_task_info.name);
	return kPwStatusOk;
}

static struct ipc_rx_task_info nep_rx_task_info = {
	.name = "NEP IPC RX Task",
	.channel_id = IPC_CHANNEL_NEP_APC_RPC,
	.client = NULL,
	.task = NULL,
};

static int nep_ipc_rx_task(void *data)
{
	return ipc_rx_task(data);
}

static PwStatus nep_rpc_channel_input_setup(PwRpcClient *client)
{
	if (!client)
		return kPwStatusInvalidArgument;
	nep_rx_task_info.client = client;
	nep_rx_task_info.task =
		kthread_run(nep_ipc_rx_task, &nep_rx_task_info, "%s", nep_rx_task_info.name);
	return kPwStatusOk;
}

int google_dpa_rpc_init(struct google_dpa *dpa)
{
	int ret = 0;

	bool is_iomem;
	struct google_dpa_shared_info __iomem *shared_info = google_dpa_get_shared_info(dpa);
	void __iomem *ipc_info_vaddr =
		google_dpa_da_to_va_internal(dpa, &dpa->ncp, readl(&shared_info->ipc_info_addr),
					     sizeof(u32), &is_iomem);

	memset(&ipc_manager_data, 0, sizeof(ipc_manager_data));
	ret = IpcMgrInit(&ipc_manager_data, kChannelEndpointApc, ipc_info_vaddr, dpa);
	if (ret) {
		dev_err(dpa->dev, "Cannot init IPC (ret=%d).", ret);
		return ret;
	}

	PwStatus status = kPwStatusOk;

	memset(&rpc_client_ncp, 0, sizeof(rpc_client_ncp));
	status = PwRpcClientInit(&rpc_client_ncp, RPC_CHANNEL_NCP_APC_DEFAULT,
				 ncp_rpc_channel_output, ncp_rpc_channel_input_setup,
				 &ipc_manager_data);
	if (status != kPwStatusOk) {
		dev_err(dpa->dev, "Cannot init NCP rpc client.");
		return -EIO;
	}

	memset(&rpc_client_nep, 0, sizeof(rpc_client_nep));
	status = PwRpcClientInit(&rpc_client_nep, RPC_CHANNEL_NEP_APC_DEFAULT,
				 nep_rpc_channel_output, nep_rpc_channel_input_setup,
				 &ipc_manager_data);
	if (status != kPwStatusOk) {
		dev_err(dpa->dev, "Cannot init NEP rpc client.");
		return -EIO;
	}

	dev_dbg(dpa->dev, "RPC clients are ready.");
	return ret;
}

int google_dpa_rpc_deinit(struct google_dpa *dpa)
{
	int ret = 0;

	kthread_stop(ncp_rx_task_info.task);
	kthread_stop(nep_rx_task_info.task);

	/* Deinit all clients regardless of failures. */
	PwRpcClientDeinit(&rpc_client_ncp);
	PwRpcClientDeinit(&rpc_client_nep);

	IpcMgrDeinit(&ipc_manager_data);

	return ret;
}

PwRpcClient *google_dpa_rpc_ncp_client(void)
{
	return &rpc_client_ncp;
}
EXPORT_SYMBOL_GPL(google_dpa_rpc_ncp_client);

PwRpcClient *google_dpa_rpc_nep_client(void)
{
	return &rpc_client_nep;
}
EXPORT_SYMBOL_GPL(google_dpa_rpc_nep_client);
