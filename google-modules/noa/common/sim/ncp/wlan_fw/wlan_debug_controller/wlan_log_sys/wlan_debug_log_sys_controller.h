#ifndef SIM_NCP_WLAN_FW_WLAN_DEBUG_CONTROLLER_WLAN_DEBUG_LOG_HELPER_H
#define SIM_NCP_WLAN_FW_WLAN_DEBUG_CONTROLLER_WLAN_DEBUG_LOG_HELPER_H

#include "wlan_debug_controller/wlan_debug_common.h"
#include "wlan_log/wlan_log.h"
#include "sys_if/types/types.h"

/// @brief Parameters used to control debugging log system
typedef struct WlanLogSystemControlCmd {
	// Enable or Disable the log matching the following conditions.
	uint8_t enable; // 0: default disabled all modules, 1: enable all modules
	uint8_t multi_category_ctrl; // ignore the enable setting, set all modules based on the modules_level
	enum Location location;
	enum WlanLogLevel level;
	union {
		uint32_t category;
		enum WlanLogLevel categories_level[kWlanLogModuleNum];
	};
} __attribute__((packed, aligned(4))) WlanLogSystemControlCmd;

/// @brief Configures the log system based on received message.
///
/// @param[in] msg message received from APC/NCP RPC
/// @return sueccessfully configure the log system
extern void WlanSvcDebugLogConfig(const struct WlanLogSystemControlCmd *msg);
#endif // SIM_NCP_WLAN_FW_WLAN_DEBUG_CONTROLLER_WLAN_DEBUG_LOG_HELPER_H
