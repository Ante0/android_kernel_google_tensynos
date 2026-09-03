/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA core header file
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_H__
#define __NOA_H__

#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/list.h>

#define MAX_PRIV_SIZE 1024
struct noa_core_entry {
	struct dentry *root;
	struct kobject kobj;
	struct list_head list;
	struct noa_core *core;
	char priv[MAX_PRIV_SIZE];
};

struct noa_core {
	struct dentry *root;
	struct kobject kobj;
	struct list_head entries;
	bool noa_wlan_enable;
};

extern void noa_entry_unregister(void *priv);
extern void *noa_entry_register(u32 size, const char *name, struct kobj_type *ktype);
extern int noa_sysfs_init(struct noa_core *core);
extern void noa_sysfs_exit(struct noa_core *core);
extern int noa_subsysfs_init(void *priv, const char *name, struct kobj_type *ktype);
extern void noa_subsysfs_exit(void *priv);
extern bool noa_wlan_enable_get(void);
extern void noa_wlan_enable_set(bool enable);
extern void noa_wlan_get_sscd_src(void **pdata, void **pdev);
#endif /* __NOA_H__ */
