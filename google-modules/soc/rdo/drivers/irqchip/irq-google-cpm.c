// SPDX-License-Identifier: GPL-2.0-only
//
#include <linux/device.h>
#include <linux/bits.h>
#include <linux/bitmap.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/interrupt.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <dt-bindings/interrupt-controller/irq-cpm-google.h>
#include <soc/google/goog_cpm_service_ids.h>

#include <soc/google/goog_mba_cpm_iface.h>

#define CPM_IRQ_REQ_IRQ_MASK GENMASK(7, 0)
#define CPM_IRQ_REQ_ARG_MASK GENMASK(23, 8)
#define CPM_IRQ_REQ_ACTION_MASK BIT(24)
#define CPM_IRQ_REQ_PARAM_MASK GENMASK(28, 25)

enum cpm_irq_action {
	CPM_IRQ_ACTION_GET,
	CPM_IRQ_ACTION_SET,
};

enum cpm_irq_param {
	CPM_IRQ_ENABLE,
	CPM_IRQ_MASK,
	CPM_IRQ_TYPE,
};

#define CPM_IRQ_RSP_RSV_MASK GENMASK(23, 0)
#define CPM_IRQ_RSP_STATUS_MASK GENMASK(31, 24)

enum cpm_irq_rsp_status {
	CPM_IRQ_RSP_STS_OK,
	CPM_IRQ_RSP_STS_INVALID_PARAMETER,
	CPM_IRQ_RSP_STS_INVALID_ACTION,
	CPM_IRQ_RSP_STS_INVALID_IRQ,
	CPM_IRQ_RSP_STS_ERROR,
};

#define CPM_IRQ_MSG_IRQ_MASK GENMASK(7, 0)

struct cpm_irq_info {
	struct device		*dev;
	struct cpm_iface_client *client;

	u32			remote_id;

	struct irq_domain	*domain;
	struct mutex		lock;				/* irq chip lock */
	DECLARE_BITMAP(mask, CPM_IRQ_COUNT);			/* irq mask */
	DECLARE_BITMAP(mask_bits_updated, CPM_IRQ_COUNT);	/* pending mask updates */
	u8			trig_type[CPM_IRQ_COUNT];	/* 8 bits per irq */
	DECLARE_BITMAP(trig_type_fields_updated,
		       CPM_IRQ_COUNT);				/* pending trig_type updates */
};

#define TRIG_TYPE_TYPE typeof(((struct cpm_irq_info *)0)->trig_type[0])
#define TRIG_TYPE_MASK GENMASK((sizeof(TRIG_TYPE_TYPE) * BITS_PER_BYTE) - 1, 0)
static_assert((IRQF_TRIGGER_MASK & TRIG_TYPE_MASK) == IRQF_TRIGGER_MASK,
	      "trig_type size is too small to represent all bits of IRQF_TRIGGER_MASK");
#define MBA_CLIENT_TX_TOUT	3000 /* in ms */

static void cpm_irq_process_irq_status(struct cpm_irq_info *info, u32 irq_number)
{
	int ret;

	ret = generic_handle_domain_irq_safe(info->domain, irq_number);
	if (ret)
		dev_err(info->dev, "Error handling IRQ %u, error code %d\n", irq_number, ret);
}

static void cpm_irq_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct cpm_irq_info *info = priv_data;
	struct cpm_iface_payload *cpm_msg = msg;
	u32 irq_number = cpm_msg->payload[0];

	cpm_irq_process_irq_status(info, irq_number);
}

static void cpm_irq_mask(struct irq_data *d)
{
	struct cpm_irq_info *info = irq_data_get_irq_chip_data(d);

	set_bit(d->hwirq, info->mask);
	set_bit(d->hwirq, info->mask_bits_updated);
}

static void cpm_irq_unmask(struct irq_data *d)
{
	struct cpm_irq_info *info = irq_data_get_irq_chip_data(d);

	clear_bit(d->hwirq, info->mask);
	set_bit(d->hwirq, info->mask_bits_updated);
}

static int cpm_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct cpm_irq_info *info = irq_data_get_irq_chip_data(d);
	u32 prev_type = info->trig_type[d->hwirq] & IRQF_TRIGGER_MASK;

	if (prev_type == type)
		return 0;

	if (!FIELD_FIT(IRQF_TRIGGER_MASK, type))
		return -EINVAL;

	info->trig_type[d->hwirq] = type & IRQF_TRIGGER_MASK;
	set_bit(d->hwirq, info->trig_type_fields_updated);

	return 0;
}

static void cpm_bus_lock(struct irq_data *d)
{
	struct cpm_irq_info *info = irq_data_get_irq_chip_data(d);

	mutex_lock(&info->lock);
}

static int cpm_irq_send_pkg(struct cpm_irq_info *info, u32 req_data)
{
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	enum cpm_irq_rsp_status rsp_status;
	int ret;

	req_msg.payload[0] = req_data;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.dst_id = info->remote_id;
	cpm_req.tout_ms = MBA_CLIENT_TX_TOUT;

	ret = cpm_send_message(info->client, &cpm_req);
	if (ret < 0) {
		dev_err(info->dev, "Send write request failed (%d)\n", ret);
		return ret;
	}

	rsp_status = FIELD_GET(CPM_IRQ_RSP_STATUS_MASK, cpm_req.resp_msg->payload[0]);
	if (rsp_status != CPM_IRQ_RSP_STS_OK) {
		dev_err(info->dev, "response status (%d)\n", rsp_status);
		return -EIO;
	}

	return ret;
}

static void cpm_bus_sync_unlock(struct irq_data *d)
{
	struct cpm_irq_info *info = irq_data_get_irq_chip_data(d);
	u32 req_data;
	int ret = 0;

	if (test_bit(d->hwirq, info->mask_bits_updated)) {
		clear_bit(d->hwirq,
			  info->mask_bits_updated); /* clear update flag */

		req_data =
			FIELD_PREP(CPM_IRQ_REQ_ARG_MASK,
				   test_bit(d->hwirq, info->mask)) |
			FIELD_PREP(CPM_IRQ_REQ_IRQ_MASK, d->hwirq) |
			FIELD_PREP(CPM_IRQ_REQ_PARAM_MASK, CPM_IRQ_MASK) |
			FIELD_PREP(CPM_IRQ_REQ_ACTION_MASK, CPM_IRQ_ACTION_SET);
		ret = cpm_irq_send_pkg(info, req_data);
		if (ret) {
			mutex_unlock(&info->lock);
			return;
		}
	}

	if (test_bit(d->hwirq, info->trig_type_fields_updated)) {
		u32 trig_type = info->trig_type[d->hwirq] & IRQF_TRIGGER_MASK;

		clear_bit(d->hwirq,
			  info->trig_type_fields_updated); /* clear update */

		req_data = FIELD_PREP(CPM_IRQ_REQ_ARG_MASK, trig_type) |
			   FIELD_PREP(CPM_IRQ_REQ_IRQ_MASK, d->hwirq) |
			   FIELD_PREP(CPM_IRQ_REQ_PARAM_MASK, CPM_IRQ_TYPE) |
			   FIELD_PREP(CPM_IRQ_REQ_ACTION_MASK, CPM_IRQ_ACTION_SET);
		ret = cpm_irq_send_pkg(info, req_data);
	}

	mutex_unlock(&info->lock);
}

static struct irq_chip cpm_irq_chip = {
	.name = "cpm_irq",
	.irq_enable = cpm_irq_unmask,
	.irq_disable = cpm_irq_mask,
	.irq_mask = cpm_irq_mask,
	.irq_unmask = cpm_irq_unmask,
	.irq_set_type = cpm_set_irq_type,
	.irq_bus_lock = cpm_bus_lock,
	.irq_bus_sync_unlock = cpm_bus_sync_unlock,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static int cpm_irq_mailbox_init(struct platform_device *pdev)
{
	struct cpm_irq_info *info = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct cpm_iface_client *client;
	int ret;

	info->remote_id = CPM_AP_NS_IRQ_SERVICE;
	client = cpm_iface_request_client(dev, CPM_AP_NS_IRQ_SERVICE,
					  cpm_irq_rx_callback, info);
	if (IS_ERR(client)) {
		ret = PTR_ERR(client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request cpm client ret %d\n", ret);

		return ret;
	}

	return 0;
}

static void cpm_irq_mailbox_exit(struct platform_device *pdev)
{
	struct cpm_irq_info *info = platform_get_drvdata(pdev);

	cpm_iface_free_client(info->client);
}

static int cpm_irq_domain_map(struct irq_domain *d, unsigned int virq,
			     irq_hw_number_t hwirq)
{
	struct cpm_irq_info *info = d->host_data;

	if (hwirq >= CPM_IRQ_COUNT)
		return -EINVAL;

	irq_set_chip_data(virq, info);
	irq_set_chip_and_handler(virq, &cpm_irq_chip, handle_simple_irq);

	dev_dbg(info->dev, "Mapped hwirq %lu to virq %u\n", hwirq, virq);

	return 0;
}

static const struct irq_domain_ops cpm_irq_domain_ops = {
	.map = cpm_irq_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int cpm_irq_irqchip_init(struct platform_device *pdev)
{
	struct cpm_irq_info *info = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	mutex_init(&info->lock);

	memset(info->trig_type, 0, sizeof(info->trig_type));

	info->domain = irq_domain_create_tree(dev_fwnode(dev), &cpm_irq_domain_ops, info);
	if (!info->domain) {
		dev_err(info->dev, "Unable to add irq domain\n");
		return -ENODEV;
	}

	return 0;
}

static int cpm_irq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpm_irq_info *info;
	int err = 0;

	if (!dev->of_node)
		return -ENODEV;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	platform_set_drvdata(pdev, info);

	err = cpm_irq_mailbox_init(pdev);
	if (err)
		return err;

	err = cpm_irq_irqchip_init(pdev);
	if (err)
		cpm_irq_mailbox_exit(pdev);

	return err;
}

static void cpm_irq_remove(struct platform_device *pdev)
{
	cpm_irq_mailbox_exit(pdev);
}

static const struct platform_device_id cpm_irq_id[] = {
	{ "google-cpm-irq", 0},
	{},
};

MODULE_DEVICE_TABLE(platform, cpm_irq_id);

static const struct of_device_id cpm_irq_of_match_table[] = {
	{ .compatible = "google,cpm-irq" },
	{},
};

struct platform_driver cpm_irq_driver = {
	.probe  = cpm_irq_probe,
	.remove = cpm_irq_remove,
	.driver = {
		.name = "google-cpm-irq-chip",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cpm_irq_of_match_table),
	},
};

module_platform_driver(cpm_irq_driver);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("Google CPM IRQ chip driver");
MODULE_AUTHOR("Jim Wylder<jwylder@google.com>");
