// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <common/core.h>
#include <common/wlan/noa_wlan_brcm.h>
#include <common/wlan/noa_wlan_qca.h>
#include <common/wlan/memory_map.h>
#include <ncp/wlan/ncp_wlan_fw.h>
#include <ncp/wlan/system_utility.h>
#include <noa.h>
#include "noa_wlan_client.h"
#include "noa_wlan_mapper.h"
#include "noa_wlan_hw.h"
#include "noa_wlan_dynamic_switch.h"
#include "wlan_rpc_service/noa_wlan_cmd_dispatch.h"
#include <linux/platform_data/sscoredump.h>

#define to_entry(_kobj) (container_of(_kobj, struct noa_core_entry, kobj))
#define to_attr(_attr) (container_of(_attr, struct noa_wlan_kobj_attr, attr))

const static char *noa_mode_str[] = {
	"NOA_MODE_NORMAL",	"NOA_MODE_AP_NCP_DIRECT", "NOA_MODE_DISABLE",
	"NOA_MODE_FEEDTHROUGH", "NOA_MODE_VPN",
};

struct noa_wlan_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct noa_wlan_client *, char *);
	ssize_t (*store)(struct noa_wlan_client *, const char *, size_t count);
};

static ssize_t noa_wlan_trigger_coredump(struct noa_wlan_client *client, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;

	struct platform_device *sscd_pdev;
	struct sscd_platform_data *sscd_pdata;
	struct sscd_segment seg;

	noa_wlan_get_sscd_src((void **)&sscd_pdata, (void **)&sscd_pdev);

	if (sscd_pdev == NULL || sscd_pdata == NULL) {
		return -EINVAL;
	}

	cnt += scnprintf(buf + cnt, len - cnt, "%s(): start reporting seg addr: %llx\n", __func__,
			 noa_wlan_cfg_space_get_base_pa(client));

	seg.addr = noa_wlan_cfg_space_get_base_va(client);
	seg.size = noa_wlan_cfg_space_get_size(client);
	seg.paddr = (void *)noa_wlan_cfg_space_get_base_pa(client);
	seg.vaddr = (void *)noa_wlan_cfg_space_get_base_pa(client);
	sscd_pdata->sscd_report(sscd_pdev, &seg, 1, SSCD_FLAGS_ELFARM32HDR,
				"google_dpa_wlan_reset");

	cnt += scnprintf(buf + cnt, len - cnt, "%s(): stop reporting", __func__);
	return cnt;
}

static ssize_t noa_wlan_dump_ring(struct noa_wlan_client *client, char *buf)
{
	struct noa_wlan_client_ops *ops = client->ops;
	int len = PAGE_SIZE;
	int cnt = 0;

	if (!ops || !ops->dump_ring)
		return 0;
	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Ring ===\n");
	cnt += ops->dump_ring(client->bus, buf + cnt, len - cnt);
	return cnt;
}

static ssize_t noa_wlan_dump_bus(struct noa_wlan_client *client, char *buf)
{
	struct noa_wlan_client_ops *ops = client->ops;
	int len = PAGE_SIZE;
	int cnt = 0;

	if (!ops || !ops->dump_bus)
		return 0;
	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump BUS ===\n");
	cnt += ops->dump_bus(client->bus, buf + cnt, len - cnt);
	return cnt;
}

static ssize_t noa_wlan_dump_ring_info(struct noa_wlan_ring_info *ring, char *buf, int len)
{
	int cnt = 0;
	u32 rd, wr;
	struct noa_ring_regs *regs = &ring->cpu_regs;

	if (!regs->read || !regs->write)
		return cnt;

	rd = sys_io_read((u32 *)regs->read);
	wr = sys_io_read((u32 *)regs->write);

	if (rd == 0 && wr == 0)
		return cnt;

	cnt += scnprintf(buf + cnt, len - cnt, "name: %s: read: %d, write: %d\n", ring->name, rd,
			 wr);
	cnt += scnprintf(buf + cnt, len - cnt, "hw_idx: %d, desc_sz: %d, ndesc %d\n", ring->hw_idx,
			 ring->desc_sz, ring->ndesc);
	cnt += scnprintf(buf + cnt, len - cnt,
			 "base_addr: %#llx, len addr: %#llx, items addr: %#llx\n", regs->base,
			 regs->len, regs->max_item);
	cnt += scnprintf(buf + cnt, len - cnt, "read addr: %#llx, write addr: %#llx\n", regs->read,
			 regs->write);
	cnt += scnprintf(buf + cnt, len - cnt, "va: %#llx , pa: %p\n", ring->dma_va, &ring->dma_pa);
	return cnt;
}

struct ring_dump_info {
	u32 cnt;
	struct noa_wlan_client *client;
	struct noa_wlan_ring_info *rings;
};

static ssize_t noa_wlan_dump_rings_info(struct ring_dump_info *info, char *buf, int len)
{
	int i;
	int cnt = 0;
	struct noa_wlan_ring_info *ring;

	for (i = 0; i < info->cnt; i++) {
		ring = &info->rings[i];
		cnt += noa_wlan_dump_ring_info(ring, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_wlan_dump_client(struct noa_wlan_client *client, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	struct ring_dump_info info = {
		.client = client,
	};

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Wlan Client Info ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "bus: %p, irq: %d, irq_nums: %d\n", client->bus,
			 client->irqs[0], client->irq_nums);

	cnt += scnprintf(buf + cnt, len - cnt,
			 "reg_addr: %pa, reg_size: %d, share_addr: %pa, share_size: %d\n",
			 &client->reg_addr, client->reg_size, &client->share_addr,
			 client->share_size);

	cnt += scnprintf(buf + cnt, len - cnt,
			 "ints_addr: %#x, intm_addr: %#x, doorbell_addr: %#llx\n",
			 client->ints_addr, client->intm_addr, client->doorbell_addr);

	info.cnt = client->rx_post_max;
	info.rings = client->rx_post_ring;
	cnt += noa_wlan_dump_rings_info(&info, buf + cnt, len - cnt);

	info.cnt = client->tx_cpl_flow_max;
	info.rings = client->tx_cpl_ring;
	cnt += noa_wlan_dump_rings_info(&info, buf + cnt, len - cnt);

	cnt += scnprintf(buf + cnt, len - cnt, "rx_pkt_max: %d, rx_flow_max: %d, rx_buf_size: %d\n",
			 client->rx_pkt_max, client->rx_flow_max, client->rx_buf_sz);

	info.cnt = client->rx_flow_max;
	info.rings = client->rx_ring;
	cnt += noa_wlan_dump_rings_info(&info, buf + cnt, len - cnt);

	cnt += scnprintf(buf + cnt, len - cnt, "tx_pkt_max: %d, tx_flow_max: %d\n",
			 client->tx_pkt_max, client->tx_flow_max);

	info.cnt = client->tx_flow_max, info.rings = client->tx_ring,
	cnt += noa_wlan_dump_rings_info(&info, buf + cnt, len - cnt);

	return cnt;
}

#if IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)
static ssize_t noa_wlan_dump_stat(struct noa_wlan_client *client, char *buf)
{
	struct noa_wlan_fw *fw = noa_wlan_get_fw();
	struct noa_wlan_stat *stat = &fw->stat;
	int len = PAGE_SIZE;
	int cnt = 0, rxbm_cnt = 0;
	int txbm_buf_cnt = 0, txbm_pkid_cnt = 0;
	int i;

	for (i = 0; i < fw->wlan_info.tx_bm_sz; i++) {
		/* free tkid */
		if (!fw->txbm[i].valid) {
			txbm_pkid_cnt++;
			txbm_buf_cnt++;
		}
	}

	for (i = 0; i < fw->wlan_info.rx_pkt_max; i++) {
		if (!fw->rxbm[i].valid)
			rxbm_cnt++;
	}

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Wlan FW statistic ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8lu %15s: %8lu\n", "rx", stat->rx, "rx_err",
			 stat->rx_err);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8lu %15s: %8lu\n", "tx", stat->tx, "tx_err",
			 stat->tx_err);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8lu %15s: %8lu\n", "rx_free", stat->rx_free,
			 "rx_free_err", stat->rx_free_err);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8lu %15s: %8lu\n", "tx_cpl", stat->tx_cpl,
			 "tx_cpl_err", stat->tx_cpl_err);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8lu %15s: %8lu\n", "rxbm_sync",
			 stat->rxbm_sync, "rxbm_sync_err", stat->rxbm_sync_err);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8lu\n", "feedback", stat->feedback);
	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Wlan FW BM statistic ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8d\n", "free_rxbm", rxbm_cnt);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8d\n", "free_txid", txbm_pkid_cnt);
	cnt += scnprintf(buf + cnt, len - cnt, "%10s: %8d\n", "free_txbuf", txbm_buf_cnt);
	return cnt;
}

enum {
	NOA_WLAN_CONFIG_TX_DBG,
	NOA_WLAN_CONFIG_RX_DBG,
	NOA_WLAN_CONFIG_DISABLE_CP,
	NOA_WLAN_CONFIG_MAX
};

static ssize_t noa_wlan_config_read(struct noa_wlan_client *client, char *buf)
{
	struct noa_wlan_fw *fw = noa_wlan_get_fw();
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "[%d] tx_dbg: %d\n", NOA_WLAN_CONFIG_TX_DBG,
			 fw->txdbg);
	cnt += scnprintf(buf + cnt, len - cnt, "[%d] rx_dbg: %d\n", NOA_WLAN_CONFIG_RX_DBG,
			 fw->rxdbg);
	cnt += scnprintf(buf + cnt, len - cnt, "[%d] disable_cp: %d\n", NOA_WLAN_CONFIG_DISABLE_CP,
			 fw->disable_cp);
	return cnt;
}

static ssize_t noa_wlan_config_write(struct noa_wlan_client *client, const char *buf, size_t size)
{
	struct noa_wlan_fw *fw = noa_wlan_get_fw();
	int value;

	sscanf(buf, "%d", &value);
	switch (value) {
	case NOA_WLAN_CONFIG_TX_DBG:
		fw->txdbg = !fw->txdbg;
		break;
	case NOA_WLAN_CONFIG_RX_DBG:
		fw->rxdbg = !fw->rxdbg;
		break;
	case NOA_WLAN_CONFIG_DISABLE_CP:
		fw->disable_cp = !fw->disable_cp;
		break;
	default:
		break;
	}
	return size;
}
#endif

static ssize_t noa_wlan_dump_ring_write(struct noa_wlan_client *client, const char *buf,
					size_t size)
{
	int value;

	sscanf(buf, "%d", &value);
	client->dump_opt = value;
	return size;
}

static ssize_t noa_wlan_mode_read(struct noa_wlan_client *client, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	int i;

	if (client->mode >= NOA_MODE_MAX)
		return 0;
	cnt += scnprintf(buf + cnt, len - cnt, "current mode: %s\n", noa_mode_str[client->mode]);
	for (i = 0; i < NOA_MODE_MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "option[%d]: %s\n", i, noa_mode_str[i]);
	}
	return cnt;
}

static ssize_t noa_wlan_mode_write(struct noa_wlan_client *client, const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);
	if (value >= NOA_MODE_MAX)
		return size;

	if (value == NOA_MODE_DISABLE && client->mode != NOA_MODE_DISABLE) {
		// doing switch to NOA disable mode
		pr_info("Switch to NOA disable mode!\n");
	} else if (client->mode == NOA_MODE_DISABLE && value != NOA_MODE_DISABLE) {
		// doing switch to NOA enable mode
		pr_info("Switch to NOA enable mode!\n");
	} else if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT) && value == NOA_MODE_AP_NCP_DIRECT &&
		   client->mode != NOA_MODE_AP_NCP_DIRECT) {
		pr_info("Switch to AP-NCP direct mode\n");
	}

	// update to firmware
	noa_wlan_fw_request_send(NOA_WLAN_CMD_NOA_MODE_CTRL, (void *)&value, sizeof(value));
	// really update the NOA mode after applying it.
	client->mode = value;
	return size;
}

static ssize_t noa_wlan_mapper_read(struct noa_wlan_client *client, char *buf)
{
	struct list_head *collection;
	struct noa_wlan_mapping_node *node;
	ssize_t pos = 0;
	int count = 0;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT) || !client)
		return pos;

	collection = noa_wlan_mapper_node_collect(client);
	if (!collection)
		return pos;

	if (list_empty(collection)) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "(List is empty)\n");
		return pos;
	}

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "----------------------------------- Contents of Mapping Node "
			 "---------------------------------------\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "Idx  Mapping_Type   TKID_Used   Pool_ID    TKID     "
			 "Size      CPU_Addr            Mapped_Addr\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "----------------------------------------------------------------"
			 "-------------------------------------\n");

	list_for_each_entry (node, collection, list) {
		count++;

		if (PAGE_SIZE - pos < 100) {
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					 "(Truncated - more entries available)\n");
			break;
		}

		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "%3d  %-15s  %-9s  %-8u  %-7u  %-8zu  %-18px  %-18pad\n", count,
				 (node->mapping_type == MAPPING_TYPE_CONTIGUOUS) ? "Contiguous" :
										   "Scatter",
				 (node->tkid_in_use == true) ? "Yes" : "No", node->pool_id,
				 node->tkid, node->size, node->cpu_addr, &node->mapped_addr);
	}

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "----------------------------------------------------------------"
			 "-------------------------------------\n");

	return pos;
}

static ssize_t noa_wlan_dpa_ssr_policy_write(struct noa_wlan_client *client, const char *buf,
					     size_t size)
{
	int value;
	struct noa_wlan_switch_manager *switch_manager = noa_wlan_get_switch_manager();

	sscanf(buf, "%d", &value);
	if (value < NOA_WLAN_DATA_PATH_BYPASS_MODE || value >= NOA_WLAN_DATA_PATH_MODE_NUM) {
		pr_err("Invalid DPA SSR policy %d\n", value);
		return size;
	}

	switch_manager->dpa_ssr_policy = value;
	pr_info("Set DPA SSR policy to %s\n",
		get_data_path_mode_name(switch_manager->dpa_ssr_policy));
	return size;
}

static ssize_t noa_wlan_dpa_ssr_policy_read(struct noa_wlan_client *client, char *buf)
{
	struct noa_wlan_switch_manager *switch_manager = noa_wlan_get_switch_manager();
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "current DPA SSR policy: %s\n",
			 get_data_path_mode_name(switch_manager->dpa_ssr_policy));
	return cnt;
}

static struct noa_wlan_kobj_attr attr_dump_ring =
	__ATTR(dump_ring, 0664, noa_wlan_dump_ring, noa_wlan_dump_ring_write);

static struct noa_wlan_kobj_attr attr_dump_bus = __ATTR(dump_bus, 0664, noa_wlan_dump_bus, NULL);

static struct noa_wlan_kobj_attr attr_dump_client =
	__ATTR(dump_client, 0664, noa_wlan_dump_client, NULL);

static struct noa_wlan_kobj_attr attr_mode =
	__ATTR(mode, 0664, noa_wlan_mode_read, noa_wlan_mode_write);

static struct noa_wlan_kobj_attr attr_ssr_dump =
	__ATTR(ssr_dump, 0664, noa_wlan_trigger_coredump, NULL);

static struct noa_wlan_kobj_attr attr_mapper = __ATTR(mapper, 0664, noa_wlan_mapper_read, NULL);

static struct noa_wlan_kobj_attr attr_dpa_ssr_policy =
	__ATTR(dpa_ssr_policy, 0664, noa_wlan_dpa_ssr_policy_read, noa_wlan_dpa_ssr_policy_write);

#if IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)
static struct noa_wlan_kobj_attr attr_config =
	__ATTR(dump_config, 0664, noa_wlan_config_read, noa_wlan_config_write);
static struct noa_wlan_kobj_attr attr_dump_stat = __ATTR(dump_stat, 0664, noa_wlan_dump_stat, NULL);
#endif

static struct attribute *default_file_attrs[] = {
	&attr_dump_ring.attr,
	&attr_dump_bus.attr,
	&attr_dump_client.attr,
	&attr_mode.attr,
	&attr_ssr_dump.attr,
	&attr_mapper.attr,
	&attr_dpa_ssr_policy.attr,
#if IS_ENABLED(CONFIG_NOA_WIFI_FW_V1)
	&attr_dump_stat.attr,
	&attr_config.attr,
#endif
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t noa_wlan_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct noa_core_entry *entry = to_entry(kobj);
	struct noa_wlan_client *client = (struct noa_wlan_client *)entry->priv;
	struct noa_wlan_kobj_attr *noa_wlan_attr = to_attr(attr);

	if (noa_wlan_attr->show)
		return noa_wlan_attr->show(client, buf);
	return -EIO;
}

static ssize_t noa_wlan_sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf,
				    size_t count)
{
	struct noa_core_entry *entry = to_entry(kobj);
	struct noa_wlan_client *client = (struct noa_wlan_client *)entry->priv;
	struct noa_wlan_kobj_attr *noa_wlan_attr = to_attr(attr);

	if (noa_wlan_attr->store)
		return noa_wlan_attr->store(client, buf, count);
	return -EIO;
}

static struct sysfs_ops noa_wlan_sysfs_ops = {
	.show = noa_wlan_sysfs_show,
	.store = noa_wlan_sysfs_store,
};

struct kobj_type noa_wlan_ktype = {
	.sysfs_ops = &noa_wlan_sysfs_ops,
	.default_groups = default_file_groups,
};
