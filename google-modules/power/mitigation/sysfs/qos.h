/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __BCL_SYSFS_QOS_H
#define __BCL_SYSFS_QOS_H

#include <linux/types.h>

struct bcl_device;

ssize_t qos_show(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t qos_store(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size);

#endif /* __BCL_SYSFS_QOS_H */
