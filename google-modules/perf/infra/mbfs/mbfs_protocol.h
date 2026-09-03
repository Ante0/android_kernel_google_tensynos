/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2024 Google LLC
 */

#ifndef MBFS_PROTOCOL_H
#define MBFS_PROTOCOL_H

#include <linux/types.h>

enum mbfs_mailbox_command {
	MBFS_GET_PROTOCOL_VERSION, // Get the supported protocol's version
	MBFS_GET_ROOT_HANDLE, // Get top level folder's handle
	MBFS_GET_NUM_CHILDREN, // Get number of children (by type) for a folder
	MBFS_GET_NTH_CHILD_HANDLE, // Get the handle of a folder's Nth child (by type)
	MBFS_FIND_CHILD_BY_BASE_NAME, // Find a folder's child using a base name
	// Find a node's sibling sharing the same base name using the name extension
	MBFS_FIND_HOMONYM_SIBLING_BY_NAME_EXTENSION,
	MBFS_GET_NODE_DESCRIPTOR, // Get information (name, type, ...) for a node
	MBFS_READ_FILE, // Read a file
	MBFS_WRITE_FILE, // Write a file
	NUM_MBFS_COMMANDS,
};
_Static_assert(NUM_MBFS_COMMANDS <= 16, ""); // Command ids need to fit on 4 bits

struct mbfs_request {
	uint32_t reserved_2 : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t reserved_0; // arg2
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_request) == 12, "");

struct mbfs_response {
	uint32_t reserved_0 : 29; // arg1[28:00]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t reserved_1; // arg2
	uint32_t reserved_2; // arg3
};
_Static_assert(sizeof(struct mbfs_response) == 12, "");

struct mbfs_protocol_version_request {
	uint32_t reserved_2 : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t reserved_0; // arg2
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_protocol_version_request) == 12, "");

struct mbfs_protocol_version_response {
	uint32_t reserved_0 : 29; // arg1[28:00]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t minor_version : 16; // arg2[15:00]
	uint32_t major_version : 16; // arg2[31:16]
	uint32_t file_index_len : 5; // arg3[04:00]
	uint32_t reserved_1 : 27; // arg3[31:05]
};
_Static_assert(sizeof(struct mbfs_protocol_version_response) == 12, "");

struct mbfs_get_root_handle_request {
	uint32_t reserved_2 : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t reserved_0; // arg2
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_get_root_handle_request) == 12, "");

struct mbfs_get_root_handle_response {
	uint32_t handle : 28; // arg1[27:00]
	uint32_t reserved_0 : 1; // arg1[28:27]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t reserved_1; // arg2
	uint32_t reserved_2; // arg3
};
_Static_assert(sizeof(struct mbfs_get_root_handle_response) == 12, "");

struct mbfs_get_num_children_request {
	uint32_t folder_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t reserved_0; // arg2
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_get_num_children_request) == 12, "");

struct mbfs_get_num_children_response {
	uint32_t reserved_0 : 29; // arg1[28:00]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t num_child_files : 16; // arg2[15:0]
	uint32_t num_child_folders : 16; // arg2[31:0]
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_get_num_children_response) == 12, "");

struct mbfs_get_nth_child_request {
	uint32_t folder_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t child_type : 1; // arg2[0]
	uint32_t child_index : 16; // arg2[16:1]
	uint32_t reserved_0 : 15; // arg2[31:17]
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_get_nth_child_request) == 12, "");

struct mbfs_get_nth_child_response {
	uint32_t child_handle : 28; // arg1[27:00]
	uint32_t reserved_0 : 1; // arg1[28]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t reserved_1; // arg2
	uint32_t reserved_2; // arg3
};
_Static_assert(sizeof(struct mbfs_get_nth_child_response) == 12, "");

struct mbfs_get_node_descriptor_request {
	uint32_t node_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t request_name_extension : 1; // arg2[0]
	uint32_t reserved_0 : 31; // arg2[31:1]
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_get_node_descriptor_request) == 12, "");

struct mbfs_get_node_descriptor_response {
	uint32_t has_name_extension : 1; // arg1[0]
	uint32_t reserved_0 : 3; // arg1[03:01]
	uint32_t expected_value_type : 4; // arg1[07:04]
	uint32_t reserved_1 : 18; // arg1[25:08]
	uint32_t read_permission : 1; // arg1[26]
	uint32_t write_permission : 1; // arg1[27]
	uint32_t node_type : 1; // arg1[28]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t base_name_0; // arg2
	uint32_t base_name_1; // arg3
};
_Static_assert(sizeof(struct mbfs_get_node_descriptor_response) == 12, "");

struct mbfs_find_child_by_base_name_request {
	uint32_t folder_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t base_name_0; // arg2
	uint32_t base_name_1; // arg3
};
_Static_assert(sizeof(struct mbfs_find_child_by_base_name_request) == 12, "");

struct mbfs_find_child_by_base_name_response {
	uint32_t handle : 28; // arg1[27:00]
	uint32_t node_type : 1; // arg1[28]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t name_extension_0; // arg2
	uint32_t name_extension_1; // arg3
};
_Static_assert(sizeof(struct mbfs_find_child_by_base_name_response) == 12, "");

struct mbfs_find_homonym_sibling_by_name_extension_request {
	uint32_t homonym_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t name_extension_0; // arg2
	uint32_t name_extension_1; // arg3
};
_Static_assert(sizeof(struct mbfs_find_homonym_sibling_by_name_extension_request) == 12, "");

struct mbfs_find_homonym_sibling_by_name_extension_response {
	uint32_t handle : 28; // arg1[27:00]
	uint32_t node_type : 1; // arg1[28]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t reserved_1; // arg2
	uint32_t reserved_2; // arg3
};
_Static_assert(sizeof(struct mbfs_find_homonym_sibling_by_name_extension_response) == 12, "");

struct mbfs_read_file_request {
	uint32_t file_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t reserved_0; // arg2
	uint32_t reserved_1; // arg3
};
_Static_assert(sizeof(struct mbfs_read_file_request) == 12, "");

struct mbfs_read_file_response {
	uint32_t reserved_0 : 29; // arg1[28:00]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t value_0; // arg2
	uint32_t value_1; // arg3
};
_Static_assert(sizeof(struct mbfs_read_file_response) == 12, "");

struct mbfs_write_file_request {
	uint32_t file_handle : 28; // arg1[27:00]
	uint32_t command_idx : 4; // arg1[31:28]
	uint32_t value_0; // arg2
	uint32_t value_1; // arg3
	// uint8_t value_0[8];
};
_Static_assert(sizeof(struct mbfs_write_file_request) == 12, "");

struct mbfs_write_file_response {
	uint32_t reserved_0 : 29; // arg1[28:00]
	uint32_t error_code : 3; // arg1[31:29]
	uint32_t reserved_1; // arg2
	uint32_t reserved_2; // arg3
};
_Static_assert(sizeof(struct mbfs_write_file_response) == 12, "");

#endif
