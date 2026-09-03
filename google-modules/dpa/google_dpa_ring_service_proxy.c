// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation for:
 * - client's calls to ring service RPC events.
 * - ring shared info registration for AP
 *
 * Copyright (c) 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 *         Wilson Chen <wilsonwh@google.com>
 */
#include "google_dpa_internal.h"
#include "google_dpa_ring_service_proxy_internel.h"
#include <soc/google/google_dpa_ring_service_proxy.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include <linux/completion.h>
#include <linux/mutex.h>
#include <soc/google/google_dpa_rpc.h>
#include <soc/google/google_dpa_ctrl.h>
#include "pw_rpc_service_client/ring_event_service_client.nanopb.h"

struct ring_service_rpc_client {
	struct mutex mutex;
	PwRpcClient *rpc_client;
	struct completion compl;
	PwStatus compl_status;
};

static struct ring_service_rpc_client g_client;

static void google_dpa_ring_service_rpc_init(void)
{
	mutex_init(&g_client.mutex);
	init_completion(&g_client.compl);
	g_client.rpc_client = google_dpa_rpc_nep_client();
}

static void rpc_event_completed(struct PwRpcCallStruct *call, const uint8_t *payload,
				size_t payload_size, PwStatus status)
{
	(void)payload;
	(void)payload_size;
	struct ring_service_rpc_client *client = (struct ring_service_rpc_client *)call->context;
	client->compl_status = status;
	complete(&client->compl);
};

static PwStatus rpc_event_sent(noa_service_ring_event_service_EventType event, u8 path_id,
			       u8 direction)
{
	PwStatus status;
	noa_service_ring_event_service_Request request = {
		.event = event,
		.port = path_id,
		.type = direction,
	};

	if (mutex_lock_interruptible(&g_client.mutex))
		return kPwStatusAborted;

	status = RingEventServiceAction(g_client.rpc_client, &request, NULL, rpc_event_completed,
					NULL, &g_client, NULL);
	if (status != kPwStatusOk) {
		pr_err("Failed to send ring rpc event %u with id %u direction %u\n", event, path_id,
		       direction);
		goto out;
	}

	wait_for_completion(&g_client.compl);
	status = g_client.compl_status;
out:
	mutex_unlock(&g_client.mutex);
	return status;
}

PwStatus google_dpa_ring_service_rpc_event_activate(u8 path_id, u8 direction)
{
	return rpc_event_sent(noa_service_ring_event_service_EventType_RING_ACTIVATE, path_id,
			      direction);
}
EXPORT_SYMBOL(google_dpa_ring_service_rpc_event_activate);

PwStatus google_dpa_ring_service_rpc_event_deactivate(u8 path_id, u8 direction)
{
	return rpc_event_sent(noa_service_ring_event_service_EventType_RING_DEACTIVATE, path_id,
			      direction);
}
EXPORT_SYMBOL(google_dpa_ring_service_rpc_event_deactivate);

PwStatus google_dpa_ring_service_rpc_event_buffer_pool_activate(u8 port_id)
{
	return rpc_event_sent(noa_service_ring_event_service_EventType_BUFFER_POOL_ACTIVATE,
			      port_id, 0);
}
EXPORT_SYMBOL(google_dpa_ring_service_rpc_event_buffer_pool_activate);

PwStatus google_dpa_ring_service_rpc_event_buffer_pool_deactivate(u8 port_id)
{
	return rpc_event_sent(noa_service_ring_event_service_EventType_BUFFER_POOL_DEACTIVATE,
			      port_id, 0);
}
EXPORT_SYMBOL(google_dpa_ring_service_rpc_event_buffer_pool_deactivate);

PwStatus google_dpa_ring_service_rpc_event_netengine_activate(void)
{
	return rpc_event_sent(noa_service_ring_event_service_EventType_NETENGINE_ACTIVATE, 0, 0);
}
EXPORT_SYMBOL(google_dpa_ring_service_rpc_event_netengine_activate);

PwStatus google_dpa_ring_service_rpc_event_netengine_deactivate(void)
{
	return rpc_event_sent(noa_service_ring_event_service_EventType_NETENGINE_DEACTIVATE, 0, 0);
}
EXPORT_SYMBOL(google_dpa_ring_service_rpc_event_netengine_deactivate);

int google_dpa_ring_service_proxy_init(struct google_dpa *dpa)
{
	int ret = 0;
	u32 ring_info_addr;
	struct device *dev = dpa->dev;
	bool is_iomem;
	void __iomem *ring_info_vaddr = NULL;

	ret = google_dpa_get_shared_ring_info_device_addr(dpa, &ring_info_addr);
	if (ret) {
		dev_err(dev, "Failed to get dpa shared ring info, ret %d\n", ret);
		return ret;
	}

	ring_info_vaddr = google_dpa_da_to_va(dpa, GOOGLE_DPA_MCU_NCP, ring_info_addr,
					      sizeof(struct google_dpa_ring_shared_info),
					      &is_iomem);

	if (IS_ERR_OR_NULL(ring_info_vaddr)) {
		dev_err(dev, "Failed to translate dpa shared ring info addr, ret %ld\n",
			PTR_ERR(ring_info_vaddr));
		return -EINVAL;
	}

	dpa->ring_shared_info = (struct google_dpa_ring_shared_info *)ring_info_vaddr;

	google_dpa_ring_service_rpc_init();

	return 0;
}

struct google_dpa_ring *google_dpa_ring_shared_info_get(struct google_dpa *dpa, uint8_t ring_id)
{
	struct device *dev = dpa->dev;
	if (ring_id <= GOOGLE_DPA_UNKNOWN_RING || ring_id >= GOOGLE_DPA_RING_MAX) {
		dev_err(dev, "Failed to get dpa ring shared info, invalid ring id %d\n", ring_id);
		return NULL;
	}
	return &dpa->ring_shared_info->entries[ring_id];
}
EXPORT_SYMBOL(google_dpa_ring_shared_info_get);
