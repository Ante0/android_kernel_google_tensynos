#ifndef MODULES_STA_TABLE_STA_TABLE_H
#define MODULES_STA_TABLE_STA_TABLE_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "ext_svc/ext_svc.h"

/// @brief Maximum number of stations supported by the Station Table.
#define MAX_NUM_STA_SUPPORT 10U
/// @brief Invalid TXQ ID value.
#define INVALID_TXQ_ID 0xFFFFU
/// @brief Invalid Station ID value.
#define INVALID_STA_ID 0xFFU
/// @brief LMAC ID for station mode.
#define STA_MODE_LMAC_ID 0

/// @brief WLAN TID values.
///
/// These values represent the priority of a WLAN packet.
enum WlanTid {
	kWlanTidStart = 0,
	kWlanTid0 = kWlanTidStart,
	kWlanTid1,
	kWlanTid2,
	kWlanTid3,
	kWlanTid4,
	kWlanTid5,
	kWlanTid6,
	kWlanTid7,
	kWlanTidEnd,
	kWlanTidNum = kWlanTidEnd,
};

/// @brief MAC address parts.
enum MacAddressPart {
	kMacAddressPartStart = 0,
	// Organizationally Unique Identifier (OUI)
	kMacAddressPartOui0 = kMacAddressPartStart,
	kMacAddressPartOui1,
	kMacAddressPartOui2,
	// Network Interface Controller (NIC) specific identifier
	kMacAddressPartNci0,
	kMacAddressPartNci1,
	kMacAddressPartNci2,
	kMacAddressPartEnd,
	kMacAddressLen = kMacAddressPartEnd,
};

/// @brief Structure representing station information.
typedef struct StaInfo {
	/// @brief Output interface.
	uint32_t oif;
	/// @brief BSS index.
	uint16_t bss_idx;
	/// @brief QoS to TXQ mapping.
	uint16_t qos_txq_map[kWlanTidNum];
	/// @brief MAC address.
	uint8_t mac_addr[kMacAddressLen];
	/// @brief Encryption type.
	uint8_t encrypt_type : 4;
	/// @brief Encapsulation type.
	uint8_t encap_type : 2;
	/// @brief LMAC ID.
	uint8_t lmac_id : 2;
	/// @brief Buffer manager ID.
	uint8_t bmid;
	/// @brief fw_metadata for exception handling.
	uint16_t fw_metadata;
	/// @brief Search index.
	uint32_t search_idx : 20;
	/// @brief Search type.
	uint32_t search_type : 2;
	/// @brief DSCP to TID mapping ID.
	uint32_t dscp_tid_map_id : 6;
	/// @brief Address Y enable.
	uint32_t addry_en : 1;
	/// @brief Address X enable.
	uint32_t addrx_en : 1;
	/// @brief Reserved for future use.
	uint32_t reserved2 : 2;
	/// @brief Station enable flag.
	uint8_t enable;
	/// @brief Station ID.
	uint8_t sta_id;
	/// @brief Reserved for future use.
	uint8_t reserved3[2];
} __attribute__((packed, aligned(4))) StaInfo;

/// @brief Structure representing the Station Table.
typedef struct StaTable {
	/// @brief Number of station entries in the table.
	uint32_t num_sta_info;
	/// @brief Number of active stations in the table.
	uint32_t num_active_sta;
	/// @brief Hotspot enable flag.
	bool hotspot_en;
	/// @brief Pointer to external services.
	ExternalServices *ext_svc;
	/// @brief Array of station entries.
	StaInfo sta_info_list[MAX_NUM_STA_SUPPORT];
} StaTable;

/// @brief Initializes the station table.
///
/// @param[in] table A pointer to the station table to be initialized.
/// @param[in] ext_svc A pointer to the external services.
/// @return 0 on success, a negative error code otherwise.
extern int32_t StaTableInit(StaTable *const table, ExternalServices *const ext_svc);

/// @brief Deinitializes the station table.
///
/// @param[in] table A pointer to the station table to be deinitialize.
extern void StaTableDeinit(StaTable *const table);

/// @brief Gets the station information for a given MAC address.
///
/// @param[in] table A pointer to the station table.
/// @param[in] sta_mac_addr A pointer to the MAC address of the station.
/// @param[in] oif An index of output interface.
/// @param[out] retrieved_sta_info A pointer to a pointer where the retrieved
/// station information will be stored.
///
/// @return The search result.
/// @retval 0 If the station information is found.
/// @retval -ENODEV If the given MAC address is unknown.
extern int32_t StaTableGetStaInfo(const StaTable *const table, const uint8_t *const sta_mac_addr,
				const uint32_t oif, const StaInfo **retrieved_sta_info);

/// @brief Adds a station to the station table.
///
/// @param[in] table A pointer to the Station Table.
/// @param[in] sta_idx The index of the station in the Station Table.
/// @param[in] sta_info A pointer to the station information to be added.
///
/// @return The operation result.
/// @retval 0 Success.
/// @retval -ENOMEM No any available entry in the station table.
extern int32_t StaTableAddStation(StaTable *const table, const uint8_t sta_idx, const StaInfo *const sta_info);

/// @brief Removes a station from the Station Table.
///
/// @param[in] table A pointer to the Station Table.
/// @param[in] sta_idx The index of the station in the Station Table.
///
/// @return The operation result.
/// @retval 0 If the station is removed successfully.
/// @retval -ENODEV If the station does not exist in the station table.
extern int32_t StaTableRemoveStation(StaTable *const table, const uint8_t sta_idx);

#endif // MODULES_STA_TABLE_STA_TABLE_H
