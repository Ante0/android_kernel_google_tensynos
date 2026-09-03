/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __NOA_MD_DPA_DOORBELL_H__
#define __NOA_MD_DPA_DOORBELL_H__

/* Enum to specify the doorbell type for ISR registration */
enum noa_md_dpa_doorbell_type {
	NOA_MD_DPA_NCP_DOORBELL,
	NOA_MD_DPA_NEP_DOORBELL,
	NOA_MD_DPA_DOORBELL_TYPE_MAX
};

/**
 * enum noa_md_apc_to_ncp_doorbell - Doorbell IDs from APC to NCP.
 *
 * Defines the interrupt IDs (reasons) used when APC rings a doorbell to notify
 * the NCP. These are typically commands for the NCP to start work.
 *
 * The values must align with the ring IDs defined in modem_ring_id.h and
 * the mailbox definitions. The hardware provides a total of 32 doorbell
 * IDs (0-31).
 */
enum noa_md_apc_to_ncp_doorbell {
	/* Notify NCP of new packets in TX rings */
	NOA_MD_APC2NCP_TX_DRB0 = 0,
	NOA_MD_APC2NCP_TX_DRB1,
	NOA_MD_APC2NCP_TX_DRB2,
	NOA_MD_APC2NCP_TX_DRB3,
	NOA_MD_APC2NCP_TX_DRB4,

	/* Notify NCP of refilled RX buffers */
	NOA_MD_APC2NCP_RX_REFILL_NORMAL_BAT0,  /* 5 */
	NOA_MD_APC2NCP_RX_REFILL_FRAG_BAT0,    /* 6 */
	NOA_MD_APC2NCP_RX_REFILL_NORMAL_BAT1,  /* 7 */
	NOA_MD_APC2NCP_RX_REFILL_FRAG_BAT1,    /* 8 */

	/* Control path commands */
	/**
	 * @NOA_MD_APC2NCP_SWITCH_CTRL: APC commands the data path switch.
	 *  (Index 9)
	 */
	NOA_MD_APC2NCP_SWITCH_CTRL,  /* 9 */

	/* Generic Shared Memory Control Path: For control plane */
	NOA_MD_APC2NCP_SHMEM_DATA_NOTIFY,   /* 10 */
	NOA_MD_APC2NCP_SHMEM_DATA_ACK,      /* 11 */

	/* Generic Shared Memory Control Path: For debug plane */
	NOA_MD_APC2NCP_SHMEM_DEBUG_NOTIFY,  /* 12 */
	NOA_MD_APC2NCP_SHMEM_DEBUG_ACK,     /* 13 */

	NOA_MD_APC2NCP_DOORBELL_MAX = 32
};

#define NOA_MD_APC2NCP_SHMEM_IFINDEX_UPDATE 10

/**
 * enum noa_md_ncp_to_apc_doorbell - Doorbell IDs from NCP to APC.
 *
 * Defines the interrupt IDs (reasons) used when the NCP rings a doorbell to
 * notify the APC. These are typically completion, error, or attention events.
 * The hardware provides a total of 32 doorbell IDs (0-31).
 */
enum noa_md_ncp_to_apc_doorbell {
	/* TX completions */
	NOA_MD_NCP2APC_TX_DONE_DRB0 = 0,
	NOA_MD_NCP2APC_TX_DONE_DRB1,
	NOA_MD_NCP2APC_TX_DONE_DRB2,
	NOA_MD_NCP2APC_TX_DONE_DRB3,
	NOA_MD_NCP2APC_TX_DONE_DRB4,

	/* RX errors */
	NOA_MD_NCP2APC_BAT0_LEN_ERR,      /* 5 */
	NOA_MD_NCP2APC_FRAGBAT0_LEN_ERR,  /* 6 */
	NOA_MD_NCP2APC_BAT1_LEN_ERR,      /* 7 */
	NOA_MD_NCP2APC_FRAGBAT1_LEN_ERR,  /* 8 */

	/* Control path events */
	/**
	 * @NOA_MD_NCP2APC_SWITCH_CTRL_EVENT: NCP event (e.g., ACK) for
	 * the data path switch handshake. (Index 9)
	 */
	NOA_MD_NCP2APC_SWITCH_CTRL_EVENT,  /* 9 */

	/* Generic Shared Memory Control Path: For control plane */
	NOA_MD_NCP2APC_SHMEM_DATA_NOTIFY,   /* 10 */
	NOA_MD_NCP2APC_SHMEM_DATA_ACK,      /* 11 */

	/* Generic Shared Memory Control Path: For debug plane */
	NOA_MD_NCP2APC_SHMEM_DEBUG_NOTIFY,  /* 12 */
	NOA_MD_NCP2APC_SHMEM_DEBUG_ACK,     /* 13 */

	/**
	 * @NOA_MD_NCP2APC_EXCEPTION: NCP reports a critical exception
	 * to the APC. (Index 14)
	 */
	NOA_MD_NCP2APC_EXCEPTION,  /* 14 */

	NOA_MD_NCP2APC_DOORBELL_MAX = 32
};


#endif /* __NOA_MD_DPA_DOORBELL_H__ */
