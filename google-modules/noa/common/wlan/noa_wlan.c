// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/platform_data/sscoredump.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/atomic.h>
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include <nep/nep.h>
#include <net/nep_cmd_rpc_service/noa_nep_cmd_dispatch.h>
#include <net/nep_cmd_rpc_service/noa_nep_cmd_rpc.h>
#include "noa_wlan_client.h"
#include "noa.h"
#include "wlan_trace.h"
#include "wlan_debug_controller/wlan_debug_controller_client.h"
#include "wlan_rpc_service/noa_wlan_cmd_dispatch.h"
#include "wlan_rpc_service/noa_wlan_rpc.h"
#include "noa_wlan_cfg_space.h"
#include "noa_wlan_nep_helper.h"
#include "noa_wlan_hw.h"
#include "noa_wlan_buffer_management.h"
#include "google_plat_internal.h"
#include "noa_wlan_dynamic_switch.h"
#ifdef CONFIG_NOA_PCIE_SUPPORT
#include <linux/pcie_google_if.h>
#endif /* CONFIG_NOA_PCIE_SUPPORT */

static struct noa_wlan_switch_manager sw_manager;
extern struct kobj_type noa_wlan_ktype;

static int txbm_alloc_bufs(struct noa_wlan_client *client)
{
	struct noa_bm_buf *entry;
	struct noa_wlan_mapping_params params = {
		.contiguous = true,
		.tkid_in_use = false,
	};
	u32 tx_bm_sz = client->tx_bm_sz;
	int ret;
	int i;

	/* allocate buffers */
	for (i = 0; i < tx_bm_sz; i++) {
		entry = &client->txbm[i];
		entry->apc_va = (unsigned long)kzalloc(MAX_TXBUF_SIZE, GFP_ATOMIC);
		entry->len = MAX_TXBUF_SIZE;
		entry->pa = dma_map_single(client->dev, (void *)entry->apc_va, entry->len,
					   DMA_TO_DEVICE);
		entry->pktid = i;
		if (dma_mapping_error(client->dev, entry->pa)) {
			dev_err(client->dev, "%s(): dma map fail\n", __func__);
			return -ENOMEM;
		}

		params.cpu_addr = (void *)entry->apc_va;
		params.size = entry->len;

		ret = noa_wlan_mapper_remap(client, &params, &entry->dpa_va);
		if (ret) {
			dev_err(client->dev, "%s(): failed to remap address, err: %d\n", __func__,
				ret);
			continue;
		}
	}
	return 0;
}

static void txbm_free_bufs(struct noa_wlan_client *client)
{
	int i;
	struct noa_bm_buf *entry;

	/* free buffers */
	for (i = 0; i < MAX_TXBM_BUF_NUM; i++) {
		entry = &client->txbm[i];
		dma_unmap_single(client->dev, entry->pa, entry->len, DMA_TO_DEVICE);
		entry->len = 0;
		if (entry->apc_va)
			kfree((void *)entry->apc_va);
	}
}

static int noa_wlan_hw_txbm_sync(struct noa_wlan_client *client, void *_bufs, int num)
{
	struct noa_bm_buf *bufs = (struct noa_bm_buf *)_bufs;
	int ret;
	u32 i = 0;

	if (!client->tx_bm_sz)
		return 0;

	ret = txbm_alloc_bufs(client);
	if (ret) {
		dev_err(client->dev, "txbm alloc fail.\n");
		return ret;
	}

	for (i = 0; i < num; i++) {
		if (noa_wlan_bm_register(&client->noa_tx_bm, bufs[i].pktid, bufs[i].len,
					 bufs[i].dpa_va)) {
			pr_err("%s(): register pkt(%u) failed.\n", __func__, bufs[i].pktid);
			return -EINVAL;
		}
	}

	return noa_wlan_replenish_ring_write(client, &client->noa_tx_replenish_ring.ring, bufs, num,
					     true);
}

static int noa_wlan_hw_rings_start(struct noa_wlan_client *client)
{
	int ret;

	ret = noa_wlan_cfg_update_wifi_ring_info(client, client->rx_flow_max, RING_TYPE_RX_DATA,
						 client->rx_ring);
	if (ret) {
		dev_err(client->dev, "failed to update wifi rx ring info: %d\n", ret);
		return ret;
	}

	ret = noa_wlan_cfg_update_wifi_ring_info(client, client->tx_flow_max, RING_TYPE_TX_DATA,
						 client->tx_ring);
	if (ret) {
		dev_err(client->dev, "failed to update wifi tx ring info: %d\n", ret);
		return ret;
	}

	ret = noa_wlan_cfg_update_wifi_ring_info(client, client->tx_cpl_flow_max, RING_TYPE_TX_CPL,
						 client->tx_cpl_ring);
	if (ret) {
		dev_err(client->dev, "failed to update wifi txcpl ring info: %d\n", ret);
		return ret;
	}

	ret = noa_wlan_cfg_update_wifi_ring_info(client, client->rx_post_max, RING_TYPE_RX_POST,
						 client->rx_post_ring);
	if (ret) {
		dev_err(client->dev, "failed to update wifi rxpost ring info: %d\n", ret);
		return ret;
	}

	return ret;
}

static void noa_wlan_hw_rings_stop(struct noa_wlan_client *client)
{
	noa_wlan_cfg_update_wifi_ring_syncback(client, client->rx_flow_max, RING_TYPE_RX_DATA,
					       client->rx_ring);

	noa_wlan_cfg_update_wifi_ring_syncback(client, client->tx_flow_max, RING_TYPE_TX_DATA,
					       client->tx_ring);

	noa_wlan_cfg_update_wifi_ring_syncback(client, client->tx_cpl_flow_max, RING_TYPE_TX_CPL,
					       client->tx_cpl_ring);

	noa_wlan_cfg_update_wifi_ring_syncback(client, client->rx_post_max, RING_TYPE_RX_POST,
					       client->rx_post_ring);
}

static int noa_wlan_client_register_nep_ring(struct noa_wlan_client *client,
					     struct wlan_sw_nep_ring *nep_ring, int type,
					     const struct noa_ring_ops *ops,
					     struct noa_ring_regs *regs, const char *name,
					     void *desc, u32 item_len, u32 size)
{
	int ret;
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.size = size,
		.item_len = item_len,
	};
	spin_lock_init(&nep_ring->lock);

	ret = noa_ring_regs_wrapper_init(&nep_ring->ring, type, ops, regs, client, name, 0);
	if (ret) {
		dev_err(client->dev, "Failed to init %s nep ring, err %d\n", name, ret);
		return ret;
	}

	if (!desc) {
		nep_ring->own_desc = true;
		nep_ring->desc = dmam_alloc_coherent(client->dpa_dev, size * item_len,
						     &nep_ring->desc_dma, GFP_KERNEL);
	} else {
		nep_ring->own_desc = false;
		nep_ring->desc = desc;
	}

	if (!nep_ring->desc) {
		dev_err(client->dev, "Failed to allocate desc buffer for %s nep ring\n", name);
		return -ENOMEM;
	}
	info.base = nep_ring->desc;

	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		info.dpa_base = (char *)nep_ring->desc_dma;
	} else {
		info.dpa_base = nep_ring->desc;
	}

	noa_ring_info_setup(&nep_ring->ring, &info);
	noa_ring_activate(&nep_ring->ring);

	dev_info(client->dpa_dev, "set ring: %s, va: %p, pa: %p, ndesc: %u, len: %u\n", name,
		 info.base, (void *)info.dpa_base, info.item_len, info.size);

	return 0;
}

static void noa_wlan_client_unregister_nep_ring(struct noa_wlan_client *client,
						struct wlan_sw_nep_ring *nep_ring)
{
	struct noa_ring_wrapper *ring = &nep_ring->ring;

	noa_ring_deactivate(ring);

	/* free dma buffer */
	if (nep_ring->own_desc)
		dmam_free_coherent(client->dpa_dev, ring->basic.item_len * ring->basic.size,
				   nep_ring->desc, nep_ring->desc_dma);
	nep_ring->desc = NULL;
	nep_ring->desc_dma = 0;
	noa_ring_info_clean(ring);
}

static int nep_rx_ring_set(struct noa_wlan_client *client, u16 max_items, u16 desc_len)
{
	struct noa_wlan_hw *hw = &client->hw;
	static struct noa_ring_ops nep_ring_rx_ops = {
		.read_payload = noa_generic_read_raw_pointer,
	};

	/* output ring to APC */
	noa_wlan_hw_nep_ring_reg_query(client, NOA_RING_TYPE_CONSUMER, &hw->output);

	return noa_wlan_client_register_nep_ring(client, &client->rx_data_ring,
						 NOA_RING_TYPE_CONSUMER, &nep_ring_rx_ops,
						 &hw->output, "NEP_WLAN_SW_RX", NULL, desc_len,
						 max_items);
}

static void complete_tx_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_unlock(&nep_ring->lock);

	if (noa_ring_pos_is_moved(ring))
		noa_wlan_hw_ringbell_nep((struct noa_wlan_client *)ring->owner);
}

static void begin_tx_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_lock(&nep_ring->lock);
}

static int nep_tx_ring_set(struct noa_wlan_client *client, u16 max_items, u16 desc_len)
{
	struct noa_wlan_hw *hw = &client->hw;
	static struct noa_ring_ops nep_ring_tx_ops = {
		.complete_hook = complete_tx_ring_processing,
		.begin_hook = begin_tx_ring_processing,
	};
	nep_ring_tx_ops.write_payload = client->ops->write_payload;

	/* input ring from APC */
	noa_wlan_hw_nep_ring_reg_query(client, NOA_RING_TYPE_PRODUCER, &hw->input);

	return noa_wlan_client_register_nep_ring(client, &client->tx_data_ring,
						 NOA_RING_TYPE_PRODUCER, &nep_ring_tx_ops,
						 &hw->input, "NEP_WLAN_SW_TX", NULL, desc_len,
						 max_items);
}

static int noa_wlan_hw_nep_start(struct noa_wlan_client *client)
{
	/* setup nep rx (output) rings */
	nep_rx_ring_set(client, client->nep_rx_items, client->nep_rx_desc_sz);
	/* setup nep tx (input) rings */
	nep_tx_ring_set(client, client->nep_tx_items, client->nep_tx_desc_sz);
	/* active nep rings */
	noa_wlan_hw_nep_rings_input_activate(true);
	noa_wlan_hw_nep_rings_output_activate(true);
	return 0;
}

static void noa_wlan_hw_nep_input_stop(struct noa_wlan_client *client)
{
	// Only send RPC to the DPA if the DPA is not under a crash state
	if (!client->dpa_crash_state) {
		noa_wlan_hw_nep_rings_input_activate(false);
	}
	noa_wlan_client_unregister_nep_ring(client, &client->tx_data_ring);
}

static void noa_wlan_hw_nep_output_stop(struct noa_wlan_client *client)
{
	// Only send RPC to the DPA if the DPA is not under a crash state
	if (!client->dpa_crash_state) {
		noa_wlan_hw_nep_rings_output_activate(false);
	}
	noa_wlan_client_unregister_nep_ring(client, &client->rx_data_ring);
}

static void complete_tx_direct_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_unlock(&nep_ring->lock);

	if (noa_ring_pos_is_moved(ring))
		noa_wlan_hw_ringbell_ncp((struct noa_wlan_client *)ring->owner,
					 DOORBELL_DIRECT_SUB_EVENT);
}

static void begin_tx_direct_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_lock(&nep_ring->lock);
}

static int noa_wlan_direct_tx_ring_set(struct noa_wlan_client *client, u16 max_items, u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;

	static struct noa_ring_ops tx_ops = {
		.complete_hook = complete_tx_direct_ring_processing,
		.begin_hook = begin_tx_direct_ring_processing,
	};
	tx_ops.write_payload = client->ops->write_payload;

	ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
					   kNoaNetworkFlowHostToDevice, kNoaWlanDirectH2DRingTxData,
					   kNoaNepRingAnyDirection, &ring_regs);
	if (ret) {
		dev_err(client->dev, "noa_wlan_hw_nep_ring_reg_get fail.\n");
		return ret;
	}

	return noa_wlan_client_register_nep_ring(client, &client->direct_tx_data_ring,
						 NOA_RING_TYPE_PRODUCER, &tx_ops, &ring_regs,
						 "WLAN_SW_TX_DIRECT", NULL, desc_len, max_items);
}

static int noa_wlan_direct_rx_ring_set(struct noa_wlan_client *client, u16 max_items, u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;
	static struct noa_ring_ops rx_ops = {
		.read_payload = noa_generic_read_raw_pointer,
	};

	ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
					   kNoaNetworkFlowDeviceToHost, kNoaWlanDirectD2HRingRxData,
					   kNoaNepRingAnyDirection, &ring_regs);
	if (ret) {
		dev_err(client->dev, "noa_wlan_hw_nep_ring_reg_get fail.\n");
		return ret;
	}

	return noa_wlan_client_register_nep_ring(client, &client->direct_rx_data_ring,
						 NOA_RING_TYPE_CONSUMER, &rx_ops, &ring_regs,
						 "WLAN_SW_RX_DIRECT", NULL, desc_len, max_items);
}

static int noa_wlan_rx_fallback_ring_set(struct noa_wlan_client *client, u16 max_items,
					 u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;
	static struct noa_ring_ops rx_ops = {
		.read_payload = noa_generic_read_raw_pointer,
	};

	ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
					   kNoaNetworkFlowDeviceToHost,
					   kNoaWlanDirectD2HRingRxFallback, kNoaNepRingAnyDirection,
					   &ring_regs);
	if (ret) {
		dev_err(client->dev, "noa_wlan_hw_nep_ring_reg_get fail.\n");
		return ret;
	}

	return noa_wlan_client_register_nep_ring(client, &client->rx_fallback_ring,
						 NOA_RING_TYPE_CONSUMER, &rx_ops, &ring_regs,
						 "WLAN_SW_RX_FALLBACK", NULL, desc_len, max_items);
}

static int noa_wlan_direct_tx_completion_ring_set(struct noa_wlan_client *client, u16 max_items,
						  u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;
	static struct noa_ring_ops rx_ops = {
		.read_payload = noa_generic_read_raw_pointer,
	};
	int i = 0;

	for (i = 0; i < client->tx_cpl_flow_max; i++) {
		ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
						   kNoaNetworkFlowDeviceToHost,
						   kNoaWlanDirectD2HRingTxCpl + i,
						   kNoaNepRingAnyDirection, &ring_regs);
		if (ret) {
			dev_err(client->dev, "Failed to get regs for TxCpl ring %d, ret=%d\n", i,
				ret);
			return ret;
		}

		ret = noa_wlan_client_register_nep_ring(client, &client->direct_tx_cpl_ring[i],
							NOA_RING_TYPE_CONSUMER, &rx_ops, &ring_regs,
							"WLAN_SW_DIRECT_TX_CPL", NULL, desc_len,
							max_items);
		if (ret) {
			dev_err(client->dev, "Failed to register TxCpl ring %d, ret=%d\n", i, ret);
			break;
		}
	}

	return ret;
}

static void complete_replenish_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_unlock(&nep_ring->lock);

	if (noa_ring_pos_is_moved(ring))
		noa_wlan_hw_ringbell_ncp((struct noa_wlan_client *)ring->owner, DOORBELL_BM_UPDATE);
}

static void begin_replenish_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_lock(&nep_ring->lock);
}

static ssize_t replenish_ring_write(void *d, size_t buf_len, const void *data, size_t data_len)
{
	const struct buffer_repln_data *_data = (const struct buffer_repln_data *)data;
	const struct noa_bm_buf *bm_buf = (const struct noa_bm_buf *)_data->buf;
	struct noa_wlan_buffer_repln_desc *desc = (struct noa_wlan_buffer_repln_desc *)d;

	desc->to_dev = _data->to_dev;
	desc->tkid = bm_buf->pktid;
	desc->len = bm_buf->len;
	desc->pa = bm_buf->pa;
	desc->dpa_va = bm_buf->dpa_va;

	return sizeof(struct noa_wlan_buffer_repln_desc);
}

static int noa_wlan_vendor_rx_replenish_ring_set(struct noa_wlan_client *client, u16 max_items,
						 u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;

	static struct noa_ring_ops vendor_rx_replenish_ops = {
		.complete_hook = complete_replenish_ring_processing,
		.begin_hook = begin_replenish_ring_processing,
	};
	vendor_rx_replenish_ops.write_payload = replenish_ring_write;

	ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
					   kNoaNetworkFlowHostToDevice,
					   kNoaWlanDirectH2DRingVendorRxBufferReplenish,
					   kNoaNepRingAnyDirection, &ring_regs);
	if (ret) {
		dev_err(client->dev, "noa_wlan_hw_nep_ring_reg_get fail.\n");
		return ret;
	}

	return noa_wlan_client_register_nep_ring(client, &client->vendor_rx_replenish_ring,
						 NOA_RING_TYPE_PRODUCER, &vendor_rx_replenish_ops,
						 &ring_regs, "WLAN_VENDOR_RX_REPLENISHMENT", NULL,
						 desc_len, max_items);
}

static int noa_wlan_noa_tx_replenish_ring_set(struct noa_wlan_client *client, u16 max_items,
					      u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;

	static struct noa_ring_ops noa_tx_replenish_ops = {
		.complete_hook = complete_replenish_ring_processing,
		.begin_hook = begin_replenish_ring_processing,
	};
	noa_tx_replenish_ops.write_payload = replenish_ring_write;

	ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
					   kNoaNetworkFlowHostToDevice,
					   kNoaWlanDirectH2DRingNoaTxBufferReplenish,
					   kNoaNepRingAnyDirection, &ring_regs);
	if (ret) {
		dev_err(client->dev, "noa_wlan_hw_nep_ring_reg_get fail.\n");
		return ret;
	}

	return noa_wlan_client_register_nep_ring(client, &client->noa_tx_replenish_ring,
						 NOA_RING_TYPE_PRODUCER, &noa_tx_replenish_ops,
						 &ring_regs, "WLAN_NOA_TX_REPLENISHMENT", NULL,
						 desc_len, max_items);
}

static void complete_feedback_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_unlock(&nep_ring->lock);
}

static void begin_feedback_ring_processing(struct noa_ring_wrapper *ring)
{
	struct wlan_sw_nep_ring *nep_ring = container_of(ring, struct wlan_sw_nep_ring, ring);

	spin_lock(&nep_ring->lock);
}

static ssize_t copy_data_write(void *d, size_t buf_len, const void *data, size_t data_len)
{
	if (data_len > buf_len) {
		return 0;
	}

	memcpy(d, data, data_len);

	return data_len;
}

static int noa_wlan_feedback_ring_set(struct noa_wlan_client *client, u16 max_items, u16 desc_len)
{
	struct noa_ring_regs ring_regs;
	int ret;

	static struct noa_ring_ops feedback_ops = {
		.complete_hook = complete_feedback_ring_processing,
		.begin_hook = begin_feedback_ring_processing,
	};
	feedback_ops.write_payload = copy_data_write;

	ret = noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlanDirect,
					   kNoaNetworkFlowHostToDevice,
					   kNoaWlanDirectH2DRingFeedback, kNoaNepRingAnyDirection,
					   &ring_regs);
	if (ret) {
		dev_err(client->dev, "noa_wlan_hw_nep_ring_reg_get fail.\n");
		return ret;
	}

	return noa_wlan_client_register_nep_ring(client, &client->feedback_ring,
						 NOA_RING_TYPE_PRODUCER, &feedback_ops, &ring_regs,
						 "WLAN_NOA_FEEDBACK", NULL, desc_len, max_items);
}

static void noa_wlan_hw_direct_ring_setup(struct noa_wlan_client *client)
{
	noa_wlan_direct_tx_ring_set(client, client->nep_tx_items, client->nep_tx_desc_sz);
	noa_wlan_direct_rx_ring_set(client, client->nep_rx_items, client->nep_rx_desc_sz);
	noa_wlan_rx_fallback_ring_set(client, client->nep_rx_items, client->wdev_rx_cpl_desc_sz);
	noa_wlan_vendor_rx_replenish_ring_set(client, client->rx_pkt_max + 1,
					      sizeof(struct noa_wlan_buffer_repln_desc));
	noa_wlan_noa_tx_replenish_ring_set(client, client->tx_bm_sz + 1,
					   sizeof(struct noa_wlan_buffer_repln_desc));
	noa_wlan_feedback_ring_set(client, client->nep_rx_items, NOA_DESC_BASIC_BYTE);
	noa_wlan_direct_tx_completion_ring_set(client, client->tx_pkt_max,
					       client->wdev_tx_cpl_desc_sz);
}

static int noa_wlan_ncp_start(struct noa_wlan_client *client)
{
	struct noa_wlan_cmd_fw_start req = {
		.share_addr = (unsigned long)client->share_addr,
		.reg_addr = (unsigned long)client->reg_addr,
		.share_size = client->share_size,
		.reg_size = client->reg_size,
		.ints_addr = client->ints_addr,
		.intm_addr = client->intm_addr,
	};
	noa_wlan_cfg_space_global_write(client, SHARE_ADDRESS, sizeof(client->share_addr),
					(void *)&client->share_addr);
	noa_wlan_cfg_space_global_write(client, SHARE_SIZE, sizeof(client->share_size),
					(void *)&client->share_size);
	noa_wlan_cfg_space_global_write(client, WDEV_REG_ADDRESS, sizeof(client->reg_addr),
					(void *)&client->reg_addr);
	noa_wlan_cfg_space_global_write(client, WDEV_REG_SIZE, sizeof(client->reg_size),
					(void *)&client->reg_size);
	noa_wlan_cfg_space_global_write(client, DOORBELL_ADDR, sizeof(client->doorbell_addr),
					(void *)&client->doorbell_addr);
	noa_wlan_cfg_space_global_write(client, FW_TRAP_ADDR, sizeof(client->fw_trap_addr),
					(void *)&client->fw_trap_addr);

	return noa_wlan_fw_request_send(NOA_WLAN_CMD_FW_START, &req, sizeof(req));
}

static int noa_wlan_hw_rx_handover(struct noa_wlan_client *client)
{
	if (!client || !client->ops || !client->ops->rx_handover)
		return 0;

	return client->ops->rx_handover(client->bus);
}

static int noa_wlan_hw_start(struct noa_wlan_client *client)
{
	int ret;

	ret = noa_wlan_bm_init(&client->vendor_rx_bm, client->rx_pkt_max + 1);
	if (ret)
		return ret;

	ret = noa_wlan_hw_rx_handover(client);
	if (ret) {
		dev_err(client->dev, "handover failed, err: %d\n", ret);
		return ret;
	}

	ret = noa_wlan_bm_init(&client->noa_tx_bm, client->tx_bm_sz);
	if (ret)
		return ret;

	ret = noa_wlan_hw_rings_start(client);
	if (ret)
		return ret;

	ret = noa_wlan_hw_nep_start(client);
	if (ret) {
		dev_err(client->dev, "nep start err: %d\n", ret);
		return ret;
	}

	noa_wlan_hw_direct_ring_setup(client);

	ret = noa_wlan_ncp_start(client);
	if (ret) {
		dev_err(client->dev, "ncp start err: %d\n", ret);
		return ret;
	}

	// TX BM should send after ncp start
	ret = noa_wlan_hw_txbm_sync(client, client->txbm, client->tx_bm_sz);
	if (ret) {
		dev_err(client->dev, "txbm sync fail.\n");
		return ret;
	}

	/* set flag to initial */
	set_bit(CLIENT_FLAG_START, &client->flags);
	return 0;
}

static void noa_wlan_hw_stop(struct noa_wlan_client *client)
{
	int ret;

	/* clear flag */
	clear_bit(CLIENT_FLAG_START, &client->flags);

	/* stop nep input rings */
	noa_wlan_hw_nep_input_stop(client);

	// Only send RPC to DPA if DPA is not under a crash state
	if (!client->dpa_crash_state) {
		ret = noa_wlan_fw_request_send(NOA_WLAN_CMD_FW_STOP, NULL, 0);
		if (ret)
			dev_err(client->dev, "hw stop err: %d\n", ret);
	}

	/* stop nep output rings */
	noa_wlan_hw_nep_output_stop(client);

	txbm_free_bufs(client);
	noa_wlan_bm_deinit(&client->vendor_rx_bm);
	noa_wlan_bm_deinit(&client->noa_tx_bm);
	/* sync back information after FW stop. */
	noa_wlan_hw_rings_stop(client);
	/* unmap dma address */
	noa_wlan_mapper_unmap_all(client);
}

static int noa_wlan_hw_rxbm_sync(struct noa_wlan_client *client, void *_bufs, int num, bool to_dev)
{
	u32 i = 0;
	struct noa_bm_buf *bufs = (struct noa_bm_buf *)_bufs;

	for (i = 0; i < num; i++) {
		if (noa_wlan_bm_register(&client->vendor_rx_bm, bufs[i].pktid, bufs[i].len,
					 bufs[i].dpa_va)) {
			pr_err("%s(): register pkt(%u) failed.\n", __func__, bufs[i].pktid);
			return -EINVAL;
		}
	}

	return noa_wlan_replenish_ring_write(client, &client->vendor_rx_replenish_ring.ring, bufs,
					     num, to_dev);
}

static int noa_wlan_hw_rx_handover_sync(struct noa_wlan_client *client, u64 handover_addr, u32 count)
{
	struct noa_wlan_cmd_rx_handover_sync req = {
		.handover_table_dpa_addr = handover_addr,
		.count = count,
	};
	int ret = 0;

	dev_info(client->dev, "%s: handover_addr=0x%llx, count=%u\n", __func__, handover_addr, count);

	ret = noa_wlan_fw_request_send(NOA_WLAN_CMD_RX_HANDOVER_SYNC, &req, sizeof(req));
	if (ret)
		dev_err(client->dev, "hw rx handover sync failed: %d\n", ret);

	return ret;
}

static int noa_wlan_hw_txq_active(struct noa_wlan_client *client, u16 ring_id, bool enable)
{
	struct {
		u16 ring_id;
		bool enable;
	} req = {
		.ring_id = ring_id,
		.enable = enable,
	};
	int ret = 0;

	dev_info(client->dev, "%s: ring %u: set enable=%u\n", __func__, ring_id, enable);
	noa_wlan_cfg_set_wifi_ring_active_state(client, RING_TYPE_TX_DATA, ring_id, enable);

	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		noa_wlan_fw_request_doorbell(DOORBELL_TX_RING_INFO_SYNC);
	} else {
		ret = noa_wlan_fw_request_send(NOA_WLAN_CMD_TX_RING_ACTIVE, &req, sizeof(req));
		if (ret)
			dev_err(client->dev, "hw txq active: %d\n", ret);
	}

	return ret;
}

static int noa_wlan_hw_sta_active(struct noa_wlan_client *client, void *info, bool enable)
{
	/* keep the struct sta_info in google_plat.h as the head of sta_info */
	int ret = 0;
	struct sta_info *sta_info = (struct sta_info *)(info);
	struct noa_wlan_sta_info noa_sta_info;

	memset(&noa_sta_info, 0, sizeof(noa_sta_info));
	noa_sta_info.enable = enable;
	noa_sta_info.oif = sta_info->oif;
	noa_sta_info.bss_idx = sta_info->bss_idx;
	memcpy(&noa_sta_info.qos_txq_map[0], &sta_info->qos_txq_map[0],
	       sizeof(u16) * PRIORITY_CLASS);
	memcpy(&noa_sta_info.addr[0], &sta_info->addr[0], sizeof(u8) * MAC_ADDR_LEN);
	noa_sta_info.encrypt_type = sta_info->encrypt_type;
	noa_sta_info.encap_type = sta_info->encap_type;
	noa_sta_info.lmac_id = sta_info->lmac_id;
	noa_sta_info.bmid = sta_info->bmid;
	noa_sta_info.search_idx = sta_info->search_idx;
	noa_sta_info.search_type = sta_info->search_type;
	noa_sta_info.dscp_tid_map_id = sta_info->dscp_tid_map_id;
	noa_sta_info.addry_en = sta_info->addry_en;
	noa_sta_info.addrx_en = sta_info->addrx_en;
	noa_sta_info.fw_metadata = sta_info->fw_metadata;

	/* updates the sta info subsection in the shared memory */
	if ((ret = noa_wlan_cfg_update_noa_wlan_sta_info(client, &noa_sta_info)) >= 0) {
		noa_sta_info.sta_table_idx = ret;
	}

	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		noa_wlan_fw_request_doorbell(DOORBELL_STATION_INFO_SYNC);
	} else {
		ret = noa_wlan_fw_request_send(NOA_WLAN_CMD_STA_ACTIVE, &noa_sta_info,
					       sizeof(noa_sta_info));
		if (ret < 0)
			dev_err(client->dev, "hw sta active: %d\n", ret);
	}

	return ret;
}

static int noa_wlan_hw_notify_pm_state(bool on)
{
	struct noa_wlan_cmd_pm_state_notify data = {
		.power_state = (uint32_t)on,
	};
	return noa_wlan_fw_request_send(NOA_WLAN_CMD_PM_STATE_NOTIFY, &data, sizeof(data));
}

static int noa_wlan_hw_ssr_dump(struct noa_wlan_client *client, void *seg)
{
	struct sscd_segment noa_seg;
	struct platform_device *sscd_pdev = NULL;
	struct sscd_platform_data *sscd_pdata = NULL;
	(void)seg;

	/* Find the implementation in noa/common/main.c */
	noa_wlan_get_sscd_src((void **)&sscd_pdata, (void **)&sscd_pdev);

	if (sscd_pdev == NULL || sscd_pdata == NULL) {
		pr_info("[WLAN] %s(): sscd_pdev is null, check mode: %d \n", __func__,
			get_noa_wlan_dp_mode(&sw_manager));
		return -EINVAL;
	}

	if (get_noa_wlan_dp_mode(&sw_manager) == NOA_WLAN_DATA_PATH_OFFLOAD_MODE) {
		noa_seg.addr = (void *)(noa_wlan_cfg_space_get_base_va(client));
		noa_seg.size = (u32)(noa_wlan_cfg_space_get_size(client));
		if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
			// Real device elf records the firmware DPA view address instead
			// of virtual address, however the elf parsing tool might read the
			// vaddr by default. To assure correctness, it needs to save
			// physical address in both vaddr and paddr.
			noa_seg.vaddr = (void *)(noa_wlan_cfg_space_get_base_pa(client));
			noa_seg.paddr = (void *)(noa_wlan_cfg_space_get_base_pa(client));
		} else {
			noa_seg.vaddr = (void *)(noa_wlan_cfg_space_get_base_va(client));
			noa_seg.paddr = (void *)(noa_wlan_cfg_space_get_base_va(client));
		}
		if (sscd_pdata->sscd_report) {
			sscd_pdata->sscd_report(sscd_pdev, &noa_seg, 1, SSCD_FLAGS_ELFARM32HDR,
						"google_dpa_wlan_reset");
		}
	}
	return 0;
}

static int noa_wlan_fw_event_txcpl_sync(struct noa_wlan_client *client, void *msg)
{
	if (client->ops && client->ops->txcpl_sync)
		client->ops->txcpl_sync(client->bus, msg);
	noa_wlan_nep_sw_ring_tx_cpl_pkt_inc(client);
	return 0;
}

static int noa_wlan_direct_txcpl_ring_read(struct noa_wlan_client *client, void **msg)
{
	if (likely(msg && *msg)) {
		return noa_wlan_fw_event_txcpl_sync(client, *msg);
	} else {
		pr_err("%s(): Received invalid descriptor: %p\n", __func__, *msg);
		return -EINVAL;
	}
}

static bool noa_wlan_hw_txcpl(struct noa_wlan_client *client, u32 *total_cnt)
{
	int ret = 0;
	int i = 0;
	noa_ring_consumer *direct_txcpl_ring;
	bool more = false;

	if (unlikely(!client)) {
		pr_err("%s(): Invalid client context\n", __func__);
		return false;
	}

	*total_cnt = 0;
	for (i = 0; i < client->tx_cpl_flow_max; i++) {
		direct_txcpl_ring = &client->direct_tx_cpl_ring[i].ring;
		ret = noa_wlan_client_ring_read_loop(client, direct_txcpl_ring,
						     noa_wlan_direct_txcpl_ring_read);
		if (unlikely(ret < 0)) {
			dev_err(client->dev, "Failed to read TXCPL ring %d, ret=%d\n", i, ret);
		} else {
			*total_cnt += ret;
		}
		more |= !noa_wlan_client_ring_is_empty(direct_txcpl_ring);
	}

	return more;
}

static int noa_wlan_hw_update_up2flow(struct noa_wlan_client *client, const uint8_t type,
				      const uint8_t *table)
{
	struct noa_wlan_cmd_update_up2flow_table req = {
		.type = type,
	};

	memcpy(req.table, table, sizeof(u8) * NUMPRIO);
	return noa_wlan_fw_request_send(NOA_WLAN_CMD_UPDATE_UP_2_FLOW, &req, sizeof(req));
}

static int noa_wlan_hw_update_flowid_lkup_entry(struct noa_wlan_client *client, const void *info)
{
	struct flow_id_entry_update *entry = (struct flow_id_entry_update *)info;
	struct noa_wlan_cmd_update_flowid_lkup_entry req;

	if (!entry) {
		dev_err(client->dev, "Invalid flowid entry\n");
		return -EINVAL;
	}

	req.flowid = entry->flowid;
	req.prio = entry->prio;
	req.ifindex = entry->ifindex;
	req.oif = entry->oif;
	req.role = entry->role;
	req.is_add = entry->is_add;
	memcpy(req.da, entry->da, sizeof(uint8_t) * ETH_MAC_LEN);

	return noa_wlan_fw_request_send(NOA_WLAN_CMD_UPDATE_FLOWID_LOOK_UP_ENTRY, &req,
					sizeof(req));
}

static void noa_wlan_hw_sync_pci_link_state(struct noa_wlan_client *client, int32_t state,
					    bool is_to_shm)
{
	static int32_t link_state_cache = 0;
	int32_t ret = 0;

	if (!client) {
		return;
	}

	if (is_to_shm) {
		link_state_cache = state;
		noa_wlan_cfg_pci_dev_state_write(client, PM_STATE, state);
	} else {
		// If the local cache is different from the shared memory, it means the desynchronization
		// between AP and DPA needs to be resolved.
		ret = noa_wlan_cfg_pci_dev_state_read(client, PM_STATE);
		if (ret != link_state_cache && ret != -EINVAL) {
			link_state_cache = ret;
#ifdef CONFIG_NOA_FULLSOC_SUPPORT
			pci_enable_link_state(to_pci_dev(client->dev), ret);
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
		}
	}
}

static void noa_wlan_hw_notify_station_state(struct noa_wlan_client *client, u8 state, int iif)
{
	dev_info(client->dev,
		 "Skipping notification: RX proxy mechanism is disabled, state: %u, iif: %d\n",
		 state, iif);
	// TODO(b/469900115) - Disabled the RPC call due to b/469898363.
	//int ret = 0;
	//if (state) {
	//	ret = noa_nep_cmd_request_send(CMD_STA_CONNECT, &iif, sizeof(int), NULL, NULL);
	//} else {
	//	ret = noa_nep_cmd_request_send(CMD_STA_DISCONNECT, &iif, sizeof(int), NULL, NULL);
	//}
	//if (ret)
	//	dev_err(client->dev, "notify station state err: %d\n", ret);
}
/* fw_ops is used for wlan driver callback */
static struct noa_wlan_fw_ops fw_ops = {
	.start = noa_wlan_hw_start,
	.stop = noa_wlan_hw_stop,
	.rxbm_sync = noa_wlan_hw_rxbm_sync,
	.txq_active = noa_wlan_hw_txq_active,
	.sta_active = noa_wlan_hw_sta_active,
	.notify_pm_state = noa_wlan_hw_notify_pm_state,
	.ssr_dump = noa_wlan_hw_ssr_dump,
	.txcpl = noa_wlan_hw_txcpl,
	.update_up2flow = noa_wlan_hw_update_up2flow,
	.update_flowid_lkup_entry = noa_wlan_hw_update_flowid_lkup_entry,
	.sync_pci_link_state = noa_wlan_hw_sync_pci_link_state,
	.notify_station_state = noa_wlan_hw_notify_station_state,
	.rx_handover_sync = noa_wlan_hw_rx_handover_sync,
};

static int noa_wlan_fw_event_doorbell(struct noa_wlan_client *client, void *msg)
{
	u32 *value = msg;

	if (client->ops && client->ops->ringbell)
		client->ops->ringbell(client->bus, *value);
	return 0;
}

static int noa_wlan_fw_event_wakeup(struct noa_wlan_client *client, void *msg)
{
	bool *lock = msg;
	if (client->ops && client->ops->wakeup)
		return client->ops->wakeup(*lock);
	return 0;
}

static int noa_wlan_fw_event_manage_power(struct noa_wlan_client *client, void *msg)
{
	bool request = *(bool *)msg;

	if (!client->ops || !client->ops->manage_power)
		return -ENOSYS;

	if (client->fw_active != request) {
		if (client->ops->manage_power(client->bus, request))
			return -EINVAL;
		client->fw_active = request;
	}
	return 0;
}

static int noa_wlan_fw_event_packet_sniffer_full(struct noa_wlan_client *client, void *msg)
{
	struct noa_wlan_entry *dbg_entry = (struct noa_wlan_entry *)(client->dbg_entry);
	struct wlan_debug_controller_client *dbg_client;
	struct wlan_dbg_entry *dbg_comp_entry;
	struct wlan_debug_component_client *dbg_comp_client;

	if (dbg_entry) {
		dbg_client = (struct wlan_debug_controller_client *)(dbg_entry->priv);
	} else {
		return -EINVAL;
	}

	dbg_comp_entry = dbg_client->component_entries[DBG_PACKET_SNIFFER];
	dbg_comp_client = (struct wlan_debug_component_client *)(dbg_comp_entry->priv);

	if (dbg_client && dbg_client->ops[DBG_PACKET_SNIFFER] &&
	    dbg_client->ops[DBG_PACKET_SNIFFER]->notify_driver) {
		return dbg_client->ops[DBG_PACKET_SNIFFER]->notify_driver(dbg_comp_client,
									  (u16 *)msg);
	}

	return -ENOSYS;
}

int noa_wlan_fw_event_recv(int event, void *msg)
{
	int ret = 0;
	struct noa_wlan_client *client = sw_manager.noa_wlan_client;

	if (!client)
		return -EINVAL;

	switch (event) {
	case NOA_WLAN_EVENT_DOORBELL:
		ret = noa_wlan_fw_event_doorbell(client, msg);
		break;
	case NOA_WLAN_EVENT_TXCPL_SYNC:
		ret = noa_wlan_fw_event_txcpl_sync(client, msg);
		break;
	case NOA_WLAN_EVENT_WAKEUP:
		ret = noa_wlan_fw_event_wakeup(client, msg);
		break;
	case NOA_WLAN_EVENT_BUS_POWER:
		ret = noa_wlan_fw_event_manage_power(client, msg);
		break;
	case NOA_WLAN_EVENT_CMD_COMPLETION:
		ret = noa_wlan_fw_event_cmd_completion(msg);
		break;
	case NOA_WLAN_EVENT_PACKET_SNIFFER_FULL:
		ret = noa_wlan_fw_event_packet_sniffer_full(client, msg);
		break;
	default:
		break;
	}
	return ret;
}

static void *noa_wlan_entry_register(u32 priv_sz, const char *name, struct kobj_type *ktype,
				     struct noa_wlan_client *client)
{
	int ret;
	struct noa_core_entry *wlan_entry =
		container_of((void *)client, struct noa_core_entry, priv);
	u32 sz = sizeof(struct noa_wlan_entry) + ALIGN(priv_sz, 4);
	// create wlan/{sub_entry}
	struct noa_wlan_entry *entry = kzalloc(sz, GFP_KERNEL);

	if (!entry)
		return NULL;

	entry->wlan_core = wlan_entry;

	// add sub_entry under parent entry
	ret = kobject_init_and_add(&entry->kobj, ktype, &wlan_entry->kobj, name);
	if (ret) {
		kobject_put(&entry->kobj);
	}
	return entry;
}

static void noa_wlan_entry_unregister(struct noa_wlan_entry *entry)
{
	if (entry != NULL) {
		kobject_del(&entry->kobj);
		kobject_put(&entry->kobj);
		kfree(entry);
	}
}

static struct noa_wlan_entry *wlan_debug_entry_register(struct noa_wlan_client *client)
{
	struct noa_wlan_entry *dbg_entry;
	dbg_entry = noa_wlan_entry_register(sizeof(struct wlan_debug_controller_client), "dbg",
					    &noa_wlan_dbg_ktype, client);
	wlan_debug_component_client_alloc(dbg_entry);
	return dbg_entry;
}

static void wlan_debug_entry_unregister(struct noa_wlan_entry *dbg_entry)
{
	wlan_debug_component_client_dealloc(dbg_entry);
	noa_wlan_entry_unregister(dbg_entry);
}

struct noa_wlan_switch_manager *noa_wlan_get_switch_manager(void)
{
	return &sw_manager;
}

static void *noa_wlan_client_pcie_config_read(struct noa_wlan_client *client)
{
	return noa_wlan_cfg_space_read_pcie_config(client);
}

static size_t noa_wlan_client_pcie_config_size(struct noa_wlan_client *client,
					       struct pci_saved_state *state)
{
	struct pci_cap_saved_data *cap;
	size_t len;
	int total_size = 16 * 4;

	cap = state->cap;
	while (cap->size) {
		len = cap->size + sizeof(struct pci_cap_saved_data);
		cap = (struct pci_cap_saved_data *)((u64)cap + len);
		total_size += len;
	}
	total_size += sizeof(struct pci_cap_saved_data);

	return total_size;
}

static int noa_wlan_client_pcie_config_write(struct noa_wlan_client *client,
					     struct pci_saved_state *state)
{
	if (state == NULL) {
		dev_err(client->dev, "no default state for pci config space\n");
		return -EINVAL;
	}

	noa_wlan_cfg_space_write_pcie_config(client, (void *)state,
					     noa_wlan_client_pcie_config_size(client, state));
	return 0;
}

int noa_wlan_client_pci_dev_state_sync(struct noa_wlan_client *client, bool save_to_shm)
{
	struct pci_dev_reconcile_fields reconcile_state;
	struct pci_dev *pdev = to_pci_dev(client->dev);
	int ret = 0;

	if (pdev == NULL) {
		return -ENODEV;
	}

	if (save_to_shm) {
		ret = noa_wlan_client_pcie_config_write(client, pci_store_saved_state(pdev));

		noa_wlan_cfg_pci_dev_state_write(client, IS_BUSMASTER, pdev->is_busmaster);
		noa_wlan_cfg_pci_dev_state_write(client, ENABLE_CNT,
						 atomic_read(&pdev->enable_cnt));
		noa_wlan_cfg_pci_dev_state_write(client, CURRENT_STATE, pdev->current_state);

	} else if (client->ops && client->ops->sync_back_pci_dev_state) {
		reconcile_state.saved_state = noa_wlan_client_pcie_config_read(client);
		reconcile_state.is_busmaster =
			noa_wlan_cfg_pci_dev_state_read(client, IS_BUSMASTER);
		reconcile_state.enable_cnt = noa_wlan_cfg_pci_dev_state_read(client, ENABLE_CNT);
		reconcile_state.current_state =
			noa_wlan_cfg_pci_dev_state_read(client, CURRENT_STATE);
		reconcile_state.saved_state_size = noa_wlan_client_pcie_config_size(
			client, (struct pci_saved_state *)reconcile_state.saved_state);
		client->ops->sync_back_pci_dev_state(client->bus, (void *)&reconcile_state);
		return 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(noa_wlan_client_pci_dev_state_sync);

struct noa_wlan_client *noa_wlan_client_alloc(struct noa_wlan_client_ops *ops)
{
	struct noa_wlan_client *client;

	/* initial client */
	client = noa_entry_register(sizeof(struct noa_wlan_client), "wlan", &noa_wlan_ktype);
	if (!client)
		return NULL;

	client->dbg_entry = wlan_debug_entry_register(client);
	client->ops = ops;
	client->fw_ops = &fw_ops;
	sw_manager.noa_wlan_client = client;
	noa_wlan_cmd_wq_init(client);
	return client;
}
EXPORT_SYMBOL_GPL(noa_wlan_client_alloc);

void noa_wlan_client_free(struct noa_wlan_client *client)
{
	if (!client)
		return;

	noa_wlan_cmd_wq_exit(client);

	/* exit client */
	wlan_debug_entry_unregister(client->dbg_entry);
	noa_entry_unregister(client);
	sw_manager.noa_wlan_client = NULL;
}
EXPORT_SYMBOL_GPL(noa_wlan_client_free);

int noa_wlan_client_register(struct noa_wlan_client *client)
{
	struct noa_wlan_cmd_fw_init req;
	int ret;

	/* assign NOA hardware values */
	noa_wlan_hw_set(client);

	memset(&req, 0, sizeof(req));
	client->tx_bm_sz = MAX_TXBM_BUF_NUM;

	if (noa_wlan_cfg_space_init(client) != 0) {
		return -ENOMEM;
	} else {
		if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT))
			req.noa_shared_mem_addr = (u64)(noa_wlan_cfg_space_get_base_pa(client));
		else
			req.noa_shared_mem_addr = (u64)(noa_wlan_cfg_space_get_base_va(client));

		req.noa_shared_mem_size = (u32)(noa_wlan_cfg_space_get_size(client));
	}

	ret = noa_wlan_hw_crash_dump_register(client);
	if (ret) {
		dev_err(client->dev, "%s(): failed to register crash dump segments, err: %d\n",
			__func__, ret);
		return ret;
	}

	ret = noa_wlan_rpc_init();
	if (ret) {
		dev_err(client->dev, "%s(): failed to init noa wlan rpc, err: %d\n", __func__, ret);
		return ret;
	}

	client->tx_bm_sz = MAX_TXBM_BUF_NUM;

	wlan_debug_component_client_init_shared_mem_info(client);

	noa_wlan_cfg_space_global_write(client, CHIP_TYPE, sizeof(client->type),
					(void *)&client->type);
	noa_wlan_cfg_space_global_write(client, RX_PACKET_NUM, sizeof(client->rx_pkt_max),
					(void *)&client->rx_pkt_max);
	noa_wlan_cfg_space_global_write(client, RX_BUFFER_SIZE, sizeof(client->rx_buf_sz),
					(void *)&client->rx_buf_sz);
	noa_wlan_cfg_space_global_write(client, VENDOR_TX_PACKET_ID_MAX, sizeof(client->tx_pkt_max),
					(void *)&client->tx_pkt_max);
	noa_wlan_cfg_space_global_write(client, NOA_TX_PACKET_NUM, sizeof(client->tx_bm_sz),
					(void *)&client->tx_bm_sz);
	noa_wlan_cfg_space_global_write(client, RX_PKT_TLV_SIZE, sizeof(client->rx_pkt_tlv_size),
					(void *)&client->rx_pkt_tlv_size);

	pci_save_state(to_pci_dev(client->dev));
	noa_wlan_client_pcie_config_write(client, pci_store_saved_state(to_pci_dev(client->dev)));

	ret = noa_wlan_fw_request_send(NOA_WLAN_CMD_FW_INIT, &req, sizeof(req));
	if (ret) {
		dev_err(client->dev, "%s(): FW_INIT failed, err: %d\n", __func__, ret);
		goto err_rpc_deinit;
	}

	return 0;

err_rpc_deinit:
	noa_wlan_rpc_deinit();
	return ret;
}
EXPORT_SYMBOL_GPL(noa_wlan_client_register);

int noa_wlan_irq_request(struct noa_wlan_client *client)
{
	int i = 0;
	struct noa_wlan_cmd_irq_request req = {
		.irq_nums = client->irq_nums,
	};
	struct wlan_irq_mapping_entry *irq_table = get_irq_table(client);
	if (irq_table == NULL) {
		pr_err("%s(): irq table is null\n", __func__);
	}

	printk("%s(): irq_num: %d, nvec_used: %d, msi_index: %d, data: %d\n", __func__,
	       client->irqs[0], client->msi_descs[0].nvec_used, client->msi_descs[0].msi_index,
	       client->msi_descs[0].data);

	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		if (client->msi_descs[0].nvec_used > 0) {
			// real device (PCIe device provided)
			for (i = 0; i < MAX_WLAN_PCIE_MSI_NUM; i++) {
				req.msi_descs[i].nvec_used = client->msi_descs[i].nvec_used;
				req.msi_descs[i].msi_index = client->msi_descs[i].msi_index;
				req.msi_descs[i].data = client->msi_descs[i].data;
			}
		}
		for (i = 0; i < req.irq_nums; i++) {
			req.irqs[i] = irq_table[i].noa_wlan_irq;
		}
	} else {
		// driver mode
		for (i = 0; i < req.irq_nums; i++) {
			req.irqs[i] = client->irqs[i];
		}
	}

	return noa_wlan_fw_request_send(NOA_WLAN_CMD_ISR_REGISTER, &req, sizeof(req));
}
EXPORT_SYMBOL_GPL(noa_wlan_irq_request);

void noa_wlan_client_unregister(struct noa_wlan_client *client)
{
	int ret;

	// Only send RPC to DPA if DPA is not under a crash state
	if (!client->dpa_crash_state) {
		ret = noa_wlan_fw_request_send(NOA_WLAN_CMD_FW_EXIT, NULL, 0);
		if (ret)
			dev_err(client->dev, "fw exit err: %d\n", ret);
		pr_info("%s(): rpc request send", __func__);
	}

	noa_wlan_rpc_deinit();
	noa_wlan_hw_crash_dump_unregister();
	noa_wlan_cfg_space_deinit(client);
	noa_wlan_hw_reset(client);
}
EXPORT_SYMBOL_GPL(noa_wlan_client_unregister);

void noa_wlan_client_obj_register(void *dpa, void *vendor, void *bus, void *dev, void **cur_ops)
{
	struct noa_wlan_switch_manager *manager = &sw_manager;
	struct noa_wlan_dynamic_switch_init_params params = {
		.vendor_plat_ops = vendor,
		.dpa_plat_ops = dpa,
		.current_plat_ops = cur_ops,
		.vendor_bus = bus,
		.vendor_dev = dev,
		.dp_mode = noa_wlan_enable_get() ? NOA_WLAN_DATA_PATH_OFFLOAD_MODE :
						   NOA_WLAN_DATA_PATH_BYPASS_MODE,
	};

	noa_wlan_dynamic_switch_init(manager, &params);
}
EXPORT_SYMBOL_GPL(noa_wlan_client_obj_register);

void noa_wlan_client_obj_unregister(void)
{
	struct noa_wlan_switch_manager *manager = &sw_manager;

	noa_wlan_dynamic_switch_deinit(manager);
}
EXPORT_SYMBOL_GPL(noa_wlan_client_obj_unregister);

void noa_wlan_dynamic_switch(bool enable)
{
	struct noa_wlan_switch_manager *manager = &sw_manager;
	enum noa_wlan_data_path_mode current_dp_mode = get_noa_wlan_dp_mode(manager);
	enum noa_wlan_data_path_mode target_dp_mode =
		enable ? NOA_WLAN_DATA_PATH_OFFLOAD_MODE : NOA_WLAN_DATA_PATH_BYPASS_MODE;

	pr_err("%s(): Try to switch from %s -> %s\n", __func__,
	       get_data_path_mode_name(current_dp_mode), get_data_path_mode_name(target_dp_mode));

	// Sanity check
	if (target_dp_mode == current_dp_mode) {
		return;
	}

	// Do dynamic switch
	noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_SERVICE_PRE_SWITCH);
	noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_DEVICE_PRE_SWITCH);
	noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_DEVICE_POST_SWITCH);
	noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_SERVICE_POST_SWITCH);
}
