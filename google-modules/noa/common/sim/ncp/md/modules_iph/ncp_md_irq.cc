// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * This file implements the Modem APC2NCP Ring interrupt/doorbell interfaces for
 * the NOA Mediatek Modem Driver.
 */

#include "ncp_md_irq.h"

static struct ncp_md_irq_simulator ncp_sim;

NotifierMailbox dpa_notifier;
struct ncp_md_irq_dpa_isr_data dpa_isr_data[NCP_MD_PORT_MAX];

struct ncp_md_irq_simulator *ncp_md_irq_sim_get(void)
{
	return &ncp_sim;
}
#ifdef linux
EXPORT_SYMBOL_GPL(ncp_md_irq_sim_get);
#endif

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

#ifdef linux
static void ncp_md_irq_apc_handler(unsigned long data)
#else
static int ncp_md_irq_apc_handler(void *data)
#endif
{
	struct ncp_md_irq_simulator *sim = (struct ncp_md_irq_simulator *)data;
	struct ncp_md_irq_port *port = NULL;
	int i;

#ifndef linux
	sim->apc_intr_mask = RD_REG(NCP_IRQ_APC_INTR_MASK_ADDR);
	sim->dpmaif_q_mask = RD_REG(NCP_IRQ_DPMAIF_Q_MASK_ADDR);
	sim->noa_q_mask = RD_REG(NCP_IRQ_NOA_Q_MASK_ADDR);
#endif
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
#ifndef linux
	WR_REG(NCP_IRQ_APC_INTR_MASK_ADDR, sim->apc_intr_mask);
	return 0;
#endif
}

// For NCP->APC direction, NCP notify APC to call the APC irq handler
#ifdef linux
void ncp_md_irq_notify_apc(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter");
	tasklet_schedule(&sim->irq_apc_handler);
}
EXPORT_SYMBOL_GPL(ncp_md_irq_notify_apc);
#else
static void ncp_md_irq_write_apc_regs(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO(
		"enter, NCP write regs for APC, apc_intr_mask=[%d], dpmaif_q_mask=[%d], noa_q_mask=[%d]",
		sim->apc_intr_mask, sim->dpmaif_q_mask, sim->noa_q_mask);
	WR_REG(NCP_IRQ_APC_INTR_MASK_ADDR, sim->apc_intr_mask);
	WR_REG(NCP_IRQ_DPMAIF_Q_MASK_ADDR, sim->dpmaif_q_mask);
	WR_REG(NCP_IRQ_NOA_Q_MASK_ADDR, sim->noa_q_mask);
}
void ncp_md_irq_notify_apc(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter, irq_notifier=[%s]", sim->irq_ncp_notifier.GetName());
	ncp_md_irq_write_apc_regs();
	sim->irq_ncp_notifier.Notify();
}
#endif

void ncp_md_irq_set_ncp_intr_mask(u32 ring_type)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	sim->ncp_intr_mask |= 1 << ring_type;
}
#ifdef linux
EXPORT_SYMBOL_GPL(ncp_md_irq_set_ncp_intr_mask);
#endif

// To call the interrupt handler which is registered by NCP
static void ncp_md_irq_ncp_task(unsigned long data)
{
	struct ncp_md_irq_port *port = (struct ncp_md_irq_port *)data;
	NCP_IRQ_INFO("for port=[%d]", port->idx);
	if (port->ncp_isr)
		port->ncp_isr(port->rcv_irq, port->priv);
}

#ifdef linux
static void ncp_md_irq_ncp_handler(unsigned long data)
#else
static int ncp_md_irq_ncp_handler(void *data)
#endif
{
	struct ncp_md_irq_simulator *sim = (struct ncp_md_irq_simulator *)data;
	struct ncp_md_irq_port *port = NULL;
	int i;

#ifndef linux
	sim->ncp_intr_mask = RD_REG(NCP_IRQ_NCP_INTR_MASK_ADDR);
#endif
	NCP_IRQ_INFO("enter, ncp_intr_mask=[%d]", sim->ncp_intr_mask);

	// schedule task depend on ncp_intr_mask
	for (i = 0; i < NCP_MD_PORT_MAX; i++) {
		port = &sim->ports[i];
		if (!(sim->ncp_intr_mask & (1 << port->idx)))
			continue;
		tasklet_schedule(&port->irq_ncp_task);
	}
	sim->ncp_intr_mask = 0;
#ifndef linux
	WR_REG(NCP_IRQ_NCP_INTR_MASK_ADDR, sim->ncp_intr_mask);
	return 0;
#endif
}

// For APC->NCP direction, APC notify NCP to call the NCP irq handler
#ifdef linux
void ncp_md_irq_notify_ncp(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter");
	tasklet_schedule(&sim->irq_ncp_handler);
}
EXPORT_SYMBOL_GPL(ncp_md_irq_notify_ncp);
#else
static void ncp_md_irq_write_ncp_regs(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter, APC write regs for NCP, ncp_intr_mask=[%d]", sim->ncp_intr_mask);
	WR_REG(NCP_IRQ_NCP_INTR_MASK_ADDR, sim->ncp_intr_mask);
}
void ncp_md_irq_notify_ncp(void)
{
	struct ncp_md_irq_simulator *sim = &ncp_sim;
	NCP_IRQ_INFO("enter, irq_notifier=[%s]", sim->irq_apc_notifier.GetName());
	ncp_md_irq_write_ncp_regs();
	sim->irq_apc_notifier.Notify();
}
#endif

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
#ifdef linux
EXPORT_SYMBOL_GPL(ncp_md_irq_register);
#endif

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
#ifdef linux
EXPORT_SYMBOL_GPL(ncp_md_irq_unregister);
#endif

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
#ifdef linux
	if (is_ncp) {
		tasklet_init(&sim->irq_apc_handler, ncp_md_irq_apc_handler, (unsigned long)sim);
	} else {
		tasklet_init(&sim->irq_ncp_handler, ncp_md_irq_ncp_handler, (unsigned long)sim);
	}
#else
	MailboxClientId client_id = (MailboxClientId)MODEM_MAILBOX_CLIENT_ID;
	int ret = 0;
	NCP_IRQ_INFO("%s mailbox notifier client_id=[%d]", name, client_id);
	if (is_ncp) {
		ret = sim->irq_ncp_notifier.Init(name, kSender | kReceiver, client_id);
		if (ret) {
			NCP_IRQ_INFO("Failed to initialize %s notifier, ret=[%d]", name, ret);
			return ret;
		}
		ret = sim->irq_ncp_notifier.RegisterNotificationHandler(ncp_md_irq_ncp_handler,
									(void *)sim);
		if (ret) {
			NCP_IRQ_INFO("Failed to set handler for %s notifier, ret=[%d]", name, ret);
			return ret;
		}
	} else {
		ret = sim->irq_apc_notifier.Init(name, kSender | kReceiver, client_id);
		if (ret) {
			NCP_IRQ_INFO("Failed to initialize %s notifier, ret=[%d]", name, ret);
			return ret;
		}
		ret = sim->irq_apc_notifier.RegisterNotificationHandler(ncp_md_irq_apc_handler,
									(void *)sim);
		if (ret) {
			NCP_IRQ_INFO("Failed to set handler for %s notifier, ret=[%d]", name, ret);
			return ret;
		}
	}
#endif
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(ncp_md_irq_init);
#endif

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
#ifdef linux
	if (is_ncp) {
		tasklet_kill(&sim->irq_ncp_handler);
	} else {
		tasklet_kill(&sim->irq_apc_handler);
	}
#else
	if (is_ncp) {
		sim->irq_ncp_notifier.UnregisterNotificationHandler();
	} else {
		sim->irq_apc_notifier.UnregisterNotificationHandler();
	}
#endif
}
#ifdef linux
EXPORT_SYMBOL_GPL(ncp_md_irq_exit);
#endif

void ncp_md_irq_dpa_notify_apc(int id)
{
	NCP_IRQ_DEBUG("enter, notifier=[%s], id=[%d]", dpa_notifier.GetName(), id);
	dpa_notifier.Notify(id);
}

int ncp_md_irq_dpa_init(NotificationHandler isr, void *data)
{
	MailboxClientId client_id = (MailboxClientId)MODEM_DPA_MAILBOX_CLIENT_ID;
	int ret = 0;
	NCP_IRQ_INFO("Initialize ncp mailbox notifier client_id=[%d], doorbell_mask=[%d]",
		     client_id, NCP_MD_PORT_MASK);
	ret = dpa_notifier.Init("ncp_md", kSender | kReceiver, client_id, NCP_MD_PORT_MASK, false);
	if (ret) {
		NCP_IRQ_ERROR("Failed to initialize ncp notifier, ret=[%d]", ret);
		return ret;
	}

	for (int id = 0; id < NCP_MD_PORT_MAX; id++) {
		NCP_IRQ_INFO("Register notification handler for ncp notifier[%d]", id);
		dpa_isr_data[id].data = data;
		dpa_isr_data[id].id = id;
		ret = dpa_notifier.RegisterNotificationHandler(isr, id, &dpa_isr_data[id]);
		if (ret) {
			NCP_IRQ_ERROR("Failed to set handler for ncp notifier[%d], ret=[%d]", id,
				      ret);
			return ret;
		}
	}
	return 0;
}

void ncp_md_irq_dpa_exit()
{
	int ret = dpa_notifier.UnregisterNotificationHandler();
	if (ret) {
		NCP_IRQ_ERROR("Failed to set handler for ncp notifier, ret=[%d]", ret);
	}
	NCP_IRQ_INFO("ncp_md_irq_dpa_exit=[%d]", ret);
}
