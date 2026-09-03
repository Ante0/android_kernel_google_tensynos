/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __EXYNOS_BCM_DBG_PRIV_H
#define __EXYNOS_BCM_DBG_PRIV_H

#include <soc/google/exynos-bcm_dbg.h>

void exynos_bcm_dbg_set_base_info(struct exynos_bcm_ipc_base_info *ipc_base_info,
				  enum exynos_bcm_event_id event_id,
				  enum exynos_bcm_event_dir direction,
				  enum exynos_bcm_ip_range ip_range);

int exynos_bcm_dbg_run_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
			    unsigned int *bcm_ip_run,
			    struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_period_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
			       unsigned int *bcm_period,
			       struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_mode_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
			     unsigned int *bcm_mode,
			     struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_ip_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
			   unsigned int *bcm_ip_enable,
			   unsigned int bcm_ip_index,
			   struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_pause_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
			      unsigned int *bcm_ip_pause,
			      unsigned int bcm_ip_index,
			      struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_event_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
			      struct exynos_bcm_event *bcm_event,
			      unsigned int bcm_ip_index,
			      struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_sample_id_ctrl(struct exynos_bcm_ipc_base_info *ipc_base_info,
				  struct exynos_bcm_sample_id *sample_id,
				  unsigned int bcm_ip_index,
				  struct exynos_bcm_dbg_data *data);

int exynos_bcm_dbg_dump_accumulators_ctrl(
			struct exynos_bcm_ipc_base_info *ipc_base_info,
			char *buf, size_t *buf_len, loff_t off, size_t size,
			struct exynos_bcm_dbg_data *data);

void exynos_bcm_dbg_set_dump(bool enable_klog, bool enable_file,
			     struct exynos_bcm_dbg_data *data);

#endif /* __EXYNOS_BCM_DBG_PRIV_H */
