// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#define pr_fmt(fmt) "pixel_trim: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <pixel_trim.h>
#include "pixel_trim_core.h"
#include "pixel_trim_sysfs.h"

static cpumask_t trim_task_cpu_affinity;
static struct task_struct *trim_task;
static atomic_t trim_pending = ATOMIC_INIT(0);

static LIST_HEAD(trim_list);
static DEFINE_MUTEX(trim_lock);
static DECLARE_WAIT_QUEUE_HEAD(trim_wait);

/**
 * register_trim - Registers a pixel trimming operation.
 * @info: Pointer to a 'struct pixel_trim' containing the trimming
 * information and callback.
 *
 * This function adds a 'pixel_trim' structure to a global list of
 * registered trimming operations.
 *
 * Return: 0 on success.
 */
int register_trim(struct pixel_trim *info)
{
	mutex_lock(&trim_lock);
	list_add(&info->list, &trim_list);
	mutex_unlock(&trim_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(register_trim);

/**
 * unregister_trim - Unregisters a pixel trimming operation.
 * @info: Pointer to the 'struct pixel_trim' to be unregistered.
 *
 * This function removes a previously registered 'pixel_trim' structure
 * from the global list.
 */
void unregister_trim(struct pixel_trim *info)
{
	mutex_lock(&trim_lock);
	list_del(&info->list);
	mutex_unlock(&trim_lock);
}
EXPORT_SYMBOL_GPL(unregister_trim);

/**
 * execute_trim - Executes all registered pixel trimming operations.
 *
 * This function iterates through all 'pixel_trim' structures currently
 * registered in the global list and calls their respective 'trim' callback
 * functions.
 * This function represents a best-effort attempt to execute
 * the registered trim operations. There is no guarantee that each individual
 * trim operation will complete successfully or achieve its intended result.
 * Success or failure of the trimming process depends on the implementation
 * of each registered 'trim' callback function.
 */
static void execute_trim(void)
{
	struct pixel_trim *trim;

	mutex_lock(&trim_lock);
	list_for_each_entry(trim, &trim_list, list)
		trim->trim(trim->private);
	mutex_unlock(&trim_lock);
}

void run_trim(void)
{
	atomic_inc(&trim_pending);
	wake_up(&trim_wait);
}

void trim_set_cpu_affinity(const struct cpumask *new_mask)
{
	cpumask_and(&trim_task_cpu_affinity, new_mask, cpu_possible_mask);

	if (set_cpus_allowed_ptr(trim_task, &trim_task_cpu_affinity))
		pr_err("Failed to change cpu affinity to %*pbl\n",
		       cpumask_pr_args(&trim_task_cpu_affinity));
}

const struct cpumask *trim_get_cpu_affinity(void)
{
	return &trim_task_cpu_affinity;
}

static int pixel_trim_thread(void *data)
{
	while (!kthread_should_stop()) {
		wait_event_idle(trim_wait, atomic_read(&trim_pending) > 0 ||
					   kthread_should_stop());
		if (kthread_should_stop())
			break;

		execute_trim();
		atomic_dec(&trim_pending);
	}
	return 0;
}

static int __init pixel_trim_init(void)
{
	int err;
	struct task_struct *task;

	cpumask_setall(&trim_task_cpu_affinity);
	task = kthread_run(pixel_trim_thread, NULL, "pixel_trim");
	if (IS_ERR(task)) {
		pr_err("Failed to create task\n");
		err = PTR_ERR(task);
		goto out;
	}
	trim_task = task;
	err = pixel_trim_sysfs_init();
	if (err)
		kthread_stop(trim_task);
out:
	return err;
}
module_init(pixel_trim_init);

MODULE_LICENSE("GPL");
