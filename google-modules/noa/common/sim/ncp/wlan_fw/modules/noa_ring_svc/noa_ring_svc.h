#ifndef MODULES_NOA_RING_SVC_NOA_RING_SVC_H
#define MODULES_NOA_RING_SVC_NOA_RING_SVC_H

#include "ext_svc/ext_svc.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"

/// @brief Wrapper structure for a ring of the ring service.
typedef struct noa_ring_wrapper NepRing;

/// @brief Number of TX post rings for the ring service.
#define NUM_RING_SVC_TX_POST_RING (1U)
/// @brief Number of RX post rings for the ring service.
#define NUM_RING_SVC_RX_POST_RING (0U)
/// @brief Number of TX completion rings for the ring service.
#define NUM_RING_SVC_TX_CMPL_RING (0U)
/// @brief Number of RX completion rings for the ring service.
#define NUM_RING_SVC_RX_CMPL_RING (1U)
/// @brief Number of direct TX post rings for the ring service.
#define NUM_RING_SVC_DIRECT_TX_POST_RING (1U)
/// @brief Number of direct RX completion rings for the ring service.
#define NUM_RING_SVC_DIRECT_RX_CMPL_RING (1U)
/// @brief Number of direct TX completion rings for the ring service.
#define NUM_RING_SVC_DIRECT_TX_CMPL_RING (1U)
/// @brief Number of direct RX fallback rings for the ring service.
#define NUM_RING_SVC_DIRECT_RX_FALLBACK_RING (1U)
/// @brief Number of direct Vendor RX replenishment rings for the ring service.
#define NUM_RING_SVC_DIRECT_VENDOR_RX_REPLENISH_RING (1U)
/// @brief Number of direct NOA TX buffer replenishment rings for the ring service.
#define NUM_RING_SVC_DIRECT_NOA_TX_REPLENISH_RING (1U)
/// @brief Number of feedback rings for the ring service.
#define NUM_RING_SVC_DIRECT_FEEDBACK_RING (1U)
/// @brief Size of the WLAN FW NEP input ring.
#define WLAN_FW_NEP_INPUT_RING_SIZE (512U)
/// @brief Size of the WLAN FW NEP output ring.
#define WLAN_FW_NEP_OUTPUT_RING_SIZE (512U)

/// @brief Structure representing the Noa Ring Service.
typedef struct NoaRingSvc {
	/// @brief Pointer to external services.
	ExternalServices *ext_svc;
	/// @brief TX post ring pool.
	NepRing ring_svc_tx_post_ring_pool[NUM_RING_SVC_TX_POST_RING];
	/// @brief RX completion ring pool.
	NepRing ring_svc_rx_cmpl_ring_pool[NUM_RING_SVC_RX_CMPL_RING];
	/// @brief Direct TX post ring pool.
	NepRing ring_svc_direct_tx_ring_pool[NUM_RING_SVC_DIRECT_TX_POST_RING];
	/// @brief Direct RX completion ring pool.
	NepRing ring_svc_direct_rx_ring_pool[NUM_RING_SVC_DIRECT_RX_CMPL_RING];
	/// @brief Direct TX completion ring pool.
	NepRing ring_svc_direct_tx_cpl_ring_pool[NUM_RING_SVC_DIRECT_TX_CMPL_RING];
	/// @brief Direct RX completion ring pool.
	NepRing ring_svc_direct_rx_fallback_ring_pool[NUM_RING_SVC_DIRECT_RX_FALLBACK_RING];
	/// @brief Direct vendor RX replenishment ring pool.
	NepRing ring_svc_direct_vendor_rx_replenish_ring_pool
		[NUM_RING_SVC_DIRECT_VENDOR_RX_REPLENISH_RING];
	/// @brief Direct NOA TX replenishment ring pool.
	NepRing ring_svc_direct_noa_tx_replenish_ring_pool[NUM_RING_SVC_DIRECT_NOA_TX_REPLENISH_RING];
	/// @brief Feedback ring pool.
	NepRing ring_svc_feedback_ring_pool[NUM_RING_SVC_DIRECT_FEEDBACK_RING];
} NoaRingSvc;

/// @brief Structure containing initialization parameters for the Noa Ring
/// Service.
typedef struct NoaRingSvcInitParams {
	/// @brief Pointer to external services.
	ExternalServices *ext_svc;
} NoaRingSvcInitParams;

/// @brief Initializes the Noa Ring Service.
///
/// @param[in] ring_svc Pointer to the NoaRingSvc structure.
/// @param[in] params Pointer to the NoaRingSvcInitParams structure.
/// @return 0 on success, a negative error code on failure.
int32_t NoaRingSvcInit(NoaRingSvc *const ring_svc, NoaRingSvcInitParams *const params);

/// @brief Deinitializes the Noa Ring Service.
///
/// @param[in] ring_svc Pointer to the NoaRingSvc structure.
void NoaRingSvcDeinit(NoaRingSvc *const ring_svc);

#endif /* MODULES_NOA_RING_SVC_NOA_RING_SVC_H */
