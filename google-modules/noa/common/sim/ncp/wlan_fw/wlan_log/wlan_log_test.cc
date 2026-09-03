#include "wlan_log.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{

TEST(WlanLogTest, Init)
{
	WlanLogInit();
	for (uint32_t module = kWlanLogModuleStart; module < kWlanLogModuleEnd; module++) {
		for (uint32_t level = kWlanLogLevelDebug; level <= kWlanLogLevelError; level++) {
			if (DEFAULT_LOG_LEVEL >= level) {
				EXPECT_TRUE(
					WlanLogModuleIsEnabled(static_cast<WlanLogModule>(module),
							       static_cast<WlanLogLevel>(level)));
			}
		}
	}
}

TEST(WlanLogTest, EnableDisable)
{
	WlanLogInit();
	WlanLogModuleSetLogLevel(kWlanLogModuleStart, kWlanLogLevelWarn);
	EXPECT_TRUE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelError));
	EXPECT_TRUE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelWarn));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelInfo));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelDebug));

	WlanLogModuleClearLogLevel(kWlanLogModuleStart, kWlanLogLevelInfo);
	EXPECT_TRUE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelError));
	EXPECT_TRUE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelWarn));
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleStart, kWlanLogLevelInfo));
}

TEST(WlanLogTest, EnableDisableAll)
{
	WlanLogInit();
	WlanLogEnableAllModules();
	for (uint32_t module = kWlanLogModuleStart; module < kWlanLogModuleEnd; module++) {
		for (uint32_t level = kWlanLogLevelDebug; level <= kWlanLogLevelError; level++) {
			EXPECT_TRUE(WlanLogModuleIsEnabled(static_cast<WlanLogModule>(module),
							   static_cast<WlanLogLevel>(level)));
		}
	}

	WlanLogDisableAllModules();
	for (uint32_t module = kWlanLogModuleStart; module < kWlanLogModuleEnd; module++) {
		for (uint32_t level = kWlanLogLevelDebug; level <= kWlanLogLevelError; level++) {
			EXPECT_FALSE(WlanLogModuleIsEnabled(static_cast<WlanLogModule>(module),
							    static_cast<WlanLogLevel>(level)));
		}
	}
}

TEST(WlanLogTest, InvalidModule)
{
	WlanLogInit();
	EXPECT_FALSE(WlanLogModuleIsEnabled(kWlanLogModuleEnd, kWlanLogLevelDebug));
}

} // anonymous namespace
} // namespace noa::service::wlan_service
