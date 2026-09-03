#include "sta_table.h"

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "ext_svc/ext_svc.h"
#include "sys_if/noa_net/noa_net.h"
#include "wlan_log/wlan_log.h"

static int32_t AddFlowIdMappings(uint32_t tid, const StaInfo *const sta_info,
				 ExternalServices *const ext_svc)
{
	NoaNetTetherFlowIdEntry cmd;
	int32_t ret;

	SysIfTetherFlowIdEntryInit(&cmd);

	// Copy the station MAC address to the command structure.
	memcpy(cmd.key.dstMac, sta_info->mac_addr, kMacAddressLen);
	cmd.key.priority = tid;
	cmd.value = sta_info->qos_txq_map[tid];

	ret = ExtSvcSendCommandToNetEngine(ext_svc, CMD_PUT_FLOWID_MAP, &cmd, sizeof(cmd));
	if (ret != 0) {
		WLAN_LOG_WARN(Nep,
			      "%s(): add station(%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16
			      ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ") with TID: %" PRIu32
			      " failed.",
			      __func__, sta_info->mac_addr[0], sta_info->mac_addr[1],
			      sta_info->mac_addr[2], sta_info->mac_addr[3], sta_info->mac_addr[4],
			      sta_info->mac_addr[5], tid);
	}

	return ret;
}

static int32_t SetupFlowIdMappings(const StaInfo *const sta_info, ExternalServices *const ext_svc)
{
	int32_t ret;
	uint32_t tid;

	// Add flow ID mappings for each valid TID.
	for (tid = kWlanTidStart; tid < kWlanTidEnd; tid++) {
		if (sta_info->qos_txq_map[tid] != INVALID_TXQ_ID) {
			ret = AddFlowIdMappings(tid, sta_info, ext_svc);
			if (ret) {
				return ret;
			}
		}
	}

	return 0;
}

static int32_t UpdateFlowIdMappings(StaInfo *entry, const StaInfo *const sta_info,
				    ExternalServices *const ext_svc)
{
	uint32_t tid;
	int32_t ret;

	for (tid = kWlanTidStart; tid < kWlanTidEnd; tid++) {
		if (sta_info->qos_txq_map[tid] == INVALID_TXQ_ID &&
		    sta_info->qos_txq_map[tid] == entry->qos_txq_map[tid]) {
			continue;
		}
		entry->qos_txq_map[tid] = sta_info->qos_txq_map[tid];
		ret = AddFlowIdMappings(tid, sta_info, ext_svc);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int32_t RemoveFlowIdMappings(const StaInfo *const sta_info, ExternalServices *const ext_svc)
{
	NoaNetTetherFlowIdKey cmd;
	uint32_t tid;
	int32_t ret;

	SysIfTetherFlowIdKeyInit(&cmd);

	// Copy the station MAC address to the command structure.
	memcpy(cmd.dstMac, sta_info->mac_addr, kMacAddressLen);

	// Remove flow ID mappings for each TID.
	for (tid = kWlanTidStart; tid < kWlanTidEnd; tid++) {
		if (sta_info->qos_txq_map[tid] != INVALID_TXQ_ID) {
			cmd.priority = tid;
			ret = ExtSvcSendCommandToNetEngine(ext_svc, CMD_REMOVE_FLOWID_MAP, &cmd,
							   sizeof(cmd));
			if (ret) {
				WLAN_LOG_WARN(Nep,
					      "%s(): remove station(%02" PRIx16 ":%02" PRIx16
					      ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16
					      ":%02" PRIx16 ") with TID: %" PRIu32 " failed.",
					      __func__, sta_info->mac_addr[0],
					      sta_info->mac_addr[1], sta_info->mac_addr[2],
					      sta_info->mac_addr[3], sta_info->mac_addr[4],
					      sta_info->mac_addr[5], tid);
				return ret;
			}
		}
	}

	return 0;
}

static bool FindNonStationModeInStaTable(const StaTable *const table)
{
	uint32_t i;

	for (i = 0; i < table->num_sta_info; i++) {
		if (table->sta_info_list[i].enable &&
		    table->sta_info_list[i].lmac_id != STA_MODE_LMAC_ID) {
			return true;
		}
	}
	return false;
}

static int32_t RemoveStaTableHelper(StaTable *const table, uint32_t sta_info_list_idx)
{
	int32_t ret;
	StaInfo *sta_info = &table->sta_info_list[sta_info_list_idx];

	ret = RemoveFlowIdMappings(sta_info, table->ext_svc);
	if (ret) {
		return ret;
	}

	memset(sta_info, 0, sizeof(StaInfo));
	table->num_active_sta--;
	table->hotspot_en = FindNonStationModeInStaTable(table);
	return 0;
}

static int32_t FindStationInStaTable(const StaTable *const table, const uint8_t *const sta_mac_addr,
				     const uint32_t oif, uint32_t *sta_info_list_idx)
{
	uint32_t i;

	for (i = 0; i < table->num_sta_info; i++) {
		if (table->sta_info_list[i].enable) {
			if (table->sta_info_list[i].oif == oif &&
			    table->sta_info_list[i].lmac_id == STA_MODE_LMAC_ID) {
				*sta_info_list_idx = i;
				return 0;
			}
			if (!memcmp(table->sta_info_list[i].mac_addr, sta_mac_addr,
				    kMacAddressLen)) {
				*sta_info_list_idx = i;
				return 0;
			}
		}
	}

	return -ENODEV;
}

int32_t StaTableInit(StaTable *const table, ExternalServices *const ext_svc)
{
	if (!table || !ext_svc) {
		return -EINVAL;
	}

	memset(table, 0, sizeof(StaTable));
	table->num_sta_info = MAX_NUM_STA_SUPPORT;
	table->ext_svc = ext_svc;

	return 0;
}

void StaTableDeinit(StaTable *const table)
{
	uint32_t i;

	for (i = 0; i < table->num_sta_info; i++) {
		if (table->sta_info_list[i].enable) {
			// Currently, if we fail to remove the station, no further action
			// is possible.
			RemoveStaTableHelper(table, i);
		}
	}

	memset(table, 0, sizeof(StaTable));
}

int32_t StaTableGetStaInfo(const StaTable *const table, const uint8_t *const sta_mac_addr,
			   const uint32_t oif, const StaInfo **retrieved_sta_info)
{
	uint32_t sta_info_list_idx = 0;

	if (FindStationInStaTable(table, sta_mac_addr, oif, &sta_info_list_idx) != 0) {
		return -ENODEV;
	}

	*retrieved_sta_info = &table->sta_info_list[sta_info_list_idx];
	return 0;
}

int32_t StaTableAddStation(StaTable *const table, const uint8_t sta_idx,
			   const StaInfo *const sta_info)
{
	StaInfo *entry;
	uint32_t tid = 0;

	if (table == NULL || sta_info == NULL || sta_idx >= table->num_sta_info) {
		return -EINVAL;
	}
	// Get the entry that needs to be updated or added.
	entry = &table->sta_info_list[sta_idx];

	if (memcmp(entry->mac_addr, sta_info->mac_addr, kMacAddressLen) == 0) {
		return UpdateFlowIdMappings(entry, sta_info, table->ext_svc);
	}

	WLAN_LOG_INFO(Cfg, "%s(): active sta %" PRIu32 ", %" PRIu16 ", %" PRIu16, __func__,
		      sta_info->oif, sta_info->bss_idx, sta_info->enable);
	WLAN_LOG_INFO(Cfg,
		      "qos_map (%" PRIu16 ", %" PRIu16 ", %" PRIu16 ", %" PRIu16 ", %" PRIu16
		      ", %" PRIu16 ", %" PRIu16 ", %" PRIu16 ")",
		      sta_info->qos_txq_map[0], sta_info->qos_txq_map[1], sta_info->qos_txq_map[2],
		      sta_info->qos_txq_map[3], sta_info->qos_txq_map[4], sta_info->qos_txq_map[5],
		      sta_info->qos_txq_map[6], sta_info->qos_txq_map[7]);
	WLAN_LOG_INFO(Cfg,
		      "mac %02" PRIX16 ":%02" PRIX16 ":%02" PRIX16 ":%02" PRIX16 ":%02" PRIX16
		      ":%02" PRIX16,
		      sta_info->mac_addr[0], sta_info->mac_addr[1], sta_info->mac_addr[2],
		      sta_info->mac_addr[3], sta_info->mac_addr[4], sta_info->mac_addr[5]);
	WLAN_LOG_INFO(Cfg, "encrypt_type %" PRIu16 ", encap_type %" PRIu16 ", lmac_id %" PRIu16,
		      sta_info->encrypt_type, sta_info->encap_type, sta_info->lmac_id);
	WLAN_LOG_INFO(Cfg, "bmid %" PRIu16 ", search_idx %" PRIu32 ", search_type %" PRIu32,
		      sta_info->bmid, sta_info->search_idx, sta_info->search_type);
	WLAN_LOG_INFO(Cfg, "dscp_tid_map_id %" PRIu32 ", addry_en %" PRIu32 ", addrx_en %" PRIu32,
		      sta_info->dscp_tid_map_id, sta_info->addry_en, sta_info->addrx_en);

	// The disabled entry will be reused, otherwise no updates for the num_active_sta
	if (!entry->enable) {
		table->num_active_sta++;
	}

	entry->enable = true;
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
	memcpy(entry->mac_addr, sta_info->mac_addr, kMacAddressLen);

	// Updates QoS to flowid mapping.
	for (tid = kWlanTidStart; tid < kWlanTidEnd; tid++) {
		entry->qos_txq_map[tid] = sta_info->qos_txq_map[tid];
	}

	// Update the hotspot settings if the device is not operating in station (client) mode.
	if (entry->lmac_id != STA_MODE_LMAC_ID) {
		table->hotspot_en = true;
	}

	// Updates the flow ID table in the net engine.
	return SetupFlowIdMappings(entry, table->ext_svc);
}

int32_t StaTableRemoveStation(StaTable *const table, const uint8_t sta_idx)
{
	if (sta_idx >= table->num_sta_info) {
		return -EINVAL;
	}

	return RemoveStaTableHelper(table, sta_idx);
}
