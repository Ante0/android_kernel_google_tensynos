#include "sys_if_mailbox.h"

#include <linux/interrupt.h>
#include "wlan_log/wlan_log.h"
#include "wlan_cast.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "sys_if/io/sys_if_io.h"

typedef struct IsrContext {
	struct noa_port *port;
	MailboxHandler handler;
	void *context;
} IsrContext;

static IsrContext g_nep_wifi_isr_ctx;

static irqreturn_t MailboxIsr(int32_t irq, void *ctx)
{
	IsrContext *isr_ctx = WLAN_REINTERPRET_CAST(IsrContext *, ctx);
	struct noa_port *port = isr_ctx->port;
	void *ints = NULL;

	if (port && isr_ctx) {
		ints = WLAN_REINTERPRET_CAST(void *, &port->ints);
		if (ints) {
			SysIfIoWritel(0, ints);
		}

		if (isr_ctx && isr_ctx->handler) {
			isr_ctx->handler(irq, isr_ctx->context);
		}
	}

	return IRQ_HANDLED;
}

int32_t SysIfRegisterMailbox(NcpWifiMailboxType type, uint32_t doorbell_num, MailboxHandler handler,
			     void *ctx)
{
	struct noa_port *port;

	if (type == kNcpWifiMailboxTypeNep) {
		port = noa_sim_get_port(NOA_PORT_WLAN_FW);

		if (port) {
			memset(&g_nep_wifi_isr_ctx, 0, sizeof(g_nep_wifi_isr_ctx));
			g_nep_wifi_isr_ctx.port = port;
			g_nep_wifi_isr_ctx.handler = handler;
			g_nep_wifi_isr_ctx.context = ctx;

			return noa_interrupt_register(port->irq, MailboxIsr, &g_nep_wifi_isr_ctx);
		}
	} else if (type == kNcpWifiMailboxTypeAp || type == kNcpWifiMailboxTypeNepToAp) {
		// There is no APC-to-NCP mailbox.
		return 0;
	}

	return -ENODEV;
}

void SysIfUnregisterMailbox(NcpWifiMailboxType type, uint32_t doorbell_num)
{
	struct noa_port *port;

	if (type == kNcpWifiMailboxTypeNep) {
		port = noa_sim_get_port(NOA_PORT_WLAN_FW);
		if (port) {
			noa_interrupt_unregister(port->irq, &g_nep_wifi_isr_ctx);
		}
	}
}

void SysIfNotifyMailbox(NcpWifiMailboxType type, uint32_t doorbell_num)
{
	struct noa_port *port;

	if (type == kNcpWifiMailboxTypeNep) {
		port = noa_sim_get_port(NOA_PORT_WLAN_FW);
		if (port) {
			port->doorbell = 1;
			noa_sim_trig_rx();
		}
	} else if (type == kNcpWifiMailboxTypeAp || type == kNcpWifiMailboxTypeNepToAp) {
		port = noa_sim_get_port(NOA_PORT_WLAN_SW);
		if (port) {
			port->ints |= 1;
			port->rcv_irq = 1;
			noa_sim_trig_tx();
		}
	} else {
		printk("%s(): failed to notify mailbox %d", __func__, type);
	}
}
