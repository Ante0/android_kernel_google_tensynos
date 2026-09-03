#include "wlan_ring.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "sys_if/io/sys_if_io.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uintptr_t kRingDescBase = 0x2000;
constexpr uintptr_t kRingBase = 0x3000;
constexpr uintptr_t kRingLen = 0x3004;
constexpr uintptr_t kRingMaxItem = 0x3008;
constexpr uintptr_t kRingRead = 0x300C;
constexpr uintptr_t kRingWrite = 0x3010;
constexpr uint16_t kStride = 2;
constexpr uint16_t kNDesc = 16;
constexpr uint16_t kDescSize = 32;

class WlanRingTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		// Initialize ring parameters
		WlanRingInitParams params;

		std::memset(&params, 0, sizeof(params));

		params.ndesc = kNDesc;
		params.desc_sz = kDescSize;
		params.stride = kStride;
		params.desc = reinterpret_cast<void *>(kRingDescBase);
		params.regs.base = kRingBase;
		params.regs.len = kRingLen;
		params.regs.max_item = kRingMaxItem;
		params.regs.read = kRingRead;
		params.regs.write = kRingWrite;

		// Initialize the ring
		ASSERT_EQ(WlanRingInit(&ring_, &params), 0);
	}

	void TearDown() override
	{
		WlanRingDeinit(&ring_);
	}

	WlanRing ring_;
};

TEST_F(WlanRingTest, WlanRingGetWriteCount)
{
	// Initial state
	EXPECT_EQ(WlanRingGetWriteCount(&ring_), kNDesc - 1);

	// Test cases with different read and write values
	SysIfIoWritel(4 * kStride, reinterpret_cast<void *>(kRingRead));
	SysIfIoWritel(8 * kStride, reinterpret_cast<void *>(kRingWrite));
	EXPECT_EQ(WlanRingGetWriteCount(&ring_), 11U);

	SysIfIoWritel(12 * kStride, reinterpret_cast<void *>(kRingRead));
	SysIfIoWritel(2 * kStride, reinterpret_cast<void *>(kRingWrite));
	EXPECT_EQ(WlanRingGetWriteCount(&ring_), 9U);

	SysIfIoWritel(0 * kStride, reinterpret_cast<void *>(kRingRead));
	SysIfIoWritel(0 * kStride, reinterpret_cast<void *>(kRingWrite));
	EXPECT_EQ(WlanRingGetWriteCount(&ring_), kNDesc - 1);
}

TEST_F(WlanRingTest, WlanRingGetReadCount)
{
	// Initial state
	EXPECT_EQ(WlanRingGetReadCount(&ring_), 0U);

	// Test cases with different read and write values
	SysIfIoWritel(4 * kStride, reinterpret_cast<void *>(kRingRead));
	SysIfIoWritel(8 * kStride, reinterpret_cast<void *>(kRingWrite));
	EXPECT_EQ(WlanRingGetReadCount(&ring_), 4U);

	SysIfIoWritel(12 * kStride, reinterpret_cast<void *>(kRingRead));
	SysIfIoWritel(2 * kStride, reinterpret_cast<void *>(kRingWrite));
	EXPECT_EQ(WlanRingGetReadCount(&ring_), 6U);

	// Test edge cases
	SysIfIoWritel(0 * kStride, reinterpret_cast<void *>(kRingRead));
	SysIfIoWritel(0 * kStride, reinterpret_cast<void *>(kRingWrite));
	EXPECT_EQ(WlanRingGetReadCount(&ring_), 0U);
}

TEST_F(WlanRingTest, WlanRingUpdateSwWrite)
{
	ring_.write = 5;
	WlanRingUpdateSwWrite(&ring_);
	EXPECT_EQ(ring_.write, 6);
	ring_.write = kNDesc - 1;
	WlanRingUpdateSwWrite(&ring_);
	// Wraps around
	EXPECT_EQ(ring_.write, 0);
}

TEST_F(WlanRingTest, WlanRingUpdateSwRead)
{
	ring_.read = 5;
	WlanRingUpdateSwRead(&ring_);
	EXPECT_EQ(ring_.read, 6);
	ring_.read = kNDesc - 1;
	WlanRingUpdateSwRead(&ring_);
	// Wraps around
	EXPECT_EQ(ring_.read, 0);
}

TEST_F(WlanRingTest, WlanRingGetWriteBase)
{
	// Initial state: read=0, write=0
	EXPECT_EQ(WlanRingGetWriteBase(&ring_), reinterpret_cast<uint8_t *>(kRingDescBase));

	ring_.write = kNDesc - 1;
	EXPECT_EQ(WlanRingGetWriteBase(&ring_),
		  reinterpret_cast<uint8_t *>(kRingDescBase + (kNDesc - 1) * kDescSize));
}

TEST_F(WlanRingTest, WlanRingGetReadBase)
{
	// Initial state: read=0, write=0
	EXPECT_EQ(WlanRingGetReadBase(&ring_), reinterpret_cast<uint8_t *>(kRingDescBase));

	ring_.read = kNDesc - 1;
	EXPECT_EQ(WlanRingGetReadBase(&ring_),
		  reinterpret_cast<uint8_t *>(kRingDescBase + (kNDesc - 1) * kDescSize));
}

TEST_F(WlanRingTest, WlanRingUpdateHwWrite)
{
	// Initial state: read=0, write=0
	WlanRingUpdateHwWrite(&ring_);
	EXPECT_EQ(SysIfIoReadl(reinterpret_cast<void *>(ring_.regs.write)), 0U);

	ring_.write = kNDesc - 1;
	WlanRingUpdateHwWrite(&ring_);
	EXPECT_EQ(SysIfIoReadl(reinterpret_cast<void *>(ring_.regs.write)),
		  static_cast<uint32_t>((kNDesc - 1) * kStride));
}

TEST_F(WlanRingTest, WlanRingUpdateHwRead)
{
	// Initial state: read=0, write=0
	WlanRingUpdateHwRead(&ring_);
	EXPECT_EQ(SysIfIoReadl(reinterpret_cast<void *>(ring_.regs.read)), 0U);

	ring_.read = kNDesc - 1;
	WlanRingUpdateHwRead(&ring_);
	EXPECT_EQ(SysIfIoReadl(reinterpret_cast<void *>(ring_.regs.read)),
		  static_cast<uint32_t>((kNDesc - 1) * kStride));
}

TEST_F(WlanRingTest, CalculateWriteCount)
{
	// Test cases where w < r
	EXPECT_EQ(CalculateWriteCount(5, 2, 10), 2U);
	EXPECT_EQ(CalculateWriteCount(10, 0, 10), 9U);

	// Test cases where w >= r
	EXPECT_EQ(CalculateWriteCount(2, 5, 10), 6U);
	EXPECT_EQ(CalculateWriteCount(0, 9, 10), 0U);

	// Test edge cases
	EXPECT_EQ(CalculateWriteCount(0, 0, 10), 9U);
	EXPECT_EQ(CalculateWriteCount(10, 10, 10), 9U);
}

TEST_F(WlanRingTest, CalculateReadCount)
{
	// Test cases where w < r
	EXPECT_EQ(CalculateReadCount(5, 2, 10), 7U);
	EXPECT_EQ(CalculateReadCount(10, 0, 10), 0U);

	// Test cases where w >= r
	EXPECT_EQ(CalculateReadCount(2, 5, 10), 3U);
	EXPECT_EQ(CalculateReadCount(0, 10, 10), 10U);

	// Test edge cases
	EXPECT_EQ(CalculateReadCount(0, 0, 9), 0U);
	EXPECT_EQ(CalculateReadCount(10, 10, 10), 0U);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
