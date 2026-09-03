// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file for NOA NEP CMD dispatcher
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifndef __NOA_NEP_CMD_DISPATCH_H__
#define __NOA_NEP_CMD_DISPATCH_H__

/**
 * @brief Sends NEP command request using RPC. Designated for FullSoc
 * mode where RPC is supported.
 */
extern int noa_nep_cmd_request_send_rpc(int cmd, void *msg, size_t len, void *res, int *res_code);

/**
 * @brief Sends a NEP command through function call, for driver
 * simulator mode.
 */
extern int noa_nep_cmd_request_send_sim(int cmd, void *msg);

/**
 * @brief Interface function for NEP commands.
 *
 * An interface function to facilitate sending NEP commands.
 * This function acts as a wrapper, selecting appropriate underlying
 * mechanism for sending command request between FullSoc mode and
 * driver simulator mode.
 *
 * @param cmd The command id.
 * @param msg Pointer to the command message.
 * @param len Length of the command message.
 * @param res Pointer to the command response buffer.
 * @param res_code Pointer to the command response code.
 *
 * @return 0 on success, otherwise a negative error code on failure.
 */
static inline int noa_nep_cmd_request_send(int cmd, void *msg, size_t len, void *res, int *res_code)
{
    if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT))
        return noa_nep_cmd_request_send_rpc(cmd, msg, len, res, res_code);
    else
        return noa_nep_cmd_request_send_sim(cmd, msg);
}

#endif  /* __NOA_NEP_CMD_DISPATCH_H__ */
