/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __PCIE_RPC_CLIENT_H__
#define __PCIE_RPC_CLIENT_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct noa_pcie_rpc_handle {
	struct mutex rpc_mutex;
	struct completion rpc_completion;
};

int send_rpc(struct noa_pcie_rpc_handle *handle, u32 domain, dma_addr_t addr);
int noa_pcie_rpc_client_init(struct noa_pcie_rpc_handle *handle);
int noa_pcie_rpc_client_exit(struct noa_pcie_rpc_handle *handle);

#endif /* __PCIE_RPC_CLIENT_H__ */
