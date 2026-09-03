/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * This header defines data structures shared between the AP (Application
 * Processor) driver and the NCP (Network Co-Processor) for the NOA Modem.
 * It serves as the single source of truth for the shared memory layout and
 * data path switching ABI.
 */

#ifndef __NOA_MD_SHMEM_LAYOUT_H__
#define __NOA_MD_SHMEM_LAYOUT_H__

#ifdef linux
#include <linux/types.h>
#else  /* linux */
#include "linux_port/types.h"
#endif  /* linux */

#define NOA_SHARED_MEM_ALIGNMENT 64
#define MAX_WWAN_IFINDEX_TABLE_SIZE 32

// Using constants from ncp_modem_data.h for consistency
#define NOA_MD_MAX_UL_QUEUE_SIZE 5
#define NOA_MD_MAX_DL_QUEUE_SIZE 3
#define NOA_MD_MAX_BAT_INFOS_SIZE 2
#define NOA_MD_FW_FIFO_SIZE 880

/* --- Generic Command Protocol --- */

/* Sub-commands for DATA_TRANSFER type. */
enum noa_md_shmem_data_transfer_cmd {
	NOA_MD_SHMEM_DATA_CMD_NONE = 0,
	NOA_MD_SHMEM_DATA_CMD_IFINDEX_TABLE_UPDATE,
	NOA_MD_SHMEM_DATA_CMD_CLDMA_OFFLOAD_CONFIG,
	NOA_MD_SHMEM_DATA_CMD_MAX,
};

/* Sub-commands for DEBUG type. */
enum noa_md_shmem_debug_cmd {
	NOA_MD_SHMEM_DEBUG_CMD_NONE = 0,
	NOA_MD_SHMEM_DEBUG_CMD_ENABLE,
	NOA_MD_SHMEM_DEBUG_CMD_MAX,
};

/**
 * enum noa_md_shmem_cmd_status - Status for generic path commands.
 */
enum noa_md_shmem_cmd_status {
    SHMEM_CMD_STATUS_IDLE = 0,
    SHMEM_CMD_STATUS_PENDING,
    SHMEM_CMD_STATUS_SUCCESS,
    SHMEM_CMD_STATUS_FAILED,
};

/* Specific payload for DATA_CMD_IFINDEX_TABLE_UPDATE request */
struct noa_md_shmem_data_ifindex_update_req {
	u32 table_size; /* Number of valid entries in wwan_ifindex_table */
};

/* Specific payload for DATA_CMD_IFINDEX_TABLE_UPDATE response */
struct noa_md_shmem_data_ifindex_update_resp {
	u32 status;    /* Specific result code for the update operation */
};

/* Specific payload for DATA_CMD_CLDMA_OFFLOAD_CONFIG request */
/**
 * struct noa_md_shmem_data_cldma_offload_config_req - CLDMA queue offload parameters.
 * @hif_id:  The CLDMA controller index.
 * @qno:     The hardware queue number.
 * @gpd_dpa: DPA address of the GPD ring.
 * @bd_dpa:  DPA address of the BD ring.
 * @nr_gpds: Number of GPDs in the ring.
 * @nr_bds:  Number of BDs in the ring.
 */
struct noa_md_shmem_data_cldma_offload_config_req {
	u32 hif_id;
	u32 qno;
	u64 gpd_dpa;
	u64 bd_dpa;
	u32 nr_gpds;
	u32 nr_bds;
};

/* Specific payload for DATA_CMD_CLDMA_OFFLOAD_CONFIG response */
struct noa_md_shmem_data_cldma_offload_config_resp {
	u32 status;    /* Specific result code for the update operation */
};

/*
 * Payload for DATA_TRANSFER commands.
 *
 * To add a new data sub-command:
 * 1. Add a new entry to enum noa_md_shmem_data_transfer_cmd.
 * 2. Define specific request/response structures for the sub-command.
 * 3. Add those structures to the union below.
 */
struct noa_md_shmem_data_payload {
	u32 sub_cmd;  /* From enum noa_md_shmem_data_transfer_cmd */
	union {
		struct noa_md_shmem_data_ifindex_update_req ifindex_req;
		struct noa_md_shmem_data_ifindex_update_resp ifindex_resp;
		struct noa_md_shmem_data_cldma_offload_config_req cldma_config_req;
		struct noa_md_shmem_data_cldma_offload_config_resp cldma_config_resp;
	};
};

/* Specific payload for DEBUG_CMD_ENABLE request */
struct noa_md_shmem_debug_enable_req {
	u32 enable;    /* 1 to enable, 0 to disable */
};

/* Specific payload for DEBUG_CMD_ENABLE response */
struct noa_md_shmem_debug_enable_resp {
	u32 status;    /* Specific result code for the enable operation */
};

/*
 * Payload for DEBUG commands.
 *
 * To add a new debug sub-command:
 * 1. Add a new entry to enum noa_md_shmem_debug_cmd.
 * 2. Define specific request/response structures for the sub-command.
 * 3. Add those structures to the union below.
 */
struct noa_md_shmem_debug_payload {
	u32 sub_cmd;  /* From enum noa_md_shmem_debug_cmd */
	union {
		struct noa_md_shmem_debug_enable_req enable_req;
		struct noa_md_shmem_debug_enable_resp enable_resp;
	};
};

/**
 * struct noa_md_shmem_cmd_payload - Payload for generic control plane
 * communication.
 *
 * This structure defines the format for asynchronous control commands
 * in the Generic Path. It uses a "Notify/Ack" handshake protocol.
 *
 * @status:   The result status code of the command.
 * Written by the responder before sending the ACK.
 * See enum noa_md_shmem_cmd_status.
 * @payload:  A union that holds the specific data for the command type.
 */
struct noa_md_shmem_cmd_payload {
	u32 status;
	union {
		struct noa_md_shmem_data_payload data_payload;
		struct noa_md_shmem_debug_payload debug_payload;
	} payload;
} __attribute__((aligned(NOA_SHARED_MEM_ALIGNMENT)));

static_assert(sizeof(struct noa_md_shmem_cmd_payload) == NOA_SHARED_MEM_ALIGNMENT);

/* --- Data Path Switch Protocol --- */

/**
 * enum noa_md_switch_command - Commands for data path switching.
 */
enum noa_md_switch_command {
	NOA_MD_SWITCH_CMD_NONE = 0,
	NOA_MD_SWITCH_CMD_NOTIFY_PREPARE_SWITCH,  /* Maps to SERVICE_STOPPING step */
	NOA_MD_SWITCH_CMD_EXCHANGE_STATE,         /* Maps to DEVICE_PREPARING step */
	NOA_MD_SWITCH_CMD_COMMIT_SWITCH,          /* Maps to DEVICE_RESUMING steps */
	NOA_MD_SWITCH_CMD_NOTIFY_RESTART,         /* Maps to SERVICE_RESTARTING steps */
	NOA_MD_SWITCH_CMD_ROLLBACK_SWITCH,        /* Maps to ROLLING_BACK step */
};

/**
 * enum noa_md_switch_status - Status reported by NCP in shared memory.
 */
enum noa_md_switch_status {
	SWITCH_STATUS_IDLE = 0,
	SWITCH_STATUS_PENDING,
	SWITCH_STATUS_SUCCESS,
	SWITCH_STATUS_FAILED,
};

/**
 * struct tx_ring_idx - The tx modem ring's index.
 * @drb_wr_idx: The write index of the tx modem ring.
 * @drb_rd_idx: The read index of the tx modem ring.
 * @drb_rel_rd_idx: The release index of the tx modem ring.
 */
struct tx_ring_idx {
	u32 drb_wr_idx;
	u32 drb_rd_idx;
	u32 drb_rel_rd_idx;
};

/**
 * struct rx_ring_idx - The rx modem PIT ring's index.
 * @pit_wr_idx: The write index of the modem PIT ring.
 * @pit_rd_idx: The read index of the modem PIT ring.
 * @pit_rel_rd_idx: The release index of the modem PIT ring.
 * @pit_seq_expect: The expected sequence number of the modem PIT.
 */
struct rx_ring_idx {
	u32 pit_wr_idx;
	u32 pit_rd_idx;
	u32 pit_rel_rd_idx;
	u32 pit_seq_expect;
};

/**
 * struct bat_ring - The rx modem BAT ring info.
 * @bat_wr_idx: The write index of the modem BAT ring.
 * @bat_rd_idx: The read index of the modem BAT ring.
 * @max_reload_cnt: The current max relaod BAT counts.
 * @to_reload_cnt: The BAT counts need to be reloaded.
 * @ncp_free_pool_dpa_base: The ncp free pool base of dpa.
 * @ncp_free_pool_fore: The fore index of ncp free pool.
 * @ncp_free_pool_rear: The rear index of ncp free pool.
 */
struct bat_ring {
	u32 bat_wr_idx;
	u32 bat_rd_idx;
	u32 max_reload_cnt;
	u32 to_reload_cnt;
	u64 ncp_free_pool_dpa_base;
	u32 ncp_free_pool_fore;
	u32 ncp_free_pool_rear;
};

/**
 * struct bat_infos - The rx modem BAT information.
 * @normal_bat_ring: The normal BAT ring info.
 * @frag_bat_ring: The fragment BAT ring info.
 */
struct bat_infos {
	struct bat_ring normal_bat_ring;
	struct bat_ring frag_bat_ring;
};

/**
 * struct tx_apc2ncp_ring_idx - The tx apc2ncp ring's index.
 * @drb_wr_idx: The write index of the tx apc2ncp ring.
 * @drb_rd_idx: The read index of the tx apc2ncp ring.
 * @drb_temp_rd_idx: The temp read index of the tx apc2ncp ring.
 */
struct tx_apc2ncp_ring_idx {
	u32 drb_wr_idx;
	u32 drb_rd_idx;
	u32 drb_temp_rd_idx;
};

/**
 * struct dpath_ap_state_payload - Data from AP to NCP for state exchange.
 * @bat_infos: The BAT information of normal and fragment BAT.
 * @rx_ring_idx: The final write, read, release index and seq_expect of RX modem rings.
 * @tx_ring_idx: The final write, read and release index of TX modem rings.
 */
struct dpath_ap_state_payload {
	/* TODO: b/434646935 - Define the actual content of ap_state. */
	struct bat_infos bat_infos[NOA_MD_MAX_BAT_INFOS_SIZE];
	struct rx_ring_idx rxqs[NOA_MD_MAX_DL_QUEUE_SIZE];
	struct tx_ring_idx txqs[NOA_MD_MAX_UL_QUEUE_SIZE];
};

/**
 * struct dpath_ncp_state_payload - Data from NCP to AP for state exchange.
 * @bat_infos: The BAT information of normal and fragment BAT.
 * @rx_ring_idx: The final write index of the NCP's receive ring.
 * @tx_ring_idx: The final write, read and release index of TX modem rings.
 * @tx_apc2ncp_ring_idx: The final write, read and temp_read index of TX apc2ncp rings.
 */
struct dpath_ncp_state_payload {
	/* TODO: Define the actual content of ncp_state. */
	struct bat_infos bat_infos[NOA_MD_MAX_BAT_INFOS_SIZE];
	struct rx_ring_idx rxqs[NOA_MD_MAX_DL_QUEUE_SIZE];
	struct tx_ring_idx txqs[NOA_MD_MAX_UL_QUEUE_SIZE];
	struct tx_apc2ncp_ring_idx apc2ncp_txqs[NOA_MD_MAX_UL_QUEUE_SIZE];
};

/**
 * struct noa_md_switch_payload - AP-NCP shared payload for data path
 * switching.
 * @command:   Command from AP to NCP. See enum noa_md_switch_command.
 * @target_path: Holds the final destination (from enum dpa_data_path)
 * @status:    Result status from NCP. See enum noa_md_switch_status.
 * @ap_state:  Payload for EXCHANGE_STATE command, sent by AP.
 * @ncp_state: Reply payload for EXCHANGE_STATE, sent by NCP.
 *
 * Defines the ABI for switch communication over shared memory. Must be
 * L1 cache aligned.
 */
struct noa_md_switch_payload {
	u32 command;
	u32 target_path;
	u32 status;
	struct dpath_ap_state_payload ap_state;
	struct dpath_ncp_state_payload ncp_state;
} __attribute__((aligned(NOA_SHARED_MEM_ALIGNMENT)));

struct noa_tx_queue_info {
	u32 pkt_cnt;
};

struct noa_md_tx_tkid_queue_fifo{
	u16 items[NOA_MD_FW_FIFO_SIZE];
	u32 head;
	u32 tail;
};

struct noa_rx_queue_info {
	u32 pkt_cnt;
};

/* Describes the layout of shared memory between AP and NCP. */
struct noa_md_shmem_layout {
	struct noa_tx_queue_info txqs[NOA_MD_MAX_UL_QUEUE_SIZE];
	struct noa_rx_queue_info rxqs[NOA_MD_MAX_DL_QUEUE_SIZE];

	/* Payload for the AP-NCP data path switch mechanism */
	struct noa_md_switch_payload switch_payload;

	/* Generic Data Payload for AP to NCP (AP-written, read by NCP) */
	struct noa_md_shmem_cmd_payload ap2ncp_data_payload;

	/* Generic Data Payload for NCP to AP (NCP-written, read by AP) */
	struct noa_md_shmem_cmd_payload ncp2ap_data_payload;

	/* Generic Debug Payload for AP to NCP (AP-written, read by NCP) */
	struct noa_md_shmem_cmd_payload ap2ncp_debug_payload;

	/* Generic Debug Payload for NCP to AP (NCP-written, read by AP) */
	struct noa_md_shmem_cmd_payload ncp2ap_debug_payload;

	/* Payload for the WWAN network interfaces id table */
	u32 wwan_ifindex_table[MAX_WWAN_IFINDEX_TABLE_SIZE];

	struct noa_md_tx_tkid_queue_fifo tx_tkid_queues[NOA_MD_MAX_UL_QUEUE_SIZE];

	/* Debug Feature Bootstrap (AP-written, read once by NCP) */
	u32 debug_enabled;
	u64 debug_shmem_pa;
} __attribute__((aligned(NOA_SHARED_MEM_ALIGNMENT)));

#endif /* __NOA_MD_SHMEM_LAYOUT_H__ */
