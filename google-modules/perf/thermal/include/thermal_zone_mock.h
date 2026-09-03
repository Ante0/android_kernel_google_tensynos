/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_zone_helper.h API's to register, unregister and handle
 * thermal zone(s).
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_ZONE_MOCK_H_
#define _THERMAL_ZONE_MOCK_H_

#include "thermal_zone_helper.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
struct thermal_zone_device *mock_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops);

void mock_devm_thermal_of_zone_unregister(
		 struct device *dev, struct thermal_zone_device *tz);

int mock_platform_get_irq_optional(struct platform_device *pdev, int idx);

int mock_devm_request_threaded_irq(struct device *dev, unsigned int irq,
				   irq_handler_t handler, irq_handler_t thread_fn,
				   unsigned long irqflags, const char *devname,
				   void *dev_id);
#else
static inline struct thermal_zone_device *mock_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void mock_devm_thermal_of_zone_unregister(
		struct device *dev, struct thermal_zone_device *tz)
{}

static inline int mock_platform_get_irq_optional(struct platform_device *pdev,
						 int idx)
{
	return -EOPNOTSUPP;
}

static inline int mock_devm_request_threaded_irq(struct device *dev, unsigned int irq,
						 irq_handler_t handler, irq_handler_t thread_fn,
						 unsigned long irqflags, const char *devname,
						 void *dev_id)
{
	return -EOPNOTSUPP;
}

#endif // CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST

static inline struct thermal_zone_device *register_thermal_zone(
		struct device *dev, int i, struct google_sensor_data *sens,
		const struct thermal_zone_device_ops *ops)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_devm_thermal_of_zone_register(dev, i, sens, ops);
#else
	return devm_thermal_of_zone_register(dev, i, sens, ops);
#endif
}

static inline void unregister_thermal_zone(struct device *dev, struct google_sensor_data *sens)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
		mock_devm_thermal_of_zone_unregister(dev, sens->tzd);
#else
		devm_thermal_of_zone_unregister(dev, sens->tzd);
#endif
}

static inline int get_irq_optional(struct platform_device *pdev)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_platform_get_irq_optional(pdev, 0);
#else
	return platform_get_irq_optional(pdev, 0);
#endif
}

static inline int register_threaded_irq(struct device *dev, unsigned int irq_num,
					irq_handler_t handler, unsigned long irqflags,
					void *data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_devm_request_threaded_irq(dev, irq_num, NULL, handler, irqflags,
					      NULL, data);
#else
	return devm_request_threaded_irq(dev, irq_num, NULL, handler, irqflags,
					 NULL, data);
#endif
}
#endif  // _THERMAL_ZONE_MOCK_H_
