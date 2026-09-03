#include "harness_modem_device.h"

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

void noa_harness_modem_isr_task(struct work_struct *work)
{
	int ret;
	struct noa_harness_vdev *vdev = container_of(work, struct noa_harness_vdev, work);
	struct noa_harness *tm = vdev->tm;

	ret = noa_harness_ring_read(vdev);
	if (ret == -EAGAIN) {
		queue_work(tm->wq, &vdev->work);
	}
}

static netdev_tx_t noa_harness_modem_xmit(struct sk_buff *skb, struct net_device *dev)
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

static const struct net_device_ops noa_harness_modem_netdev_ops = {
	.ndo_open = noa_harness_open,
	.ndo_stop = noa_harness_stop,
	.ndo_start_xmit = noa_harness_modem_xmit,
};

void noa_harness_modem_netdev_setup(struct net_device *dev)
{
	dev->netdev_ops = &noa_harness_modem_netdev_ops;

	dev->mtu = 1500;

	dev->flags |= IFF_POINTOPOINT | IFF_NOARP;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->type = ARPHRD_NONE;

	dev->features &= ~(NETIF_F_SG | NETIF_F_GSO_MASK);
	dev->hw_features = dev->features;
	dev->vlan_features = dev->features;

	netif_carrier_off(dev);
}

enum fake_modem_vendor_desc_type {
	MODEM_VENDOR_MSG_TYPE = 1,
	MODEM_VENDOR_PAYLOAD_TYPE = 2,
};

struct fake_modem_vendor_msg_desc {
	u8 flag;
	u8 resv[15];
} __attribute__((packed, aligned(4)));
static_assert(NOA_HARNESS_MODEM_DESC_BYTE == sizeof(struct fake_modem_vendor_msg_desc));

struct fake_modem_vendor_payload_desc {
	u8 flag;
	u8 resv[13];
	u16 tkid;
} __attribute__((packed, aligned(4)));
static_assert(NOA_HARNESS_MODEM_DESC_BYTE == sizeof(struct fake_modem_vendor_payload_desc));

struct fake_modem_vendor_desc {
	struct fake_modem_vendor_msg_desc msg;
	struct fake_modem_vendor_payload_desc payload;
} __attribute__((packed, aligned(4)));

int noa_harness_modem_ncp_format_tx_data(void *data, const struct noa_harness_pkt *pkt)
{
	struct noa_desc *desc = (struct noa_desc *)data;
	struct fake_modem_vendor_desc *vendor_desc =
		(struct fake_modem_vendor_desc *)&(desc->ext_data[0]);

	noa_harness_cpu_addr_to_dp((void *)pkt->address, &desc->dp_low, (u32 *)&desc->dp_high);
	desc->reason = (pkt->send_to_netengine) ? FWD_REASON_NETENGINE : FWD_REASON_FEEDTHROUGH;
	desc->mode = NOAD_MODE_DATA;
	desc->desc_type = NOA_DESC_MODEM_RX_MTK_MSG_PD;
	desc->tkid = pkt->tkid;
	desc->dv = pkt->dpa_address;
	desc->head_offset = pkt->head_offset;
	desc->dl = pkt->size;
	vendor_desc->msg.flag = MODEM_VENDOR_MSG_TYPE;
	vendor_desc->payload.flag = MODEM_VENDOR_PAYLOAD_TYPE;
	vendor_desc->payload.tkid = desc->tkid;

	return 0;
}

int noa_harness_modem_ncp_parse_rx_pkt(struct noa_harness_pkt *pkt, const void *data)
{
	const struct noa_desc *desc = (const struct noa_desc *)data;
	pkt->tkid = desc->tkid;
	pkt->should_drop = (desc->mode == NOAD_MODE_FEEDBACK);
	pkt->needs_nep_buffer_refill = (desc->reason == FWD_REASON_NETENGINE);
	pkt->size = desc->dl;
	return 0;
}

int noa_harness_modem_host_format_tx_data(void *data, const struct noa_harness_pkt *pkt)
{
	struct noa_desc *desc = (struct noa_desc *)data;

	desc->reason = FWD_REASON_FEEDTHROUGH;
	desc->mode = NOAD_MODE_DATA;
	desc->tkid = pkt->tkid;
	return 0;
}

int noa_harness_modem_host_parse_rx_pkt(struct noa_harness_pkt *pkt, const void *data)
{
	const struct fake_modem_vendor_msg_desc *msg =
		(const struct fake_modem_vendor_msg_desc *)data;
	const struct fake_modem_vendor_payload_desc *payload =
		(const struct fake_modem_vendor_payload_desc *)data;
	// If it is msg descriptor, we just skip it.
	if (msg->flag == MODEM_VENDOR_MSG_TYPE)
		return -EAGAIN;
	else if (payload->flag != MODEM_VENDOR_PAYLOAD_TYPE)
		return -EINVAL;
	pkt->tkid = payload->tkid;
	pkt->should_drop = false;
	return 0;
}
