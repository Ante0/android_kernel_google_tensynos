#include "noa_wlan_cfg_space.h"
#include "common/wlan/memory_map.h"
#include "noa_wlan_client.h"

static void noa_wlan_ring_config_init(struct noa_wlan_client *client)
{
	noa_wlan_shm_ring_config_t *ring_config =
		(noa_wlan_shm_ring_config_t *)noa_wlan_cfg_space_get_section_va(client,
										RING_CONFIG);

	if (ring_config == NULL) {
		return;
	}

	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, WdevTxPost);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, WdevTxCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, WdevRxPost);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, WdevRxCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, WdevCtrlPost);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, WdevCtrlCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, NepTxPost);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, NepRxCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, NepTxCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, NepFeedback);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcDirectTxPost);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcDirectRxCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcTxCmpl);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcVendorRxBufRepln);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcNoaTxBufRepln);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcFeedback);
	NOA_WLAN_SHM_INIT_RING_GROUP(ring_config, ApcFallbackRxCmpl);
}

int noa_wlan_cfg_space_init(struct noa_wlan_client *client)
{
	struct noa_wlan_cfg_space *cfg = &client->cfg;

	memset(cfg, 0, sizeof(struct noa_wlan_cfg_space));

	cfg->base_va = dma_alloc_coherent(client->dpa_dev, sizeof(struct NoaWlanSharedInfo),
					  &cfg->base_pa, GFP_KERNEL);
	if (!cfg->base_va) {
		return -ENOMEM;
	}
	memset(cfg->base_va, 0, sizeof(struct NoaWlanSharedInfo));
	cfg->size = sizeof(struct NoaWlanSharedInfo);

	noa_wlan_ring_config_init(client);

	return 0;
}

void noa_wlan_cfg_space_deinit(struct noa_wlan_client *client)
{
	struct noa_wlan_cfg_space *cfg = &client->cfg;

	if (cfg->base_va) {
		dma_free_coherent(client->dpa_dev, cfg->size, cfg->base_va, cfg->base_pa);
	}

	memset(cfg, 0, sizeof(struct noa_wlan_cfg_space));
}

void *noa_wlan_cfg_space_get_base_va(struct noa_wlan_client *client)
{
	return client->cfg.base_va;
}

dma_addr_t noa_wlan_cfg_space_get_base_pa(struct noa_wlan_client *client)
{
	return client->cfg.base_pa;
}

void *noa_wlan_cfg_space_get_section_va(struct noa_wlan_client *client,
					enum noa_wlan_cfg_space_section section)
{
	switch (section) {
	case GLOBAL_CONFIG:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, global_config));
	case RING_CONFIG:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, ring_config));
	case WDEV_RING_CONFIG:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, wifi_ring_config));
	case NEP_RING_CONFIG:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, nep_ring_config));
	case STA_INFO:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, sta_info));
	case PM_STATE_INFO:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, pm_state_info));
	case INTR_STATE_INFO:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, interrupt_state_info));
	case MIB_INFO:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, mib));
	case WIT_LOG_SYS:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, wit_shared_log_sys_addr));
	case WIT_PACKET_SNIFFER:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, wit_shared_packet_addr));
	case BUFFER_MGMT:
		return (void *)(noa_wlan_cfg_space_get_base_va(client) +
				offsetof(noa_wlan_shared_info_t, buffer_management_shared_memory));
	default:
		break;
	}

	return NULL;
}

dma_addr_t noa_wlan_cfg_space_get_section_pa(struct noa_wlan_client *client,
					     enum noa_wlan_cfg_space_section section)
{
	switch (section) {
	case GLOBAL_CONFIG:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, global_config));
	case RING_CONFIG:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, ring_config));
	case WDEV_RING_CONFIG:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, wifi_ring_config));
	case NEP_RING_CONFIG:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, nep_ring_config));
	case STA_INFO:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, sta_info));
	case PM_STATE_INFO:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, pm_state_info));
	case INTR_STATE_INFO:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, interrupt_state_info));
	case MIB_INFO:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, mib));
	case WIT_LOG_SYS:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, wit_shared_log_sys_addr));
	case WIT_PACKET_SNIFFER:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t, wit_shared_packet_addr));
	case BUFFER_MGMT:
		return (dma_addr_t)(noa_wlan_cfg_space_get_base_pa(client) +
				    offsetof(noa_wlan_shared_info_t,
					     buffer_management_shared_memory));
	default:
		break;
	}

	return 0;
}

size_t noa_wlan_cfg_space_get_section_size(struct noa_wlan_client *client,
					   enum noa_wlan_cfg_space_section section)
{
	switch (section) {
	case GLOBAL_CONFIG:
		return sizeof(noa_wlan_global_config_t);
	case WDEV_RING_CONFIG:
		return sizeof(noa_wlan_wifi_ring_config_t);
	case NEP_RING_CONFIG:
		return sizeof(noa_wlan_nep_ring_config_t);
	case STA_INFO:
		return sizeof(noa_wlan_sta_info_t) * NOA_MAX_STA_SUPPORT;
	case PM_STATE_INFO:
		return sizeof(noa_wlan_pm_state_info_t);
	case INTR_STATE_INFO:
		return sizeof(noa_wlan_interrupt_state_info_t);
	case MIB_INFO:
		return sizeof(noa_wlan_mib_t);
	case WIT_LOG_SYS:
		return WIFI_DBG_LOG_SYS_DRAM_SIZE;
	case WIT_PACKET_SNIFFER:
		return WIFI_DBG_PACKET_DRAM_SIZE;
	case BUFFER_MGMT:
		return sizeof(noa_wlan_shared_memory_buffer_management_t);
	default:
		break;
	}

	return 0;
}

size_t noa_wlan_cfg_space_get_size(struct noa_wlan_client *client)
{
	return client->cfg.size;
}

u32 noa_wlan_cfg_space_read_u32(struct noa_wlan_client *client, unsigned long offset)
{
	struct noa_wlan_cfg_space *cfg = &client->cfg;
	const u32 *addr = (const u32 *)(cfg->base_va + offset);

	if (cfg->base_va == NULL || offset >= cfg->size) {
		return NOA_WLAN_CFG_SPACE_INVALID_VAL;
	}
	return readl(addr);
}

int noa_wlan_cfg_space_write(struct noa_wlan_client *client, unsigned long offset, size_t size,
			     const void *data)
{
	struct noa_wlan_cfg_space *cfg = &client->cfg;
	u32 *addr = (u32 *)(cfg->base_va + offset);

	if (cfg->base_va == NULL || offset + size >= cfg->size) {
		return -ENODATA;
	}

	memcpy(addr, data, size);
	return 0;
}

#define GLOBAL_CONFIG_WRITE(client, field, data_size, data_buf)                                    \
	do {                                                                                       \
		u32 base = offsetof(noa_wlan_shared_info_t, global_config);                        \
		size_t field_size = sizeof(((NoaGlobalConfig *)0)->field);                         \
		if (data_size > field_size) {                                                      \
			return -EINVAL;                                                            \
		}                                                                                  \
		if (noa_wlan_cfg_space_write(client, base + offsetof(NoaGlobalConfig, field),      \
					     (size_t)data_size, (void *)data_buf) != 0) {          \
			return -ENXIO;                                                             \
		}                                                                                  \
	} while (0)

int noa_wlan_cfg_space_global_write(struct noa_wlan_client *client, global_config_field_t field,
				    size_t data_size, void *data_buf)
{
	switch (field) {
	case CHIP_TYPE:
		GLOBAL_CONFIG_WRITE(client, chip_type, data_size, data_buf);
		break;
	case RX_PACKET_NUM:
		GLOBAL_CONFIG_WRITE(client, rx_pkt_max, data_size, data_buf);
		break;
	case RX_BUFFER_SIZE:
		GLOBAL_CONFIG_WRITE(client, rx_buf_size, data_size, data_buf);
		break;
	case VENDOR_TX_PACKET_ID_MAX:
		GLOBAL_CONFIG_WRITE(client, tx_pkt_max, data_size, data_buf);
		break;
	case NOA_TX_PACKET_NUM:
		GLOBAL_CONFIG_WRITE(client, tx_bm_size, data_size, data_buf);
		break;
	case SHARE_ADDRESS:
		GLOBAL_CONFIG_WRITE(client, share_addr, data_size, data_buf);
		break;
	case WDEV_REG_ADDRESS:
		GLOBAL_CONFIG_WRITE(client, reg_addr, data_size, data_buf);
		break;
	case SHARE_SIZE:
		GLOBAL_CONFIG_WRITE(client, share_size, data_size, data_buf);
		break;
	case WDEV_REG_SIZE:
		GLOBAL_CONFIG_WRITE(client, reg_size, data_size, data_buf);
		break;
	case INTS_ADDR:
		GLOBAL_CONFIG_WRITE(client, ints_addr, data_size, data_buf);
		break;
	case INTM_ADDR:
		GLOBAL_CONFIG_WRITE(client, intm_addr, data_size, data_buf);
		break;
	case RX_PKT_TLV_SIZE:
		GLOBAL_CONFIG_WRITE(client, rx_pkt_tlv_size, data_size, data_buf);
		break;
	case DOORBELL_ADDR:
		GLOBAL_CONFIG_WRITE(client, doorbell_addr, data_size, data_buf);
		break;
	case FW_TRAP_ADDR:
		GLOBAL_CONFIG_WRITE(client, fw_trap_addr, data_size, data_buf);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void noa_wlan_cfg_space_ring_db(struct noa_wlan_client *client)
{
	// Placeholder.
}

irqreturn_t noa_wlan_cfg_space_db_isr(int irq, void *context)
{
	// Placeholder.
	return IRQ_HANDLED;
}

int noa_wlan_cfg_space_write_pcie_config(struct noa_wlan_client *client, void *state, ssize_t size)
{
	size_t pcie_state_offset = offsetof(noa_wlan_shared_info_t, pm_state_info) +
				   offsetof(noa_wlan_pm_state_info_t, pcie_stored_state);

	if (!client || !state) {
		return -EINVAL;
	}

	noa_wlan_cfg_space_write(client, pcie_state_offset, size, state);

	return 0;
}

void *noa_wlan_cfg_space_read_pcie_config(struct noa_wlan_client *client)
{
	size_t pcie_state_offset = offsetof(noa_wlan_pm_state_info_t, pcie_stored_state);

	if (!client) {
		return NULL;
	}

	return noa_wlan_cfg_space_get_section_va(client, PM_STATE_INFO) + pcie_state_offset;
}

int noa_wlan_cfg_update_wifi_ring_info(struct noa_wlan_client *client, u32 cnt, u32 type,
				       const struct noa_wlan_ring_info *ring_info)
{
	int i = 0;
	int ret;
	noa_wlan_wifi_ring_config_t *ring_config =
		noa_wlan_cfg_space_get_section_va(client, WDEV_RING_CONFIG);
	noa_wlan_ring_pool_config_t *ring_pool_config = ring_config->wifi_ring_pool_config;
	noa_wlan_ring_info_t *ring_info_base = noa_wlan_cfg_get_wifi_ring_info(ring_config, type);
	noa_wlan_ring_info_t *ring_info_target;
	struct noa_wlan_mapping_params params = {
		.contiguous = false,
		.tkid_in_use = false,
	};

	if (type >= MAX_RING_TYPE_NUM || cnt > noa_wlan_get_max_ring_num(type)) {
		return -EINVAL;
	}

	ring_pool_config[type].count = cnt;
	ring_pool_config[type].ring_type = type;

	memset(ring_info_base, 0, sizeof(noa_wlan_ring_info_t) * cnt);
	for (i = 0; i < cnt; i++) {
		ring_info_target = &ring_info_base[i];
		ring_info_target->regs.base = ring_info[i].regs.base;
		ring_info_target->regs.len = ring_info[i].regs.len;
		ring_info_target->regs.max_item = ring_info[i].regs.max_item;
		ring_info_target->regs.read = ring_info[i].regs.read;
		ring_info_target->regs.write = ring_info[i].regs.write;
		ring_info_target->dma_pa = ring_info[i].dma_pa;
		ring_info_target->dma_va = ring_info[i].dma_va;
		ring_info_target->ndesc = ring_info[i].ndesc;
		ring_info_target->desc_sz = ring_info[i].desc_sz;
		ring_info_target->hw_idx = ring_info[i].hw_idx;
		ring_info_target->stride = ring_info[i].stride;
		ring_info_target->sn = ring_info[i].sn;
		ring_info_target->is_active = false;
		strncpy(ring_info_target->name, ring_info[i].name, sizeof(ring_info_target->name));

		params.cpu_addr = (void *)ring_info_target->dma_va;
		params.phy_addr = ring_info_target->dma_pa;
		params.size = ring_info_target->ndesc * ring_info_target->desc_sz;

		ret = noa_wlan_mapper_remap(client, &params, &ring_info_target->dma_va);
		if (ret) {
			dev_err(client->dev, "%s(): failed to remap dma address, err: %d\n",
				__func__, ret);
			continue;
		}
	}

	return 0;
}

int noa_wlan_cfg_set_wifi_ring_active_state(struct noa_wlan_client *client, u32 type, u32 ring_id,
					    bool is_active)
{
	noa_wlan_wifi_ring_config_t *ring_config =
		noa_wlan_cfg_space_get_section_va(client, WDEV_RING_CONFIG);
	noa_wlan_ring_pool_config_t *ring_pool_config = ring_config->wifi_ring_pool_config;
	noa_wlan_ring_info_t *ring_info_base = noa_wlan_cfg_get_wifi_ring_info(ring_config, type);
	noa_wlan_ring_info_t *ring_info_target;
	uint32_t ring_cnt = ring_pool_config[type].count;

	if (ring_id < ring_cnt) {
		ring_info_target = &ring_info_base[ring_id];
		ring_info_target->is_active = is_active;
		return 0;
	}

	pr_err("%s: ring_id %u out of bounds (ring_cnt: %u)\n", __func__, ring_id, ring_cnt);

	return -ENOENT;
}

int noa_wlan_cfg_update_wifi_ring_syncback(struct noa_wlan_client *client, u32 cnt, u32 type,
					   struct noa_wlan_ring_info *ring_info)
{
	int i = 0;
	noa_wlan_wifi_ring_config_t *ring_config =
		noa_wlan_cfg_space_get_section_va(client, WDEV_RING_CONFIG);
	noa_wlan_ring_info_t *ring_info_base = noa_wlan_cfg_get_wifi_ring_info(ring_config, type);
	noa_wlan_ring_info_t *ring_info_src;

	for (i = 0; i < cnt; i++) {
		ring_info_src = &ring_info_base[i];
		ring_info[i].sn = ring_info_src->sn;
	}
	return 0;
}

static noa_wlan_ring_info_t *noa_wlan_cfg_get_nep_ring_info(noa_wlan_nep_ring_config_t *ring_config,
							    u32 type)
{
	if (!ring_config)
		return NULL;

	switch (type) {
	case RING_TYPE_TX_DATA:
		return ring_config->nep_tx_rings_info;
	case RING_TYPE_RX_DATA:
		return ring_config->nep_rx_rings_info;
	case RING_TYPE_TX_CPL:
		return ring_config->nep_txcpl_rings_info;
	case RING_TYPE_RX_POST:
		return ring_config->nep_rxpost_rings_info;
	default:
		return NULL;
	}
}

int noa_wlan_cfg_update_nep_ring_info(struct noa_wlan_client *client, u32 cnt, u32 type,
				      const struct noa_wlan_ring_info *ring_info)
{
	int i = 0;
	noa_wlan_nep_ring_config_t *ring_config =
		noa_wlan_cfg_space_get_section_va(client, NEP_RING_CONFIG);
	noa_wlan_ring_pool_config_t *ring_pool_config = ring_config->nep_ring_pool_config;
	noa_wlan_ring_info_t *ring_info_base = noa_wlan_cfg_get_nep_ring_info(ring_config, type);
	noa_wlan_ring_info_t *ring_info_target;

	if (type >= MAX_RING_TYPE_NUM || cnt > noa_wlan_get_max_ring_num(type)) {
		return -EINVAL;
	}

	ring_pool_config[type].count = cnt;
	ring_pool_config[type].ring_type = type;

	for (i = 0; i < cnt; i++) {
		ring_info_target = &ring_info_base[i];
		ring_info_target->regs.base = ring_info[i].regs.base;
		ring_info_target->regs.len = ring_info[i].regs.len;
		ring_info_target->regs.max_item = ring_info[i].regs.max_item;
		ring_info_target->regs.read = ring_info[i].regs.read;
		ring_info_target->regs.write = ring_info[i].regs.write;
		ring_info_target->dma_pa = ring_info[i].dma_pa;
		// NEP, NCP rings' dma_va are the same as dma_pa for dpa view address
		if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
			ring_info_target->dma_va = ring_info[i].dma_pa;
		} else {
			ring_info_target->dma_va = ring_info[i].dma_va;
		}
		ring_info_target->ndesc = ring_info[i].ndesc;
		ring_info_target->desc_sz = ring_info[i].desc_sz;
		ring_info_target->hw_idx = ring_info[i].hw_idx;
		ring_info_target->stride = ring_info[i].stride;
		ring_info_target->sn = ring_info[i].sn;
		strncpy(ring_info_target->name, ring_info[i].name, sizeof(ring_info_target->name));
	}
	return 0;
}

static inline noa_mib_t *noa_wlan_cfg_get_noa_wlan_mib_info(struct noa_wlan_client *client)
{
	noa_wlan_shared_info_t *shared_info =
		(noa_wlan_shared_info_t *)(noa_wlan_cfg_space_get_base_va(client));

	if (shared_info == NULL) {
		return NULL;
	}

	return (noa_mib_t *)&shared_info->mib;
}

noa_interrupt_state_info_t *
noa_wlan_cfg_update_noa_interrupt_state_info(struct noa_wlan_client *client)
{
	noa_interrupt_state_info_t *interrupt_state_info;
	noa_wlan_shared_info_t *shared_info =
		(noa_wlan_shared_info_t *)(noa_wlan_cfg_space_get_base_va(client));

	if (shared_info == NULL) {
		return NULL;
	}

	interrupt_state_info = (noa_interrupt_state_info_t *)&(shared_info->interrupt_state_info);

	interrupt_state_info->interrupt_cnt[PCIE_MSI_APC_INTR] = client->pcie_msi_apc_intr_cnt;
	interrupt_state_info->interrupt_cnt[NEP_APC_INTR] = client->nep_apc_intr_cnt;
	interrupt_state_info->interrupt_cnt[NCP_APC_INTR] = client->ncp_apc_intr_cnt;

	return interrupt_state_info;
}

noa_mib_t *noa_wlan_cfg_update_noa_wlan_mib_info(struct noa_wlan_client *client)
{
	int i;
	noa_mib_t *mib;
	struct wlan_nep_ring_stats *const nep_ring_stats = &client->nep_ring_stats;

	mib = noa_wlan_cfg_get_noa_wlan_mib_info(client);

	if (mib == NULL) {
		return NULL;
	}

	mib->nep_sw_output_ring_pkt_cnt = nep_ring_stats->nep_sw_output;
	mib->nep_sw_rxrefill_ring_pkt_cnt = nep_ring_stats->nep_sw_rxrefill;
	mib->nep_sw_txcpl_ring_pkt_cnt = nep_ring_stats->nep_sw_txcpl;

	for (i = 0; i < MAX_TX_RINGS_NUM; i++) {
		mib->nep_sw_input_ring_pkt_cnt[i] = nep_ring_stats->nep_sw_input[i];
	}

	return mib;
}

int noa_wlan_cfg_update_noa_wlan_sta_info(struct noa_wlan_client *client,
					  struct noa_wlan_sta_info *sta_info)
{
	int idx;
	noa_wlan_sta_info_t *entry;
	noa_wlan_sta_info_t *sta_info_target =
		(noa_wlan_sta_info_t *)noa_wlan_cfg_space_get_section_va(client, STA_INFO);
	if (sta_info_target == NULL) {
		return -EINVAL;
	}

	idx = noa_wlan_cfg_find_station_in_sta_table(sta_info_target, sta_info);

	if (sta_info->enable) {
		if (idx < 0) {
			noa_wlan_cfg_allocate_new_sta_info_entry(sta_info_target, &entry, &idx);
		} else {
			entry = &sta_info_target[idx];
		}

		entry->oif = sta_info->oif;
		entry->bss_idx = sta_info->bss_idx;
		entry->encrypt_type = sta_info->encrypt_type;
		entry->encap_type = sta_info->encap_type;
		entry->lmac_id = sta_info->lmac_id;
		entry->bmid = sta_info->bmid;
		entry->search_idx = sta_info->search_idx;
		entry->search_type = sta_info->search_type;
		entry->dscp_tid_map_id = sta_info->dscp_tid_map_id;
		entry->addry_en = sta_info->addry_en;
		entry->addrx_en = sta_info->addrx_en;
		entry->fw_metadata = sta_info->fw_metadata;

		memcpy(entry->mac_addr, sta_info->addr, sizeof(uint8_t) * MAC_ADDR_LEN);
		memcpy(entry->qos_txq_map, sta_info->qos_txq_map,
		       sizeof(uint16_t) * PRIORITY_CLASS);
		entry->enable = true;
	} else {
		if (idx < 0) {
			return -ENODEV;
		} else {
			memset(&sta_info_target[idx], 0, sizeof(noa_wlan_sta_info_t));
		}
	}
	return idx;
}

int noa_wlan_cfg_pci_dev_state_write(struct noa_wlan_client *client, pci_dev_scalar_field_t type,
				     int target)
{
	noa_wlan_pm_state_info_t *pm_state_info;
	pm_state_info = noa_wlan_cfg_space_get_section_va(client, PM_STATE_INFO);

	if (pm_state_info == NULL) {
		return -EINVAL;
	}

	switch (type) {
	case IS_BUSMASTER:
		pm_state_info->PCIe_is_busmaster = target;
		break;
	case ENABLE_CNT:
		pm_state_info->PCIe_enable_cnt = target;
		break;
	case CURRENT_STATE:
		pm_state_info->PCIe_current_state = target;
		break;
	case PM_STATE:
		pm_state_info->PCIe_pm_state = target;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int noa_wlan_cfg_pci_dev_state_read(struct noa_wlan_client *client, pci_dev_scalar_field_t type)
{
	noa_wlan_pm_state_info_t *pm_state_info;

	pm_state_info = noa_wlan_cfg_space_get_section_va(client, PM_STATE_INFO);
	if (pm_state_info == NULL) {
		return -EINVAL;
	}

	switch (type) {
	case IS_BUSMASTER:
		return pm_state_info->PCIe_is_busmaster;
	case ENABLE_CNT:
		return pm_state_info->PCIe_enable_cnt;
	case CURRENT_STATE:
		return pm_state_info->PCIe_current_state;
	case PM_STATE:
		return pm_state_info->PCIe_pm_state;
	default:
		return -EINVAL;
	}

	return 0;
}

int noa_wlan_cfg_update_pci_ownership(struct noa_wlan_client *client, uint8_t owner)
{
	noa_wlan_pm_state_info_t *pm_state_info;
	pm_state_info = noa_wlan_cfg_space_get_section_va(client, PM_STATE_INFO);

	if (pm_state_info == NULL || pm_state_info->PCIe_link_owner == owner) {
		return -EINVAL;
	}

	pm_state_info->PCIe_link_owner = owner;
	return 0;
}
