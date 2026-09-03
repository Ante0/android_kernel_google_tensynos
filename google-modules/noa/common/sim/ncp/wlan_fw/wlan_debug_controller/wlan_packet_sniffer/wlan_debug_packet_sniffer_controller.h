#ifndef WLAN_DEBUG_CONTROLLER_WLAN_PACET_SNIFFER_WLAN_PACKET_SNIFFER_CONTROLLER_H
#define WLAN_DEBUG_CONTROLLER_WLAN_PACET_SNIFFER_WLAN_PACKET_SNIFFER_CONTROLLER_H

#include "wlan_debug_controller/wlan_debug_common.h"

typedef struct WlanPacketSnifferControlParam {
	bool enable;
	bool enable_desc;
	uint16_t capture_size;
	enum Location location;
	enum PacketRunoutAction behavior;
} __attribute__((packed, aligned(4))) WlanPacketSnifferControlParam;

extern void WlanPacketSnifferConfig(const WlanPacketSnifferControlParam *msg);

extern void WlanPacketSnifferFlush(void);

#endif // WLAN_DEBUG_CONTROLLER_WLAN_PACET_SNIFFER_WLAN_PACKET_SNIFFER_CONTROLLER_H
