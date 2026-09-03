/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_msg_mock.h All the mock functions for the msg helper test.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_MSG_MOCK_H_
#define _THERMAL_MSG_MOCK_H_

#include <linux/err.h>
#include "thermal_cpm_mbox.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
int mock_cpm_msg_send(union thermal_cpm_message *therm_msg, int *status);
int mock_msg_cdev_to_hw_tzid(enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid);
#else
static inline int mock_cpm_msg_send(union thermal_cpm_message *therm_msg, int *status)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_cdev_to_hw_tzid(enum hw_dev_type cdev_id,
					   enum hw_thermal_zone_id *hw_tzid)
{
	return -EOPNOTSUPP;
}
#endif

static inline int thermal_msg_send_req(union thermal_cpm_message *therm_msg, int *status)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_cpm_msg_send(therm_msg, status);
#else
	return thermal_cpm_send_mbox_req(therm_msg, status);
#endif
}

static inline int thermal_msg_cdev_to_hw_tzid(enum hw_dev_type cdev_id,
					      enum hw_thermal_zone_id *hw_tzid)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_cdev_to_hw_tzid(cdev_id, hw_tzid);
#else
	return thermal_cpm_mbox_cdev_to_hw_tzid(cdev_id, hw_tzid);
#endif
}

#endif  // _THERMAL_MSG_MOCK_H_
