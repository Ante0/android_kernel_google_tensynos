/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __DEBOUNCE_TIME_H
#define __DEBOUNCE_TIME_H

enum DEBOUNCE_TYPE { deglitch, release };

#define DEVICE_ATTR_HEADER(_name) \
	extern struct device_attribute dev_attr_##_name

DEVICE_ATTR_HEADER(uvlo1_det);
DEVICE_ATTR_HEADER(uvlo2_det);
DEVICE_ATTR_HEADER(batoilo1_det);
DEVICE_ATTR_HEADER(batoilo2_det);
DEVICE_ATTR_HEADER(batoilo1_int_det);
DEVICE_ATTR_HEADER(batoilo2_int_det);
DEVICE_ATTR_HEADER(uvlo1_rel);
DEVICE_ATTR_HEADER(uvlo2_rel);
DEVICE_ATTR_HEADER(batoilo1_rel);
DEVICE_ATTR_HEADER(batoilo2_rel);
DEVICE_ATTR_HEADER(batoilo1_int_rel);
DEVICE_ATTR_HEADER(batoilo2_int_rel);

#endif /* __DEBOUNCE_TIME_H */
