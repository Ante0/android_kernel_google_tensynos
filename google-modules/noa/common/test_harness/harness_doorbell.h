#ifndef __HARNESS_DOORBELL_H__
#define __HARNESS_DOORBELL_H__

#include "noa_test_harness.h"

/**
 * noa_harness_doorbell_init() - Initializes the doorbell mechanism for the test harness.
 * @tm: Pointer to the main test harness structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_doorbell_init(struct noa_harness *tm);

/**
 * noa_harness_doorbell_enable() - Enables all configured doorbells.
 * @tm: Pointer to the main test harness structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_doorbell_enable(struct noa_harness *tm);

/**
 * noa_harness_doorbell_disable() - Disables all configured doorbells.
 * @tm: Pointer to the main test harness structure.
 */
void noa_harness_doorbell_disable(struct noa_harness *tm);

/**
 * noa_harness_trigger_doorbell() - Triggers a specific doorbell.
 * @doorbell: Pointer to the hardware doorbell instance.
 */
void noa_harness_trigger_doorbell(void *doorbell);

/**
 * noa_harness_doorbell_get() - Retrieves a specific doorbell instance.
 * @tm: Pointer to the main test harness structure.
 * @id: The ID of the doorbell to retrieve.
 *
 * Return: A pointer to the doorbell instance, or NULL if the ID is invalid.
 */
void *noa_harness_doorbell_get(struct noa_harness *tm, enum noa_harness_doorbell_id id);

/**
 * noa_harness_doorbell_register_isr_task() - Registers a task to be executed upon doorbell interrupt.
 * @doorbell: Pointer to the harness doorbell structure.
 * @task: Pointer to the ISR task to be registered.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_doorbell_register_isr_task(struct noa_harness_doorbell *doorbell,
					   struct noa_harness_isr_task *task);

/**
 * noa_harness_doorbell_unregister_isr_task() - Unregisters a previously registered ISR task.
 * @task: Pointer to the ISR task to be unregistered.
 */
void noa_harness_doorbell_unregister_isr_task(struct noa_harness_isr_task *task);

#endif /* __HARNESS_DOORBELL_H__ */
