/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD Driver
 *
 * Copyright 2025 Google LLC.
 */
#ifndef __NCP_MD_IRQ_H__
#define __NCP_MD_IRQ_H__

#include "common/modem_ring_id.h"
#include "common/ring.h"

#ifdef linux
#include <linux/interrupt.h>

#define NCP_IRQ_INFO(fmt, ...) pr_info("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_IRQ_ERROR(fmt, ...) pr_err("%s:" fmt, __func__, ##__VA_ARGS__)
#else
#include "hwio/hwio.h"
#include "linux_port/interrupt.h"
#include "linux_port/tasklet.h"
#include "notifier/notifier_mailbox.h"
#include "pw_log/log.h"

using namespace noa::driver::mailbox;
using namespace noa::module::notifier;

#define MODEM_MAILBOX_CLIENT_ID 1

#define NCP_IRQ_INFO(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_IRQ_ERROR(fmt, ...) PW_LOG_ERROR("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_IRQ_APC_INTR_MASK_ADDR DRAM_FAKE_MODEM_MEMORY_BASE + 0x06B6EFF0
#define NCP_IRQ_NCP_INTR_MASK_ADDR NCP_IRQ_APC_INTR_MASK_ADDR + 4U
#define NCP_IRQ_DPMAIF_Q_MASK_ADDR NCP_IRQ_NCP_INTR_MASK_ADDR + 4U
#define NCP_IRQ_NOA_Q_MASK_ADDR NCP_IRQ_DPMAIF_Q_MASK_ADDR + 4U
#endif

#define NCP_MD_PORT_MAX kNoaModemApcToNcpRingMax

struct ncp_md_irq_port {
	struct tasklet_struct irq_apc_task;
	struct tasklet_struct irq_ncp_task;
	u32 irq;
	u32 idx;
	u32 rcv_irq;
	irq_handler_t apc_isr;
	irq_handler_t ncp_isr;
	void *priv;
};

struct ncp_md_irq_simulator {
	struct ncp_md_irq_port ports[NCP_MD_PORT_MAX];
	struct tasklet_struct irq_apc_handler;
	struct tasklet_struct irq_ncp_handler;
#ifndef linux
	noa::module::notifier::NotifierMailbox irq_apc_notifier;
	noa::module::notifier::NotifierMailbox irq_ncp_notifier;
#endif
	u32 apc_intr_mask;
	u32 ncp_intr_mask;
	u32 dpmaif_q_mask;
	u32 noa_q_mask;
};

struct ncp_md_irq_simulator *ncp_md_irq_sim_get(void);
void ncp_md_irq_set_apc_intr_mask(u32 ring_type);
void ncp_md_irq_notify_apc(void);
void ncp_md_irq_set_ncp_intr_mask(u32 ring_type);
void ncp_md_irq_notify_ncp(void);
int ncp_md_irq_register(int id, irq_handler_t isr, void *priv, bool is_ncp);
void ncp_md_irq_unregister(int id, void *priv, bool is_ncp);
int ncp_md_irq_init(const char *name, bool is_ncp);
void ncp_md_irq_exit(bool is_ncp);

#endif /* __NCP_MD_IRQ_H__ */
