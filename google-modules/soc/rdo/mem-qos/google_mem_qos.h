/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_MEM_QOS_H
#define _GOOGLE_MEM_QOS_H

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/debugfs.h>

#include <dt-bindings/mem-qos/google,mem-qos.h>

#include "google_qos_box.h"

struct google_mem_qos_desc {
	/* protect qos_box_list */
	struct mutex mutex;
	/* list of qos_box_dev */
	struct list_head qos_box_list;
	/* scenario refcount */
	u32 scen_usage_cnt[NUM_MEM_QOS_SCENARIO];
	/* active scenario */
	u32 active_scenario;
	/* debugfs entry */
	struct dentry *qos_box_base_dir;
	struct dentry *base_dir;
};

/**
 * google_qos_box_dev_register() - register a qos_box_dev instance to mem_qos framework
 * @qos_box_dev: a qos_box_dev instance to register
 *
 * Register a qos_box_dev instance to mem_qos framework. When active scenario changed,
 * the framework will call select_config() callback to switch qos_box settings for all
 * registered qos_box_dev instances.
 *
 * Return: 0 on successful or -EINVAL if qos_box_dev parameter is invalid or qos_box_dev is
 * already registered.
 */
int google_qos_box_dev_register(struct qos_box_dev *qos_box_dev);

/**
 * google_qos_box_dev_unregister() - unregister a qos_box_dev instance from mem_qos framework
 * @qos_box_dev: a qos_box_dev instance to unregister
 *
 * Return: 0 on successful or -EINVAL if qos_box_dev parameter is invalid or qos_box_dev is
 * not registered.
 */
int google_qos_box_dev_unregister(struct qos_box_dev *qos_box_dev);

#endif /* _GOOGLE_MEM_QOS_H */
