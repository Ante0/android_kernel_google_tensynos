/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef DP_DAL_H
#define DP_DAL_H

#include <qdf_status.h>
#include "dp_types.h"
#include <wlan_cfg.h>

#ifdef FEATURE_DP_DAL_SIM
struct dp_dal_sim_ctx;
#endif

#define DP_REO_DST_REMAP_REMOVE_DAL(_reo_config) \
	((_reo_config) &= ~(DAL_DP_REO_RING_MASK))

#ifdef FEATURE_DAL_DP_SUPPORT
#define DAL_DP_TCL_RING_MASK 0x3

/*
 * Changing DAL_DP_REO_RING_MASK should change the DAL_DP_DEFAULT_REO_STA
 * and DAL_DP_DEFAULT_REO_SAP to get correct reo destination ring.
 */
#define DAL_DP_REO_RING_MASK 0xc

/* used during the peer default routing config in DAL offload mode */
#define DAL_DP_DEFAULT_REO_STA 3
#define DAL_DP_DEFAULT_REO_SAP 4

#define PRIORITY_CLASS 8
#define MAC_ADDR_LEN 6

#define DAL_RX_RINGS_MAX 2
#define DAL_TX_RINGS_MAX 2

#define DAL_TX_RING_ID0_STA 0
#define DAL_TX_RING_ID0_SAP 1

/**
 * struct dal_ring_hp_tp_info - ring info
 * @ring_type: ring type
 * @ring_id: ring id
 * @hp: HP value
 * @tp: TP value
 */
struct dal_ring_hp_tp_info {
	uint8_t ring_type;
	uint8_t ring_id;
	uint32_t hp;
	uint32_t tp;
};

/**
 * enum dal_mode - dal operation mode
 * @DAL_DP_BYPASS_MODE: bypass mode
 * @DAL_DP_OFFLOAD_MODE: offload mode
 */
enum dal_mode {
	DAL_DP_BYPASS_MODE,
	DAL_DP_OFFLOAD_MODE,
};

/**
 * struct sta_info - STA information
 * @bss_idx: BSS index
 * @qos_txq_map: QoS TX queue map
 * @addr: MAC address
 */
struct sta_info {
	u16 bss_idx;
	u16 qos_txq_map[PRIORITY_CLASS];
	u8 addr[MAC_ADDR_LEN];
};

/**
 * struct platform_bus_ops - Platform bus operations
 * @init: Initialize the bus
 * @exit: Exit the bus
 * @start: Start the bus
 * @stop: Stop the bus
 * @request_irq: Request an interrupt
 * @rx: RX operation
 * @rx_replenish: Replenish RX buffers
 * @rxbm_sync: Sync RX buffer manager
 * @tx: TX operation
 * @tx_cpl: TX completion
 * @tx_queue_active: Check if TX queue is active
 * @sta_active: Check if STA is active
 * @notify_suspend: Notify suspend
 * @notify_resume: Notify resume
 * @ssr_dump: Dump SSR
 * @intf_init: Interface init
 * @intf_deinit: Interface deinit
 * @rx_pkt_reinject: RX packet reinject (Used during RX defrag & SG)
 */
struct platform_bus_ops {
	int (*init)(void *priv);
	void (*exit)(void *priv);
	int (*start)(void *priv);
	void (*stop)(void *priv);
	int (*request_irq)(void *priv);
	bool (*rx)(void *priv, u32 *cnt, u16 ring_id);
	int (*rx_replenish)(void *priv, u32 cnt, bool use_rsv_pktid);
	int (*rxbm_sync)(void *priv, u32 cnt, void **rxbm);
	int (*tx)(void *priv, u8 ring_id, u32 ifidx, void *desc,
		  void *tx_metadata);
	bool (*tx_cpl)(void *priv, u32 *cnt, u16 ring_id);
	int (*tx_queue_active)(void *priv, u16 flowid, bool enable);
	int (*sta_active)(void *priv, struct sta_info *info, bool enable);
	int (*notify_suspend)(void *priv, bool intf_pause);
	int (*notify_resume)(void *priv);
	void (*ssr_dump)(void *segment);
	int (*intf_init)(void *priv, void *intf_info);
	int (*intf_deinit)(void *priv, uint16_t vdev_id);
	bool (*rx_pkt_reinject)(void *pkt, uint32_t length);
};

/**
 * struct vendor_cb_ops - Vendor callback operations
 * @rx_isr_cb: RX ISR callback
 * @rx_cpl_cb: RX completion callback
 * @rx_replenish_alloc_cb: RX replenish allocation callback
 * @tx_cpl_cb: TX completion callback
 * @tx_isr_cb: Tx ISR callback
 * @set_msi_config: Set MSI config callback
 * @store_ring_hp_tp: Store HP/TP of DAL rings locally
 * @early_mode_switch_ind: Early mode switch indication callback
 * @mode_switch_ind: Mode switch indication callback
 * @set_suspend_msi_config: Set suspend MSI config callback
 */
struct vendor_cb_ops {
	int (*rx_isr_cb)(int ring_num, void *priv);
	int (*rx_cpl_cb)(void *priv, void *desc, u16 ring_id);
	int (*rx_replenish_alloc_cb)(void *priv, u16 count);
	int (*tx_cpl_cb)(void *priv, void *desc, u16 ring_id);
	int (*tx_isr_cb)(int rint_num, void *priv);
	int (*set_msi_config)(void *priv, uint8_t ring_num, uint8_t ring_type,
			      uint64_t msi_address, uint32_t msi_data);
	int (*early_mode_switch_ind)(void *priv, void *ring_info,
				     uint8_t num_info, uint8_t cur_mode,
				     uint8_t new_mode);
	int (*mode_switch_ind)(void *priv, u8 cur_mode, u8 new_mode);
	int (*set_suspend_msi_config)(void *priv, uint64_t msi_addr,
				      uint32_t msi_data,
				      void *suspend_msg_data_vaddr,
				      qdf_dma_addr_t suspend_msg_data_paddr);
};

/**
 * struct dal_srng - Ring information for DAL rings
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
 * @u: union for src ring or dest ring
 * @tp: tail pointer value
 * @hp_addr: UMAC ring it is HP offset form BAR
 *		LMAC ring it is HP physical addr
 * @tp_addr: physical address of TP
 * @hp: head pointer value
 * @tp_addr: UMAC ring it is TP offset form BAR
 *		LMAC ring it is TP physical addr
 * @hp_addr: physical address of HP
 * @ring_num: ring number used to indicate isr indication
 * @grp_id: ext group context id associated with the ring
 *
 * This structure has all the ring information that will be passed to DAL
 * during init.
 */
struct dal_srng {
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
	enum hal_srng_dir ring_dir;
	union {
		struct {
			uint32_t tp;
#ifndef FEATURE_DP_DAL_SIM
			/* This is the PADDR of the shadow DDR memory */
			uint64_t hp_addr;
			/* This is the shadow TP register offset */
			uint32_t tp_addr;
#else
			/* In the case of DAL sim, this is the VADDR */
			uint64_t hp_addr;
			/* This is the shadow TP register offset */
			uint64_t tp_addr;
#endif
		} dst_ring;

		struct {
			uint32_t hp;
#ifndef FEATURE_DP_DAL_SIM
			/* This is the PADDR of the shadow DDR memory */
			uint64_t tp_addr;
			/* This is the shadow HP register offset */
			uint32_t hp_addr;
#else
			/* In the case of DAL sim, this is the VADDR */
			uint64_t tp_addr;
			/* This is the shadow TP register offset */
			uint64_t hp_addr;
#endif
		} src_ring;
	} u;
	uint8_t ring_num;
	uint8_t grp_id;
};

/**
 * enum dal_intf_type - interface type
 * @DAL_INTF_TYPE_STA: station interface
 * @DAL_INTF_TYPE_SAP: SAP interface
 *
 * @DAL_INTF_TYPE_MAX: max supported interface type
 */
enum dal_intf_type {
	DAL_INTF_TYPE_STA,
	DAL_INTF_TYPE_SAP,
	DAL_INTF_TYPE_MAX,
};

/**
 * struct dal_intf_info - DAL interface information
 * @type: Interface type
 * @mac_address: MAC address
 * @bss_idx: BSS index
 * @vdev_id: VDEV ID
 * @tcl_bank_id: TCL bank ID
 * @tx_ring_id: TX ring ID to use for tethered traffic
 * @tx_rbm_id: TX RBM ID to use for tethered traffic
 */
struct dal_intf_info {
	enum dal_intf_type type;
	uint8_t mac_address[MAC_ADDR_LEN];
	int bss_idx;
	uint16_t vdev_id;
	uint8_t tcl_bank_id;
	uint8_t tx_ring_id;
	uint8_t tx_rbm_id;
};

/**
 * struct dal_pkt_info - packet info for dal stats
 * @num: number of packets
 * @bytes: number of bytes
 */
struct dal_pkt_info {
	uint64_t num;
	uint64_t bytes;
};

/**
 * enum dal_tx_event_type - TX event types for dal stats
 * @DAL_TX_TOTAL_PKT_RCVD: Total packets received
 * @DAL_TX_BYPASSED_PKT: Bypassed packet count
 * @DAL_TX_BYPASSED_DRP: Bypassed drop count
 * @DAL_TX_NOSUPPORT_DRP: No support drop count
 * @DAL_TX_FAILED_DRP: Failed drop count
 * @DAL_TX_EVENT_MAX: Max event type
 */
enum dal_tx_event_type {
	DAL_TX_TOTAL_PKT_RCVD,
	DAL_TX_BYPASSED_PKT,
	DAL_TX_BYPASSED_DRP,
	DAL_TX_NOSUPPORT_DRP,
	DAL_TX_FAILED_DRP,
	DAL_TX_EVENT_MAX,
};

/**
 * struct dp_dal_stats - DAL statistics
 * @tx: TX related statistics
 * @rx: RX related statistics
 */
struct dp_dal_stats {
	struct {
		struct dal_pkt_info offload[DAL_TX_RINGS_MAX][DAL_TX_EVENT_MAX];
		/* DAL Tx comp failed count */
		uint32_t tx_comp_failed[MAX_TCL_DATA_RINGS];
		/* Tx comp failed due to DAL no support */
		uint32_t tx_comp_nosupport[MAX_TCL_DATA_RINGS];
	} tx;
	struct {
		/* Rx failed due to DAL no support */
		uint32_t rx_dropped_nosupport[MAX_REO_DEST_RINGS];
	} rx;
};

/**
 * struct dp_dal_rx_desc_node - DAL RX descriptor list node
 * @next: Next pointer for DAL queue
 * @rx_desc: Pointer to actual RX descriptor
 *
 * This structure is used to create a linked list for DAL RX
 * descriptor queuing.
 */
struct dp_dal_rx_desc_node {
	struct dp_dal_rx_desc_node *next;
	struct dp_rx_desc *rx_desc;
};

/**
 * struct dp_dal_ctx - Context structure for DAL simulation
 * @soc: Pointer to DP SoC
 * @rx_ring: Array of HAL SRNG structures for RX rings.
 * @tx_cmpl_ring: Array of HAL SRNG structures for TX completion rings.
 * @tx_ring: Array of HAL SRNG structures for TX rings.
 * @rx_refill_ring: HAL SRNG structure for RX refill ring.
 * @num_tx_ring_info: number of tx ring info saved
 * @num_rx_ring_info: number of rx ring info saved
 * @num_tx_cmpl_ring_info: number of tx completion ring info saved
 * @tx_cpl_desc_list: TX completion descriptor head list for each ring
 * @tx_cpl_desc_tail: TX completion descriptor tail list for each ring
 * @tx_cpl_desc_count: TX completion descriptor counts for each ring
 * @dal_tx_cpl_lock: Spinlock to protect TX completion descriptor
 *		     list operations
 * @rx_desc_head: RX descriptor node head list for each ring
 * @rx_desc_tail: RX descriptor node tail list for each ring
 * @rx_desc_count: RX descriptor counts for each ring
 * @dal_rx_desc_lock: Spinlock to protect RX descriptor list operations
 * @dal_poll_timer: Timer used to poll the DAL rings during mode switch
 *		from offload to bypass
 * @poll_count: polling counter
 * @dal_replenish_lock: Spinlock to synchronize rx replenish
 * @rx_replenish_failures: Counter to track rx replenish failures
 * @rx_replenish_retry_timer: timer to retry replenish
 * @rx_replenish_retry_count: Max replenish retry count
 * @rx_replenish_retry_interval_ms: Interval at which replenish timer runs
 * @deinit_in_progress: flag to indicate dal_soc_deinit in progress
 * @bm_replenish_not_allowed: flag to reject replenish from bypass pass
 * @mode_switch_runtime_lock: lock to prevent runtime suspend during dal ops
 * @offload_plat_ops: Offload platform operations
 * @suspended_tx_list: List of suspended TX descriptors during runtime PM
 * @suspended_tx_lock: Spinlock to protect suspended TX list operations
 * @suspended_tx_count: Count of suspended TX descriptors
 * @dal_sim_ctx: DAL simulation context
 * @suspend_msg_msi_addr: Suspend message MSI address
 * @suspend_msg_msi_data: Suspend message MSI data
 * @suspend_msg_data_vaddr: Suspend message data virtual address
 * @suspend_msg_data_paddr: Suspend message data physical address
 * @suspend_htt_msg_sent: Flag indicating if D3 WoW HTT suspend message was sent
 * @stats: DAL statistics
 *
 * This structure maintains all necessary context for DAL operations,
 * including pointers to datapath context, platform operations, vendor
 * callbacks, for RX and TX operations. This structure is passed as priv
 * argument in all DAL APIs.
 */
struct dp_dal_ctx {
	struct dp_soc *soc;
	struct dal_srng rx_ring[DAL_RX_RINGS_MAX];
	struct dal_srng tx_cmpl_ring[DAL_TX_RINGS_MAX];
	struct dal_srng tx_ring[DAL_TX_RINGS_MAX];
	struct dal_srng rx_refill_ring;
	int num_tx_ring_info;
	int num_rx_ring_info;
	int num_tx_cmpl_ring_info;
	struct dp_tx_desc_s *tx_cpl_desc_list[MAX_TCL_DATA_RINGS];
	struct dp_tx_desc_s *tx_cpl_desc_tail[MAX_TCL_DATA_RINGS];
	uint32_t tx_cpl_desc_count[MAX_TCL_DATA_RINGS];
	qdf_spinlock_t dal_tx_cpl_lock;
	struct dp_dal_rx_desc_node *rx_desc_head[MAX_REO_DEST_RINGS];
	struct dp_dal_rx_desc_node *rx_desc_tail[MAX_REO_DEST_RINGS];
	uint32_t rx_desc_count[MAX_REO_DEST_RINGS];
	qdf_spinlock_t dal_rx_desc_lock;
	qdf_timer_t dal_poll_timer;
	uint32_t poll_count;
	qdf_spinlock_t dal_replenish_lock;
	qdf_atomic_t rx_replenish_failures;
	qdf_timer_t rx_replenish_retry_timer;
	int rx_replenish_retry_count;
	int rx_replenish_retry_interval_ms;
	qdf_atomic_t deinit_in_progress;
	qdf_atomic_t bm_replenish_not_allowed;
	qdf_runtime_lock_t mode_switch_runtime_lock;
	struct platform_bus_ops *offload_plat_ops;
#if defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE)
	qdf_list_t suspended_tx_list;
	qdf_spinlock_t suspended_tx_lock;
	uint32_t suspended_tx_count;
#endif /* defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE) */
#ifdef FEATURE_DP_DAL_SIM
	struct dp_dal_sim_ctx *dal_sim_ctx;
#endif
	qdf_dma_addr_t suspend_msg_msi_addr;
	uint32_t suspend_msg_msi_data;
	void *suspend_msg_data_vaddr;
	qdf_dma_addr_t suspend_msg_data_paddr;
	bool suspend_htt_msg_sent;
	struct dp_dal_stats stats;
};

/**
 * dp_dal_soc_detach - detach DP DAL to SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: None.
 */
void dp_dal_soc_detach(struct dp_soc *soc);

/**
 * dp_dal_soc_deinit - De-initialize DP DAL for SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: None.
 */
void dp_dal_soc_deinit(struct dp_soc *soc);

/**
 * dp_dal_soc_attach - Attach DP DAL to SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure.
 */
QDF_STATUS dp_dal_soc_attach(struct dp_soc *soc);

/**
 * dp_dal_soc_init - Initialize DP DAL for SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure.
 */
QDF_STATUS dp_dal_soc_init(struct dp_soc *soc);

/**
 * dp_dal_rx_buffers_replenish() - RX buffer enqueue function used from
 * non-DAL path
 * @soc: pointer to DP SoC
 * @mac_id: mac id
 * @dp_rxdma_srng: dp rxdma circular ring
 * @rx_desc_pool: pointer to rx desc pool
 * @num_req_buffers: Number of Rx buffers to replenish
 * @desc_list: HEAD pointer to rx desc list elem list
 * @tail: TAIL pointer to rx desc list elem list
 * @req_only: If true don't replenish more than req buffers
 *
 * Invoked from a non-DAL path, such as the non-DAL REO DEST ring process, the
 * Rx error path replenishes buffers for processed descriptors. Since the OE
 * manages the rx buffer refill ring, all rx buffer replenishments must be
 * performed through the OE.
 *
 * Return: int
 */
int dp_dal_rx_buffers_replenish(struct dp_soc *soc, uint32_t mac_id,
				struct dp_srng *dp_rxdma_srng,
				struct rx_desc_pool *rx_desc_pool,
				uint32_t num_req_buffers,
				union dp_rx_desc_list_elem_t **desc_list,
				union dp_rx_desc_list_elem_t **tail,
				bool req_only);

/**
 * dp_dal_interface_add() - DAL interface add
 * @soc: pointer to DP SoC
 * @vdev: DP vdev structure
 *
 * Called during dp_vdev_attach_wifi3(), this function will add interface
 * details to offload engine.
 *
 * Return: int
 */
int dp_dal_interface_add(struct dp_soc *soc, struct dp_vdev *vdev);

/**
 * dp_dal_interface_remove() - DAL interface remove
 * @soc: pointer to DP SoC
 * @vdev: DP vdev structure
 *
 * Called during dp_vdev_detach_wifi3(), this function will remove interface
 * details from the offload engine.
 *
 * Return: None
 */
void dp_dal_interface_remove(struct dp_soc *soc, struct dp_vdev *vdev);

/**
 * dp_dal_notify_suspend() - DAL wrapper for platform notify suspend
 * @soc: pointer to DP SoC
 * @intf_pause: Interface pause flag
 *
 * This function calls the global platform ops notify_suspend function.
 * When this returns successfully, it means there are no pending transactions
 * from the DAL and the device can suspend.
 *
 * Return: 0 on success, negative error code on failure
 */
QDF_STATUS dp_dal_notify_suspend(struct dp_soc *soc, bool intf_pause);

/**
 * dp_dal_notify_resume() - DAL wrapper for platform notify resume
 * @soc: pointer to DP SoC
 *
 * This function calls the global platform ops notify_resume function.
 * This is called when the device is resuming from suspend state.
 *
 * Return: 0 on success, negative error code on failure
 */
QDF_STATUS dp_dal_notify_resume(struct dp_soc *soc);

/**
 * dp_dal_flush_suspended_tx_descs() - Wrapper to flush suspended TX descs
 * @soc: pointer to DP SoC
 *
 * This function provides a wrapper around dp_dal_tx_flush_suspended_descs
 * that can be called from dp_main.c. It takes a dp_soc pointer and internally
 * calls the DAL TX function with the dal_ctx.
 *
 * Return: Number of descriptors flushed
 */
uint32_t dp_dal_flush_suspended_tx_descs(struct dp_soc *soc);

/**
 * dp_dal_ssr_notify() - DAL wrapper for platform SSR notify
 * @soc: pointer to DP SoC
 *
 * This function calls the global platform ops ssr_dump function.
 * This is called to notify the DAL about SSR events.
 *
 * Return: None
 */
void dp_dal_ssr_notify(struct dp_soc *soc);

/**
 * dp_service_dal_srngs() - service DAL rings
 * @dp_ctx: dp intr context
 * @dp_budget: dp budget
 * @cpu: cpu
 *
 * Return: work done
 */
uint32_t dp_service_dal_srngs(void *dp_ctx, uint32_t dp_budget, int cpu);

static inline void
dp_srng_mark_dal_owned_ring(struct dp_soc *soc, struct dp_srng *srng,
			    uint8_t idx, enum hal_ring_type type)
{
	int mask;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx))
		return;

	if (type == REO_DST)
		mask = DAL_DP_REO_RING_MASK;
	else
		mask = DAL_DP_TCL_RING_MASK;

	if (BIT(idx) & mask)
		srng->dal_owned_ring = true;
}

static inline bool dp_srng_check_dal_owned_ring(struct dp_srng *srng)
{
	return srng->dal_owned_ring;
}

void dp_dal_save_srng_info(struct dp_soc *soc, struct dp_srng *srng,
			   enum hal_ring_type type, int ring_num);

int dp_dal_get_ext_grp_id(struct dp_dal_ctx *dal_ctx,
			  int ring_num, enum hal_ring_type type);
/**
 * dp_dal_notify_sta_active() - Notify DAL about STA/SAP active state
 * @soc: pointer to DP SoC
 * @peer: pointer to DP peer
 * @peer_mac: peer MAC address
 *
 * This function notifies DAL about STA connect/disconnect events for both
 * STA and AP modes. It is called during peer state transitions.
 *
 * Return: None
 */
void dp_dal_notify_sta_active(struct dp_soc *soc,
			      struct dp_peer *peer,
			      uint8_t *peer_mac);

#else
#define DAL_DP_TCL_RING_MASK 0
#define DAL_DP_REO_RING_MASK 0

static inline void
dp_dal_save_srng_info(struct dp_soc *soc, struct dp_srng *srng,
		      enum hal_ring_type type, int ring_num)
{
}

static inline bool dp_srng_check_dal_owned_ring(struct dp_srng *srng)
{
	return false;
}

static inline void
dp_srng_mark_dal_owned_ring(struct dp_soc *soc, struct dp_srng *srng,
			    uint8_t idx, enum hal_ring_type type)
{
}

static inline void dp_dal_soc_detach(struct dp_soc *soc)
{
}

static inline void dp_dal_soc_deinit(struct dp_soc *soc)
{
}

static inline QDF_STATUS dp_dal_soc_attach(struct dp_soc *soc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS dp_dal_soc_init(struct dp_soc *soc)
{
	return QDF_STATUS_SUCCESS;
}

static inline int
dp_dal_interface_add(struct dp_soc *soc, struct dp_vdev *vdev)
{
	return 0;
}

static inline void
dp_dal_interface_remove(struct dp_soc *soc, struct dp_vdev *vdev)
{
}

static inline QDF_STATUS dp_dal_notify_suspend(struct dp_soc *soc,
					       bool intf_pause)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS dp_dal_notify_resume(struct dp_soc *soc)
{
	return QDF_STATUS_SUCCESS;
}

static inline void dp_dal_ssr_notify(struct dp_soc *soc)
{
}

static inline uint32_t
dp_dal_flush_suspended_tx_descs(struct dp_soc *soc)
{
	return 0;
}

static inline void
dp_dal_notify_sta_active(struct dp_soc *soc,
			 struct dp_peer *peer,
			 uint8_t *peer_mac)
{
}
#endif /* FEATURE_DAL_DP_SUPPORT */
#endif /* DP_DAL_H */
