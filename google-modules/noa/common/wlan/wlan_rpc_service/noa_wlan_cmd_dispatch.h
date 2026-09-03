/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header File for NOA WLAN Firmware Command Dispatcher
 *
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __NOA_WLAN_CMD_DISPATCH_H__
#define __NOA_WLAN_CMD_DISPATCH_H__

struct wlan_event_cmd_completion {
	u32 cmd;
	int result;
};

/**
 * @brief Interface function for NCP commands with synchronized mode.
 *
 * This interface function synchronously sends NCP commands to a command workqueue,
 * waiting for the command result before returning.
 *
 * @param cmd The command id.
 * @param data Pointer to the command message.
 * @param len Length of the command message.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
extern int noa_wlan_fw_request_send_sync(u32 cmd, void *data, size_t len);

/**
 * @brief Interface function for NCP commands with asynchronized mode.
 *
 * This interface function asynchronously sends NCP commands by enqueuing
 * them to a command workqueue and returning immediately.
 *
 * @param cmd The command id.
 * @param data Pointer to the command message.
 * @param len Length of the command message.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
extern int noa_wlan_fw_request_send_async(u32 cmd, void *data, size_t len);

/**
 * @brief Interface function for NCP doorbell with synchronized mode.
 *
 * This interface function synchronously sends NCP doorbell to a command workqueue,
 * waiting for the command result before returning.
 *
 * @param cmd The command id.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
extern int noa_wlan_fw_request_doorbell_sync(u32 cmd);

/**
 * @brief Interface function for NCP doorbell with asynchronized mode.
 *
 * This interface function asynchronously sends NCP doorbell by enqueuing
 * them to a command workqueue and returning immediately.
 *
 * @param cmd The command id.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
extern int noa_wlan_fw_request_doorbell_async(u32 cmd);

/**
 * @brief Command completion handler from NCP
 *
 * This handler function is recving the command completion event from
 * NCP to complete the command from command wait queue.
 *
 * @param msg The command completion struct
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
extern int noa_wlan_fw_event_cmd_completion(void *msg);

/**
 * @brief Initial function of wlan command workqueue.
 *
 * @param data The private data that used in the command workqueue.
 */
extern void noa_wlan_cmd_wq_init(void *data);

/**
 * @brief Exit function of wlan command workqueue.
 *
 * @param data The private data that used in the command workqueue.
 */
extern void noa_wlan_cmd_wq_exit(void *data);

/**
 * @brief Interface function for NCP commands.
 *
 * This interface function acts as a wrapper, dynamically choosing between
 * synchronous and asynchronous command sending mechanisms based on the
 * interrupt context.
 *
 * @param cmd The command id.
 * @param data Pointer to the command message.
 * @param len Length of the command message.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
static inline int noa_wlan_fw_request_send(u32 cmd, void *data, size_t len)
{
	if (in_interrupt()) {
		return noa_wlan_fw_request_send_async(cmd, data, len);
	} else {
		return noa_wlan_fw_request_send_sync(cmd, data, len);
	}
}

/**
 * @brief Interface function for NCP doorbell.
 *
 * This interface function acts as a wrapper, dynamically choosing between
 * synchronous and asynchronous doorbell sending mechanisms based on the
 * interrupt context.
 *
 * @param cmd The command id.
 * @param data Pointer to the command message.
 * @param len Length of the command message.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
static inline int noa_wlan_fw_request_doorbell(u32 cmd)
{
	if (in_interrupt()) {
		return noa_wlan_fw_request_doorbell_async(cmd);
	} else {
		return noa_wlan_fw_request_doorbell_sync(cmd);
	}
}

#endif  /* __NOA_WLAN_CMD_DISPATCH_H__ */
