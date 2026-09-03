// NOLINTBEGIN(readability-identifier-naming)
// TODO: b/384853303 - Move noa_md_hif_ring.h to /sim/ncp/md/modules_pw/
#ifndef __NOA_MD_HIF_RING_H__
#define __NOA_MD_HIF_RING_H__

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

typedef enum {
	NOA_DPMAIF_PIT = 0,
	NOA_DPMAIF_BAT = 1,
	NOA_DPMAIF_FRAG = 2,
	NOA_DPMAIF_DRB = 3,
} noa_dpmaif_drv_ring_type;

/**
 * @brief Rings a modem doorbell.
 * @param ring_type Which type of doorbell to ring (PIT, BAT, FRAG, or DRB).
 * @param queue_id Which queue's doorbell to ring (eg. 0 to 4 for DRB, 0 to 2 for PIT).
 * @param count Number of entries added to the queue (message count, not bytes/words).
 * @return 0 on success, otherwise an error code.
 */
int noa_dpmaif_drv_send_doorbell(noa_dpmaif_drv_ring_type ring_type, uint8_t queue_id,
				 uint32_t count);

typedef enum {
	NOA_DPMAIF_PIT_WIDX = 0,
	NOA_DPMAIF_PIT_RIDX = 1,
	NOA_DPMAIF_BAT_WIDX = 2,
	NOA_DPMAIF_BAT_RIDX = 3,
	NOA_DPMAIF_FRAG_WIDX = 4,
	NOA_DPMAIF_FRAG_RIDX = 5,
	NOA_DPMAIF_DRB_WIDX = 6,
	NOA_DPMAIF_DRB_RIDX = 7,
} noa_dpmaif_ring_index;

/**
 * @brief Reads a modem ring index.
 * @param index Which index to read (see noa_dpmaif_ring_index).
 * @param queue_id Which queue's doorbell to ring (eg. 0 to 4 for DRB, 0 to 2 for PIT).
 * @return Value of the requested index, or a negative error code on failure.  Always
 * returns 0 for NOA_DPMAIF_FRAG_WIDX and NOA_DPMAIF_DRB_WIDX.
 */
int noa_dpmaif_drv_get_ring_idx(noa_dpmaif_ring_index index, uint8_t queue_id);

#endif // __NOA_MD_HIF_RING_H__
// NOLINTEND(readability-identifier-naming)
