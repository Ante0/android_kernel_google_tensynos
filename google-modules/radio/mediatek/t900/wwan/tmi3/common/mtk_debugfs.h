/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_DEBUGFS_H__
#define __MTK_DEBUGFS_H__

#include <linux/debugfs.h>

#include "mtk_dev.h"

#define MTK_DBGFS_BUF_MAX 1024

#ifdef CONFIG_DEBUG_FS

struct mtk_debugfs {
	char *name;
	umode_t mode;
	ssize_t (*read)(void *data, char *buf, ssize_t max_cnt);
	ssize_t (*write)(void *data, const char *buf, ssize_t cnt);
	const struct file_operations fops;
};

void mtk_drv_dbgfs_init(void);
void mtk_drv_dbgfs_exit(void);

struct dentry *mtk_get_drv_dentry(void);
static inline struct dentry *mtk_get_dev_dentry(struct mtk_md_dev *mdev)
{
	return mdev->dev_dentry;
}

struct dentry *mtk_dbgfs_create_dir(struct dentry *parent, const char *name);
struct dentry *mtk_dbgfs_create_file(struct dentry *parent,
				     const struct mtk_debugfs *dbgfs, void *data);
static inline void mtk_dbgfs_remove(struct dentry *dentry)
{
	debugfs_remove_recursive(dentry);
}

ssize_t __mtk_dbgfs_fops_read(const struct mtk_debugfs *dbgfs, struct file *file,
			      char __user *buf, size_t cnt, loff_t *ppos);
ssize_t __mtk_dbgfs_fops_write(const struct mtk_debugfs *dbgfs, struct file *file,
			       const char __user *buf, size_t cnt, loff_t *loff);

#define MTK_DBGFS(_name, _read, _write) \
static const struct mtk_debugfs mtk_dbgfs_##_name; \
static ssize_t __mtk_dbgfs_fops_read_##_name(struct file *file, \
			char __user *buf, size_t cnt, loff_t *ppos) \
{ \
	return __mtk_dbgfs_fops_read(&mtk_dbgfs_##_name, file, buf, cnt, ppos); \
} \
static ssize_t __mtk_dbgfs_fops_write_##_name(struct file *file, \
			const char __user *buf, size_t cnt, loff_t *loff) \
{ \
	return __mtk_dbgfs_fops_write(&mtk_dbgfs_##_name, file, buf, cnt, loff); \
} \
static const struct mtk_debugfs mtk_dbgfs_##_name = { \
	.name  = __stringify(_name), \
	.mode  = 0600, \
	.read  = _read, \
	.write = _write, \
	.fops  = { \
		.read   = __mtk_dbgfs_fops_read_##_name, \
		.write  = __mtk_dbgfs_fops_write_##_name, \
		.open   = simple_open, \
		.owner  = THIS_MODULE, \
	}, \
}

#else /* !CONFIG_DEBUG_FS */

struct mtk_debugfs {
	ssize_t (*read)(void *data, char *buf, ssize_t max_cnt);
	ssize_t (*write)(void *data, const char *buf, ssize_t cnt);
};

static inline void mtk_drv_dbgfs_init(void) {}
static inline void mtk_drv_dbgfs_exit(void) {}
static inline struct dentry *mtk_get_drv_dentry(void) {return NULL; }
static inline struct dentry *mtk_get_dev_dentry(struct mtk_md_dev *mdev) {return NULL; }
static inline struct dentry *mtk_dbgfs_create_dir(struct dentry *parent,
						  const char *name) {return NULL; }
static inline struct dentry *mtk_dbgfs_create_file(struct dentry *parent,
						   const struct mtk_debugfs *dbgfs,
						   void *data) {return NULL; }
static inline void mtk_dbgfs_remove(struct dentry *dentry) {}

#define MTK_DBGFS(_name, _read, _write) \
static const struct mtk_debugfs mtk_dbgfs_##_name = { \
	.read  = _read, \
	.write = _write, \
}

#endif /* CONFIG_DEBUG_FS */
#endif /* __MTK_DEBUGFS_H__ */

