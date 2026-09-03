/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA core header file
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_CORE_H__
#define __NOA_CORE_H__

#ifdef linux
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include "config.h"
#else
#include <cstdio>
#include <cstring>
#include <cinttypes>

#include "common/defs.h"
#include "linux_port/log.h"
#include "linux_port/types.h"
#include "linux_port/bitops.h"
#endif /* linux */

#define MAX_NAME_SIZE 32

#define NOA_DESC_VERSION_BIT_FIELD (2U)
#define NOA_DESC_MODE_BIT_FIELD (2U)
#define NOA_DESC_DATA_LENGTH_BIT_FIELD (12U)
#define NOA_DESC_HEAD_OFFSET_BIT_FIELD (12U)
#define NOA_DESC_DDONE_BIT_FIELD (1U)
#define NOA_DESC_REASON_BIT_FIELD (3U)
#define NOA_DESC_USER_INFO_BIT_FIELD (3U)
#define NOA_DESC_DESC_TYPE_BIT_FIELD (4U)
#define NOA_DESC_COPY_BIT_FIELD (2U)
#define NOA_DESC_FEEDBACK_BIT_FIELD (1U)
#define NEP_PKT_INFO_ACTION_BIT_FIELD (8U)

enum {
	NOA_PORT_WLAN_FW,
	NOA_PORT_WLAN_SW,
	NOA_PORT_MODEM_FW,
	NOA_PORT_MODEM_SW,
	NOA_PORT_NETENGINE,
	NOA_PORT_MAX,
};

typedef struct {
	uint8_t priority;
	uint8_t udp : 1;
	uint8_t tcp : 1;
	uint8_t ipv4 : 1;
	uint8_t ipv6 : 1;
	uint8_t is_wlan_forward : 1;
	uint8_t reserved : 3;
	uint8_t l3_len;
	uint8_t l4_len;
	uint16_t flowid;
	uint16_t ethertype;
	uint32_t oif;
} __attribute__((packed, aligned(4))) nep_forward_info;

enum {
	IPSEC_METADATA_STATUS_SKIP,
	IPSEC_METADATA_STATUS_SUCCESS,
	IPSEC_METADATA_STATUS_GENERIC_ERROR,
	IPSEC_METADATA_STATUS_AUTH_FAILED,
	IPSEC_METADATA_STATUS_DECRYPTION_MAX = 7,
};

struct noa_tx_ipsec_metadata {
	// Because the xfrm id is guaranteed to be non zero in the XFRM stack, if the xfrm id
	// is 0, it means the field is unset.
	u32 xfrm_interface_id;
} __attribute__((packed, aligned(4)));

struct noa_rx_ipsec_metadata {
	u8 status : 3;
	u8 status_reserved : 5;
	u8 reserved[3];

	// The ID to identify SA. It's currently set by SA SPI for now.
	// TODO(321875336): Set it by a more reliable value instead of SPI.
	u32 ipsec_handle;
} __attribute__((packed, aligned(4)));

#define NOA_TX_IPSEC_METADATA_BYTE (sizeof(struct noa_tx_ipsec_metadata))
#define NOA_RX_IPSEC_METADATA_BYTE (sizeof(struct noa_rx_ipsec_metadata))

struct network_ext_rxd {
	uint32_t iif; // interface index
} __attribute__((packed, aligned(4)));

struct network_ext_txd {
	nep_forward_info info;
	char dest[6];
	uint8_t reserved2[2];
} __attribute__((packed, aligned(4)));

typedef struct {
	u32 iif;
} __attribute__((packed, aligned(4))) nep_ppf_info;

enum NepPktAction {
	PKT_ACTION_UNDECIDED = 0,
	PKT_ACTION_FEEDTHROUGH,
	PKT_ACTION_FILTER_PACKET,
	PKT_ACTION_FALLBACK,
	PKT_ACTION_FORWARD_TO_WLAN,
	PKT_ACTION_FORWARD_TO_MODEM,
	PKT_ACTION_PROCESS_AS_VPN,
	PKT_ACTION_ROUTE_NETWORK_STACK,
	PKT_ACTION_MAX
};
static_assert(PKT_ACTION_MAX <= (1U << NEP_PKT_INFO_ACTION_BIT_FIELD));

typedef struct {
	// The processing action of the packet. Refer to NepPktAction enum.
	u8 action;
	// The length of the valid header data starting at header_address.
	uint8_t header_length;

	union { // The definition of this field changes depending on the `packet_action`.
		// Total length of the original packet in DRAM.
		uint16_t packet_length;
		// If the action is FORWARDING, this field indicates the length of
		// the unchanged payload.
		uint16_t unchanged_payload_length;
	};

	union { // The definition of this field changes depending on the `packet_action`.
		// Points to the full, original packet buffer in DRAM.
		uintptr_t packet_address;
		// If the action is forwarding, this address points to the
		// start of the unchanged payload.
		uintptr_t unchanged_payload_address;
	};
	// Points to the cached partial header in DTCM. This address
	// already accounts for HEADER_BUFFER_HEADROOM.
	uintptr_t header_address;
} __attribute__((packed, aligned(4))) nep_pkt_info;

typedef nep_pkt_info network_stack_desc;

#define NEP_HEADER_BUFFER_HEADROOM (32U)
#define NEP_HEADER_FETCH_SIZE (96U)
#define NEP_HEADER_BUFFER_SIZE ((NEP_HEADER_BUFFER_HEADROOM) + (NEP_HEADER_FETCH_SIZE))
#define NEP_HEADER_BUFFER_SIZE_BITSHIFT ((7U))
static_assert(NEP_HEADER_BUFFER_SIZE == (1U << NEP_HEADER_BUFFER_SIZE_BITSHIFT));
#define MAX_NEP_DEVICE_ENTRY_SIZE (32U)

typedef struct {
	nep_pkt_info pkt_info;
	union {
		nep_forward_info forward_info;
		nep_ppf_info ppf_info;
		struct noa_rx_ipsec_metadata ipsec_metadata;
		uint8_t reserved[MAX_NEP_DEVICE_ENTRY_SIZE - sizeof(nep_pkt_info)];
	};
} __attribute__((packed, aligned(4))) nep_device_entry;
static_assert(sizeof(nep_device_entry) <= MAX_NEP_DEVICE_ENTRY_SIZE);

#define MAX_NEP_HOST_ENTRY_SIZE (32U)

typedef struct {
	nep_pkt_info pkt_info;
	union {
		struct noa_tx_ipsec_metadata ipsec_metadata;
	};
} __attribute__((packed, aligned(4))) nep_host_entry;
static_assert(sizeof(nep_host_entry) <= MAX_NEP_HOST_ENTRY_SIZE);

#define NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(name)                                                    \
	static inline bool NEP_HOST_ENTRY_IS_##name(const void *entry)                             \
	{                                                                                          \
		return ((const nep_host_entry *)entry)->pkt_info.action == PKT_ACTION_##name;      \
	}                                                                                          \
	static inline bool NEP_DEVICE_ENTRY_IS_##name(const void *entry)                           \
	{                                                                                          \
		return ((const nep_device_entry *)entry)->pkt_info.action == PKT_ACTION_##name;    \
	}

NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(UNDECIDED)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(FEEDTHROUGH)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(FILTER_PACKET)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(FALLBACK)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(FORWARD_TO_WLAN)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(FORWARD_TO_MODEM)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(PROCESS_AS_VPN)
NEP_ENTRY_ACTION_IS_MATCHED_FUNCS(ROUTE_NETWORK_STACK)

struct noa_desc {
	union {
		u32 _DW0;

		struct {
			u32 ver : NOA_DESC_VERSION_BIT_FIELD;
			u32 mode : NOA_DESC_MODE_BIT_FIELD;
			u32 dl : NOA_DESC_DATA_LENGTH_BIT_FIELD;
			u32 head_offset : NOA_DESC_HEAD_OFFSET_BIT_FIELD;
			u32 ddone : NOA_DESC_DDONE_BIT_FIELD;
		};
	};

	union {
		u32 _DW1;

		struct {
			u32 dp_low;
		};
	};

	union {
		u32 _DW2;

		struct {
			u16 dp_high;
			u16 tkid;
		};
	};

	union {
		u32 _DW3;

		struct {
			u8 dst;
			u8 src;
			u16 reason : NOA_DESC_REASON_BIT_FIELD;
			u16 user_info : NOA_DESC_USER_INFO_BIT_FIELD;
			u16 desc_type : NOA_DESC_DESC_TYPE_BIT_FIELD;
			u16 cp : NOA_DESC_COPY_BIT_FIELD;
			u16 fk : NOA_DESC_FEEDBACK_BIT_FIELD;
		};
	};

	/*DW4, 5*/
	uint64_t dv;
	/**
	 * ext_data should be the last member, any new member
	 * should put before it. Also, you should ensure that
	 * the start address of ext_data would be aligned by 4
	 */
	u8 ext_data[0];
} __attribute__((packed, aligned(4)));

#define NOA_DESC_OFFSET_CHECKER(idx)                                                               \
	static_assert(sizeof(u32) * idx == offsetof(struct noa_desc, _DW##idx));
NOA_DESC_OFFSET_CHECKER(1)
NOA_DESC_OFFSET_CHECKER(2)
NOA_DESC_OFFSET_CHECKER(3)

static inline void ParseNoaDescToNepPktInfo(const struct noa_desc *desc, nep_pkt_info *info)
{
	info->packet_length = desc->dl - desc->head_offset;
	info->packet_address = desc->dv + desc->head_offset;
}

// TODO: b/318060433 - Add checker for last dw after we remove the dv field.

enum {
	NOAD_MODE_DATA,
	NOAD_MODE_FEEDBACK,
	NOAD_MODE_NOOP,
	NOAD_MODE_MAX,
};

static_assert(NOAD_MODE_MAX <= (1U << NOA_DESC_MODE_BIT_FIELD));

enum {
	NOAD_FEEDBACK_DISABLE = 0,
	NOAD_FEEDBACK_ENABLE = 1,
};

enum {
	NOAD_NO_COPY = 0,
	NOAD_COPY_DATA = 1,
	NOAD_COPY_DATA_AND_RENEW_TKID,
	NOAD_COPY_MAX,
};
static_assert(NOAD_COPY_MAX <= (1U << NOA_DESC_COPY_BIT_FIELD));

enum {
	FWD_REASON_FEEDTHROUGH,
	FWD_REASON_FALLBACK,
	FWD_REASON_NETENGINE,
	FWD_REASON_VPN,
	FWD_REASON_MAX,
};

static_assert(FWD_REASON_MAX <= (1U << NOA_DESC_REASON_BIT_FIELD));

/*
 * The shared region that shared with Wi-Fi / Modem on NCP,
 * so Ring Service on NEP could access the data from ring and
 * pool which is wrote by Wi-Fi / Modem on NCP.
 */
enum NOA_RING_SERVICE_BUFFER_POOL_ID {
	NOA_RING_SERVICE_BUFFER_POOL_UNKNOWN = -1,
	NOA_RING_SERVICE_BUFFER_POOL_WLAN = 0,
	NOA_RING_SERVICE_BUFFER_POOL_MODEM,
	NOA_RING_SERVICE_BUFFER_POOL_NETENGINE,
	NOA_RING_SERVICE_BUFFER_POOL_END,
};
#define NOA_RING_SERVICE_BUFFER_POOL_NUMBER ((NOA_RING_SERVICE_BUFFER_POOL_END))

typedef struct noa_buffer_pool_desc {
	uint16_t tkid;
	uint16_t dp_high;
	uint32_t dp_low;
	uint64_t dv;
} __attribute__((packed, aligned(4))) noa_buffer_pool_desc;

#define NEP_BUFFER_POOL_CACHED_RING_SIZE (128U)
#define NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN (sizeof(struct noa_buffer_pool_desc))
#ifndef linux
static_assert(is_power_of_2(NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN));
#endif /* linux */

enum {
	SESSION_PORT_WIFI_FW,
	SESSION_PORT_WIFI_SW,
	SESSION_PORT_MODEM_FW,
	SESSION_PORT_MODEM_SW,
	SESSION_PORT_NETENGINE,
	SESSION_PORT_MAX,
};

enum {
	SESSION_VER_P26,
};

enum {
	SESSION_STATE_UNUSED,
	SESSION_STATE_USED,
	SESSION_STATE_REPLY,
	SESSION_STATE_AGEOUT,
};

enum {
	SESSION_SECACT_NOACT,
};

enum {
	SESSION_VLANACT_NOACT,
};

#define SESSION_ENTRY_MAX 16
#define SESSION_RELATED_ID_INVALID 0xFFFF

struct noa_session_common {
	/* DW1 */
	u32 ver : 2;
	u32 sport : 3;
	u32 dport : 3;
	u32 ignore_llc : 1;
	u32 rsv1 : 5;
	u32 state : 2;
	u32 ageout : 16;
	/* DW2 */
	u16 eth_type : 16;
	u8 src_mac1[2];
	/* DW3 */
	u8 src_mac2[4];
	/* DW4 */
	u16 vlan_act : 2;
	u16 sec_act : 2;
	u16 rsv2 : 12;
	u8 dst_mac1[2];
	/* DW5 */
	u8 dst_mac2[4];
	/* DW6 */
	u32 vlan_info;
	/* DW7 */
	u32 sec_info;
	/* DW8 */
	u32 src_ifidx : 2;
	u32 dst_ifidx : 2;
	u32 flow_id : 11;
	u32 clat : 1;
	u32 related_id : 16;
};

struct noa_session_ipv4 {
	/* DW 0*/
	u8 proto;
	u8 priority : 3;
	u8 rsv1 : 5;
	u16 rsv2;
	/* DW1 */
	u32 src_ip;
	/* DW2 */
	u32 dst_ip;
	/* DW3 */
	u16 src_port;
	/* DW4 */
	u16 dst_port;
};

struct noa_session_ipv6 {
	u8 priority; // 4 bits, retrieved from ipv6hdr->priority
};

struct noa_session {
	struct noa_session_common common;

	union {
		struct noa_session_ipv4 ipv4;
		struct noa_session_ipv6 ipv6;
	} noa_session_ip;
};

struct noa_session_info {
	u16 ethertype;
	u8 ifidx;
	u8 flowid;
	u8 ignore_llc;
};

enum {
	NOA_DESC_BASIC = 0,
	NOA_DESC_MODEM_LASSEN,
	NOA_DESC_WLAN_TX_BRCM,
	NOA_DESC_WLAN_TX_QCA,
	NOA_DESC_NETENG_PKT_FLOW,
	/* An output descriptor type is set to NOA_DESC_VPN_RX only if the packet has been
	 * successfully decrypted and the input descriptor type is NOA_DESC_BASIC. If the input
	 * descriptor is not NOA_DESC_BASIC, the output descriptor type should not be set to
	 * NOA_DESC_VPN_RX.
	 */
	NOA_DESC_VPN_RX,
	NOA_DESC_MODEM_TX_MTK,
	NOA_DESC_MODEM_RX_MTK,
	NOA_DESC_MODEM_RX_MTK_MSG_PD,
	NOA_DESC_MODEM_RX_MTK_PD,
	NOA_DESC_TYPE_MAX,
};

static_assert(NOA_DESC_TYPE_MAX <= (1U << NOA_DESC_DESC_TYPE_BIT_FIELD));

#define NOA_DESC_BASIC_BYTE ((sizeof(struct noa_desc)))
#define NOA_DESC_MODEM_LASSEN_BYTE ((sizeof(struct noa_desc) + 4U + NOA_RX_IPSEC_METADATA_BYTE))
#define NOA_DESC_WLAN_TX_BRCM_BYTE ((sizeof(struct noa_desc) + 12U + NOA_TX_IPSEC_METADATA_BYTE))
#define NOA_DESC_WLAN_TX_QCA_BYTE ((sizeof(struct noa_desc) + 8U))
#define NOA_DESC_WLAN_TX_QCA_WCN_7760_BYTE ((sizeof(struct noa_desc) + 12U))
#define NOA_DESC_WLAN_RX_BRCM_BYTE NOA_DESC_WLAN_TX_BRCM_BYTE
#define NOA_DESC_WLAN_RX_BYTE (NOA_DESC_BASIC_BYTE + 4U)
#define NOA_DESC_NETENG_PKT_FLOW_BYTE ((sizeof(struct noa_desc) + sizeof(struct network_ext_txd)))
#define NOA_DESC_VPN_RX_BYTE ((sizeof(struct noa_desc) + NOA_RX_IPSEC_METADATA_BYTE))
#define NOA_DESC_MODEM_TX_MTK_BYTE ((sizeof(struct noa_desc) + 16U + NOA_TX_IPSEC_METADATA_BYTE))
#define NOA_DESC_MODEM_RX_MTK_BYTE ((sizeof(struct noa_desc) + 8U))
#define NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE ((sizeof(struct noa_desc) + 32U))
#define NOA_DESC_MODEM_RX_MTK_PD_BYTE ((sizeof(struct noa_desc) + 16U))
/* Ensure that the byte size of all desc should smaller than max byte define */
#define NOA_DESC_MAX_BYTE NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE
#define NOA_DESC_NETWORK_STACK_BYTE ((sizeof(network_stack_desc)))

#ifdef linux
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
#define NOA_MODEM_USE_ALCEDO_SOLUTION
#elif IS_ENABLED(CONFIG_NOA_MD_SAMSUNG_SUPPORT)
#define NOA_MODEM_USE_LASSEN_SOLUTION
#endif
#endif /* linux */

#if defined(NOA_MODEM_USE_ALCEDO_SOLUTION)
#define NOA_DESC_MODEM_TX (NOA_DESC_MODEM_TX_MTK)
#define NOA_DESC_MODEM_TX_BYTE (NOA_DESC_MODEM_TX_MTK_BYTE)
#elif defined(NOA_MODEM_USE_LASSEN_SOLUTION)
#define NOA_DESC_MODEM_TX (NOA_DESC_MODEM_LASSEN)
#define NOA_DESC_MODEM_TX_BYTE (NOA_DESC_MODEM_LASSEN_BYTE)
#else
#error "A proper modem solution has not yet been defined"
#endif

static inline void hexdump(const char *pfx, unsigned char *msg, int msglen)
{
	int i, col;
	char buf[80];

	col = 0;

	pr_info("dump_addr: %#lx\n", (unsigned long)msg);
	for (i = 0; i < msglen; i++, col++) {
		if (col % 16 == 0)
			strcpy(buf, pfx);
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%02x", msg[i]);
		if ((col + 1) % 16 == 0)
			pr_info("%s\n", buf);
		else
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " ");
	}

	if (col % 16 != 0)
		pr_info("%s\n", buf);
}

static inline int noa_desc_type_parse(const struct noa_desc *desc)
{
	return (desc->desc_type) & 0x7;
}

static inline bool noa_desc_is_noop(const struct noa_desc *desc)
{
	return desc->mode == NOAD_MODE_NOOP;
}

static inline void noa_desc_mark_noop(struct noa_desc *desc)
{
	desc->mode = NOAD_MODE_NOOP;
}

const char *noa_port_id_to_name(uint8_t port_id);
const char *noa_mode_to_str(uint32_t mode);
const char *noa_reason_to_str(uint16_t reason);
size_t noa_desc_bytes(int type);

static inline void NepFillPktInfo(const struct noa_desc *desc, nep_pkt_info *info)
{
	ParseNoaDescToNepPktInfo(desc, info);
	if (desc->mode != NOAD_MODE_DATA || desc->reason == FWD_REASON_FEEDTHROUGH) {
		info->action = PKT_ACTION_FEEDTHROUGH;
	} else if (desc->dl == 0 || desc->dv == 0) {
		pr_err("Invalid desc length %u or address 0x%lx with src %u tkid %u type %u\n",
		       desc->dl, (unsigned long)desc->dv, desc->src, desc->tkid, desc->desc_type);
		info->action = PKT_ACTION_FEEDTHROUGH;
		WARN_ON(1);
	} else {
		info->action = PKT_ACTION_UNDECIDED;
	}
}

/**
 * @brief Fills a host entry with packet information parsed from a descriptor.
 *
 * This function takes a generic Noa descriptor and extracts relevant packet
 * information (like data length, data address, mode, and reason) to populate a
 * `nep_host_entry` structure. It also determines an initial `PKT_ACTION` for the
 * packet, such as `PKT_ACTION_FEEDTHROUGH` or `PKT_ACTION_UNDECIDED`, based on the
 * descriptor's content.
 *
 * @param[in] desc Void pointer to a `noa_desc` structure containing the raw
 *  descriptor data from which packet information will be parsed.
 * @param[out] entry Void pointer to a `nep_host_entry` structure that will be
 *  filled with the parsed packet information.
 */
static inline void NepFillHostEntry(void *desc, void *entry)
{
	NepFillPktInfo((struct noa_desc *)desc, &((nep_host_entry *)entry)->pkt_info);
}

/**
 * @brief Fills a device entry with packet information parsed from a descriptor.
 *
 * This function takes a generic Noa descriptor and extracts relevant packet
 * information to populate a `nep_device_entry` structure. Similar to
 * `NepFillHostEntry`, it determines an initial `PKT_ACTION` for the packet.
 * @param[in] desc Void pointer to a `noa_desc` structure.
 * @param[out] entry Void pointer to a `nep_device_entry` structure.
 */
static inline void NepFillDeviceEntry(void *desc, void *entry)
{
	NepFillPktInfo((struct noa_desc *)desc, &((nep_device_entry *)entry)->pkt_info);
}

/**
 * @brief Fills a device entry with packet information parsed from a descriptor
 * dedicated for WiFi packets.
 *
 * This function takes a generic Noa descriptor and extracts relevant packet
 * information to populate a `nep_device_entry` structure. Similar to
 * `NepFillHostEntry`, it determines an initial `PKT_ACTION` for the packet.
 * This function is the same as `NepFillDeviceEntry`, except that it fills extra
 * information for PPF needs, which is only relevant for WiFi packets.
 * @param[in] desc Void pointer to a `noa_desc` structure.
 * @param[out] entry Void pointer to a `nep_device_entry` structure.
 */
static inline void NepFillWifiDeviceEntry(void *desc, void *entry)
{
	struct network_ext_rxd *rxd = (struct network_ext_rxd *) &(((struct noa_desc *)desc)->ext_data[0]);
	NepFillPktInfo((struct noa_desc *)desc, &((nep_device_entry *)entry)->pkt_info);
	((nep_device_entry *)entry)->ppf_info.iif = rxd->iif;
}

#endif /* __NOA_CORE_H__ */
// NOLINTEND
