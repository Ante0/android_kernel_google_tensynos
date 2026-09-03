#include "wlan_debug_log_sys_controller.h"

#include "wlan_cast.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "wlan_log/wlan_log.h"

void WlanSvcDebugLogConfig(const struct WlanLogSystemControlCmd *msg)
{
	int i;
	WlanLogModule category;

	if (msg == NULL) {
		return;
	}

	if (msg->location) {
		WlanLogSetLocation(WLAN_STATIC_CAST(enum Location, msg->location));
		return;
	}

	if (msg->multi_category_ctrl) {
		WlanLogModuleSetLogLevelForMultiModules(msg->categories_level);
		return;
	}

	for (i = kWlanLogModuleStart; i < kWlanLogModuleNum; i++) {
		category = WLAN_STATIC_CAST(WlanLogModule, i);
		if (msg->category >> i & 0x1) {
			if (msg->enable) {
				WlanLogModuleSetLogLevel(category, msg->level);
			} else {
				WlanLogModuleClearLogLevel(category, kWlanLogLevelDisableAll);
			}
		}
	}
}
