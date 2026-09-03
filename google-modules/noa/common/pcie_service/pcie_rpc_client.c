// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA PCIe RPC Client
 *
 * Copyright (c) 2025 Google LLC.
 */

#include <soc/google/google_dpa_rpc.h>

#include "pcie_rpc_client.h"

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/pcie_rpc_service_client.nanopb.h"

#define MAX_RPC_DELAY 30

static void error_callback(struct PwRpcCallStruct *call, PwStatus err)
{
	struct noa_pcie_rpc_handle *handle = (struct noa_pcie_rpc_handle *)call->context;
	complete(&handle->rpc_completion);
}

static void complete_callback(struct PwRpcCallStruct *call, const uint8_t *payload,
			      size_t payload_size, PwStatus status)
{
	struct noa_pcie_rpc_handle *handle = (struct noa_pcie_rpc_handle *)call->context;
	complete(&handle->rpc_completion);
}

static u32 call_id;
int send_rpc(struct noa_pcie_rpc_handle *handle, u32 domain, dma_addr_t addr)
{
	int err = 0;
	PwRpcClient *client;
	noa_service_pcie_service_RegionConfigRequest request;
	mutex_lock(&handle->rpc_mutex);
	client = google_dpa_rpc_ncp_client();
	if (client == NULL) {
		err = -ENODEV;
		goto unlock;
	}
	memset((void *)&request, 0, sizeof(noa_service_pcie_service_RegionConfigRequest));
	request.controller = domain;
	request.shared_buf = addr;
	reinit_completion(&handle->rpc_completion);
	PwStatus status =
		PCIeCmdServiceRegionConfigCommand(client, &request, complete_callback,
						  error_callback, (void *)handle, &call_id);
	if (status != kPwStatusOk) {
		pr_err("%s PCIeCmdServiceRegionConfigCommand failed with status=%d\n", __func__,
		       status);
		err = -EPROTO;
		goto unlock;
	}
	if (!wait_for_completion_timeout(&handle->rpc_completion, MAX_RPC_DELAY * HZ)) {
		err = -ETIMEDOUT;
	}

unlock:
	mutex_unlock(&handle->rpc_mutex);
	return err;
}

int noa_pcie_rpc_client_init(struct noa_pcie_rpc_handle *handle)
{
	mutex_init(&handle->rpc_mutex);
	init_completion(&handle->rpc_completion);
	return 0;
}

int noa_pcie_rpc_client_exit(struct noa_pcie_rpc_handle *handle)
{
	mutex_destroy(&handle->rpc_mutex);
	return 0;
}
