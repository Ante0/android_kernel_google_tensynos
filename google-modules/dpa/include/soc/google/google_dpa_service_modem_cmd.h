// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_SERVICE_MODEM_CMD_H
#define _GOOGLE_DPA_SERVICE_MODEM_CMD_H

#include "modem_cmd_service.pb.h"

#define NOA_MD_MAX_UL_QUEUE_SIZE 5
#define NOA_MD_MAX_DL_QUEUE_SIZE 3
#define NOA_MD_MAX_BAT_SIZE 2
#define NOA_MD_MAX_FRAG_BAT_SIZE 2

struct tx_buffer_pool {
	uint64_t va_base;
	uint64_t pa_base;
};

struct noa_txbm {
	uint64_t drb_base;
	uint64_t drb_dpa_base;
	int32_t drb_cnt;
	int32_t burst_submit_cnt;
	int32_t db_delay_ms;
	int32_t drb_wr_idx;
	int32_t drb_rd_idx;
	int32_t drb_rel_rd_idx;
};

struct noa_rxbm {
	uint64_t pit_base;
	uint64_t pit_dpa_base;
	int32_t pit_cnt;
	int32_t pit_seq_max;
	int32_t bat_ring_id;
	int32_t pit_wr_idx;
	int32_t pit_rd_idx;
	int32_t pit_rel_rd_idx;
};

struct noa_batbm {
	uint64_t bat_base;
	uint64_t bat_dpa_base;
	int32_t buf_size;
	int32_t bat_cnt;
	int32_t reload_cnt;
	int32_t bat_wr_idx;
	int32_t bat_rd_idx;
	uint64_t mask_table_dpa_base;
	uint64_t tkid_table_dpa_base;
	uint64_t buffer_table_dpa_base;
};

struct noa_bat {
	int32_t max_mtu;
	bool frag_bat_enabled;
	struct noa_batbm normal_bat;
	struct noa_batbm frag_bat;
};

struct noa_shared_memory_info {
	uint64_t addr;
	uint32_t size;
};

struct noa_md_fw_init {
	struct noa_txbm txqs[NOA_MD_MAX_UL_QUEUE_SIZE];
	struct noa_rxbm rxqs[NOA_MD_MAX_DL_QUEUE_SIZE];
	struct noa_bat bat_infos[NOA_MD_MAX_BAT_SIZE];
	struct tx_buffer_pool pool;
	struct noa_shared_memory_info shared_mem_info;
	const noa_service_modem_cmd_service_HifConfig *hif_config;
};

/* dpa_rpc_modem_cmd_service_fw_init - pass fw_init info to NCP.
 * @dev: struct device handle
 * @fw_info: fw init info.
 */
int dpa_rpc_modem_cmd_service_fw_init(struct device *dev, const struct noa_md_fw_init *fw_info);

#endif /* _GOOGLE_DPA_SERVICE_MODEM_CMD_H */
