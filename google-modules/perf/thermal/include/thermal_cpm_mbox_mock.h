/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_cpm_mbox_mock All the mock functions for the thermal
 * cpm mbox.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_CPM_MBOX_MOCK_H_
#define _THERMAL_CPM_MBOX_MOCK_H_

#include "thermal_cpm_mbox_helper_internal.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)
int mock_cpm_send_request(struct cpm_iface_req *cpm_req);
int mock_cpm_send_message(struct cpm_iface_req *cpm_req);
int mock_thermal_cpm_mbox_parse_device_tree(struct thermal_cpm_mbox_driver_data *drv_data);
int mock_thermal_cpm_mbox_init(void);
int mock_thermal_cpm_mbox_parse_soc_data(void);
#else
static inline int mock_cpm_send_request(void)
{
	return -EOPNOTSUPP;
}

static inline int mock_cpm_send_message(void)
{
	return -EOPNOTSUPP;
}

static inline int mock_thermal_cpm_mbox_parse_device_tree(struct thermal_cpm_mbox_driver_data
							  *drv_data)
{
	return -EOPNOTSUPP;
}

static inline int mock_thermal_cpm_mbox_init(void)
{
	return -EOPNOTSUPP;
}

static inline int mock_thermal_cpm_mbox_parse_soc_data(void)
{
	return -EOPNOTSUPP;
}
#endif // IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)

static inline int thermal_cpm_mbox_send_request(struct thermal_cpm_mbox_driver_data *drv_data,
						struct cpm_iface_req *cpm_req)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)
	return mock_cpm_send_request(cpm_req);
#else
	return cpm_mbox_send_message(drv_data, cpm_req);
#endif
}

static inline int thermal_cpm_mbox_send_message(struct thermal_cpm_mbox_driver_data *drv_data,
						struct cpm_iface_req *cpm_req)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)
	return mock_cpm_send_message(cpm_req);
#else
	return cpm_mbox_send_message(drv_data, cpm_req);
#endif
}

static inline int thermal_cpm_mbox_parse_device_tree(struct thermal_cpm_mbox_driver_data *drv_data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)
	return mock_thermal_cpm_mbox_parse_device_tree(drv_data);
#else
	return cpm_mbox_parse_device_tree(drv_data);
#endif
}

static inline int thermal_cpm_mbox_init(struct thermal_cpm_mbox_driver_data *drv_data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)
	return mock_thermal_cpm_mbox_init();
#else
	return cpm_mbox_init(drv_data);
#endif
}

static inline int thermal_cpm_mbox_parse_soc_data(struct thermal_cpm_mbox_driver_data *drv_data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_CPM_MBOX_KUNIT_TEST)
	return mock_thermal_cpm_mbox_parse_soc_data();
#else
	return cpm_mbox_parse_soc_data(drv_data);
#endif
}

#endif  // _THERMAL_CPM_MBOX_MOCK_H_
