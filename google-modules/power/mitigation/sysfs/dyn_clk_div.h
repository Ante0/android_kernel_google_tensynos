/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SYSFS_DYN_CLK_DIV_H
#define _SYSFS_DYN_CLK_DIV_H

#define GEN_DYN_CLK_DIV_EN(core)\
static ssize_t core##_dyn_clk_div_en_show(struct device *dev,\
						   struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_dyn_clk_div_enable(bcl_dev, core, buf);\
} \
\
static ssize_t core##_dyn_clk_div_en_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_dyn_clk_div_enable(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(core##_dyn_clk_div_en)

#define GEN_DYN_CLK_DIV_RATIO(core)\
static ssize_t core##_dyn_clk_div_ratio_show(struct device *dev,\
						   struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_dyn_clk_div_ratio(bcl_dev, core, buf);\
} \
\
static ssize_t core##_dyn_clk_div_ratio_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_dyn_clk_div_ratio(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(core##_dyn_clk_div_ratio)

#define GEN_DYN_CLK_DIV_THRESH(core)\
static ssize_t core##_dyn_clk_div_thresh_show(struct device *dev,\
						   struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_dyn_clk_div_thresh(bcl_dev, core, buf);\
} \
\
static ssize_t core##_dyn_clk_div_thresh_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_dyn_clk_div_thresh(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(core##_dyn_clk_div_thresh)

#endif /* _SYSFS_DYN_CLK_DIV_H */
