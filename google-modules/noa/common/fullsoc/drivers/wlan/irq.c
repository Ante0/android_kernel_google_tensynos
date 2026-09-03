// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM WLAN IRQ Handler
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file implements the interrupt and polling handling for the WLAN driver.
 * It provides functions for registering and handling interrupts, as well as
 * a polling mechanism for retrieving data from the WLAN device.
 */

#include <linux/io.h>
#include <core/print.h>
#include <dal/wlan/api.h>
#include "irq.h"
#include "topbank.h"

/**
 * wlan_irq_imode_enable - Enable WLAN interrupt mode
 * @data: Pointer to the WLAN data structure
 *
 * This function enables interrupt mode for the WLAN device.
 */
static void wlan_irq_imode_enable(struct wlan_data *data)
{
	if (!data)
		return;

	enable_irq(data->irq.id);
}

/**
 * wlan_irq_imode_disable - Disable WLAN interrupt mode
 * @data: Pointer to the WLAN data structure
 *
 * This function disables interrupt mode for the WLAN device.
 */
static void wlan_irq_imode_disable(struct wlan_data *data)
{
	if (!data)
		return;

	disable_irq(data->irq.id);
}

/**
 * wlan_irq_pmode_enable - Enable WLAN polling mode
 * @data: Pointer to the WLAN data structure
 *
 * This function enables polling mode for the WLAN device.
 */
static void wlan_irq_pmode_enable(struct wlan_data *data)
{
	if (!data)
		return;

	queue_delayed_work(data->wq, &data->irq.poll_work,
			   msecs_to_jiffies(data->irq.poll_interval));
}

/**
 * wlan_irq_pmode_disable - Disable WLAN polling mode
 * @data: Pointer to the WLAN data structure
 *
 * This function disables polling mode for the WLAN device.
 */
static void wlan_irq_pmode_disable(struct wlan_data *data)
{
	if (!data)
		return;

	cancel_delayed_work(&data->irq.poll_work);
}

/**
 * lvm_wlan_irq_status_sync - Synchronize WLAN interrupt status
 * @data: Pointer to the WLAN data structure
 *
 * This function synchronizes the software interrupt status with the
 * hardware interrupt status register.  It reads the interrupt status
 * from the WLAN topbank register and updates the driver's internal
 * interrupt status.
 *
 * Return: True if there are pending interrupts, false otherwise.
 */
static bool lvm_wlan_irq_status_sync(struct wlan_data *data)
{
	u32 status = 0;

	status = lvm_wlan_topbank_reg_read(data->topbank,
					   WLAN_TOP_REG_OUT_RING_ISR);
	lvm_wlan_topbank_reg_write(data->topbank,
				   WLAN_TOP_REG_OUT_RING_ISR, status);

	data->irq.rx_status |= status;

	return !!data->irq.rx_status;
}

/**
 * wlan_irq_pmode_cb - Callback function for polling mode deferred work
 * @work: Pointer to the work structure
 *
 * This function is called periodically by the work queue in polling mode
 * to check for WLAN events.  It synchronizes the interrupt status and
 * calls the interrupt service routine if interrupts are pending.
 * If no interrupts are pending, it reschedules itself.
 */
static void wlan_irq_pmode_cb(struct work_struct *work)
{
	struct wlan_irq *irq = container_of(work, struct wlan_irq,
					    poll_work.work);
	struct wlan_data *data = container_of(irq, struct wlan_data, irq);

	if (!irq || is_processing(irq))
		return;

	if (lvm_wlan_irq_status_sync(data))
		lvm_wlan_isr(irq->id, data);
	else
		queue_delayed_work(data->wq, &irq->poll_work,
				   msecs_to_jiffies(irq->poll_interval));
}

/**
 * lvm_wlan_rx_tasklet_cb - Tasklet callback function for RX processing
 * @t: Pointer to the tasklet structure
 *
 * This function is the callback for the RX tasklet. It is scheduled
 * by the ISR to handle received data and TX completions. It calls
 * platform_bus_rx() and platform_bus_tx_cpl() from the DAL to process
 * these events.
 */
static void lvm_wlan_rx_tasklet_cb(struct tasklet_struct *t)
{
	struct wlan_data *data = from_tasklet(data, t, irq.rx_tasklet);
	struct wlan_irq *irq;
	bool resched = false;
	u32 rx_cnt = 0;
	u32 txcpl_cnt = 0;

	if (!data)
		return;

	irq = &data->irq;

	resched |= platform_bus_rx(data, &rx_cnt);

	resched |= platform_bus_tx_cpl(data, &txcpl_cnt);

	resched |= is_bypass_mode(data) ? lvm_wlan_irq_status_sync(data) : false;

	if (resched)
		tasklet_schedule(&irq->rx_tasklet);
	else {
		if (is_bypass_mode(data))
			irq->enable(data);

		atomic_set(&irq->state, WLAN_IRQ_STATE_IDLE);
	}
}

/**
 * lvm_wlan_isr - WLAN interrupt service routine
 * @id: The interrupt ID
 * @priv: Pointer to the WLAN data structure
 *
 * This function is the interrupt service routine for the WLAN device.
 * It disables interrupts and synchronizes interrupt status (in bypass mode),
 * and schedules the RX tasklet to perform the actual data
 * processing.
 *
 * Return: IRQ_HANDLED if the interrupt was handled, IRQ_NONE otherwise.
 */
irqreturn_t lvm_wlan_isr(int id, void *priv)
{
	struct wlan_data *data = priv;
	struct wlan_irq *irq;

	if (!data)
		return IRQ_NONE;

	irq = &data->irq;

	if (is_processing(irq))
		return IRQ_NONE;

	atomic_set(&irq->state, WLAN_IRQ_STATE_BUSY);

	if (is_bypass_mode(data)) {
		irq->disable(data);
		lvm_wlan_irq_status_sync(data);
	}

	LVM_DBG("LVM wlan received interrupt (id=%d) from fake device\n", irq->id);

	tasklet_schedule(&irq->rx_tasklet);

	return IRQ_HANDLED;
}

/**
 * lvm_wlan_irq_request - Request WLAN interrupt
 * @data: Pointer to the WLAN data structure
 *
 * This function requests the IRQ for the WLAN device.  If the IRQ
 * request fails, it falls back to polling mode.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_wlan_irq_request(struct wlan_data *data)
{
	struct wlan_irq *irq;
	int ret = 0;

	if (!data)
		return -EINVAL;

	irq = &data->irq;
	ret = request_irq(irq->id, lvm_wlan_isr, IRQF_SHARED,
			  "lvm wlan", data);
	if (ret) {
		irq->mode = WLAN_IRQ_MODE_POLLING;
		irq->enable = wlan_irq_pmode_enable;
		irq->disable = wlan_irq_pmode_disable;
		INIT_DELAYED_WORK(&irq->poll_work, wlan_irq_pmode_cb);

		LVM_ERR("LVM wlan failed to request IRQ %d (err %d), falling back to polling mode\n",
			irq->id, ret);
	} else {
		irq->mode = WLAN_IRQ_MODE_INTERRUPT;
		irq->enable = wlan_irq_imode_enable;
		irq->disable = wlan_irq_imode_disable;

		LVM_INFO("LVM wlan successfully requested IRQ %d for interrupt mode\n",
			 irq->id);
	}

	atomic_set(&irq->state, WLAN_IRQ_STATE_IDLE);
	irq->enable(data);

	return 0;
}

/**
 * lvm_wlan_irq_release - Release WLAN interrupt
 * @data: Pointer to the WLAN data structure
 *
 * This function releases the IRQ or cancels the work queue, depending
 * on the current IRQ mode.
 */
void lvm_wlan_irq_release(struct wlan_data *data)
{
	struct wlan_irq *irq;

	if (!data)
		return;

	irq = &data->irq;
	irq->disable(data);

	if (is_interrupt_mode(irq))
		free_irq(irq->id, data);
	else if (is_polling_mode(irq))
		cancel_delayed_work_sync(&irq->poll_work);
}

/**
 * lvm_wlan_irq_init - Initialize WLAN interrupt handling
 * @data: Pointer to the WLAN data structure
 *
 * This function initializes interrupt handling by requesting an
 * interrupt and setting up the RX tasklet.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_wlan_irq_init(struct wlan_data *data)
{
	int ret = 0;

	if (!data)
		return -EINVAL;

	ret = platform_bus_request_irq(data);
	if (ret)
		return -EINVAL;

	tasklet_setup(&data->irq.rx_tasklet, lvm_wlan_rx_tasklet_cb);

	return 0;
}

/**
 * lvm_wlan_irq_deinit - Deinitialize WLAN interrupt handling
 * @data: Pointer to the WLAN data structure
 *
 * This function deinitializes interrupt handling by killing the RX
 * tasklet.
 */
void lvm_wlan_irq_deinit(struct wlan_data *data)
{
	if (!data)
		return;

	tasklet_kill(&data->irq.rx_tasklet);
}
