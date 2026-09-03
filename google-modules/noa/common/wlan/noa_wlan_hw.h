/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#ifndef __NOA_WLAN_HW_H__
#define __NOA_WLAN_HW_H__

#include "noa_wlan_client.h"

enum NCP2APC_DOORBELL_NUM {
	NCP2APC_DOORBELL_NUM_START = 0,
	DOORBELL_RX_EVENT = NCP2APC_DOORBELL_NUM_START,
	DOORBELL_FW_TRAP_EVENT,
	DOORBELL_PACKET_SNIFFER_FULL_EVENT,
	NCP2APC_DOORBELL_NUM_END,
};

enum APC2NCP_DOORBELL_NUM {
	APC2NCP_DOORBELL_NUM_START = 0,
	DOORBELL_BM_UPDATE = APC2NCP_DOORBELL_NUM_START,
	DOORBELL_STATION_INFO_SYNC,
	DOORBELL_TX_RING_INFO_SYNC,
	DOORBELL_DIRECT_SUB_EVENT,
	DOORBELL_PACKET_SNIFFER_RESET,
	DOORBELL_PCIE_OWNERSHIP_SWITCH,
	APC2NCP_DOORBELL_NUM_END,
};

/**
 * @brief Setup NOA Wlan hardware information to client.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @return the status of the execution.
 */
int noa_wlan_hw_set(struct noa_wlan_client *client);

/**
 * @brief Reset NOA Wlan hardware information to client.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 */
void noa_wlan_hw_reset(struct noa_wlan_client *client);

/**
 * @brief Doorbell to NEP ring service.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @return the status of the execution.
 */
int noa_wlan_hw_ringbell_nep(struct noa_wlan_client *client);

/**
 * @brief Doorbell to NCP ring service.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @param[in] doorbell_num  The specific APC-to-NCP doorbell identifier.
 * @return the status of the execution.
 */
int noa_wlan_hw_ringbell_ncp(struct noa_wlan_client *client,
			     enum APC2NCP_DOORBELL_NUM doorbell_num);

/**
 * @brief Activate/Deactivate NEP Input Rings
 *
 * @param[in] activate Deactivate or activate NEP input rings.
 */
void noa_wlan_hw_nep_rings_input_activate(bool activate);

/**
 * @brief Activate/Deactivate NEP Output Rings
 *
 * @param[in] activate Deactivate or activate NEP output rings.
 */
void noa_wlan_hw_nep_rings_output_activate(bool activate);

/**
 * @brief Get NEP ring register for any ring
 *
 * @param[in] interface Interface id for intended ring
 * @param[in] flow Flow id for intended ring
 * @param[in] category Category id for intended ring
 * @param[in] direction Direction id for intended ring
 * @param[out] regs  The register address for the NEP ring
 *
 * @return 0 on success, or a negative error code on failure..
 */
int noa_wlan_hw_nep_ring_reg_get(uint8_t interface, uint8_t flow, uint8_t category,
				 uint8_t direction, struct noa_ring_regs *regs);

/**
 * @brief Query NEP ring registers
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 * @param[in] type Type of NEP ring.
 * @param[out] regs The register address for the NEP ring.
 */
int noa_wlan_hw_nep_ring_reg_query(struct noa_wlan_client *client, int type,
				   struct noa_ring_regs *regs);

/**
 * @brief Initialize and register WLAN to the NOA dynamic switch manager.
 *
 * @param[in] priv A void pointer to private data required for initialization.
 */
int noa_wlan_hw_dynamic_switch_init(void *priv);

/**
 * @brief Deinitialize and unregister WLAN to the NOA dynamic switch manager.
 *
 * @param[in] priv A void pointer to private data required for deinitialization.
 */
void noa_wlan_hw_dynamic_switch_deinit(void *priv);

/**
 * @brief Register crash dump for NOA WLAN.
 *
 * @param[in] client Pointer to the NOA WLAN client structure.
 *
 * @return 0 on success, or a negative error code on failure..
 */
int noa_wlan_hw_crash_dump_register(struct noa_wlan_client *client);

/**
 * @brief Unregister crash dump for NOA WLAN.
 */
void noa_wlan_hw_crash_dump_unregister(void);

#endif // __NOA_WLAN_HW_H__
