// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NEP ring management instance
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "ring_manager_instance.h"

#include <linux/errno.h>
#include <linux/printk.h>

#include "common/ring.h"
#include "common/ring_id.h"
#include "common/noa_ring_id.h"
#include "common/core.h"
#include "common/noatrace.h"
#include "common/inttypes.h"
#include "port.h"
#include "ring_descriptor.h"
#include "dma_processor.h"
#else /* linux */
#include "ring_mgmt/ring_manager_instance.h"

#include <cstdint>
#include <cerrno>
#include <cinttypes>

#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/noa_ring_id.h"
#include "device_mgmt/manager.h"
#include "dma/dma.h"
#include "linux_port/log.h"
#include "ring_mgmt/port.h"
#include "ring_mgmt/port_instance.h"
#include "ring_mgmt/ring_descriptor.h"
#include "ring_mgmt/dma_processor.h"
#endif /* linux */

struct ring_manager_instance g_ring_manager_instance_array[NOA_PORT_MAX][RING_TYPE_MAX];

struct ring_manager_instance *noa_ring_manager_instance_get(uint8_t port_id, uint8_t type)
{
	if (port_id >= NOA_PORT_MAX || type >= RING_TYPE_MAX) {
		return NULL;
	}
	return &g_ring_manager_instance_array[port_id][type];
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ring_manager_instance_get);
#endif /* linux */

int noa_ring_manager_ring_activate(uint8_t path_id, uint8_t direction)
{
	struct ring_manager_instance *instance =
		NoaRingManagerInfoInstanceGetById(path_id, direction);
	if (!instance) {
		return -EINVAL;
	}
	noa_ring_activate(&instance->ring);
	// Ring services require the base address from the DPA base view.
	instance->ring.basic.base = instance->ring.basic.dpa_base;
	if (instance->reset) {
		instance->reset(instance);
	}
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ring_manager_ring_activate);
#endif /* linux */

#ifndef linux
int noa_ring_manager_ring_deactivate(struct ring_manager_instance *instance, uint8_t path_id,
				     RingStoppedCallback &&callback)
{
	if (!instance) {
		return -EINVAL;
	} else if (!is_noa_ring_activate(&instance->ring)) {
		callback(pw::OkStatus());
		return 0;
	} else if (instance->is_stopping) {
		callback(pw::Status::FailedPrecondition());
		PW_LOG_ERROR("Ring %s is stopping, abort this deactivate event",
			     instance->ring.name);
		return 0;
	}
	if (instance->stopping) {
		instance->stopped_callback = std::move(callback);
		return instance->stopping(instance, path_id);
	} else {
		noa_ring_deactivate(&instance->ring);
		callback(pw::OkStatus());
	}
	return 0;
}

int noa_ring_manager_ring_deactivate(uint8_t path_id, uint8_t direction,
				     RingStoppedCallback &&callback)
{
	return noa_ring_manager_ring_deactivate(NoaRingManagerInfoInstanceGetById(path_id,
										  direction),
						path_id, std::move(callback));
}
#endif /* linux */

int noa_ring_manager_ring_deactivate(uint8_t path_id, uint8_t direction)
{
	struct ring_manager_instance *instance =
		NoaRingManagerInfoInstanceGetById(path_id, direction);
	if (!instance) {
		return -EINVAL;
	} else if (!is_noa_ring_activate(&instance->ring)) {
		return 0;
	}
	if (instance->stopping) {
		return instance->stopping(instance, path_id);
	}
	noa_ring_deactivate(&instance->ring);
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ring_manager_ring_deactivate);
#endif /* linux */

static void trigger_port_interrupt(struct noa_ring_wrapper *ring)
{
	struct noa_port *port = NULL;
	struct ring_manager_instance *instance = (struct ring_manager_instance *)ring->owner;
	if (!instance) {
		dev_err(get_ring_device(), "Ring instance is NULL, failed to trigger interrupt\n");
		return;
	}
	port = noa_sim_get_port(instance->doorbell_port_id);
	if (!port) {
		dev_err(get_ring_device(), "Port is NULL, failed to trigger interrupt\n");
		return;
	}

	if (noa_ring_pos_is_moved(ring)) {
#ifdef linux
		trace_ring_port_trigger_interrupt(port->idx);
		port->ints |= 1;
		noa_sim_trig_tx();
#else /* linux */
		if (port->idx == NOA_PORT_NETENGINE) {
			tasklet_schedule(&port->doorbell_task);
		} else if (port->notifier) {
			port->notifier->Notify();
		} else {
			dev_err(get_ring_device(), "Port %d didn't register notifier", port->idx);
		}
#endif /* linux */
	}
}

static const struct noa_ring_ops noa_ring_manager_consumer_ops = {
	.payload_len = noa_ring_manager_payload_parser,
	.read_payload = noa_generic_read_raw_pointer,
	.fill_noop = NULL,
	.write_payload = NULL,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

static const struct noa_ring_ops noa_ring_manager_producer_ops = {
	.payload_len = NULL,
	.read_payload = NULL,
	.fill_noop = noa_ring_manager_noop_payload,
	.write_payload = noa_ring_manager_desc_transfer,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = trigger_port_interrupt,
};

int noa_ring_manager_instance_init(void)
{
	int32_t ret;
	uint8_t id;
#ifdef linux
	void *dma = NULL;
#else /* linux */
	namespace DmaDrv = ::noa::driver::dma;
	namespace DevMgr = ::noa::module::device_mgmt;

	auto *dma = DevMgr::DeviceManager::Instance()->GetDevice<DmaDrv::Dma>(DmaDrv::DmaControllerId::kDpaDmaController);
	if (!dma) {
		PW_LOG_WARN("Failed to get dma driver");
		return -EINVAL;
	}
#endif /* linux */

	for (id = 0; id < NOA_PORT_MAX; ++id) {
		uint8_t direction;
		for (direction = 0; direction < RING_TYPE_MAX; ++direction) {
			const uint8_t ring_type = (direction == INPUT) ? NOA_RING_TYPE_CONSUMER :
									 NOA_RING_TYPE_PRODUCER;
			const struct noa_ring_ops *ops = (direction == INPUT) ?
								 &noa_ring_manager_consumer_ops :
								 &noa_ring_manager_producer_ops;
			struct noa_ring_regs regs = { 0 };
			struct ring_manager_instance *instance =
				noa_ring_manager_instance_get(id, direction);
			if (!instance) {
				pr_err("Failed to get instance when init id %" PRIu16
				       ", direction %" PRIu16 "\n",
				       id, direction);
				return -EINVAL;
			}
			ret = noa_ring_manager_regs_get(&regs, id, (RingType)direction);
			if (ret) {
				pr_err("Failed to get regs when init id %" PRIu16
				       ", direction %" PRIu16 "\n",
				       id, direction);
				return ret;
			}
			ret = noa_ring_regs_wrapper_init(&instance->ring, ring_type, ops, &regs,
							 instance, noa_port_id_to_name(id), 0);
			if (ret) {
				pr_err("Failed to init wrapper when init id %" PRIu16
				       ", direction %" PRIu16 "\n",
				       id, direction);
				return ret;
			}
			instance->ring.wrap_buf = &instance->wrap_buf[0];
			instance->ring.wrap_buf_sz = sizeof(instance->wrap_buf);
			instance->doorbell_port_id = id;
#ifndef linux
			instance->dma.driver = dma;
#endif /* linux */
			instance->dma.channel = id;
			instance->dma.processing_desc = NULL;
			if (dma) {
				configure_dma(instance->dma.channel, dma);
			}
		}
	}
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ring_manager_instance_init);
#endif /* linux */

static struct NoaRingManagerInfoRoot *g_root = NULL;

struct ring_manager_instance *NoaRingManagerInfoInstanceGet(uint8_t interface, uint8_t flow,
							    uint8_t category, uint8_t direction)
{
	struct NoaRingManagerInfoRoot *root = g_root;
	if (!root || interface >= root->num || flow >= root->entries[interface]->num ||
	    category >= root->entries[interface]->entries[flow]->num) {
		pr_err("Failed to get ring instance with interface %" PRIu16 ", flow %" PRIu16
		       ", type %" PRIu16 ", dir %" PRIu16 "\n",
		       interface, flow, category, direction);
		return NULL;
	}
	return &root->entries[interface]->entries[flow]->entries[category].entries[direction];
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingManagerInfoInstanceGet);
#endif /* linux */

struct ring_manager_instance *NoaRingManagerInfoInstanceGetById(uint8_t id, uint8_t direction)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;

	NoaRingPathIdParse(id, &interface, &flow, &category);

	return NoaRingManagerInfoInstanceGet(interface, flow, category, direction);
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingManagerInfoInstanceGetById);
#endif /* linux */

static int32_t InitRingManagerInfo(struct NoaRingManagerInfo *info, const uint8_t interface,
				   const uint8_t flow, const uint8_t type, void *dma)
{
	const uint8_t output_port_id = NoaRingOutputPathToPort(interface, flow);
	const uint8_t isr_port_id = NoaRingInputPathToPort(interface, flow);
	uint8_t direction;

	if (output_port_id >= NOA_PORT_MAX || isr_port_id >= NOA_PORT_MAX) {
		pr_err("Failed to init ring info with interface %" PRIu16 " with flow %" PRIu16
		       "\n",
		       interface, flow);
		return -EINVAL;
	}

	// NEP ring manager do not handle rings between APC and NCP
	for (direction = 0; direction < kNoaNepRingAnyDirection; ++direction) {
		const uint8_t ring_type = (direction == kNoaRingNepInput) ? NOA_RING_TYPE_CONSUMER :
									    NOA_RING_TYPE_PRODUCER;
		const struct noa_ring_ops *ops = (direction == kNoaRingNepInput) ?
							 &noa_ring_manager_consumer_ops :
							 &noa_ring_manager_producer_ops;
		struct noa_ring_regs regs = { 0 };
		int32_t ret;
		struct ring_manager_instance *instance = &info->entries[direction];
		instance->ring_id = NoaRingPathIdConvert(interface, flow, type);
		instance->noa_ring_id = NoaRingIdMapping(interface, flow, type, direction);
		if (instance->noa_ring_id == kNoaUnknownRing) {
			pr_err("Invalid noa_ring_id with interface %" PRIu16 " flow %" PRIu16
			       " type %" PRIu16 " direction %" PRIu16,
			       interface, flow, type, direction);
			return -EINVAL;
		}

		ret = NoaRingSharedRegsGet(&regs, interface, flow, type, direction);
		if (ret) {
			pr_err("Failed to get regs when init noa ring with direction %" PRIu16
			       ", err %" PRId32 "\n",
			       direction, ret);
			return ret;
		}
		ret = noa_ring_regs_wrapper_init(&instance->ring, ring_type, ops, &regs, instance,
						 instance->name, 0);
		if (ret) {
			pr_err("Failed to init ring wrapper when init noa ring with direction %" PRIu16
			       ", err %" PRId32 "\n",
			       direction, ret);
			return ret;
		}
		instance->ring.wrap_buf = &instance->wrap_buf[0];
		instance->ring.wrap_buf_sz = sizeof(instance->wrap_buf);
		INIT_LIST_HEAD(&instance->stage_list);
		instance->reset = NULL;
	}
	// Register input ring into port isr handler
	{
		struct noa_port *port = noa_sim_get_port(isr_port_id);
		port->rings_bitmap |= (1U << type);
	}
	// Setup output ring
	{
		struct ring_manager_instance *instance = &info->entries[kNoaRingNepOutput];

		instance->doorbell_port_id = output_port_id;
#ifndef linux
		instance->dma.driver = dma;
#endif /* linux */
		instance->dma.channel = output_port_id;
		instance->dma.processing_desc = NULL;
		if (dma) {
			configure_dma(instance->dma.channel, dma);
		}
	}
	return 0;
}

void NoaRingManagerInstanceRootRegister(struct NoaRingManagerInfoRoot *root)
{
	g_root = root;
}

int32_t NoaRingManagerInstanceInit(struct NoaRingManagerInfoRoot *root, void *dma)
{
	uint8_t network_idx;
	uint8_t flow_idx;
	uint8_t category_idx;

	for (network_idx = 0; network_idx < root->num; network_idx++) {
		// Skip Apc Ncp direct interfaces
		if (network_idx == kNoaNetworkInterfaceWlanDirect ||
		    network_idx == kNoaNetworkInterfaceModemApcNcp) {
			continue;
		}

		struct NoaRingManagerInfoNetwork *network = root->entries[network_idx];
		if (!network) {
			continue;
		}
		for (flow_idx = 0; flow_idx < network->num; ++flow_idx) {
			struct NoaRingManagerInfoFlow *flow = network->entries[flow_idx];
			if (!flow) {
				continue;
			}
			for (category_idx = 0; category_idx < flow->num; ++category_idx) {
				struct NoaRingManagerInfo *info = &flow->entries[category_idx];
				int32_t ret = InitRingManagerInfo(info, network_idx, flow_idx,
								  category_idx, dma);
				if (ret) {
					pr_err("Falied to init ring with network %" PRIu16
					       ", flow %" PRIu16 ", type %" PRIu16 "\n",
					       network_idx, flow_idx, category_idx);
					return ret;
				}
			}
		}
	}

	NoaRingManagerInstanceRootRegister(root);
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingManagerInstanceInit);
#endif /* linux */

static inline uint64_t LoadRingFlag(struct noa_ring_wrapper *ring)
{
#ifdef linux
	return ring->flags;
#else /* linux */
	return ring->flags.load();
#endif /* linux */
}

static int32_t DumpRingInfo(struct ring_manager_instance *instance, uint8_t id, char *buf,
			    int32_t len)
{
	struct noa_ring_wrapper *ring = &instance->ring;
	return snprintf(buf, len,
			"Ring %s <%" PRIx16 "> flag 0x%" PRIu32 ", base 0x%lx, dpa_base 0x%lx"
			", size %" PRIu32 ", item_len %" PRIu32 ", read %" PRIu32 ", write %" PRIu32
			"\n",
			instance->name, id, (uint32_t)LoadRingFlag(ring),
			(unsigned long)ring->basic.base, (unsigned long)ring->basic.dpa_base,
			ring->basic.size, ring->basic.item_len, noa_ring_tail_read_once(ring),
			noa_ring_head_read_once(ring));
}

void NoaRingManagerInstancePrint(uint8_t interface, uint8_t flow, uint8_t category,
				 uint8_t direction)
{
	char buf[128] = { 0 };
	int32_t size;
	struct ring_manager_instance *instance =
		NoaRingManagerInfoInstanceGet(interface, flow, category, direction);

	if (!instance) {
		pr_err("Failed to print ring with invalid interface %" PRIu16 ", flow %" PRIu16
		       ", category %" PRIu16 ", direction %" PRIu16 "\n",
		       interface, flow, category, direction);
		return;
	}

	size = DumpRingInfo(instance, NoaRingPathIdConvert(interface, flow, category), buf,
			    sizeof(buf));
	if (size < 0) {
		pr_err("Failed to print ring with err %" PRId32 ", interface %" PRIu16
		       ", flow %" PRIu16 ", category %" PRIu16 ", direction %" PRIu16 "\n",
		       size, interface, flow, category, direction);
		return;
	}
	pr_err("%s", buf);
}

void NoaRingManagerInstancePrintAll(void)
{
	char buf[128] = { 0 };
	int32_t size = 0;
	uint8_t network_idx;
	uint8_t flow_idx;
	uint8_t category_idx;
	uint8_t direction_idx;

	if (!g_root) {
		return;
	}

	for (network_idx = 0; network_idx < g_root->num; network_idx++) {
		if (network_idx == kNoaNetworkInterfaceWlanDirect ||
		    network_idx == kNoaNetworkInterfaceModemApcNcp) {
			continue;
		}
		struct NoaRingManagerInfoNetwork *network = g_root->entries[network_idx];
		if (!network) {
			continue;
		}
		for (flow_idx = 0; flow_idx < network->num; ++flow_idx) {
			struct NoaRingManagerInfoFlow *flow = network->entries[flow_idx];
			if (!flow) {
				continue;
			}
			for (category_idx = 0; category_idx < flow->num; ++category_idx) {
				struct NoaRingManagerInfo *info = &flow->entries[category_idx];

				for (direction_idx = 0; direction_idx < kNoaNepRingAnyDirection;
				     ++direction_idx) {
					size = DumpRingInfo(&info->entries[direction_idx],
							    NoaRingPathIdConvert(network_idx,
										 flow_idx,
										 category_idx),
							    buf, sizeof(buf));
					if (size < 0) {
						continue;
					}
					pr_err("%s", buf);
				}
			}
		}
	}
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingManagerInstancePrintAll);
#endif /* linux */

int32_t NoaRingManagerInstanceDumpAll(char *buf, int32_t len)
{
	int32_t size = 0;
	int32_t offset = 0;
	uint8_t network_idx;
	uint8_t flow_idx;
	uint8_t category_idx;
	uint8_t direction_idx;

	if (!g_root) {
		pr_err("Root is un-register\n");
		return -EINVAL;
	}

	for (network_idx = 0; network_idx < g_root->num; network_idx++) {
		struct NoaRingManagerInfoNetwork *network = g_root->entries[network_idx];
		if (!network) {
			continue;
		}
		for (flow_idx = 0; flow_idx < network->num; ++flow_idx) {
			struct NoaRingManagerInfoFlow *flow = network->entries[flow_idx];
			if (!flow) {
				continue;
			}
			for (category_idx = 0; category_idx < flow->num; ++category_idx) {
				struct NoaRingManagerInfo *info = &flow->entries[category_idx];

				for (direction_idx = 0; direction_idx < kNoaRingNepDirectionMax;
				     ++direction_idx) {
					size = DumpRingInfo(&info->entries[direction_idx],
							    NoaRingPathIdConvert(network_idx,
										 flow_idx,
										 category_idx),
							    buf + offset, len);
					if (size < 0) {
						continue;
					}
					offset += size;
					len -= size;
				}
			}
		}
	}
	return offset;
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingManagerInstanceDumpAll);
#endif /* linux */

