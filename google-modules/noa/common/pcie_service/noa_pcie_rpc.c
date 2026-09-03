// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA PCIe RPC
 *
 * Copyright (c) 2025 Google LLC.
 */

#include <linux/mutex.h>

#include <soc/google/google_dpa_ctrl.h>

#include "pcie_rpc_client.h"
#include "noa_pcie_rpc.h"

#define MAX_REGIONS 2

struct noa_pcie_shared_region_info {
	u32 domain;
	dma_addr_t region_base;
	struct dpa_client *dpa_client;
};

struct noa_pcie_shared_region_registry {
	struct noa_pcie_shared_region_info regions[MAX_REGIONS];
	struct noa_pcie_rpc_handle rpc_handle;
};

static struct noa_pcie_shared_region_registry registry;

static void pcie_dpa_state_change_cb(enum dpa_state state, void *cb_context)
{
	struct noa_pcie_shared_region_info *region;
	struct noa_pcie_shared_region_registry *registry;
	int ret;

	if (state != NOA_STATE_READY) {
		return;
	}

	region = (struct noa_pcie_shared_region_info *)cb_context;
	registry = container_of(region, struct noa_pcie_shared_region_registry,
				regions[region->domain]);
	ret = send_rpc(&registry->rpc_handle, region->domain, region->region_base);
	if (ret)
		pr_err("%s: Failed to send RPC for domain %d", __func__, region->domain);
}

static struct dpa_callbacks pcie_callbacks = {
	.on_state_changed = pcie_dpa_state_change_cb,
	.on_data_path_changed = NULL,
};

int noa_pcie_register_shared_region(int domain, dma_addr_t addr)
{
	if (domain >= MAX_REGIONS) {
		return -EINVAL;
	}

	registry.regions[domain].domain = domain;
	registry.regions[domain].region_base = addr;
	// Registering immediately notifies the DPA (if running) that the region has been updated.
	registry.regions[domain].dpa_client =
		google_dpa_ctrl_register("pcie", &pcie_callbacks, &registry.regions[domain]);

	if (!registry.regions[domain].dpa_client)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL(noa_pcie_register_shared_region);

int noa_pcie_unregister_shared_region(int domain)
{
	int ret = 0;

	google_dpa_ctrl_unregister(registry.regions[domain].dpa_client);
	registry.regions[domain].dpa_client = NULL;
	// Immediately notify the DPA (if running) that the region is no longer available.
	ret = send_rpc(&registry.rpc_handle, domain, 0);
	if (ret)
		pr_err("%s: Failed to send RPC for domain %d", __func__, domain);

	// By first unregistering the region's dpa_client, we ensure that pcie_dpa_state_change_cb will
	// never be queued or in flight at this point so the fields can be safely updated.
	registry.regions[domain].region_base = 0;
	return ret;
}
EXPORT_SYMBOL(noa_pcie_unregister_shared_region);

void noa_pcie_rpc_init(void)
{
	memset(&registry, 0, sizeof(registry));
	noa_pcie_rpc_client_init(&registry.rpc_handle);
}

void noa_pcie_rpc_exit(void)
{
	noa_pcie_rpc_client_exit(&registry.rpc_handle);
}
