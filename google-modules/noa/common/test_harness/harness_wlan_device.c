#include "harness_wlan_device.h"

#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/if_arp.h>

#include "harness_buffer_management.h"
#include "harness_ring.h"
#include "common/core.h"

void noa_harness_wlan_isr_task(struct work_struct *work)
{
	int ret;
	struct noa_harness_vdev *vdev = container_of(work, struct noa_harness_vdev, work);
	struct noa_harness *tm = vdev->tm;

	ret = noa_harness_ring_read(vdev);
	if (ret == -EAGAIN) {
		queue_work(tm->wq, &vdev->work);
	}
}

static netdev_tx_t noa_harness_wlan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct noa_harness_vdev *vdev = netdev_priv(dev);
	int ret;

	WARN_ON(skb_is_nonlinear(skb));

	ret = noa_harness_ring_write(vdev, skb);
	if (ret) {
		if (ret == -EAGAIN) {
			return NETDEV_TX_BUSY;
		}
		dev_kfree_skb_any(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return NETDEV_TX_OK;
}

static int noa_harness_open(struct net_device *dev)
{
	netif_carrier_on(dev);
	netif_start_queue(dev);
	return 0;
}

static int noa_harness_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	return 0;
}

static const struct net_device_ops noa_harness_wlan_netdev_ops = {
	.ndo_open = noa_harness_open,
	.ndo_stop = noa_harness_stop,
	.ndo_start_xmit = noa_harness_wlan_xmit,
};

void noa_harness_wlan_host_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &noa_harness_wlan_netdev_ops;

	dev->mtu = 1500;

	eth_hw_addr_random(dev);

	dev->features &= ~(NETIF_F_SG | NETIF_F_GSO_MASK);
	dev->hw_features = dev->features;
	dev->vlan_features = dev->features;

	netif_carrier_off(dev);
}

static const u8 HARNESS_WLAN_NCP_FIXED_MAC_ADDR[ETH_ALEN] = {0xa6, 0x35, 0xdc, 0x8d, 0x2a, 0xdc};

void noa_harness_wlan_ncp_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &noa_harness_wlan_netdev_ops;

	dev->mtu = 1500;

	dev_addr_set(dev, HARNESS_WLAN_NCP_FIXED_MAC_ADDR);

	dev->features &= ~(NETIF_F_SG | NETIF_F_GSO_MASK);
	dev->hw_features = dev->features;
	dev->vlan_features = dev->features;

	netif_carrier_off(dev);
}

int noa_harness_wlan_format_tx_data(void *data, const struct noa_harness_pkt *pkt)
{
	struct noa_desc *desc = (struct noa_desc *)data;

	noa_harness_cpu_addr_to_dp((void *)pkt->address, &desc->dp_low, (u32 *)&desc->dp_high);
	desc->reason = (pkt->send_to_netengine) ? FWD_REASON_NETENGINE : FWD_REASON_FEEDTHROUGH;
	desc->mode = NOAD_MODE_DATA;
	desc->tkid = pkt->tkid;
	desc->dv = pkt->dpa_address;
	desc->head_offset = pkt->head_offset;
	desc->dl = pkt->size;
	return 0;
}

int noa_harness_wlan_parse_rx_pkt(struct noa_harness_pkt *pkt, const void *data)
{
	const struct noa_desc *desc = (const struct noa_desc *)data;
	pkt->tkid = desc->tkid;
	pkt->should_drop = (desc->mode == NOAD_MODE_FEEDBACK);
	pkt->needs_nep_buffer_refill = (desc->reason == FWD_REASON_NETENGINE);
	pkt->size = desc->dl;
	return 0;
}
