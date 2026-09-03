#ifndef PW_LOG_LEVEL
#define PW_LOG_LEVEL PW_LOG_LEVEL_DEBUG
#endif
#include "sys_if_log.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "sys_if_log_test_lib.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr char kTestLog[] = "Test Log";
constexpr uint32_t kTestBufferSize = 4096;

class WlanSysIfLogTest : public ::testing::Test {
    protected:
	WlanSysIfLogTest()
	{
		std::memset(test_buffer_, 0, sizeof(test_buffer_));
		SysIfLogTestInit(kTestBufferSize, test_buffer_);
	}

	~WlanSysIfLogTest()
	{
		SysIfLogTestDeinit();
	}

	char test_buffer_[kTestBufferSize];
};

TEST_F(WlanSysIfLogTest, SysIfLogDebug)
{
	char *result = std::strstr(test_buffer_, kTestLog);

	EXPECT_EQ(result, nullptr);

	SYS_IF_LOG_DEBUG("%s", kTestLog);
	result = std::strstr(test_buffer_, kTestLog);
	EXPECT_NE(result, nullptr);
}

TEST_F(WlanSysIfLogTest, SysIfLogInfo)
{
	char *result = std::strstr(test_buffer_, kTestLog);

	EXPECT_EQ(result, nullptr);

	SYS_IF_LOG_INFO("%s", kTestLog);
	result = std::strstr(test_buffer_, kTestLog);
	EXPECT_NE(result, nullptr);
}

TEST_F(WlanSysIfLogTest, SysIfLogWarn)
{
	char *result = std::strstr(test_buffer_, kTestLog);

	EXPECT_EQ(result, nullptr);

	SYS_IF_LOG_WARN("%s", kTestLog);
	result = std::strstr(test_buffer_, kTestLog);
	EXPECT_NE(result, nullptr);
}

TEST_F(WlanSysIfLogTest, SysIfLogError)
{
	char *result = std::strstr(test_buffer_, kTestLog);

	EXPECT_EQ(result, nullptr);

	SYS_IF_LOG_ERROR("%s", kTestLog);
	result = std::strstr(test_buffer_, kTestLog);
	EXPECT_NE(result, nullptr);
}

} // namespace
} // namespace noa::service::wlan_service
