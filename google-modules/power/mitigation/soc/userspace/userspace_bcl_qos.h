/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __USERSPACE_BCL_QOS_H
#define __USERSPACE_BCL_QOS_H

#include "bcl.h"

int userspace_bcl_qos_setup(struct bcl_device *bcl_dev);
int userspace_bcl_qos_update(struct bcl_device *bcl_dev, int throttle_state);

#endif /* __USERSPACE_BCL_QOS_H */
