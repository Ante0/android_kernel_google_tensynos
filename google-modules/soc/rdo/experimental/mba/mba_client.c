// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interrupt.h>

#include "mba_internal.h"

static int client_ring(struct google_mba_client *client, bool enable_autoack)
{
	struct google_mba *mba = client_to_google_mba(client);
	int idx = client->idx;
	u32 irq_config;

	irq_config = google_mba_readl(mba, MBA_CLIENT_IRQ_CONFIG(idx));
	irq_config |= MBA_CLIENT_IRQ_CONFIG_SET_HOST_IRQ;
	if (enable_autoack)
		irq_config |= MBA_CLIENT_IRQ_CONFIG_ENABLE_HOST_AUTOACK;
	google_mba_writel(mba, irq_config, MBA_CLIENT_IRQ_CONFIG(idx));
	return 0;
}

static irqreturn_t client_isr(int irq, void *data)
{
	struct google_mba_client *client = data;
	struct google_mba *mba = client_to_google_mba(client);
	int idx = client->idx;
	u32 content;

	content = google_mba_readl(mba, MBA_CLIENT_IRQ_STATUS(idx));
	google_mba_writel(mba, content, MBA_CLIENT_IRQ_STATUS(idx));

	spin_lock(&client->irq_state.lock);
	client->irq_state.content = content;
	spin_unlock(&client->irq_state.lock);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t client_isr_threaded(int irq, void *data)
{
	struct google_mba_client *client = data;
	struct google_mba *mba = client_to_google_mba(client);
	struct device *dev = mba->dev;
	unsigned long flags;
	u32 content;

	spin_lock_irqsave(&client->irq_state.lock, flags);
	content = client->irq_state.content;
	spin_unlock_irqrestore(&client->irq_state.lock, flags);

	dev_warn(dev, "%s bh: isr=0x%x\n", client->name, content);
	return IRQ_HANDLED;
}

static int client_init_irq(struct google_mba_client *client)
{
	struct google_mba *mba = client_to_google_mba(client);
	struct device *dev = mba->dev;
	int idx = client->idx;
	u32 irq_config;
	int err;

	spin_lock_init(&client->irq_state.lock);

	err = devm_request_threaded_irq(dev, client->irq, client_isr,
					client_isr_threaded, 0, dev_name(dev),
					client);
	if (err < 0) {
		dev_err(dev, "client %d: failed to request irq %d", idx,
			client->irq);
		return err;
	}

	irq_config = MBA_CLIENT_IRQ_CONFIG_MASK_MSG_INT |
		     MBA_CLIENT_IRQ_CONFIG_MASK_ACK_INT;
	google_mba_writel(mba, irq_config, MBA_CLIENT_IRQ_CONFIG(idx));
	return 0;
}

static int client_debug_ring_to_host_write(void *data, u64 val)
{
	struct google_mba_client *client = data;

	return client_ring(client, val);
}

DEFINE_DEBUGFS_ATTRIBUTE(client_debug_ring_to_host_fops, NULL,
			 client_debug_ring_to_host_write, "%llu\n");

static void client_init_debugfs(struct google_mba_client *client)
{
	struct google_mba *mba = client_to_google_mba(client);
	struct device *dev = mba->dev;

	client->debugfs = debugfs_create_dir(client->name, mba->debugfs);
	if (IS_ERR(client->debugfs)) {
		dev_warn(dev, "%s: failed to create debugfs\n", client->name);
		return;
	}

	debugfs_create_file("ring_to_host", 0200, client->debugfs, client,
			    &client_debug_ring_to_host_fops);
}

int google_mba_client_init(struct google_mba_client *client)
{
	struct google_mba *mba = client_to_google_mba(client);
	struct device *dev = mba->dev;
	int idx = client->idx;
	int err;

	client->name = devm_kasprintf(dev, GFP_KERNEL, "client%d", idx);
	if (!client->name)
		return -ENOMEM;

	err = client_init_irq(client);
	if (err < 0)
		return err;

	client_init_debugfs(client);
	return 0;
}
