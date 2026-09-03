// SPDX-License-Identifier: GPL-2.0-only
/*
 * Util module of the ring operation.
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */

#ifdef linux
#include "util/ring_util.h"
#include <common/noa_hw_ring.h>
#include "nep.h"
#else
#include "ring_util.h"
#include "ring_controller.h"
#include "ring_mgmt/port.h"
#include "ring_mgmt/port_instance.h"
#endif

uint16_t get_available_slot(struct noa_ring *ring)
{
	uint16_t r = ring->read;
	uint16_t w = ring->write;
	uint16_t len = ring->ctrl;
	return (w < r) ? (r - w - 1) : (len - w - 1 + r);
}

uint16_t get_unhandled_desc(struct noa_ring *ring)
{
	uint16_t r = ring->read;
	uint16_t w = ring->write;
	uint16_t len = ring->ctrl;
	return (w < r) ? (len - r + w) : (w - r);
}

void notify_ring_manager(unsigned long src_port_id)
{
	struct noa_port *port = noa_sim_get_port(src_port_id);
#ifdef linux
	port->doorbell = 1;
	noa_sim_trig_rx();
#else /* linux */
	tasklet_schedule(&port->input_task);
#endif /* linux */
}

void notify_port(uint8_t dst_port_id)
{
#ifdef linux
	struct noa_port *port = noa_sim_get_port(dst_port_id);
	port->ints |= 1;
	noa_sim_trig_tx();
#endif
}
