#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include <wlan/wlan_rpc_service/noa_wlan_cmd_dispatch.h>
#include "wlan_debug_trace.h"
#include "wlan_debug_controller_client.h"
#include "noa.h"
#include "wlan/noa_wlan_hw.h"

#define MAX_PACKET_ENTRY_NUM 300
#define PACKET_SNIFFER_BUFFER_OFFSET sizeof(sniffer_entry_header) * MAX_PACKET_ENTRY_NUM
#define HEX_DUMP_ROW_SIZE 16
// Prefix string including output index and packet length
#define MAX_PREFIX_SIZE 50

#define CASE_SNIFFER_RING_TYPE(ring_type)                                                          \
	case ring_type:                                                                            \
		return #ring_type "_RING"

static ssize_t wlan_dbg_packet_sniffer_read_packets(struct wlan_debug_component_client *client,
						    char *buf);

typedef struct sniffer_entry_header {
	u8 valid;
	u8 ring_type;
	u16 pkt_len;
	u32 pkt_seq;
	u16 pkt_offset;
	char timestamp[12];
} __attribute__((packed, aligned(4))) sniffer_entry_header;

static bool read_packets_ring_ctrl[PACKET_SNIFFER_RING_TYPE_NUM];
static u16 output_id = 0;

static const char *wlan_debug_packet_sniffer_get_ring_name(enum packet_sniffer_ring_type ring_type)
{
	switch (ring_type) {
		CASE_SNIFFER_RING_TYPE(NOA_HW_TX);
		CASE_SNIFFER_RING_TYPE(NOA_HW_RX);
		CASE_SNIFFER_RING_TYPE(NEP_TX);
		CASE_SNIFFER_RING_TYPE(NEP_RX);
	default:
		return "UNKNOWN";
	}
}

struct wlan_dbg_packet_sniffer_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct wlan_debug_component_client *, char *);
	ssize_t (*store)(struct wlan_debug_component_client *, const char *, size_t count);
};

static ssize_t wlan_dbg_packet_sniffer_dump_config(struct wlan_debug_component_client *client,
						   char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	int i = 0;
	struct wlan_packet_sniffer_control_param *param =
		(struct wlan_packet_sniffer_control_param *)(client->param);

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA WLAN Dump Packet Sniffer Config ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "Packet Sniffer Enable: %d\n", param->enable);
	cnt += scnprintf(buf + cnt, len - cnt, "Max readable size for each packet: %hu\n",
			 param->capture_size);
	cnt += scnprintf(buf + cnt, len - cnt, "Packet Output Location: %s\n",
			 wlan_dbg_get_log_location_name(param->location));
	cnt += scnprintf(buf + cnt, len - cnt, "Packet Run Out Behavior: %hu\n", param->behavior);
	cnt += scnprintf(buf + cnt, len - cnt, "Shared Memory Address: %#llx\n",
			 client->component_shared_mem_addr);
	cnt += scnprintf(buf + cnt, len - cnt, "Shared Memory Size: %u\n",
			 client->component_shared_mem_size);
	cnt += scnprintf(buf + cnt, len - cnt, "Output Rings: \n");
	for (i = PACKET_SNIFFER_RING_TYPE_START; i < PACKET_SNIFFER_RING_TYPE_NUM; i++) {
		if (read_packets_ring_ctrl[i] == true) {
			cnt += scnprintf(buf + cnt, len - cnt, "%s ",
					 wlan_debug_packet_sniffer_get_ring_name(i));
		}
	}
	cnt += scnprintf(buf + cnt, len - cnt, "\n");
	return cnt;
}

static ssize_t wlan_dbg_packet_sniffer_enable_write(struct wlan_debug_component_client *client,
						    const char *buf, size_t size)
{
	u8 enable;
	char *token, *value = NULL, *saveptr = (char *)kmalloc(size, GFP_KERNEL);
	struct wlan_packet_sniffer_control_param *param =
		(struct wlan_packet_sniffer_control_param *)(client->param);
	if (saveptr == NULL) {
		return -ENOMEM;
	}
	memcpy(saveptr, buf, size);

	while ((token = strsep(&saveptr, " ")) != NULL) {
		// enable packet sniffer only
		if (token[0] == '-' && token[1] == 'p') {
			value = strsep(&saveptr, " ");
			if (sscanf(value, "%hhu", &enable) == 1) {
				param->enable = (bool)enable;
			}
		} else if (token[0] == '-' && token[1] == 'd') {
			// enable dump descriptor only
			value = strsep(&saveptr, " ");
			if (sscanf(value, "%hhu", &enable) == 1) {
				param->enable_desc = (bool)enable;
			}
		}
	}
	kfree(saveptr);

	if (value != NULL) {
		noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_CTRL, (void *)param,
					 sizeof(struct wlan_packet_sniffer_control_param));
		return size;
	}
	return -EINVAL;
}

static ssize_t
wlan_dbg_packet_sniffer_capture_size_write(struct wlan_debug_component_client *client,
					   const char *buf, size_t size)
{
	u16 value;
	struct wlan_packet_sniffer_control_param *param =
		(struct wlan_packet_sniffer_control_param *)(client->param);

	if (sscanf(buf, "%hu", &value) == 1 && value > 0) {
		param->capture_size = value;
		noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_CTRL, (void *)param,
					 sizeof(struct wlan_packet_sniffer_control_param));
		return size;
	}
	return -EINVAL;
}

static ssize_t
wlan_dbg_packet_sniffer_location_config_write(struct wlan_debug_component_client *client,
					      const char *buf, size_t size)
{
	char dest[MAX_DESTINATION_NAME_LEN];
	struct wlan_packet_sniffer_control_param *param =
		(struct wlan_packet_sniffer_control_param *)(client->param);

	if (sscanf(buf, "%s", dest) != 1)
		return -EINVAL;

	wlan_dbg_token_to_upper(dest);

	if (strncmp(dest, "DRAM", strlen("DRAM")) == 0) {
		param->location = OUTPUT_LOCATION_DRAM;
	} else if (strncmp(dest, "UART", strlen("UART")) == 0) {
		param->location = OUTPUT_LOCATION_UART;
	} else if (strncmp(dest, "DRAM_AND_UART", strlen("DRAM_AND_UART")) == 0) {
		param->location = OUTPUT_LOCATION_DRAM_AND_UART;
	} else {
		return -EINVAL;
	}

	noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_CTRL, (void *)param,
				 sizeof(struct wlan_packet_sniffer_control_param));

	return size;
}

static ssize_t
wlan_dbg_packet_sniffer_run_out_behavior_write(struct wlan_debug_component_client *client,
					       const char *buf, size_t size)
{
	u8 value;
	struct wlan_packet_sniffer_control_param *param =
		(struct wlan_packet_sniffer_control_param *)(client->param);

	if (sscanf(buf, "%hhu", &value) == 1) {
		param->behavior = value;
		noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_CTRL, (void *)param,
					 sizeof(struct wlan_packet_sniffer_control_param));
		return size;
	}
	return -EINVAL;
}

static int wlan_dbg_packet_sniffer_packet_formatter(struct sniffer_entry_header *entry_header,
						    u8 *packet_addr,
						    enum wlan_debug_client_destination dest)
{
	int i;
	char fmt_prefix[100];
	char *packet_buf_te = NULL;
	int cnt = 0;
	int len = PAGE_SIZE;

	packet_buf_te = (char *)kmalloc(
		entry_header->pkt_len * 3 + MAX_RING_NAME_SIZE + MAX_PREFIX_SIZE, GFP_KERNEL);
	if (packet_buf_te == NULL) {
		return -ENOMEM;
	}

	if (entry_header->pkt_len == 0) {
		return -EINVAL;
	}

	// The output format for trace event: i.e. 0:4:13 #1 NOA_HW_RX_RING|Packet Size: 128 0a82b0a8...
	cnt += snprintf(packet_buf_te + cnt, len - cnt, "%s||[WLAN] #%d %s|Packet Size: %d\n",
			entry_header->timestamp, entry_header->pkt_seq,
			wlan_debug_packet_sniffer_get_ring_name(
				(enum packet_sniffer_ring_type)(entry_header->ring_type)),
			entry_header->pkt_len);

	if (dest == DBG_CLIENT_TRACE_EVENT) {
		for (i = 0; i < entry_header->pkt_len; i++) {
			cnt += snprintf(packet_buf_te + cnt, len - cnt, "%02x", *(packet_addr + i));
		}
		trace_wlan_noa_packet_sniffer(packet_buf_te);
	} else {
		pr_info("%s", packet_buf_te);
		snprintf(fmt_prefix, 100, "[WLAN] %s ",
			 wlan_debug_packet_sniffer_get_ring_name(
				 (enum packet_sniffer_ring_type)(entry_header->ring_type)));
		print_hex_dump(KERN_INFO, fmt_prefix, DUMP_PREFIX_NONE, 16, 1, (void *)packet_addr,
			       entry_header->pkt_len, false);
	}

	kfree(packet_buf_te);
	return cnt;
}

static void wlan_dbg_packet_sniffer_traverse_all_packets(struct wlan_debug_component_client *client,
							 int *skipped_cnt, int *invalid_cnt)
{
	u16 curr_entry_idx = 0;
	struct wlan_debug_controller_client *ctrl_client = client->ctlr_client;
	u8 *sniffer_read_pointer =
		(u8 *)(client->component_shared_mem_addr + PACKET_SNIFFER_BUFFER_OFFSET);
	struct sniffer_entry_header *prev_entry_header = NULL;
	struct sniffer_entry_header *entry_header =
		(struct sniffer_entry_header *)client->component_shared_mem_addr;
	if (entry_header == NULL) {
		return;
	}

	for (curr_entry_idx = 0; curr_entry_idx < MAX_PACKET_ENTRY_NUM; curr_entry_idx++) {
		entry_header = (struct sniffer_entry_header *)(client->component_shared_mem_addr +
							       sizeof(sniffer_entry_header) *
								       curr_entry_idx);
		sniffer_read_pointer =
			(u8 *)(client->component_shared_mem_addr + PACKET_SNIFFER_BUFFER_OFFSET +
			       entry_header->pkt_offset);

		if (prev_entry_header &&
		    prev_entry_header->pkt_offset + prev_entry_header->pkt_len >
			    entry_header->pkt_offset) {
			pr_info("[WLAN] skipped entry: %d offset: %d", entry_header->pkt_seq,
				entry_header->pkt_offset);
			(*skipped_cnt)++;
			continue;
		} else if ((entry_header->ring_type < PACKET_SNIFFER_RING_TYPE_NUM &&
			    read_packets_ring_ctrl[entry_header->ring_type] == false) ||
			   !entry_header->valid) {
			pr_info("[WLAN] invalid entry: %d offset: %d", entry_header->pkt_seq,
				entry_header->pkt_offset);
			prev_entry_header = entry_header;
			(*invalid_cnt)++;
			continue;
		} else {
			wlan_dbg_packet_sniffer_packet_formatter(entry_header, sniffer_read_pointer,
								 ctrl_client->destination);
			output_id++;
			prev_entry_header = entry_header;
			entry_header->valid = 0;
		}
	}
}

static ssize_t wlan_dbg_packet_sniffer_read_packets(struct wlan_debug_component_client *client,
						    char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, output_id_start = 0, skipped_cnt = 0, invalid_cnt = 0;
	struct wlan_packet_sniffer_control_param *param =
		(struct wlan_packet_sniffer_control_param *)(client->param);
	bool store_enable_state = param->enable;
	u8 *sniffer_read_pointer =
		(u8 *)(client->component_shared_mem_addr + PACKET_SNIFFER_BUFFER_OFFSET);

	// Stop writing from NCP
	if (store_enable_state != 0) {
		param->enable = 0;
		if (sniffer_read_pointer == NULL ||
		    noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_CTRL, (void *)param,
					     sizeof(struct wlan_packet_sniffer_control_param)) !=
			    0) {
			return -EINVAL;
		}
	}

	if (buf != NULL) {
		cnt += snprintf(buf + cnt, len - cnt, "Start capturing the %d-th packet\n",
				output_id);
	}
	output_id_start = output_id;

	wlan_dbg_packet_sniffer_traverse_all_packets(client, &skipped_cnt, &invalid_cnt);

	if (buf != NULL) {
		cnt += snprintf(
			buf + cnt, len - cnt,
			"Stop capturing packets from the %d-th packet, total: %d, skipped: %d,\
				invalid : %d\n ",
				output_id,
			output_id - output_id_start, skipped_cnt, invalid_cnt);
	}

	// Resume writing in NCP
	if (store_enable_state == 1) {
		param->enable = 1;
		if (noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_CTRL, (void *)param,
					     sizeof(struct wlan_packet_sniffer_control_param)) !=
		    0) {
			return -EINVAL;
		}
	}

	return cnt;
}

static void wlan_dbg_packet_sniffer_set_ring_types_enable(char *buf, bool enable)
{
	int i = 0;
	char *ring_type;

	if (buf == NULL)
		return;

	wlan_dbg_token_to_upper(buf);

	while ((ring_type = strsep(&buf, ",")) != NULL) {
		for (i = PACKET_SNIFFER_RING_TYPE_START; i < PACKET_SNIFFER_RING_TYPE_NUM; i++) {
			if (strncmp(wlan_debug_packet_sniffer_get_ring_name(i), ring_type,
				    strlen(wlan_debug_packet_sniffer_get_ring_name(i))) == 0) {
				read_packets_ring_ctrl[i] = enable;
				break;
			}
		}
	}
}

static ssize_t
wlan_dbg_packet_sniffer_set_read_packets_with_ring_type(struct wlan_debug_component_client *client,
							const char *buf, size_t size)
{
	char *save_ptr = (char *)kmalloc(size, GFP_KERNEL);
	char *token, *value;

	if (save_ptr == NULL) {
		return -ENOMEM;
	}
	memcpy(save_ptr, buf, size);

	while ((token = strsep(&save_ptr, " ")) != NULL) {
		if (*token == '\0') {
			continue;
		}

		if (strncmp(token, "-u", 2) == 0) {
			value = strsep(&save_ptr, " ");
			wlan_dbg_packet_sniffer_set_ring_types_enable(value, true);
		} else if (strncmp(token, "-d", 2) == 0) {
			value = strsep(&save_ptr, " ");
			wlan_dbg_packet_sniffer_set_ring_types_enable(value, false);
		}
	}

	kfree(save_ptr);

	return size;
}

static struct wlan_dbg_packet_sniffer_kobj_attr attr_dump_config =
	__ATTR(dump_config, 0664, wlan_dbg_packet_sniffer_dump_config, NULL);
static struct wlan_dbg_packet_sniffer_kobj_attr attr_enable =
	__ATTR(capture_enable, 0664, NULL, wlan_dbg_packet_sniffer_enable_write);
static struct wlan_dbg_packet_sniffer_kobj_attr attr_capture_size =
	__ATTR(capture_size, 0664, NULL, wlan_dbg_packet_sniffer_capture_size_write);
static struct wlan_dbg_packet_sniffer_kobj_attr attr_location_config =
	__ATTR(location_config, 0664, NULL, wlan_dbg_packet_sniffer_location_config_write);
static struct wlan_dbg_packet_sniffer_kobj_attr attr_run_out_behavior =
	__ATTR(run_out_behavior, 0664, NULL, wlan_dbg_packet_sniffer_run_out_behavior_write);
static struct wlan_dbg_packet_sniffer_kobj_attr attr_read_packets =
	__ATTR(read_packets, 0664, wlan_dbg_packet_sniffer_read_packets,
	       wlan_dbg_packet_sniffer_set_read_packets_with_ring_type);

static struct attribute *default_file_attrs[] = {
	&attr_dump_config.attr,
	&attr_enable.attr,
	&attr_capture_size.attr,
	&attr_location_config.attr,
	&attr_run_out_behavior.attr,
	&attr_read_packets.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t wlan_dbg_packet_sniffer_sysfs_show(struct kobject *kobj, struct attribute *attr,
						  char *buf)
{
	struct wlan_dbg_entry *packet_sniffer_entry =
		container_of(kobj, struct wlan_dbg_entry, kobj);
	struct wlan_debug_component_client *client =
		(struct wlan_debug_component_client *)packet_sniffer_entry->priv;
	struct wlan_dbg_packet_sniffer_kobj_attr *wlan_dbg_packet_sniffer_attr =
		container_of(attr, struct wlan_dbg_packet_sniffer_kobj_attr, attr);

	if (wlan_dbg_packet_sniffer_attr->show)
		return wlan_dbg_packet_sniffer_attr->show(client, buf);
	return -EIO;
}

static ssize_t wlan_dbg_packet_sniffer_sysfs_store(struct kobject *kobj, struct attribute *attr,
						   const char *buf, size_t count)
{
	struct wlan_dbg_entry *packet_sniffer_entry =
		container_of(kobj, struct wlan_dbg_entry, kobj);
	struct wlan_debug_component_client *client =
		(struct wlan_debug_component_client *)packet_sniffer_entry->priv;
	struct wlan_dbg_packet_sniffer_kobj_attr *wlan_dbg_packet_sniffer_attr =
		container_of(attr, struct wlan_dbg_packet_sniffer_kobj_attr, attr);

	if (wlan_dbg_packet_sniffer_attr->store)
		return wlan_dbg_packet_sniffer_attr->store(client, buf, count);
	return -EIO;
}

static void wlan_dbg_packet_sniffer_init_param(struct wlan_debug_component_client *client)
{
	int i = 0;
	struct wlan_packet_sniffer_control_param *packet_sniffer_param =
		(struct wlan_packet_sniffer_control_param *)(client->param);
	packet_sniffer_param->location = OUTPUT_LOCATION_DRAM;
	packet_sniffer_param->behavior = OVERWRITE;
	packet_sniffer_param->capture_size = DEFAULT_CAPTURE_SIZE;
	packet_sniffer_param->enable = false;
	for (i = PACKET_SNIFFER_RING_TYPE_START; i < PACKET_SNIFFER_RING_TYPE_NUM; i++) {
		read_packets_ring_ctrl[i] = 1;
	}
}

static void wlan_dbg_packet_sniffer_entry_register(struct wlan_dbg_entry *packet_sniffer_entry,
						   struct noa_wlan_entry *dbg_entry)
{
	int ret;
	ret = kobject_init_and_add(&packet_sniffer_entry->kobj, &noa_wlan_dbg_packet_sniffer_ktype,
				   &dbg_entry->kobj, "packet_sniffer");
	if (ret) {
		kobject_put(&packet_sniffer_entry->kobj);
	}
}

static int
wlan_dbg_packet_sniffer_notify_driver_callback(struct wlan_debug_component_client *client, u16 *msg)
{
	struct noa_wlan_client *wlan_client =
		(struct noa_wlan_client *)client->ctlr_client->wlan_client;
	wlan_dbg_packet_sniffer_read_packets(client, NULL);
	// Notify the driver to reset the entry header table
	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		noa_wlan_hw_ringbell_ncp(wlan_client, DOORBELL_PACKET_SNIFFER_RESET);
	} else {
		noa_wlan_fw_request_send(NOA_WLAN_CMD_PACKET_SNIFFER_RESET, NULL, 0);
	}
	return 0;
}

static struct sysfs_ops wlan_dbg_packet_sniffer_sysfs_ops = {
	.show = wlan_dbg_packet_sniffer_sysfs_show,
	.store = wlan_dbg_packet_sniffer_sysfs_store,
};

// define 4 kobject type for different dbg/{sub_entry}
struct kobj_type noa_wlan_dbg_packet_sniffer_ktype = {
	.sysfs_ops = &wlan_dbg_packet_sniffer_sysfs_ops,
	.default_groups = default_file_groups,
};

struct wlan_debug_component_client_ops wlan_dbg_packet_sniffer_client_ops = {
	.init_param = wlan_dbg_packet_sniffer_init_param,
	.entry_register = wlan_dbg_packet_sniffer_entry_register,
	.notify_driver = wlan_dbg_packet_sniffer_notify_driver_callback,
};