/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_QOS_BOX_DEBUGFS_H
#define _GOOGLE_QOS_BOX_DEBUGFS_H

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "google_qos_box.h"
#include "google_qos_box_reg.h"

#define DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(__name, __addr, __bitmask, __bitshift)          \
static int __name ## _set(void *data, u64 val)                                                  \
{                                                                                               \
	struct qos_box_dbg_policy *dbg_policy = (struct qos_box_dbg_policy *)data;              \
	struct qos_box_dbg *dbg = dbg_policy->instance;                                         \
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;                                     \
	u32 addr = dbg_policy->base + __addr;                                                   \
	u32 read_val = 0;                                                                       \
	u32 write_val = 0;                                                                      \
	int ret = 0;                                                                            \
	unsigned long flags;                                                                    \
	ret = qos_box_rpm_get(qos_box_dev);                                                     \
	if (ret < 0)                                                                            \
		goto out;                                                                       \
	spin_lock_irqsave(&qos_box_dev->lock, flags);                                           \
	if (dbg->ctl) {                                                                         \
		read_val = readl(qos_box_dev->base_addr + addr);                                \
		read_val &= ~__bitmask;                                                         \
		write_val = (u32)(val << __bitshift);                                           \
		write_val &= __bitmask;                                                         \
		writel((unsigned int)write_val | read_val, qos_box_dev->base_addr + addr);      \
	}                                                                                       \
	spin_unlock_irqrestore(&qos_box_dev->lock, flags);                                      \
	qos_box_rpm_put(qos_box_dev);                                                           \
out:                                                                                            \
	return ret;                                                                             \
}                                                                                               \
static int __name ## _get(void *data, u64 *val)                                                 \
{                                                                                               \
	struct qos_box_dbg_policy *dbg_policy = (struct qos_box_dbg_policy *)data;              \
	struct qos_box_dbg *dbg = dbg_policy->instance;                                         \
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;                                     \
	u32 addr = dbg_policy->base + __addr;                                                   \
	u32 tmp = 0;                                                                            \
	int ret = 0;                                                                            \
	unsigned long flags;                                                                    \
	*val = 0;                                                                               \
	ret = qos_box_rpm_get(qos_box_dev);                                                     \
	if (ret < 0)                                                                            \
		goto out;                                                                       \
	spin_lock_irqsave(&qos_box_dev->lock, flags);                                           \
	tmp = readl(qos_box_dev->base_addr + addr);                                             \
	spin_unlock_irqrestore(&qos_box_dev->lock, flags);                                      \
	qos_box_rpm_put(qos_box_dev);                                                           \
out:                                                                                            \
	tmp &= __bitmask;                                                                       \
	tmp >>= __bitshift;                                                                     \
	*val = (u64)tmp;                                                                        \
	return ret;                                                                             \
}                                                                                               \
DEFINE_DEBUGFS_ATTRIBUTE(__name, __name ## _get, __name ## _set, "0x%08llx\n")

#define DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(__name, __addr, __bitmask, __bitshift)                 \
static int __name ## _set(void *data, u64 val)                                                  \
{                                                                                               \
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;                                   \
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;                                     \
	u32 read_val = 0;                                                                       \
	u32 write_val = 0;                                                                      \
	int ret = 0;                                                                            \
	unsigned long flags;                                                                    \
	ret = qos_box_rpm_get(qos_box_dev);                                                     \
	if (ret < 0)                                                                            \
		goto out;                                                                       \
	spin_lock_irqsave(&qos_box_dev->lock, flags);                                           \
	if (dbg->ctl) {                                                                         \
		read_val = readl(qos_box_dev->base_addr + __addr);                              \
		read_val &= ~__bitmask;                                                         \
		write_val = (u32)(val << __bitshift);                                           \
		write_val &= __bitmask;                                                         \
		writel((unsigned int)write_val | read_val, qos_box_dev->base_addr + __addr);    \
	}                                                                                       \
	spin_unlock_irqrestore(&qos_box_dev->lock, flags);                                      \
	qos_box_rpm_put(qos_box_dev);                                                           \
out:                                                                                            \
	return ret;                                                                             \
}                                                                                               \
static int __name ## _get(void *data, u64 *val)                                                 \
{                                                                                               \
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;                                   \
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;                                     \
	u32 tmp = 0;                                                                            \
	int ret = 0;                                                                            \
	unsigned long flags;                                                                    \
	*val = 0;                                                                               \
	ret = qos_box_rpm_get(qos_box_dev);                                                     \
	if (ret < 0)                                                                            \
		goto out;                                                                       \
	spin_lock_irqsave(&qos_box_dev->lock, flags);                                           \
	tmp = readl(qos_box_dev->base_addr + __addr);                                           \
	spin_unlock_irqrestore(&qos_box_dev->lock, flags);                                      \
	qos_box_rpm_put(qos_box_dev);                                                           \
out:                                                                                            \
	tmp &= __bitmask;                                                                       \
	tmp >>= __bitshift;                                                                     \
	*val = (u64)tmp;                                                                        \
	return ret;                                                                             \
}                                                                                               \
DEFINE_DEBUGFS_ATTRIBUTE(__name, __name ## _get, __name ## _set, "0x%08llx\n")

#define DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(__name, __addr, __bitmask, __bitshift)              \
static int __name ## _set(void *data, u64 val)                                                  \
{                                                                                               \
	return 0;                                                                               \
}                                                                                               \
static int __name ## _get(void *data, u64 *val)                                                 \
{                                                                                               \
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;                                   \
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;                                     \
	u32 tmp = 0;                                                                            \
	int ret = 0;                                                                            \
	unsigned long flags;                                                                    \
	ret = qos_box_rpm_get(qos_box_dev);                                                     \
	if (ret < 0)                                                                            \
		goto out;                                                                       \
	spin_lock_irqsave(&qos_box_dev->lock, flags);                                           \
	tmp = readl(qos_box_dev->base_addr + __addr);                                           \
	spin_unlock_irqrestore(&qos_box_dev->lock, flags);                                      \
	qos_box_rpm_put(qos_box_dev);                                                           \
out:                                                                                            \
	tmp &= __bitmask;                                                                       \
	tmp >>= __bitshift;                                                                     \
	*val = (u64)tmp;                                                                        \
	return ret;                                                                             \
}                                                                                               \
DEFINE_DEBUGFS_ATTRIBUTE(__name, __name ## _get, __name ## _set, "0x%08llx\n")

struct qos_box_dbg_policy {
	struct dentry *dir;
	struct qos_box_dbg *instance;
	u32 base;
};

struct qos_box_dbg {
	struct dentry *dir;
	struct dentry *root_dir;
	struct qos_box_dev *qos_box_dev;
	u8 ctl;
	/* protect power_vote */
	struct mutex mutex;
	u8 power_vote;
	u8 restore_when_release_ctl;
	struct qos_box_dbg_policy dbg_policy[NUM_QOS_POLICY];
};

int qos_box_init_debugfs(struct qos_box_dev *qos_box_dev, struct dentry *root_dir);
void qos_box_remove_debugfs(struct qos_box_dev *qos_box_dev);

#endif /* _GOOGLE_QOS_BOX_DEBUGFS_H */
