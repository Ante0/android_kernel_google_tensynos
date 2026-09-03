// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/string.h>
#include <perf/mbfs.h>

#include "mbfs_base.h"

#define PR_ALERT(fmt, ...) pr_alert("%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PR_ERR(fmt, ...) pr_err("%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PR_INFO(fmt, ...) pr_info("%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PR_DBG(fmt, ...) pr_dbg("%s:%d" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define DEV_ALERT(dev, fmt, ...) dev_alert(dev, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DEV_ERR(dev, fmt, ...) dev_err(dev, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DEV_INFO(dev, fmt, ...) dev_info(dev, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DEV_DBG(dev, fmt, ...) dev_dbg(dev, fmt, ##__VA_ARGS__)

#define TOP_FOLDER_IN_FIRMWARE_DIR "mbfs_root"

// The kobject and inode associated with the top folder under /sys/firmware/
static struct kobject *top_kobj_in_firmware;
static struct inode *top_inode;

static DEFINE_MUTEX(mbfs_cherry_pick_mutex);

/*
 * This is the node in the tree we created to support query. The reason we don't use the kobject
 * tree is because the kobject tree is not supposed to be directly used by drivers.
 */
struct tree_node {
	/*
	 * Exposing the mbfs_handle_t type here simplifies the implementation. While using a pointer
	 * would hide its internal structure, the added complexity is unnecessary as our code only
	 * uses mbfs_handle_t as a numerical identifier and does not access its members.
	 */
	union mbfs_client_handle h;
	struct mbfs_client_node_desc desc;
	// This list is only modified during probe, remove, refresh and cherry-pick
	// (in sysfs_ops store function), any of which inherently ensures exclusiveness, so no
	// lock is needed.
	struct list_head children; // Head used to contain children nodes.
	struct list_head child_entry; // Used as a node for iteration in parent list.
};

/**
 * @brief The structure holding the data needed to create a folder in sysfs.
 *
 */
struct mbfs_folder {
	struct device *dev;
	struct kobject base_kobj;
	struct tree_node tn;
};

/**
 * @brief The structure holding the data needed to create a file in sysfs.
 *
 */
struct mbfs_file {
	struct attribute base_attr;
	struct tree_node ftn;
};

static struct inode *find_inode_from_kobject(struct kobject *kobj)
{
	char *rel_path = NULL;
	char *full_path = NULL;
	int full_path_len;
	struct path sysfs_path;
	struct inode *inode = NULL;
	int ret;

	rel_path = kobject_get_path(kobj, GFP_KERNEL);
	if (!rel_path)
		return NULL;

	full_path_len = strlen("/sys") + strlen(rel_path) + 1;
	full_path = kmalloc(full_path_len, GFP_KERNEL);
	if (!full_path)
		goto exit;

	snprintf(full_path, full_path_len, "/sys%s", rel_path);
	ret = kern_path(full_path, 0, &sysfs_path);
	if (ret == 0) {
		inode = igrab(sysfs_path.dentry->d_inode);
		path_put(&sysfs_path);
	}

	kfree(full_path);
exit:
	kfree(rel_path);
	return inode;
}

static ssize_t mbfs_file_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct mbfs_folder *folder;
	struct mbfs_file *file;
	union val64 file_content;
	enum mbfs_file_value_type type;
	enum mbfs_error_code mbfs_ret;
	int ret;

	folder = container_of(kobj, struct mbfs_folder, base_kobj);
	file = container_of(attr, struct mbfs_file, base_attr);

	mbfs_ret = mbfs_read_file(file->ftn.h, &file_content);
	if (mbfs_ret) {
		struct device *dev = folder->dev;

		DEV_ERR(dev, "Error reading file %s: %s\n", file->ftn.desc.name,
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	type = file->ftn.desc.value_type;
	switch (type) {
	case MBFS_UINT64:
		ret = sysfs_emit(buf, "%llu\n", file_content.number);
		break;
	case MBFS_INT64:
		ret = sysfs_emit(buf, "%lld\n", file_content.number);
		break;
	case MBFS_SHORT_STRING:
		ret = sysfs_emit(buf, "%.8s\n", file_content.text);
		break;
	case MBFS_DOUBLE:
		/* TODO(b/356220299): Add basic string to double converter */
	default: {
		struct device *dev = folder->dev;

		DEV_ERR(dev, "Unknown file type: %s\n", get_mbfs_file_value_type_string(type));
		return -EINVAL;
	}
	}

	return ret;
}

// count doesn't include the null character.
static ssize_t mbfs_file_store(struct kobject *kobj, struct attribute *attr, const char *buf,
			       size_t count)
{
	struct mbfs_folder *folder;
	struct mbfs_file *file;
	union val64 file_content = { 0 };
	struct device *dev;
	enum mbfs_file_value_type type;
	enum mbfs_error_code mbfs_ret;
	int ret;
	int len_to_copy = count;

	folder = container_of(kobj, struct mbfs_folder, base_kobj);
	file = container_of(attr, struct mbfs_file, base_attr);
	dev = folder->dev;

	type = file->ftn.desc.value_type;
	switch (type) {
	case MBFS_UINT64:
		ret = kstrtoull(buf, 0, &file_content.number);
		if (ret) {
			DEV_ERR(dev, "Error converting %s to an unsigned long long\n", buf);
			return ret;
		}
		break;
	case MBFS_INT64:
		ret = kstrtoll(buf, 0, &file_content.number);
		if (ret) {
			DEV_ERR(dev, "Error converting %s to a long long\n", buf);
			return ret;
		}
		break;
	case MBFS_SHORT_STRING:
		// Exclude the line feed in the end if 'echo' is used instead of
		// 'echo -n' (meaning do not output the trailing newline)
		if (count > MBFS_PAYLOAD_BYTES + 1) {
			DEV_ERR(dev, "input string size %lu is larger than %d\n", count,
				MBFS_PAYLOAD_BYTES + 1);
			return -EINVAL;
		} else if (count == MBFS_PAYLOAD_BYTES + 1) {
			if (buf[MBFS_PAYLOAD_BYTES] != '\n') {
				DEV_ERR(dev,
					"the last character isn't line feed, causing input string size larger than %d\n",
					MBFS_PAYLOAD_BYTES);
				return -EINVAL;
			}
		}
		if (buf[count - 1] == '\n')
			len_to_copy--;
		memcpy(file_content.text, buf, len_to_copy);
		break;
	case MBFS_DOUBLE: /* TODO(b/356220299): Add basic string to double converter */
	default:
		DEV_ERR(dev, "Unknown file type: %s\n", get_mbfs_file_value_type_string(type));
		return -EINVAL;
	}

	mbfs_ret = mbfs_write_file(file->ftn.h, file_content);
	if (mbfs_ret) {
		DEV_ERR(dev, "Error writing file %s: %s\n", file->ftn.desc.name,
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	return count;
}

static const struct sysfs_ops mbfs_sysfs_ops = {
	.show = mbfs_file_show,
	.store = mbfs_file_store,
};

static void mbfs_get_ownership(const struct kobject *kobj, kuid_t *uid, kgid_t *gid)
{
	/*
	 * Newly created folders/files in mbfs inherit uid and gid
	 * from the TOP_FOLDER_IN_FIRMWARE_DIR.
	 */
	if (top_inode) {
		*uid = top_inode->i_uid;
		*gid = top_inode->i_gid;
	} else {
		*uid = GLOBAL_ROOT_UID;
		*gid = GLOBAL_ROOT_GID;
	}
}

static const struct kobj_type folder_ktype = {
	.release = NULL, /* because devres_alloc is used when creating the folder */
	.sysfs_ops = &mbfs_sysfs_ops,
	.get_ownership = mbfs_get_ownership,
};

static void mbfs_delete_sysfs_folder(struct device *dev, void *res)
{
	struct mbfs_folder *folder = res;

	DEV_DBG(dev, "Deleting obj %s : %p\n", folder->tn.desc.name, folder);
	kobject_put(&folder->base_kobj);
}

/// @brief Create a sysfs folder.
/// @param parent_kobj the parent kobject under which to create the incoming folder.
/// @param child_handle the handle of the incoming folder.
/// @param child_desc the description of the incoming folder.
/// @param dev the device used for log printing.
/// @param new_folder The newly created folder.
/// @return 0 when successful, error value otherwise.
/* Because we use managed device resource, there is no need to release the memory manually */
static int mbfs_create_sysfs_folder(struct kobject *parent_kobj,
				    union mbfs_client_handle child_handle,
				    struct mbfs_client_node_desc *child_desc, struct device *dev,
				    struct mbfs_folder **new_folder, bool is_root)
{
	int ret;
	struct mbfs_folder *parent_folder;

	/* If devm_kzalloc() is used here, we still need to call kobject_put() manually.
	 * In contrast, with devres_alloc(),  kobject_put() is called automatically in the release
	 * function.
	 */
	struct mbfs_folder *folder =
		devres_alloc(mbfs_delete_sysfs_folder, sizeof(*folder), GFP_KERNEL);

	DEV_DBG(dev, "Allocating obj %s : %p\n", child_desc->name, folder);

	ret = kobject_init_and_add(&folder->base_kobj, &folder_ktype, parent_kobj,
				   child_desc->name);
	if (ret) {
		DEV_ERR(dev, "Error calling kobject_init_and_add(): %d\n", ret);
		kobject_put(&folder->base_kobj);
		devres_free(folder);
		return ret;
	}
	*new_folder = folder;

	folder->dev = dev;
	folder->tn.desc = *child_desc;
	folder->tn.h = child_handle; /* currently not used */
	INIT_LIST_HEAD(&folder->tn.children);
	INIT_LIST_HEAD(&folder->tn.child_entry);

	if (!is_root) {
		parent_folder = container_of(parent_kobj, struct mbfs_folder, base_kobj);
		list_add_tail(&folder->tn.child_entry, &parent_folder->tn.children);
	}

	devres_add(dev, folder);

	return ret;
}

/// @brief Check whether a child (folder or file) with a certain name exists under a folder.
/// @param folder_tn the tree_node of the folder
/// @param child_name_to_find the name of the child
/// @return child position if exists, NULL otherwhise.
static struct tree_node *mbfs_find_tree_node_child(struct tree_node *folder_tn,
						   const char *child_name_to_find)
{
	struct tree_node *child = NULL;

	list_for_each_entry(child, &folder_tn->children, child_entry) {
		if (strcmp(child->desc.name, child_name_to_find) == 0)
			return child;
	}

	return NULL;
}

/// @brief Create a sysfs folder if it doesn't exist
/// @param parent_kobj the parent kobject under which to create the incoming folder.
/// @param child_handle the handle of the incoming folder.
/// @param new_desc the description of the incoming folder.
/// @param dev the device used for log printing.
/// So it could be a sibling to the incoming folder. When NULL, it indicates that the incoming
/// folder is a root folder.
/// @param child_folder The newly created folder.
/// @return 0 when successful, error value otherwise.
static int mbfs_refresh_sysfs_folder(struct mbfs_folder *parent_folder,
				     union mbfs_client_handle child_handle,
				     struct mbfs_client_node_desc *new_desc, struct device *dev,
				     struct mbfs_folder **child_folder)
{
	int ret = 0;
	struct tree_node *node = mbfs_find_tree_node_child(&parent_folder->tn, new_desc->name);

	if (node == NULL) {
		ret = mbfs_create_sysfs_folder(&parent_folder->base_kobj, child_handle, new_desc,
					       dev, child_folder, false);
		if (ret) {
			DEV_ERR(dev,
				"Error calling mbfs_create_sysfs_folder for folder child: name: %s, err: %d\n",
				new_desc->name, ret);
			return ret;
		}
	} else {
		*child_folder = container_of(node, struct mbfs_folder, tn);
	}

	return ret;
}

/// @brief Create a sysfs file.
/// @param parent_folder the parent mbfs_folder under which to create the incoming file.
/// @param child_handle the handle of the incoming file.
/// @param child_desc the description of the incoming file.
/// @param dev the device used for log printing.
/// @return 0 when successful, error value otherwise.
static int mbfs_create_sysfs_file(struct mbfs_folder *parent_folder,
				  union mbfs_client_handle child_handle,
				  struct mbfs_client_node_desc *child_desc, struct device *dev)
{
	int ret;
	umode_t _mode = 0;
	/* Because of devm_kzalloc, memory is released automatically when the device is removed */
	struct mbfs_file *file = devm_kzalloc(dev, sizeof(*file), GFP_KERNEL);

	DEV_DBG(dev, "Allocating attr %s : %p\n", child_desc->name, file);
	if (child_desc->read_permission)
		_mode |= 0400;
	if (child_desc->write_permission)
		_mode |= 0200;
	file->base_attr.mode = _mode;

	// Copy the node description to durable memory space first so that base_attr.name always
	// points to legitimate content.
	file->ftn.desc = *child_desc;
	file->base_attr.name = file->ftn.desc.name;

	sysfs_attr_init(&file->base_attr);
	/* no need to call sysfs_remove_file() in device removal because kobject_put() removes
	 * all the files recursively for the whole directory.
	 */
	ret = sysfs_create_file(&parent_folder->base_kobj, &file->base_attr);
	if (ret) {
		DEV_ERR(dev, "Create sysfs file failed: %s\n", child_desc->name);
		devm_kfree(dev, file);
		return ret;
	}

	file->ftn.h = child_handle;
	INIT_LIST_HEAD(&file->ftn.children);
	INIT_LIST_HEAD(&file->ftn.child_entry);

	list_add_tail(&file->ftn.child_entry, &parent_folder->tn.children);

	return ret;
}

/// @brief Create a sysfs file if it doesn't exist, otherwise update its permissions if needed.
/// @param parent_folder the parent mbfs_folder under which to create the incoming file.
/// @param child_handle the handle of the incoming file.
/// @param new_desc the description of the incoming file.
/// @param dev the device used for log printing.
/// @return 0 when successful, error value otherwise.
static int mbfs_refresh_sysfs_file(struct mbfs_folder *parent_folder,
				   union mbfs_client_handle child_handle,
				   struct mbfs_client_node_desc *new_desc, struct device *dev)
{
	struct mbfs_file *child_file;
	struct tree_node *node;
	int ret = 0;

	node = mbfs_find_tree_node_child(&parent_folder->tn, new_desc->name);
	if (node) {
		child_file = container_of(node, struct mbfs_file, ftn);

		if (child_file->ftn.desc.read_permission != new_desc->read_permission ||
		    child_file->ftn.desc.write_permission != new_desc->write_permission) {
			umode_t _mode = 0;

			if (new_desc->read_permission)
				_mode |= 0400;
			if (new_desc->write_permission)
				_mode |= 0200;

			// This requires child_file->base_attr.name to be valid
			ret = sysfs_chmod_file(&parent_folder->base_kobj, &child_file->base_attr,
					       _mode);
			if (ret) {
				DEV_ERR(dev,
					"Error calling sysfs_chmod_file for file child: name: %s, err: %d\n",
					child_file->ftn.desc.name, ret);
				return ret;
			}

			child_file->ftn.desc = *new_desc;
		}
	} else {
		ret = mbfs_create_sysfs_file(parent_folder, child_handle, new_desc, dev);
		if (ret) {
			DEV_ERR(dev,
				"Error calling mbfs_create_sysfs_file for file child: name: %s, err: %d\n",
				new_desc->name, ret);
			return ret;
		}
	}

	return ret;
}

static int mbfs_get_nth_child_desc(struct device *dev, union mbfs_client_handle handle,
				   struct kobject *parent_kobj,
				   enum mbfs_node_type node_type, uint32_t n,
				   union mbfs_client_handle *child_handle,
				   struct mbfs_client_node_desc *child_desc)
{
	enum mbfs_error_code mbfs_ret;

	mbfs_ret = mbfs_get_nth_child(handle, node_type, n, child_handle);
	if (mbfs_ret) {
		DEV_ERR(dev, "Error calling get_nth_child for file child %d: %s\n", n,
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	mbfs_ret = mbfs_get_node_desc(*child_handle, child_desc);
	if (mbfs_ret) {
		char *parent_path;

		parent_path = kobject_get_path(parent_kobj, GFP_KERNEL);
		if (!parent_path) {
			/* This can happen only if there is no memory to allocate or some kobj
			 * in path has no name
			 */
			DEV_ERR(dev,
				"Error calling kobject_get_path for: %s, mbfs get node ret: %s\n",
				(char *)parent_kobj->name, get_mbfs_error_string(mbfs_ret));
			return mbfs_error2linux(mbfs_ret);
		}
		switch (mbfs_ret) {
		case MBFS_FORBIDDEN:
		case MBFS_NOT_FOUND:
			DEV_DBG(dev,
				"Error mbfs_get_node_desc failed for child: %d, parent path: %s, err: %s\n",
				n, parent_path, get_mbfs_error_string(mbfs_ret));
			break;
		default:
			DEV_ERR(dev,
				"Error calling mbfs_get_node_desc for child: %d, parent path: %s, err: %s\n",
				n, parent_path, get_mbfs_error_string(mbfs_ret));
		}

		kfree(parent_path);

		return mbfs_error2linux(mbfs_ret);
	}

	return 0;
}

/*
 * Map all the folders and files that can be successfully mapped even if some errors are
 * encountered.
 *
 * Assume the relative order of children (files and directories) within an MBFS directory is
 * preserved across refreshes.
 *
 * No error handling is needed because both mbfs_create_sysfs_file() and mbfs_create_sysfs_folder()
 * use managed device resource.
 */
static void refresh_map_mbfs_to_sysfs(struct mbfs_folder *folder, struct device *dev)
{
	union mbfs_client_handle handle = folder->tn.h;
	struct mbfs_client_node_desc *desc = &folder->tn.desc;
	union mbfs_client_handle child_handle;
	struct mbfs_client_node_desc child_desc;
	int ret;

	for (int i = 0; i < desc->num_files; i++) {
		ret = mbfs_get_nth_child_desc(dev, handle, &folder->base_kobj, MBFS_FILE, i,
					      &child_handle, &child_desc);
		if (ret)
			continue;

		ret = mbfs_refresh_sysfs_file(folder, child_handle, &child_desc, dev);
		if (ret) {
			DEV_ERR(dev,
				"Error calling mbfs_refresh_sysfs_file for file child: %d, name: %s, err: %d\n",
				i, child_desc.name, ret);
			continue;
		}
	}

	for (int i = 0; i < desc->num_subfolders; i++) {
		struct mbfs_folder *child_folder;

		ret = mbfs_get_nth_child_desc(dev, handle, &folder->base_kobj, MBFS_FOLDER, i,
					      &child_handle, &child_desc);
		if (ret)
			continue;

		ret = mbfs_refresh_sysfs_folder(folder, child_handle, &child_desc, dev,
						&child_folder);
		if (ret) {
			DEV_ERR(dev,
				"Error calling mbfs_refresh_sysfs_folder for folder child: %d, name: %s, err: %d\n",
				i, child_desc.name, ret);
			continue;
		}

		refresh_map_mbfs_to_sysfs(child_folder, dev);
	}
}

/// @brief Map a specific MBFS path to sysfs.
/// @param dev the device used for log printing.
/// @param path the path to be mapped.
/// @param parent_folder the parent mbfs_folder under which to create the incoming folder.
/// @return 0 on success, error otherwhise.
static int
mbfs_map_path_to_sysfs(struct device *dev, const char *path,
		       struct mbfs_folder *parent_folder) // assume last param is not null
{
	union mbfs_client_handle child_handle;
	struct mbfs_client_node_desc child_desc;
	int error_code;
	struct mbfs_folder *new_folder;
	struct tree_node *child_node;
	char current_token[MAX_MBFS_NAME_LENGTH + 1] = { 0 };
	const char *next_token = path;

	if (!mbfs_get_next_token(next_token, current_token, sizeof(current_token), &next_token)) {
		DEV_ERR(dev, "Error: while obtaining token from path. %s\n", path);
		return -EINVAL;
	}

	/* existing nodes */
	child_node = mbfs_find_tree_node_child(&parent_folder->tn, current_token);
	if (child_node != NULL) {
		/* found existing node */
		if (!mbfs_is_next_token_valid(next_token)) {
			if (child_node->desc.node_type == MBFS_FOLDER) {
				refresh_map_mbfs_to_sysfs(
					container_of(child_node, struct mbfs_folder, tn), dev);
			}
		} else {
			mbfs_map_path_to_sysfs(dev, next_token,
					       container_of(child_node, struct mbfs_folder, tn));
		}
		return 0;
	}

	error_code = mbfs_get_child_by_name(parent_folder->tn.h, current_token, &child_handle);
	if (error_code != MBFS_OK) {
		DEV_ERR(dev, "Error: while obtaining handle for node: %s err code: %d\n",
			current_token, error_code);
		return -ENOENT;
	}

	error_code = mbfs_get_node_desc(child_handle, &child_desc);
	if (error_code != MBFS_OK) {
		DEV_ERR(dev, "Error: Obtaining child description for: %s\n", current_token);
		return -ENOENT;
	}

	switch (child_desc.node_type) {
	case MBFS_FILE:
		error_code = mbfs_create_sysfs_file(parent_folder, child_handle, &child_desc, dev);
		if (error_code) {
			DEV_ERR(dev, "Error while creating mbfs FILE %s\n", child_desc.name);
			return error_code;
		}
		break;
	case MBFS_FOLDER:
		error_code = mbfs_create_sysfs_folder(&parent_folder->base_kobj, child_handle,
						      &child_desc, dev, &new_folder, false);
		if (error_code) {
			DEV_ERR(dev, "Error while creating mbfs FOLDER %s\n", child_desc.name);
			return error_code;
		}
		if (mbfs_is_next_token_valid(next_token))
			mbfs_map_path_to_sysfs(dev, next_token, new_folder);
		else
			refresh_map_mbfs_to_sysfs(new_folder, dev);
		break;
	default:
		// Not possible to get here
		DEV_ALERT(dev, "MBFS Child node of unknown type.\n");
		return -EFAULT;
	}
	return 0;
}

static int create_root_mbfs_folder(struct device *dev, struct mbfs_folder **ret_root_folder,
				   const char *path_string)
{
	enum mbfs_error_code mbfs_ret;
	union mbfs_client_handle top_folder_h;
	struct mbfs_client_node_desc root_desc;
	int ret;

	mbfs_ret = mbfs_get_handle(path_string, &top_folder_h);
	if (mbfs_ret) {
		DEV_ERR(dev, "Failed to get root handle for %s: %s\n", path_string,
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	mbfs_ret = mbfs_get_node_desc(top_folder_h, &root_desc);
	if (mbfs_ret) {
		DEV_ERR(dev, "Error calling mbfs_get_node_desc for root: %s\n",
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	if (!strlen(root_desc.name)) {
		int len_w_nil, count;
		char *pos;

		// There is no leading '/'
		pos = strchr(path_string, '/');
		// Because strscpy makes sure the destination buffer is always nil terminated,
		// we won't copy too many.
		if (!pos)
			count = strlen(path_string) + 1;
		else
			count = pos - path_string + 1;

		len_w_nil = sizeof(root_desc.name) < count ? sizeof(root_desc.name) : count;
		strscpy(root_desc.name, path_string, len_w_nil);
		DEV_INFO(dev, "The deduced mbfs root folder name in sysfs: %s\n", root_desc.name);
	}

	ret = mbfs_create_sysfs_folder(top_kobj_in_firmware, top_folder_h, &root_desc, dev,
				       ret_root_folder, true);
	if (ret) {
		DEV_ERR(dev, "Failed to create MBFS root folder\n");
		return ret;
	}

	return 0;
}

/* Module attributes */
static ssize_t cherry_pick_store(struct kobject *kobj, struct kobj_attribute *attr,
				 const char *path, size_t count)
{
	int ret = 0;
	struct mbfs_client_backend *backend = NULL;
	struct mbfs_folder *root_folder;
	char current_token[MAX_MBFS_NAME_LENGTH + 1] = { 0 };
	const char *next_token = path;

	if (!mbfs_get_next_token(next_token, current_token, sizeof(current_token), &next_token)) {
		PR_ERR("Error: while obtaining token from path. %s\n", path);
		ret = -EINVAL;
		goto exit;
	}

	backend = mbfs_find_backend(current_token);

	if (backend == NULL) {
		PR_ERR("Cherry-picking backend \"%s\" failed, no such backend", path);
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&mbfs_cherry_pick_mutex);

	if (top_inode == NULL)
		top_inode = find_inode_from_kobject(top_kobj_in_firmware);

	if (backend->root_folder == NULL) {
		ret = create_root_mbfs_folder(backend->dev, &root_folder, current_token);
		if (ret != 0) {
			PR_ERR("Error while cherry-picking root folder not mapped %d\n", ret);
			goto unlock;
		}
		backend->root_folder = root_folder;
	}

	if (mbfs_is_next_token_valid(next_token)) {
		ret = mbfs_map_path_to_sysfs(backend->dev, next_token, backend->root_folder);
		if (ret)
			PR_ERR("Error while cherry-picking %d\n", ret);
	} else {
		refresh_map_mbfs_to_sysfs(backend->root_folder, backend->dev);
	}

unlock:
	mutex_unlock(&mbfs_cherry_pick_mutex);

exit:
	if (ret == 0)
		ret = count;

	return ret;
}

static struct kobj_attribute cherry_pick_attribute = __ATTR_WO(cherry_pick);

static struct attribute *mbfs_top_folder_attrs[] = { &cherry_pick_attribute.attr, NULL };

static struct attribute_group mbfs_top_folder_attr_group = {
	.attrs = mbfs_top_folder_attrs,
};

static int __init mbfs_init(void)
{
	int ret = 0;

	/*
	 * Create a directory under /sys/firmware/ .
	 */
	top_kobj_in_firmware = kobject_create_and_add(TOP_FOLDER_IN_FIRMWARE_DIR, firmware_kobj);
	if (!top_kobj_in_firmware) {
		PR_ERR("mbfs: Failed to create the mbfs folder\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(top_kobj_in_firmware, &mbfs_top_folder_attr_group);
	if (ret) {
		PR_ERR("Failed to add mbfs attribute group.");
		kobject_put(top_kobj_in_firmware);
	}

	return ret;
}

static void __exit mbfs_exit(void)
{
	iput(top_inode);
	sysfs_remove_group(top_kobj_in_firmware, &mbfs_top_folder_attr_group);
	kobject_put(top_kobj_in_firmware);
}

module_init(mbfs_init);
module_exit(mbfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vincent Palomares <paillon@google.com>");
MODULE_AUTHOR("Yong Zhao <yozhao@google.com>");
MODULE_DESCRIPTION("MailBox File System API");
