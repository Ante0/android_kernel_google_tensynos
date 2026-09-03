// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file for NOA NEP CMD RPC
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifndef __NOA_NEP_CMD_RPC_H__
#define __NOA_NEP_CMD_RPC_H__

#include <linux/types.h>

/**
 * @brief Sends a NEP command through RPC.
 *
 * Sends NEP command request using RPC. Designated for FullSoc mode
 * where RPC is supported.
 *
 * @param cmd The command id.
 * @param msg Pointer to the command message.
 * @param len Length of the command message.
 * @param res Pointer to the command response buffer.
 * @param res_code Pointer to the command response code.
 *
 * @return 0 on success transmission, otherwise a negative error code on failure.
 */
int noa_nep_cmd_request_send_rpc(int cmd, void *msg, size_t len, void *res, int *res_code);

/**
 * @brief Initializes the NEP command RPC client.
 */
void noa_nep_cmd_rpc_init(void);

#endif  /* __NOA_WLAN_RPC_H__ */
