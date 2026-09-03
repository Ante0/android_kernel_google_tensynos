// SPDX-License-Identifier: GPL-2.0-only

#include <linux/debugfs.h>
#include <linux/mutex.h>

#include "google_mem_qos.h"
#include "google_mem_qos_debugfs.h"

static struct google_mem_qos_desc *mem_qos_desc;

static int active_scenario_get(void *data, u64 *val)
{
	mutex_lock(&mem_qos_desc->mutex);

	*val = (u64)mem_qos_desc->active_scenario;

	mutex_unlock(&mem_qos_desc->mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(active_scenario, active_scenario_get, NULL, "%llu\n");

struct dentry *qos_box_debugfs_entry_get(void)
{
	return mem_qos_desc ? mem_qos_desc->qos_box_base_dir : NULL;
}

int google_mem_qos_init_debugfs(struct google_mem_qos_desc *desc)
{
	if (!desc)
		return 0;

	mem_qos_desc = desc;

	desc->qos_box_base_dir = debugfs_create_dir("qos_box", NULL);
	desc->base_dir = debugfs_create_dir("mem_qos", NULL);

	debugfs_create_file("active_scenario", 0400, desc->base_dir, NULL, &active_scenario);

	return 0;
}

void google_mem_qos_remove_debugfs(struct google_mem_qos_desc *desc)
{
	if (!desc)
		return;

	debugfs_remove_recursive(desc->qos_box_base_dir);
	debugfs_remove_recursive(desc->base_dir);
}
