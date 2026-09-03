/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#ifndef UWB_SYSNODES_H
#define UWB_SYSNODES_H

#include <linux/types.h>

struct u100_ctx;

struct uwb_sysnode {
	bool init_kobj_res;
	struct kobject uwb_kobj;
	struct kobject power_stats_kobj;
};

int uwb_sysfs_init(struct u100_ctx *u100_ctx);
void uwb_sysfs_exit(struct u100_ctx *u100_ctx);

#endif /* UWB_SYSNODES_H */

