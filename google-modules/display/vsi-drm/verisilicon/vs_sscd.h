/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Google LLC
 */

#ifndef __VS_SSCD_H__
#define __VS_SSCD_H__

#include <drm/drm_device.h>

/**
 * trigger_coredump - Trigger a coordinated subsystem coredump
 * @drm_dev: DRM device to perform the coredump on
 * @calling_dev: device that is triggering the coredump
 * @reason: string describing the reason for the coredump
 *
 * This function initiates a coordinated subsystem coredump (SSCD) for the
 * display subsystem.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int trigger_coredump(struct drm_device *drm_dev, struct device *calling_dev, const char *reason);

#endif /* __VS_SSCD_H__ */
