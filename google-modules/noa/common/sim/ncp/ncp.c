// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP core firmware simulator
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "ncp_wlan_middle_layer.h"

#define to_entry(_kobj) (container_of(_kobj, struct ncp_sim, kobj))
#define to_attr(_attr) (container_of(_attr, struct noa_ncp_sim_kobj_attr, attr))

typedef struct ncp_sim {
	struct kobject kobj;
	struct sysfs_ops sysfs_ops;
	struct kobj_type ktype;
} ncp_sim_t;

struct noa_ncp_sim_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(ncp_sim_t *ncp_sim, char *buf);
	ssize_t (*store)(ncp_sim_t *ncp_sim, const char *buf, size_t size);
};

static ssize_t ncp_sim_shell_exec_result_show(ncp_sim_t *ncp_sim, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "To see the output, please run the `dmesg` command.\n");

	return cnt;
};

typedef struct shell_cmd_table_entry {
	const char *cmd_namespace;
	void (*cmd_handler)(const char *cmd, int argv_len, const char *argv);
} shell_cmd_table_entry_t;

static ssize_t ncp_sim_shell_process_command(ncp_sim_t *ncp_sim, const char *cmd_str, size_t size)
{
	static const shell_cmd_table_entry_t table[] = {
		{ "wlan", ncp_wlan_ml_shell_cmd_handler },
	};
	static const int table_size = sizeof(table) / sizeof(shell_cmd_table_entry_t);
	int i;
	char *cmd_namespace;
	char *sub_cmd;
	char *dup;
	char *cur;

	pr_info("%s(): %s", __func__, cmd_str);

	if (!ncp_sim || !cmd_str) {
		return size;
	}

	dup = kstrdup(cmd_str, GFP_KERNEL);
	cur = dup;
	cmd_namespace = strsep(&cur, " ");
	for (i = 0; i < table_size; i++) {
		if (strcmp(table[i].cmd_namespace, cmd_namespace) == 0) {
			sub_cmd = strsep(&cur, " ");
			if (table[i].cmd_handler) {
				// Add two to the length for whitespace.
				int processed_len =
					strlen(table[i].cmd_namespace) + 1 + strlen(sub_cmd) + 1;
				table[i].cmd_handler(sub_cmd, size - processed_len,
						     cmd_str + processed_len);
			}
			break;
		}
	}

	kfree(dup);

	return size;
};

static struct noa_ncp_sim_kobj_attr attr_shell =
	__ATTR(shell, 0664, ncp_sim_shell_exec_result_show, ncp_sim_shell_process_command);

static struct attribute *default_file_attrs[] = {
	&attr_shell.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t noa_ncp_sim_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	ncp_sim_t *ncp_sim = to_entry(kobj);
	struct noa_ncp_sim_kobj_attr *noa_ncp_sim_attr = to_attr(attr);

	if (noa_ncp_sim_attr->show)
		return noa_ncp_sim_attr->show(ncp_sim, buf);
	return -EIO;
}

static ssize_t noa_ncp_sim_sysfs_store(struct kobject *kobj, struct attribute *attr,
				       const char *buf, size_t count)
{
	ncp_sim_t *ncp_sim = to_entry(kobj);
	struct noa_ncp_sim_kobj_attr *noa_ncp_sim_attr = to_attr(attr);

	if (noa_ncp_sim_attr->store)
		return noa_ncp_sim_attr->store(ncp_sim, buf, count);
	return -EIO;
}

static ncp_sim_t *ncp_sim_get_instance(void)
{
	static ncp_sim_t ncp_sim;
	return &ncp_sim;
}

static int __init ncp_sim_init(void)
{
	ncp_sim_t *ncp_sim = ncp_sim_get_instance();
	int ret;

	memset(ncp_sim, 0, sizeof(ncp_sim_t));
	ncp_sim->sysfs_ops.show = noa_ncp_sim_sysfs_show;
	ncp_sim->sysfs_ops.store = noa_ncp_sim_sysfs_store;
	ncp_sim->ktype.sysfs_ops = &ncp_sim->sysfs_ops;
	ncp_sim->ktype.default_groups = default_file_groups;

	ret = kobject_init_and_add(&ncp_sim->kobj, &ncp_sim->ktype, NULL, "noa_ncp");
	if (ret)
		kobject_put(&ncp_sim->kobj);

	ncp_wlan_ml_platform_init();

	return 0;
}

static void __exit ncp_sim_exit(void)
{
	ncp_sim_t *ncp_sim = ncp_sim_get_instance();

	kobject_del(&ncp_sim->kobj);
	kobject_put(&ncp_sim->kobj);

	ncp_wlan_ml_platform_deinit();
}

module_init(ncp_sim_init);
module_exit(ncp_sim_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Star Chang <starchang@google.com>");
MODULE_DESCRIPTION("NCP Firmware Simualtor Driver");
