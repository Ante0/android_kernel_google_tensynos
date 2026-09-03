#ifndef SIM_NCP_WLAN_FW_WLAN_DEBUG_CONTROLLER_WLAN_DEBUG_COMMON_H
#define SIM_NCP_WLAN_FW_WLAN_DEBUG_CONTROLLER_WLAN_DEBUG_COMMON_H

#include "sys_if/types/types.h"

enum Location {
	kUndefinedLocation = 0,
	kDram,
	kUart,
	kDramAndUart,
	kLocationNum,
};

enum PacketCaptureAction {
	kDisable = 0,
	kEnable,
};

enum PacketRunoutAction {
	kUndefinedPacketRunoutAction = 0,
	kOverwrite,
	kDiscard,
	kNotifyDriver,
	kPacketRunoutActionNum,
};

#endif // SIM_NCP_WLAN_FW_WLAN_DEBUG_CONTROLLER_WLAN_DEBUG_COMMON_H
