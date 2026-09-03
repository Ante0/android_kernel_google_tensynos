// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <perf/mbfs.h>

#include "mbfs_base.h"
#include "mbfs_backend.h"
#include "mbfs_protocol.h"

// The code supports dealing with multiple firmware backends
#define MBFS_CLIENT_MAX_BACKENDS 15

// Variables required to be defined by mbfs library
static struct mbfs_client_backend *mbfs_client_backends[MBFS_CLIENT_MAX_BACKENDS];
static DEFINE_MUTEX(mbfs_client_backends_mutex);

#define REQUIRED_MAJOR_MBFS_VERSION 0
#define REQUIRED_MIN_MINOR_MBFS_VERSION 1

int mbfs_error2linux(enum mbfs_error_code error)
{
	switch (error) {
	case MBFS_OK:
		return 0;
	case MBFS_UNRECOGNIZED_COMMAND:
		return -EBADMSG;
	case MBFS_NOT_FOUND:
		return -ENOENT;
	case MBFS_FORBIDDEN:
		return -EACCES;
	case MBFS_ILLEGAL_OPERATION:
		return -EPERM;
	case MBFS_TRY_AGAIN:
		return -EAGAIN;
	case MBFS_CUSTOM_FILE_ERROR:
		return -ECANCELED;
	case MBFS_COMMUNICATION_FAILURE:
		return -ECOMM;
	case MBFS_PROBE_DEFER:
		return -EPROBE_DEFER;
	case MBFS_INVALID_ARG:
		return -EINVAL;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(mbfs_error2linux);

static const char *mbfs_error_strings[NUM_MBFS_ERROR_CODES] = {
	[MBFS_OK] = "OK",
	[MBFS_UNRECOGNIZED_COMMAND] = "Unknown cmd",
	[MBFS_NOT_FOUND] = "Not found",
	[MBFS_FORBIDDEN] = "Forbidden",
	[MBFS_ILLEGAL_OPERATION] = "Illegal op",
	[MBFS_TRY_AGAIN] = "Try again",
	[MBFS_CUSTOM_FILE_ERROR] = "Custom file error",
	[MBFS_COMMUNICATION_FAILURE] = "Communication failure",
	[MBFS_PROBE_DEFER] = "Probe defer",
	[MBFS_INVALID_ARG] = "Invalid arg",
};

const char *get_mbfs_error_string(enum mbfs_error_code code)
{
	if (code >= NUM_MBFS_ERROR_CODES)
		return "Unknown error";

	return mbfs_error_strings[code];
}
EXPORT_SYMBOL_GPL(get_mbfs_error_string);

static const char *mbfs_file_value_type_strings[NUM_MBFS_VALUE_TYPES + 1] = {
	[MBFS_UINT64] = "uint64",	    [MBFS_INT64] = "int64",
	[MBFS_DOUBLE] = "double",	    [MBFS_SHORT_STRING] = "string",
	[NUM_MBFS_VALUE_TYPES] = "Unknown",
};

const char *get_mbfs_file_value_type_string(enum mbfs_file_value_type type)
{
	if (type > NUM_MBFS_VALUE_TYPES)
		type = NUM_MBFS_VALUE_TYPES;

	return mbfs_file_value_type_strings[type];
}
EXPORT_SYMBOL_GPL(get_mbfs_file_value_type_string);

// This is currently called inside mbfs_client_backends_mutex for writing.
static int initialize_mbfs_backend(struct mbfs_client_backend *backend, int index)
{
	int err;

	if (backend->initialized)
		return -EPERM;

	if (index < 0 || index >= MBFS_CLIENT_MAX_BACKENDS)
		return -EINVAL;

	if (backend->submit_request == NULL)
		return -EINVAL;

	struct mbfs_protocol_version_request protocol_req = { .command_idx =
								      MBFS_GET_PROTOCOL_VERSION };
	struct mbfs_protocol_version_response protocol_resp;

	err = backend->submit_request((struct mbfs_request *)&protocol_req,
				      (struct mbfs_response *)&protocol_resp);
	if (err)
		return err;

	if (protocol_resp.error_code != MBFS_OK)
		// Failed to retrieve protocol version for backend!
		return mbfs_error2linux(protocol_resp.error_code);

	if (protocol_resp.major_version != REQUIRED_MAJOR_MBFS_VERSION ||
	    protocol_resp.minor_version < REQUIRED_MIN_MINOR_MBFS_VERSION)
		// Incompatible protocol version for backend!
		return -EPERM;

	struct mbfs_get_root_handle_request root_req = { .command_idx = MBFS_GET_ROOT_HANDLE };
	struct mbfs_get_root_handle_response root_resp;

	err = backend->submit_request((struct mbfs_request *)&root_req,
				      (struct mbfs_response *)&root_resp);
	if (err)
		return err;

	if (root_resp.error_code != MBFS_OK)
		// Failed to retrieve root handle for backend!
		return mbfs_error2linux(root_resp.error_code);

	backend->idx_registered = index;
	backend->root_handle.backend_idx = index;
	backend->root_handle.remote_handle = root_resp.handle;
	backend->bits_file_index = protocol_resp.file_index_len;
	backend->initialized = true;

	return 0;
}

/**
 * @brief Register a mbfs backend.
 *
 * @param backend: the backend to be registered
 * @return int: return 0 when successful.
 * return -EEXIST if a backend with the same name has been registered previously.
 * return -ENOENT if no slot can be found for a new backend.
 */
int register_mbfs_backend(struct mbfs_client_backend *backend)
{
	int i;
	int candidate = -1;
	int ret = 0;

	mutex_lock(&mbfs_client_backends_mutex);
	for (i = 0; i < MBFS_CLIENT_MAX_BACKENDS; i++) {
		if (!mbfs_client_backends[i]) {
			if (candidate < 0)
				candidate = i;

			continue;
		}

		if (!strcmp(mbfs_client_backends[i]->name, backend->name)) {
			mutex_unlock(&mbfs_client_backends_mutex);
			return -EEXIST;
		}
	}

	if (candidate < 0) {
		mutex_unlock(&mbfs_client_backends_mutex);
		return -ENOSPC;
	}

	ret = initialize_mbfs_backend(backend, candidate);

	backend->root_folder = NULL;

	if (!ret)
		mbfs_client_backends[candidate] = backend;

	mutex_unlock(&mbfs_client_backends_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(register_mbfs_backend);

/**
 * @brief Unregister a mbfs backend. This function is designed for use exclusively during
 * development, where it is ensured that all backend consumers have been unbinded or removed
 * beforehand. This allows us to focus solely on protecting the slots in the
 * mbfs_client_backends. Synchronization concerns related to the backend and its consumers
 * can be safely disregarded.
 *
 * @param backend: the backend to be unregistered
 */
void unregister_mbfs_backend(struct mbfs_client_backend *backend)
{
	mutex_lock(&mbfs_client_backends_mutex);
	mbfs_client_backends[backend->idx_registered] = NULL;
	mutex_unlock(&mbfs_client_backends_mutex);
}
EXPORT_SYMBOL_GPL(unregister_mbfs_backend);

// Retrieve the next 'token' in a path (i.e. the next folder or file name).
// Returns false if the name is illegally long, or no new token can be extracted.
bool mbfs_get_next_token(const char *path, char *new_token, int new_token_length,
			 const char **next_token)
{
	int i = 0;
	int j = 0;
	bool token_started = false;

	if (new_token_length < MAX_MBFS_NAME_LENGTH + 1 || path[0] == '\0')
		return false;

	new_token[0] = '\0';

	while (j < new_token_length) {
		if (path[i] == '\0' || path[i] == '\n')
			break;

		if (path[i] == '/') {
			i++;
			if (!token_started)
				continue;
			else
				break;
		}
		token_started = true;
		new_token[j++] = path[i++];
	}

	if (j == new_token_length)
		return false;

	new_token[j] = '\0';
	*next_token = path + i;

	return new_token[0] != '\0';
}

bool mbfs_is_next_token_valid(const char *path)
{
	if (path[0] == '\0' || path[0] == '/' || path[0] == '\n')
		return false;
	return true;
}

// Funnel mbfs requests to the appropriate backend callback.
static enum mbfs_error_code mbfs_submit_request(uint32_t backend_index,
						const struct mbfs_request *req,
						struct mbfs_response *resp)
{
	int err;

	if (backend_index >= MBFS_CLIENT_MAX_BACKENDS)
		return MBFS_NOT_FOUND;

	err = mbfs_client_backends[backend_index]->submit_request(req, resp);
	if (err)
		return MBFS_COMMUNICATION_FAILURE;

	return resp->error_code;
}

// Retrieve the handle of the provided parent node's child whose name matches
// the input.
enum mbfs_error_code mbfs_get_child_by_name(const union mbfs_client_handle parent,
					    const char *child_name, union mbfs_client_handle *child)
{
	uint32_t child_name_length = strlen(child_name);
	char padded_child_name[MAX_MBFS_NAME_LENGTH + 1];
	enum mbfs_error_code error_code;
	union mbfs_client_handle potential_child = { .backend_idx = parent.backend_idx };
	void *tmp;

	if (child_name_length > MAX_MBFS_NAME_LENGTH)
		return MBFS_NOT_FOUND;

	strscpy(padded_child_name, child_name, sizeof(padded_child_name));

	struct mbfs_find_child_by_base_name_request base_name_req = {
		.command_idx = MBFS_FIND_CHILD_BY_BASE_NAME,
		.folder_handle = parent.remote_handle,
	};
	struct mbfs_find_child_by_base_name_response base_name_resp;

	tmp = &base_name_req.base_name_0;
	memcpy(tmp, padded_child_name, 8);

	error_code = mbfs_submit_request(parent.backend_idx, (struct mbfs_request *)&base_name_req,
					 (struct mbfs_response *)&base_name_resp);
	if (error_code != MBFS_OK)
		return error_code;

	potential_child.remote_handle = base_name_resp.handle;

	if (memcmp(padded_child_name + 8, &base_name_resp.name_extension_0, 8) == 0) {
		// Potential child is the real intended child!
		*child = potential_child;
		return MBFS_OK;
	}

	struct mbfs_find_homonym_sibling_by_name_extension_request homonym_req = {
		.command_idx = MBFS_FIND_HOMONYM_SIBLING_BY_NAME_EXTENSION,
		.homonym_handle = potential_child.remote_handle,
	};
	struct mbfs_find_homonym_sibling_by_name_extension_response homonym_resp;

	tmp = &homonym_req.name_extension_0;
	memcpy(tmp, padded_child_name + 8, 8);

	error_code = mbfs_submit_request(parent.backend_idx, (struct mbfs_request *)&homonym_req,
					 (struct mbfs_response *)&homonym_resp);
	if (error_code != MBFS_OK)
		return error_code;

	potential_child.remote_handle = homonym_resp.handle;
	*child = potential_child;

	return MBFS_OK;
}
EXPORT_SYMBOL_GPL(mbfs_get_child_by_name);

// Most convenient method to look up an MBFS node. Given there will be at least
// 1 mailbox transaction for each folder or file in the path (and possibly 2 if
// some of these have names exceeding 8 bytes), retrieved handles should
// ideally be cached by clients for later use.
// Return 0 when successful.
// Return MBFS_PROBE_DEFER if a backend couldn't be found.
// Return MBFS_INVALID_ARG if there is an error with `path`.
// Return the error as is if mbfs protocol returns an error.
enum mbfs_error_code mbfs_get_handle(const char *path, union mbfs_client_handle *handle)
{
	char current_token[MAX_MBFS_NAME_LENGTH + 1] = { 0 };
	const char *next_token = path;
	union mbfs_client_handle tmp_handle = { 0 };
	struct mbfs_client_backend *backend = NULL;

	if (!mbfs_get_next_token(next_token, current_token, sizeof(current_token), &next_token))
		return MBFS_INVALID_ARG;

	backend = mbfs_find_backend(current_token);

	// Assume that the first token is free from user input error. If not found,
	// the user should try again.
	if (backend == NULL)
		return MBFS_PROBE_DEFER;

	tmp_handle = backend->root_handle;

	while (mbfs_get_next_token(next_token, current_token, sizeof(current_token), &next_token)) {
		enum mbfs_error_code error_code;

		error_code = mbfs_get_child_by_name(tmp_handle, current_token, &tmp_handle);
		if (error_code != MBFS_OK)
			return error_code;
	}

	if (next_token[0] != '\0') // The whole path could not be walked
		return MBFS_INVALID_ARG;

	*handle = tmp_handle;

	return MBFS_OK;
}
EXPORT_SYMBOL_GPL(mbfs_get_handle);

// The descriptor collects a lot of information that _should_ not typically
// matter to production users (node name, num children, etc.), or should
// be cached <by users> to avoid being called repeatedly.
enum mbfs_error_code mbfs_get_node_desc(union mbfs_client_handle handle,
					struct mbfs_client_node_desc *desc)
{
	enum mbfs_error_code error_code;
	struct mbfs_client_node_desc tmp = { 0 };

	for (uint32_t i = 0; i < 2; i++) { // Possible 2nd iteration for name extension
		struct mbfs_get_node_descriptor_request req = {
			.command_idx = MBFS_GET_NODE_DESCRIPTOR,
			.node_handle = handle.remote_handle,
			.request_name_extension = i,
		};
		struct mbfs_get_node_descriptor_response resp;

		error_code = mbfs_submit_request(handle.backend_idx, (struct mbfs_request *)&req,
						 (struct mbfs_response *)&resp);

		if (error_code != MBFS_OK)
			return error_code;

		memcpy(tmp.name + i * 8, &resp.base_name_0, 8);
		tmp.value_type = resp.expected_value_type;
		tmp.read_permission = resp.read_permission;
		tmp.write_permission = resp.write_permission;
		tmp.node_type = resp.node_type;

		if (!resp.has_name_extension) {
			// The node's name extension is empty; no need for a 2nd call to retrieve it
			break;
		}
	}

	if (tmp.node_type == MBFS_FOLDER) {
		struct mbfs_get_num_children_request req = {
			.command_idx = MBFS_GET_NUM_CHILDREN,
			.folder_handle = handle.remote_handle,
		};
		struct mbfs_get_num_children_response resp;

		error_code = mbfs_submit_request(handle.backend_idx, (struct mbfs_request *)&req,
						 (struct mbfs_response *)&resp);

		if (error_code != MBFS_OK)
			return error_code;

		tmp.num_subfolders = resp.num_child_folders;
		tmp.num_files = resp.num_child_files;
	}

	*desc = tmp;
	return MBFS_OK;
}
EXPORT_SYMBOL_GPL(mbfs_get_node_desc);

enum mbfs_error_code mbfs_get_nth_child(union mbfs_client_handle handle,
					enum mbfs_node_type node_type, uint32_t n,
					union mbfs_client_handle *child_handle)
{
	// If the target MBFS service advertises a non-0 file index length, we are allowed to
	// compute file handles ourselves, with the guarantee that child handles are identical
	// to their parent's, save for the lowest file_index_len bits (which should instead be set
	// to the file index directly).
	if (node_type == MBFS_FILE && handle.backend_idx < MBFS_CLIENT_MAX_BACKENDS &&
	    mbfs_client_backends[handle.backend_idx]->bits_file_index) {
		uint8_t bits = mbfs_client_backends[handle.backend_idx]->bits_file_index;
		uint32_t file_index_mask = ((1 << bits) - 1);

		if (n >= file_index_mask)
			return MBFS_INVALID_ARG;
		*child_handle = (union mbfs_client_handle){
			.backend_idx = handle.backend_idx,
			.remote_handle = (handle.remote_handle & ~file_index_mask) | n,
		};
		return MBFS_OK;
	}

	enum mbfs_error_code error_code;
	struct mbfs_get_nth_child_request req = {
		.command_idx = MBFS_GET_NTH_CHILD_HANDLE,
		.folder_handle = handle.remote_handle,
		.child_type = node_type,
		.child_index = n,
	};
	struct mbfs_get_nth_child_response resp;

	error_code = mbfs_submit_request(handle.backend_idx, (struct mbfs_request *)&req,
					 (struct mbfs_response *)&resp);

	if (error_code != MBFS_OK)
		return error_code;

	*child_handle = (union mbfs_client_handle){
		.backend_idx = handle.backend_idx,
		.remote_handle = resp.child_handle,
	};

	return MBFS_OK;
}
EXPORT_SYMBOL_GPL(mbfs_get_nth_child);

enum mbfs_error_code mbfs_read_file(union mbfs_client_handle handle, union val64 *output)
{
	enum mbfs_error_code error_code;
	struct mbfs_read_file_request req = {
		.command_idx = MBFS_READ_FILE,
		.file_handle = handle.remote_handle,
	};
	struct mbfs_read_file_response resp;

	error_code = mbfs_submit_request(handle.backend_idx, (struct mbfs_request *)&req,
					 (struct mbfs_response *)&resp);

	if (error_code != MBFS_OK)
		return error_code;

	memcpy(output, &resp.value_0, 8);

	return MBFS_OK;
}
EXPORT_SYMBOL_GPL(mbfs_read_file);

enum mbfs_error_code mbfs_write_file(union mbfs_client_handle handle, union val64 input)
{
	struct mbfs_write_file_request req = {
		.command_idx = MBFS_WRITE_FILE,
		.file_handle = handle.remote_handle,
		.value_0 = input.number & 0xFFFFFFFF,
		.value_1 = input.number >> 32,
	};
	struct mbfs_write_file_response resp;

	return mbfs_submit_request(handle.backend_idx, (struct mbfs_request *)&req,
					 (struct mbfs_response *)&resp);
}
EXPORT_SYMBOL_GPL(mbfs_write_file);

enum mbfs_error_code mbfs_read_child_by_name(union mbfs_client_handle parent_handle,
					     const char *child_name, union val64 *output)
{
	union mbfs_client_handle tmp_handle;
	enum mbfs_error_code error_code;

	error_code = mbfs_get_child_by_name(parent_handle, child_name, &tmp_handle);
	if (error_code != MBFS_OK)
		return error_code;

	return mbfs_read_file(tmp_handle, output);
}
EXPORT_SYMBOL_GPL(mbfs_read_child_by_name);

bool is_mbfs_handle_invalid(union mbfs_client_handle handle)
{
	return handle.backend_idx >= MBFS_CLIENT_MAX_BACKENDS;
}
EXPORT_SYMBOL_GPL(is_mbfs_handle_invalid);

struct mbfs_client_backend *mbfs_find_backend(const char *name)
{
	struct mbfs_client_backend *ret = NULL;

	for (size_t i = 0; i < MBFS_CLIENT_MAX_BACKENDS; i++) {
		if (mbfs_client_backends[i] != NULL &&
		    strcmp(mbfs_client_backends[i]->name, name) == 0) {
			ret = mbfs_client_backends[i];
			break;
		}
	}

	return ret;
}
