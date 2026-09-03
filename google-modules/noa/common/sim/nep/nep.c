// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NEP simulator
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <common/noatrace.h>
#include <common/map_def.h>

#include <net/nep_cmd_rpc_service/noa_nep_cmd_dispatch.h>

#include "dma_simulator.h"
#include "nep.h"
#include "nep_tables.h"
#include "network_pipeline_service/service.h"
#include "ra_proxy.h"
#include "ring_manager_instance.h"
#include "ring_shared_info_instance.h"
#include "ring_controller.h"
#include "ring_manager.h"
#include "ring_buffer_pool.h"
#include "port_init.h"
#include "vpn/packed_xfrm.h"
#include "network_pipeline_service/service.h"
#include "network_pipeline_service/nested_ring_service.h"

#define NET_ENGINE_BUF_SIZE 1600
#define UTILIZATION_MAX_RECORD 1024
#define WAKE_UP_DURATION_MS 933

/*
 * Expected the net engine buffers should be in the SRAM.
 * The buffers are 1-1 mapping to a descriptor that doesn't
 * need to extra effort to maintain.
 */

static struct noa_simulator noa_sim;

int noa_port_irq_get(uint8_t port_id)
{
	switch (port_id) {
	case NOA_PORT_WLAN_SW:
		return 2;
	case NOA_PORT_MODEM_SW:
		return 4;
	case NOA_PORT_WLAN_FW:
		return 1;
	case NOA_PORT_MODEM_FW:
		return 5;
	case NOA_PORT_NETENGINE:
		return 3;
	};
	return 0;
}
EXPORT_SYMBOL(noa_port_irq_get);

static void noa_sim_utilization_monitor_init(void)
{
	utilization_monitor_init(&noa_sim.offload_util, UTILIZATION_MAX_RECORD,
				 WAKE_UP_DURATION_MS);
	utilization_monitor_init(&noa_sim.no_offload_util, UTILIZATION_MAX_RECORD,
				 WAKE_UP_DURATION_MS);
}

static int nep_netengine_flowid_update_handler(struct noa_simulator *nep, void *msg)
{
	struct {
		char dst[ETH_ALEN];
		u8 priority;
		u8 enable;
		u32 flowid;
	} *cmd = msg;
	TetherFlowIdKey key = {
		.priority = cmd->priority,
	};
	memcpy(key.dstMac , cmd->dst, ETH_ALEN);
	if (cmd->enable)
		return nep_tables_flowid_map_add(&key, &cmd->flowid);
	return nep_tables_flowid_map_remove(&key);
}

static int nep_ring_active_handler(struct noa_simulator *nep, void *msg)
{
	struct {
		bool active;
		uint8_t path_id;
		uint8_t direction;
	} *cmd = msg;

	if (cmd->active) {
		return noa_ring_manager_ring_activate(cmd->path_id, cmd->direction);
	}
	return noa_ring_manager_ring_deactivate(cmd->path_id, cmd->direction);
}

void nep_ring_service_request_send(int event, bool active, uint8_t id, uint8_t direction)
{
	struct {
		bool active;
		uint8_t id;
		uint8_t direction;
	} cmd = {
		.active = active,
		.id = id,
		.direction = direction,
	};
	nep_request_send(event, (void *)&cmd);
}
EXPORT_SYMBOL_GPL(nep_ring_service_request_send);

void nep_ring_service_request_send_with_callback(int event, bool active, uint8_t id,
						 uint8_t direction,
						 void (*callback)(int32_t status, void *context),
						 void *context)
{
	int32_t ret;
	struct {
		bool active;
		uint8_t id;
		uint8_t direction;
	} cmd = {
		.active = active,
		.id = id,
		.direction = direction,
	};
	ret = nep_request_send(event, (void *)&cmd);
	callback(ret, context);
}
EXPORT_SYMBOL_GPL(nep_ring_service_request_send_with_callback);

int nep_request_send(u32 cmd, void *msg)
{
	int ret = -EINVAL;
	struct noa_simulator *nep = &noa_sim;

	switch(cmd) {
	case NEP_CMD_NETENGINE_FLOWID_UPDATE:
		ret = nep_netengine_flowid_update_handler(nep, msg);
		break;
	case NEP_CMD_RING_ACTIVE:
		ret = nep_ring_active_handler(nep, msg);
		break;
	default:
		dev_err(get_ring_device(), "Unsupported CMD ID %d\n", cmd);
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(nep_request_send);

/* Call port ISR to handle NOA output ring. */
static void noa_output_ring_db(unsigned long data)
{
	struct noa_simulator *sim = (struct noa_simulator *)data;
	struct noa_port *port;
	int i;

	for (i = 0; i < NOA_PORT_MAX; i++) {
		port = &sim->ports[i];
		if (!port->ints)
			continue;
		/* clear INTS before handle to avoid interrupt lost */
		if (NOA_SIM_DIRECT_TX)
			noa_doorbell_task((unsigned long)port);
		else
			tasklet_schedule(&port->doorbell_task);
	}
}

/* Trigger Port interrupt to handle NOA output ring */
void noa_sim_trig_tx(void)
{
	struct noa_simulator *sim = &noa_sim;

	if (NOA_SIM_DIRECT_DB)
		noa_output_ring_db((unsigned long)sim);
	else
		tasklet_schedule(&sim->output_ring_db);
}
EXPORT_SYMBOL_GPL(noa_sim_trig_tx);

/* Input ring dispatcher */
static void noa_input_ring_isr(unsigned long data)
{
	struct noa_simulator *sim = (struct noa_simulator *)data;
	struct noa_port *port;
	int i;

	/* schedule task depend on doorbell flags */
	for (i = 0; i < NOA_PORT_MAX; i++) {
		port = &sim->ports[i];
		if (!port->doorbell)
			continue;
		port->doorbell = 0;
		tasklet_schedule(&port->input_task);
	}
}

struct noa_port *noa_sim_get_port(u8 id)
{
	if (id >= NOA_PORT_MAX)
		return NULL;
	return &noa_sim.ports[id];
}
EXPORT_SYMBOL_GPL(noa_sim_get_port);

struct noa_simulator *noa_sim_get(void)
{
	noa_sim.nep_stat = *get_nep_stat();
	return &noa_sim;
}
EXPORT_SYMBOL_GPL(noa_sim_get);

// TODO: Move to util and it can be replaced by nofify_port(NOA_PORT_WLAN_SW).
void noa_sim_interrupt_to_wlansw(int irq)
{
	struct noa_port *port = noa_sim_get_port(NOA_PORT_WLAN_SW);

	port->ints |= 1;
	port->rcv_irq = irq;
	noa_sim_trig_tx();
}
EXPORT_SYMBOL_GPL(noa_sim_interrupt_to_wlansw);

/* Trigger NOA DMA scheduler */
void noa_sim_trig_rx(void)
{
	struct noa_simulator *sim = &noa_sim;
	if (NOA_SIM_DIRECT_ISR)
		noa_input_ring_isr((unsigned long)sim);
	else
		tasklet_schedule(&sim->input_ring_isr);
}
EXPORT_SYMBOL_GPL(noa_sim_trig_rx);

/* Register Port ISR */
int noa_interrupt_register(int id, irq_handler_t isr, void *priv)
{
	struct noa_port *hw = NULL;
	struct noa_simulator *sim = &noa_sim;
	int i;

	for (i = 0; i < NOA_PORT_MAX; i++) {
		if (sim->ports[i].irq == id) {
			hw = &sim->ports[i];
			break;
		}
	}

	if (!hw) {
		dev_err(&sim->dev, "%s(): id %d is not found!\n", __func__, id);
		return -ENODEV;
	}
	hw->isr = isr;
	hw->priv = priv;
	return 0;
}
EXPORT_SYMBOL_GPL(noa_interrupt_register);

void noa_interrupt_unregister(int id, void *priv)
{
	struct noa_port *hw = NULL;
	struct noa_simulator *sim = &noa_sim;
	int i;

	for (i = 0; i < NOA_PORT_MAX; i++) {
		if (sim->ports[i].irq == id) {
			hw = &sim->ports[i];
			break;
		}
	}

	if (!hw || hw->priv != priv) {
		dev_err(&sim->dev, "%s(): id %d is not found!\n", __func__, id);
		return;
	}
	hw->isr = NULL;
	hw->priv = NULL;
}
EXPORT_SYMBOL_GPL(noa_interrupt_unregister);

bool is_sim_force_fallback(void)
{
	return noa_sim.force_fallback;
}

int noa_nep_cmd_request_send_sim(int cmd, void* msg) {
	int ret = -1;
	pr_info("NEP cmd type: %d", cmd);

	switch(cmd) {
		case CMD_PUT_UPSTREAM_4MAP: {
			Tether4Entry* entry = (Tether4Entry*) msg;
			ret = noa_sim_put_ipv4_entry(entry, true);
			break;
		}
		case CMD_PUT_DOWNSTREAM_4MAP: {
			Tether4Entry* entry = (Tether4Entry*) msg;
			ret = noa_sim_put_ipv4_entry(entry, false);
			break;
		}
		case CMD_REMOVE_UPSTREAM_4MAP: {
			Tether4Key* key = (Tether4Key*) msg;
			ret = noa_sim_remove_ipv4_entry(key, true);
			break;
		}
		case CMD_REMOVE_DOWNSTREAM_4MAP: {
			Tether4Key* key = (Tether4Key*) msg;
			ret = noa_sim_remove_ipv4_entry(key, false);
			break;
		}
		case CMD_PUT_UPSTREAM_6MAP: {
			TetherUpstream6Entry* entry = (TetherUpstream6Entry*) msg;
			ret = noa_sim_put_upstream_ipv6_entry(entry);
			break;
		}
		case CMD_PUT_DOWNSTREAM_6MAP: {
			TetherDownstream6Entry* entry = (TetherDownstream6Entry*) msg;
			ret = noa_sim_put_downstream_ipv6_entry(entry);
			break;
		}
		case CMD_REMOVE_UPSTREAM_6MAP: {
			TetherUpstream6Key* key = (TetherUpstream6Key*) msg;
			ret = noa_sim_remove_upstream_ipv6_entry(key);
			break;
		}
		case CMD_REMOVE_DOWNSTREAM_6MAP: {
			TetherDownstream6Key* key = (TetherDownstream6Key*) msg;
			ret = noa_sim_remove_downstream_ipv6_entry(key);
			break;
		}
		case CMD_SET_CONFIG: {
			TetherConfig* config = (TetherConfig*) msg;
			ret = noa_sim_set_config(config);
			break;
		}
		case CMD_REMOVE_CONFIG: {
			ret = noa_sim_remove_config();
			break;
		}
		case CMD_GET_STATS: {
			TetherStats* stats = (TetherStats*) msg;
			ret = noa_sim_get_stats(stats);
			break;
		}
		case CMD_PUT_NETLINK_CONF: {
			NetlinkConfig* netlink_configp = (NetlinkConfig*) msg;
			pr_debug("%s: netlink_conf: type %d \n",
                                 __func__, netlink_configp->type);
			ret = 0;
			break;
		}
		case CMD_VPN_ADD_SA: {
			packed_xfrm_state* sa = (packed_xfrm_state*) msg;
			pr_info("%s: VpnAddSa: spi=0x%x, seq=%d \n",
				__func__, htonl(sa->id.spi), sa->seq);
			ret = 0;
			break;
		}
		case CMD_VPN_DEL_SA: {
			u32* seq = (u32*) msg;
			pr_info("%s: VpnDelSa: seq=%d \n", __func__, *seq);
			ret = 0;
			break;
		}
		case CMD_VPN_FREE_SA: {
			u32* seq = (u32*) msg;
			pr_info("%s: VpnFreeSa: seq=%d \n", __func__, *seq);
			ret = 0;
			break;
		}
		default: {
			pr_info("cmd type: %d is not supported!", cmd);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(noa_nep_cmd_request_send_sim);

int get_monitor_packet_type(void) {
	return noa_sim.monitor_packet_type;
}

int noa_sim_put_ipv4_entry(Tether4Entry *entry, bool is_upstream)
{
	Tether4Key *key;
	Tether4Value *value;
	trace_nep_put_tether_4_entry(entry, is_upstream);
	key = (Tether4Key *) &(entry->key);
	value= (Tether4Value *) &(entry->value);

	if (is_upstream) {
		nep_tables_upstream4_map_add(key, value);
	} else {
		nep_tables_downstream4_map_add(key, value);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_put_ipv4_entry);

int noa_sim_remove_ipv4_entry(Tether4Key *key, bool is_upstream)
{
	pr_info("NEP %s: %s port %d/%d", __func__, is_upstream ? "UPSTREAM" : "DOWNSTREAM",
		key->srcPort, key->dstPort);
	if (is_upstream) {
		nep_tables_upstream4_map_remove((Tether4Key *) key);
	} else {
		nep_tables_downstream4_map_remove((Tether4Key *) key);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_remove_ipv4_entry);

int noa_sim_put_upstream_ipv6_entry(TetherUpstream6Entry *entry)
{
	TetherUpstream6Key *key;
	Tether6Value *value;
	pr_info("NEP %s: src64 %llu", __func__, entry->key.src64);
	key = (TetherUpstream6Key *) &(entry->key);
	value= (Tether6Value *) &(entry->value);

	nep_tables_upstream6_map_add(key, value);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_put_upstream_ipv6_entry);

int noa_sim_remove_upstream_ipv6_entry(TetherUpstream6Key *key)
{
	pr_info("NEP %s: src64 %llu", __func__, key->src64);

	nep_tables_upstream6_map_remove((TetherUpstream6Key *) key);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_remove_upstream_ipv6_entry);

int noa_sim_put_downstream_ipv6_entry(TetherDownstream6Entry *entry)
{
	TetherDownstream6Key *key;
	Tether6Value *value;
	pr_info("NEP %s", __func__);
	key = (TetherDownstream6Key *) &(entry->key);
	value= (Tether6Value *) &(entry->value);

	nep_tables_downstream6_map_add(key, value);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_put_downstream_ipv6_entry);

int noa_sim_remove_downstream_ipv6_entry(TetherDownstream6Key *key)
{
	pr_info("NEP %s", __func__);
	nep_tables_downstream6_map_remove((TetherDownstream6Key *) key);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_remove_downstream_ipv6_entry);

int noa_sim_set_config(TetherConfig *config)
{
	struct offload_info* ol_info = get_tethering_offload_info();
	ol_info->upstreamIif = config->upstreamIif;
	ol_info->downstreamIif = config->downstreamIif;
	ol_info->pmtu = config->pmtu;
	memcpy(ol_info->upstream_if_Mac, config->upstream_if_Mac, ETH_ALEN);
	memcpy(ol_info->downstream_if_Mac, config->downstream_if_Mac, ETH_ALEN);
	ol_info->limit_bytes = config->limit;
	ol_info->ever_notify_limit_reach = false;
	ol_info->pmtu = 1500;
	memset(&ol_info->stats, 0, sizeof(TetherStatsValue));
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_set_config);

int noa_sim_remove_config(void)
{
	struct offload_info* ol_info = get_tethering_offload_info();
	ol_info->upstreamIif = 0;
	ol_info->downstreamIif = 0;
	ol_info->limit_bytes = 0;
	ol_info->ever_notify_limit_reach = false;
	memset(&ol_info->stats, 0, sizeof(TetherStatsValue));
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_remove_config);

int noa_sim_get_stats(TetherStats *stats)
{
	struct offload_info* ol_info = get_tethering_offload_info();
	stats->upstream = ol_info->upstreamIif;
	memcpy(&stats->value, &ol_info->stats, sizeof(TetherStatsValue));
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_get_stats);

int noa_sim_utilization_calculate(struct utilization_monitor *util_mon)
{
	return utilization_monitor_calculate(util_mon);
}
EXPORT_SYMBOL_GPL(noa_sim_utilization_calculate);

int noa_sim_utilization_clear(struct utilization_monitor *util_mon)
{
	return utilization_monitor_clear(util_mon);
}
EXPORT_SYMBOL_GPL(noa_sim_utilization_clear);

void noa_register_neteng_callback(const struct neteng_callback_ops *cb_ops)
{
	get_tethering_offload_info()->cb_ops = cb_ops;
}
EXPORT_SYMBOL_GPL(noa_register_neteng_callback);

static int __init nep_sim_init(void)
{
	struct noa_simulator *sim;
	int ret;

	sim = &noa_sim;
	device_initialize(&sim->dev);
	dev_set_name(&sim->dev, "noa_sim");
	ret = device_add(&sim->dev);
	if (ret)
		return ret;
	noa_sim_utilization_monitor_init();
	start_dma_simulator();
	noa_ports_init();
	memset(get_nep_stat(), 0, sizeof(*get_nep_stat));

#if !IS_ENABLED(CONFIG_NOA_NETWORK_PIPELINE_SERVICE)
	// Only for legacy ring service
	noa_ports_enable_ring_service();
#endif /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE */
	ret = NoaRingManagerInstanceInit(NoaRingManagerRootInstance(), NULL);
	if (ret) {
		pr_err("Failed to init ring instance, ret: %d\n", ret);
		return 0;
	}

#if IS_ENABLED(CONFIG_NOA_NETWORK_PIPELINE_SERVICE)
#if IS_ENABLED(CONFIG_NOA_NETWORK_PIPELINE_SERVICE_WITH_NESTED_RING)
	NepNestedRingServiceSetup();
#else /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE_WITH_NESTED_RING */
	NetworkPipelineServiceSetup();
#endif /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE_WITH_NESTED_RING */
#endif /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE */

	noa_ring_service_buffer_pool_register(noa_ring_service_buffer_pool_singleton());
	noa_ring_service_buffer_pool_init();

	tasklet_init(&sim->input_ring_isr, noa_input_ring_isr, (unsigned long)sim);
	tasklet_init(&sim->output_ring_db, noa_output_ring_db, (unsigned long)sim);
	sim->dbg = false;
	sim->force_fallback = false;
	sim->monitor_packet_type = MONITOR_PACKET_TYPE_NONE;
	/* initial network engine */
	nep_tables_init();
	ra_proxy_init();
	net_engine_init(sim);
	dev_err(&sim->dev, "%s(): Hello World new simulator init!\n", __func__);
	return 0;
}

static void __exit nep_sim_exit(void)
{
	struct noa_simulator *sim = &noa_sim;
#if IS_ENABLED(CONFIG_NOA_NETWORK_PIPELINE_SERVICE)
#if IS_ENABLED(CONFIG_NOA_NETWORK_PIPELINE_SERVICE_WITH_NESTED_RING)
	NepNestedRingServiceExit();
#else /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE_WITH_NESTED_RING */
	NetworkPipelineServiceExit();
#endif /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE_WITH_NESTED_RING */
#else /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE */
	noa_ports_free();
#endif /* CONFIG_NOA_NETWORK_PIPELINE_SERVICE */

	stop_dma_simulator();
	net_engine_exit();
	tasklet_kill(&sim->input_ring_isr);
	tasklet_kill(&sim->output_ring_db);
	device_del(&sim->dev);
}

module_init(nep_sim_init);
module_exit(nep_sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Star Chang <starchang@google.com>");
MODULE_DESCRIPTION("NEP Firmware Simualtor Driver");
