/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Linux Kernel crypto Asymmetric key algorithms implementation for GPCA.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_ASYMM_H
#define _GOOGLE_GPCA_ASYMM_H

#include <linux/types.h>

struct gpca_dev;

/**
 * gpca_asymm_register() - Register GPCA asymmetric algorithms implementation.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_asymm_register(struct gpca_dev *gpca_dev);

/**
 * gpca_asymm_unregister() - Unregister GPCA asymmetric algorithms implementation.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
void gpca_asymm_unregister(struct gpca_dev *gpca_dev);

#endif /* _GOOGLE_GPCA_ASYMM_H */
