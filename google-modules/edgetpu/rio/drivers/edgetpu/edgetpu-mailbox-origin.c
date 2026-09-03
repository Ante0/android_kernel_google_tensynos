// SPDX-License-Identifier: GPL-2.0-only
/*
 * Utility functions of mailbox protocol for Edge TPU ML accelerator.
 *
 * Copyright (C) 2019-2026 Google LLC
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "edgetpu-mailbox.h"
#include "edgetpu-telemetry.h"
#include "edgetpu.h"

int edgetpu_mailbox_set_queue(struct edgetpu_mailbox *mailbox, enum gcip_mailbox_queue_type type,
			      u64 addr, u32 size)
{
	u32 low = addr & 0xffffffff;
	u32 high = addr >> 32;

	if (!gcip_valid_circ_queue_size(size, CIRC_QUEUE_WRAP_BIT))
		return -EINVAL;
	/* addr is a 36-bit address, checks if the higher bits are clear */
	if (high & 0xfffffff0)
		return -EINVAL;

	switch (type) {
	case GCIP_MAILBOX_CMD_QUEUE:
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_address_low,
					      low);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_address_high,
					      high);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_size, size);
		mailbox->cmd_queue_size = size;
		edgetpu_mailbox_set_cmd_queue_tail(mailbox, 0);
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE(mailbox, head, 0);
		break;
	case GCIP_MAILBOX_RESP_QUEUE:
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_address_low,
					      low);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_address_high,
					      high);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_size, size);
		mailbox->resp_queue_size = size;
		edgetpu_mailbox_set_resp_queue_head(mailbox, 0);
		EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, tail, 0);
		break;
	}

	return 0;
}

void edgetpu_mailbox_set_queue_as_unused(struct edgetpu_mailbox *mailbox,
					 enum gcip_mailbox_queue_type type)
{
	switch (type) {
	case GCIP_MAILBOX_CMD_QUEUE:
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_address_low, 0);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_address_high, 0);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_size, 0);
		mailbox->cmd_queue_size = 0;
		edgetpu_mailbox_set_cmd_queue_tail(mailbox, 0);
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE(mailbox, head, 0);
		break;
	case GCIP_MAILBOX_RESP_QUEUE:
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_address_low, 0);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_address_high, 0);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_size, 0);
		mailbox->resp_queue_size = 0;
		edgetpu_mailbox_set_resp_queue_head(mailbox, 0);
		EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, tail, 0);
		break;
	}
}

void edgetpu_mailbox_reset(struct edgetpu_mailbox *mailbox)
{
	edgetpu_mailbox_disable(mailbox);
	EDGETPU_MAILBOX_CMD_QUEUE_WRITE(mailbox, head, 0);
	edgetpu_mailbox_set_cmd_queue_tail(mailbox, 0);
	edgetpu_mailbox_set_resp_queue_head(mailbox, 0);
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, tail, 0);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, config_spare_0, 0);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, config_spare_1, 0);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, config_spare_2, 0);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, config_spare_3, 0);
	edgetpu_mailbox_enable(mailbox);
}

/* Helper function to request a mailbox's doorbell interrupt and initialize mailbox->irq. */
static int edgetpu_mailbox_request_irq(struct edgetpu_mailbox *mailbox, int irq)
{
	struct edgetpu_dev *etdev = mailbox->etdev;
	int ret;

	/* Physical interrupts are not supported in the test environment. */
	if (IS_ENABLED(CONFIG_EDGETPU_TEST) && !mailbox->msi_enabled)
		return 0;

	/* irq == 0 implies this mailbox does not route interrupts to the AP. */
	if (!irq)
		return 0;

	ret = devm_request_irq(etdev->dev, irq, edgetpu_mailbox_irq_handler, 0, etdev->dev_name,
			       mailbox);
	if (!ret)
		mailbox->irq = irq;

	return ret;
}

struct edgetpu_mailbox *edgetpu_mailbox_alloc(struct edgetpu_dev *etdev, void __iomem *csr_base,
					      int irq, uint index, bool msi_enabled)
{
	/*
	 * TODO(b/376971597) switch from GFP_ATOMIC to GFP_KERNEL once this is no longer called
	 *                   while holding a lock.
	 */
	struct edgetpu_mailbox *mailbox = kzalloc(sizeof(*mailbox), GFP_ATOMIC);
	int ret;

	if (!mailbox)
		return ERR_PTR(-ENOMEM);
	mailbox->mailbox_id = index;
	mailbox->etdev = etdev;
	mailbox->csr_base = csr_base;
	mailbox->msi_enabled = msi_enabled;
	ret = edgetpu_mailbox_request_irq(mailbox, irq);
	if (ret) {
		etdev_err(etdev, "failed to request irq %d for mailbox %u: %d\n", mailbox->irq,
			  index, ret);
		goto err;
	}
	edgetpu_mailbox_init_doorbells(mailbox);

	return mailbox;
err:
	kfree(mailbox);
	return ERR_PTR(ret);
}

void edgetpu_mailbox_release(struct edgetpu_mailbox *mailbox)
{
	if (mailbox->irq)
		devm_free_irq(mailbox->etdev->dev, mailbox->irq, mailbox);
	kfree(mailbox);
}

void edgetpu_mailbox_clear_doorbells(struct edgetpu_mailbox *mailbox)
{
	/* Clear any stale doorbells requested */
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, doorbell_clear, 1);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_doorbell_clear, 1);
}

void edgetpu_mailbox_disable_doorbells(struct edgetpu_mailbox *mailbox)
{
	/* Disable the command and response doorbell interrupts */
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_doorbell_enable, 0);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_doorbell_enable, 0);
}

void edgetpu_mailbox_enable_doorbells(struct edgetpu_mailbox *mailbox)
{
	/* Enable the command and response doorbell interrupts */
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_doorbell_enable, 1);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_doorbell_enable, 1);
}

void edgetpu_mailbox_init_doorbells(struct edgetpu_mailbox *mailbox)
{
	edgetpu_mailbox_clear_doorbells(mailbox);
	edgetpu_mailbox_enable_doorbells(mailbox);
}

/* Handle a mailbox response doorbell interrupt. */
irqreturn_t edgetpu_mailbox_irq_handler(int irq, void *arg)
{
	struct edgetpu_mailbox *mailbox = arg;

	edgetpu_telemetry_irq_handler(mailbox->etdev);

	/*
	 * If the device is no longer held powered on then we probably just handled a telemetry
	 * event above for FW logs sent during or after a timed out power off handshake.  The
	 * powered-off device should not continue to assert a mailbox response IRQ.
	 *
	 * Else ensure device remains powered while we access its CSRs and handle responses.
	 */

	if (pm_runtime_get_if_active(mailbox->etdev->dev) <= 0)
		return IRQ_HANDLED;

	if (!EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, doorbell_status))
		goto out;

	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, doorbell_clear, 1);
	etdev_dbg(mailbox->etdev, "mbox %u resp doorbell irq tail=%u\n", mailbox->mailbox_id,
		  EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, tail));

	if (mailbox->handle_irq)
		mailbox->handle_irq(mailbox);

out:
	pm_runtime_put(mailbox->etdev->dev);
	return IRQ_HANDLED;
}

void edgetpu_mailbox_irq_enable(struct edgetpu_mailbox *mailbox, bool enable)
{
	if (enable)
		enable_irq(mailbox->irq);
	else
		disable_irq(mailbox->irq);
}

void edgetpu_mailbox_set_irq_handler(struct edgetpu_mailbox *mailbox,
				     void (*handle_irq)(struct edgetpu_mailbox *mailbox))
{
	/* Interrupts must be paused when updating the handler to avoid races. */
	if (mailbox->irq)
		disable_irq(mailbox->irq);

	mailbox->handle_irq = handle_irq;

	if (mailbox->irq)
		enable_irq(mailbox->irq);
}

void edgetpu_mailbox_dump(struct edgetpu_mailbox *mailbox)
{
	/* Ensure the TPU block is powered. */
	if (pm_runtime_get_if_active(mailbox->etdev->dev) <= 0)
		return;

	etdev_info(mailbox->etdev, "mailbox id %u cmd head=%#x tail=%#x doorbell_status=%u",
		   mailbox->mailbox_id,
		   EDGETPU_MAILBOX_CMD_QUEUE_READ(mailbox, head),
		   EDGETPU_MAILBOX_CMD_QUEUE_READ(mailbox, tail),
		   EDGETPU_MAILBOX_CMD_QUEUE_READ(mailbox, doorbell_status));
	etdev_info(mailbox->etdev, "  resp head=%#x tail=%#x doorbell_status=%u\n",
		   EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, head),
		   EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, tail),
		   EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, doorbell_status));
	pm_runtime_put(mailbox->etdev->dev);
}
