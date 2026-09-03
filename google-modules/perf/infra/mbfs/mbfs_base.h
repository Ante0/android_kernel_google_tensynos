/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2025 Google LLC
 */

#ifndef MBFS_BASE_H
#define MBFS_BASE_H

#include <perf/mbfs.h>
#include <linux/device.h>

#include "mbfs_backend.h"

const char *get_mbfs_file_value_type_string(enum mbfs_file_value_type type);

bool mbfs_get_next_token(const char *path, char *new_token, int new_token_length,
			 const char **next_token);

bool mbfs_is_next_token_valid(const char *path);

struct mbfs_client_backend *mbfs_find_backend(const char *name);

#endif /* MBFS_BASE_H */
