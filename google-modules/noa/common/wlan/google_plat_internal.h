#ifndef GOOGLE_PLAT_INTERNAL_H_
#define GOOGLE_PLAT_INTERNAL_H_

#define PRIORITY_CLASS 8
#define MAC_ADDR_LEN 6
#define STA_INFO_COMM 28
#define STA_INFO_PRIVATE 8

struct pci_dev_reconcile_fields {
	u8 is_busmaster;
	int8_t enable_cnt;
	u8 current_state;
	u16 saved_state_size;
	void *saved_state;
} __attribute__((packed, aligned(4)));

struct flow_id_entry_update {
	u16 flowid;
	u8 prio;
	u8 da[MAC_ADDR_LEN];
	u8 ifindex;
	u32 oif;
	u8 role;
	u8 is_add; /* 1: add, 0: remove */
} __attribute__((packed, aligned(4)));

struct sta_info {
	/* commom */
	u32 oif;
	u16 bss_idx;
	u16 qos_txq_map[PRIORITY_CLASS];
	u8 addr[MAC_ADDR_LEN];
	/* private */
	u8 encrypt_type : 4, encap_type : 2, lmac_id : 2;
	u8 bmid;
	u16 fw_metadata;
	u32 search_idx : 20, search_type : 2, dscp_tid_map_id : 6, addry_en : 1, addrx_en : 1,
		reserved2 : 2;
} __attribute__((packed, aligned(4)));
static_assert((STA_INFO_COMM + STA_INFO_PRIVATE) == sizeof(struct sta_info));

struct platform_bus_ops {
	int (*init)(void *pdev, void *priv);
	void (*exit)(void *priv);
	int (*start)(void *priv);
	int (*request_irq)(void *priv);
	void (*stop)(void *priv);
	bool (*rx)(void *priv, u32 *cnt);
	int (*rx_replenish)(void *priv, u32 cnt, bool use_rsv_pktid);
	int (*tx)(void *priv, void *pkt, u32 ifidx);
	bool (*tx_cpl)(void *priv, u32 *cnt);
	int (*tx_queue_active)(void *priv, u16 flowid, bool enable);
	int (*sta_active)(void *priv, struct sta_info *info, bool enable);
	bool (*check_suspend)(void);
	void (*notify_resume)(void);
	void (*ssr_dump)(void *seg);
	void (*update_up2flow)(const uint8_t type, const uint8_t *tbl);
	void (*update_flowid_lkup_entry)(struct flow_id_entry_update *entry);
	void (*sync_pci_link_state)(int32_t state, bool is_to_shm);
	void (*notify_station_state)(uint8_t state, void *dhd_pub, int iflist_idx);
	int (*rx_handover)(void *priv);
};

#endif // GOOGLE_PLAT_INTERNAL_H_
