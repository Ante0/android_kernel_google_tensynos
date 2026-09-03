/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN IRQ Handler Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_IRQ_H__
#define __LVM_DRIVER_WLAN_IRQ_H__

struct wlan_data;

/**
 * enum wlan_irq_mode - WLAN interrupt operating modes.
 * @WLAN_IRQ_MODE_INTERRUPT: Interrupt-driven mode.
 * @WLAN_IRQ_MODE_POLLING: Polling mode.
 * @__WLAN_IRQ_MODE_MAX: Maximum number of modes.
 */
enum wlan_irq_mode {
	WLAN_IRQ_MODE_INTERRUPT,
	WLAN_IRQ_MODE_POLLING,

	__WLAN_IRQ_MODE_MAX,
};

/**
 * enum wlan_irq_state - WLAN interrupt processing states.
 * @WLAN_IRQ_STATE_IDLE: The interrupt handler is idle.
 * @WLAN_IRQ_STATE_BUSY: The interrupt handler is actively processing
 *			 an interrupt.
 * @__WLAN_IRQ_STATE_MAX: Maximum number of states.
 */
enum wlan_irq_state {
	WLAN_IRQ_STATE_IDLE,
	WLAN_IRQ_STATE_BUSY,

	__WLAN_IRQ_STATE_MAX,
};

/**
 * struct wlan_irq - WLAN interrupt related information
 * @id: Interrupt ID assigned to the WLAN device.
 * @mode: Current interrupt handling mode (interrupt or polling).
 * @state: Current state of the interrupt handler (idle or busy).
 * @rx_status: Stores the status of received interrupts.  Each bit represents a
 *             specific interrupt source.
 * @rx_tasklet: Tasklet scheduled to handle received interrupts and TX completions.
 * @poll_interval: Interval (in milliseconds) for polling the device in polling mode.
 * @poll_work: Delayed work structure used for scheduling polling operations.
 * @enable: Function pointer to enable the chosen interrupt handling mode.
 * @disable: Function pointer to disable the chosen interrupt handling mode.
 */
struct wlan_irq {
	u32				id;
	enum wlan_irq_mode		mode;
	atomic_t			state;

	u32				rx_status;
	struct tasklet_struct		rx_tasklet;

	u32				poll_interval;
	struct delayed_work		poll_work;

	void				(*enable)(struct wlan_data *data);
	void				(*disable)(struct wlan_data *data);
};

irqreturn_t lvm_wlan_isr(int irq, void *priv);
int lvm_wlan_irq_request(struct wlan_data *data);
void lvm_wlan_irq_release(struct wlan_data *data);
int lvm_wlan_irq_init(struct wlan_data *data);
void lvm_wlan_irq_deinit(struct wlan_data *data);

static inline bool is_interrupt_mode(struct wlan_irq *irq)
{
	return irq->mode == WLAN_IRQ_MODE_INTERRUPT;
}

static inline bool is_polling_mode(struct wlan_irq *irq)
{
	return irq->mode == WLAN_IRQ_MODE_POLLING;
}

static inline bool is_processing(struct wlan_irq *irq)
{
	return atomic_read(&irq->state) == WLAN_IRQ_STATE_BUSY;
}

#endif  /* __LVM_DRIVER_WLAN_IRQ_H__ */
