#include "harness_ring.h"

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <net/if_inet6.h>

#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_ring_service_proxy.h>

#include "common/ring_id.h"
#include "common/core.h"
#include "common/ring.h"
#include "noa_test_harness.h"
#include "harness_buffer_management.h"
#include "harness_memory_mapper.h"
#include "harness_doorbell.h"
#include "ring_service/ring_mgmt/ring_manager.h"

static struct noa_ring_ops g_ops = {
	NULL,
};

#define MAX_HANDLE_COUNT (32U)

int noa_harness_ring_read(struct noa_harness_vdev *vdev)
{
	bool needs_nep_buffer_refill = false;
	int ret;
	u32 index = 0;
	u32 i;
	u32 count;
	struct noa_harness *tm = vdev->tm;
	struct harness_ring *h_ring = &vdev->rx_ring;
	struct noa_ring_wrapper *ring = &h_ring->ring;
	struct sk_buff *skb;
	struct net_device *dev = vdev->dev;
	struct noa_harness_pkt pkts[MAX_HANDLE_COUNT];

	spin_lock(&h_ring->lock);
	count = min(MAX_HANDLE_COUNT, noa_ring_items_count(noa_ring_head_read_once(ring),
							   ring->basic.tail, ring->basic.size));

	if (!count) {
		ret = 0;
		spin_unlock(&h_ring->lock);
		goto out;
	}

	for (i = 0; i < count; ++i) {
		ret = vdev->parse_rx_pkt(&pkts[index],
					 noa_ring_buf_pos(ring->basic.base, ring->basic.tail,
							  ring->basic.item_len));
		if (ret) {
			goto next;
		}
		index++;
	next:
		ring->basic.tail = noa_ring_move_pos(ring->basic.tail, 1, ring->basic.size);
	}
	noa_ring_tail_write_once(ring, ring->basic.tail);
	spin_unlock(&h_ring->lock);

	for (i = 0; i < index; ++i) {
		bool is_from_nep_buffer_pool = false;
		if (pkts[i].needs_nep_buffer_refill)
			needs_nep_buffer_refill = true;

		skb = noa_harness_buf_mgr_get_skb(&tm->buf_mgr, pkts[i].tkid);
		noa_harness_buf_mgr_free_id(&tm->buf_mgr, pkts[i].tkid);
		if (!skb) {
			dev_err(tm->dev, "Invalid skb from ring %s with tkid %u, drop this data\n",
				ring->name, pkts[i].tkid);
			continue;
		}
		if (pkts[i].should_drop) {
			dev_kfree_skb_any(skb);
			continue;
		}
		if (pkts[i].tkid >= MAX_NOA_HARNESS_GENERIC_TKID_SIZE) {
			is_from_nep_buffer_pool = true;
		}
		// Prepare the skb for the receive path on the destination device
		skb->dev = dev;
		if (is_from_nep_buffer_pool) {
			struct iphdr *iph;
			skb_put(skb, pkts[i].size);
			if (dev->type == ARPHRD_ETHER) {
				skb->protocol = eth_type_trans(skb, dev);
			} else {
				skb_reset_network_header(skb);
				iph = ip_hdr(skb);
				if (iph->version == 4) {
					skb->protocol = htons(ETH_P_IP);
				} else if (iph->version == 6) {
					skb->protocol = htons(ETH_P_IPV6);
				}
			}
		} else {
			if (dev->type == ARPHRD_ETHER) {
				skb->protocol = eth_type_trans(skb, dev);
			}
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		// Update RX stats for the destination device
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;

		// Pass the packet up to the network stack for the destination device
		if (unlikely(netif_rx(skb) == NET_RX_DROP)) {
			dev->stats.rx_dropped++;
		}
	}
	ret = 0;

out:
	if (!__noa_ring_is_empty(noa_ring_head_read_once(ring), ring->basic.tail)) {
		ret = -EAGAIN;
	}

	if (needs_nep_buffer_refill && vdev->support_nep_buffer_pool) {
		queue_work(tm->wq, &vdev->refill_work);
	}

	return ret;
}

int noa_harness_ring_write(struct noa_harness_vdev *vdev, struct sk_buff *skb)
{
	int ret;
	int skb_id;
	const bool is_send_to_netengine =
		(vdev->mode == NOA_HARNESS_DEVICE_MODE_NETENGINE && (skb->len > 0));
	struct noa_harness *tm = vdev->tm;
	struct noa_harness_buffer_manager *buf_mgr = &tm->buf_mgr;
	struct harness_ring *h_ring = &vdev->tx_ring;
	struct noa_ring_wrapper *ring = &h_ring->ring;
	struct noa_harness_pkt pkt = {
		.should_drop = false,
		.send_to_netengine = false,
		.needs_nep_buffer_refill = false,
		.head_offset = skb_headroom(skb),
		.size = skb_headroom(skb) + skb->len,
		.tkid = 0,
		.dpa_address = 0,
		.address = (u64)skb->head,
	};
	struct noa_harness_mapping_params params = { 0 };
	struct noa_harness_skb_table_entry entry = {
		.skb_address = skb,
		.is_map_to_dpa = false,
		.map_dpa_len = 0,
	};

	if (is_send_to_netengine) {
		params.cpu_addr = (void *)pkt.address;
		// This additional size is mapped because the DMA330 rounds up all transfers to 256-byte
		// alignment for unaligned data handling.
		params.size = round_up(pkt.size, 256U) + 32U;
		ret = noa_harness_memory_mapper_remap(&buf_mgr->mapper, &params, &pkt.dpa_address);
		if (ret) {
			dev_err(tm->dev, "Failed to remap address 0x%lx, err: %d\n",
				(unsigned long)params.cpu_addr, ret);
			return ret;
		}
		entry.is_map_to_dpa = true;
		entry.map_dpa_len = params.size;
		pkt.send_to_netengine = true;
	}
	skb_id = noa_harness_buf_mgr_alloc_id(&tm->buf_mgr, &entry);
	if (skb_id < 0) {
		ret = -EAGAIN;
		goto out;
	}

	spin_lock(&h_ring->lock);

	if (__noa_ring_is_full(ring->basic.head, noa_ring_tail_read_once(ring), ring->basic.size)) {
		ret = -EAGAIN;
		goto out;
	}

	pkt.tkid = skb_id;
	ret = vdev->format_tx_data(
		noa_ring_buf_pos(ring->basic.base, ring->basic.head, ring->basic.item_len), &pkt);
	if (ret) {
		goto out;
	}
	ring->basic.head = noa_ring_move_pos(ring->basic.head, 1, ring->basic.size);
	noa_ring_head_write_once(ring, ring->basic.head);
	ret = 0;

out:
	spin_unlock(&h_ring->lock);
	if (ret) {
		if (skb_id >= 0) {
			noa_harness_buf_mgr_free_id(&tm->buf_mgr, skb_id);
		} else if (params.cpu_addr) {
			noa_harness_memory_mapper_unmap(&buf_mgr->mapper, &params);
		}
	} else {
		noa_harness_trigger_doorbell(vdev->doorbell);
	}
	return ret;
}

int noa_harness_register_ring(struct noa_harness *tm, struct noa_harness_vdev *vdev,
			      struct harness_ring *h_ring, u8 type, u8 direction, u8 ring_id,
			      const char *name, u32 desc_len, u32 max_items)
{
	struct device *dpa_dev = google_dpa_get_dpa_dev(tm->dpa);
	int ret;
	u8 interface, flow, category;
	struct noa_ring_regs regs;
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.size = max_items,
		.item_len = desc_len,
	};

	if (!dpa_dev) {
		dev_err(tm->dev, "Failed to get DPA device for ring %s\n", name);
		return -ENODEV;
	}
	h_ring->id = ring_id;
	h_ring->direction = direction;

	NoaRingPathIdParse(ring_id, &interface, &flow, &category);
	ret = NoaRingSharedRegsGet(&regs, interface, flow, category, direction);
	if (ret) {
		dev_err(tm->dev, "Failed to get shared regs for ring %s, err %d\n", name, ret);
		return ret;
	}

	spin_lock_init(&h_ring->lock);

	h_ring->ring_buffer = dmam_alloc_coherent(dpa_dev, max_items * desc_len,
						  &h_ring->ring_buffer_dma, GFP_KERNEL);
	if (!h_ring->ring_buffer) {
		dev_err(tm->dev, "Failed to allocate ring buffer for %s\n", name);
		return -ENOMEM;
	}

	info.base = h_ring->ring_buffer;
	info.dpa_base = (char *)h_ring->ring_buffer_dma;

	ret = noa_ring_regs_wrapper_init(&h_ring->ring, type, &g_ops, &regs, vdev, name, 0);
	if (ret) {
		dev_err(tm->dev, "Failed to init ring wrapper for %s, err %d\n", name, ret);
		goto free_buffer;
	}

	noa_ring_info_setup(&h_ring->ring, &info);
	noa_ring_activate(&h_ring->ring);

	ret = google_dpa_ring_service_rpc_event_activate(ring_id, direction);
	if (ret) {
		dev_err(tm->dev, "Failed to activate ring %u %u\n", ring_id, direction);
		goto deactivate_ring;
	}

	return 0;
deactivate_ring:
	noa_ring_deactivate(&h_ring->ring);

free_buffer:
	dmam_free_coherent(dpa_dev, max_items * desc_len, h_ring->ring_buffer,
			   h_ring->ring_buffer_dma);
	return ret;
}

void noa_harness_unregister_ring(struct noa_harness *tm, struct harness_ring *h_ring)
{
	struct device *dpa_dev = google_dpa_get_dpa_dev(tm->dpa);
	struct noa_ring_wrapper *ring = &h_ring->ring;

	google_dpa_ring_service_rpc_event_deactivate(h_ring->id, h_ring->direction);

	noa_ring_deactivate(ring);

	if (h_ring->ring_buffer) {
		dmam_free_coherent(dpa_dev, ring->basic.item_len * ring->basic.size,
				   h_ring->ring_buffer, h_ring->ring_buffer_dma);
	}
	h_ring->ring_buffer = NULL;
	h_ring->ring_buffer_dma = 0;
	noa_ring_info_clean(ring);
}
