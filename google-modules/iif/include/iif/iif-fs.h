/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * File system operations for the IIF device.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/fs.h>

/* Contains the file operations of the IIF char device. */
extern const struct file_operations iif_fops;
