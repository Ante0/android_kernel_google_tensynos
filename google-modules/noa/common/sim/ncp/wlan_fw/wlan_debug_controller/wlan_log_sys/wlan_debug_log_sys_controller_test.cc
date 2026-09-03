#include "wlan_debug_log_sys_controller.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "wlan_log/wlan_log.h"

namespace noa::service::wlan_service
{
namespace
{

static inline uint32_t ConvertCategoryToBitmap(WlanLogModule category)
{
	uint32_t bitmap = static_cast<uint32_t>(category);
	return 1 << bitmap;
}

TEST(WlanDebugLogSysController, DisableModuleLogLevel)
{
	// INIT
	WlanLogInit();
	struct WlanLogSystemControlCmd req = {
		.enable = 0,
		.location = kUndefinedLocation,
		.level = kWlanLogLevelDebug,
		.category = ConvertCategoryToBitmap(kWlanLogModuleDp),
	};

	// TEST
	WlanSvcDebugLogConfig(&req);

	// EXPECTATION
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleDp, kWlanLogLevelDebug));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleDp, kWlanLogLevelInfo));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleDp, kWlanLogLevelWarn));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleDp, kWlanLogLevelError));
}

// Same as change log level, if the log level is set,
// then the specific module log is enabled.
TEST(WlanDebugLogSysController, EnableModuleLogLevel)
{
	WlanLogInit();
	WlanLogDisableAllModules();
	struct WlanLogSystemControlCmd req = {
		.enable = 1,
		.location = kUndefinedLocation,
		.level = kWlanLogLevelWarn,
		.category = ConvertCategoryToBitmap(kWlanLogModuleNep),
	};

	WlanSvcDebugLogConfig(&req);

	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleNep, kWlanLogLevelInfo));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleNep, kWlanLogLevelDebug));
	EXPECT_TRUE(WlanLogModuleIsEnabled(kWlanLogModuleNep, kWlanLogLevelWarn));
	EXPECT_TRUE(WlanLogModuleIsEnabled(kWlanLogModuleNep, kWlanLogLevelError));
}

TEST(WlanDebugLogSysController, ChangeLogLocation)
{
	WlanLogInit(); // default log location is kUart
	struct WlanLogSystemControlCmd req = {
		.enable = 1,
		.location = kDram, // change log location to kDram
		.level = kWlanLogLevelWarn,
		.category = ConvertCategoryToBitmap(kWlanLogModuleNep),
	};

	WlanSvcDebugLogConfig(&req);

	EXPECT_TRUE(WlanLogModuleGetLocation(kWlanLogModuleNep) == kDram);
}
} // namespace
} // namespace noa::service::wlan_service
