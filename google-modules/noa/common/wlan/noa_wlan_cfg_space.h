/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WLAN Configuration Space API
 *
 * This header file provides an API for managing the configuration space for
 * a WLAN client. The configuration space is a shared memory region used for
 * communication between the NOA WLAN driver and the NOA WLAN firmware.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Wade Shih <wadeshih@google.com>
 */
#ifndef __NOA_WLAN_CFG_SPACE_H__
#define __NOA_WLAN_CFG_SPACE_H__

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <common/wlan/memory_map.h>
#include <common/wlan/noa_wlan.h>
#include "wlan/noa_wlan_buffer_management.h"

#define NOA_WLAN_CFG_SPACE_INVALID_VAL (0xDEADBEEF)
#define STA_MODE_LMAC_ID 0
#define NEP_TX_POST_RING_NUM 1U
#define NEP_RX_CMPL_RING_NUM 1U
#define NEP_TX_CMPL_RING_NUM 1U
#define WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(struct_type, struct_ptr, field_name,                \
					       field_type_fmt)                                     \
	do {                                                                                       \
		cnt += scnprintf(buf + cnt, len - cnt,                                             \
				 "[WLAN] (0x%08lx) %s value: %" field_type_fmt "\n",               \
				 offsetof(struct_type, field_name), #field_name,                   \
				 (struct_ptr)->field_name);                                        \
	} while (0)

#define WLAN_DBG_SHOW_MEMORY_MAP_U32_ARRAY_VALUE(struct_type, struct_ptr, field_name, idx)         \
	do {                                                                                       \
		cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] (0x%08lx) %s[%d] value: %u \n",     \
				 offsetof(struct_type, field_name) + sizeof(uint32_t) * idx,       \
				 #field_name, idx, (struct_ptr)->field_name[idx]);                 \
	} while (0)

#define WLAN_DBG_PRINT_MEMORY_MAP_SECTION_VALUE(struct_type, struct_ptr, field_name,               \
						field_type_fmt)                                    \
	do {                                                                                       \
		pr_info("[WLAN] (0x%08lx) %s value: %" field_type_fmt "\n",                        \
			offsetof(struct_type, field_name), #field_name, (struct_ptr)->field_name); \
	} while (0)

#define WLAN_DBG_SHOW_MEMORY_MAP_RING_CONFIG_VALUE(print_macro, ring_pool, i)                      \
	do {                                                                                       \
		print_macro(noa_ring_regs_t, &(ring_pool[i].regs), base, "llx");                   \
		print_macro(noa_ring_regs_t, &(ring_pool[i].regs), len, "llx");                    \
		print_macro(noa_ring_regs_t, &(ring_pool[i].regs), max_item, "llx");               \
		print_macro(noa_ring_regs_t, &(ring_pool[i].regs), read, "llx");                   \
		print_macro(noa_ring_regs_t, &(ring_pool[i].regs), write, "llx");                  \
		print_macro(noa_ring_regs_t, &(ring_pool[i].regs), dpa_base, "llx");               \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), ndesc, "u");                    \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), desc_sz, "u");                  \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), offset, "u");                   \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), sn, "u");                       \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), hw_idx, "hhu");                 \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), stride, "hhu");                 \
		print_macro(noa_wlan_ring_info_t, &(ring_pool[i]), name, "s");                     \
	} while (0)

typedef struct NoaWlanSharedInfo noa_wlan_shared_info_t;
typedef struct NoaGlobalConfig noa_wlan_global_config_t;
typedef struct NoaWlanWiFiRingConfig noa_wlan_wifi_ring_config_t;
typedef struct NoaWlanNepRingConfig noa_wlan_nep_ring_config_t;
typedef struct NoaWlanStaInfo noa_wlan_sta_info_t;
typedef struct NoaPmStateInfo noa_wlan_pm_state_info_t;
typedef struct NoaInterruptStateInfo noa_wlan_interrupt_state_info_t;
typedef struct NoaMib noa_wlan_mib_t;
typedef struct NoaBufferManagementSharedMemory noa_wlan_shared_memory_buffer_management_t;
typedef struct NoaWlanRingConfig noa_wlan_ring_config_t;
typedef struct noa_ring_regs noa_ring_regs_t;
typedef struct NoaWlanRingInfo noa_wlan_ring_info_t;
typedef struct NoaWlanRingPoolConfig noa_wlan_ring_pool_config_t;
typedef struct NoaMib noa_mib_t;
typedef struct NoaInterruptStateInfo noa_interrupt_state_info_t;
typedef struct NoaWlanShmRingConfig noa_wlan_shm_ring_config_t;
typedef struct NoaWlanShmRingEntry noa_wlan_shm_ring_entry_t;

enum noa_wlan_cfg_space_section {
	SECTION_START = kSectionStart,
	GLOBAL_CONFIG = kGlobalConfig,
	RING_CONFIG = kRingConfig,
	WDEV_RING_CONFIG = kWdevRingConfig,
	NEP_RING_CONFIG = kNepRingConfig,
	STA_INFO = kStaInfo,
	PM_STATE_INFO = kPmStateInfo,
	INTR_STATE_INFO = kIntrStateInfo,
	MIB_INFO = kMibInfo,
	WIT_LOG_SYS = kWitLogSys,
	WIT_PACKET_SNIFFER = kWitPacketSniffer,
	BUFFER_MGMT = kBufferMgmt,
	SECTION_END = kSectionEnd,
	SECTION_NUM = kSectionNum,
};

struct noa_wlan_client;

typedef enum global_config_field {
	CHIP_TYPE = 0,
	RX_PACKET_NUM,
	RX_BUFFER_SIZE,
	VENDOR_TX_PACKET_ID_MAX,
	NOA_TX_PACKET_NUM,
	SHARE_ADDRESS,
	WDEV_REG_ADDRESS,
	SHARE_SIZE,
	WDEV_REG_SIZE,
	INTS_ADDR,
	INTM_ADDR,
	RX_PKT_TLV_SIZE,
	DOORBELL_ADDR,
	FW_TRAP_ADDR,
} global_config_field_t;

typedef enum pci_dev_scalar_field {
	IS_BUSMASTER = 0,
	ENABLE_CNT,
	CURRENT_STATE,
	PM_STATE,
	PCI_DEV_STATE_NUM,
} pci_dev_scalar_field_t;

/**
 * @brief WLAN configuration space structure.
 *
 * This structure represents the configuration space for a WLAN client.
 */
struct noa_wlan_cfg_space {
	void *base_va;
	dma_addr_t base_pa;
	size_t size;
};

struct noa_wlan_sta_info {
	u32 oif;
	u16 bss_idx;
	u16 qos_txq_map[PRIORITY_CLASS];
	u8 addr[MAC_ADDR_LEN];
	u8 encrypt_type : 4, encap_type : 2, lmac_id : 2;
	u8 bmid;
	u16 fw_metadata;
	u32 search_idx : 20, search_type : 2, dscp_tid_map_id : 6, addry_en : 1, addrx_en : 1,
		reserved2 : 2;
	u8 enable;
	u8 sta_id;
	u8 sta_table_idx;
	u8 reserved3[1];
};

/**
 * @brief Initialize the configuration space for the WLAN client.
 *
 * This function allocates DMA-coherent memory for the configuration space
 * and initializes the `wlan_cfg_space` structure within the `wlan_client`.
 *
 * @param[in] client The WLAN client.
 * @return 0 on success, negative error code on failure.
 */
int noa_wlan_cfg_space_init(struct noa_wlan_client *client);

/**
 * @brief De-initialize the configuration space for the WLAN client.
 *
 * This function frees the DMA-coherent memory allocated for the configuration
 * space and resets the `wlan_cfg_space` structure within the `wlan_client`.
 *
 * @param[in] client The WLAN client.
 */
void noa_wlan_cfg_space_deinit(struct noa_wlan_client *client);

/**
 * @brief Get the virtual base address of the configuration space.
 *
 * @param[in] client The WLAN client.
 * @return The virtual base address of the configuration space.
 */
void *noa_wlan_cfg_space_get_base_va(struct noa_wlan_client *client);

/**
 * @brief Get the physical base address of the configuration space.
 *
 * @param[in] client The WLAN client.
 * @return The physical base address of the configuration space.
 */
dma_addr_t noa_wlan_cfg_space_get_base_pa(struct noa_wlan_client *client);

/**
 * @brief Gets the virtual address of a section in the NOA WLAN configuration
 * space.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @param[in] section The section to access.
 * @return The virtual address of the section, or NULL if the section is not
 * valid.
 */
void *noa_wlan_cfg_space_get_section_va(struct noa_wlan_client *client,
					enum noa_wlan_cfg_space_section section);

/**
 * @brief Gets the physical address of a section in the NOA WLAN configuration
 * space.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @param[in] section The section to access.
 * @return The physical address of the section.
 */
dma_addr_t noa_wlan_cfg_space_get_section_pa(struct noa_wlan_client *client,
					     enum noa_wlan_cfg_space_section section);

/**
 * @brief Gets the size of a section in the NOA WLAN configuration space.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @param[in] section The section to access.
 * @return The size of the section in bytes.
 */
size_t noa_wlan_cfg_space_get_section_size(struct noa_wlan_client *client,
					   enum noa_wlan_cfg_space_section section);

/**
 * @brief Get the size of the configuration space.
 *
 * @param[in] client The WLAN client.
 * @return The size of the configuration space.
 */
size_t noa_wlan_cfg_space_get_size(struct noa_wlan_client *client);

/**
 * @brief Read a 32-bit value from the configuration space.
 *
 * This function reads a 32-bit value from the specified offset in the
 * configuration space.
 *
 * @note It performs a DMA synchronization to ensure data consistency between
 * CPU and device.
 *
 * @param[in] client The WLAN client.
 * @param[in] offset The offset within the configuration space.
 * @return The 32-bit value read from the configuration space.
 */
u32 noa_wlan_cfg_space_read_u32(struct noa_wlan_client *client, unsigned long offset);

/**
 * @brief Write a 32-bit value to the configuration space.
 *
 * This function writes a 32-bit value to the specified offset in the
 * configuration space.
 *
 * @note It performs a DMA synchronization to ensure data consistency between
 * CPU and device.
 *
 * @param[in] client The WLAN client.
 * @param[in] offset The offset within the configuration space.
 * @param[in] size The size of the data.
 * @param[in] data The data to write.
 *
 * @return 0 on success, negative error code on failure.
 */
int noa_wlan_cfg_space_write(struct noa_wlan_client *client, unsigned long offset, size_t size,
			     const void *data);

/**
 * @brief Ring the doorbell for the WLAN client.
 *
 * This function triggers an interrupt to notify the WLAN firmware about
 * changes in the configuration space.
 *
 * @param[in] client The WLAN client.
 */
void noa_wlan_cfg_space_ring_db(struct noa_wlan_client *client);

/**
 * @brief Interrupt handler for the WLAN client's doorbell.
 *
 * This function handles the interrupt triggered by the WLAN firmware when it
 * changes the configuration space.
 *
 * @param[in] irq The interrupt request number.
 * @param[in] context The interrupt context.
 * @return IRQ_HANDLED if the interrupt was handled, IRQ_NONE otherwise.
 */
irqreturn_t noa_wlan_cfg_space_db_isr(int irq, void *context);

/**
 * @brief Store the PCIe configuration space for the WLAN client.
 *
 * This function read the PCIe configuration space from the shared memory.
 *
 * @param[in] client The WLAN client.
 * @return 0 on success, negative error code on failure.
 */
void *noa_wlan_cfg_space_read_pcie_config(struct noa_wlan_client *client);

/**
 * @brief Update the PCIe configuration space for the WLAN client.
 *
 * This function updates the PCIe configuration space with the saved state
 * from the WLAN client. The confuguration space will be read from the NCP
 * firmware during the start process.
 *
 * @param[in] client The WLAN client.
 * @param[in] state The saved PCIe state.
 * @param[in] size The size of the saved state.
 * @return 0 on success, negative error code on failure.
 */
int noa_wlan_cfg_space_write_pcie_config(struct noa_wlan_client *client, void *state, ssize_t size);

/** @brief Fill in the WiFi Ring information in shared memory
 *
 * Based on the input ring information, this function will fill in the fields
 * one by one to ensure the completeness. Since the structure of noa_wlan_ring_info
 * might be different from NoaWlanRingInfo, it is better to fill them explicitly.
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @param[in] cnt the number of ring for the specific ring_type
 * @param[in] type the type of a ring
 * @param[in] ring_info the expected input ring information
 * @return the status of the execution
 */
int noa_wlan_cfg_update_wifi_ring_info(struct noa_wlan_client *client, u32 cnt, u32 type,
				       const struct noa_wlan_ring_info *ring_info);

/**
 * @brief  Finds a specific Wi-Fi hardware ring and sets its active state.
 *
 * @param[in] client A pointer to the active `noa_wlan_client` context.
 * @param[in] type The category or type of ring pool to search within
 * (e.g., TX, RX).
 * @param[in] ring_id The ring index of the specific ring to modify.
 * @param[in] is_active The new state to set for the ring. `true` activates
 * the ring, `false` deactivates it.
 *
 * @return 0 on success.
 * @return -ENOENT if a ring with the specified `target_hw_idx` could not
 * be found in the given ring pool type.
 */
int noa_wlan_cfg_set_wifi_ring_active_state(struct noa_wlan_client *client, u32 type, u32 ring_id,
					    bool is_active);

/** @brief Sync back WiFi Ring information from shared memory
 *
 * Based on the shared memory ring information, this function will syncback in the fields
 * one by one to ensure the completeness. Since the structure of noa_wlan_ring_info
 * might be different from NoaWlanRingInfo, it is better to syncback them explicitly.
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @param[in] cnt the number of ring for the specific ring_type
 * @param[in] type the type of a ring
 * @param[in] ring_info the expected syncback ring information
 * @return the status of the execution
 */
int noa_wlan_cfg_update_wifi_ring_syncback(struct noa_wlan_client *client, u32 cnt, u32 type,
					   struct noa_wlan_ring_info *ring_info);

/** @brief Fill in the NEP Ring information in shared memory
 *
 * Based on the input ring information, this function will fill in the fields
 * one by one to ensure the completeness. Since the structure of noa_wlan_ring_info
 * might be different from NoaWlanRingInfo, it is better to fill them explicitly.
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @param[in] cnt the number of ring for the specific ring_type
 * @param[in] type the type of a ring
 * @param[in] ring_info the expected input ring information
 * @return the status of the execution
 */
int noa_wlan_cfg_update_nep_ring_info(struct noa_wlan_client *client, u32 cnt, u32 type,
				      const struct noa_wlan_ring_info *ring_info);

/**
 * @brief Writes a value to the global configuration section.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @param[in] field Enum value representing the field to write.
 * @param[in] data_size Size of the data to write in bytes.
 * @param[in] data_buf Pointer to the buffer containing the data to write.
 *
 * @return 0 on success, negative error code on failure.
 */
int noa_wlan_cfg_space_global_write(struct noa_wlan_client *client, global_config_field_t field,
				    size_t data_size, void *data_buf);

/**
 * @brief Updates the WiFi interrupt state subsection in shared memory
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @return WiFi interrupt state subsection in shared memory
 */
noa_interrupt_state_info_t *
noa_wlan_cfg_update_noa_interrupt_state_info(struct noa_wlan_client *client);

/**
 * @brief Update the WiFi MIB subsection in shared memory
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @return  WiFi MIB subsection in shared memory
 */
noa_mib_t *noa_wlan_cfg_update_noa_wlan_mib_info(struct noa_wlan_client *client);

/**
 * @brief Get the WiFi Station Info in shared memory
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @return WiFi Station Info in shared memory
 */
noa_wlan_sta_info_t *noa_wlan_cfg_get_noa_wlan_sta_info(struct noa_wlan_client *client);

/**
 * @brief Update the WiFi Sta Info Table subsection in shared memory
 *
 * @param[in] client the NOA wlan client equipped with shared memory address
 * @param[in] sta_info the WiFi Station Info needs to be updated
 * @return the index of the updated sta info table
 */
int noa_wlan_cfg_update_noa_wlan_sta_info(struct noa_wlan_client *client,
					  struct noa_wlan_sta_info *sta_info);

/**
 * @brief Update the PCIe PM state in shared memory.
 *
 * Before each ownership switch, the NOA WLAN driver should update the PCIe PM state.
 * @param[in] client The NOA WLAN client.
 * @param[in] type The PCIe PM state type to update.
 * @param[in] target The state with the target value to write.
 * @return 0 on success, negative error code on failure.
 */
int noa_wlan_cfg_pci_dev_state_write(struct noa_wlan_client *client, pci_dev_scalar_field_t type,
				     int target);

/**
 * @brief Read the PCIe PM state from shared memory.
 *
 * This function reads the PCIe PM state from the shared memory.
 * It is used to retrieve the current PCIe PM state before switching ownership.
 *
 * @param[in] client The NOA WLAN client.
 * @param[in] type The PCIe PM state type to read.
 * @return value read from shared memory, or -EINAL if not found.
 */
int noa_wlan_cfg_pci_dev_state_read(struct noa_wlan_client *client, pci_dev_scalar_field_t type);

/**
 * @brief Update the ownership of PCIe in the shared memory
 *
 * @param[in] client The NOA WLAN client.
 * @param[in] owner The new owner of the PCIe link.
 * @return 0 on success, negative error code on failure.
 */
int noa_wlan_cfg_update_pci_ownership(struct noa_wlan_client *client, uint8_t owner);

/**
 * @brief Find the station in the station information table.
 *
 * @param[in] sta_info_list The station information table.
 * @param[in] sta_info The station information to search for.
 * @return The index of the station in the table, or -ENODEV if the station
 * is not found.
 */
static inline int noa_wlan_cfg_find_station_in_sta_table(noa_wlan_sta_info_t *sta_info_list,
							 struct noa_wlan_sta_info *sta_info)
{
	uint32_t i;

	for (i = 0; i < NOA_MAX_STA_SUPPORT; i++) {
		if (sta_info_list[i].enable) {
			if (sta_info_list[i].oif == sta_info->oif &&
			    sta_info_list[i].lmac_id == STA_MODE_LMAC_ID) {
				return i;
			}
			if (!memcmp(sta_info_list[i].mac_addr, sta_info->addr, MAC_ADDR_LEN)) {
				return i;
			}
		}
	}

	return -ENODEV;
}

/**
 * @brief Allocate a new station information entry in the station information table.
 *
 * @param[in] sta_info_list The station information table.
 * @param[out] sta_info The newly allocated station information entry.
 * @param[out] idx The index of the newly allocated station information entry.
 * @return 0 on success, -ENOMEM if no free entry is found.
 */
static inline int noa_wlan_cfg_allocate_new_sta_info_entry(noa_wlan_sta_info_t *sta_info_list,
							   noa_wlan_sta_info_t **sta_info, int *idx)
{
	int i;

	for (i = 0; i < NOA_MAX_STA_SUPPORT; i++) {
		if (!sta_info_list[i].enable) {
			*sta_info = &sta_info_list[i];
			*idx = i;
			return 0;
		}
	}

	return -ENOMEM;
}

static inline ssize_t noa_wlan_cfg_show_global_config_value(struct noa_wlan_client *client,
							    char *buf, ssize_t cnt)
{
	int len = PAGE_SIZE;

	noa_wlan_global_config_t *global_config =
		noa_wlan_cfg_space_get_section_va(client, GLOBAL_CONFIG);

	cnt += scnprintf(buf + cnt, len - cnt,
			 "[WLAN] ======= (APC) NOA WLAN Global Config =======\n");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, chip_type,
					       "llu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, rx_pkt_max,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, rx_buf_size,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, tx_pkt_max,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, tx_bm_size,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, share_addr,
					       "llu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, reg_addr,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, share_size,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, reg_size,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, ints_addr,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config, intm_addr,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_global_config_t, global_config,
					       rx_pkt_tlv_size, "u");

	return cnt;
}

static inline ssize_t noa_wlan_cfg_show_nep_ring_config_value(struct noa_wlan_client *client,
							      char *buf, ssize_t cnt)
{
	int len = PAGE_SIZE;

	int i = 0;

	noa_wlan_nep_ring_config_t *nep_ring_config =
		noa_wlan_cfg_space_get_section_va(client, NEP_RING_CONFIG);
	noa_wlan_ring_info_t *nep_tx_rings = nep_ring_config->nep_tx_rings_info;
	noa_wlan_ring_info_t *nep_rxcpl_rings = nep_ring_config->nep_rx_rings_info;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "[WLAN] ======= (APC) NOA WLAN NEP Ring Config =======\n");
	for (i = 0; i < NEP_TX_POST_RING_NUM; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "NEP Ring TX %d value", i);
		WLAN_DBG_SHOW_MEMORY_MAP_RING_CONFIG_VALUE(WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE,
							   nep_tx_rings, i);
	}

	for (i = 0; i < NEP_RX_CMPL_RING_NUM; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "NEP Ring RX %d value", i);
		WLAN_DBG_SHOW_MEMORY_MAP_RING_CONFIG_VALUE(WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE,
							   nep_rxcpl_rings, i);
	}

	return cnt;
}

static inline ssize_t noa_wlan_cfg_show_sta_info_value(struct noa_wlan_client *client, char *buf,
						       ssize_t cnt)
{
	int len = PAGE_SIZE;

	int i = 0;
	uint32_t bit_field_offset =
		offsetof(noa_wlan_sta_info_t, mac_addr) + sizeof(uint8_t) * MAC_ADDR_LEN;

	noa_wlan_sta_info_t *sta_infos = noa_wlan_cfg_space_get_section_va(client, STA_INFO);

	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) NOA WLAN STA Info =======\n");
	for (i = 0; i < NOA_MAX_STA_SUPPORT; i++) {
		if (sta_infos[i].enable) {
			cnt += scnprintf(buf + cnt, len - cnt,
					 "Station Info %d pointer address: %p\n", i, &sta_infos[i]);
			WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_sta_info_t, &sta_infos[i],
							       oif, "u");
			WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_sta_info_t, &sta_infos[i],
							       bss_idx, "hu");
			cnt += scnprintf(buf + cnt, len - cnt,
					 "(0x%08lx) qos_map (%d, %d, %d, %d, %d, %d, %d, %d)\n",
					 offsetof(noa_wlan_sta_info_t, qos_txq_map),
					 sta_infos[i].qos_txq_map[0], sta_infos[i].qos_txq_map[1],
					 sta_infos[i].qos_txq_map[2], sta_infos[i].qos_txq_map[3],
					 sta_infos[i].qos_txq_map[4], sta_infos[i].qos_txq_map[5],
					 sta_infos[i].qos_txq_map[6], sta_infos[i].qos_txq_map[7]);
			cnt += scnprintf(buf + cnt, len - cnt,
					 "(0x%08lx) mac_addr %02x:%02x:%02x:%02x:%02x:%02x\n",
					 offsetof(noa_wlan_sta_info_t, mac_addr),
					 sta_infos[i].mac_addr[0], sta_infos[i].mac_addr[1],
					 sta_infos[i].mac_addr[2], sta_infos[i].mac_addr[3],
					 sta_infos[i].mac_addr[4], sta_infos[i].mac_addr[5]);
			cnt += scnprintf(
				buf + cnt, len - cnt,
				"(0x%08x) encrypt_type %d, (0x%08x) encap_type %d, (0x%08x)lmac_id %d\n",
				bit_field_offset, sta_infos[i].encrypt_type, bit_field_offset + 4,
				sta_infos[i].encap_type, bit_field_offset + 4 + 2,
				sta_infos[i].lmac_id);
			WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_sta_info_t, &sta_infos[i],
							       bmid, "hhu");
			bit_field_offset =
				offsetof(noa_wlan_sta_info_t, fw_metadata) + sizeof(uint8_t) * 2;
			cnt += scnprintf(buf + cnt, len - cnt,
					 "(0x%08x) search_idx %d, (0x%08x)search_type %d\n",
					 bit_field_offset, sta_infos[i].search_idx,
					 bit_field_offset + 20, sta_infos[i].search_type);
			cnt += scnprintf(
				buf + cnt, len - cnt,
				"(0x%08x) dscp_tid_map_id %d, (0x%08x) addry_en %d, (0x%08x) addrx_en %d\n",
				bit_field_offset + 20 + 2, sta_infos[i].dscp_tid_map_id,
				bit_field_offset + 20 + 2 + 6, sta_infos[i].addry_en,
				bit_field_offset + 20 + 2 + 6 + 1, sta_infos[i].addrx_en);
		}
	}

	return cnt;
}

static uint32_t noa_wlan_get_max_ring_num(uint32_t type)
{
	switch (type) {
	case RING_TYPE_TX_DATA:
		return MAX_TX_RINGS_NUM;
	case RING_TYPE_RX_DATA:
		return MAX_RX_RINGS_NUM;
	case RING_TYPE_RX_POST:
		return MAX_RX_POST_RINGS_NUM;
	case RING_TYPE_TX_CPL:
		return MAX_TX_CPL_RINGS_NUM;
	default:
		return 0;
	}
}

static noa_wlan_ring_info_t *
noa_wlan_cfg_get_wifi_ring_info(noa_wlan_wifi_ring_config_t *ring_config, u32 type)
{
	if (!ring_config)
		return NULL;

	switch (type) {
	case RING_TYPE_TX_DATA:
		return ring_config->wifi_tx_rings_info;
	case RING_TYPE_RX_DATA:
		return ring_config->wifi_rx_rings_info;
	case RING_TYPE_TX_CPL:
		return ring_config->wifi_tx_cpl_rings_info;
	case RING_TYPE_RX_POST:
		return ring_config->wifi_rx_post_rings_info;
	default:
		return NULL;
	}
}

static inline ssize_t noa_wlan_cfg_show_wdev_ring_config_value(struct noa_wlan_client *client,
							       int32_t ring_type,
							       int32_t wdev_ring_id)
{
	noa_wlan_ring_info_t *ring_info = NULL;
	noa_wlan_wifi_ring_config_t *ring_config =
		noa_wlan_cfg_space_get_section_va(client, WDEV_RING_CONFIG);

	ring_info = noa_wlan_cfg_get_wifi_ring_info(ring_config, ring_type);
	if (!ring_info || wdev_ring_id >= noa_wlan_get_max_ring_num(ring_type))
		return -EINVAL;

	WLAN_DBG_SHOW_MEMORY_MAP_RING_CONFIG_VALUE(WLAN_DBG_PRINT_MEMORY_MAP_SECTION_VALUE,
						   ring_info, wdev_ring_id);

	return 0;
}

static inline ssize_t noa_wlan_cfg_show_pm_state_config_value(struct noa_wlan_client *client,
							      char *buf, ssize_t cnt)
{
	struct pci_cap_saved_data {
		u16 cap_nr;
		bool cap_extended;
		u32 size;
		u32 data[];
	};

	struct pci_saved_state {
		u32 config_space[16];
		struct pci_cap_saved_data cap[];
	};

	size_t block_size;
	noa_wlan_pm_state_info_t *pm_state_info =
		noa_wlan_cfg_space_get_section_va(client, PM_STATE_INFO);
	int32_t len = PAGE_SIZE;
	int32_t i = 0;
	struct pci_saved_state *pci_state = noa_wlan_cfg_space_read_pcie_config(client);
	struct pci_cap_saved_data *cap = pci_state->cap;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "[WLAN] ======= (APC) NOA WLAN PM State Info =======\n");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       PCIe_link_owner, "hu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       PCIe_is_busmaster, "hu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       PCIe_enable_cnt, "d");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       PCIe_current_state, "hu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       PCIe_pm_state, "d");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       runtime_suspend_cnt1, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       runtime_resume_cnt1, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       system_suspend_cnt1, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       system_resume_cnt1, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       runtime_suspend_cnt2, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       runtime_resume_cnt2, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       system_suspend_cnt2, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_wlan_pm_state_info_t, pm_state_info,
					       system_resume_cnt2, "u");

	// Dump PCIe configuration space state
	for (i = 0; i < 16; i += 4) {
		cnt += scnprintf(buf + cnt, len - cnt, "[WLAN]    0x%02X: %08X %08X %08X %08X\n",
				 i * 4, pci_state->config_space[i], pci_state->config_space[i + 1],
				 pci_state->config_space[i + 2], pci_state->config_space[i + 3]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN]   Extended Capabilities:\n");
	while (cap->size > 0) {
		cnt += scnprintf(buf + cnt, len - cnt,
				 "[WLAN]    %sCap ID: 0x%02X, Data Size: %u bytes\n",
				 cap->cap_extended ? "Ext " : "", cap->cap_nr, cap->size);

		block_size = sizeof(struct pci_cap_saved_data) + cap->size;
		cap = (struct pci_cap_saved_data *)((uint8_t *)cap + block_size);
	}

	return cnt;
}

static inline ssize_t noa_wlan_cfg_show_mib_info_value(struct noa_wlan_client *client, char *buf,
						       ssize_t cnt)
{
	int len = PAGE_SIZE;

	int i = 0;

	noa_mib_t *mib_info = noa_wlan_cfg_space_get_section_va(client, MIB_INFO);

	cnt += scnprintf(buf + cnt, len - cnt, "[WLAN] ======= (APC) NOA WLAN MIB INFO =======\n");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rx_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rx_forward_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rx_pkt_err_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rx_pkt_byte, "llu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_tx_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_tx_forward_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_tx_pkt_err_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_tx_pkt_byte, "llu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_tx_cpl_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_tx_cpl_err_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rx_replenish_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rx_replenish_err_cnt,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_rxbm_sync_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, total_txbm_sync_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, nep_sw_output_ring_pkt_cnt,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, nep_sw_txcpl_ring_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, nep_sw_rxrefill_ring_pkt_cnt,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, nep_fw_input_ring_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, nep_fw_txcpl_ring_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, nep_fw_rxrefill_ring_pkt_cnt,
					       "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, device_rx_ring_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, device_txcpl_ring_pkt_cnt, "u");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_mib_t, mib_info, device_rxrefill_ring_pkt_cnt,
					       "u");

	for (i = 0; i < NEP_TX_POST_RING_NUM; i++) {
		WLAN_DBG_SHOW_MEMORY_MAP_U32_ARRAY_VALUE(noa_mib_t, mib_info,
							 nep_sw_input_ring_pkt_cnt, i);
	}

	for (i = 0; i < NEP_TX_POST_RING_NUM; i++) {
		WLAN_DBG_SHOW_MEMORY_MAP_U32_ARRAY_VALUE(noa_mib_t, mib_info,
							 nep_fw_output_ring_pkt_cnt, i);
	}

	for (i = 0; i < MAX_TX_RINGS_NUM; i++) {
		WLAN_DBG_SHOW_MEMORY_MAP_U32_ARRAY_VALUE(noa_mib_t, mib_info,
							 device_tx_ring_pkt_cnt, i);
	}

	return cnt;
}

static inline ssize_t noa_wlan_cfg_show_interrupt_state_info_value(struct noa_wlan_client *client,
								   char *buf, ssize_t cnt)
{
	int len = PAGE_SIZE;

	int i = 0;

	noa_interrupt_state_info_t *interrupt_state_info =
		noa_wlan_cfg_space_get_section_va(client, INTR_STATE_INFO);

	cnt += scnprintf(buf + cnt, len - cnt,
			 "[WLAN] ======= (APC) NOA WLAN INTR STATE INFO =======\n");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_interrupt_state_info_t, interrupt_state_info,
					       interrupt_owner, "hhu");
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_interrupt_state_info_t, interrupt_state_info,
					       interrupt_proxy_status, "u");
	for (i = 0; i < INTERRUPT_TYPE_NUM; i++) {
		WLAN_DBG_SHOW_MEMORY_MAP_U32_ARRAY_VALUE(noa_interrupt_state_info_t,
							 interrupt_state_info, interrupt_cnt, i);
	}
	WLAN_DBG_SHOW_MEMORY_MAP_SECTION_VALUE(noa_interrupt_state_info_t, interrupt_state_info,
					       interrupt_proxy_cnt, "u");

	return cnt;
}

static inline ssize_t noa_wlan_cfg_show_memory_map_section_value(struct noa_wlan_client *client,
								 const char *current_section,
								 char *buf, ssize_t cnt)
{
	if (strncmp(current_section, "GLOBAL_CONFIG", strlen("GLOBAL_CONFIG")) == 0) {
		return noa_wlan_cfg_show_global_config_value(client, buf, cnt);
	} else if (strncmp(current_section, "NEP_RING_CONFIG", strlen("NEP_RING_CONFIG")) == 0) {
		return noa_wlan_cfg_show_nep_ring_config_value(client, buf, cnt);
	} else if (strncmp(current_section, "STA_INFO", strlen("STA_INFO")) == 0) {
		return noa_wlan_cfg_show_sta_info_value(client, buf, cnt);
	} else if (strncmp(current_section, "PM_STATE_INFO", strlen("PM_STATE_INFO")) == 0) {
		return noa_wlan_cfg_show_pm_state_config_value(client, buf, cnt);
	} else if (strncmp(current_section, "MIB_INFO", strlen("MIB_INFO")) == 0) {
		return noa_wlan_cfg_show_mib_info_value(client, buf, cnt);
	} else if (strncmp(current_section, "INTR_STATE_INFO", strlen("INTR_STATE_INFO")) == 0) {
		return noa_wlan_cfg_show_interrupt_state_info_value(client, buf, cnt);
	}

	return -EINVAL;
}

#endif /* __NOA_WLAN_CFG_SPACE_H__ */
