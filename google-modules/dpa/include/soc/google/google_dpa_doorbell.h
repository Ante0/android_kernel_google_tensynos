/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interim doorbell driver for DPA. This API is *not* stable.

 * Long term, some of the doorbell functionality may migrate to the Kernel irq APIs (i.e
 * platform_get_irq, request_irq, and so on). In the meantime, this API provides a similar feature
 * set.
 *
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_DOORBELL_H
#define _GOOGLE_DPA_DOORBELL_H

struct google_dpa_doorbell;
extern struct platform_driver google_dpa_doorbell_driver;

/*
 * Get a handle to the doorbell with the requested index.
 *
 * Returns:
 *  pointer on success
 *  PTR encoded -ENODEV
 *  Other PTR encoded code if the doorbell with requested index isn't found
 */
struct google_dpa_doorbell *google_dpa_get_doorbell_by_index(struct device *dev, int index);

/*
 * Get a handle to the doorbell with the requested ID.
 *
 * Returns:
 *  pointer on success
 *  PTR encoded -ENODEV
 *  Other PTR encoded code if the doorbell with requested ID isn't found
 */
struct google_dpa_doorbell *google_dpa_get_doorbell(struct device *dev, const char *id);

/**
 * Get the dpa_doorbel's struct device handle.
 *
 * @doorbell: Google dpa doorbell handle.
 *
 * This is an API to allow client drivers to call runtime PM APIs on doorbell.
 * Some client drivers wants to call the doorbell APIs like
 * google_dpa_doorbell_ring_mcu() in the atomic context. In such cases, the
 * client driver can call pm_runtime_get_sync() on the doorbell device in the
 * process context to resume the doorbell device and safely use the doorbell
 * APIs.
 */
struct device *google_dpa_get_doorbell_dev(struct google_dpa_doorbell *doorbell);

/*
 * Ring one of 32 individual doorbells on the MCU, as identified by id
 * This will interrupt the remote MCU. Callers are responsible for ensuring
 * that any writes to shared data that the remote will process are completed
 * and viewable by the remote. This requires a dma_wmb for coherent allocations
 * or a dma_sync_for* for streaming allocations.
 */
void google_dpa_doorbell_ring_mcu(struct google_dpa_doorbell *doorbell, u8 id);

/*
 * Atomic safe variant of google_dpa_doorbell_ring_mcu().
 * google_dpa_doorbell_ring_mcu() might sleep because it calls
 * pm_runtime_get_sync() in it and it is not allowed to call it in the atomic
 * context. This function can be called in the atomic context but the caller is
 * responsible for resuming the doorbell device by calling pm_ruitime_get() in
 * the process context.
 */
int google_dpa_doorbell_ring_mcu_atomic_safe(struct google_dpa_doorbell *doorbell, u8 id);

/* Mask the given doorbell id, preventing it from contributing to the doorbell interrupt */
void google_dpa_doorbell_mask(struct google_dpa_doorbell *doorbell, u8 id);

/* Unmask the given doorbell id, allowing it to contribute to the doorbell interrupt again */
void google_dpa_doorbell_unmask(struct google_dpa_doorbell *doorbell, u8 id);

typedef void (*doorbell_cb_t)(void *);

/* google_dpa_doorbell_enable_doorbell - enable handling of incoming doorbell id
 * @doorbell: Handle to the doorbell to register on.
 * @id: Which doorbell id to enable ([0:31]) If this id was already enabled,
 *      returns -EINVAL and no action is taken
 * @callback: Callback to be executed when the doorbell bit is triggered
 * @data: Optional cookie that is passed to the calback function
 *
 * The callback is executed in interrupt context. You must mask the doorbell and schedule work
 * yourself if you need to process it in task context.
 * Note that the doorbell bit is unmasked in this call, and the callback may therefore be
 * triggered before it returns.
 */
int google_dpa_doorbell_enable_doorbell(struct google_dpa_doorbell *doorbell, u8 id,
					doorbell_cb_t callback, void *data);

/* Disable and remove callback for doorbell bit that was previously enabled */
void google_dpa_doorbell_disable_doorbell(struct google_dpa_doorbell *doorbell, u8 id);

#endif /* _GOOGLE_DPA_DOORBELL_H */
