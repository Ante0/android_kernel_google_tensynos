/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Linux Kernel crypto cipher implementation for GPCA.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_CIPHER_H
#define _GOOGLE_GPCA_CIPHER_H

#include <linux/types.h>

struct gpca_dev;

/**
 * gpca_cipher_register() - Register GPCA cipher implementation.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_cipher_register(struct gpca_dev *gpca_dev);

/**
 * gpca_cipher_unregister() - Unregister GPCA cipher implementation.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
void gpca_cipher_unregister(struct gpca_dev *gpca_dev);

#endif /* _GOOGLE_GPCA_CIPHER_H */
