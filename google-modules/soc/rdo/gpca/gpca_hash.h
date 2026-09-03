/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Linux Kernel crypto hash implementation for GPCA.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_HASH_H
#define _GOOGLE_GPCA_HASH_H

#include <linux/types.h>

struct gpca_dev;

/**
 * gpca_hash_register() - Register GPCA hash implementation.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_hash_register(struct gpca_dev *gpca_dev);

/**
 * gpca_hash_unregister() - Unregister GPCA hash implementation.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
void gpca_hash_unregister(struct gpca_dev *gpca_dev);

#endif /* _GOOGLE_GPCA_HASH_H */
