// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of ring doorbell notifier component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/ring_doorbell_notifier.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/core.h"
#include "common/compiler.h"
#include "common/inttypes.h"
#include "port.h"
#else /* linux */
#include "ring_pipeline_service/ring_doorbell_notifier.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/compiler.h"
#include "common/core.h"
#include "linux_port/log.h"
#include "noa_configs/noa_configs.h"
#include "notifier/notifier_mailbox.h"
#endif /* linux */

SEC_FAST_DATA static RingServiceDoorbellContext g_doorbell_notifiers[NOA_PORT_MAX];

RingServiceDoorbellContext *RingServiceDoorbellNotifierGet(uint8_t doorbell_id)
{
	if (doorbell_id >= NOA_PORT_MAX) {
		return NULL;
	}
	return &g_doorbell_notifiers[doorbell_id];
}

#ifdef linux
static void DoorbellTask(unsigned long data)
{
	struct noa_port *port = (struct noa_port *)data;

	if (port->isr) {
		port->isr(port->rcv_irq, port->priv);
	}
}
#endif /* linux */

int32_t RingServiceDoorbellNotifierInitAll(void)
{
	int32_t doorbell_id;

	for (doorbell_id = 0; doorbell_id < NOA_PORT_MAX; ++doorbell_id) {
		RingServiceDoorbellContext *notifier = RingServiceDoorbellNotifierGet(doorbell_id);
		if (doorbell_id == NOA_PORT_NETENGINE) {
			continue;
		}
#ifdef linux
		notifier->port = noa_sim_get_port(doorbell_id);
		if (!notifier->port) {
			pr_err("Failed to get port with group id %" PRIu16 "\n", doorbell_id);
			return -EINVAL;
		}
		tasklet_init(&notifier->port->doorbell_task, DoorbellTask,
			     (unsigned long)notifier->port);
#else /* linux */
		int32_t ret;
		using ::noa::module::notifier::kSender;
		using ::noa::module::notifier::NotificationHandler;
		using ::noa::module::notifier::NotifierMailbox;

		auto mailbox_id =
			noa::module::noa_configs::NoaConfigs::Instance().MailboxId(doorbell_id);
		if (!mailbox_id.has_value()) {
			pr_err("Failed to get mailbox with group id %" PRIu16 "\n", doorbell_id);
			return -EINVAL;
		}
		ret = notifier->notifier.Init("ring service", kSender, *mailbox_id);
		if (ret) {
			pr_err("Failed to init notifier on ring service %" PRIu16 ", err: %" PRId32
			       "\n",
			       doorbell_id, ret);
			return ret;
		}
#endif /* linux */
	}
	return 0;
}
