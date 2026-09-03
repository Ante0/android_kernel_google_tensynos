// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */
#include <dvfs-frontend/dvfs_target_frontend.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include "dvfs_target_frontend_debug.h"

struct dvfs_tgt_fe_debug {
	struct dentry *debugfs_root;
	int current_domain_id;
};

struct dvfs_tgt_fe_debug debug_controller = {
	.debugfs_root = NULL,
	.current_domain_id = 0,
};


static int debugfs_domain_id_get(void *data, u64 *val)
{
	*val = (u64)debug_controller.current_domain_id;

	return 0;
}

static int debugfs_domain_id_set(void *data, u64 val)
{
	debug_controller.current_domain_id = (int)val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(debugfs_domain_id_fops, debugfs_domain_id_get,
			 debugfs_domain_id_set, "%llu\n");

static int debugfs_prepare(void *data, u64 *val)
{
	int domain_id = debug_controller.current_domain_id;

	*val = (u64)dvfs_fe_prepare_domain(domain_id);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(debugfs_prepare_fops, debugfs_prepare, NULL, "%llu\n");

static int debugfs_pf_level_get(void *data, u64 *val)
{
	int domain_id = debug_controller.current_domain_id;

	*val = (u64)dvfs_fe_get_pf_level(domain_id);

	return 0;
}

static int debugfs_pf_level_set(void *data, u64 val)
{
	int domain_id = debug_controller.current_domain_id;

	dvfs_fe_set_pf_level(domain_id, (int)val);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(debugfs_pf_level_fops, debugfs_pf_level_get,
			 debugfs_pf_level_set, "%llu\n");

void dvfs_target_frontend_debugfs_init(void)
{
	struct dvfs_tgt_fe_debug *controller = &debug_controller;
	struct dentry *root;

	root = debugfs_create_dir("dvfs_target_frontend", NULL);
	controller->debugfs_root = root;

	if (!root) {
		pr_err("Failed to create debugfs root directory\n");
		return;
	}

	debugfs_create_file("domain_id", 0644, root, NULL,
			    &debugfs_domain_id_fops);

	debugfs_create_file("pf_level", 0644, root, NULL,
			    &debugfs_pf_level_fops);

	debugfs_create_file("prepare", 0444, root, NULL, &debugfs_prepare_fops);
}

void dvfs_target_frontend_debugfs_remove(void)
{
	struct dvfs_tgt_fe_debug *controller = &debug_controller;

	debugfs_remove_recursive(controller->debugfs_root);
}
