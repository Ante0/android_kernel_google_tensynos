/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
// NOLINTBEGIN(readability-identifier-naming)
#ifndef __NOA_WLAN_H__
#define __NOA_WLAN_H__

#include <common/core.h>
#include <common/noa_hw_ring.h>
#ifdef linux
#else
#include "linux_port/types.h"
#endif

#define WLAN_TX_RING_MAX 40
#define WLAN_RX_RING_MAX 5
#define WLAN_RX_POST_MAX 3
#define WLAN_TXCPL_MAX 5
#define WLAN_RING_MAX WLAN_TX_RING_MAX
#define PRIORITY_CLASS 8
#define MAC_ADDR_LEN 6
#define MAX_STA_SUPPORT 10
#define MAX_TXBM_BUF_NUM 4096
#define MAX_TXBUF_SIZE 1600

#define MAX_NAME_SIZE 32
#define MAX_IRQ_NUM 8

#define MAX_SHELL_CMD_LEN 128
#define MAX_SHELL_ARGV_LEN 128

#define MAX_WLAN_PCIE_MSI_NUM 1

#define	MAXPRIO			7
#define NUMPRIO			(MAXPRIO + 1)

#define ETH_MAC_LEN		6

// The ragne of noncache region is defined in the dts/malibu-dpa.dtsi.
#define NONCACHE_IOVA_START 0x98000000
#define NONCACHE_IOVA_SIZE 0x8000000
#define NONCACHE_IOVA_END (0x98000000 + NONCACHE_IOVA_SIZE - 1)

enum {
	WLAN_FW_TYPE_BRCM_4389,
	WLAN_FW_TYPE_BRCM_4390,
	WLAN_FW_TYPE_FAKE_BRCM_4389,
	WLAN_FW_TYPE_FAKE_BRCM_4390,
	WLAN_FW_TYPE_QCA,
	WLAN_FW_TYPE_MAX,
};

struct noa_bm_buf {
	u16 pktid;
	u16 len;
	u64 pa;
	u64 apc_va;
	u64 dpa_va;
} __aligned(4);
#define WLAN_PKT_PAD 60

enum {
	NOA_WLAN_CMD_RXBM_SYNC,
	NOA_WLAN_CMD_FW_INIT,
	NOA_WLAN_CMD_FW_START,
	NOA_WLAN_CMD_FW_EXIT,
	NOA_WLAN_CMD_FW_STOP,
	NOA_WLAN_CMD_RING_UPDATE,
	NOA_WLAN_CMD_TX_RING_ACTIVE,
	NOA_WLAN_CMD_STA_ACTIVE,
	NOA_WLAN_CMD_REG_RECEIVER,
	NOA_WLAN_CMD_ISR_REGISTER,
	NOA_WLAN_CMD_TXBM_SYNC,
	NOA_WLAN_CMD_CLIENT_DEV,
	NOA_WLAN_CMD_NOA_MODE_CTRL,
	NOA_WLAN_CMD_MOCK_BM_SYNC_DOORBELL,
	NOA_WLAN_CMD_LOG_SYS_CTRL,
	NOA_WLAN_CMD_PACKET_SNIFFER_CTRL,
	NOA_WLAN_CMD_SHARE_INFO_CTRL,
	NOA_WLAN_CMD_REPORT_CTRL,
	NOA_WLAN_CMD_SHELL_REQUEST,
	NOA_WLAN_CMD_PM_STATE_NOTIFY,
	NOA_WLAN_CMD_PACKET_SNIFFER_RESET,
	NOA_WLAN_CMD_UPDATE_UP_2_FLOW,
	NOA_WLAN_CMD_UPDATE_FLOWID_LOOK_UP_ENTRY,
	NOA_WLAN_CMD_RX_HANDOVER_SYNC,
	NOA_WLAN_CMD_MAX,
};

enum {
	NOA_WLAN_EVENT_DOORBELL = 0,
	NOA_WLAN_EVENT_RXBM_SYNC,
	NOA_WLAN_EVENT_TXCPL_SYNC,
	NOA_WLAN_EVENT_WAKEUP,
	NOA_WLAN_EVENT_MOCK_RING_DOORBELL_TO_APC,
	NOA_WLAN_EVENT_BUS_POWER,
	NOA_WLAN_EVENT_DEVICE_POWER,
	NOA_WLAN_EVENT_CMD_COMPLETION,
	NOA_WLAN_EVENT_PACKET_SNIFFER_FULL,
	NOA_WLAN_EVENT_MAX,
};

struct noa_wlan_cmd_fw_init {
	u32 rx_pkt_max;
	u32 tx_pkt_max;
	u32 rx_buf_sz;
	u32 tx_bm_sz;
	u32 type;
	u64 noa_shared_mem_addr;
	u32 noa_shared_mem_size;
};

struct noa_wlan_rx_handover_item {
	u16 tkid;
	u16 buf_size;
	u32 rsvd;
	u64 host_pa;
	u64 dpa_addr;
};

struct noa_wlan_cmd_rx_handover_sync {
	u64 handover_table_dpa_addr;
	u32 count;
	u32 rsvd;
};

struct noa_wlan_msi_desc_t {
	u32 nvec_used;
	u16 msi_index;
	u32 data;
};

struct noa_wlan_cmd_irq_request {
	u32 irq_nums;
	u32 irqs[MAX_IRQ_NUM];
	// PCIe MSI interrupt mapping table, only available in real device
	struct noa_wlan_msi_desc_t msi_descs[MAX_WLAN_PCIE_MSI_NUM];
};

struct noa_wlan_cmd_fw_start {
	u64 share_addr;
	u64 reg_addr;
	u32 ints_addr;
	u32 intm_addr;
	u32 share_size;
	u32 reg_size;
};

#define RING_MAX_NAME 32
struct noa_wlan_ring_info {
	struct noa_ring_regs regs;
	u32 ndesc;
	u32 desc_sz;
	u32 offset;
	u32 sn;
	u8 hw_idx;
	u8 stride;
	char name[RING_MAX_NAME];
	/* driver mode simulator only */
	u64 dma_va;
	u64 dma_pa;
	// used for dump ring read, write value.
	struct noa_ring_regs cpu_regs;
};

enum {
	RING_TYPE_TX_DATA,
	RING_TYPE_RX_DATA,
	RING_TYPE_TX_CPL,
	RING_TYPE_RX_POST,
};

struct noa_wlan_cmd_ring_info {
	u32 ring_type;
	u32 count;
	struct noa_wlan_ring_info info[WLAN_RING_MAX];
};

struct noa_wlan_cmd_bm {
	u32 count;
	bool to_dev;
	struct noa_bm_buf bufs[MAX_TXBM_BUF_NUM];
};

struct noa_wlan_shell_request {
	char cmd[MAX_SHELL_CMD_LEN];
	uint32_t argv_len;
	char argv[MAX_SHELL_ARGV_LEN];
};

struct noa_wlan_cmd_update_up2flow_table {
	u8 type; // TID or AC
	u8 table[NUMPRIO];
} __attribute__((packed, aligned(4)));

struct noa_wlan_cmd_update_flowid_lkup_entry {
	u16 flowid;
	u8 prio;
	u8 da[ETH_MAC_LEN];
	u8 ifindex;
	u32 oif;
	u8 role;
	u8 is_add; // 1: add, 0: delete
} __attribute__((packed, aligned(4)));

struct noa_bcm_txd {
	u8 flags;
	u8 ext_flags;
	u8 ifidx;
	u8 current_phase;
	u8 ring_id;
	u8 ext_tag;
	u8 pkt_csum_type;
	u8 l3_hdr_len;
	u8 l4_hdr_len;
	u8 reserved;
	u16 ethertype;
	struct noa_tx_ipsec_metadata ipsec_metadata;
} __attribute__((packed, aligned(4)));

struct noa_qca_txd {
	u8 encrypt_type : 4, encap_type : 2, l3_checksum_en : 1, l4_checksum_en : 1;
	u8 set_hlos_tid : 4, to_fw : 1, ring_id : 3;
	u8 bmid;
	u8 frag;
	u32 search_index : 30, addry_en : 1, addrx_en : 1;
} __attribute__((packed, aligned(4)));

struct noa_qca_wcn7760_txd {
	/* DW 0: Control & Priority */
	uint8_t set_hlos_tid   : 4, // Priority (TID)
	        to_fw          : 1, // Route to Firmware instead of Air
	        l3_checksum_en : 1, // IPv4 Checksum offload
	        l4_checksum_en : 1, // TCP/UDP Checksum offload
	        rsvd1          : 1;

	uint8_t ring_id;            // Target TCL ring index
	uint8_t bmid;               // Buffer Manager ID
	uint8_t frag;               // Fragmentation indicator

	/* DW 1: Peer Context & QoS */
	uint32_t search_index    : 20, // AST index for peer lookup
	         cache_set_num   :  4, // AST Cache Set (ast_hash & 0xF)
	         bank_id         :  6, // Beryllium DSCP-to-TID mapping bank
	         rsvd2           :  2;

	/* DW 2: Interface & Alignment */
	uint8_t vdev_id;               // Virtual Device ID (for AP+STA)
	uint8_t tx_notify_frame : 3,   // Hardware notification control
	        pmac_id         : 2,
	        rsvd3           : 3;
	uint16_t fw_metadata;          // exception metadata
} __attribute__((packed, aligned(4)));


struct noa_wlan_cmd_pm_state_notify {
	u32 power_state;
};

struct noa_wlan_buffer_repln_desc {
	u8 to_dev;
	u8 rsv[3];
	u16 tkid;
	u16 len;
	u64 pa;
	u64 dpa_va;
} __attribute__((packed, aligned(4)));

extern int __noa_wlan_fw_request_send_sim(int cmd, void *msg);
#endif /* __NOA_WLAN_H__ */
// NOLINTEND(readability-identifier-naming)
