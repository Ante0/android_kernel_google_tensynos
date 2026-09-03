/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_MEM_QOS_DEBUGFS_H
#define _GOOGLE_MEM_QOS_DEBUGFS_H

#include <linux/debugfs.h>

#include "google_mem_qos.h"

int google_mem_qos_init_debugfs(struct google_mem_qos_desc *desc);
void google_mem_qos_remove_debugfs(struct google_mem_qos_desc *desc);
struct dentry *qos_box_debugfs_entry_get(void);

#endif /* _GOOGLE_MEM_QOS_DEBUGFS_H */
