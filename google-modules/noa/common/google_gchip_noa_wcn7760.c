// SPDX-License-Identifier: Google-proprietary
// Copyright 2024 Google LLC

#include "google_gchip_noa_wcn7760.h"
#include "tcl_data_cmd.h"
#include <linux/msi.h>
#include "hal_hw_headers.h"
#include "hal_api.h"
#include "hal_reo.h"
#include "hal_rx.h"
#include "hal_be_rx.h"
#include "hal_be_tx.h"
#include <dp_rx.h>
#include <dp_be.h>
#include <dp_be_rx.h>
#include <dp_be_tx.h>
#include "dp_rx_buffer_pool.h"

#define WCN_NOA_PCIE_REG_BASE 0xB0200000

extern struct noa_wlan_client* get_noa_wlan_client(void);
static struct noa_wlan_client *client = NULL;
bool is_noa_init = true;
u32 rx_buff_count = 0;
extern void noa_wcn_platform_init(void* dev, void* soc, struct platform_bus_ops* vendor_ops);

static int irq_start = 0;
static struct hif_exec_context *irq_map[MAX_IRQ_NUM];
static int noa_dump_bus(void *priv, char *buf, int len);
static int noa_dump_rings(void *priv, char *buf, int len);

struct ring_indices {
	u64 rd;
	u64 wr;
};

struct ring_indices idx;
u32 rx_ppt_id_start;

#define NUM_RING_ISRS_REG 2
enum RING_ISRS{
	RING_TX_ISR = 14,
	RING_RX_ISR = 19,
};

int ring_irqs_reg[] ={
		RING_TX_ISR,
		RING_RX_ISR,
};

bool is_noa_init_completed(void) {
    return is_noa_init;
}

static void noa_client_ring_query(struct hal_soc *hal,
	struct hal_srng_params *ring_params,
	struct noa_wlan_ring_info *info, uint8_t lmac_ring)
{
	u32 ring_id = ring_params->ring_id;
	struct noa_ring_regs *regs = &info->regs;
	struct hal_srng *hal_srng = &(hal->srng_list[ring_id]);
	unsigned long addr_offset;

	/* Ring configure register in share memory */
	info->hw_idx = ring_id;
	info->ndesc = ring_params->num_entries;
	info->desc_sz = ring_params->entry_size * 4;
	info->offset = 0;
	info->dma_pa = (unsigned long)ring_params->ring_base_paddr;
	info->dma_va = (unsigned long)ring_params->ring_base_vaddr;
	info->stride = ring_params->entry_size;

	if (ring_params->ring_dir == HAL_SRNG_SRC_RING) {
		regs->read = idx.rd + (ring_id * sizeof(*hal->shadow_rdptr_mem_vaddr));
		if (lmac_ring) {
			regs->write = idx.wr + ((ring_id-HAL_SRNG_LMAC1_ID_START) * sizeof(*hal->shadow_wrptr_mem_vaddr));
		} else {
			addr_offset = (unsigned long)(hal_srng->u.src_ring.hp_addr) -
				(unsigned long)(hal->dev_base_addr);
			regs->write = WCN_NOA_PCIE_REG_BASE + addr_offset;
		} 
	} else {
		regs->write = idx.rd + (ring_id * sizeof(*hal->shadow_rdptr_mem_vaddr));
		if (lmac_ring) {
			regs->read = idx.wr + ((ring_id-HAL_SRNG_LMAC1_ID_START) * sizeof(*hal->shadow_wrptr_mem_vaddr));
		} else {
			addr_offset = (unsigned long)(hal_srng->u.dst_ring.tp_addr) -
				(unsigned long)(hal->dev_base_addr);
			regs->read = WCN_NOA_PCIE_REG_BASE + addr_offset;
		}
	}
	snprintf(info->name,  RING_MAX_NAME, "%s%d",
		ring_params->ring_dir ? "rx_ring" : "tx_ring", ring_id);
	printk("%s %d", ring_params->ring_dir ? "rx_ring" : "tx_ring", ring_id);
	printk("%s %d read %llu write %llu", ring_params->ring_dir ? "rx_ring" : "tx_ring", ring_id,
		regs->read, regs->write);
}

extern int hal_get_srng_ring_id(struct hal_soc *hal, int ring_type,
				int ring_num, int mac_id);
bool oneRing = true;
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

	printk("%s oneRing %d" , __func__, oneRing);
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
			&ring_info[num++], ring_config->lmac_ring);
		if(oneRing)
			break;
	}
	*num_ring = num;
	return 0;
}

static int noa_wlan_bus_init(struct noa_wlan_client *client)
{
	printk("%s" , __func__);
	struct dp_soc *soc = client->bus;
	noa_wlan_ring_init(client, TCL_DATA, &client->tx_flow_max,
		client->tx_ring, soc->tcl_data_ring);
	noa_wlan_ring_init(client, REO_DST, &client->rx_flow_max,
		client->rx_ring, soc->reo_dest_ring);
	noa_wlan_ring_init(client, WBM2SW_RELEASE, &client->tx_cpl_flow_max,
		client->tx_cpl_ring, soc->tx_comp_ring);
	noa_wlan_ring_init(client, RXDMA_BUF, &client->rx_post_max,
		client->rx_post_ring, soc->rx_refill_buf_ring);
	printk("rx %d, tx %d, txcpl %d, rxpost %d", client->rx_flow_max, client->tx_flow_max, client->tx_cpl_flow_max, client->rx_post_max);
	client->rx_flow_max = 1;
	client->tx_flow_max = 1;
	client->tx_cpl_flow_max = 1;
	client->rx_post_max = 1;
	return 0;
}


static int noa_wlan_txq_active(void *priv, u16 flowid, bool enable)
{
	return noa_wlan_fw_txq_active(client, flowid, enable);
}

#define LOG_BUFF_SIZE 1024
static int noa_wlan_start(void* priv)
{
	printk("%s" , __func__);
	char buff[LOG_BUFF_SIZE];
	struct dp_soc *soc = client->bus;
	memset(buff, 0, LOG_BUFF_SIZE);
	noa_wlan_bus_init(client);
	client->ints_addr = 0;
	client->intm_addr = 0;
	client->doorbell_addr = 0;
	client->fw_trap_addr = (u64)rx_ppt_id_start;
	client->nep_tx_desc_sz = NOA_DESC_WLAN_TX_QCA_WCN_7760_BYTE;
	client->nep_tx_items = client->tx_ring[0].ndesc;
	client->nep_rx_desc_sz = NOA_DESC_BASIC_BYTE;
	client->nep_rx_items = client->rx_ring[0].ndesc;

	client->wdev_tx_post_desc_sz = 32;
	client->wdev_tx_cpl_desc_sz = 32; //sizeof(host_txbuf_cmpl_t);
	client->wdev_rx_post_desc_sz = 32; //sizeof(host_rxbuf_post_t);
	client->wdev_rx_cpl_desc_sz = 32; //sizeof(host_rxbuf_cmpl_t);
	noa_wlan_fw_start(client);

	printk("%s calling rxbm_sync buff_count %d" , __func__, rx_buff_count);
	noa_wlan_rxbm_sync(NULL, rx_buff_count, true);

	memset(buff, 0 , LOG_BUFF_SIZE);
	noa_wlan_txq_active(priv, 0, 1); //Temp change, TODO: with sta active
	soc->dpa_enable = true;
#if 0
	//TODO: Add runtime control to enable/disable ring dump
	client->dump_opt = REO_DST;
	int ret = noa_dump_rings(priv, buff, LOG_BUFF_SIZE);
	printk("%s ring dump  for REO_DST \n%s", __func__, buff);

	memset(buff, 0 , LOG_BUFF_SIZE);
	client->dump_opt = TCL_DATA;
	ret = noa_dump_rings(priv, buff, LOG_BUFF_SIZE);
	printk("%s ring dump  for TCL_DATA \n%s", __func__, buff);

	memset(buff, 0 , LOG_BUFF_SIZE);
	client->dump_opt = WBM2SW_RELEASE;
	ret = noa_dump_rings(priv, buff, LOG_BUFF_SIZE);
	printk("%s ring dump  for WBM2SW_RELEASE \n%s", __func__, buff);

	memset(buff, 0 , LOG_BUFF_SIZE);
	client->dump_opt = RXDMA_BUF;
	ret = noa_dump_rings(priv, buff, LOG_BUFF_SIZE);
	printk("%s ring dump  for RXDMA_BUF \n%s", __func__, buff);

	memset(buff, 0 , LOG_BUFF_SIZE);
	client->dump_opt = 1;// NOA_DUMP_BUS_TXDESC;
	ret = noa_dump_bus(priv, buff, LOG_BUFF_SIZE);
	printk("%s bus dump  %s", __func__, buff);
#endif
	return 0;
}

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

void noa_wlan_client_feedback_ring_update(uint32_t cookie)
{
	struct noa_desc desc = {0};
	uint32_t ppt_index;
	uint32_t spt_index;

	if (!client)
		return;
	ppt_index = cookie >> 9;
	spt_index = cookie & 0x1FF;
	desc.tkid = ((ppt_index - rx_ppt_id_start) << 9) | spt_index;
	desc.reason = FWD_REASON_FALLBACK;
	noa_wlan_feedback_ring_write(client, &client->feedback_ring.ring, &desc);
	noa_wlan_bm_remove(&client->vendor_rx_bm, desc.tkid);
	noa_wlan_mapper_unmap_by_tkid(client, WCN_MAPPER_POOL_APC_RX, desc.tkid);
}

struct wcn_noa_wlan_tx_args {
	struct noa_wlan_client *client;
	void *pkt;
};
#define TXCPL_RING_BASE 128

extern uint32_t dp_tx_comp_handler(struct dp_intr *int_ctx, struct dp_soc *soc,
			    hal_ring_handle_t hal_srng, uint8_t ring_id,
			    uint32_t quota);
/*
	struct dp_reo_rx_priv {
		struct dp_intr *int_ctx;
		uint32_t quota;
		struct dp_soc *soc;
		uint8_t rx_mask;
	}  *rx_priv = (struct dp_reo_rx_priv *)priv;
	hal_ring_handle_t hal_ring_hdl = rx_priv->soc->reo_dest_ring[0].hal_srng;
	struct dp_intr_stats *intr_stats = &rx_priv->int_ctx->intr_stats;
	*/
static void noa_txcpl_sync(void *bus, void *desc)
{
	struct dp_soc *soc = (struct dp_soc *)bus;
	struct dp_soc_be *be_soc = dp_get_be_soc_from_dp_soc(soc);
	uint8_t ring_id = 0;// tx_cpl->ring_id - TXCPL_RING_BASE;
	struct wbm_release_ring *tx_cpl = desc;
	uint32_t tkid = tx_cpl->released_buff_or_desc_addr_info.sw_buffer_cookie;
	struct dp_hw_cookie_conversion_t *cc_ctx = &be_soc->tx_cc_ctx[0];
	uint32_t ppt_id_start = DP_CMEM_OFFSET_TO_PPT_ID(cc_ctx->cmem_offset);
	uint32_t ppt_index = tx_cpl->released_buff_or_desc_addr_info.sw_buffer_cookie >> 9;
	uint32_t spt_index = tx_cpl->released_buff_or_desc_addr_info.sw_buffer_cookie & 0x1FF;
	uint32_t cookie = ((ppt_index + ppt_id_start) << 9) | spt_index;

	tx_cpl->released_buff_or_desc_addr_info.sw_buffer_cookie = cookie;
	noa_wlan_mapper_unmap_by_tkid(client, WCN_MAPPER_POOL_APC_TX, tkid);
	dp_tx_cmpl_handle(soc, ring_id, desc);
}

static void noa_wlan_isr(int irq)
{
	//printk("%s irq %d irq_start %d", __func__, irq, irq_start);
	struct hif_exec_context *hif_ext_group = NULL;
	if (irq >= 0 && irq < MAX_IRQ_NUM) {
		hif_ext_group = irq_map[irq];
		//irq = irq;
	} else if ((irq - irq_start) < MAX_IRQ_NUM && (irq - irq_start) >= 0) {
		hif_ext_group = irq_map[irq - irq_start];
		return;
	} else {
		printk("%s irq %d is out of irq_map", __func__, irq);
		return;
	}

	hif_ext_group_interrupt_handler(irq, hif_ext_group);
	return;
}

static ssize_t noa_wlan_tx(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	const struct wcn_noa_wlan_tx_args *tx_info = (const struct wcn_noa_wlan_tx_args *)data;
	struct noa_desc *d = (struct noa_desc *)buf;
	struct noa_qca_wcn7760_txd *ext_txd = (struct noa_qca_wcn7760_txd *)d->ext_data;
	struct sk_buff *skb = tx_info->pkt;
	struct qdf_nbuf_cb *cb = (struct qdf_nbuf_cb *)skb->cb;
	struct tcl_data_cmd *cmd = (struct tcl_data_cmd *)cb->u.tx.dev.priv_cb_w.ext_cb_ptr;
	struct dp_soc_be *be_soc = dp_get_be_soc_from_dp_soc((struct dp_soc *)tx_info->client->bus);
	//TODO: fix -> bool hotspot = cmd->addrx_en ? true : false;
	bool hotspot = be_soc->bank_profiles[cmd->bank_id].bank_config.addrx_en;
	/* record tx_desc first since they will be remark later */
	struct dp_hw_cookie_conversion_t *cc_ctx = &be_soc->tx_cc_ctx[0];
	u32 ppt_id_start = DP_CMEM_OFFSET_TO_PPT_ID(cc_ctx->cmem_offset);
	u8 *pkt_va = skb->data;
	u16 pkt_len = cmd->data_length;
	u32 ppt_index = cmd->buf_addr_info.sw_buffer_cookie >> 9;
	u32 spt_index = cmd->buf_addr_info.sw_buffer_cookie & 0x1FF;
	u16 pkt_id = ((ppt_index - ppt_id_start) << 9) | spt_index;

	unsigned long pa;
	int ret;
	struct noa_wlan_mapping_params params = {
		.cpu_addr = (void *)pkt_va,
		.size = pkt_len,
		.contiguous = true,
		.tkid_in_use = true,
		.pool_id = WCN_MAPPER_POOL_APC_TX,
		.tkid = pkt_id,
	};

	/* keep for debugging */
	//TODO: Fix this
	/*if (client->dump_opt == NOA_DUMP_BUS_TXDESC)
		dump_txd_qca("noa_wlan_tx", cmd); */
	pa = cmd->buf_addr_info.buffer_addr_39_32;
	pa = (pa << 32 | cmd->buf_addr_info.buffer_addr_31_0);
	/* remark tx_desc to noa_desc */
	memset(d, 0, NOA_DESC_WLAN_TX_QCA_WCN_7760_BYTE);
	/* assign extend */
	//TODO: Fix this.
	ext_txd->set_hlos_tid = cmd->hlos_tid;
	ext_txd->to_fw = cmd->to_fw;
	ext_txd->l3_checksum_en = cmd->ipv4_checksum_en;
	ext_txd->l4_checksum_en = cmd->udp_over_ipv4_checksum_en;

	ext_txd->ring_id = 0; //cmd->ring_id;
	ext_txd->bmid = 5; //cmd->buf_addr_info.return_buffer_manager;
	ext_txd->frag = cmd->buf_or_ext_desc_type;
	ext_txd->search_index = cmd->search_index & 0x3f;
	ext_txd->cache_set_num = cmd->cache_set_num;
	ext_txd->bank_id = cmd->bank_id;
	ext_txd->vdev_id = cmd->vdev_id;
	ext_txd->pmac_id = cmd->pmac_id;
	ext_txd->tx_notify_frame = cmd->tx_notify_frame;
	ext_txd->fw_metadata = cmd->tcl_cmd_number;
	/* add addrx, addry since they are different between STA, AP mode */
	//TODO: Fix this.
	/*ext_txd->addrx_en = cmd->addrx_en;
	ext_txd->addry_en = cmd->addry_en; */
	if (hotspot) {
		u8 *eth = pkt_va + cmd->packet_offset;
		struct noa_session_info info = {
			.ethertype = 0, /* means search from eth header */
			//.ifidx = cmd->lmac_id,
			.ifidx = cmd->pmac_id,
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

	ret = noa_wlan_mapper_remap(client, &params, &d->dv);
	if (ret) {
		dev_err(client->dev, "%s(): failed to remap dma address, err: %d\n", __func__, ret);
		return ret;
	}
	/* write ddone at last */
	d->ddone = 0;
	return 0;
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

#define REG_ADDR_SIZE 2097152
#define MAX_TX_PACKET_SIZE 4096
#define MAX_RX_PACKET_SIZE 65536
#define MAX_RX_BUFFER_SIZE 2048

static int noa_wcn7760_map_resources(struct noa_wlan_client *client, struct hal_soc *hal)
{
	struct noa_wlan_mapping_params p = {
		.size = 4,
		.contiguous = false,
		.noncache = true,
		.tkid_in_use = false,
	};

	p.cpu_addr = hal->shadow_rdptr_mem_vaddr;
	p.phy_addr = hal->shadow_rdptr_mem_paddr;
	p.size = 0x4000; //TODO: use size of shadow pointer
	p.contiguous = false;
	noa_wlan_mapper_remap(client, &p, &idx.rd);
	p.cpu_addr = hal->shadow_wrptr_mem_vaddr;
	p.phy_addr = hal->shadow_wrptr_mem_paddr;
	noa_wlan_mapper_remap(client, &p, &idx.wr);
	return 0;
}

static int noa_wlan_init(void *dev, void *priv)
{
	struct dp_soc *soc = priv;
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;
	qdf_device_t osdev = dev;
	struct dp_tx_desc_pool_s *tx_desc_pool = &soc->tx_desc[0];
	struct rx_desc_pool *rx_desc_pool = &soc->rx_desc_buf[0];
	struct hif_softc *scn = (struct hif_softc *)hal->hif_handle;
	struct hif_pci_softc *sc = HIF_GET_PCI_SOFTC(scn);
	struct noa_wlan_mapping_setup setup = {
		.profile_num = 2,
		.profile = {
			{
				.pool_id = WCN_MAPPER_POOL_APC_RX,
				.tkid_range = PKTID_MAX_MAP_SZ_RXCPLRING,
			},
			{
				.pool_id = WCN_MAPPER_POOL_APC_TX,
				.tkid_range = MAX_PKTID_TX,
			},
		},
	};
	int ret = 0;

	client = noa_wlan_client_alloc(&noa_ops);
	if (!client)
		return -ENOMEM;

	/* assign client*/
	client->bus = soc;
	/* assign client*/
	client->dev = osdev->dev;
	/* physical base address */
	dev_info(client->dev, "%s modifiedFeb25", __func__);
	dev_info(client->dev, "reg_addr %llu", sc->ce_sc.ol_sc.mem_pa);
	//17184063488
	client->reg_addr = 0x400400000;//sc->ce_sc.ol_sc.mem_pa;
	client->share_addr = 0x400300000;//0;
	client->reg_size = 200000;//;
	client->share_size = 200000*2;
	/* fixed info */
	client->rx_pkt_max = rx_desc_pool->pool_size; //MAX_RX_PACKET_SIZE;
	client->tx_pkt_max = MAX_TX_PACKET_SIZE; //tx_desc_pool->pool_size;
	/* sz*/
	client->rx_buf_sz = rx_desc_pool->buf_size; //MAX_RX_BUFFER_SIZE
	client->type = WLAN_FW_TYPE_QCA;
	client->rx_pkt_tlv_size = soc->rx_pkt_tlv_size;

	ret = noa_wlan_mapper_init(client, &setup);
	if (ret) {
		dev_err(client->dev, "%s(): failed to init mapper, err: %d\n", __func__, ret);
		return -EINVAL;
	} else {
		dev_err(client->dev, "noa_wlan_mapper_init successful");
	}
	dev_err(client->dev, "%s rxpool %d txpool %d rxbuf %d", __func__,
			rx_desc_pool->pool_size, tx_desc_pool->pool_size, rx_desc_pool->buf_size);
	ret = noa_wlan_client_register(client);
	if (ret) {
		dev_err(client->dev, "%s(): failed to register wlan client, err: %d\n", __func__, ret);
		return -EINVAL;
	} else {
		dev_err(client->dev, "noa_wlan_client_register successful");
	}
	return noa_wcn7760_map_resources(client, hal);
}

static int noa_wlan_get_irqs(struct hal_soc *hal, u32 *irqs)
{
	struct hif_softc *scn = HIF_GET_SOFTC(hal->hif_handle);
	struct HIF_CE_state *hif_state = HIF_GET_CE_STATE(scn);
	struct hif_exec_context *hif_ext_group;
	int i, j, irq;
	int cur = 0;
	dev_info(client->dev, "%s", __func__);
	irq_start = 0;
	dev_info(client->dev, "%s hif_num_extgroup = %d", __func__, hif_state->hif_num_extgroup);
	for (i = 0; i < hif_state->hif_num_extgroup && cur < MAX_IRQ_NUM; i++) {
		hif_ext_group = hif_state->hif_ext_group[i];
		dev_info(client->dev, "%s  numirq = %d", __func__, hif_ext_group->numirq);
		for (j = 0 ; j < hif_ext_group->numirq && cur < MAX_IRQ_NUM; j++) {
			irq = hif_ext_group->irq[j];
			if (cur == 0)
				irq_start = irq;
			irqs[cur++] = irq;
			irq_map[irq - irq_start] = hif_ext_group;
		}
	}
	return cur;
}

/*static void disable_irqs(u32 irq_nums, u32* irqs) {
	int id = 0;
	for (id = 0; id < irq_nums; id++) {
		//dev_err(client->dev, "disabling irq %d", irqs[id]);
		//disable_irq(irqs[id]);
		dev_err(client->dev, "freeing irq %d", irqs[id]);
		free_irq(irqs[id], client->dev);
	}
}*/
static int noa_irq_request(void *priv)
{
	struct dp_soc *soc = client->bus;
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;
	struct hif_softc *scn = (struct hif_softc *)hal->hif_handle;
	struct hif_pci_softc *sc = HIF_GET_PCI_SOFTC(scn);
	struct msi_desc *entry;
	struct HIF_CE_state *hif_state = HIF_GET_CE_STATE(scn);

	dev_info(client->dev, "%s", __func__);
	client->irq_nums = noa_wlan_get_irqs(hal, client->irqs);
	client->irq_nums = 2;
	for (int i = 0; i < hif_state->hif_num_extgroup; i++) {
		struct hif_exec_context *hif_ext_group = hif_state->hif_ext_group[i];
		if (hif_ext_group->irq_requested) {
			for (int j = 0; j < hif_ext_group->numirq; j++) {
				int irq = hif_ext_group->os_irq[j];
				if (irq == ring_irqs_reg[0] ||
				    irq == ring_irqs_reg[1]) {
					// 1. Disable first
					disable_irq(irq);
					// 2. Free using the correct dev_id (hif_ext_group)
					pfrm_free_irq(scn->qdf_dev->dev, irq, hif_ext_group);
					dev_info(client->dev, "Freed irq %d", irq);
					hif_ext_group->irq_requested = false;
				}
			}
		}
	}

	msi_lock_descs(&sc->pdev->dev);
	msi_for_each_desc(entry, &sc->pdev->dev, MSI_DESC_ASSOCIATED)
	{
		for (int j = 0; j < NUM_RING_ISRS_REG; j++) {
			if (entry->msi_index == ring_irqs_reg[j]) {
				client->msi_descs[j].nvec_used = 1; // MSI-X uses 1 vector per desc
				client->msi_descs[j].msi_index = entry->msi_index;
				client->msi_descs[j].data = entry->msg.data;
				client->irqs[j] = entry->irq;
				dev_info(client->dev, "Mapping MSI %d to OS IRQ %d , msg data %d",
					 entry->msi_index, entry->irq, entry->msg.data);
			}
		}
	}
	msi_unlock_descs(&sc->pdev->dev);

	return noa_wlan_irq_request(client);
}

static void noa_wlan_exit(void *priv)
{
	noa_wlan_client_unregister(client);
	noa_wlan_mapper_deinit(client);
	noa_wlan_client_free(client);
	client = NULL;
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

static int noa_wlan_tx_packet(void *priv, void *pkt, u32 vdev_id)
{
	const struct wcn_noa_wlan_tx_args args = {
		.client = client,
		.pkt = pkt,
	};
	return noa_wlan_client_ring_write(client, &client->tx_data_ring.ring, (void *)&args);
}

static int noa_wlan_sta_active(void *priv, struct sta_info *info, bool enable)
{
	int i;
	int ret = noa_wlan_fw_sta_active(client, info, enable);

	if (ret) {
		dev_err(client->dev, "sta_active fail.\n");
		return ret;
	}

	for (i = 0 ; i < client->tx_flow_max; i++) {
		//TODO: currently enabling tx queue at startup itself
		//ret = platform_bus_tx_queue_active(priv, i, enable);
		if (ret) {
			dev_err(client->dev, "tx_queue %d active fail.\n", i);
			return ret;
		}
	}
	return ret;
}

static bool noa_wlan_rx_poll(void *priv, u32 *cnt)
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

	work_done = dp_rx_process_be_qca(rx_priv->int_ctx,
					 hal_ring_hdl, 0,
					 rx_priv->quota, (void *)client,
					 rx_ppt_id_start);
	if (work_done) {
		intr_stats->num_rx_ring_masks[0]++;
		rx_priv->quota -=  work_done;
		*cnt += work_done;
	}
	return false;
}

static bool noa_wlan_tx_cpl(void *priv, u32 *cnt)
{
	if (unlikely(!client)) {
		pr_err("%s(): Invalid client context\n", __func__);
		return false;
	}

	if (noa_wlan_client_ring_is_empty(&client->direct_tx_cpl_ring[0].ring))
		return 0;
	return noa_wlan_fw_txcpl(client, cnt);
}

int _noa_wlan_rxbm_sync(void *priv, void *bufs, int cnt)
{
	struct noa_bm_buf *bm_bufs = (struct noa_bm_buf *)bufs;
 	struct noa_bm_buf *buf;
	struct noa_wlan_mapping_params params = {
		.contiguous = true,
		.tkid_in_use = true,
		.pool_id = WCN_MAPPER_POOL_APC_RX,
	};
	u32 i = 0;
	int ret = 0;

	for (i = 0; i < cnt; i++) {
		buf = &bm_bufs[i];
		buf->len += WLAN_PKT_PAD;
		buf->pa -= WLAN_PKT_PAD;
		buf->apc_va -= WLAN_PKT_PAD;
		params.cpu_addr = (void *)buf->apc_va;
		params.phy_addr = buf->pa;
		params.size = buf->len;
		params.tkid = buf->pktid;
		ret = noa_wlan_mapper_remap(client, &params, &buf->dpa_va);//TODO: Handle unmapping
		if (ret) {
			dev_err(client->dev, "%s(): failed to remap dma address, err: %d\n",
					__func__, ret);
			continue;
		}
	}
	return noa_wlan_fw_rxbm_sync(client, bufs, cnt, true);
}

void noa_update_rx_buff_count(u32 count) {
	rx_buff_count = count;
}

void noa_update_rx_ppt_id_start(u32 ppt_id_start)
{
	rx_ppt_id_start = ppt_id_start;
}

int noa_wlan_rxbm_sync(void *priv, u32 cnt, bool init)
{
	if (client == NULL) {
		printk("Client ptr is null cnt %d init %d\n", cnt, init);
		return 0;
	}

	if (!init) {
		dp_rx_replenish_priv *replenish_priv = (dp_rx_replenish_priv *)priv;
		return __dp_rx_buffers_replenish_qca(
				replenish_priv->dp_soc, replenish_priv->mac_id,
				replenish_priv->dp_rxdma_srng, replenish_priv->rx_desc_pool,
				replenish_priv->num_req_buffers, replenish_priv->desc_list,
				replenish_priv->tail, replenish_priv->req_only,
				replenish_priv->force_replenish, rx_ppt_id_start,
				replenish_priv->func_name);
	}
	return 0;
}
static int wcn_noa_wlan_init(void *dev, void *priv){
	printk("%s", __func__);
	return 0;
}
static void wcn_noa_wlan_exit(void *priv)
{
	printk("%s", __func__);
}
static int wcn_noa_wlan_start(void* priv) {
	printk("%s", __func__);
	return 0;	
}
static void wcn_noa_wlan_stop(void *priv) 
{
	struct dp_soc *soc = (struct dp_soc *)priv;
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;
	struct hif_softc *scn = (struct hif_softc *)hal->hif_handle;
	struct HIF_CE_state *hif_state = HIF_GET_CE_STATE(scn);
	struct hif_pci_softc *sc = HIF_GET_PCI_SOFTC(scn);
	struct msi_desc *entry;

	msi_lock_descs(&sc->pdev->dev);
	msi_for_each_desc(entry, &sc->pdev->dev, MSI_DESC_ASSOCIATED) {
		for (int j = 0; j < NUM_RING_ISRS_REG; j++) {
			if (entry->msi_index == ring_irqs_reg[j]) {
				disable_irq(entry->irq);
				for (int i = 0; i < hif_state->hif_num_extgroup; i++) {
					struct hif_exec_context *hif_ext_group = hif_state->hif_ext_group[i];
					struct hif_napi_exec_context *ctx;
					ctx = hif_exec_get_napi(hif_ext_group);
					for (int k = 0; k < hif_ext_group->numirq; k++) {
						if (hif_ext_group->os_irq[k] == entry->irq) {
							napi_synchronize(&ctx->napi);
							printk("Freed MSI %d OS IRQ %d , msg data %d \n", entry->msi_index, entry->irq, entry->msg.data);
						}
					}
				}
			}
		}
	}
	msi_unlock_descs(&sc->pdev->dev);
}

static bool wcn_noa_wlan_rx(void *priv, u32 *cnt)
{
	struct dp_reo_rx_priv {
		struct dp_intr *int_ctx;
		uint32_t quota;
		struct dp_soc *soc;
		uint8_t rx_mask;
	}  *rx_priv = (struct dp_reo_rx_priv *)priv;
	struct dp_soc *soc = rx_priv->soc;
	struct dp_intr *int_ctx = rx_priv->int_ctx;
	struct dp_intr_stats *intr_stats = &int_ctx->intr_stats;
	uint32_t quota = rx_priv->quota;
	uint8_t rx_mask = rx_priv->rx_mask;
	int ring;
	int work_done;

	for (ring = 0; ring < soc->num_reo_dest_rings; ring++) {
		if (!(rx_mask & (1 << ring)))
			continue;
		work_done = dp_rx_process_be_bn(
				int_ctx,
				soc->reo_dest_ring[ring].hal_srng,
				ring,
				quota);
		if (work_done) {
			intr_stats->num_rx_ring_masks[ring]++;
			dp_verbose_debug("rx mask 0x%x ring %d, work_done %d budget %d",
						rx_mask, ring,
						work_done, quota);
			quota -=  work_done;
			*cnt += work_done;
			if (quota <= 0)
				break;
		}
	}
	return false;
}

static int wcn_bus_rx_replenish(void *priv, u32 cnt, bool init)
{
	if (init) {
	} else {
		dp_rx_replenish_priv *replenish_priv= (dp_rx_replenish_priv *)priv;
		return ___dp_rx_buffers_replenish(replenish_priv->dp_soc, replenish_priv->mac_id,
			replenish_priv->dp_rxdma_srng, replenish_priv->rx_desc_pool,
			replenish_priv->num_req_buffers, replenish_priv->desc_list,
			replenish_priv->tail, replenish_priv->req_only,
			replenish_priv->force_replenish, replenish_priv->func_name);
	}
	return 0;
}

/* physical plat_ops that is used at runtime. */
struct platform_bus_ops *plat_ops;
struct platform_bus_ops google_bus_ops = {
	.init = noa_wlan_init,
	.exit = noa_wlan_exit,
	.start = noa_wlan_start,
	.stop = noa_wlan_stop,
	.tx = noa_wlan_tx_packet,
	.rx = noa_wlan_rx_poll,
	.tx_cpl = noa_wlan_tx_cpl,
	.request_irq = noa_irq_request,
	.tx_queue_active = noa_wlan_txq_active,
	.sta_active = noa_wlan_sta_active,
	.rx_replenish = noa_wlan_rxbm_sync,
};
struct platform_bus_ops wcn_bus_ops = {
	.init = wcn_noa_wlan_init,
	.exit = wcn_noa_wlan_exit,
	.start = wcn_noa_wlan_start,
	.stop = wcn_noa_wlan_stop,
	.tx = dp_tx_hw_be_tx,
	.rx = wcn_noa_wlan_rx,
	.tx_cpl = wcn_noa_bus_tx_cpl,
	//.request_irq = noa_irq_request,
	//.tx_queue_active = noa_wlan_txq_active,
	//.sta_active = noa_wlan_sta_active,
	.rx_replenish = wcn_bus_rx_replenish,
};
void wcn_noa_init(void* osdev, void* soc) {
	printk("processing noa init, sending wcn_bus_ops");
	platform_bus_init((void *)osdev, (void *)soc, &wcn_bus_ops);
}
