#include "sys_if/io/sys_if_io.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uintptr_t kTestAddr = 0x12345678;
constexpr uint64_t kTestSize = 0xF;
constexpr uint32_t kTestData = 0x87654321;

TEST(WlanSysIfIoTest, IoRemap)
{
	EXPECT_EQ(SysIfIoRemap(kTestAddr, kTestSize), reinterpret_cast<void *>(kTestAddr));
}

TEST(WlanSysIfIoTest, IoWritelReadl)
{
	SysIfIoWritel(kTestData, reinterpret_cast<void *>(kTestAddr));
	EXPECT_EQ(SysIfIoReadl(reinterpret_cast<void *>(kTestAddr)), kTestData);
}

} // anonymous namespace
} // namespace noa::service::wlan_service