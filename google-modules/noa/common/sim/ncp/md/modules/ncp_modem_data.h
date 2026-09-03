#ifndef __NCP_MODEM_DATA_H__
#define __NCP_MODEM_DATA_H__

#include "common/ring.h"
#include "sys_common.h"

// Sync from mtk_pkt_format.h
// @brief Mtk message drb(Descriptor Ring Buffer).
struct MtkMessageDescriptorRingBuffer {
	uint16_t descriptor_type : 2;
	uint16_t continue_bit : 1;
	uint16_t noa_tcp_in_slow_start : 1;
	uint16_t reserved_1 : 12; // 13U->12U
	uint16_t packet_length;
	uint16_t count_l_psn;
	uint8_t channel_id : 8;
	uint8_t network_type : 3;
	uint8_t reserved_2 : 1;
	uint8_t ipv4 : 1;
	uint8_t l4_checksum : 1;
	uint8_t reserved_3 : 2;
	uint32_t reserved_4;
	uint32_t reserved_5;
};

// @brief Mtk payload drb(Descriptor Ring Buffer).
struct MtkPayloadDescriptorRingBuffer {
	uint16_t descriptor_type : 2;
	uint16_t continue_bit : 1;
	uint16_t reserved_1 : 13;
	uint16_t data_length;
	uint32_t address_low;
	uint32_t address_high;
	uint32_t reserved_2;
};

// @brief Mtk message pit(Packet Information Table) used in ncp.
struct MtkMessagePacketInfoTable {
	uint8_t packet_type : 1;
	uint8_t continue_bit : 1;
	uint8_t chechsum_result : 2;
	uint8_t error_bit : 1;
	uint8_t source_queue_id : 3;
	uint8_t hpc_index : 4;
	uint8_t reserved_1 : 4;
	uint8_t channel_id;
	uint8_t network_type : 3;
	uint8_t destination_queue_id : 3;
	uint8_t reserved_2 : 1;
	uint8_t drop_bit : 1;
	uint32_t count_l_psn : 18;
	uint8_t flow : 5;
	uint8_t reserved_3 : 1;
	uint8_t reserved_4 : 3;
	uint8_t hp_id : 5;
	uint16_t reserved_5;
	uint8_t protocol : 2;
	uint8_t reserved_6 : 6;
	uint8_t hash;
	uint8_t pit_sequence;
	uint16_t reserved_7 : 12;
	uint8_t mr : 2;
	uint8_t reserved_8 : 1;
	uint8_t ip : 1;
	uint8_t uplink_queue_done : 6;
	uint8_t downlink_queue_done : 2;
};

// @brief Mtk normal payload pit(Packet Information Table) used in ncp.
struct MtkPayloadPacketInfoTable {
	uint8_t packet_type : 1;
	uint8_t continue_bit : 1;
	uint8_t buffer_type : 1;
	uint16_t buffer_id : 13;
	uint16_t data_length;
	uint32_t address_low;
	uint32_t address_high;
	uint8_t pit_sequence;
	uint8_t h_buffer_id : 3;
	uint8_t reserved_1;
	uint8_t header_offset : 5;
	uint8_t uplink_queue_done : 6;
	uint8_t downlink_queue_done : 2;
};

// @brief Mtk bat(Buffer Address Table) used in ncp.
struct MtkBufferAddressTable {
	uint32_t address_low;
	uint32_t address_high;
};

struct noa_pd_pit {
	uint32_t pd_header;
	uint32_t addr_low;
	uint32_t addr_high;
	uint32_t pd_footer;
};

struct noa_msg_pit {
	uint32_t dword1;
	uint32_t dword2;
	uint32_t dword3;
	uint32_t dword4;
};

#endif /* __NCP_MODEM_DATA_H__ */
