// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of NEP Pipeline Task Scheduler
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/list.h>

#include "task_scheduler.h"
#include "engine.h"
#include "packet_initiator.h"
#include "stage.h"
#else /* linux */
#include "network_pipeline_framework/task_scheduler.h"

#include <cerrno>
#include <cstdint>

#include "linux_port/log.h"
#include "linux_port/list.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_initiator.h"
#include "network_pipeline_framework/stage.h"
#endif /* linux */

static inline bool HasTodoTask(struct NepTaskScheduler *scheduler)
{
	return !list_empty(&scheduler->stage_tasks) || !list_empty(&scheduler->engine_tasks) ||
	       !list_empty(&scheduler->pkt_initiator_tasks) ||
	       !list_empty(&scheduler->runnable_tasks);
}

static inline bool ShouldStop(const struct NepTaskScheduler *scheduler)
{
	return scheduler->should_stop
#ifdef linux
	       || kthread_should_stop()
#endif /* linux */
		;
}

static inline void WaitTask(struct NepTaskScheduler *scheduler)
{
#ifdef linux
	wait_event_interruptible(scheduler->wait, ShouldStop(scheduler) || HasTodoTask(scheduler));
#else /* linux */
	while (!HasTodoTask(scheduler) && !ShouldStop(scheduler)) {
		scheduler->wait.acquire();
	}
#endif /* linux */
	return;
}

int32_t NepTaskSchedulerRunner(void *context)
{
	int32_t ret = 0;
	struct NepTaskScheduler *scheduler = (struct NepTaskScheduler *)context;

	if (!context) {
		pr_err("Invalid arguments on scheduler runner\n");
		return -EINVAL;
	}

	while (!ShouldStop(scheduler)) {
		unsigned long flags = 0;
		struct NepPacketInitiator *initiator;
		struct NepPacketInitiator *next;
		struct NepRunnableTask *task;
		struct NepRunnableTask *next_task;
		struct list_head retry_engine_list;
		INIT_LIST_HEAD(&retry_engine_list);

		WaitTask(scheduler);

		spin_lock_irqsave(&scheduler->runnable_tasks_lock, flags);
		list_for_each_entry_safe (task, next_task, &scheduler->runnable_tasks, list) {
			list_del_init(&task->list);
			spin_unlock_irqrestore(&scheduler->runnable_tasks_lock, flags);
			task->func(task, task->data);
			spin_lock_irqsave(&scheduler->runnable_tasks_lock, flags);
		}
		spin_unlock_irqrestore(&scheduler->runnable_tasks_lock, flags);

		while (!list_empty(&scheduler->engine_tasks) ||
		       !list_empty(&scheduler->stage_tasks)) {
			while (!list_empty(&scheduler->engine_tasks)) {
				struct NepEngine *engine = list_first_entry(
					&scheduler->engine_tasks, struct NepEngine, waiting_hook);
				ret = engine->func(engine);
				if (ret == -EAGAIN) {
					list_move_tail(&engine->waiting_hook, &retry_engine_list);
				} else if (ret) {
					pr_err("Failed to process the engine, err %" PRId32 "\n",
					       ret);
					list_del_init(&engine->waiting_hook);
				}
			}

			while (!list_empty(&scheduler->stage_tasks)) {
				struct NepStage *stage = list_first_entry(
					&scheduler->stage_tasks, struct NepStage, waiting_hook);
				ret = NepStageRouteCompletedPackets(stage);
				if (ret) {
					pr_err("Failed to process the completed stage, err %" PRId32
					       "\n",
					       ret);
					list_del_init(&stage->waiting_hook);
				}
			}
		}

		list_for_each_entry_safe (initiator, next, &scheduler->pkt_initiator_tasks,
					  waiting_hook) {
			if (NepPacketTableIsEmpty(scheduler->pkt_table)) {
				break;
			}
			ret = NepPacketIntitatorProcessing(initiator);
			if (ret) {
				if (ret == -EAGAIN) {
					break;
				}
				pr_err("Failed to process the packet initiator, err %" PRId32 "\n",
				       ret);
				list_del_init(&initiator->waiting_hook);
			}
		}

		list_splice_tail(&retry_engine_list, &scheduler->engine_tasks);
	}
	ret = 0;

	return ret;
}

void NepTaskSchedulerReset(struct NepTaskScheduler *scheduler)
{
	struct NepStage *stage;
	struct NepEngine *engine;
	struct NepPacketInitiator *initiator;

	if (!scheduler) {
		return;
	}

	list_for_each_entry (stage, &scheduler->all_stages, tracking_hook) {
		NepStageReset(stage);
	}

	list_for_each_entry (engine, &scheduler->all_engines, tracking_hook) {
		NepEngineReset(engine);
	}

	list_for_each_entry (initiator, &scheduler->all_pkt_initiators, tracking_hook) {
		NepPacketInitiatorReset(initiator);
	}
	NepPacketTableReset(scheduler->pkt_table);

	if (!list_empty(&scheduler->stage_tasks)) {
		pr_err("Some stage tasks remain incomplete. Something went wrong!\n");
		list_del_init(&scheduler->stage_tasks);
	}

	if (!list_empty(&scheduler->engine_tasks)) {
		pr_err("Some engine tasks remain incomplete. Something went wrong!\n");
		list_del_init(&scheduler->engine_tasks);
	}

	if (!list_empty(&scheduler->pkt_initiator_tasks)) {
		pr_err("Some pkt initiator tasks remain incomplete. Something went wrong!\n");
		list_del_init(&scheduler->pkt_initiator_tasks);
	}
}
