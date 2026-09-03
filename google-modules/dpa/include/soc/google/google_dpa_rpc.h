/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This module is responsible for establishing the RPC communication
 * between the kernel and NOA. Additionally, it provides an API
 * allowing clients to acquire an RPC client handle for invoking
 * remote procedures.
 *
 * Initialization involves retrieving shared RPC and IPC configuration
 * data from the NOA shared memory region and subsequently
 * instantiating RPC clients for future communication.
 */

#ifndef _GOOGLE_DPA_RPC_CLIENT_H
#define _GOOGLE_DPA_RPC_CLIENT_H

struct PwRpcClientStruct;

/**
 * google_dpa_rpc_ncp_client() Return NCP client.
 *
 * A client is usable only after receiving READY event and until receiving
 * UNAVAILABLE event.
 *
 * Return: NCP RPC client pointer.
 */
struct PwRpcClientStruct *google_dpa_rpc_ncp_client(void);

/**
 * google_dpa_rpc_nep_client() Return NEP RPC client.
 *
 * A client is usable only after receiving READY event and until receiving
 * UNAVAILABLE event.
 *
 * Return: NEP RPC client pointer.
 */
struct PwRpcClientStruct *google_dpa_rpc_nep_client(void);

#endif /* _GOOGLE_DPA_RPC_CLIENT_H */
