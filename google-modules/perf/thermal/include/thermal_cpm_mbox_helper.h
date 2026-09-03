/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_cpm_mbox_helper.h Helper for thermal_cpm_mbox.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_CPM_MBOX_HELPER_H_
#define _THERMAL_CPM_MBOX_HELPER_H_

#include <linux/platform_device.h>
#include "thermal_cpm_mbox.h"

int thermal_cpm_mbox_probe_helper(struct thermal_cpm_mbox_driver_data *drv_data);
int __thermal_cpm_send_mbox_req(struct thermal_cpm_mbox_driver_data *drv_data,
				union thermal_cpm_message *message,
				int *status);
int __thermal_cpm_send_mbox_msg(struct thermal_cpm_mbox_driver_data *drv_data,
				union thermal_cpm_message message);
int __thermal_cpm_mbox_register_notification(struct thermal_cpm_mbox_driver_data *drv_data,
					     enum hw_dev_type type,
					     struct notifier_block *nb);
void __thermal_cpm_mbox_unregister_notification(struct thermal_cpm_mbox_driver_data *drv_data,
						enum hw_dev_type type,
						struct notifier_block *nb);

#endif  // _THERMAL_CPM_MBOX_HELPER_H_
