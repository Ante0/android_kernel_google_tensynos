// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation for Nested Ring Task
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#include "network_pipeline_framework/nested_ring_task.h"

void NestedRingTaskSetup(NestedRingTask *task, void *context, int32_t (*run)(void *context))
{
	task->context = context;
	task->run = run;
	task->should_defer = NULL;
}
