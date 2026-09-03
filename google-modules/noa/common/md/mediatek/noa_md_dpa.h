/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD DPA Driver
 *
 * Copyright 2025 Google LLC.
 */
#ifndef __NOA_MD_DPA_H__
#define __NOA_MD_DPA_H__

#include "common/md/mediatek/noa_md_dpa_doorbell.h"
#include "common/modem_ring_id.h"
#include "noa_md.h"
#include "soc/google/google_dpa_ctrl.h"  /* For enum dpa_state */

/* Forward declaration */
struct noa_md_dev;

/* The max id number for DPA ISR */
#define NOA_MD_DPA_ISR_ID_MAX NOA_MD_NCP2APC_DOORBELL_MAX

/* Typedef for the state change callback function */
typedef void (*noa_md_dpa_state_callback_t)(
	enum dpa_state state, void *context);

struct noa_md_dpa_state_client {
	struct list_head node;
	noa_md_dpa_state_callback_t callback;
	void *context;
};

/**
 * noa_md_dpa_isr_callback_t - Callback function prototype for registered ISRs.
 * @id:       The doorbell ID (ring_type) that triggered the interrupt.
 * @resource: User-provided private data for the callback.
 */
typedef void (*noa_md_dpa_isr_callback_t)(int id, void *resource);

/**
 * struct noa_md_dpa_isr_handler - Holds registration info for a single ISR.
 */
struct noa_md_dpa_isr_handler {
	noa_md_dpa_isr_callback_t callback;
	void *resource;
};

/**
 * struct noa_md_dpa - Manages all resources related to the DPA hardware.
 *
 * This structure holds pointers to the DPA device, registered clients,
 * doorbells, and tracks the hardware state and ISR handlers.
 */
struct noa_md_dpa {
	/* Core Device Pointers */
	struct device *dev;
	struct device *dpa_dev;  /* DPA device pointer */
	struct google_dpa *dpa;  /* Google DPA context */
	struct dpa_client *registered_dpa_client;  /* Registered DPA client */

	/* Hardware Resources */
	struct google_dpa_doorbell *ncp_doorbell;
	struct google_dpa_doorbell *nep_doorbell;

	/* State and Concurrency Control */
	enum dpa_state state;  /* Current DPA state */
	spinlock_t lock;  /* Lock to protect doorbell state and other shared data */

	/* Client and ISR Management */
	struct list_head state_client_list;  /* List head for state change client registrations */
	unsigned long enabled_ncp_doorbells;  /* Bitmap for enabled ncp doorbells status */
	unsigned long enabled_nep_doorbells;  /* Bitmap for enabled nep doorbells status */
	struct noa_md_dpa_isr_handler ncp_isr_handlers[NOA_MD_DPA_ISR_ID_MAX];
	struct noa_md_dpa_isr_handler nep_isr_handlers[NOA_MD_DPA_ISR_ID_MAX];
	struct noa_md_dpa_isr_data *isr_data;
};

/**
 * noa_md_dpa_get_dpa_context() - Retrieve the DPA context from the global
 * modem device.
 *
 * Return: A pointer to the noa_md_dpa structure.
 */
struct noa_md_dpa *noa_md_dpa_get_dpa_context(void);

/**
 * noa_md_dpa_register_state_client() - Register for DPA state change notifications.
 * @callback: The function to call when the DPA state changes.
 * @context:  A private context pointer to be passed back to the callback.
 *
 * This function allows other modules to subscribe to DPA state changes.
 *
 * Return: A pointer to a client handle on success (to be used for unregistering),
 * or NULL on failure.
 */
struct noa_md_dpa_state_client *noa_md_dpa_register_state_client(
	noa_md_dpa_state_callback_t callback, void *context);

/**
 * noa_md_dpa_unregister_state_client() - Unregister from DPA state change notifications.
 * @client: The client handle returned by the register function.
 */
void noa_md_dpa_unregister_state_client(struct noa_md_dpa_state_client *client);

/**
 * noa_md_dpa_register_isr() - Register a doorbell ISR callback.
 * @type:     The doorbell type (NCP or NEP).
 * @id:       The ISR ID (ring type) to register for.
 * @callback: The client's callback function to be invoked.
 * @resource: A private data pointer to be passed back to the callback.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_dpa_register_isr(
	enum noa_md_dpa_doorbell_type type, int id,
	noa_md_dpa_isr_callback_t callback, void *resource);

/**
 * noa_md_dpa_unregister_isr() - Unregister a doorbell ISR callback.
 * @type: The doorbell type (NCP or NEP).
 * @id:   The ISR ID (ring type) to unregister.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_dpa_unregister_isr(enum noa_md_dpa_doorbell_type type, int id);

/**
 * noa_md_dpa_notify_ncp() - Ring a doorbell to notify ncp by doorbell id.
 * @id: The doorbell id.
 *
 * This function is called to ring a doorbell to notify ncp by doorbell id.
 */
void noa_md_dpa_notify_ncp(int id);

// Initialize and release
int noa_md_dpa_init(struct noa_md_dev *p_noa_dev);
void noa_md_dpa_release(struct noa_md_dev *p_noa_dev);

#endif /* __NOA_MD_DPA_H__ */
