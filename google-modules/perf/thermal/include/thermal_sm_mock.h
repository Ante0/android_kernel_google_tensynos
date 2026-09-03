/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_sm_mock.h Helper to declare and use thermal sm mock functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_SM_MOCK_H_
#define _THERMAL_SM_MOCK_H_

#include <linux/io.h>

#include "thermal_msg_helper.h"
#include "thermal_sm_helper.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
int mock_thermal_sm_get_section_addr(u8, u32*, u32*, u32*);
void __iomem *mock_devm_ioremap(struct device*, u32, u32);
void mock_memcpy_fromio(void*, void __iomem*, u32, u32);
void mock_memcpy_toio(void __iomem*, void*, u32, u32);
int mock_sm_cdev_to_hw_tzid(enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid);
#else
static inline int mock_thermal_sm_get_section_addr(u8 section, u32 *version, u32 *addr, u32 *size)
{
	return -EOPNOTSUPP;
}
static inline void __iomem *mock_devm_ioremap(struct device *dev, u32 addr, u32 size)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static inline void mock_memcpy_fromio(void *dest, void __iomem *src, size_t size)
{
	return;
}
static inline void mock_memcpy_toio(void __iomem *dest, void *src, size_t size)
{
	return;
}
static inline int mock_sm_cdev_to_hw_tzid(enum hw_dev_type cdev_id,
					  enum hw_thermal_zone_id *hw_tzid)
{
	return -EOPNOTSUPP;
}
#endif // CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST

static inline int thermal_sm_get_section_addr(u8 section, u32 *version, u32 *addr, u32 *size)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_sm_get_section_addr(section, version, addr, size);
#else
	return msg_thermal_sm_get_section_addr(section, version, addr, size);
#endif
}

static inline void __iomem *thermal_sm_devm_ioremap(struct device *dev, u32 addr, u32 size)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_devm_ioremap(dev, addr, size);
#else
	return devm_ioremap(dev, addr, size);
#endif
}

static inline void thermal_sm_memcpy_fromio(void *dest, void __iomem *base, u32 offset, u32 size)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_memcpy_fromio(dest, base, offset, size);
#else
	return memcpy_fromio(dest, base + offset, size);
#endif
}

static inline void thermal_sm_memcpy_toio(void __iomem *base, void *source, u32 offset, u32 size)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_memcpy_toio(base, source, offset, size);
#else
	return memcpy_toio(base + offset, source, size);
#endif
}

static inline int thermal_sm_cdev_to_hw_tzid(enum hw_dev_type cdev_id,
					     enum hw_thermal_zone_id *hw_tzid)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_sm_cdev_to_hw_tzid(cdev_id, hw_tzid);
#else
	return thermal_cpm_mbox_cdev_to_hw_tzid(cdev_id, hw_tzid);
#endif
}

#endif // _THERMAL_SM_MOCK_H_
