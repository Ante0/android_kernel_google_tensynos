/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NCP System utility
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#ifndef __NCP_WLAN_SYSTEM_UTILITY_H__
#define __NCP_WLAN_SYSTEM_UTILITY_H__

#ifdef linux
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/io.h>
#include "ncp_wlan_fw.h"

extern int ncp_register_event_receiver(struct noa_wlan_fw *fw, void *fun);
extern int ncp_send_event_to_apc(struct noa_wlan_fw *fw, int event, void *msg);
extern int32_t get_nep_wlan_irq(void);
extern struct noa_wlan_fw *noa_wlan_get_fw(void);
extern void ncp_doorbell_to_apc(u32 irq_id);

#else

static inline int noa_interrupt_register(int32_t irq, irq_handler_t handler, void *ctx)
{
	return request_irq(irq, handler, IRQF_SHARED, "noa_irq", ctx);
}

static inline struct noa_port *noa_sim_get_port(u8 id)
{
	// FIXME: this part is not yet implemented.
	return NULL;
}

static inline void noa_sim_trig_rx(void)
{

}

#define EXPORT_SYMBOL_GPL(func) do { } while (0)
#endif
#endif /* __NCP_WLAN_SYSTEM_UTILITY_H__ */