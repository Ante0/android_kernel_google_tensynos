#include "wlan_debug_packet_sniffer_controller.h"

#include "sys_if/memory/sys_if_memory.h"
#include "wlan_debug_packet_sniffer.h"

void WlanPacketSnifferConfig(const WlanPacketSnifferControlParam *msg)
{
	if (msg != NULL) {
		WlanPacketSnifferSetRunoutAction(msg->behavior);
		WlanPackerSnifferSetLocation(msg->location);
		WlanPacketSnifferSetCaptureSize(msg->capture_size);
		WlanPacketSnifferSwitchDescDump(msg->enable_desc);
		WlanPacketSnifferSwitch(msg->enable);
	}
}

void WlanPacketSnifferFlush(void)
{
	SysIfFlushDCache(WlanPacketSnifferGetDramAddr(), PACKET_SNIFFER_PARTITION_SIZE);
}
