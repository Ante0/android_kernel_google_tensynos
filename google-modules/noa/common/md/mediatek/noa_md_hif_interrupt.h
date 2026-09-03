// NOLINTBEGIN(readability-identifier-naming)
// TODO: b/384853303 - Move noa_md_hif_interrupt.h to /sim/ncp/md/modules_pw/
#ifndef __NOA_MD_HIF_INTERRUPT_H__
#define __NOA_MD_HIF_INTERRUPT_H__

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

enum {
	NOA_DPMAIF_CLEAR_INTR = 0,
	NOA_DPMAIF_UNMASK_INTR = 1,
};

typedef enum {
	NOA_DPMAIF_INTR_MIN = 0,
	/* uplink part */
	NOA_DPMAIF_INTR_UL_DONE = 1,
	NOA_DPMAIF_INTR_UL_DRB_EMPTY = 2,
	NOA_DPMAIF_INTR_UL_MD_NOTREADY = 3,
	NOA_DPMAIF_INTR_UL_MD_PWR_NOTREADY = 4,
	NOA_DPMAIF_INTR_UL_LEN_ERR = 5,
	/* downlink part */
	NOA_DPMAIF_INTR_DL_LEGACY_DONE = 6,
	NOA_DPMAIF_INTR_DL_SKB_LEN_ERR = 7,
	NOA_DPMAIF_INTR_DL_BATCNT_LEN_ERR = 8,
	NOA_DPMAIF_INTR_DL_PKT_EMPTY_SET = 9,
	NOA_DPMAIF_INTR_DL_FRG_EMPTY_SET = 10,
	NOA_DPMAIF_INTR_DL_MTU_ERR = 11,
	NOA_DPMAIF_INTR_DL_FRGCNT_LEN_ERR = 12,
	NOA_DPMAIF_INTR_DL_PITCNT_LEN_ERR = 13,
	NOA_DPMAIF_INTR_DL_HPC_ENT_TYPE_ERR = 14,
	NOA_DPMAIF_INTR_DL_DONE = 15,
	/* traffic sync */
	NOA_DPMAIF_INTR_TRAS_SYNC = 16,
	NOA_DPMAIF_INTR_MAX
} noa_dpmaif_drv_intr_type;

/**
 * @brief Registers a DPMAIF event handler.
 *
 * The registered handler is called for each DPMAIF event, regardless of type.
 * The handler shall take two parameters:
 *   event_type - the type of event which occurred (see
 *     noa_dpmaif_drv_intr_type).
 *   queue_mask - a bitmask of which queues are affected.  Eg. if queues 0 and 2
 *     both have the same event, a single callback will be issued with the
 *     queue_mask set to 0b0101.
 *
 * @param handler The handler to be invoked for DPMAIF events.
 * @return 0 if registration is successful, negative error code otherwise.
 */
int noa_dpmaif_register_event_handler(int (*handler)(noa_dpmaif_drv_intr_type event_type,
						     unsigned int queue_mask));

/**
 * @brief Registers a callback handler for CLDMA RX done events.
 *
 * This function allows a control path component to register a handler that
 * will be invoked when the modem signals a CLDMA RX transfer completion.
 *
 * @param[in] handler A pointer to the callback function.
 *                    - The handler receives a uint32_t representing the RX
 *                      status bits from the hardware.
 *                    - The handler should return true if the Application
 *                      Processor Core (APC) should be notified of the event,
 *                      or false if the interrupt has been fully handled.
 *
 * @return 0 on success, or -1 if the modem interrupt manager is not initialized.
 */
int32_t noa_cldma_rx_done_register_event_handler(bool (*handler)(uint32_t));

/**
 * @brief Marks an interrupt complete.
 *
 * Must be invoked by register DPMAIF event handlers to unmask or clear
 * interrupts when handling finishes.  May be invoked asynchonrously.
 *
 * @param event_type Which type of event finished.
 * @param queue_id For which queue the event completed.
 * @param data NOA_DPMAIF_CLEAR_INTR (0) to clear the interrupt, NOA_DPMAIF_UNMASK_INTR
 *   (1) to unmask the interrupt.
 * @return 0 on success, or a negative error code on failure.
 */
int noa_dpmaif_drv_intr_complete(noa_dpmaif_drv_intr_type event_type, uint8_t queue_id,
				 uint64_t data);

#endif // __NOA_MD_HIF_INTERRUPT_H__
// NOLINTEND(readability-identifier-naming)
