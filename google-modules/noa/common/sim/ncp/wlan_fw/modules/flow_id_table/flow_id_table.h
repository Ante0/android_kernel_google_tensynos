#ifndef MODULES_FLOW_ID_TABLE_FLOW_ID_TABLE_H
#define MODULES_FLOW_ID_TABLE_FLOW_ID_TABLE_H

#define ETH_P_IPV6 0x86DD /* IPv6 over bluebook */
#define ETH_P_IP 0x0800 /* Internet Protocol packet */

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "ext_svc/ext_svc.h"

#define PRIO_8021D_NONE 2 /* None = - */
#define PRIO_8021D_BK 1 /* BK - Background */
#define PRIO_8021D_BE 0 /* BE - Best-effort */
#define PRIO_8021D_EE 3 /* EE - Excellent-effort */
#define PRIO_8021D_CL 4 /* CL - Controlled Load */
#define PRIO_8021D_VI 5 /* Vi - Video */
#define PRIO_8021D_VO 6 /* Vo - Voice */
#define PRIO_8021D_NC 7 /* NC - Network Control */
#define MAXPRIO 7
#define NUMPRIO (MAXPRIO + 1)

/// Copied from bcmutils.h
/* DSCP type definitions (RFC4594) */
/* DF: Standard (RFC2474) */
#define DSCP_DF 0x00u
/* AF1x: High-Throughput Data (RFC2597) */
#define DSCP_AF11 0x0Au
#define DSCP_AF12 0x0Cu
#define DSCP_AF13 0x0Eu
/* CS1: Low-Priority Data (RFC3662) */
#define DSCP_CS1 0x08u
/* AF2x: Low-Latency Data (RFC2597) */
#define DSCP_AF21 0x12u
#define DSCP_AF22 0x14u
#define DSCP_AF23 0x16u
/* CS2: OAM (RFC2474) */
#define DSCP_CS2 0x10u
/* AF3x: Multimedia Streaming (RFC2597) */
#define DSCP_AF31 0x1Au
#define DSCP_AF32 0x1Cu
#define DSCP_AF33 0x1Eu
/* CS3: Broadcast Video (RFC2474) */
#define DSCP_CS3 0x18u
/* AF4x: Multimedia Conferencing (RFC2597) */
#define DSCP_AF41 0x22u
#define DSCP_AF42 0x24u
#define DSCP_AF43 0x26u
/* CS4: Real-Time Interactive (RFC2474) */
#define DSCP_CS4 0x20u
/* CS5: Signaling (RFC2474) */
#define DSCP_CS5 0x28u
/* VA: VOCIE-ADMIT (RFC5865) */
#define DSCP_VA 0x2Cu
/* EF: Telephony (RFC3246) */
#define DSCP_EF 0x2Eu
/* CS6: Network Control (RFC2474) */
#define DSCP_CS6 0x30u
/* CS7: Network Control (RFC2474) */
#define DSCP_CS7 0x38u

/*
 * Takes a pointer, returns true if a 48-bit multicast address
 * (including broadcast, since it is all ones)
 */
#define ETHER_ISMULTI(ea) (((const uint8_t *)(ea))[0] & 1)
#define ETH_P_IPV6 0x86DD /* IPv6 over bluebook */
#define ETH_P_IP 0x0800 /* Internet Protocol packet */

#define MAX_SOFT_AP_CLIENTS 10
#define ETH_MAC_LEN 6
#define WLAN_MAX_IFS 16u
#define DSCP_MAX_NUM 64u

enum { FLOW_PRIO_AC_MAP, FLOW_PRIO_TID_MAP, FLOW_PRIO_LLR_MAP, FLOW_PRIO_NUM };

#define IF_ROLE_STA 0
#define IF_ROLE_AP 1
#define IF_ROLE_WDS 2
#define IF_ROLE_P2P_GO 3
#define IF_ROLE_P2P_CLIENT 4
#define IF_ROLE_IBSS 8
#define IF_ROLE_NAN 9
#define IF_ROLE_MESH 10u
#define IF_ROLE_NAN_NMI 11u
#define IF_ROLE_NUM 12

#define INVALID_FLOWID 0xFFFF
#define INVALID_FLOW_PRIORITY 0xFF
#define INVALID_OIF 0xFFFFFFFF

#define IF_ROLE_GENERIC_STA(role)                                                                  \
	((role) == IF_ROLE_STA || (role) == IF_ROLE_P2P_GO || (role) == IF_ROLE_WDS)

typedef struct Up2FlowPriorityTable {
	uint8_t type;
	uint8_t table[NUMPRIO];
} __attribute__((aligned(4))) Up2FlowPriorityTable;

/// @brief Structure representing each flowid corresponding to da and prio.
typedef struct FlowInfoNode {
	uint16_t flowid;
	uint8_t prio;
} __attribute__((aligned(4))) FlowInfoNode;

typedef struct StaFlowInfoNode {
	uint8_t enable;
	uint8_t ifindex;
	uint8_t da[ETH_MAC_LEN];
	FlowInfoNode flow_info[NUMPRIO];
} __attribute__((aligned(4))) StaFlowInfoNode;

/// @brief Structure representing flow id look up table.
///
/// Follow the same design of if_flow_lkup in WiFi driver.
typedef struct FlowIdLookUpTable {
	uint8_t role;
	StaFlowInfoNode sta_flow_info[MAX_SOFT_AP_CLIENTS];
} __attribute__((aligned(4))) FlowIdLookUpTable;

typedef struct FlowIdTable {
	uint8_t dscp2up[DSCP_MAX_NUM];
	uint32_t bssidx2oif[WLAN_MAX_IFS];
	Up2FlowPriorityTable up2flow;
	FlowIdLookUpTable if_flow_lk_up[WLAN_MAX_IFS];
	ExternalServices *ext_svc;
} __attribute__((aligned(4))) FlowIdTable;

int32_t UpdateUp2FlowPriorityTable(FlowIdTable *tbl, const uint8_t type, const uint8_t *table);

int32_t FlowIdTableInit(FlowIdTable *tbl, ExternalServices *const ext_svc);

int32_t FlowIdTableDeinit(FlowIdTable *tbl);

int32_t AddFlowIdLookUpTableEntry(FlowIdTable *tbl, const uint32_t oif, const uint8_t ifindex,
				  const uint16_t flowid, const uint8_t prio, const uint8_t *da,
				  const uint8_t role);

int32_t DeleteFlowIdLookUpTableEntry(FlowIdTable *tbl, const uint32_t oif, const uint8_t ifindex,
				     const uint16_t flowid);

int32_t FlowIdLookUp(const FlowIdTable *tbl, const uint16_t ethertype, const uint8_t up,
		     const uint8_t ifindex, const void *da);

uint8_t TranslateFlowPriority2Up(Up2FlowPriorityTable *up2flow_tbl, uint8_t flow_prio,
				 uint8_t *candidates);

void PrintAllValidFlowIds(FlowIdTable *tbl);

int32_t FindOifFromBssIdx(FlowIdTable *tbl, const uint8_t bss_idx);

#endif // MODULES_FLOW_ID_TABLE_FLOW_ID_TABLE_H