/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __OCP_TIMER_H
#define __OCP_TIMER_H

#define DEVICE_ATTR_HEADER(_name) \
	extern struct device_attribute dev_attr_##_name

DEVICE_ATTR_HEADER(ocp_batfet_timeout_enable);
DEVICE_ATTR_HEADER(ocp_bat_throttle_timeout_enable);
DEVICE_ATTR_HEADER(ocp_batfet_timeout);
DEVICE_ATTR_HEADER(ocp_bat_throttle_timeout);

#endif /* __OCP_TIMER_H */
