// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for init noa ports
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "port_init.h"
#include "nep.h"
#include <common/noatrace.h>
#include <common/ring_id.h>
#include "ring_controller.h"
#else /* linux */
#include "common/core.h"
#include "common/ring_id.h"
#include "device_mgmt/manager.h"
#include "dma/dma.h"
#include "noa_configs/noa_configs.h"
#include "notifier/notifier.h"
#include "dma_processor.h"
#include "ring_mgmt/port.h"
#include "ring_mgmt/port_instance.h"
#include "ring_mgmt/ring_controller.h"
#endif /* linux */

/* DMA scheduler: RX NOA input ring, fifo */
static void noa_input_task(unsigned long data)
{
	struct noa_port *port = (struct noa_port *)data;
	NoaRingServiceHandleIsr(port);
}

/* Send a doorbell to trigger a destination interrupt. */
void noa_doorbell_task(unsigned long data)
{
	struct noa_port *port = (struct noa_port *)data;

	if (port->isr)
		port->isr(port->rcv_irq, port->priv);
}

#ifdef linux
#else /* linux */
using ::noa::driver::mailbox::MailboxClientId;
using ::noa::module::notifier::kReceiver;
using ::noa::module::notifier::kSender;
using ::noa::module::notifier::NotificationHandler;
using ::noa::module::notifier::NotifierMailbox;

static int32_t IsrForRingData(void *context)
{
	struct noa_port *port = (struct noa_port *)context;
	if (!port) {
		return -EINVAL;
	}
	tasklet_schedule(&port->input_task);
	return 0;
}

static std::unique_ptr<NotifierMailbox> AllocateNotifier(struct noa_port *port, MailboxClientId id)
{
	std::unique_ptr<NotifierMailbox> notifier(new (std::nothrow) NotifierMailbox());
	if (!notifier) {
		return nullptr;
	}
	int ret = notifier->Init(port->name, kSender | kReceiver, id);
	if (ret) {
		PW_LOG_WARN("Failed to init %s ring handler, err: %d", port->name, ret);
		return nullptr;
	}
	ret = notifier->RegisterNotificationHandler(IsrForRingData, port);
	if (ret) {
		PW_LOG_WARN("Failed to register handler in %s ring handler, err: %d", port->name,
			    ret);
		return nullptr;
	}
	return notifier;
}
#endif /* linux */

static void MapToNetworkFlowForIsr(const uint8_t port_id, uint8_t *interface, uint8_t *flow)
{
	switch (port_id) {
	case NOA_PORT_WLAN_SW:
		*interface = kNoaNetworkInterfaceWlan;
		*flow = kNoaNetworkFlowHostToDevice;
		break;
	case NOA_PORT_WLAN_FW:
		*interface = kNoaNetworkInterfaceWlan;
		*flow = kNoaNetworkFlowDeviceToHost;
		break;
	case NOA_PORT_MODEM_SW:
		*interface = kNoaNetworkInterfaceModem;
		*flow = kNoaNetworkFlowHostToDevice;
		break;
	case NOA_PORT_MODEM_FW:
		*interface = kNoaNetworkInterfaceModem;
		*flow = kNoaNetworkFlowDeviceToHost;
		break;
	case NOA_PORT_NETENGINE:
		*interface = kNoaNetworkInterfaceNetengine;
		*flow = kNoaNetengineTunnel;
		break;
	default:
		break;
	}
}

void noa_ports_init(void)
{
	int i;
	for (i = 0; i < NOA_PORT_MAX; i++) {
		struct noa_port *port = noa_sim_get_port(i);
		const char *name = noa_port_id_to_name((uint8_t)i);
		if (!port) {
			continue;
		}
		strncpy(port->name, name, strlen(name));
		port->idx = i;
		port->irq = noa_port_irq_get((uint8_t)i);
		port->intm = 0;
		port->ints = 0;
		port->doorbell = 0;
		port->isr = NULL;
		port->priv = NULL;
	}
}

void noa_ports_enable_ring_service(void)
{
	int i;
	for (i = 0; i < NOA_PORT_MAX; i++) {
		struct noa_port *port = noa_sim_get_port(i);
		if (!port) {
			continue;
		}
		MapToNetworkFlowForIsr(i, &port->interface_of_rings, &port->flow_of_rings);
		port->rings_bitmap = 0;
		tasklet_init(&port->input_task, noa_input_task, (unsigned long)port);
#ifdef linux
		tasklet_init(&port->doorbell_task, noa_doorbell_task, (unsigned long)port);
#else /* linux */
		// TODO(b/350615515) - Refactor this section to enforce consistent behavior across
		// NetEngine components by introducing an abstract layer for shared notification classes.
		if (i == NOA_PORT_NETENGINE) {
			tasklet_init(&port->doorbell_task, noa_doorbell_task, (unsigned long)port);
		}
		auto mailbox_id = noa::module::noa_configs::NoaConfigs::Instance().MailboxId(i);
		if (mailbox_id.has_value()) {
			port->notifier = AllocateNotifier(port, *mailbox_id);
			if (!port->notifier) {
				PW_LOG_WARN("Failed to allocate notifier on port %d", i);
			}
		}
#endif /* linux */
	}
	pr_info("Ring Service: Enable legacy mode\n");
}

void noa_ports_free(void)
{
	int i;

	for (i = 0; i < NOA_PORT_MAX; i++) {
		struct noa_port *port = noa_sim_get_port(i);
		tasklet_kill(&port->input_task);
#ifdef linux
		tasklet_kill(&port->doorbell_task);
#endif /* linux */
	}
}
