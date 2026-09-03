/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC
 */

#ifndef __QOS_BOX_GOOGLE_QOS_BOX_REG_API_H
#define __QOS_BOX_GOOGLE_QOS_BOX_REG_API_H

#include <linux/errno.h>

struct qos_box_dev;

#if IS_ENABLED(CONFIG_GOOGLE_MEM_QOS)

/**
 * google_qos_box_vc_map_cfg_read() - read VC_MAP_CFG register
 * @qos_box_dev: the qos_box_dev
 * @val: read value
 *
 * Read VC_MAP_CFG register for a specific qos_box_dev.
 *
 * Return: 0 on successful or error code on failure
 */
int google_qos_box_vc_map_cfg_read(struct qos_box_dev *qos_box_dev, u32 *val);

/**
 * get_qos_box_dev_by_index() - look up qos_box device with the index
 * @consumer: client driver with device node having the qos_box phandle
 * @index: the index of a qos_box phandle
 *
 * Adds the device link between the consumer and qos_box device found.
 * The device link is to define the dependency of the consumer and the supplier (qos_box) devices.
 *
 * Return: a pointer to the qos_box_dev or an ERR_PTR() encoded error code on failure.
 */
struct qos_box_dev *get_qos_box_dev_by_index(struct device *consumer, int index);

/**
 * get_qos_box_dev_by_name() - look up qos_box device with the name
 * @consumer: client driver with device node having the qos_box phandle
 * @name: the name of the qos_box
 *
 * Adds the device link between the consumer and qos_box device found.
 * The device link is to define the dependency of the consumer and the supplier (qos_box) devices.
 *
 * Return: a pointer to the qos_box_dev or an ERR_PTR() encoded error code on failure.
 */
struct qos_box_dev *get_qos_box_dev_by_name(struct device *consumer, const char *name);

#else

static inline int google_qos_box_vc_map_cfg_read(struct qos_box_dev *qos_box_dev, u32 *val)
{
	return 0;
}

static inline struct qos_box_dev *get_qos_box_dev_by_index(struct device *consumer, int index)
{
	return ERR_PTR(-EINVAL);
}

static inline struct qos_box_dev *get_qos_box_dev_by_name(struct device *consumer, const char *name)
{
	return ERR_PTR(-EINVAL);
}

#endif

#endif /* __QOS_BOX_GOOGLE_QOS_BOX_REG_API_H */
