// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __METRICS_COLLECTION_H__
#define __METRICS_COLLECTION_H__

#include <linux/types.h>

/* Metric - PCIe link state */
struct mcf_pcie_link_state_info {
	u32 link_state;
};

/* common duration information */
struct mcf_duration_info {
	u64 count;
	u64 duration_ms;
	u64 last_entry_ms;
};

/* Metric - PCIe up/down statistics */
struct mcf_pcie_link_updown_info {
	struct mcf_duration_info link_up;
	struct mcf_duration_info link_down;
};

/* Max supported PCIe link gen speed */
#define MCF_MAX_PCIE_LINK_SPEED (4)

/* Metric - PCIe link duration statistics */
struct mcf_pcie_link_duration_info {
	u32 last_link_speed;
	struct mcf_duration_info speed[MCF_MAX_PCIE_LINK_SPEED];
};

/* Metric - PCIe link error/recovery statistics */
struct mcf_pcie_link_stats_info {
	u64 link_up_failure_count;
	u64 link_recovery_failure_count;
	u64 link_down_irq_count;
	u64 cmpl_timeout_irq_count;
	u64 link_up_time_avg;
};

/**
 * @brief AP wakeup by modem source type id
 */
enum mcf_modem_wakeup_src_id {
	WAKEUP_SRC_ID_NETWORK,
	WAKEUP_SRC_ID_GNSS,
	WAKEUP_SRC_ID_LOG,
	WAKEUP_SRC_ID_CONTROL,
	WAKEUP_SRC_ID_MISC,
	WAKEUP_SRC_MAX_ID,
};

/**
 * @brief Metric - AP wakeup by modem statistics
 */
struct mcf_modem_wakeup_ap_stats {
	u64 counts[WAKEUP_SRC_MAX_ID];
};

/**
 * @brief Possilbe modem boot type
 */
enum modem_boot_type {
	MODEM_BOOT_TYPE_NORMAL,
	MODEM_BOOT_TYPE_WARM_RESET,
	MODEM_BOOT_TYPE_PARTIAL_RESET,
	MODEM_BOOT_TYPE_DUMP,
	MODEM_BOOT_TYPE_MAX,
};

/**
 * @brief Mask of possible modem boot type
 * @details These masks are used to represent multiple boot types in one value.
 */
#define MODEM_BOOT_TYPE_MASK_NORMAL (1U << MODEM_BOOT_TYPE_NORMAL)
#define MODEM_BOOT_TYPE_MASK_WARM_RESET (1U << MODEM_BOOT_TYPE_WARM_RESET)
#define MODEM_BOOT_TYPE_MASK_PARTIAL_RESET (1U << MODEM_BOOT_TYPE_PARTIAL_RESET)
#define MODEM_BOOT_TYPE_MASK_DUMP (1U << MODEM_BOOT_TYPE_DUMP)

/* Callback function to query PCIe link state */
typedef int (*mcf_pull_pcie_link_state_cb_t)(
	struct mcf_pcie_link_state_info *data, void *priv);

/* Callback function to query PCIe link updown statistics */
typedef int (*mcf_pull_pcie_link_updown_cb_t)(
	struct mcf_pcie_link_updown_info *data, void *priv);

/* Callback function to query PCIe link duration statistics */
typedef int (*mcf_pull_pcie_link_duration_cb_t)(
	struct mcf_pcie_link_duration_info *data, void *priv);

/* Callback function to query PCIe link error/recovery statistics */
typedef int (*mcf_pull_pcie_link_stats_cb_t)(
	struct mcf_pcie_link_stats_info *data, void *priv);

/* Callback function to query modem wakeup ap statistics */
typedef int (*mcf_pull_modem_wakeup_ap_cb_t)(
	struct mcf_modem_wakeup_ap_stats *data, void *priv);

/* Register query pcie link state function to metrics collection framework */
int mcf_register_pcie_link_state(mcf_pull_pcie_link_state_cb_t callback,
				 void *priv);
int mcf_unregister_pcie_link_state(mcf_pull_pcie_link_state_cb_t callback,
				   void *priv);

/* Register query pcie link updown function to metrics collection framework */
int mcf_register_pcie_link_updown(mcf_pull_pcie_link_updown_cb_t callback,
				  void *priv);
int mcf_unregister_pcie_link_updown(mcf_pull_pcie_link_updown_cb_t callback,
				    void *priv);

/* Register query pcie link duration function to metrics collection framework */
int mcf_register_pcie_link_duration(mcf_pull_pcie_link_duration_cb_t callback,
				    void *priv);
int mcf_unregister_pcie_link_duration(mcf_pull_pcie_link_duration_cb_t callback,
				      void *priv);

/* Register query pcie link error/recovery statistics function to
 * metrics collection framework
 */
int mcf_register_pcie_link_stats(mcf_pull_pcie_link_stats_cb_t callback,
				 void *priv);
int mcf_unregister_pcie_link_stats(mcf_pull_pcie_link_stats_cb_t callback,
				   void *priv);

/* Register query modem wakeup ap statistics function to metrics collection
 * framework
 */
int mcf_register_modem_wakeup_ap(mcf_pull_modem_wakeup_ap_cb_t callback,
				 void *priv);
int mcf_unregister_modem_wakeup_ap(mcf_pull_modem_wakeup_ap_cb_t callback,
				   void *priv);

/**
 * @brief Notify the start point of modem boot
 *
 * @param[in] boot_types_mask group of possible boot types, it will be combin
 * with the group in the @ref mcf_notify_modem_boot_end to determine the
 * real boot type.
 * @param[in] fail_type When a boot failure is detected, record the failure to
 * this default boot type.
 * @return 0 on success or error on failure
 */
int mcf_notify_modem_boot_start(u32 boot_types_mask,
				enum modem_boot_type fail_type);

/**
 * @brief Notify the end point of modem boot
 *
 * @param[in] boot_types_mask group of possible boot types, combin with
 * previous group in the @ref mcf_notify_modem_boot_start to determine the real
 * boot type.
 * @return 0 on success or error on failure
 */
int mcf_notify_modem_boot_end(u32 boot_types_mask);

#endif //__METRICS_COLLECTION_H__
