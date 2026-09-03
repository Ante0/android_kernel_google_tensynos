// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA WLAN RPC
 *
 * Copyright (c) 2025 Google LLC.
 */

#include "noa_wlan_rpc.h"
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa_rpc.h>
#include "wlan_cmd_rpc_client.h"
#include "wlan_event_rpc_client.h"

static int noa_wlan_event_rpc_cb(const u32 event, const void *msg, size_t size)
{
	return noa_wlan_fw_event_recv(event, (void *)msg);
}

/**
 * __noa_wlan_fw_request_send_rpc - Sends a command to the WLAN firmware via RPC
 * @cmd: The command ID to send
 * @msg: A pointer to the message data to send
 * @len: The length of the message data
 *
 * This function sends a command to the WLAN firmware using the RPC mechanism.
 * It is only executed when 'CONFIG_NOA_FULLSOC_SUPPORT' is set.
 *
 * Return: The result of sending the command via RPC.
 *         Returns 0 on success, or a negative error code on failure.
 */
int __noa_wlan_fw_request_send_rpc(int cmd, void *msg, size_t len)
{
	return wlan_cmd_rpc_client_send_command(cmd, msg, len, NULL, NULL);
}
EXPORT_SYMBOL_GPL(__noa_wlan_fw_request_send_rpc);

/**
 * noa_wlan_rpc_init - Initializes the WLAN RPC service
 *
 * This function initializes the WLAN command and event RPC clients.
 * It is only executed when 'CONFIG_NOA_FULLSOC_SUPPORT' is set.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_wlan_rpc_init(void)
{
	int ret = 0;

	/* Initialize wlan cmd rpc */
	ret = wlan_cmd_rpc_client_init(google_dpa_rpc_ncp_client());
	if (ret)
		return -EINVAL;

	/* Initialize wlan event rpc */
	ret = wlan_event_rpc_client_init(google_dpa_rpc_ncp_client());
	if (ret)
		goto err_cmd;

	/* Register wlan event rpc callback */
	wlan_event_rpc_client_register_event_callback(noa_wlan_event_rpc_cb);

	/* Activate wlan event rpc */
	ret = wlan_event_rpc_client_open();
	if (ret)
		goto err_event;

	return 0;

err_event:
	wlan_event_rpc_client_deinit();

err_cmd:
	wlan_cmd_rpc_client_deinit();

	return ret;
}

/**
 * noa_wlan_rpc_deinit - Deinitializes the WLAN RPC service
 *
 * This function deinitializes the WLAN command and event RPC clients.
 * It is only executed when 'CONFIG_NOA_FULLSOC_SUPPORT' is set.
 */
void noa_wlan_rpc_deinit(void)
{
	wlan_event_rpc_client_close();

	wlan_event_rpc_client_deinit();

	wlan_cmd_rpc_client_deinit();

	return;
}

#else

int __noa_wlan_fw_request_send_rpc(int cmd, void *msg, size_t len)
{
	/* Not supported */
	return 0;
}

int noa_wlan_rpc_init(void)
{
	/* Not supported */
	return 0;
}

void noa_wlan_rpc_deinit(void)
{
	/* Not supported */
	return;
}

#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
