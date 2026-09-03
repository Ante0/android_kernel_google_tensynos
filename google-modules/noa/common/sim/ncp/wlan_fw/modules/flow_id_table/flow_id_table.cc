#include "flow_id_table.h"

#include "wlan_log/wlan_log.h"
#include "wlan_cast.h"
#include "sys_if/memory/sys_if_memory.h"
#include "sys_if/noa_net/noa_net.h"

#define DEFAULT_STA_INFO_IDX 0

int32_t UpdateUp2FlowPriorityTable(FlowIdTable *tbl, const uint8_t type, const uint8_t *table)
{
	if (table == NULL || type >= FLOW_PRIO_NUM) {
		return -EINVAL;
	}
	tbl->up2flow.type = type;
	memcpy(&tbl->up2flow.table, table, sizeof(uint8_t) * NUMPRIO);

	return 0;
}

int32_t FlowIdTableInit(FlowIdTable *tbl, ExternalServices *const ext_svc)
{
	int i = 0, j = 0;
	Up2FlowPriorityTable *up2flow = &tbl->up2flow;

	up2flow->type = 0;
	memset(&up2flow->table, 0, sizeof(uint8_t) * NUMPRIO);

	for (i = 0; i < WLAN_MAX_IFS; i++) {
		tbl->bssidx2oif[i] = INVALID_OIF;
	}

	for (i = 0; i < WLAN_MAX_IFS; i++) {
		tbl->if_flow_lk_up[i].role = IF_ROLE_NUM;
		for (j = 0; j < MAX_SOFT_AP_CLIENTS; j++) {
			StaFlowInfoNode *sta_flow_info = &tbl->if_flow_lk_up[i].sta_flow_info[j];
			sta_flow_info->enable = 0;
			sta_flow_info->ifindex = WLAN_MAX_IFS;
			memset(sta_flow_info->da, 0, ETH_MAC_LEN);
			for (int k = 0; k < NUMPRIO; k++) {
				sta_flow_info->flow_info[k].flowid = INVALID_FLOWID;
				sta_flow_info->flow_info[k].prio = INVALID_FLOW_PRIORITY;
			}
		}
	}

	/// The procedure to parse the DSCP should follow the same logic in
	/// the bcm4390/bcmutils.c
	for (i = 0; i < DSCP_MAX_NUM; i++) {
		switch (i) {
		case DSCP_EF:
		case DSCP_VA:
			tbl->dscp2up[i] = PRIO_8021D_VO;
			break;
		case DSCP_AF31:
		case DSCP_AF32:
		case DSCP_AF33:
		case DSCP_CS3:
			tbl->dscp2up[i] = PRIO_8021D_CL;
			break;
		case DSCP_AF21:
		case DSCP_AF22:
		case DSCP_AF23:
			tbl->dscp2up[i] = PRIO_8021D_EE;
			break;
		case DSCP_AF11:
		case DSCP_AF12:
		case DSCP_AF13:
		case DSCP_CS2:
			tbl->dscp2up[i] = PRIO_8021D_BE;
			break;
		case DSCP_CS6:
		case DSCP_CS7:
			tbl->dscp2up[i] = PRIO_8021D_NC;
			break;
		default:
			tbl->dscp2up[i] = i;
			break;
		}
	}

	if (ext_svc) {
		tbl->ext_svc = ext_svc;
		ExtSvcSendCommandToNetEngine(ext_svc, CMD_PUT_DSCP2UP_MAP, tbl->dscp2up,
					     sizeof(tbl->dscp2up));
	}

	return 0;
}

int32_t FlowIdTableDeinit(FlowIdTable *tbl)
{
	memset(tbl, 0, sizeof(FlowIdTable));
	return 0;
}

void PrintAllValidFlowIds(FlowIdTable *tbl)
{
	bool header_printed = false;
	bool any_flows_found_at_all = false;
	FlowIdLookUpTable *if_flow_lk_up;
	StaFlowInfoNode *sta_flow_info;
	FlowInfoNode *flow_info;

	if (!tbl) {
		WLAN_LOG_INFO(Shell, "Error: FlowIdTable has not been initialized.\n");
		return;
	}

	WLAN_LOG_INFO(Shell, "========== Flow ID Table Dump ==========\n");

	/* Iterate through all possible interface indexes */
	for (int ifindex = 0; ifindex < WLAN_MAX_IFS; ifindex++) {
		if_flow_lk_up = &tbl->if_flow_lk_up[ifindex];
		header_printed = false;
		for (int sta_idx = 0; sta_idx < MAX_SOFT_AP_CLIENTS; sta_idx++) {
			sta_flow_info = &if_flow_lk_up->sta_flow_info[sta_idx];

			if (!sta_flow_info->enable) {
				continue;
			}

			for (int i = 0; i < NUMPRIO; i++) {
				flow_info = &sta_flow_info->flow_info[i];

				if (flow_info->flowid == INVALID_FLOWID) {
					continue;
				}

				if (!header_printed) {
					WLAN_LOG_INFO(Shell, "Interface Index: %d, OIF: %" PRIu32 "\n",
						      ifindex, tbl->bssidx2oif[ifindex]);
					header_printed = true;
				}

				WLAN_LOG_INFO(Shell,
					      "Flow ID: %u, Priority: %u, OIF: %" PRIu32 ","
					      " DA: %02x:%02x:%02x:%02x:%02x:%02x\n",
					      flow_info->flowid, flow_info->prio,
					      tbl->bssidx2oif[ifindex],
					      sta_flow_info->da[0], sta_flow_info->da[1],
					      sta_flow_info->da[2], sta_flow_info->da[3],
					      sta_flow_info->da[4], sta_flow_info->da[5]);

				any_flows_found_at_all = true;
			}
		}
	}
	if (!any_flows_found_at_all) {
		WLAN_LOG_INFO(Shell, "\nNo valid flow IDs found in the table.\n");
	}
	WLAN_LOG_INFO(Shell, "\n======================================\n");
}

static int32_t FindMatchOrAvailableFlowInfoIndex(const FlowIdLookUpTable *if_flow_lk_up,
						 const uint8_t ifindex, const uint8_t *da,
						 bool find_available)
{
	int i = 0;
	const StaFlowInfoNode *sta_flow_info;
	int first_empty_idx = -1;
	int role = if_flow_lk_up[ifindex].role;

	if (IF_ROLE_GENERIC_STA(role)) {
		return DEFAULT_STA_INFO_IDX;
	}

	for (i = 0; i < MAX_SOFT_AP_CLIENTS; i++) {
		sta_flow_info = &if_flow_lk_up[ifindex].sta_flow_info[i];
		if (sta_flow_info->enable && !memcmp(sta_flow_info->da, da, ETH_MAC_LEN)) {
			return i;
		} else if (sta_flow_info->enable == 0 && first_empty_idx == -1) {
			first_empty_idx = i;
		}
	}

	return find_available == 1 ? first_empty_idx : -EINVAL;
}

int32_t AddFlowIdLookUpTableEntry(FlowIdTable *tbl, const uint32_t oif, const uint8_t ifindex,
				  const uint16_t flowid, const uint8_t prio, const uint8_t *da,
				  const uint8_t role)
{
	StaFlowInfoNode *sta_flow_info;
	FlowInfoNode *fl_info_node;
	FlowIdLookUpTable *if_flow_lk_up = tbl->if_flow_lk_up;
	int32_t sta_idx;

	if (da == NULL || ifindex >= WLAN_MAX_IFS || if_flow_lk_up == NULL || prio >= NUMPRIO) {
		WLAN_LOG_ERROR(Shell, "Invalid parameters for GetFlowInfoNode.\n");
		return -EINVAL;
	}

	if_flow_lk_up[ifindex].role = role;

	sta_idx = FindMatchOrAvailableFlowInfoIndex(if_flow_lk_up, ifindex, da, true);
	if (sta_idx < 0) {
		return -EINVAL;
	}

	tbl->bssidx2oif[ifindex] = oif;

	sta_flow_info = &if_flow_lk_up[ifindex].sta_flow_info[sta_idx];
	if (!sta_flow_info->enable) {
		sta_flow_info->ifindex = ifindex;
		memcpy(sta_flow_info->da, da, sizeof(uint8_t) * ETH_MAC_LEN);
		sta_flow_info->enable = 1;
	}

	fl_info_node = &sta_flow_info->flow_info[prio];
	fl_info_node->flowid = flowid;
	fl_info_node->prio = prio;

	PrintAllValidFlowIds(tbl);

	return 0;
}

static int32_t TranslateUp2FlowPriority(const Up2FlowPriorityTable *up2flow_tbl, const int32_t up)
{
	if (up < 0 || up > MAXPRIO) {
		return -EINVAL;
	}

	return up2flow_tbl->table[up];
}

static int32_t TranslateFlowPriority2FlowId(const FlowIdLookUpTable *if_flow_lk_up,
					    const uint8_t ifindex, const int32_t prio,
					    const uint8_t *da)
{
	int32_t i = 0, flowid = 0;
	i = FindMatchOrAvailableFlowInfoIndex(if_flow_lk_up, ifindex, da, false);
	if (i < 0) {
		return -ENOENT;
	}

	flowid = if_flow_lk_up[ifindex].sta_flow_info[i].flow_info[prio].flowid;
	if (flowid == INVALID_FLOWID) {
		WLAN_LOG_ERROR(Shell,
			       "No flowid found for ifindex %u, prio %d,"
			       " DA %02x:%02x:%02x:%02x:%02x:%02x ",
			       ifindex, prio, da[0], da[1], da[2], da[3], da[4], da[5]);
		return -ENOENT;
	}

	return flowid;
}

int32_t FlowIdLookUp(const FlowIdTable *tbl, const uint16_t ethertype, const uint8_t up,
		     const uint8_t ifindex, const void *da)
{
	int32_t flow_prio = 0;
	int32_t flow_id = 0;

	if (ethertype != ETH_P_IPV6 && ethertype != ETH_P_IP) {
		WLAN_LOG_ERROR(Shell, "Unsupported ethertype: %u\n", ethertype);
		return -EINVAL;
	}

	flow_prio = TranslateUp2FlowPriority(&tbl->up2flow, up);
	if (flow_prio < 0) {
		return -EINVAL;
	}

	WLAN_LOG_INFO(Shell, "Flow priority: %d", flow_prio);

	flow_id = TranslateFlowPriority2FlowId(tbl->if_flow_lk_up, ifindex, flow_prio,
					       WLAN_REINTERPRET_CAST(const uint8_t *, da));

	if (flow_id < 0) {
		return -EINVAL;
	}

	return flow_id;
}

int32_t DeleteFlowIdLookUpTableEntry(FlowIdTable *tbl, const uint32_t oif, const uint8_t ifindex,
				     const uint16_t flowid)
{
	StaFlowInfoNode *sta_flow_info;
	FlowIdLookUpTable *lookup_table;
	FlowInfoNode *flow_info;
	int i = 0, j = 0, empty_flow = 0, flow_removed = 0;

	if (!tbl) {
		WLAN_LOG_ERROR(Shell, "Error: FlowIdTable not initialized.\n");
		return -EINVAL;
	}

	if (ifindex >= WLAN_MAX_IFS) {
		WLAN_LOG_ERROR(
			Shell,
			"Error: Invalid ifindex %d or flowid %d for DeleteFlowIdLookUpTableEntry\n",
			ifindex, flowid);
		return -EINVAL;
	}

	lookup_table = &tbl->if_flow_lk_up[ifindex];

	for (i = 0; i < MAX_SOFT_AP_CLIENTS; i++) {
		sta_flow_info = &lookup_table->sta_flow_info[i];
		if (!sta_flow_info->enable) {
			continue;
		}

		empty_flow = 0;
		for (j = 0; j < NUMPRIO; j++) {
			flow_info = &sta_flow_info->flow_info[j];
			if (flow_info->flowid == flowid) {
				flow_info->flowid = INVALID_FLOWID;
				flow_info->prio = INVALID_FLOW_PRIORITY;
				WLAN_LOG_INFO(Shell, "Deleted flowid %u for ifindex %u\n", flowid,
					      ifindex);
				flow_removed = 1;
			}
			if (flow_info->flowid == INVALID_FLOWID) {
				empty_flow++;
			}
		}

		if (empty_flow == NUMPRIO) {
			// If all flow info nodes are cleared, disable the STA flow info
			sta_flow_info->enable = 0;
			sta_flow_info->ifindex = WLAN_MAX_IFS;
			memset(sta_flow_info->da, 0, ETH_MAC_LEN);
			WLAN_LOG_INFO(Shell,
				      "Disabled STA flow info for ifindex %u, sta index %d\n",
				      ifindex, i);
		}
	}

	PrintAllValidFlowIds(tbl);

	if (!flow_removed) {
		WLAN_LOG_ERROR(Shell, "%s: Could not find flowid %u to delete on ifindex %u.\n",
			       __func__, flowid, ifindex);
		return -ENOENT;
	}

	return 0;
}

uint8_t TranslateFlowPriority2Up(Up2FlowPriorityTable *up2flow_tbl, uint8_t flow_prio,
				 uint8_t *candidates)
{
	uint8_t idx = 0;

	if (flow_prio > MAXPRIO) {
		return 0;
	}

	for (int i = 0; i < NUMPRIO; i++) {
		if (up2flow_tbl->table[i] == flow_prio) {
			candidates[idx++] = i;
		}
	}

	return idx;
}

int32_t FindOifFromBssIdx(FlowIdTable *tbl, const uint8_t bss_idx)
{
	if (!tbl) {
		return -EINVAL;
	} else {
		return tbl->bssidx2oif[bss_idx];
	}
}
