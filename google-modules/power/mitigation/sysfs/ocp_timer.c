// SPDX-License-Identifier: GPL-2.0 only

#include "ocp_timer.h"
#include <linux/kernel.h>
#include "soc/soc_defs.h"
#include "bcl.h"

static ssize_t ocp_batfet_timeout_enable_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_batfet_timeout_enable(bcl_dev, buf);
}

static ssize_t ocp_batfet_timeout_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_batfet_timeout_enable(bcl_dev, buf, size);
}

DEVICE_ATTR_RW(ocp_batfet_timeout_enable);

static ssize_t ocp_bat_throttle_timeout_enable_show(struct device *dev,
						    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_bat_throttle_timeout_enable(bcl_dev, buf);
}

static ssize_t ocp_bat_throttle_timeout_enable_store(struct device *dev,
						     struct device_attribute *attr,
						     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_bat_throttle_timeout_enable(bcl_dev, buf, size);
}

DEVICE_ATTR_RW(ocp_bat_throttle_timeout_enable);

static ssize_t ocp_batfet_timeout_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_batfet_timeout(bcl_dev, buf);
}

static ssize_t ocp_batfet_timeout_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_batfet_timeout(bcl_dev, buf, size);
}

DEVICE_ATTR_RW(ocp_batfet_timeout);

static ssize_t ocp_bat_throttle_timeout_show(struct device *dev,
					     struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_bat_throttle_timeout(bcl_dev, buf);
}

static ssize_t ocp_bat_throttle_timeout_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_bat_throttle_timeout(bcl_dev, buf, size);
}

DEVICE_ATTR_RW(ocp_bat_throttle_timeout);
