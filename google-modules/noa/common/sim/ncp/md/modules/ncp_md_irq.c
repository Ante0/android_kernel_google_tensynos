// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * This file implements the Modem APC2NCP Ring interrupt/doorbell interfaces for
 * the NOA Mediatek Modem Driver.
 */

#include "ncp_md_irq.h"

static struct ncp_md_irq_simulator ncp_sim;

struct ncp_md_irq_simulator *ncp_md_irq_sim_get(void)
{
	return &ncp_sim;
}
EXPORT_SYMBOL_GPL(ncp_md_irq_sim_get);

static void ncp_md_irq_update_q_mask(u32 ring_type)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	u32 mask_base = 0;

	NCP_IRQ_INFO("enter");

	sim->noa_q_mask = 0;

	// for 5 NOA Tx DRB Rings
	if (kNoaModemRingTxDrb0 <= ring_type && ring_type <= kNoaModemRingTxDrb4) {
		mask_base = 1 << kNoaModemRingTxDrb0;
	}

	// 4 NOA Rx Refill Rings
	if (kNoaModemRingRxRefillNormalBat0 <= ring_type &&
		ring_type <= kNoaModemRingRxRefillFragBat1) {
		mask_base = 1 << kNoaModemRingRxRefillNormalBat0;
	}

	sim->noa_q_mask = sim->dpmaif_q_mask * mask_base;
	NCP_IRQ_INFO("dpmaif_q_mask=[%d], noa_q_mask=[%d]", sim->dpmaif_q_mask, sim->noa_q_mask);
}

void ncp_md_irq_set_apc_intr_mask(u32 ring_type)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	sim->apc_intr_mask |= 1 << ring_type;
	ncp_md_irq_update_q_mask(ring_type);
}

// To call the interrupt handler which is registered by APC
static void ncp_md_irq_apc_task(unsigned long data)
{
	struct ncp_md_irq_port *port = (struct ncp_md_irq_port *)data;

	NCP_IRQ_INFO("for port=[%d]", port->idx);
	if (port->apc_isr)
		port->apc_isr(port->rcv_irq, port->priv);
}

static void ncp_md_irq_apc_handler(unsigned long data)
{
	struct ncp_md_irq_simulator *sim = (struct ncp_md_irq_simulator *)data;
	struct ncp_md_irq_port *port = NULL;
	int i;

	NCP_IRQ_INFO("enter, apc_intr_mask=[%d], dpmaif_q_mask=[%d], noa_q_mask=[%d]",
		     sim->apc_intr_mask, sim->dpmaif_q_mask, sim->noa_q_mask);

	// schedule task depend on apc_intr_mask
	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		port = &sim->ports[i];
		if (!(sim->apc_intr_mask & (1 << port->idx)))
			continue;
		tasklet_schedule(&port->irq_apc_task);
	}
	sim->apc_intr_mask = 0;
}

// For NCP->APC direction, NCP notify APC to call the APC irq handler
void ncp_md_irq_notify_apc(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter");
	tasklet_schedule(&sim->irq_apc_handler);
}
EXPORT_SYMBOL_GPL(ncp_md_irq_notify_apc);

void ncp_md_irq_set_ncp_intr_mask(u32 ring_type)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	sim->ncp_intr_mask |= 1 << ring_type;
}
EXPORT_SYMBOL_GPL(ncp_md_irq_set_ncp_intr_mask);

// To call the interrupt handler which is registered by NCP
static void ncp_md_irq_ncp_task(unsigned long data)
{
	struct ncp_md_irq_port *port = (struct ncp_md_irq_port *)data;
	NCP_IRQ_INFO("for port=[%d]", port->idx);
	if (port->ncp_isr)
		port->ncp_isr(port->rcv_irq, port->priv);
}

static void ncp_md_irq_ncp_handler(unsigned long data)
{
	struct ncp_md_irq_simulator *sim = (struct ncp_md_irq_simulator *)data;
	struct ncp_md_irq_port *port = NULL;
	int i;

	NCP_IRQ_INFO("enter, ncp_intr_mask=[%d]", sim->ncp_intr_mask);

	// schedule task depend on ncp_intr_mask
	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		port = &sim->ports[i];
		if (!(sim->ncp_intr_mask & (1 << port->idx)))
			continue;
		tasklet_schedule(&port->irq_ncp_task);
	}
	sim->ncp_intr_mask = 0;
}

// For APC->NCP direction, APC notify NCP to call the NCP irq handler
void ncp_md_irq_notify_ncp(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter");
	tasklet_schedule(&sim->irq_ncp_handler);
}
EXPORT_SYMBOL_GPL(ncp_md_irq_notify_ncp);

/*
 * Register an interrupt handler.
 *
 * @id: The interrupt id.
 * @isr: The interrupt handler.
 * @priv: The private data passed to the interrupt handler.
 * @is_ncp: If true, the interrupt is for the NCP, otherwise it is for the APC.
 */
int ncp_md_irq_register(int id, irq_handler_t isr, void *priv, bool is_ncp)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	struct ncp_md_irq_port *port = NULL;
	int i;

	NCP_IRQ_INFO("id=[%d], is_ncp=[%d]", id, is_ncp);
	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		if (sim->ports[i].irq == id) {
			port = &sim->ports[i];
			break;
		}
	}

	if (!port) {
		NCP_IRQ_ERROR("id %d is not found!", id);
		return -ENODEV;
	}
	if (is_ncp) {
		port->ncp_isr = isr;
	} else {
		port->apc_isr = isr;
	}
	port->priv = priv;
	return 0;
}
EXPORT_SYMBOL_GPL(ncp_md_irq_register);

void ncp_md_irq_unregister(int id, void *priv, bool is_ncp)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	struct ncp_md_irq_port *port = NULL;
	int i;

	NCP_IRQ_INFO("enter, id=[%d], is_ncp=[%d]", id, is_ncp);
	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		if (sim->ports[i].irq == id) {
			port = &sim->ports[i];
			break;
		}
	}

	if (!port || port->priv != priv) {
		NCP_IRQ_ERROR("id %d is not found!", id);
		return;
	}
	if (is_ncp) {
		port->ncp_isr = NULL;
	} else {
		port->apc_isr = NULL;
	}
	port->priv = NULL;
}
EXPORT_SYMBOL_GPL(ncp_md_irq_unregister);

int ncp_md_irq_init(const char *name, bool is_ncp)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	struct ncp_md_irq_port *port = NULL;
	int i;

	NCP_IRQ_INFO("enter, name=[%s], is_ncp=[%d]", name, is_ncp);

	sim->apc_intr_mask = 0;
	sim->ncp_intr_mask = 0;
	sim->dpmaif_q_mask = 0;
	sim->noa_q_mask = 0;

	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		port = &ncp_sim.ports[i];
		if (!port) {
			continue;
		}
		port->idx = i;
		port->irq = i;
		port->rcv_irq = i;
		port->priv = NULL;
		if (is_ncp) {
			port->ncp_isr = NULL;
			tasklet_init(&port->irq_ncp_task, ncp_md_irq_ncp_task, (unsigned long)port);
		} else {
			port->apc_isr = NULL;
			tasklet_init(&port->irq_apc_task, ncp_md_irq_apc_task, (unsigned long)port);
		}
	}

	if (is_ncp) {
		tasklet_init(&sim->irq_apc_handler, ncp_md_irq_apc_handler, (unsigned long)sim);
	} else {
		tasklet_init(&sim->irq_ncp_handler, ncp_md_irq_ncp_handler, (unsigned long)sim);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ncp_md_irq_init);

void ncp_md_irq_exit(bool is_ncp)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	struct ncp_md_irq_port *port = NULL;
	int i;

	NCP_IRQ_INFO("enter, is_ncp=[%d]", is_ncp);
	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		port = &ncp_sim.ports[i];
		if (is_ncp) {
			tasklet_kill(&port->irq_ncp_task);
		} else {
			tasklet_kill(&port->irq_apc_task);
		}
	}

	if (is_ncp) {
		tasklet_kill(&sim->irq_ncp_handler);
	} else {
		tasklet_kill(&sim->irq_apc_handler);
	}
}
EXPORT_SYMBOL_GPL(ncp_md_irq_exit);
