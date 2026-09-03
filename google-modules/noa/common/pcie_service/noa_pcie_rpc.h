/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __NOA_PCIE_RPC_H__
#define __NOA_PCIE_RPC_H__

#include <linux/types.h>

int noa_pcie_register_shared_region(int domain, dma_addr_t addr);
int noa_pcie_unregister_shared_region(int domain);

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
void noa_pcie_rpc_init(void);
void noa_pcie_rpc_exit(void);
#else
static void noa_pcie_rpc_init(void) {
    /* Not supported */
	return;
}
static void noa_pcie_rpc_exit(void) {
    /* Not supported */
	return;
}
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#endif /* __NOA_PCIE_RPC_H__ */
