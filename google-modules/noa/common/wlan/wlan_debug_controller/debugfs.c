#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include "wlan_debug_controller_client.h"
#include "wlan/noa_wlan_client.h"
#include "wlan/noa_wlan_cfg_space.h"
#include "wlan/noa_wlan_buffer_management.h"
#include "noa.h"
#include <wlan/wlan_rpc_service/noa_wlan_cmd_dispatch.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
#include <stdarg.h>
#else
#include <linux/stdarg.h>
#endif

#define MAX_DESTINATION_NAME_LEN 16
#define MEMORY_MAP_SECTION_NAME_LEN 20
#define MAX_MEMORY_MAP_DUMP_ARGV_LEN MEMORY_MAP_SECTION_NAME_LEN + 10

#define WLAN_CLIENT_CASE_DESTINATION_NAME(dest)                                                    \
	case DBG_CLIENT_##dest:                                                                    \
		return #dest

static const char *get_destination_name(enum wlan_debug_client_destination dest)
{
	switch (dest) {
		WLAN_CLIENT_CASE_DESTINATION_NAME(TRACE_EVENT);
		WLAN_CLIENT_CASE_DESTINATION_NAME(SYSFS);
	default:
		break;
	}

	return "Unknown";
}

void wlan_debug_component_client_init_shared_mem_info(struct noa_wlan_client *wlan_client)
{
	int i;
	struct noa_wlan_entry *dbg_entry = (struct noa_wlan_entry *)(wlan_client->dbg_entry);
	struct wlan_debug_controller_client *dbg_client =
		(struct wlan_debug_controller_client *)(dbg_entry->priv);
	struct wlan_debug_component_client *client;

	for (i = DBG_COMPONENT_START; i < DBG_COMPONENT_NUM; i++) {
		client = (struct wlan_debug_component_client *)(dbg_client->component_entries[i]
									->priv);
		if (client == NULL) {
			pr_err("[WLAN] %s(): wlan_debug_component_client is NULL", __func__);
			return;
		}

		switch (i) {
		case DBG_LOG_SYSTEM:
			client->component_shared_mem_addr =
				(u64)noa_wlan_cfg_space_get_section_va(wlan_client, WIT_LOG_SYS);
			client->component_shared_mem_size =
				noa_wlan_cfg_space_get_section_size(wlan_client, WIT_LOG_SYS);
			break;
		case DBG_PACKET_SNIFFER:
			client->component_shared_mem_addr = (u64)noa_wlan_cfg_space_get_section_va(
				wlan_client, WIT_PACKET_SNIFFER);
			client->component_shared_mem_size = noa_wlan_cfg_space_get_section_size(
				wlan_client, WIT_PACKET_SNIFFER);
			break;
		default:
			break;
		}
	}
}

static struct wlan_debug_component_client_ops *wlan_dbg_client_ops[DBG_COMPONENT_NUM] = {
	&wlan_dbg_log_system_client_ops,
	&wlan_dbg_packet_sniffer_client_ops,
	NULL,
};

void wlan_debug_component_client_alloc(struct noa_wlan_entry *dbg_entry)
{
	int i;
	struct wlan_debug_component_client *component_client = NULL;
	struct wlan_debug_controller_client *dbg_client =
		(struct wlan_debug_controller_client *)dbg_entry->priv;

	if (dbg_client == NULL) {
		return;
	}

	dbg_client->wlan_client = (struct noa_wlan_client *)(dbg_entry->wlan_core->priv);
	dbg_client->destination = DBG_CLIENT_TRACE_EVENT;

	for (i = DBG_COMPONENT_START; i < DBG_COMPONENT_NUM; i++) {
		struct wlan_dbg_entry *component_entry =
			kzalloc(sizeof(struct wlan_dbg_entry) +
					ALIGN(sizeof(struct wlan_debug_component_client), 4),
				GFP_KERNEL);
		if (!component_entry || !wlan_dbg_client_ops[i])
			continue;

		component_client = (struct wlan_debug_component_client *)(component_entry->priv);
		component_client->ctlr_client = dbg_client;

		dbg_client->ops[i] = wlan_dbg_client_ops[i];
		dbg_client->component_entries[i] = component_entry;
		component_entry->parent_entry = dbg_entry;

		if (wlan_dbg_client_ops[i]->init_param) {
			wlan_dbg_client_ops[i]->init_param(component_client);
		}
		if (wlan_dbg_client_ops[i]->entry_register) {
			wlan_dbg_client_ops[i]->entry_register(component_entry, dbg_entry);
		}
	}
}

void wlan_debug_component_client_dealloc(struct noa_wlan_entry *dbg_entry)
{
	int i;
	struct wlan_dbg_entry *entry;
	struct wlan_debug_controller_client *dbg_client;

	dbg_client = (struct wlan_debug_controller_client *)dbg_entry->priv;

	if (dbg_client == NULL) {
		return;
	}

	for (i = DBG_COMPONENT_START; i < DBG_COMPONENT_NUM; i++) {
		entry = (struct wlan_dbg_entry *)dbg_client->component_entries[i];
		if (entry != NULL) {
			kobject_del(&entry->kobj);
			kobject_put(&entry->kobj);
			kfree(entry);
		}
	}
}

struct wlan_dbg_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct wlan_debug_controller_client *, char *);
	ssize_t (*store)(struct wlan_debug_controller_client *, const char *, size_t count);
};

static ssize_t wlan_dbg_dump_config(struct wlan_debug_controller_client *client, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA WLAN Dump DBG Config ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "Output Destination: %s\n",
			 get_destination_name(client->destination));
	cnt += scnprintf(buf + cnt, len - cnt, "Read Clear Mask: %d\n", client->read_clear_mask);
	return cnt;
}

static ssize_t wlan_dbg_write_output_destination(struct wlan_debug_controller_client *client,
						 const char *buf, size_t count)
{
	char destination[MAX_DESTINATION_NAME_LEN];

	if (sscanf(buf, "%s", destination) != 1)
		return -EINVAL;

	wlan_dbg_token_to_upper(destination);

	if (strncmp(destination, "TRACE_EVENT", strlen("TRACE_EVENT")) == 0) {
		client->destination = DBG_CLIENT_TRACE_EVENT;
	} else if (strncmp(destination, "SYSFS", strlen("SYSFS")) == 0) {
		client->destination = DBG_CLIENT_SYSFS;
	} else {
		return -EINVAL;
	}

	return count;
}

static void wlan_dbg_send_show_memory_map_section_request(const char *section_name, const char *fmt,
							  ...)
{
	char argv[MAX_MEMORY_MAP_DUMP_ARGV_LEN];
	va_list args;
	int cnt = 0;
	struct noa_wlan_shell_request req = {
		.cmd = "memory-map-section",
		.argv_len = 0,
	};

	if (fmt != NULL) {
		cnt += scnprintf(argv, MAX_MEMORY_MAP_DUMP_ARGV_LEN, "%s", section_name);

		va_start(args, fmt);
		cnt += vsnprintf(argv + cnt, MAX_MEMORY_MAP_DUMP_ARGV_LEN - cnt, fmt, args);
		va_end(args);

		memcpy(req.argv, argv, cnt);
		req.argv_len = cnt;
	} else {
		memcpy(req.argv, section_name, strlen(section_name));
		req.argv_len = strlen(section_name);
	}

	noa_wlan_fw_request_send(NOA_WLAN_CMD_SHELL_REQUEST, &req, sizeof(req));
}

static ssize_t
wlan_dbg_show_mmemory_map_global_config_value(struct wlan_debug_controller_client *client,
					      char *buf)
{
	int32_t len = PAGE_SIZE;
	ssize_t cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) PRE-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "GLOBAL_CONFIG", buf,
							  cnt);
	wlan_dbg_send_show_memory_map_section_request("GLOBAL_CONFIG", NULL);
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) POST-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "GLOBAL_CONFIG", buf,
							  cnt);
	return cnt;
}

static ssize_t
wlan_dbg_show_memory_map_nep_ring_config_value(struct wlan_debug_controller_client *client,
					       char *buf)
{
	int32_t len = PAGE_SIZE;
	ssize_t cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) PRE-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "NEP_RING_CONFIG",
							  buf, cnt);
	wlan_dbg_send_show_memory_map_section_request("NEP_RING_CONFIG", NULL);
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) POST-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "NEP_RING_CONFIG",
							  buf, cnt);
	return cnt;
}

static ssize_t
wlan_dbg_show_memory_map_wdev_ring_config_value(struct wlan_debug_controller_client *client,
						const char *buf, size_t size)
{
	int32_t ring_type = -1, wdev_ring_id = -1;

	if (sscanf(buf, "%d %d", &ring_type, &wdev_ring_id) != 2) {
		pr_err("[WLAN] Incorrect arguments, please input ring_type and wdev_ring_id.");
		return -EINVAL;
	}

	if (ring_type < 0 || wdev_ring_id < 0) {
		pr_err("[WLAN] Incorrect arguments, please input ring_type and wdev_ring_id.");
		return -EINVAL;
	}

	pr_info("[WLAN] ======= (APC) PRE-SYNC ======\n");
	noa_wlan_cfg_show_wdev_ring_config_value(client->wlan_client, ring_type, wdev_ring_id);
	wlan_dbg_send_show_memory_map_section_request("WDEV_RING_CONFIG", "%d %d", ring_type,
						      wdev_ring_id);
	pr_info("[WLAN] ======= (APC) POST-SYNC ======\n");
	noa_wlan_cfg_show_wdev_ring_config_value(client->wlan_client, ring_type, wdev_ring_id);

	return size;
}

static ssize_t wlan_dbg_show_memory_map_sta_info_value(struct wlan_debug_controller_client *client,
						       char *buf)
{
	int32_t len = PAGE_SIZE;
	ssize_t cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) PRE-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "STA_INFO", buf,
							  cnt);
	wlan_dbg_send_show_memory_map_section_request("STA_INFO", NULL);
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) POST-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "STA_INFO", buf,
							  cnt);
	return cnt;
}

static ssize_t wlan_dbg_dump_pm_state_config_value(struct wlan_debug_controller_client *client,
						   char *buf)
{
	int32_t len = PAGE_SIZE;
	ssize_t cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) PRE-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "PM_STATE_INFO", buf,
							  cnt);
	wlan_dbg_send_show_memory_map_section_request("PM_STATE_INFO", NULL);
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) POST-SYNC =======\n");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "PM_STATE_INFO", buf,
							  cnt);
	return cnt;
}

/// Dump the whole memory map offset divided in section
static ssize_t wlan_dbg_dump_memory_map_offset(struct wlan_debug_controller_client *client,
					       char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	struct noa_wlan_shell_request req = {
		.cmd = "memory-map-offset",
		.argv_len = 0,
	};

	// print the whole shared memory map with offset
	cnt += scnprintf(
		buf + cnt, len - cnt,
		"=============== (APC) NOA WLAN Shared Memory Map Offset ===============\n");
	cnt += scnprintf(buf + cnt, len - cnt, "base address pa: %llx\n",
			 noa_wlan_cfg_space_get_base_pa(client->wlan_client));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, global_config), "GLOBAL_CONFIG",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, GLOBAL_CONFIG));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, wifi_ring_config), "WDEV_RING_CONFIG",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client,
							     WDEV_RING_CONFIG));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, nep_ring_config), "NEP_RING_CONFIG",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, NEP_RING_CONFIG));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, sta_info), "STA_INFO",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, STA_INFO));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, pm_state_info), "PM_STATE_INFO",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, PM_STATE_INFO));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, interrupt_state_info), "INTR_STATE_INFO",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, INTR_STATE_INFO));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, mib), "MIB",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, MIB_INFO));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, wit_shared_log_sys_addr), "WIT_LOG_SYS",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, WIT_LOG_SYS));
	cnt += scnprintf(
		buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
		offsetof(noa_wlan_shared_info_t, wit_shared_packet_addr), "WIT_PACKET_SNIFFER",
		noa_wlan_cfg_space_get_section_size(client->wlan_client, WIT_PACKET_SNIFFER));
	cnt += scnprintf(buf + cnt, len - cnt, "(0x%08lx) %s size: %zu\n",
			 offsetof(noa_wlan_shared_info_t, buffer_management_shared_memory),
			 "BUFFER_MGMT",
			 noa_wlan_cfg_space_get_section_size(client->wlan_client, BUFFER_MGMT));

	noa_wlan_fw_request_send(NOA_WLAN_CMD_SHELL_REQUEST, &req, sizeof(req));
	return cnt;
}

static ssize_t wlan_dbg_dump_flow_id_table(struct wlan_debug_controller_client *client, char *buf)
{
	struct noa_wlan_shell_request req = {
		.cmd = "flow-id-table",
		.argv_len = 0,
	};

	noa_wlan_fw_request_send(NOA_WLAN_CMD_SHELL_REQUEST, &req, sizeof(req));
	scnprintf(buf, PAGE_SIZE, "Dumped flow id table, please check the log for details.\n");

	return strlen(buf);
}

static ssize_t wlan_dbg_show_memory_map_mib_info_value(struct wlan_debug_controller_client *client,
						       char *buf)
{
	int32_t len = PAGE_SIZE;
	ssize_t cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) PRE-SYNC =======");
	// Sync APC side mib counter to DRAM
	noa_wlan_cfg_update_noa_wlan_mib_info(client->wlan_client);
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "MIB_INFO", buf,
							  cnt);
	wlan_dbg_send_show_memory_map_section_request("MIB_INFO", NULL);
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) POST-SYNC =======");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "MIB_INFO", buf,
							  cnt);
	return cnt;
}

static ssize_t
wlan_dbg_show_memory_map_interrupt_state_info_value(struct wlan_debug_controller_client *client,
						    char *buf)
{
	int32_t len = PAGE_SIZE;
	ssize_t cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) PRE-SYNC =======");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "INTR_STATE_INFO",
							  buf, cnt);
	wlan_dbg_send_show_memory_map_section_request("INTR_STATE_INFO", NULL);
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) POST-SYNC =======");
	cnt += noa_wlan_cfg_show_memory_map_section_value(client->wlan_client, "INTR_STATE_INFO",
							  buf, cnt);
	return cnt;
}

static struct wlan_dbg_kobj_attr attr_dump_config =
	__ATTR(dump_config, 0664, wlan_dbg_dump_config, NULL);
static struct wlan_dbg_kobj_attr attr_write_destination =
	__ATTR(output_dest, 0664, NULL, wlan_dbg_write_output_destination);
static struct wlan_dbg_kobj_attr attr_dump_memory_map_offset =
	__ATTR(dump_memory_map_offset, 0664, wlan_dbg_dump_memory_map_offset, NULL);
static struct wlan_dbg_kobj_attr attr_dump_global_config =
	__ATTR(dump_global_config, 0664, wlan_dbg_show_mmemory_map_global_config_value, NULL);
static struct wlan_dbg_kobj_attr attr_dump_nep_ring_config =
	__ATTR(dump_nep_ring_config, 0664, wlan_dbg_show_memory_map_nep_ring_config_value, NULL);
static struct wlan_dbg_kobj_attr attr_dump_sta_info =
	__ATTR(dump_sta_info, 0664, wlan_dbg_show_memory_map_sta_info_value, NULL);
static struct wlan_dbg_kobj_attr attr_dump_wdev_ring_config =
	__ATTR(dump_wdev_ring_config, 0664, NULL, wlan_dbg_show_memory_map_wdev_ring_config_value);
static struct wlan_dbg_kobj_attr attr_dump_flow_id_table =
	__ATTR(dump_flow_id_table, 0664, wlan_dbg_dump_flow_id_table, NULL);
static struct wlan_dbg_kobj_attr attr_dump_pm_state_config =
	__ATTR(dump_pm_state_config, 0664, wlan_dbg_dump_pm_state_config_value, NULL);
static struct wlan_dbg_kobj_attr attr_dump_mib_info =
	__ATTR(dump_mib_info, 0664, wlan_dbg_show_memory_map_mib_info_value, NULL);
static struct wlan_dbg_kobj_attr attr_dump_interrupt_state_info = __ATTR(
	dump_interrupt_state_info, 0664, wlan_dbg_show_memory_map_interrupt_state_info_value, NULL);

static struct attribute *default_file_attrs[] = {
	&attr_dump_config.attr,
	&attr_write_destination.attr,
	&attr_dump_memory_map_offset.attr,
	&attr_dump_global_config.attr,
	&attr_dump_nep_ring_config.attr,
	&attr_dump_sta_info.attr,
	&attr_dump_wdev_ring_config.attr,
	&attr_dump_flow_id_table.attr,
	&attr_dump_pm_state_config.attr,
	&attr_dump_mib_info.attr,
	&attr_dump_interrupt_state_info.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t wlan_dbg_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct noa_wlan_entry *dbg_entry = container_of(kobj, struct noa_wlan_entry, kobj);
	struct wlan_debug_controller_client *client =
		(struct wlan_debug_controller_client *)dbg_entry->priv;
	struct wlan_dbg_kobj_attr *wlan_dbg_attr =
		container_of(attr, struct wlan_dbg_kobj_attr, attr);

	if (wlan_dbg_attr->show)
		return wlan_dbg_attr->show(client, buf);
	return -EIO;
}

static ssize_t wlan_dbg_sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf,
				    size_t count)
{
	struct noa_wlan_entry *dbg_entry = container_of(kobj, struct noa_wlan_entry, kobj);
	struct wlan_debug_controller_client *client =
		(struct wlan_debug_controller_client *)dbg_entry->priv;
	struct wlan_dbg_kobj_attr *wlan_dbg_attr =
		container_of(attr, struct wlan_dbg_kobj_attr, attr);

	if (wlan_dbg_attr->store)
		return wlan_dbg_attr->store(client, buf, count);
	return -EIO;
}

static struct sysfs_ops wlan_dbg_sysfs_ops = {
	.show = wlan_dbg_sysfs_show,
	.store = wlan_dbg_sysfs_store,
};

// define 4 kobject type for different dbg/{sub_entry}
struct kobj_type noa_wlan_dbg_ktype = {
	.sysfs_ops = &wlan_dbg_sysfs_ops,
	.default_groups = default_file_groups,
};
