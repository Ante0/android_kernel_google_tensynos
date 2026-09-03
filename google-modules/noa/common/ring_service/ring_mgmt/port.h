/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NOA port structure
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_SIM_NEP_PORT_H__
#define __NOA_SIM_NEP_PORT_H__

#ifdef linux
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <common/core.h>
#include <common/ring.h>
#else /* linux */
#include <memory>

#include "common/core.h"
#include "common/ring.h"
#include "linux_port/interrupt.h"
#include "linux_port/tasklet.h"
#include "notifier/notifier_mailbox.h"
#endif /* linux */

typedef enum ring_type {
	INPUT,
	OUTPUT,
	RING_TYPE_MAX,
} RingType;

/* real address for port configuration */
struct noa_port {
	u32 irq;
	u32 idx;
	u32 rcv_irq;
	irq_handler_t isr;
	/* Config Register */
	u32 ints;
	u32 intm;
	u32 doorbell;
	uint8_t interface_of_rings;
	uint8_t flow_of_rings;
	uint32_t rings_bitmap;
	struct tasklet_struct input_task;
	// TODO(b/350615515) - Remove `doorbell_task` after refactoring.
	struct tasklet_struct doorbell_task;
#ifndef linux
	std::unique_ptr<noa::module::notifier::NotifierMailbox> notifier;
#endif /* linux */
	char name[MAX_NAME_SIZE];
	void *priv;
};
#endif /* __NOA_SIM_NEP_PORT_H__ */
