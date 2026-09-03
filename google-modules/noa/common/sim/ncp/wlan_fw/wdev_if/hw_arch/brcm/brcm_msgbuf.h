#ifndef MODULES_WDEV_IF_HW_ARCH_BRCM_BRCM_MSGBUF_DESCRIPTOR_H
#define MODULES_WDEV_IF_HW_ARCH_BRCM_BRCM_MSGBUF_DESCRIPTOR_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"

// Referred from BCMDHD: bcmmsgbuf.h

/// @brief The number of bytes in an ethernet (MAC) address.
#define BCM_ETHER_ADDR_LEN 6
/// @brief The number of bytes in the type field.
#define BCM_ETHER_TYPE_LEN 2
/// @brief The number of bytes in the trailing CRC field.
#define BCM_ETHER_CRC_LEN 4
/// @brief The length of the combined header.
#define BCM_ETHER_HDR_LEN (BCM_ETHER_ADDR_LEN * 2 + BCM_ETHER_TYPE_LEN)
/// @brief Flag indicating an 802.3 Ethernet frame.
#define BCMPCIE_PKT_FLAGS_FRAME_802_3 0x1
/// @brief Flag indicating an 802.11 Wi-Fi frame.
#define BCMPCIE_PKT_FLAGS_FRAME_802_11 0x2
/// @brief Modulo value for D2H epoch counters.
#define D2H_EPOCH_MODULO 253UL
/// @brief Modulo value for H2D epoch counters.
#define H2D_EPOCH_MODULO 253UL
/// @brief Number of steps for PCIe D2H synchronization.
#define PCIE_D2H_SYNC_NUM_OF_STEPS 5UL
/// @brief Number of wait tries for PCIe D2H synchronization.
#define PCIE_D2H_SYNC_WAIT_TRIES 512UL
/// @brief Initial nibble value for the BCMPCIE flow ring phase.
#define BCMPCIE_FLOWRING_PHASE_NIBBLE_INIT 0xA0

/// @brief Enum defining bit masks for different checksum types.
enum BrcmPktCsumTypeMask {
	kPktCsumTypeIpV4Mask = (1 << 0),
	kPktCsumTypeIpV6Mask = (1 << 1),
	kPktCsumTypeTcpMask = (1 << 2),
	kPktCsumTypeUdpMask = (1 << 3),
	kPktCsumTypeNwkCsumMask = (1 << 4),
	kPktCsumTypeTransCsumMask = (1 << 5),
	kPktCsumTypePseudohdrCsumMask = (1 << 6),
};

/// @brief Message type
enum BrcmPcieMsgType {
	kBrcmPcieMsgTypeTxPost = 0xF,
	kBrcmPcieMsgTypeTxStatus = 0x10,
	kBrcmPcieMsgTypeRxPost = 0x11,
	kBrcmPcieMsgTypeRxCmplt = 0x12,
};

/// @brief Common message header.
typedef struct CmnMsgHdr {
	/// @brief Message type.
	uint8_t msg_type;
	/// @brief Interface index this is valid for.
	uint8_t if_id;
	/// @brief flags.
	uint8_t flags;
	/// @brief sequence number.
	uint8_t epoch;
	/// @brief packet Identifier for the associated host buffer.
	uint32_t request_id;
} CmnMsgHdr;

/// @brief H2D Rxpost ring work items.
typedef struct HostRxbufPost {
	/// @brief Common message header.
	CmnMsgHdr cmn_hdr;
	/// @brief Provided meta data buffer length.
	uint16_t metadata_buf_len;
	/// @brief Provided data buffer len to receive data.
	uint16_t data_buf_len;
	/// @brief Alignment to make the host buffers start on 8 byte boundary.
	uint32_t rsvd;
	/// @brief Provided meta data buffer.
	uint64_t metadata_buf_addr;
	/// @brief Provided data buffer to receive data.
	uint64_t data_buf_addr;
} HostRxbufPost;

/// @brief Packet information for checksum offload.
typedef struct PktInfoCso {
	/// @brief Version.
	uint8_t ver;
	/// @brief Packet csum type = ipv4/v6|udp|tcp|nwk_csum|trans_csum|ph_csum
	uint8_t pkt_csum_type;
	/// @brief IP header length.
	uint8_t nwk_hdr_len;
	/// @brief TCP header length.
	uint8_t trans_hdr_len;
} PktInfoCso;

typedef struct HostTxbufPostV1 {
	/// @brief Common message header.
	CmnMsgHdr cmn_hdr;
	/// @brief Eth header.
	uint8_t txhdr[BCM_ETHER_HDR_LEN];
	/// @brief Flags.
	uint8_t flags;
	/// @brief Number of segments.
	uint8_t seg_cnt;
	/// @brief Provided meta data buffer for txstatus.
	uint64_t metadata_buf_addr;
	/// @brief Provided data buffer containing Tx payload.
	uint64_t data_buf_addr;
	/// Provided meta data buffer length.
	uint16_t metadata_buf_len;
	/// Provided data buffer length.
	uint16_t data_len;
	union {
		struct {
			/// @brief Extended transmit flags.
			uint8_t ext_flags;
			uint8_t scale_factor;
			/// @brief User defined rate.
			uint8_t rate;
			uint8_t exp_time;
		};
		/// @brief XOR checksum or a magic number to audit DMA done.
		uint32_t marker;
	};
} HostTxbufPostV1;

typedef struct HostTxbufPostV2 {
	HostTxbufPostV1 v1;
	struct {
		PktInfoCso pkt_info_cso;
		uint32_t pad;
	} v2_ext;
} HostTxbufPostV2;

/// @brief Control Completion messages (20 bytes)
typedef struct ComplMsgHdr {
	union {
		/// @brief Status for the completion.
		int16_t status;

		/// @brief Mutually exclusive with pkt fate debug feature.
		struct {
			/// @brief Delta TimeStamp 3: T4-tref
			uint16_t d_t4;
		} tx_pktts;
	};
	/// @brief Submission flow ring id which generated this status.
	union {
		uint16_t ring_id;
		uint16_t flow_ring_id;
	};
} ComplMsgHdr;

typedef struct TsTimestampSrcid {
	union {
		/// @brief Time stamp low 32 bits.
		uint32_t ts_low;
		/// @brief Use ratespec.
		uint32_t rate_spec;
	};
	union {
		/// @brief Time stamp high 28 bits.
		uint32_t ts_high;
		union {
			/// @brief Time stamp high 28 bits.
			uint32_t ts_high_ext : 28;
			/// @brief Clock ID source.
			uint32_t clk_id_ext : 3;
			/// @brief Phase bit.
			uint32_t phase : 1;
			uint32_t marker_ext;
		};
		uint32_t tx_pkt_band_retry_info;
	};
} TsTimestampSrcid;

typedef struct PktTs {
	/// @brief Ref Clk in uSec (currently, tsf)
	uint32_t tref;
	/// @brief Delta TimeStamp 1: T2-tref
	uint16_t d_t2;
	/// @brief Delta TimeStamp 2: T3-tref
	uint16_t d_t3;
} PktTs;

/// @brief D2H Rxcompletion ring work items for IPC rev7.
typedef struct HostRxbufCmpl {
	/// @brief Common message header.
	CmnMsgHdr cmn_hdr;
	/// @brief Completion message header.
	ComplMsgHdr compl_hdr;
	/// @brief Filled up meta data len.
	uint16_t metadata_len;
	/// @brief Filled up buffer len to receive data.
	uint16_t data_len;
	/// @brief Offset in the host rx buffer where the data starts.
	uint16_t data_offset;
	/// @brief Offset in the host rx buffer where the data starts.
	uint16_t flags;
	/// @brief Rx status.
	uint32_t rx_status_0;
	uint32_t rx_status_1;
	/// @brief Size per IPC = (3 x uint32) bytes
	union {
		struct {
			/// @brief Used by Monitor mode.
			uint32_t marker;
			/// @brief Timestamp.
			TsTimestampSrcid ts;
		};

		/// @brief LatTS_With_XORCSUM.
		struct {
			/// @brief Latency timestamp.
			PktTs rx_pktts;
			/// @brief XOR checksum or a magic number to audit DMA done.
			uint32_t marker_ext;
		};
	};
} HostRxbufCmpl;

/// @brief D2H Txcompletion ring work items - extended for IOC rev7
typedef struct HostTxbufCmpl {
	/// @brief Common message header.
	CmnMsgHdr cmn_hdr;
	/// @brief Completion message header.
	ComplMsgHdr compl_hdr;
	/// @brief Size per IPC = (3 x uint32) bytes
	union {
		// Usage 1: TxS_With_TimeSync
		struct {
			struct {
				/// @brief Ext_TxStatus
				union {
					/// @brief Provided meta data length.
					uint16_t metadata_len;
					/// @brief Provided extended TX status.
					uint16_t tx_status_ext;
				};
				/// @brief WLAN side txstatus.
				uint16_t tx_status;
			};
			/// @brief Timestamp.
			TsTimestampSrcid ts;
		};
		// Usage 2: LatTS_With_XORCSUM
		struct {
			/// @brief Latency timestamp.
			PktTs tx_pktts;
			/// @brief XOR checksum or a magic number to audit DMA done.
			uint32_t marker_ext;
		};
	};
} HostTxbufCmpl;

#endif /* MODULES_WDEV_IF_HW_ARCH_BRCM_BRCM_MSGBUF_DESCRIPTOR_H */
