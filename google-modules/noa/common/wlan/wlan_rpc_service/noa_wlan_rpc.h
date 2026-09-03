/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __NOA_WLAN_RPC_H__
#define __NOA_WLAN_RPC_H__

#include <linux/types.h>

int noa_wlan_fw_event_recv(int event, void *msg);
int __noa_wlan_fw_request_send_rpc(int cmd, void *msg, size_t len);
int noa_wlan_rpc_init(void);
void noa_wlan_rpc_deinit(void);

#endif  /* __NOA_WLAN_RPC_H__ */
