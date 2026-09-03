// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2024 Google LLC
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <soc/google/goog_mba_q_xport.h>
#include <soc/google/goog_mba_cpm_iface.h>
#include <mailbox/protocols/mba/cpm/ap_ns_mailbox/service_ids.h>

#include <ip-idle-notifier/google_ip_idle_notifier.h>
#include <mailbox/protocols/mba/cpm/ap_ns_mailbox/ssram_service/ssram_service.h>

#define MBA_CLIENT_TX_TOUT 3000
#define MBA_REQUEST_TIMEOUT 3000

#define IP_INDEX_MIN 0
#define IP_NUM 32

#define SLOT_SIZE 0x4

struct google_ip_idle_notifier {
	struct device *dev;
	u32 shared_memory_addr;
	u32 shared_memory_size;

	struct cpm_iface_client *cpm_client;

	u32 remote_ch;
};

static inline bool index_is_valid(int index)
{
	return (index >= IP_INDEX_MIN && index < IP_NUM);
}

static inline bool state_is_valid(enum ip_idle_state idle)
{
	return (idle == STATE_BUSY || idle == STATE_IDLE);
}

static void __iomem *shared_memory_base;

static inline void __iomem *get_ip_slot(int index)
{
	return shared_memory_base + index * SLOT_SIZE;
}

static inline void ip_idle_writel(int val, int index)
{
	writel(val, get_ip_slot(index));
}

static DEFINE_SPINLOCK(idle_ip_lock);
static bool ip_states[IP_NUM];
/**
 * @index: index of idle-ip
 * @idle: idle status, (idle == 0)busy or (idle == 1)idle
 */
int google_update_ip_idle_status(int index, enum ip_idle_state idle)
{
	unsigned long flags;

	if (!index_is_valid(index)) {
		pr_err("Invalid ip index %d, the valid range is [%d, %d]\n",
		       index, IP_INDEX_MIN, IP_NUM - 1);
		return -EINVAL;
	}

	if (!state_is_valid(idle)) {
		pr_err("Invalid state %d, the valid state is either %d or %d\n",
		       idle, STATE_BUSY, STATE_IDLE);
		return -EINVAL;
	}

	if (ip_states[index] == idle)
		return 0;

	spin_lock_irqsave(&idle_ip_lock, flags);
	ip_states[index] = idle;
	ip_idle_writel(idle, index);
	spin_unlock_irqrestore(&idle_ip_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(google_update_ip_idle_status);

static void get_shared_memory_addr(struct google_ip_idle_notifier *ip_idle_notifier,
				   u32 addr, u32 size)
{
	ip_idle_notifier->shared_memory_addr = addr;
	ip_idle_notifier->shared_memory_size = size;

	shared_memory_base = devm_ioremap(ip_idle_notifier->dev,
					  ip_idle_notifier->shared_memory_addr,
					  ip_idle_notifier->shared_memory_size);
}

static void google_ip_idle_notifier_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct google_ip_idle_notifier *ip_idle_notifier = priv_data;
	struct device *dev = ip_idle_notifier->dev;
	struct cpm_iface_payload *resp_msg = msg;

	dev_dbg(dev, "rx callback type: %#x, msg: %#x\n",
		goog_mba_q_xport_get_type(msg), resp_msg->payload[0]);
}

static int google_ip_idle_notifier_request_mailbox(struct device *dev,
						   struct google_ip_idle_notifier *ip_idle_notifier)
{
	struct cpm_iface_client *cpm_client;

	cpm_client = cpm_iface_request_client(dev, CPM_AP_NS_SSRAM_SERVICE,
					google_ip_idle_notifier_rx_callback, ip_idle_notifier);
	if (IS_ERR(cpm_client))
		return PTR_ERR(cpm_client);

	ip_idle_notifier->cpm_client = cpm_client;

	return 0;
}

static int mailbox_request_shared_memory_addr(struct google_ip_idle_notifier *ip_idle_notifier)
{
	int ret;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req client_req = {
		.msg_type = REQUEST_MSG,
		.req_msg = &req_msg,
		.resp_msg = &resp_msg,
		.dst_id = ip_idle_notifier->remote_ch,
		.tout_ms = MBA_CLIENT_TX_TOUT,
	};

	req_msg.payload[0] = SSRAM_QUERY_IP_IDLE_ADDR;
	req_msg.payload[1] = 0;
	req_msg.payload[2] = 0;

	ret = cpm_send_message(ip_idle_notifier->cpm_client, &client_req);
	if (ret < 0) {
		dev_err(ip_idle_notifier->dev, "Send message failed ret (%d)\n", ret);
		return ret;
	}

	get_shared_memory_addr(ip_idle_notifier, resp_msg.payload[1], resp_msg.payload[2]);

	return 0;
}

static inline void google_ip_idle_notifier_release_mailbox(
					struct google_ip_idle_notifier *ip_idle_notifier)
{
	if (ip_idle_notifier->cpm_client)
		cpm_iface_free_client(ip_idle_notifier->cpm_client);
}

static void google_ip_idle_notifier_remove(struct platform_device *pdev)
{
	struct google_ip_idle_notifier *ip_idle_notifier = platform_get_drvdata(pdev);

	google_ip_idle_notifier_release_mailbox(ip_idle_notifier);
}

static int google_ip_idle_notifier_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_ip_idle_notifier *ip_idle_notifier;
	struct device_node *np = dev->of_node;
	int ret = 0;

	ip_idle_notifier = devm_kzalloc(dev, sizeof(*ip_idle_notifier), GFP_KERNEL);
	if (!ip_idle_notifier)
		return -ENOMEM;

	ip_idle_notifier->dev = dev;
	platform_set_drvdata(pdev, ip_idle_notifier);

	ret = google_ip_idle_notifier_request_mailbox(dev, ip_idle_notifier);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "CPM interface is not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request mailbox channel err %d.\n", ret);
		goto probe_exit;
	}

	ret = of_property_read_u32(np, "mba-dest-channel", &ip_idle_notifier->remote_ch);
	if (ret) {
		dev_err(ip_idle_notifier->dev, "Failed to read mba-dest-channel.\n");
		goto probe_exit;
	}

	ret = mailbox_request_shared_memory_addr(ip_idle_notifier);
	return ret;

probe_exit:
	google_ip_idle_notifier_remove(pdev);
	return ret;
}

static const struct of_device_id google_ip_idle_notifier_of_match_table[] = {
	{ .compatible = "google,ip-idle-notifier", },
	{},
};
MODULE_DEVICE_TABLE(of, google_ip_idle_notifier_of_match_table);

static struct platform_driver google_ip_idle_notifier_driver = {
	.driver = {
		.name = "google-ip-idle-notifier",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_ip_idle_notifier_of_match_table),
	},
	.probe  = google_ip_idle_notifier_probe,
	.remove = google_ip_idle_notifier_remove,
};

module_platform_driver(google_ip_idle_notifier_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google ip-idle-notifier driver");
MODULE_LICENSE("GPL");
