/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2024 Google LLC
 */

#ifndef MBFS_H
#define MBFS_H

#include <linux/device.h>

#define MBFS_PAYLOAD_BYTES 8

enum mbfs_error_code {
	MBFS_OK,
	MBFS_UNRECOGNIZED_COMMAND,
	MBFS_NOT_FOUND,
	MBFS_FORBIDDEN,
	MBFS_ILLEGAL_OPERATION,
	MBFS_TRY_AGAIN,
	MBFS_CUSTOM_FILE_ERROR, // the last from mbfs protocol
	NUM_MBFS_PROTOCOL_ERROR_CODES,
	MBFS_COMMUNICATION_FAILURE =
		NUM_MBFS_PROTOCOL_ERROR_CODES, // the first related to linux platform
	MBFS_PROBE_DEFER,
	MBFS_INVALID_ARG,
	NUM_MBFS_ERROR_CODES,
};
_Static_assert(NUM_MBFS_PROTOCOL_ERROR_CODES <= 8,
	       ""); // Error codes need to fit on 3 bits

enum mbfs_node_type {
	MBFS_FOLDER,
	MBFS_FILE,
	NUM_MBFS_NODE_TYPES,
};
_Static_assert(NUM_MBFS_NODE_TYPES == 2, ""); // Type should fit on 1 bit

enum mbfs_file_value_type {
	MBFS_UINT64,
	MBFS_INT64,
	MBFS_DOUBLE,
	MBFS_SHORT_STRING, // 8-byte string (may not be nil-terminated)
	NUM_MBFS_VALUE_TYPES
};
_Static_assert(NUM_MBFS_VALUE_TYPES <= 16, "");

// This mbfs module implements a client library for the mbfs pseudo-file
// system protocol.
// This provides a sysfs-like interface operable over MBA mailbox exchanges,
// that can be mounted from different locations.

#define MAX_MBFS_NAME_LENGTH (MBFS_PAYLOAD_BYTES * 2)
#define MBFS_INVALID_HANDLE (union mbfs_client_handle){.backend_idx = 0xF}

// Convenient way to refer to 8-byte values as values of appropriate types
// without using casts.
union val64 {
	uint64_t number;
	double fp_number;
	char text[MBFS_PAYLOAD_BYTES];
};
_Static_assert(sizeof(union val64) == MBFS_PAYLOAD_BYTES, "");

#define val64_equals(a, b) ((a).number == (b).number)

// We take advantage of MBFS handles only being 28-bit long to use 4 bits
// to refer to the mount id. This means library users just need 32-bit
// handles to uniquely identify a file regardless of what MBFS tree it is in.
union mbfs_client_handle {
	uint32_t handle;
	struct {
		uint32_t remote_handle : 28; // Handle used in MBFS protocol exchanges
		uint32_t backend_idx : 4; // Index of the local mount point for the mbfs tree
	};
};

struct mbfs_client_node_desc {
	char name[MAX_MBFS_NAME_LENGTH + 1];
	bool read_permission;
	bool write_permission;
	enum mbfs_node_type node_type; // Folder or file?
	enum mbfs_file_value_type value_type; // If file, expected value type?
	uint16_t num_subfolders; // If folder, number of sub folders?
	uint16_t num_files; // If folder, number of files?
};

extern int mbfs_error2linux(enum mbfs_error_code error);
extern const char *get_mbfs_error_string(enum mbfs_error_code code);

extern enum mbfs_error_code mbfs_get_handle(const char *path, union mbfs_client_handle *handle);
extern enum mbfs_error_code mbfs_get_node_desc(union mbfs_client_handle handle,
					       struct mbfs_client_node_desc *desc);
extern enum mbfs_error_code mbfs_get_child_by_name(const union mbfs_client_handle parent,
						   const char *child_name,
						   union mbfs_client_handle *child);
extern enum mbfs_error_code mbfs_get_nth_child(union mbfs_client_handle handle,
					       enum mbfs_node_type node_type, uint32_t n,
					       union mbfs_client_handle *child_handle);
extern enum mbfs_error_code mbfs_read_file(union mbfs_client_handle handle, union val64 *output);
extern enum mbfs_error_code mbfs_write_file(union mbfs_client_handle handle, union val64 input);

extern enum mbfs_error_code mbfs_read_child_by_name(union mbfs_client_handle parent_h,
						    const char *child_name, union val64 *output);

extern bool is_mbfs_handle_invalid(union mbfs_client_handle handle);

#endif
