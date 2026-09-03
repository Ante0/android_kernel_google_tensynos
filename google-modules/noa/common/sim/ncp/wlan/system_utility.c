// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WiFi System Utility for Linux kernel
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#include "system_utility.h"

/*
 * temp solution to trigger event to NOA WiFi driver.
 * should be replaced by IPC framework.
 */
static int (*send_event_to_apc)(int event, void *msg);

int ncp_register_event_receiver(struct noa_wlan_fw *fw, void *fun)
{
	send_event_to_apc = (int (*)(int, void *))fun;
	return 0;
}

int ncp_send_event_to_apc(struct noa_wlan_fw *fw, int event, void *msg)
{
	if (send_event_to_apc) {
		return send_event_to_apc(event, msg);
	}
	return -ESRCH;
}

int32_t get_nep_wlan_irq(void)
{
	struct noa_port *port = noa_sim_get_port(NOA_PORT_WLAN_FW);
	return port->irq;
}

void ncp_doorbell_to_apc(u32 irq_id)
{
	noa_sim_interrupt_to_wlansw(irq_id);
}

extern struct noa_wlan_fw wlan_fw;
struct noa_wlan_fw *noa_wlan_get_fw(void)
{
	return &wlan_fw;
}
EXPORT_SYMBOL_GPL(noa_wlan_get_fw);