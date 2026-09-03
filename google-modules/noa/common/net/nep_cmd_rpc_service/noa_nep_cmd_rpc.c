// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA NEP CMD RPC
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#include "noa_nep_cmd_rpc.h"

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa_rpc.h>
#include "nep_cmd_rpc_client.h"
#include "nep_event_rpc_client.h"

int noa_nep_cmd_request_send_rpc(int cmd, void *msg, size_t len, void *res, int *res_code)
{
    return NepCmdRpcClientSendCommand(cmd, msg, len, res, res_code);
}
EXPORT_SYMBOL_GPL(noa_nep_cmd_request_send_rpc);

void noa_nep_cmd_rpc_init(void)
{
    NepCmdRpcClientInit(google_dpa_rpc_nep_client());
    nep_event_rpc_client_init(google_dpa_rpc_nep_client());
}
EXPORT_SYMBOL_GPL(noa_nep_cmd_rpc_init);

#else

int noa_nep_cmd_request_send_rpc(int cmd, void *msg, size_t len, void *res, int *res_code)
{
    /* Not supported */
    return 0;
}

void noa_nep_cmd_rpc_init(void)
{
    /* Not supported */
    return 0;
}

#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
