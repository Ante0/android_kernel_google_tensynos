// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform Dependent file for Hikey
 *
 * Copyright (C) 2022, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 *
 */
#include <linux/vmalloc.h>
#include <linux/msi.h>
#include <wlan/noa_wlan_client.h>
#include <wlan/noa_wlan_nep_helper.h>
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include <dhd_linux_priv.h>
#include <dhd_msgbuf.h>

#include "dhd_custom_google_noa_4390.h"
#include "dhd_custom_google_noa_trace.h"
#include "noa_wlan_buffer_management.h"

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
#include <tether/noa_vpn_manager.h>
#endif

struct ring_indices_buf {
	u64 rd_buf;
	u64 wr_buf;
};

struct dhd_custom_google_noa_4390 {
	struct ring_indices_buf h2d_ring_indeices;
	struct ring_indices_buf d2h_ring_indeices;
};

static struct noa_wlan_client *client;
static ssize_t dhd_noa_wlan_tx(void *buf, size_t buf_len, const void *data, size_t data_len);

static int dhd_noa_dump_ring(dhd_pub_t *dhd, msgbuf_ring_t *ring, char *buf, int len)
{
	int cnt = 0;
	uint16 rd, wr, drd, dwr;
	uint32 dma_buf_len = ring->max_items * ring->item_len;
	ulong ring_tcm_rd_addr; /* dongle address */
	ulong ring_tcm_wr_addr; /* dongle address */

	ring_tcm_rd_addr = dhd->bus->ring_sh[ring->idx].ring_state_r;
	ring_tcm_wr_addr = dhd->bus->ring_sh[ring->idx].ring_state_w;

	cnt += scnprintf(
		buf + cnt, len - cnt,
		"name: %s, type: %d, ring: %p, idx: %d, init: %d, item_len: %d, max_items: %d\n",
		ring->name, ring->ring_type, ring, ring->idx, ring->inited, ring->item_len,
		ring->max_items);

	cnt += scnprintf(buf + cnt, len - cnt,
			 "Mem Info: BASE(VA) %p BASE(PA) %x:%x tcm_rd_wr 0x%lx:0x%lx SIZE %d\n",
			 ring->dma_buf.va, ltoh32(ring->base_addr.high_addr),
			 ltoh32(ring->base_addr.low_addr), ring_tcm_rd_addr, ring_tcm_wr_addr,
			 dma_buf_len);

	cnt += scnprintf(buf + cnt, len - cnt, "From Host mem: RD: %d WR %d\n", ring->rd, ring->wr);

	if (dhd->dma_d2h_ring_upd_support) {
		if (ring->idx < dhd->bus->max_cmn_rings) {
			drd = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, H2D_DMA_INDX_WR_UPD, ring->idx);
		} else {
			drd = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_RD_UPD, ring->idx);
			dwr = dhd_prot_dma_indx_get(dhd, D2H_DMA_INDX_WR_UPD, ring->idx);
		}
		cnt += scnprintf(buf + cnt, len - cnt, "From Host DMA mem: RD: %d WR %d\n", drd,
				 dwr);
	} else {
		if (ring->idx < DHD_FLOWRING_START_FLOWID) {
			if (dhd->bus->is_linkdown) {
				cnt += scnprintf(
					buf + cnt, len - cnt,
					"From Shared Mem: RD and WR are invalid due to PCIe link down\n");
			} else {
				dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
				dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
				cnt += scnprintf(buf + cnt, len - cnt,
						 "From Shared Mem: RD: %d WR %d\n", rd, wr);
			}
		}
	}
	{
		u32 rd = 0, wr = 0, item_len = 0, max_item = 0, base = 0;
		dhd_bus_resume(dhd, 0);
		/* CAUTION: ring::base_addr already in Little Endian */
		dhd_bus_cmn_readshared(dhd->bus, &base, RING_BUF_ADDR, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &max_item, RING_MAX_ITEMS, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &item_len, RING_ITEM_LEN, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &wr, RING_WR_UPD, ring->idx);
		dhd_bus_cmn_readshared(dhd->bus, &rd, RING_RD_UPD, ring->idx);
		cnt += scnprintf(buf + cnt, len - cnt,
				 "RD: %d WR: %d MAX: %d, Len: %d, Base: %lx\n", rd, wr,
				 max_item & 0xffff, item_len & 0xffff, (unsigned long)base);
	}
	return cnt;
}

static int dhd_noa_dump_bus(void *priv, char *buf, int len)
{
	dhd_bus_t *bus = priv;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	int cnt = 0;

	cnt += scnprintf(
		buf + cnt, len - cnt,
		"dma_h2d_ring_upd_support: %d, d2h_intr_control: %d, buscoreidx: %d, socitype: %d\n",
		dhd->dma_h2d_ring_upd_support, bus->d2h_intr_control, bus->sih->buscoreidx,
		CHIPTYPE(bus->sih->socitype));
	cnt += scnprintf(buf + cnt, len - cnt,
			 "pcie: %s, irq: %d,intr: %d, irq_registered: %d, multi-en: %d\n",
			 pci_name(bus->dev), bus->dev->irq, bus->intr, bus->irq_registered,
			 MULTIBP_ENAB(bus->sih));
	cnt += scnprintf(buf + cnt, len - cnt,
			 "host_irq_enable_count: %lu, host_irq_disable_count: %lu, intrcount: %d\n",
			 bus->host_irq_enable_count, bus->host_irq_disable_count, bus->intrcount);
	cnt += scnprintf(buf + cnt, len - cnt,
			 "dngl_intmask_enable_count: %lu, dngl_intmask_disable_count: %lu\n",
			 bus->dngl_intmask_enable_count, bus->dngl_intmask_disable_count);
	cnt += scnprintf(buf + cnt, len - cnt,
			 "pcie_mb_intr_osh: %p, pcie_mb_intr_addr: %p, pcie_mb_intr_2_addr: %p\n",
			 bus->pcie_mb_intr_osh, bus->pcie_mb_intr_addr, bus->pcie_mb_intr_2_addr);
	cnt += scnprintf(buf + cnt, len - cnt,
			 "pcie_mailbox_int: %x, pcie_mailbox_mask: %x, def_intmask: %x\n",
			 bus->pcie_mailbox_int, bus->pcie_mailbox_mask, bus->def_intmask);
	cnt += scnprintf(buf + cnt, len - cnt, "rxbm_avail_cnt: %d\n",
			 dhd_pktid_map_get_avail(dhd, prot->pktid_rx_map));
	return cnt;
}

static void dhd_noa_sync_bus(void *priv)
{
	dhd_bus_t *bus = priv;

	client->pcie_msi_apc_intr_cnt = bus->intrcount;
}

static int dhd_noa_dump_rings(void *priv, char *buf, int len)
{
	dhd_bus_t *bus = priv;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	msgbuf_ring_t *ring;
	uint16 flowid;
	u32 h2d_flowrings_total = dhd_get_max_flow_rings(dhd);
	int cnt = 0;

	cnt += dhd_noa_dump_ring(dhd, &prot->d2hring_ctrl_cpln, buf + cnt, len - cnt);
	cnt += dhd_noa_dump_ring(dhd, &prot->h2dring_ctrl_subn, buf + cnt, len - cnt);
	cnt += dhd_noa_dump_ring(dhd, &prot->d2hring_rx_cpln, buf + cnt, len - cnt);
	cnt += dhd_noa_dump_ring(dhd, &prot->h2dring_rxp_subn, buf + cnt, len - cnt);
	cnt += dhd_noa_dump_ring(dhd, &prot->d2hring_tx_cpln, buf + cnt, len - cnt);
	FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, h2d_flowrings_total)
	{
		if (ring->inited)
			cnt += dhd_noa_dump_ring(dhd, ring, buf + cnt, len - cnt);
	}
	cnt += scnprintf(buf + cnt, len - cnt,
			 "max_rxbufpost: %d, rxbufpost: %d, rx_bufpost_threshold:%d\n",
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
			 prot->max_rxbufpost, atomic_read(&prot->rxbufpost),
#else
			 prot->max_rxbufpost, prot->rxbufpost,
#endif
			 prot->rx_bufpost_threshold);
	cnt += scnprintf(buf + cnt, len - cnt, "host_sfhllc_supported: %d\n",
			 dhd->host_sfhllc_supported);
	return cnt;
}

void dhdpcie_bus_ringbell_fast(struct dhd_bus *bus, uint32 value);
static void dhd_noa_ringbell(void *bus, u32 value)
{
	dhdpcie_bus_ringbell_fast((struct dhd_bus *)bus, value);
}

extern void dhd_prot_txstatus_process(dhd_pub_t *dhd, void *msg);
static void dhd_noa_txcpl_sync(void *bus, void *desc)
{
	struct dhd_bus *_bus = bus;
	struct host_txbuf_cmpl *txcpl = desc;
	u32 tkid = txcpl->cmn_hdr.request_id;

	noa_wlan_mapper_unmap_by_tkid(client, DHD_MAPPER_POOL_APC_TX, tkid);
	dhd_prot_txstatus_process(_bus->dhd, desc);
}

extern irqreturn_t dhdpcie_isr(int irq, void *arg);
static void noa_wlan_isr(int irq)
{
	dhdpcie_isr(irq, client->bus);
	return;
}

extern int dhd_pktid_rxbm_sync(dhd_pub_t *dhd);
extern int dhd_flow_rings_sync(dhd_pub_t *dhd);
#ifdef CONFIG_NOA_PCIE_SUPPORT
extern int dhd_sync_back_pci_dev_state(void *bus, void *state);
#endif /* CONFIG_NOA_PCIE_SUPPORT */
static void noa_wlan_dyswitch_sync(void *bus)
{
	struct dhd_bus *_bus = bus;
	// dhd_pktid_rxbm_sync(_bus->dhd);
	dhd_flow_rings_sync(_bus->dhd);
}

extern int dhd_prot_inc_hostactive_devwake_assert(dhd_bus_t *bus, const char *context);
extern void dhd_prot_dec_hostactive_ack_pending_dsreq(dhd_bus_t *bus, const char *context);
static int dhd_noa_manage_power(void *bus, bool acquire)
{
	dhd_bus_t *busp = bus;
	dhd_pub_t *dhdp = busp->dhd;

	if (!dhdp || !dhdp->bus) {
		pr_err("DHD context is not valid");
		return -EINVAL;
	}

#if defined(CONFIG_NOA_POWER_SAVING) && defined(PCIE_INB_DW) && defined(DHD_PCIE_RUNTIMEPM)
	if (acquire) {
		if (DHD_BUS_CHECK_SUSPEND_OR_SUSPEND_IN_PROGRESS(dhdp)) {
			dhdpcie_runtime_bus_wake(dhdp, TRUE, dhd_wl_ioctl);
		}
		dhd_prot_inc_hostactive_devwake_assert(busp, "noa");
		DHD_STOP_RPM_TIMER(dhdp);
	} else {
		dhd_prot_dec_hostactive_ack_pending_dsreq(busp, "noa");
		DHD_START_RPM_TIMER(dhdp);
	}
#endif

	return 0;
}

static int dhd_noa_wlan_rx_handover(void *bus_priv);

/* NOA driver used operation */
static struct noa_wlan_client_ops noa_ops = {
	.dump_ring = dhd_noa_dump_rings,
	.dump_bus = dhd_noa_dump_bus,
	.sync_bus = dhd_noa_sync_bus,
	.ringbell = dhd_noa_ringbell,
	.txcpl_sync = dhd_noa_txcpl_sync,
	.rx_isr = noa_wlan_isr,
	.write_payload = dhd_noa_wlan_tx,
#ifdef CONFIG_NOA_PCIE_SUPPORT
	.sync_back_pci_dev_state = dhd_sync_back_pci_dev_state,
#endif /* CONFIG_NOA_PCIE_SUPPORT */
	.manage_power = dhd_noa_manage_power,
	.rx_handover = dhd_noa_wlan_rx_handover,
};

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
static void dhd_noa_wlan_setup_xfrmdev_ops(dhd_bus_t *bus)
{
	dhd_pub_t *dhdp = bus->dhd;
	dhd_info_t *dhdi = dhdp->info;

	for (int i = 0; i < DHD_MAX_IFS; i++) {
		if (dhdi->iflist[i]) {
			struct net_device *net = dhdi->iflist[i]->net;

			if (net)
				noa_vpn_manager_setup_xfrmdev_ops(net);
		}
	}
}
#endif

#define PKTID_MAX_MAP_SZ_RXCPLRING (8 * 1024)
static int dhd_noa_wlan_init(void *dev, void *priv)
{
	u64 size;
	dhd_bus_t *bus = priv;
	struct pci_dev *pdev = dev;
	struct noa_wlan_mapping_setup setup = {
		.profile_num = 2,
		.profile = {
			{
				.pool_id = DHD_MAPPER_POOL_APC_RX,
				.tkid_range = PKTID_MAX_MAP_SZ_RXCPLRING,
			},
			{
				.pool_id = DHD_MAPPER_POOL_APC_TX,
				.tkid_range = MAX_PKTID_TX,
			},
		},
	};
	int ret;

	client = noa_wlan_client_alloc(&noa_ops);
	if (!client)
		return -ENOMEM;
	client->plat_priv = kmalloc(sizeof(struct dhd_custom_google_noa_4390), GFP_KERNEL);
	if (!client->plat_priv)
		return -ENOMEM;

	/* assign client*/
	client->bus = bus;
	client->dev = &pdev->dev;
	/* physical base address */
	client->reg_addr = pci_resource_start(pdev, 0);
	client->share_addr = pci_resource_start(pdev, 2);
	client->reg_size = DONGLE_REG_MAP_SIZE;
	size = pci_resource_len(pdev, 2);
	client->share_size = (size > DONGLE_TCM_MAP_SIZE) ? size : DONGLE_TCM_MAP_SIZE;
	/* fixed info */
	client->rx_pkt_max = PKTID_MAX_MAP_SZ_RXCPLRING;
	client->tx_pkt_max = MAX_PKTID_TX;
	/* sz*/
	client->rx_buf_sz = DHD_FLOWRING_RX_BUFPOST_PKTSZ;
	client->type = WLAN_FW_TYPE_BRCM_4390;
	/* configuration space */
	pci_save_state(pdev);

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
	dhd_noa_wlan_setup_xfrmdev_ops(bus);
#endif
	client->fw_active = false;

	ret = noa_wlan_mapper_init(client, &setup);
	if (ret) {
		dev_err(client->dev, "%s(): failed to init mapper, err: %d\n", __func__, ret);
		return -EINVAL;
	}

	return noa_wlan_client_register(client);
}

static int dhd_noa_wlan_dma_buf_remap(dhd_dma_buf_t *dma_buf, u64 *mapped_addr, bool cache)
{
	struct noa_wlan_mapping_params params;
	int ret;
	u64 pa;

	pa = dma_buf->pa.hiaddr;
	pa = pa << 32 | dma_buf->pa.loaddr;

	params.cpu_addr = (void *)(dma_buf->va);
	params.phy_addr = (dma_addr_t)(pa);
	params.size = dma_buf->_alloced;
	params.contiguous = false;
	params.tkid_in_use = false;
	params.noncache = cache ? false : true;
	ret = noa_wlan_mapper_remap(client, &params, mapped_addr);
	if (ret) {
		dev_err(client->dev, "%s(): failed to remap address, err: %d\n", __func__, ret);
	}

	return ret;
}

static void dhd_noa_wlan_ring_indices_deinit(struct noa_wlan_client *client)
{
	// TODO - It is necessary to perform a memory unmap operation.
}

static int dhd_noa_wlan_ring_indices_init(struct noa_wlan_client *client)
{
	struct dhd_custom_google_noa_4390 *custom_plat =
		(struct dhd_custom_google_noa_4390 *)client->plat_priv;
	dhd_bus_t *bus = client->bus;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	struct ring_indices_buf *ring_indices_buf;

	// D2H
	ring_indices_buf = &custom_plat->d2h_ring_indeices;
	dhd_noa_wlan_dma_buf_remap(&prot->d2h_dma_indx_rd_buf, &ring_indices_buf->rd_buf, false);
	dhd_noa_wlan_dma_buf_remap(&prot->d2h_dma_indx_wr_buf, &ring_indices_buf->wr_buf, false);

	// H2D
	ring_indices_buf = &custom_plat->h2d_ring_indeices;
	dhd_noa_wlan_dma_buf_remap(&prot->h2d_dma_indx_rd_buf, &ring_indices_buf->rd_buf, false);
	dhd_noa_wlan_dma_buf_remap(&prot->h2d_dma_indx_wr_buf, &ring_indices_buf->wr_buf, false);

	return 0;
}

extern void dhdpcie_disable_msi(struct pci_dev *pdev);
static void dhd_noa_wlan_exit(void *priv)
{
	dhd_bus_t *bus = (dhd_bus_t *)priv;
	struct pci_dev *pdev = bus->dev;

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return;
	}

	dhdpcie_bus_intr_disable(bus);
	dhd_bus_stop_queue((dhd_bus_t *)priv);
	dhd_noa_wlan_ring_indices_deinit(client);
	if (client->plat_priv)
		kfree(client->plat_priv);
	noa_wlan_client_unregister(client);
	/* noa_wlan_client_unregister free the irq then can disable msi. */
	if (bus->d2h_intr_method == PCIE_MSI) {
		dhdpcie_disable_msi(pdev);
	}
	noa_wlan_mapper_deinit(client);
	noa_wlan_client_free(client);
	client = NULL;
}

static void dhd_client_ring_query(struct noa_wlan_client *client, dhd_bus_t *bus,
				  msgbuf_ring_t *ring, struct noa_wlan_ring_info *info, bool d2h)
{
	u32 addr;
	u64 dpa_va;
	u8 *va;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	struct noa_ring_regs *regs = &info->regs;
	unsigned long dma_pa, dma_va;
	struct dhd_custom_google_noa_4390 *custom_plat =
		(struct dhd_custom_google_noa_4390 *)client->plat_priv;
	struct ring_indices_buf *h2d_ring_indices = &custom_plat->h2d_ring_indeices;
	struct ring_indices_buf *d2h_ring_indices = &custom_plat->d2h_ring_indeices;

	/* get ring addr*/
	dma_pa = ring->base_addr.high_addr;
	dma_pa = dma_pa << 32 | ring->base_addr.low_addr;
	dma_va = (unsigned long)ring->dma_buf.va;
	/* Ring configure register in share memory */
	addr = DHD_RING_MEM_MEMBER_ADDR(bus, ring->idx, len_items);
	regs->len = (unsigned long)addr;
	addr = DHD_RING_MEM_MEMBER_ADDR(bus, ring->idx, max_item);
	regs->max_item = (unsigned long)addr;
	addr = DHD_RING_MEM_MEMBER_ADDR(bus, ring->idx, base_addr);
	regs->base = (unsigned long)addr;
	info->hw_idx = ring->idx;
	info->ndesc = ring->max_items;
	info->desc_sz = ring->item_len;
	info->offset = (d2h) ? DHD_D2H_RING_OFFSET(ring->idx, bus->max_submission_rings) :
			       DHD_H2D_RING_OFFSET(ring->idx);
	info->offset = info->offset * prot->rw_index_sz;
	info->dma_pa = dma_pa;
	info->dma_va = dma_va;
	info->stride = 1;
	info->sn = ring->seqnum % D2H_EPOCH_MODULO;
	//dpa view regs
	// Read index
	dpa_va = (d2h) ? d2h_ring_indices->rd_buf : h2d_ring_indices->rd_buf;
	regs->read = (u64)(dpa_va + info->offset);
	// Write index
	dpa_va = (d2h) ? d2h_ring_indices->wr_buf : h2d_ring_indices->wr_buf;
	regs->write = (u64)(dpa_va + info->offset);
	// cpu view regs
	regs = &info->cpu_regs;
	va = (d2h) ? prot->d2h_dma_indx_rd_buf.va : prot->h2d_dma_indx_rd_buf.va;
	regs->read = (unsigned long)(va + info->offset);
	va = (d2h) ? prot->d2h_dma_indx_wr_buf.va : prot->h2d_dma_indx_wr_buf.va;
	regs->write = (unsigned long)(va + info->offset);
	strncpy(info->name, ring->name, RING_MAX_NAME);
}

static int dhd_noa_wlan_ring_syncback(dhd_bus_t *bus, msgbuf_ring_t *ring,
				      const struct noa_wlan_ring_info *info)
{
	ring->rd = *(uint16_t *)(info->cpu_regs.read);
	ring->wr = *(uint16_t *)(info->cpu_regs.write);
	ring->curr_rd = *(uint16_t *)(info->cpu_regs.read);
	ring->seqnum = info->sn;
	return 0;
}

static int dhd_noa_wlan_bus_init(struct noa_wlan_client *client)
{
	dhd_bus_t *bus = client->bus;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	int i;
	int ret;

	ret = dhd_noa_wlan_ring_indices_init(client);
	if (ret) {
		dev_err(client->dev, "%s(): Ring indices initialization failed. err: %d\n",
			__func__, ret);
		return ret;
	}

	/*	RX ring info */
	client->rx_flow_max = 1;
	client->rx_buf_sz = prot->rxbufpost_sz;
	client->rx_metadata_offset = prot->rx_metadata_offset;
	dhd_client_ring_query(client, bus, &prot->d2hring_rx_cpln, &client->rx_ring[0], true);
	/* TX CPL ring info query*/
	client->tx_cpl_flow_max = 1;
	dhd_client_ring_query(client, bus, &prot->d2hring_tx_cpln, &client->tx_cpl_ring[0], true);
	/* TX ring info*/
	client->tx_flow_max = dhd_get_max_flow_rings(dhd);
	if (prot->h2d_flowrings_pool) {
		for (i = 0; i < bus->max_tx_flowrings; i++) {
			dhd_client_ring_query(client, bus, &prot->h2d_flowrings_pool[i],
					      &client->tx_ring[i], false);
		}
	}
	/* RX post info */
	client->rx_post_max = 1;
	dhd_client_ring_query(client, bus, &prot->h2dring_rxp_subn, &client->rx_post_ring[0],
			      false);
	return 0;
}

static void dhd_noa_wlan_bus_exit(struct noa_wlan_client *client)
{
	dhd_bus_t *bus = client->bus;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	int i;

	/*	RX ring info */
	dhd_noa_wlan_ring_syncback(bus, &prot->d2hring_rx_cpln, &client->rx_ring[0]);
	/* TX CPL ring info query*/
	client->tx_cpl_flow_max = 1;
	dhd_noa_wlan_ring_syncback(bus, &prot->d2hring_tx_cpln, &client->tx_cpl_ring[0]);
	/* TX ring info*/
	if (prot->h2d_flowrings_pool) {
		for (i = 0; i < bus->max_tx_flowrings; i++) {
			dhd_noa_wlan_ring_syncback(bus, &prot->h2d_flowrings_pool[i],
						   &client->tx_ring[i]);
		}
	}
	/* RX post info */
	dhd_noa_wlan_ring_syncback(bus, &prot->h2dring_rxp_subn, &client->rx_post_ring[0]);
}

extern uint dhd_bus_db0_addr_get(struct dhd_bus *bus);
static inline u64 dhd_noa_doorbell_addr_get(struct dhd_bus *bus)
{
	u64 doorbell_addr = (unsigned long)bus->pcie_mb_intr_addr;

	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		doorbell_addr = NOA_PCIE_BAR0_BASE | PCI_16KB0_PCIREGS_OFFSET |
				(unsigned long)dhd_bus_db0_addr_get(bus);
	}
	return doorbell_addr;
}

static int dhd_noa_wlan_start(void *priv)
{
	dhd_bus_t *bus;
	dhd_pub_t *dhd;
	dhd_prot_t *prot;
	int ret;

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return -EINVAL;
	}

	bus = client->bus;
	dhd = bus->dhd;
	prot = dhd->prot;

	/* update bus related information to client struct*/
	dhd_noa_wlan_bus_init(client);
	/* remap the fw_trap buffer */
	ret = dhd_noa_wlan_dma_buf_remap(&prot->fw_trap_buf, &client->fw_trap_addr, true);
	if (ret) {
		noa_wlan_err("%s(): failed to remap fw_trap_buf, err: %d\n", __func__, ret);
		return ret;
	}
	/* core dongle register */
	client->ints_addr = PCI_16KB0_PCIREGS_OFFSET + bus->pcie_mailbox_int;
	client->intm_addr = PCI_16KB0_PCIREGS_OFFSET + bus->pcie_mailbox_mask;
	client->doorbell_addr = dhd_noa_doorbell_addr_get(bus);
	/* nep information */
	client->nep_tx_desc_sz = NOA_DESC_WLAN_TX_BRCM_BYTE;
	client->nep_tx_items = prot->h2d_flowrings_pool[0].max_items;
	client->nep_rx_desc_sz = max(NOA_DESC_WLAN_RX_BRCM_BYTE, NOA_DESC_VPN_RX_BYTE);
	client->nep_rx_items = prot->d2hring_rx_cpln.max_items;
	client->wdev_tx_post_desc_sz = sizeof(host_txbuf_post_t);
	client->wdev_tx_cpl_desc_sz = sizeof(host_txbuf_cmpl_t);
	client->wdev_rx_post_desc_sz = sizeof(host_rxbuf_post_t);
	client->wdev_rx_cpl_desc_sz = sizeof(host_rxbuf_cmpl_t);
	/* NOA fw and hw start */
	ret = noa_wlan_fw_start(client);
	if (ret) {
		noa_wlan_err("%s(): err %d\n", __func__, ret);
		return ret;
	}
	noa_wlan_dyswitch_sync(bus);
	/* enable interrupt and start soft queue */
	dhd_bus_start_queue(bus);
	return 0;
}

static void dhd_noa_wlan_stop(void *priv)
{
	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return;
	}

	if (!test_bit(CLIENT_FLAG_START, &client->flags))
		return;
	noa_wlan_fw_stop(client);
	dhd_noa_wlan_bus_exit(client);
}

#define RXBM_BUSRT_SIZE 8192
/* return success count */
int _dhd_noa_wlan_rxbm_sync(void *priv, u32 num, void **rxbm, bool to_dev)
{
	dhd_bus_t *bus = priv;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	dmaaddr_t *pkt_pa;
	u32 *pkt_len, *pkt_id;
	struct noa_bm_buf *bufs, *buf;
	struct noa_wlan_mapping_params params = {
		.contiguous = true,
		.tkid_in_use = true,
		.pool_id = DHD_MAPPER_POOL_APC_RX,
	};
	int j, k, times, ret, total = 0;

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return 0;
	}

	bufs = (struct noa_bm_buf *)kzalloc(sizeof(struct noa_bm_buf) * RXBM_BUSRT_SIZE,
					    GFP_KERNEL);
	if (!bufs) {
		return 0;
	}

	times = (num / RXBM_BUSRT_SIZE) + 1;
	pkt_pa = (dmaaddr_t *)((u8 *)rxbm + sizeof(void *) * prot->rx_buf_burst);
	pkt_len = (u32 *)((u8 *)pkt_pa + sizeof(dmaaddr_t) * prot->rx_buf_burst);
	pkt_id = (u32 *)((u8 *)pkt_len + sizeof(u32) * prot->rx_buf_burst);

	for (j = 0; j < times; j++) {
		int i, cnt = 0;
		for (k = 0; k < RXBM_BUSRT_SIZE; k++) {
			i = j * RXBM_BUSRT_SIZE + k;
			if (i >= num)
				break;
			buf = &bufs[k];
			buf->pktid = pkt_id[i];
			buf->pa = PHYSADDRHI(pkt_pa[i]);
			buf->pa = buf->pa << 32 | PHYSADDRLO(pkt_pa[i]);
			buf->pa -= WLAN_PKT_PAD;
			buf->len = pkt_len[i] + WLAN_PKT_PAD;
			buf->apc_va = (unsigned long)(PKTDATA(dhd->osh, rxbm[i]));
			buf->apc_va -= WLAN_PKT_PAD;
			cnt++;

			params.cpu_addr = (void *)buf->apc_va;
			params.size = buf->len;
			params.tkid = buf->pktid;

			ret = noa_wlan_mapper_remap(client, &params, &buf->dpa_va);
			if (ret) {
				dev_err(client->dev, "%s(): failed to remap dma address, err: %d\n",
					__func__, ret);
				continue;
			}
		}
		/* Send cmd to NCP WiFi firmware through wrapper driver */
		ret = noa_wlan_fw_rxbm_sync(client, bufs, cnt, to_dev);
		if (!ret) {
			total += cnt;
		}
	}
	kfree(bufs);
	return total;
}
extern int BCMFASTPATH(dhd_prot_rxbuf_post_packets)(dhd_pub_t *dhd, uint16 count,
						    bool use_rsv_pktid);
static int dhd_noa_wlan_rxbm_sync(void *priv, u32 count, bool use_rsv_pktid)
{
	struct dhd_pub *dhd = priv;
	return dhd_prot_rxbuf_post_packets(dhd, count, use_rsv_pktid);
}

extern bool dhd_prot_process_msgbuf_rxcpl_packet(dhd_pub_t *dhd, void *_msg);
static int dhd_noa_wlan_rx(struct noa_wlan_client *client, void **msg)
{
	dhd_bus_t *bus = client->bus;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	struct noa_desc *d = *msg;
	void *pkt;
	int ret;
	uint16_t tkid = d->tkid;
	const struct noa_wlan_bm_tkid_item *tkid_item;

	if (d->reason == FWD_REASON_FALLBACK || d->reason == FWD_REASON_VPN) {
		noa_wlan_feedback_ring_write(client, &client->feedback_ring.ring, d);
	}

	/* prefix of buffer is used to store the rx desc from wifi device */
	pkt = dhd_pktid_map_get(dhd, prot->pktid_rx_map, ltoh32(tkid));
	if (pkt) {
#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
		noa_vpn_manager_handle_ipsec_offload_rx_skb(
			(struct sk_buff *)pkt, (struct noa_rx_ipsec_metadata *)d->ext_data);
#endif
#ifdef linux
		trace_wlan_dump_noa_rx(0, (u32)sizeof(struct noa_desc), (char *)d,
				       (u32)(d->dl + WLAN_PKT_PAD),
				       (char *)PKTDATA(dhd->osh, pkt) - WLAN_PKT_PAD);
#endif

		if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
			ret = noa_wlan_bm_find(&client->vendor_rx_bm, tkid, &tkid_item);
			if (ret) {
				dev_err(client->dev,
					"%s(): failed to get buffer info tkid: %u, ret: %d\n",
					__func__, tkid, ret);
				return -EINVAL;
			}
			dma_sync_single_for_cpu(client->dpa_dev, tkid_item->dpa_va, WLAN_PKT_PAD,
						DMA_FROM_DEVICE);
		}
		*msg = (host_rxbuf_cmpl_t *)((u8 *)PKTDATA(dhd->osh, pkt) - WLAN_PKT_PAD);
		noa_wlan_bm_remove(&client->vendor_rx_bm, tkid);
		noa_wlan_mapper_unmap_by_tkid(client, DHD_MAPPER_POOL_APC_RX, tkid);
		dhd_prot_process_msgbuf_rxcpl_packet(dhd, *msg);
	}

	return 0;
}

static int dhd_noa_wlan_fallback_rx(struct noa_wlan_client *client, void **msg)
{
	dhd_bus_t *bus = client->bus;
	dhd_pub_t *dhd = bus->dhd;

	if (*msg) {
		dhd_prot_process_msgbuf_rxcpl_packet(dhd, *msg);
	}

	return 0;
}

static int _dhd_noa_wlan_tx(struct noa_wlan_client *client, void *pkt, host_txbuf_post_t *_tx_desc)
{
	host_txbuf_post_v2_t *tx_desc = (host_txbuf_post_v2_t *)_tx_desc;
	pkt_info_cso_t *cso_info = &tx_desc->pktinfo;
	dhd_bus_t *bus = client->bus;
	dhd_pub_t *dhd = bus->dhd;
	struct noa_bcm_txd bcm_txd, *txd = &bcm_txd;
	struct noa_desc *d = (struct noa_desc *)tx_desc;
	/* record tx_desc first since they will be remark later */
	u8 *ext_data = &d->ext_data[0];
	u8 *pkt_va = PKTDATA(dhd->osh, pkt);
	u16 pkt_len = tx_desc->data_len;
	u16 pkt_id = tx_desc->cmn_hdr.request_id;
	u8 forward = (DHD_PKT_GET_FORWARD(pkt) == 1) ? 1 : 0;
	unsigned long pa;
	int ret;
	struct noa_wlan_mapping_params params = {
		.cpu_addr = (void *)pkt_va,
		.size = pkt_len,
		.contiguous = true,
		.tkid_in_use = true,
		.pool_id = DHD_MAPPER_POOL_APC_TX,
		.tkid = pkt_id,
	};

	pa = tx_desc->data_buf_addr.high_addr;
	pa = (pa << 32 | tx_desc->data_buf_addr.low_addr);
	/* put bcm_txd to head room of packet buffer */
	txd->current_phase = tx_desc->cmn_hdr.flags;
	txd->ext_flags = tx_desc->ext_flags;
	txd->ring_id = FLOWID_TO_RINGID(DHD_PKT_GET_FLOWID(pkt));
	txd->flags = tx_desc->flags;
	txd->ifidx = tx_desc->cmn_hdr.if_id;
	txd->ethertype = *((u16 *)&tx_desc->txhdr[12]);
	txd->ext_tag = cso_info->ver;
	txd->pkt_csum_type = cso_info->pkt_csum_type;
	txd->l3_hdr_len = cso_info->nwk_hdr_len;
	txd->l4_hdr_len = cso_info->trans_hdr_len;

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
	if (client->mode == NOA_MODE_VPN) {
		noa_vpn_manager_handle_ipsec_offload_tx_skb((const struct sk_buff *)pkt,
							    &txd->ipsec_metadata);
	}
#endif

	/* must before remarked by d */
	if (forward) {
		struct noa_session_info info = {
			.ethertype = txd->ethertype,
			.ifidx = txd->ifidx,
			.flowid = txd->ring_id,
		};
		noa_wlan_add_session_entry(client, pkt_va, &info);
	}
	/* remark tx_desc to noa_desc */
	memset(d, 0, sizeof(struct noa_desc));
	d->ver = 0;
	d->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
				      kNoaWlanRingTxData);
	d->mode = NOAD_MODE_DATA;
	d->cp = 0;
	d->fk = 0;
	/* extend dp and dl to include brcm_txd */
	d->dp_low = pa & 0xFFFFFFFF;
	d->dp_high = (pa >> 32) & 0xFFFFFFFF;
	d->dl = pkt_len;
	d->tkid = pkt_id;
	d->ddone = 0;
	d->head_offset = 0;
	d->reason = FWD_REASON_FEEDTHROUGH;

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
	/**
	 * Route all packet to NetEngine because some of them are VPN packets
	 * that need to be further processed in NetEngine.
	 * TODO(b/355097058): Support dynamic switch for fast path, and remove the preprocessor.
	 */
	if (client->mode == NOA_MODE_VPN) {
		d->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel,
					      kNoaNetengineRingData);
		d->reason = FWD_REASON_NETENGINE;
	}
#endif

	ret = noa_wlan_mapper_remap(client, &params, &d->dv);
	if (ret) {
		dev_err(client->dev, "%s(): failed to remap dma address, err: %d\n", __func__, ret);
		return ret;
	}

	d->desc_type = NOA_DESC_WLAN_TX_BRCM;
	memcpy(ext_data, txd, sizeof(struct noa_bcm_txd));
	return 0;
}

extern int dhd_noa_wlan_tx_packet_prepare(dhd_pub_t *dhd, void *pkt, u32 ifidx,
					  host_txbuf_post_t *txdesc);

struct dhd_noa_wlan_tx_args {
	struct noa_wlan_client *client;
	void *pkt;
	u32 ifidx;
};

static ssize_t dhd_noa_wlan_tx(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	const struct dhd_noa_wlan_tx_args *tx_info = (const struct dhd_noa_wlan_tx_args *)data;
	dhd_bus_t *bus = tx_info->client->bus;
	dhd_pub_t *dhd = bus->dhd;
	host_txbuf_post_t *txbuf_post = (host_txbuf_post_t *)buf;
	int ret;

	ret = dhd_noa_wlan_tx_packet_prepare(dhd, tx_info->pkt, tx_info->ifidx, txbuf_post);
	if (ret)
		return ret;

	ret = _dhd_noa_wlan_tx(tx_info->client, tx_info->pkt, txbuf_post);

#ifdef linux
	trace_wlan_dump_noa_tx(DHD_PKT_GET_FLOWID(tx_info->pkt), (u32)sizeof(struct noa_desc),
			       (char *)txbuf_post, (u32)(((struct noa_desc *)buf)->dl),
			       (char *)PKTDATA(dhd->osh, tx_info->pkt));
#endif
	return ret;
}

static int dhd_noa_wlan_tx_packet(void *priv, void *pkt, u32 ifidx)
{
	const struct dhd_noa_wlan_tx_args args = {
		.client = client,
		.pkt = pkt,
		.ifidx = ifidx,
	};

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(client->mode == NOA_MODE_AP_NCP_DIRECT)) {
		return noa_wlan_client_ring_write(client, &client->direct_tx_data_ring.ring,
						  (void *)&args);
	} else {
		return noa_wlan_client_ring_write(client, &client->tx_data_ring.ring,
						  (void *)&args);
	}
}

extern int dhdpcie_enable_msi(struct pci_dev *pdev, unsigned int min_vecs, unsigned int max_vecs);
static int dhd_noa_irq_request(void *priv)
{
	struct msi_desc *entry;
	dhd_bus_t *bus;
	struct pci_dev *pdev;
	int32_t descs = 0;

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return -EINVAL;
	}

	bus = (dhd_bus_t *)client->bus;
	pdev = bus->dev;

	/* release original irq first */
	if (bus->intr) {
		dhdpcie_bus_intr_disable(bus);
		dhdpcie_free_irq(bus);
		bus->intr = false;
	}

	brcm4390_irq_table[0].pdev_irq = pdev->irq;

	if (bus->d2h_intr_method == PCIE_MSI) {
		dhdpcie_enable_msi(pdev, 1, 1);
		msi_for_each_desc(entry, &pdev->dev, MSI_DESC_ASSOCIATED)
		{
			client->msi_descs[descs].nvec_used = entry->nvec_used;
			client->msi_descs[descs].msi_index = entry->msi_index;
			client->msi_descs[descs].data = entry->msg.data;
			brcm4390_irq_table[descs].noa_wlan_irq = entry->msi_index;
			brcm4390_irq_table[descs].pdev_irq = pdev->irq;
			descs += 1;
		}
	}
	client->irqs[0] = pdev->irq;
	client->irq_nums = 1;

	return noa_wlan_irq_request(client);
}

extern int dhd_prot_lb_rxp_flow_ctrl(dhd_pub_t *dhd);
extern void dhd_lb_rx_napi_dispatch(dhd_pub_t *dhd);
extern void dhd_bus_rx_post_check(struct dhd_bus *bus, u32 cnt);
static bool dhd_noa_wlan_rx_poll(void *priv, u32 *_cnt)
{
	noa_ring_consumer *rx_data_ring = NULL;
	noa_ring_consumer *rx_fallback_ring = &client->rx_fallback_ring.ring;
	int cnt;
	dhd_pub_t *dhd = priv;

	if (!client)
		return false;

	/* Select the appropriate primary RX data ring based on the operational mode. */
	if (unlikely(client->mode == NOA_MODE_AP_NCP_DIRECT)) {
		rx_data_ring = &client->direct_rx_data_ring.ring;
	} else {
		rx_data_ring = &client->rx_data_ring.ring;
	}

	if (noa_wlan_client_ring_is_empty(rx_data_ring) &&
	    noa_wlan_client_ring_is_empty(rx_fallback_ring))
		return 0;

#ifdef DHD_LB_RXP
	(void)dhd_prot_lb_rxp_flow_ctrl(dhd);
#endif /* DHD_LB_RXP */

	/* Process all available packets in the primary RX data ring. */
	cnt = noa_wlan_client_ring_read_loop(client, rx_data_ring, dhd_noa_wlan_rx);
	if (cnt < 0)
		goto rx_data_err;
	*_cnt = cnt;

	/* Process all available packets in the RX fallback ring. */
	cnt = noa_wlan_client_ring_read_loop(client, rx_fallback_ring, dhd_noa_wlan_fallback_rx);
	if (cnt < 0)
		goto rx_fallback_err;
	*_cnt += cnt;

rx_fallback_err:
	/* Perform post-processing steps after handling the rings. */
	dhd_lb_rx_napi_dispatch(dhd);
	dhd_bus_rx_post_check(client->bus, *_cnt);
rx_data_err:
	/* Return true if either ring still contains data */
	return !noa_wlan_client_ring_is_empty(rx_data_ring) ||
	       !noa_wlan_client_ring_is_empty(rx_fallback_ring);
}

static int dhd_noa_wlan_txq_active(void *priv, u16 flowid, bool enable)
{
	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return -EINVAL;
	}

	/* flow id mapping to ring id (queue id) should minus 2 for BRCM4389*/
	return noa_wlan_fw_txq_active(client, FLOWID_TO_RINGID(flowid), enable);
}

static int dhd_noa_wlan_sta_active(void *priv, struct sta_info *info, bool enable)
{
	int i;

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return -EINVAL;
	}

	if (!info)
		return -EINVAL;

	for (i = 0; i < PRIORITY_CLASS; i++) {
		if (info->qos_txq_map[i] != INVALID_TXQ_ID) {
			info->qos_txq_map[i] = FLOWID_TO_RINGID(info->qos_txq_map[i]);
		}
	}
	return noa_wlan_fw_sta_active(client, info, enable);
}

static bool dhd_noa_wlan_check_suspend(void)
{
	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return true;
	}

	return !client->fw_active;
}

static void dhd_noa_wlan_notify_resume(void)
{
	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return;
	}

	client->fw_ops->notify_pm_state(true);
}

static void dhd_noa_wlan_ssr_dump(void *seg)
{
	if (!client || client->fw_ops->ssr_dump == NULL)
		return;

	client->fw_ops->ssr_dump(client, seg);
}

static bool dhd_noa_wlan_tx_cpl(void *priv, u32 *cnt)
{
	if (unlikely(!client)) {
		pr_err("%s(): Invalid client context\n", __func__);
		return false;
	}

	return noa_wlan_fw_txcpl(client, cnt);
}

static void dhd_noa_wlan_update_up2flow(const u8 type, const u8 *tbl)
{
	if (!client || client->fw_ops->update_up2flow == NULL) {
		return;
	}

	client->fw_ops->update_up2flow(client, type, tbl);
}

static void dhd_noa_wlan_update_flowid_lkup_entry(struct flow_id_entry_update *entry)
{
	if (!client || client->fw_ops->update_flowid_lkup_entry == NULL)
		return;
	entry->flowid = FLOWID_TO_RINGID(entry->flowid);
	client->fw_ops->update_flowid_lkup_entry(client, entry);
}

static void dhd_noa_wlan_sync_pci_link_state(int32_t state, bool is_to_shm)
{
	if (!client || client->fw_ops->sync_pci_link_state == NULL)
		return;

	client->fw_ops->sync_pci_link_state(client, state, is_to_shm);
}

static void dhd_noa_wlan_notify_station_state(u8 state, void *src, int iflist_idx)
{
	dhd_if_t *ifp = NULL;
	int iif = 0;
	dhd_pub_t *dhd_pub = (dhd_pub_t *)src;

	if (!client || client->fw_ops->notify_station_state == NULL)
		return;

	if (iflist_idx >= 0 && iflist_idx < DHD_MAX_IFS) {
		ifp = dhd_pub->info->iflist[iflist_idx];
		if (ifp)
			iif = ifp->net->ifindex;
	}

	client->fw_ops->notify_station_state(client, state, iif);
}

/* physical plat_ops that is used at runtime. */
struct platform_bus_ops *plat_ops;
static int dhd_noa_wlan_rx_handover(void *bus_priv);
/* This google bus ops will be used in google_plat.h directly */
struct platform_bus_ops google_bus_ops = {
	.init = dhd_noa_wlan_init,
	.exit = dhd_noa_wlan_exit,
	.start = dhd_noa_wlan_start,
	.stop = dhd_noa_wlan_stop,
	.request_irq = dhd_noa_irq_request,
	.rx = dhd_noa_wlan_rx_poll,
	.rx_replenish = dhd_noa_wlan_rxbm_sync,
	.tx = dhd_noa_wlan_tx_packet,
	.tx_cpl = dhd_noa_wlan_tx_cpl,
	.tx_queue_active = dhd_noa_wlan_txq_active,
	.sta_active = dhd_noa_wlan_sta_active,
	.check_suspend = dhd_noa_wlan_check_suspend,
	.notify_resume = dhd_noa_wlan_notify_resume,
	.ssr_dump = dhd_noa_wlan_ssr_dump,
	.update_up2flow = dhd_noa_wlan_update_up2flow,
	.update_flowid_lkup_entry = dhd_noa_wlan_update_flowid_lkup_entry,
	.sync_pci_link_state = dhd_noa_wlan_sync_pci_link_state,
	.notify_station_state = dhd_noa_wlan_notify_station_state,
	.rx_handover = dhd_noa_wlan_rx_handover,
};

static int dhd_noa_wlan_rx_handover(void *bus_priv)
{
	dhd_bus_t *bus = bus_priv;
	dhd_pub_t *dhd = bus->dhd;
	dhd_prot_t *prot = dhd->prot;
	dhd_pktid_map_t *map = (dhd_pktid_map_t *)prot->pktid_rx_map;
	struct noa_wlan_rx_handover_item *items = NULL;
	struct noa_wlan_mapping_params params = {
		.contiguous = true,
		.tkid_in_use = true,
		.pool_id = DHD_MAPPER_POOL_APC_RX,
	};
	u32 i, count = 0;
	u32 active_count = 0;
	size_t tbl_size;
	u64 table_dpa_addr;
	int ret = 0;

	if (!client) {
		pr_err("%s(): NULL client.\n", __func__);
		return -ENODEV;
	}

	if (!map) {
		pr_err("%s(): NULL rx pktid map.\n", __func__);
		return -EINVAL;
	}

	// Count active lockers
	for (i = 1; i <= map->items; i++) {
		if (map->lockers[i].state == LOCKER_IS_BUSY) {
			active_count++;
		}
	}

	dev_info(client->dev, "%s: active rx buffers count = %u\n", __func__, active_count);

	if (active_count == 0) {
		dev_info(client->dev, "%s(): No active RX buffers to handover.\n", __func__);
		return 0;
	}

	tbl_size = active_count * sizeof(struct noa_wlan_rx_handover_item);
	items = kzalloc(tbl_size, GFP_KERNEL);
	if (!items) {
		return -ENOMEM;
	}

	// Scan, remap, and register in Host BM
	for (i = 1; i <= map->items; i++) {
		dhd_pktid_item_t *item = &map->lockers[i];
		if (item->state == LOCKER_IS_BUSY) {
			struct noa_wlan_rx_handover_item *dst = &items[count];
			u64 pa = ((u64)PHYSADDRHI(item->pa) << 32) | PHYSADDRLO(item->pa);
			unsigned long apc_va = (unsigned long)(PKTDATA(dhd->osh, item->pkt));
			u32 len = item->len;
			u64 dpa_addr;

			pa -= WLAN_PKT_PAD;
			len += WLAN_PKT_PAD;
			apc_va -= WLAN_PKT_PAD;

			params.cpu_addr = (void *)apc_va;
			params.size = len;
			params.tkid = i;

			ret = noa_wlan_mapper_remap(client, &params, &dpa_addr);
			if (ret) {
				dev_err(client->dev, "%s(): Failed to remap buffer %d, err: %d\n", __func__, i, ret);
				goto err_unmap_buffers;
			}

			ret = noa_wlan_bm_register(&client->vendor_rx_bm, i, len, dpa_addr);
			if (ret) {
				dev_err(client->dev, "%s(): Failed to register buffer %d in bm, err: %d\n", __func__, i, ret);
				noa_wlan_mapper_unmap_by_tkid(client, DHD_MAPPER_POOL_APC_RX, i);
				goto err_unmap_buffers;
			}

			dst->tkid = i;
			dst->buf_size = len;
			dst->host_pa = pa;
			dst->dpa_addr = dpa_addr;

			count++;
		}
	}

	// Map the table itself
	struct noa_wlan_mapping_params tbl_map_params = {
		.cpu_addr = items,
		.size = tbl_size,
		.contiguous = true,
		.tkid_in_use = false,
	};
	ret = noa_wlan_mapper_remap(client, &tbl_map_params, &table_dpa_addr);
	if (ret) {
		dev_err(client->dev, "%s(): Failed to map handover table, err: %d\n", __func__, ret);
		goto err_unmap_buffers;
	}

	// Send RPC
	ret = noa_wlan_fw_rx_handover_sync(client, table_dpa_addr, count);
	if (ret) {
		dev_err(client->dev, "%s(): Handover RPC failed, err: %d\n", __func__, ret);
		noa_wlan_mapper_unmap_by_addr(client, items);
		goto err_unmap_buffers;
	}

	// Immediately unmap the table
	noa_wlan_mapper_unmap_by_addr(client, items);
	kfree(items);
	return 0;

err_unmap_buffers:
	for (i = 0; i < count; i++) {
		noa_wlan_mapper_unmap_by_tkid(client, DHD_MAPPER_POOL_APC_RX, items[i].tkid);
		noa_wlan_bm_remove(&client->vendor_rx_bm, items[i].tkid);
	}
	kfree(items);
	return ret;
}
