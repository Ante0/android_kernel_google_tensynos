// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP mailbox array driver implementation.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <asm/barrier.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

#include "gxp-config.h"
#include "gxp-mailbox-driver.h"
#include "gxp-mailbox.h"

#include "gxp-mailbox-driver.c"

void gxp_mailbox_reset_hw(struct gxp_mailbox *mailbox)
{
	//TODO(b/261670165): check if client flush is required.
}

void gxp_mailbox_chip_irq_handler(struct gxp_mailbox *mailbox)
{
	/* Clear pending IRQ */
	gxp_mailbox_clear_interrupts(mailbox, MBOX_CLIENT_STATUS_MSG_INT);
	gxp_mailbox_handle_irq(mailbox);
}

void gxp_mailbox_generate_device_interrupt(struct gxp_mailbox *mailbox, u32 int_mask)
{
	/*
	 * Ensure all memory writes have been committed to memory before
	 * signalling to the device to read from them. This avoids the scenario
	 * where the interrupt trigger write gets delivered to the MBX HW before
	 * the DRAM transactions made it to DRAM since they're Normal
	 * transactions and can be re-ordered and backed off behind other
	 * transfers.
	 */
	wmb();

	gxp_mailbox_csr_write(mailbox, MBOX_CLIENT_IRQ_TRIG, 0x1);
}

void gxp_mailbox_clear_interrupts(struct gxp_mailbox *mailbox, u32 intr_bits)
{
	/* Write 1 to clear */
	gxp_mailbox_csr_write(mailbox, MBOX_CLIENT_IRQ_STATUS, intr_bits);
}

void gxp_mailbox_enable_interrupt(struct gxp_mailbox *mailbox)
{
	gxp_mailbox_csr_write(mailbox, MBOX_CLIENT_IRQ_CFG, MBOX_CLIENT_IRQ_MSG_INT);
}

int gxp_mailbox_wait_for_device_mailbox_init(struct gxp_mailbox *mailbox)
{
	u32 ctr = 1000000;

	if (IS_GXP_TEST)
		return 0;

	while (ctr &&
	       !(gxp_mailbox_csr_read(mailbox, MBOX_CLIENT_SHDW) & MBOX_CLIENT_SHDW_HOST_EN)) {
		udelay(1 * GXP_TIME_DELAY_FACTOR);
		ctr--;
	}

	if (!ctr)
		return -ETIMEDOUT;
	return 0;
}
