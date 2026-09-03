/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef MD2AP_WAKEMON_H
#define MD2AP_WAKEMON_H

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/kernfs.h>
#include <linux/types.h>

#include "../common/radio-google.h"
#include "mtk_pci.h"
#include "mtk_port.h"

#define WAKEUP_REASON_DESC_LEN (20)

enum md2ap_wakeup_reason_lv1 { LV1_REASON_PEWAKE = BIT(0), LV1_REASON_MSIX = BIT(1) };

#define LV2_REASONS_LIST(X) \
	X(CLDMA0, 0)        \
	X(CLDMA1, 1)        \
	X(CLDMA2, 2)        \
	X(CLDMA3, 3)        \
	X(CLDMA4, 4)        \
	X(DPMAIF, 5)        \
	X(DPMAIF2, 6)       \
	X(DPMAIF3, 7)       \
	X(DPMAIF6, 8)       \
	X(MHCCIF, 9)        \
	X(SAP_RGU, 10)      \
	X(PM_LOCK, 11)      \
	X(ADO, 12)          \
	X(TRAS_SYNC, 13)

enum md2ap_wakeup_reason_lv2 {
#define ENUM_GEN(name, bit) LV2_REASON_##name = BIT(bit),
	LV2_REASONS_LIST(ENUM_GEN)
#undef ENUM_GEN
};

/* CLDMA used by host driver */
#define LV2_REASON_CLDMA_GROUP (LV2_REASON_CLDMA0 | LV2_REASON_CLDMA1 | LV2_REASON_CLDMA4)

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
#define MCF_LV2_REASON_NETWORK_GROUP \
	(LV2_REASON_DPMAIF | LV2_REASON_DPMAIF2 | LV2_REASON_DPMAIF3 | LV2_REASON_DPMAIF6)

#define MCF_LV2_REASON_GNSS_GROUP (LV2_REASON_CLDMA4)

#define MCF_LV2_REASON_CONTROL_GROUP \
	(LV2_REASON_MHCCIF | LV2_REASON_SAP_RGU | LV2_REASON_PM_LOCK | LV2_REASON_TRAS_SYNC)
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */

#define LV3_REASON_RXQNO_MASK GENMASK(31, 16)
#define LV3_REASON_RXCH_MASK GENMASK(15, 0)

/**
 * struct md2ap_wakemon - MD-to-AP wakeup monitor state
 * @goog: Pointer to the core radio_google structure for device access.
 * @pewake_flag: Atomic flag set when a PEWAKE trigger is detected.
 * @wakeup_flag_lv1: Gatekeeper flag; captures the first valid resume trigger.
 * @wakeup_flag_lv2: Tracks the specific hardware IRQ reason during resume.
 * @wakeup_flag_lv3: Tracks details for CLDMA-specific resume sources.
 * @wakeup_reason_lv1: The primary trigger type (e.g., PEWAKE or MSI-X).
 * @wakeup_reason_lv2: Bitmask representing the hardware/IRQ level reason.
 * @wakeup_reason_lv3: Bitfield containing RX queue and channel information.
 * @wakeup_cnt: Total number of recorded MD-to-AP wakeup events.
 * @wakeup_reason_lv2_desc: String description of the LV2 hardware reason.
 * @wakeup_reason_lv3_desc: String description (e.g., port name) for LV3.
 * @mcf_network_wakeup_cnt: MCF counter for network-related wakeups.
 * @mcf_gnss_wakeup_cnt: MCF counter for GNSS-related wakeups.
 * @mcf_log_wakeup_cnt: MCF counter for logging wakeups.
 * @mcf_control_wakeup_cnt: MCF counter for control/handshake wakeups.
 * @mcf_misc_wakeup_cnt: MCF counter for uncategorized wakeup events.
 *
 * This structure maintains the state of the wakeup monitoring system, allowing
 * the driver to categorize and log why the modem triggered a AP resume.
 */
struct md2ap_wakemon {
	struct radio_google *goog;

	u32 irq_id_to_lv2_map[MTK_IRQ_CNT_MAX];

	atomic_t pewake_flag;
	atomic_t wakeup_flag_lv1;
	atomic_t wakeup_flag_lv2;
	atomic_t wakeup_flag_lv3;
	atomic_t wakeup_reason_lv1;
	atomic_t wakeup_reason_lv2;
	atomic_t wakeup_reason_lv3;
	atomic_t wakeup_cnt;

	char wakeup_reason_lv2_desc[WAKEUP_REASON_DESC_LEN];
	char wakeup_reason_lv3_desc[WAKEUP_REASON_DESC_LEN];

	struct kernfs_node *wakeup_notify_node;

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	atomic_t mcf_network_wakeup_cnt;
	atomic_t mcf_gnss_wakeup_cnt;
	atomic_t mcf_log_wakeup_cnt;
	atomic_t mcf_control_wakeup_cnt;
	atomic_t mcf_misc_wakeup_cnt;
#endif
};

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)

int md2ap_wakemon_init(struct radio_google *goog);

void md2ap_wakemon_exit(struct radio_google *goog);

void md2ap_wakemon_pewake(struct radio_google *goog);

void md2ap_wakemon_resume(struct radio_google *goog);

void md2ap_wakemon_resume_link_on(struct radio_google *goog, u32 irq_state);

void md2ap_wakemon_pci_irq(struct radio_google *goog, u32 irq_id);

bool md2ap_wakemon_cldma_rx_check(struct radio_google *goog, u32 hw_id);

void md2ap_wakemon_cldma_rx(struct radio_google *goog, u32 rxqno, struct mtk_port *port);

void md2ap_wakemon_suspend(struct radio_google *goog);

#else

static inline int md2ap_wakemon_init(struct radio_google *goog)
{
	return 0;
}

static inline void md2ap_wakemon_exit(struct radio_google *goog)
{
}

static inline void md2ap_wakemon_pewake(struct radio_google *goog)
{
}

static inline void md2ap_wakemon_resume(struct radio_google *goog)
{
}

static inline void md2ap_wakemon_resume_link_on(struct radio_google *goog, u32 irq_state)
{
}

static inline void md2ap_wakemon_pci_irq(struct radio_google *goog, u32 irq_id)
{
}

static inline bool md2ap_wakemon_cldma_rx_check(struct radio_google *goog, u32 hw_id)
{
	return false;
}

static inline void md2ap_wakemon_cldma_rx(struct radio_google *goog, u32 rxqno,
					  struct mtk_port *port)
{
}

static inline void md2ap_wakemon_suspend(struct radio_google *goog)
{
}

#endif /* CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR */

#endif /* MD2AP_WAKEMON_H */
