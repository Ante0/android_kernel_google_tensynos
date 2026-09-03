/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * EdgeTPU specific coresight remote header.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __EDGETPU_CORESIGHT_REMOTE_H__
#define __EDGETPU_CORESIGHT_REMOTE_H__

#include "edgetpu-config.h"
#include "edgetpu-internal.h"

#if EDGETPU_USE_CORESIGHT_REMOTE

/**
 * edgetpu_coresight_remote_init() - Initialize coresight remote tracing.
 * @etdev: The EdgeTPU device structure.
 *
 * This function registers the EdgeTPU device with the coresight remote
 * framework. If coresight is not supported by the platform, it returns 0.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int edgetpu_coresight_remote_init(struct edgetpu_dev *etdev);

/**
 * edgetpu_coresight_remote_exit() - Deinitialize coresight remote tracing.
 * @etdev: The EdgeTPU device structure.
 *
 * This function unregisters the EdgeTPU device from the coresight remote
 * framework.
 */
void edgetpu_coresight_remote_exit(struct edgetpu_dev *etdev);

#else /* EDGETPU_USE_CORESIGHT_REMOTE */

static inline int edgetpu_coresight_remote_init(struct edgetpu_dev *etdev)
{
	return 0;
}

static inline void edgetpu_coresight_remote_exit(struct edgetpu_dev *etdev)
{
}

#endif /* EDGETPU_USE_CORESIGHT_REMOTE */

#endif /* __EDGETPU_CORESIGHT_REMOTE_H__ */
