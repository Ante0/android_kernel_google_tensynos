/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_cpm_mbox_helper_internal Implement for thermal_cpm_mbox functionality.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_CPM_MBOX_HELPER_INTERNAL_H_
#define _THERMAL_CPM_MBOX_HELPER_INTERNAL_H_

#include "thermal_cpm_mbox.h"

int cpm_mbox_parse_soc_data(struct thermal_cpm_mbox_driver_data *drv_data);
int cpm_mbox_parse_device_tree(struct thermal_cpm_mbox_driver_data *drv_data);
int cpm_mbox_send_message(struct thermal_cpm_mbox_driver_data *driver_data,
			  struct cpm_iface_req *cpm_req);
void cpm_mbox_rx_callback(u32 context, void *msg, void *priv_data);
int cpm_mbox_init(struct thermal_cpm_mbox_driver_data *drv_data);
void thermal_cpm_mbox_init_notifier(struct thermal_cpm_mbox_driver_data *drv_data);
void thermal_cpm_mbox_free(struct thermal_cpm_mbox_driver_data *drv_data);
int sensor_id_to_hw_tzid(struct thermal_cpm_mbox_driver_data *drv_data, int sensor_id,
			   enum hw_thermal_zone_id *hw_tzid);
int hw_tzid_to_rx_cb_type(struct thermal_cpm_mbox_driver_data *drv_data,
			  enum hw_thermal_zone_id hw_tzid, enum hw_dev_type *cdev_id);
int hw_cdev_id_to_hw_tzid(struct thermal_cpm_mbox_driver_data *drv_data,
			  enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid);
const char *get_tz_name(struct thermal_cpm_mbox_driver_data *drv_data,
			enum hw_thermal_zone_id tz_id);
#endif  // _THERMAL_CPM_MBOX_HELPER_INTERNAL_H_
