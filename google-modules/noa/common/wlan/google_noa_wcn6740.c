// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA WiFi Driver - WCN6740
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#include <wlan/noa_wlan_client.h>
#include <wlan/noa_wlan_nep_helper.h>
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include <dp_types.h>
#include "hal_hw_headers.h"
#include "hal_api.h"
#include "hal_reo.h"
#include "hal_rx.h"
#include "hal_li_rx.h"
#include "dp_rx.h"
#include "dp_hist.h"
#include "dp_rx_buffer_pool.h"
#include "li/dp_li.h"
#include "li/dp_li_rx.h"
#include "dp_ipa.h"
#include "dp_internal.h"
#include "target_type.h"
#include "qdf_module.h"
#include "google_plat.h"

static struct noa_wlan_client *client;
static int irq_start;
static struct hif_exec_context *irq_map[MAX_IRQ_NUM];
static ssize_t noa_wlan_tx(void *buf, size_t buf_len, const void *data, size_t data_len);

static int noa_wlan_wakeup(bool lock)
{
	struct dp_soc *soc = client->bus;
	struct hal_soc *hal_soc = (struct hal_soc *)soc->hal_soc;
	if (hal_soc->init_phase)
		return 0;
	if (lock)
		return hif_force_wake_request(hal_soc->hif_handle);
	else
		return hif_force_wake_release(hal_soc->hif_handle);
}

static int noa_dump_tx_desc_pools(struct dp_soc *soc, char *buf, int len)
{
	struct dp_tx_desc_pool_s *tx_desc_pool;
	int cnt = 0, i, j;

	for (i = 0 ; i < MAX_TXDESC_POOLS; i++) {
		tx_desc_pool = &soc->tx_desc[i];
		cnt += scnprintf(buf + cnt, len - cnt,
			"=========TX Pool ID: %d ==========\n", i);
		cnt += scnprintf(buf + cnt, len - cnt,
			"elem_size: %d, num_allocated: %d, pool_size: %d\n",
			tx_desc_pool->elem_size, tx_desc_pool->num_allocated,
			tx_desc_pool->pool_size);
		cnt += scnprintf(buf + cnt, len - cnt,
			"flow_pool_id: %d, num_invalid_bin: %d, avail_desc: %d\n",
			tx_desc_pool->flow_pool_id, tx_desc_pool->num_invalid_bin,
			tx_desc_pool->avail_desc);
		cnt += scnprintf(buf + cnt, len - cnt,
			"status: %d, flow_type: %d, pkt_drop_no_desc: %d, pool_create_cnt: %d\n",
			tx_desc_pool->status, tx_desc_pool->flow_type,
			tx_desc_pool->pkt_drop_no_desc, tx_desc_pool->pool_create_cnt);
		for (j = 0; j < FL_TH_MAX; j++) {
			cnt += scnprintf(buf + cnt, len - cnt,
				"stop_th: %d, start_th: %d, max_pause_time: %lu, "
				"latest_pause_time: %lu\n",
				tx_desc_pool->stop_th[j], tx_desc_pool->start_th[j],
				tx_desc_pool->max_pause_time[j],
				tx_desc_pool->latest_pause_time[j]);
		}
	}
	return cnt;
}

static int noa_dump_rx_desc_pools(struct dp_soc *soc, char *buf, int len)
{
	struct rx_desc_pool *rx_desc_pool;
	int cnt = 0, i;

	for (i = 0 ; i < MAX_RXDESC_POOLS; i++) {
		rx_desc_pool = &soc->rx_desc_buf[i];
		cnt += scnprintf(buf + cnt, len - cnt,
			"=========RX Pool ID: %d ==========\n", i);
		cnt += scnprintf(buf + cnt, len - cnt,
			"elem_size: %d, buf_size: %d, pool_size: %d\n",
			rx_desc_pool->elem_size, rx_desc_pool->buf_size,
			rx_desc_pool->pool_size);
		cnt += scnprintf(buf + cnt, len - cnt,
			"buf_alignment: %d, rx_mon_dest_frag_enable: %d\n",
			rx_desc_pool->buf_alignment,
			rx_desc_pool->rx_mon_dest_frag_enable);
		cnt += scnprintf(buf + cnt, len - cnt,
			"desc_type: %d, owner: %d\n",
			rx_desc_pool->desc_type, rx_desc_pool->owner);
	}
	return cnt;
}

static int noa_dump_irq(struct dp_soc *soc, char *buf, int len)
{
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;
	struct hif_softc *scn = HIF_GET_SOFTC(hal->hif_handle);
	struct HIF_CE_state *hif_state = HIF_GET_CE_STATE(scn);
	struct hif_exec_context *hif_ext_group;
	int i, j;
	int cnt = 0;

	for (i = 0; i < hif_state->hif_num_extgroup; i++) {
		hif_ext_group = hif_state->hif_ext_group[i];
		cnt += scnprintf(buf + cnt, len - cnt,
			"numirq: %d, grp_id: %d, scale_bin_shift: %d\n",
			hif_ext_group->numirq, hif_ext_group->grp_id,
			hif_ext_group->scale_bin_shift);
		cnt += scnprintf(buf + cnt, len - cnt,
			"configured: %d, cpu: %d, inited: %d\n",
			hif_ext_group->configured, hif_ext_group->cpu,
			hif_ext_group->inited);
		cnt += scnprintf(buf + cnt, len - cnt,
			"irq_requested: %d, irq_enabled: %d, type: %d\n",
			hif_ext_group->irq_requested, hif_ext_group->irq_enabled,
			hif_ext_group->type);
		for (j = 0 ; j < hif_ext_group->numirq; j++) {
			cnt += scnprintf(buf + cnt, len - cnt,
				"irq: %d, os_irq: %d, irq_name: %s\n",
				hif_ext_group->irq[j], hif_ext_group->os_irq[j],
				hif_ext_group->irq_name(hif_ext_group->irq[j]));
		}
	}
	return cnt;
}

enum {
	NOA_DUMP_BUS_NONE,
	NOA_DUMP_BUS_TXDESC,
	NOA_DUMP_BUS_RXDESC,
	NOA_DUMP_BUS_IRQ,
	NOA_DUMP_BUS_MAX,
};

static int noa_dump_bus(void *priv, char *buf, int len)
{
	struct dp_soc *soc = (struct dp_soc *)priv;
	struct hal_soc *hal = (struct hal_soc *)soc;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
		"use_register_windowing: %d\n",
		hal->use_register_windowing);

	switch (client->dump_opt) {
	case NOA_DUMP_BUS_TXDESC:
		cnt += noa_dump_tx_desc_pools(soc, buf + cnt, len - cnt);
		break;
	case NOA_DUMP_BUS_RXDESC:
		cnt += noa_dump_rx_desc_pools(soc, buf + cnt, len - cnt);
		break;
	case NOA_DUMP_BUS_IRQ:
		cnt += noa_dump_irq(soc, buf + cnt, len - cnt);
		break;
	default:
		cnt += scnprintf(buf + cnt, len - cnt,
			"dump_opt: (0): None, (1): TX desc, (2): RX desc, (3): IRQ\n");
		break;
	}
	return cnt;
}

extern int hal_get_srng_ring_id(struct hal_soc *hal, int ring_type,
				int ring_num, int mac_id);

/*
types:
	REO_DST = 0,
	REO_EXCEPTION = 1,
	REO_REINJECT = 2,
	REO_CMD = 3,
	REO_STATUS = 4,
	TCL_DATA = 5,
	TCL_CMD_CREDIT = 6,
	TCL_STATUS = 7,
	CE_SRC = 8,
	CE_DST = 9,
	CE_DST_STATUS = 10,
	WBM_IDLE_LINK = 11,
	SW2WBM_RELEASE = 12,
	WBM2SW_RELEASE = 13,
	RXDMA_BUF = 14,
	RXDMA_DST = 15,
	RXDMA_MONITOR_BUF = 16,
	RXDMA_MONITOR_STATUS = 17,
	RXDMA_MONITOR_DST = 18,
	RXDMA_MONITOR_DESC = 19,
	DIR_BUF_RX_DMA_SRC = 20,
 */
static int noa_dump_ring(struct hal_soc *hal_soc, int type, char *buf, int len)
{
	struct hal_hw_srng_config *ring_config = HAL_SRNG_CONFIG(hal_soc, type);
	struct hal_srng *srng;
	int cnt = 0, ring_id, i;
	u32 tp_val = 0, hp_val = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
		"==========Type(%d)==========\n", type);

	cnt += scnprintf(buf + cnt, len - cnt,
		"start_id: %d, max_rings: %d, entry_size: %d\n",
		ring_config->start_ring_id, ring_config->max_rings,
		ring_config->entry_size);

	cnt += scnprintf(buf + cnt, len - cnt,
		"lmac_ring: %d, ring_dir: %d, max_size: %d, nf_irq_support: %d\n",
		ring_config->lmac_ring, ring_config->ring_dir, ring_config->max_size,
		ring_config->nf_irq_support);

	for (i = 0; i < ring_config->max_rings; i++) {
		ring_id = hal_get_srng_ring_id(hal_soc, type, i, 0);

		if (ring_id < 0)
			continue;

		srng =  &(hal_soc->srng_list[ring_id]);
		if (srng && !srng->initialized)
			continue;

		cnt += scnprintf(buf + cnt, len - cnt,
			"ring_id: %d, ring_type: %d, ring_dir: %d\n",
			srng->ring_id, srng->ring_type, srng->ring_dir);
		cnt += scnprintf(buf + cnt, len - cnt,
			"pa: %#lx, va: %#lx\n",
			(unsigned long)srng->ring_base_paddr,
			(unsigned long)srng->ring_base_vaddr);
		cnt += scnprintf(buf + cnt, len - cnt,
			"entry_size: %d, num_entries: %d, ring_size: %d\n",
			srng->entry_size, srng->num_entries, srng->ring_size);
		cnt += scnprintf(buf + cnt, len - cnt,
			"msi_addr: %#lx, msi_data: %x, irq: %d\n",
			(unsigned long)srng->msi_addr, srng->msi_data, srng->irq);
		cnt += scnprintf(buf + cnt, len - cnt,
			"hwreg_base0: %#lx, hwreg_base1: %#lx\n",
			(unsigned long)srng->hwreg_base[0],
			(unsigned long)srng->hwreg_base[1]);
		cnt += scnprintf(buf + cnt, len - cnt,
			"flags: %x, prefetch_timer: %d, intr_timer_thres_us: %d\n",
			srng->flags, srng->prefetch_timer, srng->intr_timer_thres_us);

		noa_wlan_wakeup(true);
		if (srng->ring_dir == HAL_SRNG_SRC_RING) {
			tp_val = *(volatile u32 *)(srng->u.src_ring.tp_addr);
			hp_val = *(volatile u32 *)(srng->u.src_ring.hp_addr);
			cnt += scnprintf(buf + cnt, len - cnt,
				"hp: %d, reap_hp: %d, cached_tp: %d\n",
				srng->u.src_ring.hp, srng->u.src_ring.reap_hp,
				srng->u.src_ring.cached_tp);

			cnt += scnprintf(buf + cnt, len - cnt,
				"hp_addr: %lx(%d), tp_addr: %lx(%d)\n",
				(unsigned long)srng->u.src_ring.hp_addr, hp_val,
				(unsigned long)srng->u.src_ring.tp_addr, tp_val);

			cnt += scnprintf(buf + cnt, len - cnt,
				"base_lsb: %x, base_msb: %x, hp: %d, tp: %d, tp_addrl: %x, tp_addrm: %x\n",
				SRNG_SRC_REG_READ(srng, BASE_LSB),
				SRNG_SRC_REG_READ(srng, BASE_MSB),
				SRNG_SRC_REG_READ(srng, HP),
				SRNG_SRC_REG_READ(srng, TP),
				SRNG_SRC_REG_READ(srng, TP_ADDR_LSB),
				SRNG_SRC_REG_READ(srng, TP_ADDR_MSB));
			cnt += scnprintf(buf + cnt, len - cnt,
				"tp_addr: %#lx, hp_addr: %#lx\n",
				(unsigned long)SRNG_SRC_ADDR(srng, TP),
				(unsigned long)SRNG_SRC_ADDR(srng, HP));
		} else {
			hp_val = *(volatile u32 *)(srng->u.dst_ring.hp_addr);
			tp_val = *(volatile u32 *)(srng->u.dst_ring.tp_addr);
			cnt += scnprintf(buf + cnt, len - cnt,
				"tp: %d, loop_cnt: %d, cached_hp: %d\n",
				srng->u.dst_ring.tp, srng->u.dst_ring.loop_cnt,
				srng->u.dst_ring.cached_hp);

			cnt += scnprintf(buf + cnt, len - cnt,
				"hp_addr: %lx(%d), tp_addr: %lx(%d)\n",
				(unsigned long)srng->u.dst_ring.hp_addr, hp_val,
				(unsigned long)srng->u.dst_ring.tp_addr, tp_val);
		}
		noa_wlan_wakeup(false);
	}
	return cnt;
}

static int noa_dump_rings(void *priv, char *buf, int len)
{
	struct dp_soc *soc = (struct dp_soc *)priv;
	struct hal_soc *hal_soc = (struct hal_soc *)soc->hal_soc;
	int cnt = 0;

	cnt += noa_dump_ring(hal_soc, client->dump_opt, buf + cnt, len - cnt);
	return cnt;
}

static void noa_ringbell(void *bus, u32 value)
{
}

#define TXCPL_RING_BASE 128
extern uint32_t dp_tx_cmpl_handle(struct dp_soc *soc, uint8_t ring_id, void *tx_comp_hal_desc);
static void noa_txcpl_sync(void *bus, void *desc)
{
	struct dp_soc *soc = (struct dp_soc *)bus;
	struct wbm_release_ring *tx_cpl = desc;
	uint8_t ring_id =  tx_cpl->ring_id - TXCPL_RING_BASE;

	dp_tx_cmpl_handle(soc, ring_id, desc);
}

static void noa_wlan_isr(int irq)
{
	struct hif_exec_context *hif_ext_group = irq_map[irq - irq_start];
	hif_ext_group_interrupt_handler(irq, hif_ext_group);
	return;
}

static struct noa_wlan_client_ops noa_ops = {
	.dump_ring = noa_dump_rings,
	.dump_bus = noa_dump_bus,
	.ringbell = noa_ringbell,
	.txcpl_sync = noa_txcpl_sync,
	.rx_isr = noa_wlan_isr,
	.wakeup = noa_wlan_wakeup,
	.write_payload = noa_wlan_tx,
};

static int noa_wlan_get_irqs(struct hal_soc *hal, u32 *irqs)
{
	struct hif_softc *scn = HIF_GET_SOFTC(hal->hif_handle);
	struct HIF_CE_state *hif_state = HIF_GET_CE_STATE(scn);
	struct hif_exec_context *hif_ext_group;
	int i, j, irq;
	int cur = 0;

	irq_start = 0;
	for (i = 0; i < hif_state->hif_num_extgroup; i++) {
		hif_ext_group = hif_state->hif_ext_group[i];
		for (j = 0 ; j < hif_ext_group->numirq; j++) {
			irq = hif_ext_group->irq[j];
			if (cur == 0)
				irq_start = irq;
			irqs[cur++] = irq;
			irq_map[irq - irq_start] = hif_ext_group;
		}
	}
	return cur;
}

#define REG_ADDR_SIZE 2097152
#define MAX_TX_PACKET_SIZE 4096
#define MAX_RX_PACKET_SIZE 65536
#define MAX_RX_BUFFER_SIZE 2048
static int noa_wlan_init(void *dev, void *priv)
{
	struct dp_soc *soc = priv;
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;
	qdf_device_t osdev = dev;
	struct hif_softc *scn = (struct hif_softc *)hal->hif_handle;
	struct hif_pci_softc *sc = HIF_GET_PCI_SOFTC(scn);

	client = noa_wlan_client_alloc(&noa_ops);
	if (!client)
		return -ENOMEM;

	/* assign client*/
	client->bus = soc;
	/* assign client*/
	client->dev = osdev->dev;
	/* physical base address */
	client->reg_addr = sc->ce_sc.ol_sc.mem_pa;
	client->share_addr = 0;
	client->reg_size = REG_ADDR_SIZE;
	client->share_size = 0;
	/* fixed info */
	client->rx_pkt_max = MAX_RX_PACKET_SIZE;
	client->tx_pkt_max = MAX_TX_PACKET_SIZE;
	/* sz*/
	client->rx_buf_sz = MAX_RX_BUFFER_SIZE;
	client->type = WLAN_FW_TYPE_QCA;
	return noa_wlan_client_register(client);
}

static int noa_irq_request(void *priv)
{
	struct dp_soc *soc = client->bus;
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;

	client->irq_nums = noa_wlan_get_irqs(hal, client->irqs);
	return noa_wlan_irq_request(client);
}

static void noa_wlan_exit(void *priv)
{
	noa_wlan_client_unregister(client);
	noa_wlan_client_free(client);
	client = NULL;
}

static void noa_client_ring_query(struct hal_soc *hal,
	struct hal_srng_params *ring_params,
	struct noa_wlan_ring_info *info)
{
	u32 id, ring_id = ring_params->ring_id;
	unsigned long addr;
	struct noa_ring_regs *regs = &info->regs;
	struct hal_srng *hal_srng = &(hal->srng_list[ring_id]);

	/* Ring configure register in share memory */
	id = ring_params->ring_dir ? DST_ID : SRC_ID;
	addr = (unsigned long)(ring_params->hwreg_base[0] + hal->hal_hw_reg_offset[id]);
	regs->len = addr;
	id = ring_params->ring_dir ? DST_BASE_MSB : SRC_BASE_MSB;
	addr = (unsigned long)(ring_params->hwreg_base[0] + hal->hal_hw_reg_offset[id]);
	regs->max_item = addr;
	id = ring_params->ring_dir ? DST_BASE_LSB : SRC_BASE_LSB;
	addr = (unsigned long)(ring_params->hwreg_base[0] + hal->hal_hw_reg_offset[id]);
	regs->base = addr;
	info->hw_idx = ring_id;
	info->ndesc = ring_params->num_entries;
	info->desc_sz = ring_params->entry_size * 4;
	info->offset = 0;
	info->dma_pa = (unsigned long)ring_params->ring_base_paddr;
	info->dma_va = (unsigned long)ring_params->ring_base_vaddr;
	if (ring_params->ring_dir == HAL_SRNG_SRC_RING) {
		regs->read = (unsigned long)hal_srng->u.src_ring.tp_addr;
		regs->write = (unsigned long)hal_srng->u.src_ring.hp_addr;
	} else {
		regs->read = (unsigned long)hal_srng->u.dst_ring.tp_addr;
		regs->write = (unsigned long)hal_srng->u.dst_ring.hp_addr;
	}
	snprintf(info->name,  RING_MAX_NAME, "%s%d",
		ring_params->ring_dir ? "rx_ring" : "tx_ring", ring_id);
}

static int noa_wlan_ring_init(struct noa_wlan_client *client, u32 ring_type,
	u32 *num_ring, struct noa_wlan_ring_info *ring_info, struct dp_srng *_srng)
{
	struct dp_soc *soc = client->bus;
	struct hal_soc *hal_soc = (struct hal_soc *)soc->hal_soc;
	struct hal_hw_srng_config *ring_config = HAL_SRNG_CONFIG(hal_soc, ring_type);
	struct hal_srng_params ring_params;
	u32 num = 0;
	int i, j, ring_id;
	struct dp_srng *srng;

	for (i = 0 ; i < ring_config->max_rings; i++) {
		ring_id = hal_get_srng_ring_id(hal_soc, ring_type, i, 0);

		if (ring_id < 0)
			continue;

		srng = &_srng[i];
		qdf_mem_zero(&ring_params, sizeof(struct hal_srng_params));
		ring_params.ring_id = ring_id;
		ring_params.entry_size = ring_config->entry_size;
		ring_params.num_entries = srng->num_entries;
		ring_params.ring_dir = ring_config->ring_dir;
		ring_params.ring_base_paddr = srng->base_paddr_aligned;
		ring_params.ring_base_vaddr = srng->base_vaddr_aligned;
		for (j = 0 ; j < MAX_SRNG_REG_GROUPS; j++) {
			ring_params.hwreg_base[j] = hal_soc->dev_base_addr +
				ring_config->reg_start[j] +
				(i * ring_config->reg_size[j]);
		}
		noa_client_ring_query(hal_soc, &ring_params,
			&ring_info[num++]);
	}
	*num_ring = num;
	return 0;
}

static int noa_wlan_bus_init(struct noa_wlan_client *client)
{
	struct dp_soc *soc = client->bus;
	noa_wlan_ring_init(client, TCL_DATA, &client->tx_flow_max,
		client->tx_ring, soc->tcl_data_ring);
	noa_wlan_ring_init(client, REO_DST, &client->rx_flow_max,
		client->rx_ring, soc->reo_dest_ring);
	noa_wlan_ring_init(client, WBM2SW_RELEASE, &client->tx_cpl_flow_max,
		client->tx_cpl_ring, soc->tx_comp_ring);
	noa_wlan_ring_init(client, RXDMA_BUF, &client->rx_post_max,
		client->rx_post_ring, soc->rx_refill_buf_ring);
	return 0;
}

static int noa_wlan_start(void *priv)
{
	/* update bus related information to client struct*/
	noa_wlan_bus_init(client);
	/* core dongle register */
	client->ints_addr = 0;
	client->intm_addr = 0;
	client->doorbell_addr = 0;
	/* NEP information */
	client->nep_tx_desc_sz = NOA_DESC_WLAN_TX_QCA_BYTE;
	client->nep_tx_items = client->tx_ring[0].ndesc;
	client->nep_rx_desc_sz = NOA_DESC_BASIC_BYTE;
	client->nep_rx_items = client->rx_ring[0].ndesc;
	/* NOA fw and hw start */
	return noa_wlan_fw_start(client);
}

static void noa_wlan_bus_exit(struct noa_wlan_client *client)
{
	/* do nothing */
}

static void noa_wlan_stop(void *priv)
{
	if (!test_bit(CLIENT_FLAG_START, &client->flags))
		return;
	noa_wlan_fw_stop(client);
	noa_wlan_bus_exit(client);
}

struct wcn_noa_wlan_tx_args {
	struct noa_wlan_client *client;
	void *pkt;
};

static void dump_txd_qca(char *str, struct tcl_data_cmd *txd)
{
	printk("%s: buf_l: %x, buf_h: %x, bm_id: %d, cook: %d\n",
		str,
		txd->buf_addr_info.buffer_addr_31_0,
		txd->buf_addr_info.buffer_addr_39_32,
		txd->buf_addr_info.return_buffer_manager,
		txd->buf_addr_info.sw_buffer_cookie);
	printk(
		"%s: buf_or_ext_desc_type: %d, epd: %d, encap_type: %d, encrypt_type: %d\n",
		str,
		txd->buf_or_ext_desc_type,
		txd->epd,
		txd->encap_type,
		txd->encrypt_type);
	printk(
		"%s: src_buffer_swap: %d, link_meta_swap: %d, tqm_no_drop: %d, reserved_2a: %d\n",
		str,
		txd->src_buffer_swap,
		txd->link_meta_swap,
		txd->tqm_no_drop,
		txd->reserved_2a);
	printk(
		"%s: search_type: %d, addrx_en: %d, addry_en: %d, tcl_cmd_number: %d\n",
		str,
		txd->search_type,
		txd->addrx_en,
		txd->addry_en,
		txd->tcl_cmd_number);
	printk(
		"%s: data_length: %d, ipv4_checksum_en: %d, udp_over_ipv4_checksum_en: %d "
		"udp_over_ipv6_checksum_en: %d\n",
		str,
		txd->data_length,
		txd->ipv4_checksum_en,
		txd->udp_over_ipv4_checksum_en,
		txd->udp_over_ipv6_checksum_en);
	printk(
		"%s: tcp_over_ipv4_checksum_en: %d, tcp_over_ipv6_checksum_en: %d, to_fw: %d, "
		"reserved_3a: %d\n",
		str,
		txd->tcp_over_ipv4_checksum_en,
		txd->tcp_over_ipv6_checksum_en,
		txd->to_fw,
		txd->reserved_3a);
	printk(
		"%s: packet_offset: %d, buffer_timestamp: %d, buffer_timestamp_valid: %d, "
		"reserved_4a: %d\n",
		str,
		txd->packet_offset,
		txd->buffer_timestamp,
		txd->buffer_timestamp_valid,
		txd->reserved_4a);
	printk(
		"%s: hlos_tid_overwrite: %d, hlos_tid: %d, lmac_id: %d, reserved_4b: %d\n",
		str,
		txd->hlos_tid_overwrite,
		txd->hlos_tid,
		txd->lmac_id,
		txd->reserved_4b);
	printk(
		"%s: dscp_tid_table_num: %d, search_index: %d, cache_set_num: %d, mesh_enable: %d\n",
		str,
		txd->dscp_tid_table_num,
		txd->search_index,
		txd->cache_set_num,
		txd->mesh_enable);
	printk("%s: reserved_6a: %d, ring_id: %d, looping_count: %d\n",
		str,
		txd->reserved_6a,
		txd->ring_id,
		txd->looping_count);

	hexdump(str, (u8 *)txd, sizeof(struct tcl_data_cmd));
}

static ssize_t noa_wlan_tx(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	const struct wcn_noa_wlan_tx_args *tx_info = (const struct wcn_noa_wlan_tx_args *)data;
	struct noa_desc *d = (struct noa_desc *)buf;
	struct noa_qca_txd *ext_txd = (struct noa_qca_txd *)d->ext_data;
	struct sk_buff *skb = tx_info->pkt;
	struct qdf_nbuf_cb *cb = (struct qdf_nbuf_cb *)skb->cb;
	struct tcl_data_cmd *cmd = (struct tcl_data_cmd *)cb->u.tx.dev.priv_cb_w.ext_cb_ptr;
	bool hotspot = cmd->addrx_en ? true : false;
	/* record tx_desc first since they will be remark later */
	u8 *pkt_va = skb->data;
	u16 pkt_len = cmd->data_length;
	u16 pkt_id = cmd->buf_addr_info.sw_buffer_cookie;
	unsigned long pa;

	/* keep for debugging */
	if (client->dump_opt == NOA_DUMP_BUS_TXDESC)
		dump_txd_qca("noa_wlan_tx", cmd);
	pa = cmd->buf_addr_info.buffer_addr_39_32;
	pa = (pa << 32 | cmd->buf_addr_info.buffer_addr_31_0);
	/* remark tx_desc to noa_desc */
	memset(d, 0, NOA_DESC_WLAN_TX_QCA_BYTE);
	/* assign extend */
	ext_txd->encrypt_type = cmd->encrypt_type;
	ext_txd->encap_type = cmd->encap_type;
	ext_txd->l3_checksum_en = cmd->ipv4_checksum_en;
	ext_txd->l4_checksum_en = cmd->udp_over_ipv4_checksum_en;
	ext_txd->set_hlos_tid = cmd->hlos_tid;
	ext_txd->to_fw = cmd->to_fw;
	ext_txd->ring_id = cmd->ring_id;
	ext_txd->bmid = cmd->buf_addr_info.return_buffer_manager;
	ext_txd->frag = cmd->buf_or_ext_desc_type;
	ext_txd->search_index = cmd->search_index & 0x3f;
	/* add addrx, addry since they are different between STA, AP mode */
	ext_txd->addrx_en = cmd->addrx_en;
	ext_txd->addry_en = cmd->addry_en;
	if (hotspot) {
		u8 *eth = pkt_va + cmd->packet_offset;
		struct noa_session_info info = {
			.ethertype = 0, /* means search from eth header */
			.ifidx = cmd->lmac_id,
			.flowid = ext_txd->ring_id,
			.ignore_llc = true,
		};
		noa_wlan_add_session_entry(client, eth, &info);
	}
	d->ver = 0;
	d->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
				      kNoaWlanRingTxData);
	d->mode = NOAD_MODE_DATA;
	d->desc_type = NOA_DESC_WLAN_TX_QCA;
	d->cp = NOAD_NO_COPY;
	d->fk = 0;
	/* extend dp and dl to include brcm_txd */
	d->dp_low = pa & 0xFFFFFFFF;
	d->dp_high = (pa >> 32) & 0xFFFFFFFF;
	d->dl = pkt_len;
	d->tkid = pkt_id;
	d->head_offset = 0;
	d->reason = FWD_REASON_FEEDTHROUGH;
	/* simulator only */
	d->dv = (unsigned long)pkt_va;
	/* write ddone at last */
	d->ddone = 0;
	return 0;
}

static int noa_wlan_tx_packet(void *priv, void *pkt, u32 vdev_id)
{
	const struct wcn_noa_wlan_tx_args args = {
		.client = client,
		.pkt = pkt,
	};
	return noa_wlan_client_ring_write(client, &client->tx_data_ring.ring, (void *)&args);
}

static int noa_wlan_txq_active(void *priv, u16 flowid, bool enable)
{
	return noa_wlan_fw_txq_active(client, flowid, enable);
}

static int noa_wlan_sta_active(void *priv, struct sta_info *info, bool enable)
{
	int i;
	int ret = noa_wlan_fw_sta_active(client, info, enable);

	if (ret) {
		dev_err(client->dev, "sta_active fail.\n");
		return ret;
	}

	/* WCN6740 queue activity must always be consistent with the STA activity state. */
	for (i = 0 ; i < client->tx_flow_max; i++) {
		ret = platform_bus_tx_queue_active(priv, i, enable);
		if (ret) {
			dev_err(client->dev, "tx_queue %d active fail.\n", i);
			return ret;
		}
	}
	return ret;
}

static inline
bool is_sa_da_idx_valid(struct dp_soc *soc, uint8_t *rx_tlv_hdr,
			qdf_nbuf_t nbuf, struct hal_rx_msdu_metadata msdu_info)
{
	if ((qdf_nbuf_is_sa_valid(nbuf) &&
	    (msdu_info.sa_idx > wlan_cfg_get_max_ast_idx(soc->wlan_cfg_ctx))) ||
	    (!qdf_nbuf_is_da_mcbc(nbuf) && qdf_nbuf_is_da_valid(nbuf) &&
	     (msdu_info.da_idx > wlan_cfg_get_max_ast_idx(soc->wlan_cfg_ctx))))
		return false;

	return true;
}

static uint32_t dp_rx_process_li_qca(struct dp_intr *int_ctx,
			  hal_ring_handle_t hal_ring_hdl, uint8_t reo_ring_num,
			  uint32_t quota)
{
	hal_ring_desc_t ring_desc;
	hal_ring_desc_t last_prefetched_hw_desc;
	hal_soc_handle_t hal_soc;
	struct dp_rx_desc *rx_desc = NULL;
	struct dp_rx_desc *last_prefetched_sw_desc = NULL;
	qdf_nbuf_t nbuf, next;
	bool near_full;
	union dp_rx_desc_list_elem_t *head[MAX_PDEV_CNT];
	union dp_rx_desc_list_elem_t *tail[MAX_PDEV_CNT];
	uint32_t num_pending = 0;
	uint32_t rx_bufs_used = 0, rx_buf_cookie;
	uint16_t msdu_len = 0;
	uint16_t peer_id;
	uint8_t vdev_id;
	struct dp_peer *peer;
	struct dp_vdev *vdev;
	uint32_t pkt_len = 0;
	struct hal_rx_mpdu_desc_info mpdu_desc_info;
	struct hal_rx_msdu_desc_info msdu_desc_info;
	enum hal_reo_error_status error;
	uint32_t peer_mdata;
	uint8_t *rx_tlv_hdr;
	uint32_t rx_bufs_reaped[MAX_PDEV_CNT];
	uint8_t mac_id = 0;
	struct dp_pdev *rx_pdev;
	struct dp_srng *dp_rxdma_srng;
	struct rx_desc_pool *rx_desc_pool;
	struct dp_soc *soc = int_ctx->soc;
	struct cdp_tid_rx_stats *tid_stats;
	qdf_nbuf_t nbuf_head;
	qdf_nbuf_t nbuf_tail;
	qdf_nbuf_t deliver_list_head;
	qdf_nbuf_t deliver_list_tail;
	uint32_t num_rx_bufs_reaped = 0;
	uint32_t intr_id;
	struct hif_opaque_softc *scn;
	int32_t tid = 0;
	bool is_prev_msdu_last = true;
	uint32_t rx_ol_pkt_cnt = 0;
	uint32_t num_entries = 0;
	struct hal_rx_msdu_metadata msdu_metadata;
	QDF_STATUS status;
	qdf_nbuf_t ebuf_head;
	qdf_nbuf_t ebuf_tail;
	uint8_t pkt_capture_offload = 0;
	int max_reap_limit;
	uint64_t current_time = 0;
	int cnt = 0, ret;
	struct noa_desc *desc;
	struct wlan_sw_nep_ring *nep_ring;
	noa_ring_consumer *ring;

	DP_HIST_INIT();
	hal_soc = soc->hal_soc;
	qdf_assert_always(hal_soc);

	scn = soc->hif_handle;
	hif_pm_runtime_mark_dp_rx_busy(scn);
	intr_id = int_ctx->dp_intr_id;

more_data:
	/* reset local variables here to be re-used in the function */
	nbuf_head = NULL;
	nbuf_tail = NULL;
	deliver_list_head = NULL;
	deliver_list_tail = NULL;
	peer = NULL;
	vdev = NULL;
	num_rx_bufs_reaped = 0;
	ebuf_head = NULL;
	ebuf_tail = NULL;
	max_reap_limit = dp_rx_get_loop_pkt_limit(soc);

	qdf_mem_zero(rx_bufs_reaped, sizeof(rx_bufs_reaped));
	qdf_mem_zero(&mpdu_desc_info, sizeof(mpdu_desc_info));
	qdf_mem_zero(&msdu_desc_info, sizeof(msdu_desc_info));
	qdf_mem_zero(head, sizeof(head));
	qdf_mem_zero(tail, sizeof(tail));

	dp_pkt_get_timestamp(&current_time);

	if (!client)
		goto end;

	nep_ring = &client->rx_data_ring;
	ring = &nep_ring->ring;
	if (noa_wlan_client_ring_is_empty(ring))
		goto end;

	ret = noa_wlan_ring_begin_processing(ring);
	if (ret < 0) {
		goto end;
	} else if (!ret) {
		dev_err(client->dev,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		goto end;
	}
	/* currently we didn't limite the number of batch count */
	while (true) {
		unsigned long data_addr = 0;
		ret = noa_wlan_ring_read(ring, &data_addr, sizeof(data_addr));
		if (!ret) {
			break;
		} else if (ret < 0 || !data_addr) {
			dev_err(client->dev,
				"Failed to read data from wifi sw ring, drop this item, err: %d\n",
				ret);
			cnt++;
			noa_ring_tail_inc(ring);
			continue;
		}
		cnt++;
		desc = (struct noa_desc *)data_addr;

		if (!desc->ddone) {
			dev_err(client->dev, "%s(): ddone %d error.\n", __func__, desc->ddone);
			continue;
		}
		rx_desc = dp_rx_cookie_2_va_rxdma_buf(soc, desc->tkid);
		if (!rx_desc)
			continue;
		/* get real qca reo_desc */
		ring_desc = (hal_ring_desc_t)(rx_desc->nbuf->data - WLAN_PKT_PAD);

		if (qdf_unlikely(!ring_desc))
			break;
		rx_buf_cookie = HAL_RX_REO_BUF_COOKIE_GET(ring_desc);
		error = HAL_RX_ERROR_STATUS_GET(ring_desc);
		if (qdf_unlikely(error == HAL_REO_ERROR_DETECTED)) {
			dp_rx_err("%pK: HAL RING 0x%pK:error %d",
				  soc, hal_ring_hdl, error);
			DP_STATS_INC(soc, rx.err.hal_reo_error[reo_ring_num],
				     1);
			/* Don't know how to deal with this -- assert */
			qdf_assert(0);
		}

		dp_rx_ring_record_entry(soc, reo_ring_num, ring_desc);
		rx_buf_cookie = HAL_RX_REO_BUF_COOKIE_GET(ring_desc);
		status = dp_rx_cookie_check_and_invalidate(ring_desc);
		if (qdf_unlikely(QDF_IS_STATUS_ERROR(status))) {
			DP_STATS_INC(soc, rx.err.stale_cookie, 1);
			break;
		}
		rx_desc = dp_rx_cookie_2_va_rxdma_buf(soc, rx_buf_cookie);
		status = dp_rx_desc_sanity(soc, hal_soc, hal_ring_hdl,
					   ring_desc, rx_desc);
		if (QDF_IS_STATUS_ERROR(status)) {
			if (qdf_unlikely(rx_desc && rx_desc->nbuf)) {
				qdf_assert_always(!rx_desc->unmapped);
				dp_ipa_reo_ctx_buf_mapping_lock(soc,
								reo_ring_num);
				dp_ipa_handle_rx_buf_smmu_mapping(
							soc,
							rx_desc->nbuf,
							RX_DATA_BUFFER_SIZE,
							false);
				qdf_nbuf_unmap_nbytes_single(
							soc->osdev,
							rx_desc->nbuf,
							QDF_DMA_FROM_DEVICE,
							RX_DATA_BUFFER_SIZE);
				rx_desc->unmapped = 1;
				dp_ipa_reo_ctx_buf_mapping_unlock(soc,
								  reo_ring_num);
				dp_rx_buffer_pool_nbuf_free(soc, rx_desc->nbuf,
							    rx_desc->pool_id);
				dp_rx_add_to_free_desc_list(
							&head[rx_desc->pool_id],
							&tail[rx_desc->pool_id],
							rx_desc);
			}
			continue;
		}

		/*
		 * this is a unlikely scenario where the host is reaping
		 * a descriptor which it already reaped just a while ago
		 * but is yet to replenish it back to HW.
		 * In this case host will dump the last 128 descriptors
		 * including the software descriptor rx_desc and assert.
		 */

		if (qdf_unlikely(!rx_desc->in_use)) {
			DP_STATS_INC(soc, rx.err.hal_reo_dest_dup, 1);
			dp_info_rl("Reaping rx_desc not in use!");
			dp_rx_dump_info_and_assert(soc, hal_ring_hdl,
						   ring_desc, rx_desc);
			/* ignore duplicate RX desc and continue to process */
			/* Pop out the descriptor */
			continue;
		}

		status = dp_rx_desc_nbuf_sanity_check(soc, ring_desc, rx_desc);
		if (qdf_unlikely(QDF_IS_STATUS_ERROR(status))) {
			DP_STATS_INC(soc, rx.err.nbuf_sanity_fail, 1);
			dp_info_rl("Nbuf validity check failure!");
			dp_rx_dump_info_and_assert(soc, hal_ring_hdl,
						   ring_desc, rx_desc);
			rx_desc->in_err_state = 1;
			continue;
		}

		if (qdf_unlikely(!dp_rx_desc_check_magic(rx_desc))) {
			dp_err("Invalid rx_desc cookie=%d", rx_buf_cookie);
			DP_STATS_INC(soc, rx.err.rx_desc_invalid_magic, 1);
			dp_rx_dump_info_and_assert(soc, hal_ring_hdl,
						   ring_desc, rx_desc);
		}

		/* Get MPDU DESC info */
		hal_rx_mpdu_desc_info_get_li(ring_desc, &mpdu_desc_info);

		/* Get MSDU DESC info */
		hal_rx_msdu_desc_info_get_li(ring_desc, &msdu_desc_info);

		if (qdf_unlikely(msdu_desc_info.msdu_flags &
				 HAL_MSDU_F_MSDU_CONTINUATION)) {
			/* previous msdu has end bit set, so current one is
			 * the new MPDU
			 */
			if (is_prev_msdu_last) {
				/* For new MPDU check if we can read complete
				 * MPDU by comparing the number of buffers
				 * available and number of buffers needed to
				 * reap this MPDU
				 */
				if ((msdu_desc_info.msdu_len /
				     (RX_DATA_BUFFER_SIZE -
				      soc->rx_pkt_tlv_size) + 1) >
				    num_pending) {
					DP_STATS_INC(soc,
						     rx.msdu_scatter_wait_break,
						     1);
					dp_rx_cookie_reset_invalid_bit(
								     ring_desc);
					/* As we are going to break out of the
					 * loop because of unavailability of
					 * descs to form complete SG, we need to
					 * reset the TP in the REO destination
					 * ring.
					 */
					hal_srng_dst_dec_tp(hal_soc,
							    hal_ring_hdl);
					break;
				}
				is_prev_msdu_last = false;
			}
		}

		if (mpdu_desc_info.mpdu_flags & HAL_MPDU_F_RETRY_BIT)
			qdf_nbuf_set_rx_retry_flag(rx_desc->nbuf, 1);

		if (qdf_unlikely(mpdu_desc_info.mpdu_flags &
				 HAL_MPDU_F_RAW_AMPDU))
			qdf_nbuf_set_raw_frame(rx_desc->nbuf, 1);

		if (!is_prev_msdu_last &&
		    msdu_desc_info.msdu_flags & HAL_MSDU_F_LAST_MSDU_IN_MPDU)
			is_prev_msdu_last = true;

		rx_bufs_reaped[rx_desc->pool_id]++;
		peer_mdata = mpdu_desc_info.peer_meta_data;
		QDF_NBUF_CB_RX_PEER_ID(rx_desc->nbuf) =
			dp_rx_peer_metadata_peer_id_get_li(soc, peer_mdata);
		QDF_NBUF_CB_RX_VDEV_ID(rx_desc->nbuf) =
			DP_PEER_METADATA_VDEV_ID_GET_LI(peer_mdata);

		/* to indicate whether this msdu is rx offload */
		pkt_capture_offload =
			DP_PEER_METADATA_OFFLOAD_GET_LI(peer_mdata);

		/*
		 * save msdu flags first, last and continuation msdu in
		 * nbuf->cb, also save mcbc, is_da_valid, is_sa_valid and
		 * length to nbuf->cb. This ensures the info required for
		 * per pkt processing is always in the same cache line.
		 * This helps in improving throughput for smaller pkt
		 * sizes.
		 */
		if (msdu_desc_info.msdu_flags & HAL_MSDU_F_FIRST_MSDU_IN_MPDU)
			qdf_nbuf_set_rx_chfrag_start(rx_desc->nbuf, 1);

		if (msdu_desc_info.msdu_flags & HAL_MSDU_F_MSDU_CONTINUATION)
			qdf_nbuf_set_rx_chfrag_cont(rx_desc->nbuf, 1);

		if (msdu_desc_info.msdu_flags & HAL_MSDU_F_LAST_MSDU_IN_MPDU)
			qdf_nbuf_set_rx_chfrag_end(rx_desc->nbuf, 1);

		if (msdu_desc_info.msdu_flags & HAL_MSDU_F_DA_IS_MCBC)
			qdf_nbuf_set_da_mcbc(rx_desc->nbuf, 1);

		if (msdu_desc_info.msdu_flags & HAL_MSDU_F_DA_IS_VALID)
			qdf_nbuf_set_da_valid(rx_desc->nbuf, 1);

		if (msdu_desc_info.msdu_flags & HAL_MSDU_F_SA_IS_VALID)
			qdf_nbuf_set_sa_valid(rx_desc->nbuf, 1);

		qdf_nbuf_set_tid_val(rx_desc->nbuf,
				     HAL_RX_REO_QUEUE_NUMBER_GET(ring_desc));

		/* set reo dest indication */
		qdf_nbuf_set_rx_reo_dest_ind_or_sw_excpt(
				rx_desc->nbuf,
				HAL_RX_REO_MSDU_REO_DST_IND_GET(ring_desc));

		QDF_NBUF_CB_RX_PKT_LEN(rx_desc->nbuf) = msdu_desc_info.msdu_len;

		QDF_NBUF_CB_RX_CTX_ID(rx_desc->nbuf) = reo_ring_num;

		/*
		 * move unmap after scattered msdu waiting break logic
		 * in case double skb unmap happened.
		 */
		rx_desc_pool = &soc->rx_desc_buf[rx_desc->pool_id];
		dp_ipa_reo_ctx_buf_mapping_lock(soc, reo_ring_num);
		dp_ipa_handle_rx_buf_smmu_mapping(soc, rx_desc->nbuf,
						  rx_desc_pool->buf_size,
						  false);
		qdf_nbuf_unmap_nbytes_single(soc->osdev, rx_desc->nbuf,
					     QDF_DMA_FROM_DEVICE,
					     rx_desc_pool->buf_size);
		rx_desc->unmapped = 1;
		dp_ipa_reo_ctx_buf_mapping_unlock(soc, reo_ring_num);
		DP_RX_PROCESS_NBUF(soc, nbuf_head, nbuf_tail, ebuf_head,
				   ebuf_tail, rx_desc);
		/*
		 * if continuation bit is set then we have MSDU spread
		 * across multiple buffers, let us not decrement quota
		 * till we reap all buffers of that MSDU.
		 */
		if (qdf_likely(!qdf_nbuf_is_rx_chfrag_cont(rx_desc->nbuf))) {
			quota -= 1;
			num_pending -= 1;
		}

		dp_rx_add_to_free_desc_list(&head[rx_desc->pool_id],
					    &tail[rx_desc->pool_id], rx_desc);
		num_rx_bufs_reaped++;

		dp_rx_prefetch_hw_sw_nbuf_desc(soc, hal_soc, num_pending,
					       hal_ring_hdl,
					       &last_prefetched_hw_desc,
					       &last_prefetched_sw_desc);

		/*
		 * only if complete msdu is received for scatter case,
		 * then allow break.
		 */
		if (is_prev_msdu_last &&
		    dp_rx_reap_loop_pkt_limit_hit(soc, num_rx_bufs_reaped,
						  max_reap_limit))
			break;
	}
	/* update NOA RX ring */
	noa_wlan_ring_complete_processing(ring);
	DP_STATS_INCC(soc,
		      rx.ring_packets[qdf_get_smp_processor_id()][reo_ring_num],
		      num_rx_bufs_reaped, num_rx_bufs_reaped);

	for (mac_id = 0; mac_id < MAX_PDEV_CNT; mac_id++) {
		/*
		 * continue with next mac_id if no pkts were reaped
		 * from that pool
		 */
		if (!rx_bufs_reaped[mac_id])
			continue;

		dp_rxdma_srng = &soc->rx_refill_buf_ring[mac_id];

		rx_desc_pool = &soc->rx_desc_buf[mac_id];

		dp_rx_buffers_replenish(soc, mac_id, dp_rxdma_srng,
					rx_desc_pool, rx_bufs_reaped[mac_id],
					&head[mac_id], &tail[mac_id]);
	}

	dp_verbose_debug("replenished %u\n", rx_bufs_reaped[0]);
	/* Peer can be NULL is case of LFR */
	if (qdf_likely(peer))
		vdev = NULL;

	/*
	 * BIG loop where each nbuf is dequeued from global queue,
	 * processed and queued back on a per vdev basis. These nbufs
	 * are sent to stack as and when we run out of nbufs
	 * or a new nbuf dequeued from global queue has a different
	 * vdev when compared to previous nbuf.
	 */
	nbuf = nbuf_head;
	while (nbuf) {
		next = nbuf->next;
		dp_rx_prefetch_nbuf_data(nbuf, next);

		if (qdf_unlikely(dp_rx_is_raw_frame_dropped(nbuf))) {
			nbuf = next;
			DP_STATS_INC(soc, rx.err.raw_frm_drop, 1);
			continue;
		}

		rx_tlv_hdr = qdf_nbuf_data(nbuf);
		vdev_id = QDF_NBUF_CB_RX_VDEV_ID(nbuf);
		peer_id =  QDF_NBUF_CB_RX_PEER_ID(nbuf);

		if (dp_rx_is_list_ready(deliver_list_head, vdev, peer,
					peer_id, vdev_id)) {
			dp_rx_deliver_to_stack(soc, vdev, peer,
					       deliver_list_head,
					       deliver_list_tail);
			deliver_list_head = NULL;
			deliver_list_tail = NULL;
		}

		/* Get TID from struct cb->tid_val, save to tid */
		if (qdf_nbuf_is_rx_chfrag_start(nbuf)) {
			tid = qdf_nbuf_get_tid_val(nbuf);
			if (tid >= CDP_MAX_DATA_TIDS) {
				DP_STATS_INC(soc, rx.err.rx_invalid_tid_err, 1);
				qdf_nbuf_free(nbuf);
				nbuf = next;
				continue;
			}
		}

		if (qdf_unlikely(!peer)) {
			peer = dp_peer_get_ref_by_id(soc, peer_id,
						     DP_MOD_ID_RX);
		} else if (peer && peer->peer_id != peer_id) {
			dp_peer_unref_delete(peer, DP_MOD_ID_RX);
			peer = dp_peer_get_ref_by_id(soc, peer_id,
						     DP_MOD_ID_RX);
		}

		if (peer) {
			QDF_NBUF_CB_DP_TRACE_PRINT(nbuf) = false;
			qdf_dp_trace_set_track(nbuf, QDF_RX);
			QDF_NBUF_CB_RX_DP_TRACE(nbuf) = 1;
			QDF_NBUF_CB_RX_PACKET_TRACK(nbuf) =
				QDF_NBUF_RX_PKT_DATA_TRACK;
		}

		rx_bufs_used++;

		if (qdf_likely(peer)) {
			vdev = peer->vdev;
		} else {
			nbuf->next = NULL;
			dp_rx_deliver_to_pkt_capture_no_peer(
					soc, nbuf, pkt_capture_offload);
			if (!pkt_capture_offload)
				dp_rx_deliver_to_stack_no_peer(soc, nbuf);
			nbuf = next;
			continue;
		}

		if (qdf_unlikely(!vdev)) {
			qdf_nbuf_free(nbuf);
			nbuf = next;
			DP_STATS_INC(soc, rx.err.invalid_vdev, 1);
			continue;
		}

		/* when hlos tid override is enabled, save tid in
		 * skb->priority
		 */
		if (qdf_unlikely(vdev->skip_sw_tid_classification &
					DP_TXRX_HLOS_TID_OVERRIDE_ENABLED))
			qdf_nbuf_set_priority(nbuf, tid);

		rx_pdev = vdev->pdev;
		DP_RX_TID_SAVE(nbuf, tid);
		if (qdf_unlikely(rx_pdev->delay_stats_flag) ||
		    qdf_unlikely(wlan_cfg_is_peer_ext_stats_enabled(
				 soc->wlan_cfg_ctx)) ||
		    dp_rx_pkt_tracepoints_enabled())
			qdf_nbuf_set_timestamp(nbuf);

		tid_stats =
		&rx_pdev->stats.tid_stats.tid_rx_stats[reo_ring_num][tid];

		/*
		 * Check if DMA completed -- msdu_done is the last bit
		 * to be written
		 */
		if (qdf_likely(!qdf_nbuf_is_rx_chfrag_cont(nbuf))) {
			if (qdf_unlikely(!hal_rx_attn_msdu_done_get_li(
								 rx_tlv_hdr))) {
				dp_err_rl("MSDU DONE failure");
				DP_STATS_INC(soc, rx.err.msdu_done_fail, 1);
				hal_rx_dump_pkt_tlvs(hal_soc, rx_tlv_hdr,
						     QDF_TRACE_LEVEL_INFO);
				tid_stats->fail_cnt[MSDU_DONE_FAILURE]++;
				qdf_assert(0);
				qdf_nbuf_free(nbuf);
				nbuf = next;
				continue;
			} else if (qdf_unlikely(hal_rx_attn_msdu_len_err_get_li(
								 rx_tlv_hdr))) {
				DP_STATS_INC(soc, rx.err.msdu_len_err, 1);
				qdf_nbuf_free(nbuf);
				nbuf = next;
				continue;
			}
		}

		DP_HIST_PACKET_COUNT_INC(vdev->pdev->pdev_id);
		/*
		 * First IF condition:
		 * 802.11 Fragmented pkts are reinjected to REO
		 * HW block as SG pkts and for these pkts we only
		 * need to pull the RX TLVS header length.
		 * Second IF condition:
		 * The below condition happens when an MSDU is spread
		 * across multiple buffers. This can happen in two cases
		 * 1. The nbuf size is smaller than the received msdu.
		 *    ex: we have set the nbuf size to 2048 during
		 *        nbuf_alloc. but we received an msdu which is
		 *        2304 bytes in size then this msdu is spread
		 *        across 2 nbufs.
		 *
		 * 2. AMSDUs when RAW mode is enabled.
		 *    ex: 1st MSDU is in 1st nbuf and 2nd MSDU is spread
		 *        across 1st nbuf and 2nd nbuf and last MSDU is
		 *        spread across 2nd nbuf and 3rd nbuf.
		 *
		 * for these scenarios let us create a skb frag_list and
		 * append these buffers till the last MSDU of the AMSDU
		 * Third condition:
		 * This is the most likely case, we receive 802.3 pkts
		 * decapsulated by HW, here we need to set the pkt length.
		 */
		hal_rx_msdu_metadata_get(hal_soc, rx_tlv_hdr, &msdu_metadata);
		if (qdf_unlikely(qdf_nbuf_is_frag(nbuf))) {
			bool is_mcbc, is_sa_vld, is_da_vld;

			is_mcbc = hal_rx_msdu_end_da_is_mcbc_get(soc->hal_soc,
								 rx_tlv_hdr);
			is_sa_vld =
				hal_rx_msdu_end_sa_is_valid_get(soc->hal_soc,
								rx_tlv_hdr);
			is_da_vld =
				hal_rx_msdu_end_da_is_valid_get(soc->hal_soc,
								rx_tlv_hdr);

			qdf_nbuf_set_da_mcbc(nbuf, is_mcbc);
			qdf_nbuf_set_da_valid(nbuf, is_da_vld);
			qdf_nbuf_set_sa_valid(nbuf, is_sa_vld);

			qdf_nbuf_pull_head(nbuf, soc->rx_pkt_tlv_size);
		} else if (qdf_nbuf_is_rx_chfrag_cont(nbuf)) {
			msdu_len = QDF_NBUF_CB_RX_PKT_LEN(nbuf);
			nbuf = dp_rx_sg_create(soc, nbuf);
			next = nbuf->next;

			if (qdf_nbuf_is_raw_frame(nbuf)) {
				DP_STATS_INC(vdev->pdev, rx_raw_pkts, 1);
				DP_STATS_INC_PKT(peer, rx.raw, 1, msdu_len);
			} else {
				qdf_nbuf_free(nbuf);
				DP_STATS_INC(soc, rx.err.scatter_msdu, 1);
				dp_info_rl("scatter msdu len %d, dropped",
					   msdu_len);
				nbuf = next;
				continue;
			}
		} else {
			msdu_len = QDF_NBUF_CB_RX_PKT_LEN(nbuf);
			pkt_len = msdu_len +
				  msdu_metadata.l3_hdr_pad +
				  soc->rx_pkt_tlv_size;

			qdf_nbuf_set_pktlen(nbuf, pkt_len);
			dp_rx_skip_tlvs(soc, nbuf, msdu_metadata.l3_hdr_pad);
		}

		dp_rx_send_pktlog(soc, rx_pdev, nbuf, QDF_TX_RX_STATUS_OK);

		/*
		 * process frame for mulitpass phrase processing
		 */
		if (qdf_unlikely(vdev->multipass_en)) {
			if (dp_rx_multipass_process(peer, nbuf, tid) == false) {
				DP_STATS_INC(peer, rx.multipass_rx_pkt_drop, 1);
				qdf_nbuf_free(nbuf);
				nbuf = next;
				continue;
			}
		}

		if (!dp_wds_rx_policy_check(rx_tlv_hdr, vdev, peer)) {
			dp_rx_err("%pK: Policy Check Drop pkt", soc);
			DP_STATS_INC(peer, rx.policy_check_drop, 1);
			tid_stats->fail_cnt[POLICY_CHECK_DROP]++;
			/* Drop & free packet */
			qdf_nbuf_free(nbuf);
			/* Statistics */
			nbuf = next;
			continue;
		}

		if (qdf_unlikely(peer && (peer->nawds_enabled) &&
				 (qdf_nbuf_is_da_mcbc(nbuf)) &&
				 (hal_rx_get_mpdu_mac_ad4_valid(soc->hal_soc,
								rx_tlv_hdr) ==
				  false))) {
			tid_stats->fail_cnt[NAWDS_MCAST_DROP]++;
			DP_STATS_INC(peer, rx.nawds_mcast_drop, 1);
			qdf_nbuf_free(nbuf);
			nbuf = next;
			continue;
		}

		/*
		 * Drop non-EAPOL frames from unauthorized peer.
		 */
		if (qdf_likely(peer) && qdf_unlikely(!peer->authorize) &&
		    !qdf_nbuf_is_raw_frame(nbuf)) {
			bool is_eapol = qdf_nbuf_is_ipv4_eapol_pkt(nbuf) ||
					qdf_nbuf_is_ipv4_wapi_pkt(nbuf);

			if (!is_eapol) {
				DP_STATS_INC(peer,
					     rx.peer_unauth_rx_pkt_drop, 1);
				qdf_nbuf_free(nbuf);
				nbuf = next;
				continue;
			}
		}

		if (soc->process_rx_status)
			dp_rx_cksum_offload(vdev->pdev, nbuf, rx_tlv_hdr);

		/* Update the protocol tag in SKB based on CCE metadata */
		dp_rx_update_protocol_tag(soc, vdev, nbuf, rx_tlv_hdr,
					  reo_ring_num, false, true);

		/* Update the flow tag in SKB based on FSE metadata */
		dp_rx_update_flow_tag(soc, vdev, nbuf, rx_tlv_hdr, true);

		dp_rx_msdu_stats_update(soc, nbuf, rx_tlv_hdr, peer,
					reo_ring_num, tid_stats);

		if (qdf_unlikely(vdev->mesh_vdev)) {
			if (dp_rx_filter_mesh_packets(vdev, nbuf, rx_tlv_hdr)
					== QDF_STATUS_SUCCESS) {
				dp_rx_info("%pK: mesh pkt filtered", soc);
				tid_stats->fail_cnt[MESH_FILTER_DROP]++;
				DP_STATS_INC(vdev->pdev, dropped.mesh_filter,
					     1);

				qdf_nbuf_free(nbuf);
				nbuf = next;
				continue;
			}
			dp_rx_fill_mesh_stats(vdev, nbuf, rx_tlv_hdr, peer);
		}

		if (qdf_likely(vdev->rx_decap_type ==
			       htt_cmn_pkt_type_ethernet) &&
		    qdf_likely(!vdev->mesh_vdev)) {
			/* WDS Destination Address Learning */
			dp_rx_da_learn(soc, rx_tlv_hdr, peer, nbuf);

			/* Due to HW issue, sometimes we see that the sa_idx
			 * and da_idx are invalid with sa_valid and da_valid
			 * bits set
			 *
			 * in this case we also see that value of
			 * sa_sw_peer_id is set as 0
			 *
			 * Drop the packet if sa_idx and da_idx OOB or
			 * sa_sw_peerid is 0
			 */
			if (!is_sa_da_idx_valid(soc, rx_tlv_hdr, nbuf,
						msdu_metadata)) {
				qdf_nbuf_free(nbuf);
				nbuf = next;
				DP_STATS_INC(soc, rx.err.invalid_sa_da_idx, 1);
				continue;
			}

			/* WDS Source Port Learning */
			if (qdf_likely(vdev->wds_enabled))
				dp_rx_wds_srcport_learn(soc,
							rx_tlv_hdr,
							peer,
							nbuf,
							msdu_metadata);

			/* Intrabss-fwd */
			if (dp_rx_check_ap_bridge(vdev))
				if (dp_rx_intrabss_fwd_li(soc, peer, rx_tlv_hdr,
							  nbuf,
							  msdu_metadata)) {
					nbuf = next;
					tid_stats->intrabss_cnt++;
					continue; /* Get next desc */
				}
		}

		dp_rx_fill_gro_info(soc, rx_tlv_hdr, nbuf, &rx_ol_pkt_cnt);

		dp_rx_mark_first_packet_after_wow_wakeup(vdev->pdev, rx_tlv_hdr,
							 nbuf);

		dp_rx_update_stats(soc, nbuf);

		dp_pkt_add_timestamp(peer->vdev, QDF_PKT_RX_DRIVER_ENTRY,
				     current_time, nbuf);

		DP_RX_LIST_APPEND(deliver_list_head,
				  deliver_list_tail,
				  nbuf);
		DP_STATS_INC_PKT(peer, rx.to_stack, 1,
				 QDF_NBUF_CB_RX_PKT_LEN(nbuf));
		if (qdf_unlikely(peer->in_twt))
			DP_STATS_INC_PKT(peer, rx.to_stack_twt, 1,
					 QDF_NBUF_CB_RX_PKT_LEN(nbuf));

		tid_stats->delivered_to_stack++;
		nbuf = next;
	}

	if (qdf_likely(deliver_list_head)) {
		if (qdf_likely(peer)) {
			dp_rx_deliver_to_pkt_capture(soc, vdev->pdev, peer_id,
						     pkt_capture_offload,
						     deliver_list_head);
			if (!pkt_capture_offload)
				dp_rx_deliver_to_stack(soc, vdev, peer,
						       deliver_list_head,
						       deliver_list_tail);
		} else {
			nbuf = deliver_list_head;
			while (nbuf) {
				next = nbuf->next;
				nbuf->next = NULL;
				dp_rx_deliver_to_stack_no_peer(soc, nbuf);
				nbuf = next;
			}
		}
	}

	if (qdf_likely(peer))
		dp_peer_unref_delete(peer, DP_MOD_ID_RX);

	if (dp_rx_enable_eol_data_check(soc) && rx_bufs_used) {
		if (quota) {
			num_pending =
				dp_rx_srng_get_num_pending(hal_soc,
							   hal_ring_hdl,
							   num_entries,
							   &near_full);
			if (num_pending) {
				DP_STATS_INC(soc, rx.hp_oos2, 1);

				if (!hif_exec_should_yield(scn, intr_id))
					goto more_data;

				if (qdf_unlikely(near_full)) {
					DP_STATS_INC(soc, rx.near_full, 1);
					goto more_data;
				}
			}
		}

		if (vdev && vdev->osif_fisa_flush)
			vdev->osif_fisa_flush(soc, reo_ring_num);

		if (vdev && vdev->osif_gro_flush && rx_ol_pkt_cnt) {
			vdev->osif_gro_flush(vdev->osif_vdev,
					     reo_ring_num);
		}
	}

	/* Update histogram statistics by looping through pdev's */
	DP_RX_HIST_STATS_PER_PDEV();
end:
	return rx_bufs_used; /* Assume no scale factor for now */
}

static bool noa_wlan_rx_pool(void *priv, u32 *cnt)
{
	struct dp_reo_rx_priv {
		struct dp_intr *int_ctx;
		uint32_t quota;
		struct dp_soc *soc;
		uint8_t rx_mask;
	}  *rx_priv = (struct dp_reo_rx_priv *)priv;
	/* Since NoA mode only support 1 output ring for WiFi RX, always using 0 */
	hal_ring_handle_t hal_ring_hdl = rx_priv->soc->reo_dest_ring[0].hal_srng;
	struct dp_intr_stats *intr_stats = &rx_priv->int_ctx->intr_stats;
	int work_done;

	work_done = dp_rx_process_li_qca(rx_priv->int_ctx, hal_ring_hdl, 0, rx_priv->quota);
	if (work_done) {
		intr_stats->num_rx_ring_masks[0]++;
		dev_info(client->dev, "rx mask 0x%x ring %d, work_done %d budget %d",
			rx_priv->rx_mask, 0,
			work_done, rx_priv->quota);
		rx_priv->quota -=  work_done;
		*cnt += work_done;
	}
	return false;
}

int _noa_wlan_rxbm_sync(void *priv, void *bufs, int cnt);
int _noa_wlan_rxbm_sync(void *priv, void *bufs, int cnt)
{
	return noa_wlan_fw_rxbm_sync(client, bufs, cnt, true);
}

static int noa_wlan_rxbm_sync(void *priv, u32 cnt, bool init)
{
	if (init) {
		struct dp_rx_attach_priv {
			struct dp_soc *dp_soc;
			uint32_t mac_id;
			struct dp_srng *dp_rxdma_srng;
			struct rx_desc_pool *rx_desc_pool;
			uint32_t num_req_buffers;
		} *attach_priv = (struct dp_rx_attach_priv *)priv;
		return dp_pdev_rx_buffers_attach_qca(attach_priv->dp_soc, attach_priv->mac_id,
			attach_priv->dp_rxdma_srng, attach_priv->rx_desc_pool,
			attach_priv->num_req_buffers);
	} else {
		struct dp_rx_replenish_priv {
			struct dp_soc *dp_soc;
			uint32_t mac_id;
			struct dp_srng *dp_rxdma_srng;
			struct rx_desc_pool *rx_desc_pool;
			uint32_t num_req_buffers;
			union dp_rx_desc_list_elem_t **desc_list;
			union dp_rx_desc_list_elem_t **tail;
			const char *func_name;
		} *replenish_priv = (struct dp_rx_replenish_priv *)priv;
		return dp_rx_buffers_replenish_qca(replenish_priv->dp_soc, replenish_priv->mac_id,
			replenish_priv->dp_rxdma_srng, replenish_priv->rx_desc_pool,
			replenish_priv->num_req_buffers,
			replenish_priv->desc_list, replenish_priv->tail, replenish_priv->func_name);
	}
}

/* physical plat_ops that is used at runtime. */
struct platform_bus_ops *plat_ops;
/* This google bus ops will be used in google_plat.h directly */
struct platform_bus_ops google_bus_ops = {
	.init = noa_wlan_init,
	.exit = noa_wlan_exit,
	.start = noa_wlan_start,
	.stop = noa_wlan_stop,
	.tx = noa_wlan_tx_packet,
	.rx = noa_wlan_rx_pool,
	.tx_cpl = NULL,
	.request_irq = noa_irq_request,
	.tx_queue_active = noa_wlan_txq_active,
	.sta_active = noa_wlan_sta_active,
	.rx_replenish = noa_wlan_rxbm_sync,
};
