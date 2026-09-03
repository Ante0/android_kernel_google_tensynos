/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2025 Google LLC
 */
#ifndef __LINUX_GSA_AOC_H
#define __LINUX_GSA_AOC_H

#include <linux/device.h>
#include <linux/types.h>

/*
 * GSA General Core interface
 */

/**
 * gsa_get_gsa_version() - Reads the GSA version string into buf
 * @gsa: pointer to GSA &struct device
 * @buf: A character buffer to store the result (at least 4096B)
 *
 * Return: The length of the string written to buf
 */
ssize_t gsa_get_gsa_version(struct device *gsa, char *buf);

#endif /* __LINUX_GSA_IMG_H */
