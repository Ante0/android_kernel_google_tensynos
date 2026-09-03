/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2025 Google LLC
 */

#ifndef MBFS_BACKEND_H
#define MBFS_BACKEND_H

#include <perf/mbfs.h>
#include <linux/device.h>

struct mbfs_request;
struct mbfs_response;

struct mbfs_client_backend {
	// Static information
	const char *name;
	int (*submit_request)(const struct mbfs_request *req, struct mbfs_response *res);
	// Fields updated at runtime
	uint8_t initialized;
	uint8_t bits_file_index;
	// The index which this backend is registered to in the global backend array.
	int idx_registered;
	union mbfs_client_handle root_handle;
	void *root_folder;
	struct device *dev;
};

extern int register_mbfs_backend(struct mbfs_client_backend *backend);

/*
 * This function is designed for use exclusively during development, where it is
 * ensured that all backend consumers have been unbinded or removed beforehand.
 */
extern void unregister_mbfs_backend(struct mbfs_client_backend *backend);

#endif /* MBFS_BACKEND_H */
