/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_QOS_BOX_OF_H
#define _GOOGLE_QOS_BOX_OF_H

#include <linux/of.h>
#include "google_qos_box.h"

int of_qos_box_read_qcfg(struct qos_box_dev *qos_box_dev, struct device_node *np);
int of_qos_box_read_common_property(struct qos_box_dev *qos_box_dev, struct device_node *np);

#endif /* _GOOGLE_QOS_BOX_OF_H */
