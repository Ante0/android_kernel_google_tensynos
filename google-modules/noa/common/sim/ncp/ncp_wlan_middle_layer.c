// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WLAN Middle Layer
 *
 * Copyright (c) 2024 Google LLC.
 *
 * This file provides the implementation for the NCP WLAN Middle Layer,
 * which facilitates communication between the NCP and the NCP WLAN firmware.
 * It handles initialization, deinitialization, and communication with the NCP
 * WLAN firmware (WLAN service).
 *
 * Author: Wade Shih <wadeshih@google.com>
 */

#include <linux/export.h>
#include <linux/irq.h>

#include "ncp_wlan_middle_layer.h"
#if !IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)
#include <common/wlan/noa_wlan.h>
#include "wlan_build/core/wlan_svc/wlan_service.h"
#include "wlan_fw/core/wlan_svc/wlan_service.h"
#include "wlan_fw/wlan_service_rpc_protocol.h"
#include "nep_helpers.h"
#endif // !IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)

#if !IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)
/**
 * @brief Callback function type for sending events.
 *
 * @param[in] event The event to send.
 * @param[in] msg A pointer to the message data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
typedef int (*send_event_callback_fn)(int, void *);

/**
 * @brief Callback function type for handling commands.
 *
 * @param[in] cmd The event to send.
 * @param[in] msg A pointer to the message data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
typedef int (*wlan_command_handler_callback_fn)(int, void *);

/**
 * @brief  NCP WLAN Middle Layer data structure.
 *
 * This structure holds the wlan service instance and function pointers for the
 * NCP WLAN Middle Layer.
 */
typedef struct ncp_wlan_middle_layer {
	WlanService wlan_svc;
	send_event_callback_fn send_event_to_apc;
	wlan_command_handler_callback_fn wlan_command_handler;
	struct device *client_dev;
} ncp_wlan_middle_layer_t;

/**
 * @brief Sends a service request to the NEP.
 *
 * @param[in] event The event to send.
 * @param[in] active Whether the service is being activated or deactivated.
 * @param[in] id The id of buffer_pool or the id of ring.
 * @param[in] direction The NEP direction of rings is only used on the NEP_CMD_RING_ACTIVE event.
 */
extern void nep_ring_service_request_send(int event, bool active, uint8_t id, uint8_t direction);

/**
 * @brief Instance of the NCP WLAN Middle Layer data structure.
 */
static ncp_wlan_middle_layer_t ncp_wlan_ml;

/**
 * @brief Sends an event to the APC.
 *
 * @param[in] event The event to send.
 * @param[in] msg A pointer to the message data.
 * @param[in] msg_len The length of the message data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
static int ncp_wlan_ml_send_event_to_apc(uint32_t event, void *msg, const uint32_t msg_len)
{
	if (ncp_wlan_ml.send_event_to_apc) {
		return ncp_wlan_ml.send_event_to_apc(event, msg);
	}
	return -ESRCH;
}

/**
 * @brief Activates the NEP ring.
 *
 * @return 0 on success.
 */
static int ncp_wlan_ml_nep_ring_activate(void)
{
	// TODO(b/380424306) - The old API registers both input and output rings
	// into the ring service simultaneously, while the new API registers only
	// one ring at a time. Therefore, aligning the interface with the new API
	// is recommended.
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowDeviceToHost,
							   kNoaWlanRingRxData),
				      kNoaRingNepInput);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowHostToDevice,
							   kNoaWlanRingTxData),
				      kNoaRingNepOutput);
	return 0;
}

/**
 * @brief Deactivates the NEP input ring.
 *
 * @return 0 on success.
 */
static int ncp_wlan_ml_nep_input_ring_deactivate(void)
{
	// TODO(b/380424306) - The old API registers both input and output rings
	// into the ring service simultaneously, while the new API registers only
	// one ring at a time. Therefore, aligning the interface with the new API
	// is recommended.
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowDeviceToHost,
							   kNoaWlanRingRxData),
				      kNoaRingNepInput);
	return 0;
}

/**
 * @brief Deactivates the NEP output ring.
 *
 * @return 0 on success.
 */
static int ncp_wlan_ml_nep_output_ring_deactivate(void)
{
	// TODO(b/380424306) - The old API registers both input and output rings
	// into the ring service simultaneously, while the new API registers only
	// one ring at a time. Therefore, aligning the interface with the new API
	// is recommended.
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowHostToDevice,
							   kNoaWlanRingTxData),
				      kNoaRingNepOutput);
	return 0;
}

/**
 * @brief Activates the NEP ring buffer pool.
 *
 * @return 0 on success.
 */
static int ncp_wlan_ml_nep_ring_buffer_pool_activate(void)
{
	nep_ring_service_request_send(NEP_CMD_RING_BUFFER_POOL_ACTIVE, true, NOA_PORT_WLAN_FW,
				      NEP_RING_BUFFER_POOL_DEFAULT_TYPE);
	return 0;
}

/**
 * @brief Deactivates the NEP ring buffer pool.
 *
 * @return 0 on success.
 */
static int ncp_wlan_ml_nep_ring_buffer_pool_deactivate(void)
{
	nep_ring_service_request_send(NEP_CMD_RING_BUFFER_POOL_ACTIVE, false, NOA_PORT_WLAN_FW,
				      NEP_RING_BUFFER_POOL_DEFAULT_TYPE);
	return 0;
}

/**
 * @brief Sends a command to the NEP.
 *
 * @param[in] cmd The command to send.
 * @param[in] msg A pointer to the message data.
 * @param[in] msg_len The length of the message data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
static int ncp_wlan_ml_send_command_to_net_engine(int cmd, void *msg, size_t msg_len)
{
	return nep_request_send(cmd, msg);
}

static int ncp_wlan_ml_noa_power_vote(bool active)
{
	return 0;
}

/**
 * @brief Handles WLAN service commands.
 *
 * @param[in] cmd The command to handle.
 * @param[in] msg A pointer to the message data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
static int ncp_wlan_ml_wlan_svc_command(int cmd, void *msg)
{
	pr_debug("%s(): cmd: %d\n", __func__, cmd);

	if (cmd == kWlanCmdRegReceiver) {
		ncp_wlan_ml.send_event_to_apc = (send_event_callback_fn)(*(void **)msg);
		return 0;
	} else if (cmd == kWlanCmdRegClientDev) {
		ncp_wlan_ml.client_dev = (struct device *)(*(void **)msg);
		return 0;
	}

	if (WlanServiceCommand(&ncp_wlan_ml.wlan_svc, cmd, msg, 0, NULL) != 0) {
		pr_err("%s(): run cmd %d failed.", __func__, cmd);
	}

	// Always return 0 to prevent wlan interface startup failure.
	return 0;
}

/**
 * @brief Initializes the NCP WLAN Middle Layer platform.
 *
 * This function initializes the NCP WLAN Middle Layer, including
 * setting up the WLAN service and registering necessary callbacks.
 *
 * @return 0 on success, a negative error code otherwise.
 */
int ncp_wlan_ml_platform_init(void)
{
	WlanServicePlatformInitParams wlan_svc_plat_init_params;

	memset(&ncp_wlan_ml, 0, sizeof(ncp_wlan_ml));

	ncp_wlan_ml.wlan_command_handler = ncp_wlan_ml_wlan_svc_command;

	memset(&wlan_svc_plat_init_params, 0, sizeof(wlan_svc_plat_init_params));
	wlan_svc_plat_init_params.send_event_to_apc = ncp_wlan_ml_send_event_to_apc;
	wlan_svc_plat_init_params.nep_ring_activate = ncp_wlan_ml_nep_ring_activate;
	wlan_svc_plat_init_params.nep_input_ring_deactivate = ncp_wlan_ml_nep_input_ring_deactivate;
	wlan_svc_plat_init_params.nep_output_ring_deactivate =
		ncp_wlan_ml_nep_output_ring_deactivate;
	wlan_svc_plat_init_params.nep_ring_buffer_pool_activate =
		ncp_wlan_ml_nep_ring_buffer_pool_activate;
	wlan_svc_plat_init_params.nep_ring_buffer_pool_deactivate =
		ncp_wlan_ml_nep_ring_buffer_pool_deactivate;
	wlan_svc_plat_init_params.send_command_to_net_engine =
		ncp_wlan_ml_send_command_to_net_engine;
	wlan_svc_plat_init_params.noa_power_vote = ncp_wlan_ml_noa_power_vote;

	return WlanServicePlatInit(&ncp_wlan_ml.wlan_svc, &wlan_svc_plat_init_params);
}

/**
 * @brief Deinitializes the NCP WLAN Middle Layer platform.
 *
 * This function deinitializes the NCP WLAN Middle Layer,
 * releasing any allocated resources.
 */
void ncp_wlan_ml_platform_deinit(void)
{
	memset(&ncp_wlan_ml, 0, sizeof(ncp_wlan_ml));
}

/**
 * @brief Sends a request to the NOA WLAN firmware through directly
 *	  invoking simulator functions.
 *
 * This function sends a command to the WLAN firmware through the
 * registered command handler.
 *
 * @param[in] cmd The command to send.
 * @param[in] msg A pointer to the message data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
int __noa_wlan_fw_request_send_sim(int cmd, void *msg)
{
	if (!ncp_wlan_ml.wlan_command_handler) {
		pr_err("%s(): wlan_command_handler is not set.\n", __func__);
		return -ENODEV;
	}
	return ncp_wlan_ml.wlan_command_handler(cmd, msg);
}
EXPORT_SYMBOL_GPL(__noa_wlan_fw_request_send_sim);

void ncp_wlan_ml_shell_cmd_handler(const char *cmd, int argv_len, const char *argv)
{
	WlanCmdShellRequest req;

	if (!cmd) {
		return;
	}

	memset(&req, 0, sizeof(req));
	strncpy(req.cmd, cmd, MAX_SHELL_CMD_LEN);
	if (argv_len > 0 && argv) {
		strncpy(req.argv, argv, MAX_SHELL_ARGV_LEN);
		req.argv_len = argv_len;
	}

	__noa_wlan_fw_request_send_sim(kWlanCmdShellRequest, &req);
}

struct device *ncp_wlan_get_wlan_client_device(void)
{
	return ncp_wlan_ml.client_dev;
}
#else
/**
 * @brief Initializes the NCP WLAN Middle Layer platform
 * (for CONFIG_NOA_WIFI_FW_V1).
 *
 * This function is a stub for when CONFIG_NOA_WIFI_FW_V1 is enabled. It performs
 * no actions.
 *
 * @return 0
 */
int ncp_wlan_ml_platform_init(void)
{
	// Not supported.
	return 0;
}

/**
 * @brief Deinitializes the NCP WLAN Middle Layer platform
 * (for CONFIG_NOA_WIFI_FW_V1).
 *
 * This function is a stub for when CONFIG_NOA_WIFI_FW_V1 is enabled. It performs
 * no actions.
 */
void ncp_wlan_ml_platform_deinit(void)
{
	// Not supported.
}

void ncp_wlan_ml_shell_cmd_handler(const char *cmd, int argv_len, const char *argv)
{
	// Not supported.
}
#endif // !IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)
