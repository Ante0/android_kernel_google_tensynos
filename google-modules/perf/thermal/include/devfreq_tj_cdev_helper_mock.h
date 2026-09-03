/* SPDX-License-Identifier: GPL-2.0 */
/*
 * devfreq_tj_cdev_helper_mock.h Helper to declare and use devfreq cooling device
 * mock functions.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#ifndef _DEVFREQ_TJ_CDEV_HELPER_MOCK_H_
#define _DEVFREQ_TJ_CDEV_HELPER_MOCK_H_

#include "devfreq_tj_cdev_helper.h"
#include "thermal_msg_helper.h"

#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
int mock_cdev_devfreq_init(struct cdev_devfreq_data *cdev,
		      struct device_node *np,
		      enum hw_dev_type cdev_id,
		      cdev_cb success_cb,
		      cdev_cb release_cb);
void mock_cdev_devfreq_exit(struct cdev_devfreq_data *cdev);
int mock_cpm_mbox_register_notification(enum hw_dev_type type, struct notifier_block *nb);
void mock_cpm_mbox_unregister_notification(enum hw_dev_type type, struct notifier_block *nb);
void mock_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq);
struct thermal_cooling_device *
mock_of_cooling_device_register(struct device *dev,
				struct device_node *np,
				char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops);
int mock_of_property_read_u32(const struct device_node *np, const char *propname, u32 *out_value);
struct device_node *mock_of_parse_phandle(const struct device_node *np,
					  const char *phandle_name,
					  int index);
#else
static inline int mock_cdev_devfreq_init(struct cdev_devfreq_data *cdev,
		      struct device_node *np,
		      enum hw_dev_type cdev_id,
		      cdev_cb success_cb,
		      cdev_cb release_cb)
{
	return -EOPNOTSUPP;
}

static inline void mock_cdev_devfreq_exit(struct cdev_devfreq_data *cdev)
{ }

static inline int mock_cpm_mbox_register_notification(enum hw_dev_type type,
						      struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline void mock_cpm_mbox_unregister_notification(enum hw_dev_type type,
							 struct notifier_block *nb)
{ }

static inline void mock_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev,
						   unsigned long freq)
{ }

static inline struct thermal_cooling_device *
mock_of_cooling_device_register(struct device *dev,
				struct device_node *np,
				char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int mock_of_property_read_u32(const struct device_node *np, const char *propname,
					    u32 *out_value)
{
	return -EOPNOTSUPP;
}

static inline struct device_node *mock_of_parse_phandle(const struct device_node *np,
							const char *phandle_name, int index)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif // CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST

static inline int devfreq_tj_cdev_devfreq_init(struct cdev_devfreq_data *cdev,
		      struct device_node *np,
		      enum hw_dev_type cdev_id,
		      cdev_cb success_cb,
		      cdev_cb release_cb)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_cdev_devfreq_init(cdev, np, cdev_id, success_cb, release_cb);
#else
	return cdev_devfreq_init(cdev, np, cdev_id, success_cb, release_cb);
#endif
}

static inline void devfreq_tj_cdev_devfreq_exit(struct cdev_devfreq_data *cdev)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_cdev_devfreq_exit(cdev);
#else
	return cdev_devfreq_exit(cdev);
#endif
}

static inline int devfreq_tj_cpm_mbox_register_notification(enum hw_dev_type type,
							    struct notifier_block *nb)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_cpm_mbox_register_notification(type, nb);
#else
	return thermal_cpm_mbox_register_notification(type, nb);
#endif
}

static inline void devfreq_tj_cpm_mbox_unregister_notification(enum hw_dev_type type,
							      struct notifier_block *nb)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_cpm_mbox_unregister_notification(type, nb);
#else
	return thermal_cpm_mbox_unregister_notification(type, nb);
#endif
}

static inline void devfreq_tj_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev,
							 unsigned long freq)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_cdev_pm_qos_update_request(cdev, freq);
#else
	return cdev_pm_qos_update_request(cdev, freq);
#endif
}

static inline struct thermal_cooling_device *
devfreq_tj_of_cooling_device_register(struct device *dev,
				      struct device_node *np,
				      char *type, void *devdata,
				      const struct thermal_cooling_device_ops *ops)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_of_cooling_device_register(dev, np, type, devdata, ops);
#else
	return devm_thermal_of_cooling_device_register(dev, np, type, devdata, ops);
#endif
}

static inline int devfreq_tj_property_read_u32(const struct device_node *np,
					       const char *propname, u32 *out_value)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_of_property_read_u32(np, propname, out_value);
#else
	return of_property_read_u32(np, propname, out_value);
#endif
}

static inline struct device_node *devfreq_tj_parse_phandle(const struct device_node *np,
							   const char *phandle_name,
							   int index)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return mock_of_parse_phandle(np, phandle_name, index);
#else
	return of_parse_phandle(np, phandle_name, index);
#endif
}

static inline void devfreq_tj_fatal_error(void)
{
#if IS_ENABLED(CONFIG_DEVFREQ_TJ_CDEV_HELPER_KUNIT_TEST)
	return;
#else
	WARN_ON_ONCE(1);
#endif
}
#endif  // _DEVFREQ_TJ_CDEV_HELPER_MOCK_H_
