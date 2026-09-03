/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef DP_DAL_SIM_H
#define DP_DAL_SIM_H

#include "dp_dal.h"
#include <qdf_atomic.h>
#include <qdf_list.h>
#include "hal_tx.h"

/**
 * struct dp_dal_sim_sw2sw_ring - SW2SW SRNG for descriptor storage
 * @hp: Head pointer (DWORD index, like HAL SRNG)
 * @tp: Tail pointer (DWORD index, like HAL SRNG)
 * @ring_base_vaddr: Ring buffer base address (uint32_t array like HAL SRNG)
 * @ring_size: Total ring size in DWORDs (like HAL SRNG)
 * @entry_size: Size of each descriptor entry in DWORDs (like HAL SRNG)
 * @entry_size_bytes: Size of each descriptor entry in bytes (for qdf_mem_copy)
 * @lock: Spinlock to protect the ring
 */
struct dp_dal_sim_sw2sw_ring {
	uint32_t hp;
	uint32_t tp;
	uint32_t *ring_base_vaddr;
	uint32_t ring_size;
	uint32_t entry_size;
	uint32_t entry_size_bytes;
	qdf_spinlock_t lock;
};

/* SW2SW SRNG configuration */
#define DP_DAL_SIM_SW2SW_SRNG_SIZE 8192 /* 8K entries per ring */

/* Descriptor sizes for SW2SW SRNG */
#define DP_DAL_SIM_TX_CPL_DESC_SIZE HAL_TX_COMPLETION_DESC_LEN_BYTES
#define DP_DAL_SIM_RX_CPL_DESC_SIZE 32

/* D3 WoW Message Format Definitions */
/* Message format: Bits [31:28] - Tag, [27:12] - Type, [11:0] - Value/Payload */
#define OLE_WOW_MSG_MSG_TAG_SHIFT        28
#define OLE_WOW_MSG_MSG_TAG_MASK         0xF0000000
#define OLE_WOW_MSG_MSG_TYPE_SHIFT       12
#define OLE_WOW_MSG_MSG_TYPE_MASK        0x0FFFF000
#define OLE_WOW_MSG_MSG_VALUE_SHIFT      0
#define OLE_WOW_MSG_MSG_VALUE_MASK       0x00000FFF

/* Message Tags */
#define OLE_WOW_MSG_TAG_OLE_TO_WLAN      0x1
#define OLE_WOW_MSG_TAG_WLAN_TO_OLE      0x2

/* Message Types - OLE to WLAN */
#define OLE_WOW_MSG_TYPE_INTF_PAUSE      0x1
#define OLE_WOW_MSG_TYPE_INTF_RESUME     0x2
#define OLE_WOW_MSG_TYPE_OFFLOAD_CRASH   0x4

/* Message Types - WLAN to OLE */
#define OLE_WOW_MSG_TYPE_ACK             0x1
#define OLE_WOW_MSG_TYPE_NACK            0x2
#define OLE_WOW_MSG_TYPE_WLAN_CRASH      0x4

/* Complete Message Values */
#define OLE_WOW_MSG_MSG_OLE_INTF_PAUSE   0x10001000
#define OLE_WOW_MSG_MSG_OLE_INTF_RESUME  0x10002000
#define OLE_WOW_MSG_MSG_OLE_CRASH        0x10004000
#define OLE_WOW_MSG_MSG_WLAN_ACK         0x20001000
#define OLE_WOW_MSG_MSG_WLAN_NACK_BASE   0x20002000
#define OLE_WOW_MSG_MSG_WLAN_CRASH       0x20004000

/* Helper Macros for Message Construction/Parsing */
#define DAL_MSG_GET_TAG(msg)        (((msg) & OLE_WOW_MSG_MSG_TAG_MASK) >> OLE_WOW_MSG_MSG_TAG_SHIFT)
#define DAL_MSG_GET_TYPE(msg)       (((msg) & OLE_WOW_MSG_MSG_TYPE_MASK) >> OLE_WOW_MSG_MSG_TYPE_SHIFT)
#define DAL_MSG_GET_VAL(msg)        (((msg) & OLE_WOW_MSG_MSG_VALUE_MASK) >> OLE_WOW_MSG_MSG_VALUE_SHIFT)
#define DAL_MSG_CONSTRUCT(tag, type, val) \
    (((tag) << OLE_WOW_MSG_MSG_TAG_SHIFT) | ((type) << OLE_WOW_MSG_MSG_TYPE_SHIFT) | ((val) & OLE_WOW_MSG_MSG_VALUE_MASK))

/**
 * enum dal_d3_wow_ack_status - WoW ACK/NACK status reasons
 * @OLE_WOW_MSG_ACK_ACKED: ACK received
 * @OLE_WOW_MSG_NACK_REASON_TX: TX in progress
 * @OLE_WOW_MSG_NACK_REASON_IS_BLOCK: Interface is blocked
 * @OLE_WOW_MSG_NACK_REASON_NOT_ALLOW: Operation not allowed
 * @OLE_WOW_MSG_NACK_REASON_HW_FAIL: Hardware failure
 * @OLE_WOW_MSG_NACK_REASON_TIMEOUT: Operation timeout
 * @OLE_WOW_MSG_NACK_REASON_RTT_DMA: RTT DMA in progress
 * @OLE_WOW_MSG_NACK_REASON_ROAM: Roaming in progress
 * @OLE_WOW_MSG_NACK_REASON_PEER_ACTIVE: Peer is active
 * @OLE_WOW_MSG_NACK_DEFER_FAILURE: Defer failure
 * @OLE_WOW_MSG_NACK_DEFER_TIMEOUT: Defer timeout
 * @OLE_WOW_MSG_NACK_FATAL_EVENT: Fatal event occurred
 * @OLE_WOW_MSG_NACK_REASON_UNIT_TEST_CMD: Unit test command in progress
 * @OLE_WOW_MSG_NACK_REASON_MCC_LITE: MCC lite in progress
 * @OLE_WOW_MSG_NACK_RESUME_IN_PROCESS: Resume already in process
 * @OLE_WOW_MSG_NACK_REASON_PLATFORM: Platform specific reason
 * @OLE_WOW_MSG_NACK_REASON_USD: USD in progress
 * @OLE_WOW_MSG_NACK_CLOSE_TO_TBTT: Close to TBTT
 */
enum dal_d3_wow_ack_status {
	OLE_WOW_MSG_ACK_ACKED = 0,
	OLE_WOW_MSG_NACK_REASON_TX = 1,
	OLE_WOW_MSG_NACK_REASON_IS_BLOCK = 2,
	OLE_WOW_MSG_NACK_REASON_NOT_ALLOW = 3,
	OLE_WOW_MSG_NACK_REASON_HW_FAIL = 4,
	OLE_WOW_MSG_NACK_REASON_TIMEOUT = 5,
	OLE_WOW_MSG_NACK_REASON_RTT_DMA = 6,
	OLE_WOW_MSG_NACK_REASON_ROAM = 7,
	OLE_WOW_MSG_NACK_REASON_PEER_ACTIVE = 8,
	OLE_WOW_MSG_NACK_DEFER_FAILURE = 9,
	OLE_WOW_MSG_NACK_DEFER_TIMEOUT = 10,
	OLE_WOW_MSG_NACK_FATAL_EVENT = 11,
	OLE_WOW_MSG_NACK_REASON_UNIT_TEST_CMD = 12,
	OLE_WOW_MSG_NACK_REASON_MCC_LITE = 13,
	OLE_WOW_MSG_NACK_RESUME_IN_PROCESS = 14,
	OLE_WOW_MSG_NACK_REASON_PLATFORM = 15,
	OLE_WOW_MSG_NACK_REASON_USD = 16,
	OLE_WOW_MSG_NACK_CLOSE_TO_TBTT = 17,
};

#ifdef FEATURE_DP_DAL_SIM
/**
 * dp_dal_sim_attach() - Attach dal sim context to DAL context
 * @priv: pointer to dal context
 *
 * This function allocates memory for DAL SIM context and attach it to
 * the DAL context. It attaches the platform bus ops as well.
 *
 * Return: 0 on success
 */
int dp_dal_sim_attach(void *priv);

/**
 * dp_dal_sim_detach() - Detach dal sim context from DAL context
 * @priv: pointer to DAL context
 *
 * This function detaches DAL SIM context from DAL context and free the
 * dal sim context.
 *
 * Return: None
 */
void dp_dal_sim_detach(void *priv);

/* Total number of rings used for DAL SIM
 * 2 Rx rings + 2 Tx rings + 2 Tx cmpl rings + 1 Rx reffill ring
 */
#define DAL_SIM_TOTAL_NUM_RINGS (DAL_SIM_NUM_RX_RINGS + \
				 DAL_SIM_NUM_TX_RINGS + \
				 DAL_SIM_NUM_TX_RINGS + 1)

/* Number of RX and TX rings for DAL simulator */
#define DAL_SIM_NUM_RX_RINGS DAL_RX_RINGS_MAX
#define DAL_SIM_NUM_TX_RINGS DAL_TX_RINGS_MAX

/**
 * struct dal_sim_work_ctx - Work context for ring-specific processing
 * @sim_ctx: Pointer to DAL simulation context
 * @ring_id: Ring ID (0 or 1)
 */
struct dal_sim_work_ctx {
	struct dp_dal_sim_ctx *sim_ctx;
	uint8_t ring_id;
};

/**
 * struct dal_sim_error_stats - DAL simulator error statistics
 * @mode_switch_desc_list_timeout: Count of forced mode switches due to desc
 * list processing  timeout
 * @bypass_mode_switch_ind_fail: bypass mode switch indication failure count
 * @offload_mode_switch_ind_fail: offload mode switch indication failure count
 * @sw2sw_ring_enq_fail: SW2SW ring enqueue failure count
 */
struct dal_sim_error_stats {
	uint32_t mode_switch_desc_list_timeout;
	uint32_t bypass_mode_switch_ind_fail;
	uint32_t offload_mode_switch_ind_fail;
	uint64_t sw2sw_ring_enq_fail;
};

/**
 * struct dal_sim_stats - DAL simulator statistics per ring
 * @tx_enqueued: Number of TX packets enqueued
 * @tx_completed: Number of TX completions processed
 * @rx_received: Number of RX packets received
 * @rx_replenished: Number of RX buffers replenished (global)
 * @rx_isr_count: Number of RX ISR invocations
 * @tx_cpl_isr_count: Number of TX completion ISR invocations
 * @rx_work_queued: Number of times RX work was queued
 * @tx_cpl_work_queued: Number of times TX completion work was queued
 * @rx_work_scheduled: Number of times RX work was scheduled
 * @tx_work_scheduled: Number of times TX completion work was scheduled
 * @error_stats: Error statistics for DAL simulator
 */
struct dal_sim_stats {
	uint64_t tx_enqueued[DAL_SIM_NUM_TX_RINGS];
	uint64_t tx_completed[DAL_SIM_NUM_TX_RINGS];
	uint64_t rx_received[DAL_SIM_NUM_RX_RINGS];
	uint64_t rx_replenished;
	uint64_t rx_isr_count[DAL_SIM_NUM_RX_RINGS];
	uint64_t tx_cpl_isr_count[DAL_SIM_NUM_TX_RINGS];
	uint64_t rx_work_queued[DAL_SIM_NUM_RX_RINGS];
	uint64_t tx_cpl_work_queued[DAL_SIM_NUM_TX_RINGS];
	uint64_t rx_work_scheduled[DAL_SIM_NUM_RX_RINGS];
	uint64_t tx_work_scheduled[DAL_SIM_NUM_TX_RINGS];
	struct dal_sim_error_stats error_stats;
};

/**
 * struct dal_sim_srng - Ring information for DAL sim rings
 * @hal_ring_id: hal ring id
 * @initialized: ring initialized or not
 * @ring_base_paddr: ring base paddr
 * @ring_base_vaddr: ring base vaddr
 * @num_entries: number of entries in this ring
 * @ring_size: ring size
 * @ring_size_mask: ring size mask
 * @entry_size: size of the ring entry
 * @lmac_ring: ring is lmac ring or not
 * @ring_type: HAL ring type
 * @ring_dir: ring direction src or dest
 * @u: Union containing ring pointer information based on ring direction
 * @u.dst_ring: Destination ring pointer structure
 * @u.dst_ring.tp: Tail pointer value for destination ring
 * @u.dst_ring.hp_addr: Head pointer address (UMAC: HP offset from BAR,
 *                      LMAC: HP physical addr)
 * @u.dst_ring.tp_addr: Tail pointer address (UMAC: TP offset from BAR,
 *                      LMAC: TP physical addr)
 * @u.src_ring: Source ring pointer structure
 * @u.src_ring.hp: Head pointer value for source ring
 * @u.src_ring.tp_addr: Tail pointer address (UMAC: TP offset from BAR,
 *                      LMAC: TP physical addr)
 * @u.src_ring.hp_addr: Head pointer address (UMAC: HP offset from BAR,
 *                      LMAC: HP physical addr)
 * @ring_num: ring number used to indicate isr indication
 * @irq_num: irq number
 * @grp_id: ext group context id associated with the ring
 * @msi_addr: MSI address
 * @msi_data: MSI Data
 *
 * This structure has all the ring information that will be passed to DAL
 * during init.
 */
struct dal_sim_srng {
	uint8_t hal_ring_id;
	bool initialized;
	qdf_dma_addr_t ring_base_paddr;
	uint32_t *ring_base_vaddr;
	uint32_t num_entries;
	uint32_t ring_size;
	uint32_t ring_size_mask;
	uint32_t entry_size;
	bool lmac_ring;
	enum hal_ring_type ring_type;
	int ring_dir;
	union {
		struct {
			uint32_t tp;
			uint64_t hp_addr;
			uint64_t tp_addr;
		} dst_ring;
		struct {
			uint32_t hp;
			uint64_t tp_addr;
			uint64_t hp_addr;
		} src_ring;
	} u;

	uint8_t ring_num;
	int irq_num;
	uint8_t grp_id;
	qdf_dma_addr_t msi_addr;
	uint32_t msi_data;
};

/**
 * struct dp_dal_sim_ctx - Context structure for DAL simulation
 * @dp_dal_ctx: Pointer to DAL datapath context
 * @offload_sim_ctx: Pointer to offload simulation context
 * @vendor_ops: Structure containing vendor callback operations
 * @rx_process_work: Work structure for RX processing
 * @rx_work_queue: Workqueue for RX tasks
 * @tx_compl_process_work: Work structure for TX completion processing
 * @tx_compl_work_queue: Workqueue for TX completion tasks
 * @rx_work_ctx: Work context for RX processing per ring
 * @tx_cpl_work_ctx: Work context for TX completion processing per ring
 * @rx_work_scheduled: Atomic variable indicating RX work scheduling status
 * @tx_compl_work_scheduled: Atomic variable indicating TX completion work
 *                           scheduling status
 * @rx_ring: Array of HAL SRNG structures for RX rings
 * @tx_cmpl_ring: Array of HAL SRNG structures for TX completion rings
 * @tx_ring: Array of HAL SRNG structures for TX rings
 * @rx_refill_ring: HAL SRNG structure for RX refill ring
 * @rx_sw2sw_ring: SW2SW rings for RX rings (per ring ID)
 * @tx_cpl_sw2sw_ring: SW2SW rings for TX completion rings (per ring ID)
 * @stats: Statistics for the simulator
 * @sim_ctx_initialized: Flag indicating if context is initialized
 * @dev: Pointer to device
 * @intf_info: Interface information maintained in DAL sim context
 * @dev_base_addr: device base address
 * @sim_mode_switch_in_progress: Flag indicating if mode switch is in progress
 * @rxbm_sync_lock: Lock for rxbm_sync operations during mode switch
 * @use_dal_vndr_hal: Use dal vendor hal for overwriitng tx desc
 * @suspend_msg_msi_addr: Derived MSI address for FW write
 * @suspend_msg_msi_data: Derived MSI data for FW write
 * @suspend_msg_irq_num: IRQ number for suspend message
 * @suspend_msg_event: Event for FW write completion
 * @suspend_msg_data_vaddr: Coherent buffer virtual address for FW messages
 * @suspend_msg_data_paddr: Coherent buffer physical address for FW messages
 *
 * This structure maintains all necessary context for DAL simulation,
 * including pointers to datapath context, platform operations, vendor
 * callbacks, work structures, workqueues, atomic scheduling indicators,
 * and ring buffers for RX and TX operations.
 */
struct dp_dal_sim_ctx {
	void *dp_dal_ctx;
	void *offload_sim_ctx;

	/* Work structures for async processing - separate for each ring */
	qdf_work_t rx_process_work[DAL_SIM_NUM_RX_RINGS];
	qdf_workqueue_t *rx_work_queue[DAL_SIM_NUM_RX_RINGS];
	qdf_work_t tx_compl_process_work[DAL_SIM_NUM_TX_RINGS];
	qdf_workqueue_t *tx_compl_work_queue[DAL_SIM_NUM_TX_RINGS];

	/* Work context for each ring */
	struct dal_sim_work_ctx rx_work_ctx[DAL_SIM_NUM_RX_RINGS];
	struct dal_sim_work_ctx tx_cpl_work_ctx[DAL_SIM_NUM_TX_RINGS];

	/* Atomic scheduling indicators - separate for each ring */
	qdf_atomic_t rx_work_scheduled[DAL_SIM_NUM_RX_RINGS];
	qdf_atomic_t tx_compl_work_scheduled[DAL_SIM_NUM_TX_RINGS];

	/* Ring structures */
	struct dal_sim_srng rx_ring[DAL_SIM_NUM_RX_RINGS];
	struct dal_sim_srng tx_cmpl_ring[DAL_SIM_NUM_TX_RINGS];
	struct dal_sim_srng tx_ring[DAL_SIM_NUM_TX_RINGS];
	struct dal_sim_srng rx_refill_ring;

	/* SW2SW rings for maintaining descriptors per ring ID */
	struct dp_dal_sim_sw2sw_ring rx_sw2sw_ring[DAL_SIM_NUM_RX_RINGS];
	struct dp_dal_sim_sw2sw_ring tx_cpl_sw2sw_ring[DAL_SIM_NUM_TX_RINGS];

	/* Statistics */
	struct dal_sim_stats stats;

	/* Initialization flag */
	bool sim_ctx_initialized;

	struct device *dev;

	/* Interface information maintained in DAL sim context */
	struct dal_intf_info intf_info[MAX_VDEV_CNT];
	void *dev_base_addr;

	/* Mode switch control */
	qdf_atomic_t sim_mode_switch_in_progress;
	qdf_spinlock_t rxbm_sync_lock;

	/* Use dal vendor hal for overwriitng tx desc*/
	bool use_dal_vndr_hal;

	/* Derived MSI config for FW write */
	qdf_dma_addr_t suspend_msg_msi_addr;
	uint32_t suspend_msg_msi_data;
	int suspend_msg_irq_num;
	qdf_event_t suspend_msg_event;

	/* Coherent buffer for FW to write messages to Host */
	void *suspend_msg_data_vaddr;
	qdf_dma_addr_t suspend_msg_data_paddr;
};

/**
 * dp_dal_sim_schedule_work() - Schedule work for interrupt processing
 * @arg: Pointer to offload_sim_irq_ctx structure
 *
 * This function is called by the offload simulation interrupt handler
 * to queue work for processing the interrupt. It checks if work is
 * already scheduled and queues work on the appropriate work queue.
 */
void dp_dal_sim_schedule_work(void *arg);

/**
 * dp_dal_sim_sw2sw_ring_enqueue() - Enqueue descriptor to SW2SW ring
 * @sim_ctx: DAL SIM context
 * @ring: Pointer to SW2SW ring structure
 * @desc: Descriptor pointer to enqueue
 *
 * This function enqueues a descriptor to the SW2SW ring using HP/TP mechanism.
 * It's called by the offload engine to queue descriptors.
 * Follows hal_srng_src_get_next pattern for optimal performance.
 *
 * Return: 0 on success, negative error code on failure
 */
static inline int
dp_dal_sim_sw2sw_ring_enqueue(struct dp_dal_sim_ctx *sim_ctx,
			      struct dp_dal_sim_sw2sw_ring *ring,
			      void *desc)
{
	uint32_t *dest_desc;
	uint32_t next_hp;

	if (!ring || !desc || !ring->ring_base_vaddr) {
		dp_err("NULL descriptor pointer");
		return -EINVAL;
	}

	/* Follow hal_srng_src_get_next pattern exactly */
	next_hp = (ring->hp + ring->entry_size) % ring->ring_size;

	/* Check if ring is full (next_hp would equal TP) */
	if (next_hp == ring->tp) {
		sim_ctx->stats.error_stats.sw2sw_ring_enq_fail++;
		dp_debug("SW2SW SRNG is full, HP=%u, TP=%u",
			 ring->hp, ring->tp);
		return -ENOSPC;
	}

	/* Get descriptor address at current HP like HAL SRNG */
	dest_desc = &ring->ring_base_vaddr[ring->hp];

	/* Copy descriptor content into SW2SW SRNG */
	qdf_mem_copy(dest_desc, desc, ring->entry_size_bytes);

	/* Update HP to next_hp like HAL SRNG */
	ring->hp = next_hp;

	dp_debug("Descriptor enqueued to SW2SW SRNG, HP=%u, TP=%u",
		 ring->hp, ring->tp);
	return 0;
}

/**
 * dp_dal_sim_sw2sw_ring_dequeue() - Dequeue descriptor from SW2SW ring
 * @ring: Pointer to SW2SW ring structure
 *
 * This function dequeues a descriptor from the SW2SW ring.
 * It's called by DAL sim to get descriptors for WLAN driver.
 * Follows hal_srng_dst_get_next pattern for optimal performance.
 *
 * Return: Descriptor pointer on success, NULL if ring is empty
 */
static inline void *dp_dal_sim_sw2sw_ring_dequeue(
	struct dp_dal_sim_sw2sw_ring *ring)
{
	uint32_t *desc;

	if (!ring || !ring->ring_base_vaddr) {
		dp_err("NULL SW2SW ring pointer");
		return NULL;
	}

	/* Follow hal_srng_dst_get_next pattern exactly */
	/* Check if ring is empty (TP == HP) */
	if (ring->tp == ring->hp) {
		dp_debug("SW2SW SRNG is empty, HP=%u, TP=%u",
			 ring->hp, ring->tp);
		return NULL;
	}

	/* Get descriptor address at current TP like HAL SRNG */
	desc = &ring->ring_base_vaddr[ring->tp];

	/* Update TP like HAL SRNG dst_get_next */
	ring->tp = (ring->tp + ring->entry_size);
	if (ring->tp == ring->ring_size)
		ring->tp = 0;

	dp_debug("Descriptor dequeued from SW2SW SRNG, HP=%u, TP=%u",
		 ring->hp, ring->tp);
	return (void *)desc;
}

/**
 * dp_dal_sim_sw2sw_ring_access_start() - Start accessing SW2SW ring
 * @ring: Pointer to SW2SW ring structure
 *
 * This function takes the spinlock for the SW2SW ring to allow
 * safe access for multiple dequeue operations. Must be paired with
 * dp_dal_sim_sw2sw_ring_access_end().
 */
static inline void dp_dal_sim_sw2sw_ring_access_start(
	struct dp_dal_sim_sw2sw_ring *ring)
{
	if (!ring) {
		dp_err("NULL SW2SW ring pointer in access_start");
		return;
	}

	qdf_spin_lock_bh(&ring->lock);
}

/**
 * dp_dal_sim_sw2sw_ring_access_end() - End accessing SW2SW ring
 * @ring: Pointer to SW2SW ring structure
 *
 * This function releases the spinlock for the SW2SW ring after
 * completing multiple dequeue operations. Must be paired with
 * dp_dal_sim_sw2sw_ring_access_start().
 */
static inline void dp_dal_sim_sw2sw_ring_access_end(
	struct dp_dal_sim_sw2sw_ring *ring)
{
	if (!ring) {
		dp_err("NULL SW2SW ring pointer in access_end");
		return;
	}

	qdf_spin_unlock_bh(&ring->lock);
}

/**
 * dp_dal_sim_trigger_mode_switch() - Trigger DAL sim mode switch
 * @soc_hdl: soc handle
 * @mode_requested: requested mode
 * This function sets the sim_mode_switch_in_progress flag to true and
 * blocks all replenish, tx, rx requests from wlan driver.
 *
 * Return: None
 */
void dp_dal_sim_trigger_mode_switch(
	struct cdp_soc_t *soc_hdl, uint8_t mode_requested);

/**
 * dp_dal_sim_get_curr_mode() - Get current mode of dal sim
 *
 * This function gets the current mode of dal sim.
 *
 * Return: current mode of dal sim (0 = bypass, 1 = offload)
 */
uint8_t dp_dal_sim_get_curr_mode(void);

/**
 * dp_dal_sim_cfg_use_vndr_hal() - This function returns if config to use
 * dal vendor hal for overwriting tx desc is present.
 * @sim_ctx: sim ctx
 *
 * Returns true if use dal vendor hal config is true else false
 *
 */
static inline bool dp_dal_sim_cfg_use_vndr_hal(struct dp_dal_sim_ctx *sim_ctx)
{
	if (!sim_ctx) {
		dp_err("Null sim ctx");
		return false;
	}

	return sim_ctx->use_dal_vndr_hal;
}
#else
static inline int dp_dal_sim_attach(void *priv)
{
	return 0;
}

static inline void dp_dal_sim_detach(void *priv)
{
}
#endif /* FEATURE_DP_DAL_SIM */
#endif /* DP_DAL_SIM_H */
