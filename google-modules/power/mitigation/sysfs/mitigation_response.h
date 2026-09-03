/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SYSFS_MITIGATION_RESPONSE_H
#define _SYSFS_MITIGATION_RESPONSE_H

#define GEN_MITIGATION_RES_EN(core)\
static ssize_t mitigation_##core##_res_en_show(struct device *dev,\
					       struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_mitigation_res_en(bcl_dev, core, buf);\
} \
\
static ssize_t mitigation_##core##_res_en_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_mitigation_res_en(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(mitigation_##core##_res_en)

#define GEN_MITIGATION_RES_TYPE(core)\
static ssize_t mitigation_##core##_res_type_show(struct device *dev,\
						 struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_mitigation_res_type(bcl_dev, core, buf);\
} \
\
static ssize_t mitigation_##core##_res_type_store(struct device *dev,\
						  struct device_attribute *attr,\
						  const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_mitigation_res_type(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(mitigation_##core##_res_type)

#define GEN_MITIGATION_RES_HYST(core)\
static ssize_t mitigation_##core##_res_hyst_show(struct device *dev,\
						 struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_mitigation_res_hyst(bcl_dev, core, buf);\
} \
\
static ssize_t mitigation_##core##_res_hyst_store(struct device *dev,\
						  struct device_attribute *attr,\
						  const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_mitigation_res_hyst(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(mitigation_##core##_res_hyst)

#endif /* _SYSFS_MITIGATION_RESPONSE_H */
