/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_WLAN_CLIENT_H__
#define __NOA_WLAN_CLIENT_H__

#include <linux/interrupt.h>
#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include "noa.h"
#include "noa_wlan_cfg_space.h"
#include "noa_wlan_buffer_management.h"
#include "noa_wlan_mapper.h"

struct noa_wlan_client;

#define noa_wlan_err(fmt, ...)                                                                     \
	do {                                                                                       \
		pr_err("%s:" fmt, "noa_wlan", ##__VA_ARGS__);                                      \
	} while (0)

#define noa_wlan_nep_ring_pkt_cnt_inc(client, name) (client->nep_ring_stats.name++)
#define noa_wlan_nep_sw_ring_rx_refill_pkt_inc(client)                                             \
	noa_wlan_nep_ring_pkt_cnt_inc(client, nep_sw_rxrefill)
#define noa_wlan_nep_sw_ring_tx_cpl_pkt_inc(client)                                                \
	noa_wlan_nep_ring_pkt_cnt_inc(client, nep_sw_txcpl)

/* WiFi driver send cmd to NOA WiFi firmware */
struct noa_wlan_fw_ops {
	int (*start)(struct noa_wlan_client *client);
	void (*stop)(struct noa_wlan_client *client);
	int (*rxbm_sync)(struct noa_wlan_client *client, void *bufs, int cnt, bool to_dev);
	int (*txq_active)(struct noa_wlan_client *client, u16 flowid, bool enable);
	int (*sta_active)(struct noa_wlan_client *client, void *info, bool enable);
	int (*notify_pm_state)(bool on);
	int (*ssr_dump)(struct noa_wlan_client *client, void *seg);
	bool (*txcpl)(struct noa_wlan_client *client, u32 *cnt);
	int (*update_up2flow)(struct noa_wlan_client *client, const u8 type, const u8 *tbl);
	int (*update_flowid_lkup_entry)(struct noa_wlan_client *client, const void *entry);
	void (*sync_pci_link_state)(struct noa_wlan_client *client, int32_t state, bool is_to_shm);
	void (*notify_station_state)(struct noa_wlan_client *client, u8 state, int iif);
	int (*rx_handover_sync)(struct noa_wlan_client *client, u64 handover_addr, u32 count);
};

/* NOA WiFi firmware send event to WiFi driver */
struct noa_wlan_client_ops {
	int (*dump_ring)(void *bus, char *buf, int len);
	int (*dump_bus)(void *bus, char *buf, int len);
	void (*sync_bus)(void *bus);
	void (*ringbell)(void *bus, u32 value);
	void (*txcpl_sync)(void *bus, void *desc);
	void (*rx_isr)(int irq);
	int (*wakeup)(bool lock);
	ssize_t (*write_payload)(void *buf, size_t buf_len, const void *data, size_t data_len);
	int (*vote_device_power)(void *bus, bool active);
	int (*sync_back_pci_dev_state)(void *bus, void *state);
	int (*manage_power)(void *bus, bool acquire);
	int (*rx_handover)(void *bus);
};

struct noa_wlan_hw {
	u32 irq;
	unsigned long ints_addr;
	unsigned long intm_addr;
	unsigned long doorbell_addr;
	struct noa_ring_regs input;
	struct noa_ring_regs output;
};

enum {
	CLIENT_FLAG_START,
	CLIENT_FLAG_MAX,
};

enum { IO_TYPE_SHARE, IO_TYPE_REG, IO_TYPE_MAX };

enum {
	NOA_MODE_NORMAL,
	NOA_MODE_AP_NCP_DIRECT,
	NOA_MODE_DISABLE,
	NOA_MODE_FEEDTHROUGH,
	NOA_MODE_VPN,
	NOA_MODE_MAX,
};

#define NOA_PCIE_BAR0_BASE 0xB0600000

struct wlan_sw_nep_ring {
	struct noa_ring_wrapper ring;
	bool own_desc; // for wcn6740, the desc is not owned by this struct.
	void *desc;
	dma_addr_t desc_dma;
	spinlock_t lock;
};

struct wlan_nep_ring_stats {
	u32 nep_sw_input[MAX_TX_RINGS_NUM];
	u32 nep_sw_output;
	u32 nep_sw_txcpl;
	u32 nep_sw_rxrefill;
};

struct wlan_irq_mapping_entry {
	uint32_t pdev_irq;
	uint32_t noa_wlan_irq;
};

struct pci_cap_saved_data {
	u16 cap_nr;
	bool cap_extended;
	unsigned int size;
	u32 data[];
};

struct pci_saved_state {
	u32 config_space[16];
	struct pci_cap_saved_data cap[];
};

struct noa_wlan_client {
	void *bus;
	struct device *dev;
	struct device *dpa_dev;
	struct platform_device *sscd_pdev;
	struct sscd_platform_data *sscd_pdata;
	u32 irqs[MAX_IRQ_NUM];
	u32 pcie_msi_apc_intr_cnt;
	u32 nep_apc_intr_cnt;
	u32 ncp_apc_intr_cnt;
	unsigned long flags;
	phys_addr_t share_addr;
	phys_addr_t reg_addr;
	u32 share_size;
	u32 reg_size;
	u32 ints_addr;
	u32 intm_addr;
	u64 doorbell_addr;
	u64 fw_trap_addr;
	u32 busstate;
	u32 rx_buf_sz;
	u32 rx_pkt_max;
	u32 rx_metadata_offset;
	u32 tx_pkt_max;
	u32 tx_flow_max;
	u32 tx_bm_sz;
	u32 rx_pkt_tlv_size;
	u32 rx_flow_max;
	u32 tx_cpl_flow_max;
	u32 rx_post_max;
	u32 dump_opt;
	u16 nep_tx_desc_sz;
	u16 nep_tx_items;
	u16 nep_rx_desc_sz;
	u16 nep_rx_items;
	u16 wdev_tx_post_desc_sz;
	u16 wdev_rx_post_desc_sz;
	u16 wdev_tx_cpl_desc_sz;
	u16 wdev_rx_cpl_desc_sz;
	u8 irq_nums;
	u64 type;
	u8 mode;
	u8 dpa_crash_state;
	void *dbg_entry;
	unsigned long buswin_flags;
	bool fw_active;
	struct noa_wlan_ring_info rx_ring[WLAN_RX_RING_MAX];
	struct noa_wlan_ring_info tx_cpl_ring[WLAN_TXCPL_MAX];
	struct noa_wlan_ring_info tx_ring[WLAN_TX_RING_MAX];
	struct noa_wlan_ring_info rx_post_ring[WLAN_RX_POST_MAX];
	struct noa_wlan_client_ops *ops;
	struct noa_wlan_fw_ops *fw_ops;
	struct wlan_sw_nep_ring rx_data_ring;
	struct wlan_sw_nep_ring tx_data_ring;
	struct wlan_sw_nep_ring rx_fallback_ring;
	struct wlan_sw_nep_ring direct_tx_cpl_ring[WLAN_TXCPL_MAX];
	struct wlan_sw_nep_ring direct_rx_data_ring;
	struct wlan_sw_nep_ring direct_tx_data_ring;
	struct wlan_sw_nep_ring vendor_rx_replenish_ring;
	struct wlan_sw_nep_ring noa_tx_replenish_ring;
	struct wlan_sw_nep_ring feedback_ring;
	struct wlan_nep_ring_stats nep_ring_stats;
	struct noa_bm_buf txbm[MAX_TXBM_BUF_NUM];
	struct noa_wlan_bm vendor_rx_bm;
	struct noa_wlan_bm noa_tx_bm;
	struct noa_wlan_hw hw;
	struct noa_wlan_cfg_space cfg;
	struct noa_wlan_mapper mapper;
	struct noa_wlan_msi_desc_t msi_descs[MAX_WLAN_PCIE_MSI_NUM];
	void *plat_priv;
};

struct buffer_repln_data {
	u8 buffer_type;
	bool to_dev;
	struct noa_bm_buf *buf;
};

typedef enum noa_wlan_intr_type {
	NOA_WLAN_INTR_TYPE_START = 0,
	NOA_WLAN_INTR_TYPE_RX = NOA_WLAN_INTR_TYPE_START,
	NOA_WLAN_INTR_TYPE_TX,
	NOA_WLAN_INTR_TYPE_MAX,
} noa_wlan_intr_type_t;

static struct wlan_irq_mapping_entry fake_brcm4390_irq_table[MAX_IRQ_NUM] = {
	[NOA_WLAN_INTR_TYPE_RX] = {
		.pdev_irq = 1002,
		.noa_wlan_irq = 202,
	},
};

static struct wlan_irq_mapping_entry brcm4390_irq_table[MAX_IRQ_NUM] = {
	[NOA_WLAN_INTR_TYPE_RX] = {
		/* With PCIe MSI enabled, the noa_wlan_irq is set to msi_index per PCIe device.
		 * Since WLAN only needs 1 irq, we can use 0 as default. */
		.noa_wlan_irq = 0,
	},
};

static struct wlan_irq_mapping_entry wcn7760_irq_table[MAX_IRQ_NUM] = {
	[NOA_WLAN_INTR_TYPE_RX] = {.noa_wlan_irq = 19},
	[NOA_WLAN_INTR_TYPE_TX] = {.noa_wlan_irq = 14},
};
static inline struct wlan_irq_mapping_entry *get_irq_table(struct noa_wlan_client *client)
{
	switch (client->type) {
	case WLAN_FW_TYPE_BRCM_4389:
	case WLAN_FW_TYPE_BRCM_4390:
		return brcm4390_irq_table;
	case WLAN_FW_TYPE_FAKE_BRCM_4389:
	case WLAN_FW_TYPE_FAKE_BRCM_4390:
		return fake_brcm4390_irq_table;
	case WLAN_FW_TYPE_QCA:
		return wcn7760_irq_table;
	default:
		return NULL;
	}
}

static inline int noa_wlan_fw_start(struct noa_wlan_client *client)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return -EINVAL;
	}

	if (!client->fw_ops || !client->fw_ops->start) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return -ENODEV;
	}

	return client->fw_ops->start(client);
}

static inline void noa_wlan_fw_stop(struct noa_wlan_client *client)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return;
	}

	if (!client->fw_ops || !client->fw_ops->stop) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return;
	}

	client->fw_ops->stop(client);
}

static inline int noa_wlan_fw_rxbm_sync(struct noa_wlan_client *client, void *bufs, int cnt,
					bool to_dev)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return -EINVAL;
	}

	if (!client->fw_ops || !client->fw_ops->rxbm_sync) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return -ENODEV;
	}

	return client->fw_ops->rxbm_sync(client, bufs, cnt, to_dev);
}

static inline int noa_wlan_fw_rx_handover_sync(struct noa_wlan_client *client, u64 handover_addr,
					       u32 count)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return -EINVAL;
	}

	if (!client->fw_ops || !client->fw_ops->rx_handover_sync) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return -ENODEV;
	}

	return client->fw_ops->rx_handover_sync(client, handover_addr, count);
}

static inline int noa_wlan_fw_txq_active(struct noa_wlan_client *client, u16 flowid, bool enable)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return -EINVAL;
	}

	if (!client->fw_ops || !client->fw_ops->txq_active) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return -ENODEV;
	}

	return client->fw_ops->txq_active(client, flowid, enable);
}

static inline int noa_wlan_fw_sta_active(struct noa_wlan_client *client, void *info, bool enable)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return -EINVAL;
	}

	if (!client->fw_ops || !client->fw_ops->sta_active) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return -ENODEV;
	}

	return client->fw_ops->sta_active(client, info, enable);
}

static inline bool noa_wlan_fw_txcpl(struct noa_wlan_client *client, u32 *cnt)
{
	if (!client) {
		noa_wlan_err("%s(): invalid NOA WLAN client.", __func__);
		return false;
	}

	if (!client->fw_ops || !client->fw_ops->txcpl) {
		noa_wlan_err("%s(): FW operation is not registered.", __func__);
		return false;
	}

	return client->fw_ops->txcpl(client, cnt);
}

extern struct noa_wlan_client *noa_wlan_client_alloc(struct noa_wlan_client_ops *ops);
extern void noa_wlan_client_free(struct noa_wlan_client *client);
extern int noa_wlan_client_register(struct noa_wlan_client *client);
extern void noa_wlan_client_unregister(struct noa_wlan_client *client);
extern int noa_wlan_irq_request(struct noa_wlan_client *client);
extern int noa_wlan_add_session_entry(struct noa_wlan_client *client, u8 *pkt,
				      struct noa_session_info *txinfo);
extern void noa_wlan_client_obj_register(void *dpa, void *vendor, void *bus, void *dev,
					 void **cur_ops);
extern void noa_wlan_client_obj_unregister(void);
extern void noa_wlan_dynamic_switch(bool noa_en);
extern struct noa_wlan_switch_manager *noa_wlan_get_switch_manager(void);
extern int noa_wlan_client_pci_dev_state_sync(struct noa_wlan_client *client, bool save_to_shm);
#endif /*__NOA_WLAN_CLIENT_H__*/
