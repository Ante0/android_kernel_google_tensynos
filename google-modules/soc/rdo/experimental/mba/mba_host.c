// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interrupt.h>

#include "mba_internal.h"

static void host_enable(struct google_mba_host *host)
{
	struct google_mba *mba = host_to_google_mba(host);

	google_mba_writel(mba, 0x3F, MBA_HOST_ENABLE);
}

static int host_ring(struct google_mba_host *host, bool enable_autoack,
		     unsigned int client_id)
{
	struct google_mba *mba = host_to_google_mba(host);
	u32 trig_val;

	if (client_id >= MBA_NUM_CLIENT)
		return -EINVAL;

	trig_val = MBA_HOST_IRQ_TRIG_SET_CLIENT_IRQ;
	if (enable_autoack)
		trig_val |= MBA_HOST_IRQ_TRIG_ENABLE_CLIENT_AUTOACK;
	google_mba_writel(mba, trig_val, MBA_HOST_IRQ_TRIG(client_id));
	return 0;
}

static irqreturn_t host_isr(int irq, void *data)
{
	struct google_mba_host *host = data;
	struct google_mba *mba = host_to_google_mba(host);
	struct google_mba_host_raw_irq_state content;

	content.msg = google_mba_readl(mba, MBA_HOST_IRQ_STATUS_MSG);
	google_mba_writel(mba, content.msg, MBA_HOST_IRQ_STATUS_MSG);

	content.ack = google_mba_readl(mba, MBA_HOST_IRQ_STATUS_ACK);
	google_mba_writel(mba, content.ack, MBA_HOST_IRQ_STATUS_ACK);

	spin_lock(&host->irq_state.lock);
	host->irq_state.content = content;
	spin_unlock(&host->irq_state.lock);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t host_isr_threaded(int irq, void *data)
{
	struct google_mba_host *host = data;
	struct google_mba *mba = host_to_google_mba(host);
	struct device *dev = mba->dev;
	unsigned long flags;
	struct google_mba_host_raw_irq_state content;

	spin_lock_irqsave(&host->irq_state.lock, flags);
	content = host->irq_state.content;
	spin_unlock_irqrestore(&host->irq_state.lock, flags);

	dev_warn(dev, "host bh: msg=0x%x, ack=0x%x\n", content.msg,
		 content.ack);
	return IRQ_HANDLED;
}

static int host_init_irq(struct google_mba_host *host)
{
	struct google_mba *mba = host_to_google_mba(host);
	struct device *dev = mba->dev;
	int err;

	spin_lock_init(&host->irq_state.lock);

	err = devm_request_threaded_irq(dev, host->irq, host_isr,
					host_isr_threaded, 0, dev_name(dev),
					host);
	if (err < 0) {
		dev_err(dev, "host: failed to request irq %d", host->irq);
		return err;
	}

	google_mba_writel(mba, 0x3F, MBA_HOST_IRQ_MASK_MSG);
	google_mba_writel(mba, 0x3F, MBA_HOST_IRQ_MASK_ACK);
	return 0;
}

static int host_debug_enable_read(void *data, u64 *val)
{
	struct google_mba_host *host = data;
	struct google_mba *mba = host_to_google_mba(host);

	*val = google_mba_readl(mba, MBA_HOST_ENABLE);
	return 0;
}

static int host_debug_enable_write(void *data, u64 val)
{
	struct google_mba_host *host = data;
	struct google_mba *mba = host_to_google_mba(host);

	if (val > U32_MAX)
		return -EINVAL;
	google_mba_writel(mba, val, MBA_HOST_ENABLE);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(host_debug_enable_fops, host_debug_enable_read,
			 host_debug_enable_write, "0x%llx\n");

static int host_debug_ring_to_client_write(void *data, u64 val)
{
	struct google_mba_host *host = data;

	if (val >= MBA_NUM_CLIENT)
		return -EINVAL;
	return host_ring(host, val, host->debug_client);
}

DEFINE_DEBUGFS_ATTRIBUTE(host_debug_ring_to_client_fops, NULL,
			 host_debug_ring_to_client_write, "%llu\n");

static void host_init_debugfs(struct google_mba_host *host)
{
	struct google_mba *mba = host_to_google_mba(host);
	struct device *dev = mba->dev;

	host->debugfs = debugfs_create_dir("host", mba->debugfs);
	if (IS_ERR(host->debugfs)) {
		dev_warn(dev, "host: failed to create debugfs\n");
		return;
	}

	debugfs_create_file("enable", 0600, host->debugfs, host,
			    &host_debug_enable_fops);
	debugfs_create_u32("client", 0600, host->debugfs, &host->debug_client);
	debugfs_create_file("ring_to_client", 0200, host->debugfs, host,
			    &host_debug_ring_to_client_fops);
}

int google_mba_host_init(struct google_mba_host *host)
{
	int err;

	err = host_init_irq(host);
	if (err < 0)
		return err;
	host_enable(host);
	host_init_debugfs(host);
	return 0;
}
