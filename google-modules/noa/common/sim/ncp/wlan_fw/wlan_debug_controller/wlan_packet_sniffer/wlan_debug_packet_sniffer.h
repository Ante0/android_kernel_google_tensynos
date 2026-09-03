#ifndef WLAN_DEBUG_CONTROLLER_WLAN_PACKET_SNIFFER_WLAN_PACKET_SNIFFER_H
#define WLAN_DEBUG_CONTROLLER_WLAN_PACKET_SNIFFER_WLAN_PACKET_SNIFFER_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "ext_svc/ext_svc.h"

#define TIMESTAMP_MAX_SIZE 12
#define PACKET_ALIGNMENT_SIZE 8
#define PACKET_SNIFFER_PARTITION_SIZE 40000
#define DEFAULT_PACKET_CAPTURE_SIZE 128
#define MAX_PACKET_ENTRY_NUM 300
#define MAX_PACKET_SIZE 4096
#define PACKET_SNIFFER_BUFFER_OFFSET sizeof(SnifferEntryHeader) * MAX_PACKET_ENTRY_NUM

enum WlanPacketSnifferRingType {
	kNoaHwTx,
	kNoaHwRx,
	kNepTx,
	kNepRx,
	kWlanPacketSnifferRingTypeNum,
};

// Insert before every packet sniffer entry
typedef struct SnifferEntryHeader {
	uint8_t valid;
	uint8_t ring_type;
	uint16_t pkt_len;
	uint32_t pkt_seq;
	uint16_t pkt_offset;
	char timestamp[TIMESTAMP_MAX_SIZE];
} __attribute__((packed, aligned(4))) SnifferEntryHeader;

// For every one character in a packet, the hexdump will make it
// to "xx ", which will occupy 3 characters. Also, adding the last
// character for the null character forms the PACKET_SNIFFER_PARTITION_SIZE*3+1.
#if IS_ENABLED(CONFIG_NOA_WLAN_PACKET_SNIFFER_SUPPORT)
#define WLAN_PACKET_SNIFF(addr, dl, desc, dump_desc, ring_type)                                    \
	WlanPacketSniff(addr, dl, desc, dump_desc, k##ring_type);
#else
#define WLAN_PACKET_SNIFF(...)
#endif

/// @brief Initializes the packet sniffer before first using it
///
/// @param[in] dram_addr address of the wlan shared memory for packet sniffer.
/// @param[in] dram_size size of the wlan shared memory for packet sniffer.
extern void WlanPacketSnifferInit(ExternalServices *ext_svc, uint64_t dram_addr,
				  uint32_t dram_size);

/// @brief Set the action to take when the packet sniffer buffer is full.
/// @param[in] action The action to take.
extern void WlanPacketSnifferSetRunoutAction(enum PacketRunoutAction action);

/// @brief Set the location where the packet sniffer should output data.
/// @param[in] location The location to output data to.
extern void WlanPackerSnifferSetLocation(enum Location location);

/// @brief Set the capture size for the packet sniffer.
/// @param[in] capture_size The capture size in bytes.
extern void WlanPacketSnifferSetCaptureSize(uint16_t capture_size);

/// @brief Enable or disable the packet sniffer.
/// @param[in] enable True to enable the packet sniffer, false to disable it.
extern void WlanPacketSnifferSwitch(bool enable);

/// @brief Record a packet.
/// @param[in] pkt_addr The address of the packet to sniff.
/// @param[in] pkt_len The packet size
/// @param[in] desc The descriptor pointer refers to the packet
/// @param[in] dump_desc The function pointer to dump the specific type of descriptor
/// @param[in] ring_type type of sniffer ring
/// @return packet hex dump if the output location is kUart, otherwise it will be null.
extern void WlanPacketSniff(uint64_t pkt_addr, uint32_t pkt_len, void *desc,
			    void (*dump_desc)(void *desc),
			    enum WlanPacketSnifferRingType ring_type);

/// @brief Get the name of each ring type
/// @param[in] ring_type type of the packet sniffer ring
/// @return the name of each ring type
extern const char *WlanPacketSnifferGetRingType(enum WlanPacketSnifferRingType ring_type);

/// @brief Get sniffer output location
/// @return enum Location output location
extern enum Location WlanPacketSnifferGetLocation(void);

/// @brief Get the DRAM address of the packet sniffer buffer.
/// @return The DRAM address of the packet sniffer buffer.
extern uint64_t WlanPacketSnifferGetDramAddr(void);

/// @brief Set the DRAM address of the packet sniffer buffer.
/// @param[in] dram_addr The DRAM address to store packets captured.
extern void WlanPacketSnifferSetDramAddr(uint64_t dram_addr);

/// @brief Set the DRAM address of the packet sniffer buffer.
/// @param[in] dram_addr The DRAM address to store packets captured.
extern void WlanPacketSnifferSetDramAddr(uint64_t dram_addr);

/// @brief Get the DRAM size of the packet sniffer buffer.
/// @return The DRAM size of the packet sniffer buffer.
extern uint32_t WlanPacketSnifferGetDramSize(void);

/// @brief Set the DRAM size of the packet sniffer buffer.
/// @param[in] dram_size The DRAM size to store packets captured.
extern void WlanPacketSnifferSetDramSize(uint32_t dram_size);

/// @brief Enable or disable the descriptor dump in packet sniffer.
/// @param[in] enable True to enable the descriptot dump, false to disable it.
extern void WlanPacketSnifferSwitchDescDump(bool enable_desc);

/// @brief Get the capture size of the packet sniffer.
/// @return The capture size of the packet sniffer in bytes.
extern uint16_t WlanPacketSnifferGetCaptureSize(void);

/// @brief Convert the packet address to hex decimal with a space followed
/// @param[in] pkt_addr address of the packet needs to be converted
/// @param[in] pkt_len the length of the packet captured
/// @param[in] ring_type the ring type that the packet is captured from
extern void WlanPacketSnifferHexDump(uint64_t pkt_addr, uint32_t pkt_len,
				     enum WlanPacketSnifferRingType ring_type);

/// @brief Reset the packet sniffer control structure to initial state.
extern void WlanPacketSnifferReset(void);

#endif // WLAN_DEBUG_CONTROLLER_WLAN_PACKET_SNIFFER_WLAN_PACKET_SNIFFER_H
