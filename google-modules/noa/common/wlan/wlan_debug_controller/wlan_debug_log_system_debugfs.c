#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include <wlan/wlan_rpc_service/noa_wlan_cmd_dispatch.h>
#include "wlan_debug_trace.h"
#include "wlan_debug_controller_client.h"
#include "noa.h"

#define MAX_LOCATION_NAME_LEN 16
#define LOG_ENTRY_SIZE 200

#define WLAN_LOG_CASE_LEVEL_NAME(level)                                                            \
	case LOG_LEVEL_##level:                                                                    \
		return #level

#define WLAN_LOG_CASE_MODULE_NAME(module)                                                          \
	case LOG_MODULE_##module:                                                                  \
		return #module

static const char *get_log_level_name(enum log_level level)
{
	switch (level) {
		WLAN_LOG_CASE_LEVEL_NAME(DISABLE_ALL);
		WLAN_LOG_CASE_LEVEL_NAME(ERROR);
		WLAN_LOG_CASE_LEVEL_NAME(WARN);
		WLAN_LOG_CASE_LEVEL_NAME(INFO);
		WLAN_LOG_CASE_LEVEL_NAME(DEBUG);
	default:
		break;
	}

	return "Unknown";
}

static const char *get_log_module_name(enum log_module module)
{
	switch (module) {
		WLAN_LOG_CASE_MODULE_NAME(DP);
		WLAN_LOG_CASE_MODULE_NAME(BM);
		WLAN_LOG_CASE_MODULE_NAME(SHM);
		WLAN_LOG_CASE_MODULE_NAME(STATS);
		WLAN_LOG_CASE_MODULE_NAME(NEP);
		WLAN_LOG_CASE_MODULE_NAME(RPC);
		WLAN_LOG_CASE_MODULE_NAME(CFG);
		WLAN_LOG_CASE_MODULE_NAME(TXCPL);
		WLAN_LOG_CASE_MODULE_NAME(RXPOST);
		WLAN_LOG_CASE_MODULE_NAME(ISR);
		WLAN_LOG_CASE_MODULE_NAME(NEP_BUFFER_POOL);
		WLAN_LOG_CASE_MODULE_NAME(WDEV);
		WLAN_LOG_CASE_MODULE_NAME(TX);
		WLAN_LOG_CASE_MODULE_NAME(RX);
		WLAN_LOG_CASE_MODULE_NAME(SHELL);
	default:
		break;
	}

	return "Unknown";
}

struct SyncLogEntryHeader {
	uint32_t log_size;
	uint32_t log_idx;
};

struct wlan_dbg_log_system_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct wlan_debug_component_client *, char *);
	ssize_t (*store)(struct wlan_debug_component_client *, const char *, size_t count);
};

static ssize_t wlan_dbg_log_system_dump_config(struct wlan_debug_component_client *client,
					       char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	struct wlan_log_system_control_param *param =
		(struct wlan_log_system_control_param *)(client->param);

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA WLAN Dump Log System Config ====\n");
	cnt += scnprintf(buf + cnt, len - cnt,
			 "==== Module Name ==== Log Level ==== Location ====\n");
	for (i = 0; i < LOG_MODULE_NUM; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "(bit%d)%s\t\t%s\t%s\n", i,
				 get_log_module_name(i), get_log_level_name(param->level[i]),
				 wlan_dbg_get_log_location_name(param->location));
	}
	return cnt;
}

static inline void wlan_dbg_log_system_param_config(struct wlan_debug_component_client *client,
						    const struct wlan_log_system_control_cmd *msg)
{
	int i;
	struct wlan_log_system_control_param *param =
		(struct wlan_log_system_control_param *)(client->param);

	if (msg == NULL) {
		return;
	}

	if (msg->location) {
		param->location = msg->location;
		return;
	}

	if (msg->multi_module_ctrl) {
		memcpy(param->level, msg->modules_level, sizeof(msg->modules_level));
		return;
	}

	for (i = LOG_MODULE_START; i < LOG_MODULE_NUM; i++) {
		if (msg->module >> i & 0x1) {
			if (msg->enable) {
				param->level[i] = msg->level;
			} else {
				param->level[i] = LOG_LEVEL_DISABLE_ALL;
			}
		}
	}
}

static inline int32_t wlan_dbg_log_system_module_names_to_bitmap(char *buf, u32 *output_modules)
{
	int i;
	char *module;

	if (buf == NULL) {
		return -EINVAL;
	}

	*output_modules = 0;
	wlan_dbg_token_to_upper(buf);

	while ((module = strsep(&buf, ",")) != NULL) {
		if (strncmp("ALL", module, 3) == 0) {
			*output_modules = UINT_MAX;
			return 0;
		}

		for (i = LOG_MODULE_START; i < LOG_MODULE_END; i++) {
			if (strncmp(get_log_module_name(i), module,
				    strlen(get_log_module_name(i))) == 0) {
				*output_modules |= (1 << i);
				break;
			}
		}
	}

	return 0;
}

static inline int32_t wlan_dbg_log_system_level_name_to_int(char *buf, u32 *output_level)
{
	int i;

	if (buf == NULL) {
		return -EINVAL;
	}

	wlan_dbg_token_to_upper(buf);

	for (i = LOG_LEVEL_START; i < LOG_LEVEL_END; i++) {
		if (strncmp(get_log_level_name(i), buf, strlen(get_log_level_name(i))) == 0) {
			*output_level = i;
			break;
		}
	}

	return 0;
}

static ssize_t wlan_dbg_log_system_config_write(struct wlan_debug_component_client *client,
						const char *buf, size_t size)
{
	char *token, *value, *saveptr = (char *)kmalloc(size, GFP_KERNEL);
	struct wlan_log_system_control_cmd cmd = {
		.enable = 1, .multi_module_ctrl = 0, .module = UINT_MAX, .level = LOG_LEVEL_DEBUG
	};

	if (saveptr == NULL) {
		return -ENOMEM;
	}
	memcpy(saveptr, buf, size);

	while ((token = strsep(&saveptr, " ")) != NULL) {
		if (token[0] != '-' || !saveptr || saveptr[0] == '-') {
			kfree(saveptr);
			return -EINVAL;
		}

		if (strlen(token) < 2) {
			continue;
		}

		value = strsep(&saveptr, " ");
		if (token[1] == 'e' && value != NULL && value[0] == '0') {
			cmd.enable = 0;
		} else if (token[1] == 'm') {
			if (wlan_dbg_log_system_module_names_to_bitmap(value, &cmd.module) < 0) {
				continue;
			}
		} else if (token[1] == 'l') {
			if (wlan_dbg_log_system_level_name_to_int(value, &cmd.level) < 0) {
				continue;
			}
		}
	}

	kfree(saveptr);

	if (noa_wlan_fw_request_send(NOA_WLAN_CMD_LOG_SYS_CTRL, (void *)&cmd, sizeof(cmd)) == 0) {
		wlan_dbg_log_system_param_config(client, &cmd);
		return size;
	}

	return -EINVAL;
}

static ssize_t wlan_dbg_log_system_location_config_write(struct wlan_debug_component_client *client,
							 const char *buf, size_t size)
{
	char location[MAX_LOCATION_NAME_LEN];
	int i;
	struct wlan_log_system_control_cmd cmd = { .location = UNDEFINED_LOCATION };

	if (sscanf(buf, "%s", location) == 1) {
		wlan_dbg_token_to_upper(location);
		for (i = OUTPUT_LOCATION_DRAM; i < OUTPUT_LOCATION_NUM; i++) {
			if (strncmp(wlan_dbg_get_log_location_name(i), location,
				    strlen(wlan_dbg_get_log_location_name(i))) == 0) {
				cmd.location = i;
				break;
			}
		}
	}

	if (cmd.location == UNDEFINED_LOCATION) {
		return -EINVAL;
	}

	if (noa_wlan_fw_request_send(NOA_WLAN_CMD_LOG_SYS_CTRL, (void *)&cmd, sizeof(cmd)) == 0) {
		wlan_dbg_log_system_param_config(client, &cmd);
		return size;
	}

	return size;
}

static ssize_t wlan_dbg_log_system_dump_location_config(struct wlan_debug_component_client *client,
							char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	struct wlan_log_system_control_param *param =
		(struct wlan_log_system_control_param *)(client->param);

	cnt += scnprintf(buf + cnt, len - cnt, "Output Location: %s\n",
			 wlan_dbg_get_log_location_name(param->location));
	return cnt;
}

static u32 wlan_dbg_log_system_get_next_log(u32 cur_idx, char *log_buf,
					    struct SyncLogEntryHeader *header, u8 **read_pointer)
{
	memcpy(header, *read_pointer, sizeof(struct SyncLogEntryHeader));
	*read_pointer += sizeof(struct SyncLogEntryHeader);

	if (header->log_idx == cur_idx + 1) {
		memcpy(log_buf, *read_pointer, header->log_size);
		*read_pointer += header->log_size;
		return header->log_idx;
	}

	return 0;
}

static ssize_t wlan_dbg_log_system_dump_log(struct wlan_debug_component_client *client, char *buf)
{
	struct wlan_log_system_control_cmd cmd;
	int cnt = 0;
	u32 cur_idx = 1;
	struct SyncLogEntryHeader header;
	char log_buf[LOG_ENTRY_SIZE];
	struct wlan_log_system_control_param *param =
		(struct wlan_log_system_control_param *)(client->param);
	u8 *read_pointer = (u8 *)(client->component_shared_mem_addr);
	u64 addr_boundary = client->component_shared_mem_addr + client->component_shared_mem_size;
	struct wlan_debug_controller_client *ctrl_client = client->ctlr_client;

	// Stop writing from NCP
	cmd.enable = 0;
	if (read_pointer == NULL || client->component_shared_mem_addr == 0 ||
	    noa_wlan_fw_request_send(NOA_WLAN_CMD_LOG_SYS_CTRL, (void *)&cmd, sizeof(cmd)) != 0) {
		return -EINVAL;
	}

	// Get first log entry
	memcpy(&header, read_pointer, sizeof(struct SyncLogEntryHeader));
	read_pointer += sizeof(struct SyncLogEntryHeader);
	memcpy(log_buf, read_pointer, header.log_size);
	read_pointer += header.log_size;
	cur_idx = header.log_idx;

	while ((u64)(read_pointer) + sizeof(struct SyncLogEntryHeader) < addr_boundary) {
		cur_idx =
			wlan_dbg_log_system_get_next_log(cur_idx, log_buf, &header, &read_pointer);
		if (cur_idx == 0) {
			break;
		}

		if (ctrl_client->destination == DBG_CLIENT_TRACE_EVENT) {
			log_buf[header.log_size] = '\0';
			trace_wlan_noa_log(log_buf);
		} else {
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "%s\n", log_buf);
		}
	}

	// Resume writing in NCP
	cmd.multi_module_ctrl = 1;
	memcpy(cmd.modules_level, param->level, sizeof(param->level));

	if (noa_wlan_fw_request_send(NOA_WLAN_CMD_LOG_SYS_CTRL, (void *)&cmd, sizeof(cmd)) != 0) {
		return -EINVAL;
	}

	return cnt;
}

static struct wlan_dbg_log_system_kobj_attr attr_module_config = __ATTR(
	module_config, 0664, wlan_dbg_log_system_dump_config, wlan_dbg_log_system_config_write);
static struct wlan_dbg_log_system_kobj_attr attr_location_config =
	__ATTR(location_config, 0664, wlan_dbg_log_system_dump_location_config,
	       wlan_dbg_log_system_location_config_write);
static struct wlan_dbg_log_system_kobj_attr attr_read_log =
	__ATTR(read_log, 0664, wlan_dbg_log_system_dump_log, NULL);
static struct attribute *default_file_attrs[] = {
	&attr_module_config.attr,
	&attr_location_config.attr,
	&attr_read_log.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t wlan_dbg_log_system_sysfs_show(struct kobject *kobj, struct attribute *attr,
					      char *buf)
{
	struct wlan_dbg_entry *log_system_entry = container_of(kobj, struct wlan_dbg_entry, kobj);
	struct wlan_debug_component_client *client =
		(struct wlan_debug_component_client *)log_system_entry->priv;
	struct wlan_dbg_log_system_kobj_attr *wlan_dbg_log_system_attr =
		container_of(attr, struct wlan_dbg_log_system_kobj_attr, attr);

	if (wlan_dbg_log_system_attr->show)
		return wlan_dbg_log_system_attr->show(client, buf);
	return -EIO;
}

static ssize_t wlan_dbg_log_system_sysfs_store(struct kobject *kobj, struct attribute *attr,
					       const char *buf, size_t count)
{
	struct wlan_dbg_entry *log_system_entry = container_of(kobj, struct wlan_dbg_entry, kobj);
	struct wlan_debug_component_client *client =
		(struct wlan_debug_component_client *)log_system_entry->priv;
	struct wlan_dbg_log_system_kobj_attr *wlan_dbg_log_system_attr =
		container_of(attr, struct wlan_dbg_log_system_kobj_attr, attr);

	if (wlan_dbg_log_system_attr->store)
		return wlan_dbg_log_system_attr->store(client, buf, count);
	return -EIO;
}

static void wlan_dbg_log_system_init_param(struct wlan_debug_component_client *client)
{
	int i;
	struct wlan_log_system_control_param *log_system_param =
		(struct wlan_log_system_control_param *)(client->param);
	log_system_param->location = OUTPUT_LOCATION_UART;
	for (i = LOG_MODULE_START; i < LOG_MODULE_END; i++) {
		log_system_param->level[i] = LOG_LEVEL_ENABLE_ALL;
	}
}

static void wlan_dbg_log_system_entry_register(struct wlan_dbg_entry *log_system_entry,
					       struct noa_wlan_entry *dbg_entry)
{
	int ret;
	ret = kobject_init_and_add(&log_system_entry->kobj, &noa_wlan_dbg_log_system_ktype,
				   &dbg_entry->kobj, "log_sys");
	if (ret) {
		kobject_put(&log_system_entry->kobj);
	}
}

static struct sysfs_ops wlan_dbg_log_system_sysfs_ops = {
	.show = wlan_dbg_log_system_sysfs_show,
	.store = wlan_dbg_log_system_sysfs_store,
};

struct kobj_type noa_wlan_dbg_log_system_ktype = {
	.sysfs_ops = &wlan_dbg_log_system_sysfs_ops,
	.default_groups = default_file_groups,
};

struct wlan_debug_component_client_ops wlan_dbg_log_system_client_ops = {
	.init_param = wlan_dbg_log_system_init_param,
	.entry_register = wlan_dbg_log_system_entry_register,
};
