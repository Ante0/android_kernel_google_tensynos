// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of ring ISR handler component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/ring_isr_handler.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/core.h"
#include "common/compiler.h"
#include "common/inttypes.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#include "nep.h"
#include "port.h"
#else /* linux */
#include "ring_pipeline_service/ring_isr_handler.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/core.h"
#include "common/compiler.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#include "noa_configs/noa_configs.h"
#include "notifier/notifier_mailbox.h"
#endif /* linux */

int32_t RingServiceIsrHandler(void *context)
{
	uint8_t i;
	RingServiceIsrContext *handler = (RingServiceIsrContext *)context;
	if (!context) {
		return -EINVAL;
	}

	for (i = 0; i < MAX_NUM_OF_TASK_PER_RING_SERVICE_ISR; i++) {
		if (!(handler->task_bitmask & (1U << i))) {
			continue;
		}
		NestedRingTaskQueueToScheduler(handler->tasks[i]);
	}

	NepBitmapTaskSchedulerSignal(handler->scheduler);
	return 0;
}

#ifdef linux
static void RingServiceIsrHandlerWrapper(unsigned long context)
{
	RingServiceIsrHandler((void *)context);
}

#endif /* linux */

SEC_FAST_DATA static RingServiceIsrContext g_isr_handlers[NOA_PORT_MAX];

static inline RingServiceIsrContext *GetIsrHandler(uint8_t interface_id)
{
	if (interface_id >= NOA_PORT_MAX) {
		return NULL;
	}
	return &g_isr_handlers[interface_id];
}

void RingServiceIsrHandlerInitAll(NepBitmapTaskScheduler *scheduler)
{
	int32_t interface_id;

	for (interface_id = 0; interface_id < NOA_PORT_MAX; ++interface_id) {
		uint8_t i;
		RingServiceIsrContext *handler = GetIsrHandler(interface_id);
		handler->task_bitmask = 0;
		handler->scheduler = scheduler;
		for (i = 0; i < MAX_NUM_OF_TASK_PER_RING_SERVICE_ISR; ++i) {
			handler->tasks[i] = NULL;
		}
		handler->name = noa_port_id_to_name(interface_id);
	}
}

int32_t RingServiceIsrHandlerEnableAll(void)
{
	int32_t interface_id;

	for (interface_id = 0; interface_id < NOA_PORT_MAX; ++interface_id) {
#ifdef linux
		struct noa_port *port;
		RingServiceIsrContext *handler;

		port = noa_sim_get_port(interface_id);
		if (!port) {
			pr_err("Failed to get port with group id %" PRIu16 "\n", interface_id);
			return -EINVAL;
		}
		handler = GetIsrHandler(interface_id);
		tasklet_init(&port->input_task, RingServiceIsrHandlerWrapper,
			     (unsigned long)handler);
#else /* linux */
		using ::noa::module::notifier::kReceiver;
		using ::noa::module::notifier::NotificationHandler;
		using ::noa::module::notifier::NotifierMailbox;
		int32_t ret;
		RingServiceIsrContext *handler = GetIsrHandler(interface_id);

		if (interface_id == NOA_PORT_NETENGINE) {
			continue;
		}

		auto mailbox_id =
			noa::module::noa_configs::NoaConfigs::Instance().MailboxId(interface_id);
		if (!mailbox_id.has_value()) {
			pr_err("Failed to get mailbox with group id %" PRIu16 "\n", interface_id);
			return -EINVAL;
		}

		ret = handler->notifier.Init("ring service", kReceiver, *mailbox_id);
		if (ret) {
			pr_err("Failed to init notifier on ring service %" PRIu16 ", err: %" PRId32
			       "\n",
			       interface_id, ret);
			return ret;
		}
		ret = handler->notifier.RegisterNotificationHandler(RingServiceIsrHandler, handler);
		if (ret) {
			pr_err("Failed to register notifier on ring service %" PRIu16
			       ", err: %" PRId32 "\n",
			       interface_id, ret);
			return ret;
		}
#endif /* linux */
	}
	return 0;
}

int32_t RingServiceRegisterIsr(uint8_t interface_id, uint8_t ring_id, NestedRingTask *task)
{
	uint32_t bit = 1U << ring_id;
	RingServiceIsrContext *handler = GetIsrHandler(interface_id);

	if (!handler) {
		pr_err("Failed to register ring service ISR, invalid ISR id %" PRIu16 "\n",
		       interface_id);
		return -EINVAL;
	} else if (ring_id >= MAX_NUM_OF_TASK_PER_RING_SERVICE_ISR) {
		pr_err("Failed to register ring service ISR, invalid ring id %" PRIu16 "\n",
		       ring_id);
		return -EINVAL;

	} else if (handler->task_bitmask & bit) {
		pr_err("Failed to register ring service ISR, ring id %" PRIu16 " is registered\n",
		       ring_id);
		return -EINVAL;
	}
	handler->task_bitmask |= bit;
	handler->tasks[ring_id] = task;
	return 0;
}
