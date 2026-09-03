/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Key slot management functions.
 *
 * Copyright (C) 2022 Google LLC.
 */

#ifndef _GOOGLE_GPCA_KEYS_INTERNAL_H
#define _GOOGLE_GPCA_KEYS_INTERNAL_H

#include <linux/types.h>

struct gpca_dev;
struct gpca_key_handle;

/**
 * gpca_key_init() - Initialize Key slot management data structures.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_key_init(struct gpca_dev *gpca_dev);

/**
 * gpca_key_reserve_keyslot() - Reserve a key slot for key handle.
 *
 * @gpca_dev: Pointer to GPCA device struct
 * @handle: Key handle for which to reserve keyslot.
 *
 * Return : 0 on success, negative if unable to reserve keyslot.
 */
int gpca_key_reserve_keyslot(struct gpca_dev *gpca_dev,
			     struct gpca_key_handle *handle);

/**
 * gpca_key_swap_in_key() - Swap in a key for use in GPCA command.
 *
 * @gpca_dev: Pointer to GPCA device struct
 * @handle: Key handle to swap in.
 *
 * Return : 0 on success, negative if unable to swap in key.
 */
int gpca_key_swap_in_key(struct gpca_dev *gpca_dev,
			 struct gpca_key_handle *handle);

/**
 * gpca_key_free_keyslot() - Free up keyslot.
 *
 * @gpca_dev: Pointer to GPCA device struct
 * @keyslot: Key slot to free.
 */
void gpca_key_free_keyslot(struct gpca_dev *gpca_dev, u8 keyslot);

/**
 * gpca_key_mark_key_swappable() - Move the key from pinned to swappable.
 *
 * @gpca_dev: Pointer to GPCA device struct
 * @handle: Key handle to move from pinned list to swappable.
 */
void gpca_key_mark_key_swappable(struct gpca_dev *gpca_dev,
				 struct gpca_key_handle *handle);

/**
 * gpca_key_deinit() - Free up Key slot management data structures.
 *
 * @gpca_dev: Pointer to GPCA device struct
 */
void gpca_key_deinit(struct gpca_dev *gpca_dev);

#endif /* _GOOGLE_GPCA_KEYS_INTERNAL_H */
