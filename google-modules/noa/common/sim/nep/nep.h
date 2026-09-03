/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_NEP_SIM_H__
#define __NOA_NEP_SIM_H__

#ifdef linux
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <common/core.h>
#include <common/ring.h>
#include <linux/time.h>
#include "nep_apf.h"
#include "port.h"
#else
#include "linux_port/tasklet.h"
#endif
#include "ring_manager.h"
#include "netengine.h"
#include "dma_processor.h"
#include "nep_map_table.h"
#include "offload.h"
#include "util/utilization_monitor.h"

struct noa_simulator;

/* NET_FIFO must be the first and second */
enum {
	NOA_IN_NET_FIFO,
	NOA_OUT_NET_FIFO,
	NOA_INPUT_RING0,
	NOA_INPUT_RING1, // MODEM_SW
	NOA_INPUT_RING2,
	NOA_INPUT_RING3, // MODEM_FW
	NOA_OUTPUT_RING0,
	NOA_OUTPUT_RING1, // MODEM_SW
	NOA_OUTPUT_RING2,
	NOA_OUTPUT_RING3, // MODEM_FW
	NOA_RING_MAX,
};

/* nep cmd id declartion */
enum {
	NEP_CMD_NETENGINE_FLOWID_UPDATE,
	NEP_CMD_RING_ACTIVE,
	NEP_CMD_RING_BUFFER_POOL_ACTIVE,
	NEP_CMD_MAX,
};

/* Monitor packet type */
enum {
	MONITOR_PACKET_TYPE_NONE,
	MONITOR_PACKET_TYPE_RS,
	MONITOR_PACKET_TYPE_MAX,
};

#define NEP_RING_BUFFER_POOL_DEFAULT_TYPE (0U)

struct noa_simulator {
	struct device dev;
	struct tasklet_struct input_ring_isr;
	struct tasklet_struct output_ring_db;
	struct noa_port ports[NOA_PORT_MAX];
	struct noa_session *session_entries;
	struct nep_tables g_nep_tables;
	uint8_t nep_tether_dscp_to_up_table[NEP_DSCP_TO_UP_TABLE_SIZE];
	int nep_error_counters[BPF_TETHER_ERR__MAX];
	struct noa_nep_stat nep_stat;
	bool dbg;
	bool force_fallback;
	int monitor_packet_type;
	struct utilization_monitor offload_util;
	struct utilization_monitor no_offload_util;
	struct apf_info nep_apf_info;
};

extern bool is_sim_force_fallback(void);
extern int get_monitor_packet_type(void);

/* debugfs used only */
extern struct noa_simulator *noa_sim_get(void);
/* wlan, modem, network used */
extern struct noa_port *noa_sim_get_port(u8 id);
extern int noa_port_irq_get(uint8_t port_id);
extern void noa_sim_trig_tx(void);
extern void noa_sim_trig_rx(void);
extern void noa_sim_interrupt_to_wlansw(int irq);
extern int noa_interrupt_register(int id, irq_handler_t isr, void *priv);
extern void noa_interrupt_unregister(int id, void *priv);
extern int noa_sim_add_session_entry(struct noa_session *entry);
extern int noa_sim_put_ipv4_entry(Tether4Entry *entry, bool is_upstream);
extern int noa_sim_remove_ipv4_entry(Tether4Key *key, bool is_upstream);
extern int noa_sim_put_upstream_ipv6_entry(TetherUpstream6Entry *entry);
extern int noa_sim_remove_upstream_ipv6_entry(TetherUpstream6Key *key);
extern int noa_sim_put_downstream_ipv6_entry(TetherDownstream6Entry *entry);
extern int noa_sim_remove_downstream_ipv6_entry(TetherDownstream6Key *key);
extern void nep_ring_service_request_send(int event, bool active, uint8_t id, uint8_t direction);
extern void
nep_ring_service_request_send_with_callback(int event, bool active, uint8_t id, uint8_t direction,
					    void (*callback)(int32_t status, void *context),
					    void *context);
extern int nep_request_send(u32 cmd, void *msg);
extern int noa_sim_set_config(TetherConfig *config);
extern int noa_sim_remove_config(void);
extern int noa_sim_get_stats(TetherStats *stats);
extern int noa_sim_utilization_calculate(struct utilization_monitor *util_mon);
extern int noa_sim_utilization_clear(struct utilization_monitor *util_mon);
extern void noa_register_neteng_callback(const struct neteng_callback_ops *cb_ops);
#endif /*__NOA_NEP_SIM_H__*/
