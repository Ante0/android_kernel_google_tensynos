// SPDX-License-Identifier: GPL-2.0-only
/*
 * Utility functions for NEP Service.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifdef linux
#include "nep_service.h"
#else
#include "net/nep_service.h"
#endif

static struct NepRpcService nep_rpc_service;

struct NepRpcService* GetNepService(void) {
    if (nep_rpc_service.send_event_to_apc_from_nep == nullptr) {
#ifdef linux
        pr_info("%s: SendEventToApcFromNep function is not initialized.\n", __func__);
#else
        PW_LOG_INFO("%s: SendEventToApcFromNep function is not initialized.", __func__);
#endif
    }
    return &nep_rpc_service;
}

void NepRpcServiceCallbackInit(SendEventToApcFromNep send_event_to_apc_from_nep) {
    nep_rpc_service.send_event_to_apc_from_nep = send_event_to_apc_from_nep;
}
