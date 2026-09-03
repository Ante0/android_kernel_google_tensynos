#include "wlan_buffer_manager.h"

#include <cerrno>
#include <cstdint>

#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uint16_t kEntryNum = 128;
constexpr uint16_t kPktidOffset = 100;

// Test fixture for BufferManager tests.
class BmTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		memset(&bm_info_, 0, sizeof(struct BufferManagerInfo));

		for (uint32_t i = kBufferTableTypeStart; i < kBufferTableTypeEnd; i++) {
			CreateMockBufferManagementTable(&table_info_[i]);
		}

		bm_info_.noa_rx_buffer_table_info_addr =
			reinterpret_cast<uint64_t>(&table_info_[kBufferTableTypeNoaRx]);
		bm_info_.apc_rx_buffer_table_info_addr =
			reinterpret_cast<uint64_t>(&table_info_[kBufferTableTypeApcRx]);
		bm_info_.noa_tx_buffer_table_info_addr =
			reinterpret_cast<uint64_t>(&table_info_[kBufferTableTypeNoaTx]);
		bm_info_.apc_tx_buffer_table_info_addr =
			reinterpret_cast<uint64_t>(&table_info_[kBufferTableTypeApcTx]);
	}

	void TearDown() override
	{
		for (uint32_t i = kBufferTableTypeStart; i < kBufferTableTypeEnd; i++) {
			DestroyMockBufferManagementTable(&table_info_[i]);
		}
	}

	void CreateMockBufferManagementTable(struct BufferManagementTableInfo *const tbl_info)
	{
		tbl_info->entry_num = kEntryNum;
		tbl_info->pktid_offset = kPktidOffset;

		// Allocate memory for pending bitmap and buffer management entries
		uint32_t *refill_bitmap = new uint32_t[BITS_TO_UINT32S(kEntryNum)];
		struct BufferManagementEntry *bmes = new struct BufferManagementEntry[kEntryNum];

		ASSERT_NE(refill_bitmap, nullptr);
		ASSERT_NE(bmes, nullptr);

		memset(refill_bitmap, 0, BITS_TO_UINT32S(kEntryNum) * sizeof(uint32_t));
		memset(bmes, 0, sizeof(struct BufferManagementEntry) * kEntryNum);

		tbl_info->refill_bitmap_addr = reinterpret_cast<uint64_t>(refill_bitmap);
		tbl_info->refill_bitmap_size = BITS_TO_UINT32S(kEntryNum) * sizeof(uint32_t);
		tbl_info->bmes_addr = reinterpret_cast<uint64_t>(bmes);
		tbl_info->bmes_size = sizeof(struct BufferManagementEntry) * kEntryNum;
	}

	void DestroyMockBufferManagementTable(struct BufferManagementTableInfo *const tbl_info)
	{
		// Free the allocated memory
		delete[] reinterpret_cast<uint32_t *>(tbl_info->refill_bitmap_addr);
		delete[] reinterpret_cast<struct BufferManagementEntry *>(tbl_info->bmes_addr);
	}

	struct BufferManager bm_;
	struct BufferManagerInfo bm_info_;
	struct BufferManagementTableInfo table_info_[kBufferTableTypeNum];
};

TEST_F(BmTest, InitSuccess)
{
	// Initialize the BufferManagementTable
	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);
	EXPECT_EQ(bm_.bm_info, &bm_info_);
	BmDeinit(&bm_);
}

TEST_F(BmTest, DeinitSuccess)
{
	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);
	BmDeinit(&bm_);
	EXPECT_EQ(bm_.bm_info, nullptr);
}

TEST_F(BmTest, BmSetReplenishCallback)
{
	auto callback = [](struct BufferManager *const) {};

	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);

	BmSetReplenishCallback(&bm_, kBufferTableTypeNoaRx, callback);
	ASSERT_EQ(callback, bm_.buffer_replenish_callbacks[kBufferTableTypeNoaRx]);
	BmDeinit(&bm_);
}

TEST_F(BmTest, BmSync)
{
	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);

	(reinterpret_cast<uint32_t *>(table_info_[kBufferTableTypeNoaRx].refill_bitmap_addr))[0] =
		1;

	struct BufferManagementEntry *entry = &(reinterpret_cast<struct BufferManagementEntry *>(
		table_info_[kBufferTableTypeNoaRx].bmes_addr))[0];
	entry->pktid = kPktidOffset;
	entry->buffer_addr_phy = 0x12345678;
	entry->buffer_addr_cpu = 0x87654321;
	entry->buffer_size = 4096;

	table_info_[kBufferTableTypeNoaRx].wlan_sw_sync_request = 1;
	BmSync(&bm_);
	EXPECT_EQ(table_info_[kBufferTableTypeNoaRx].wlan_sw_sync_request, 0);

	struct BufferInfo buffer_info;
	ASSERT_EQ(BmGetBufferInfo(&bm_, kBufferTableTypeNoaRx, kPktidOffset, &buffer_info), 0);

	EXPECT_EQ(buffer_info.buffer_addr_phy, 0x12345678U);
	EXPECT_EQ(buffer_info.buffer_addr_cpu, 0x87654321U);
	EXPECT_EQ(buffer_info.buffer_size, 4096U);
	EXPECT_EQ(buffer_info.pktid, kPktidOffset);
	BmDeinit(&bm_);
}

TEST_F(BmTest, BmBufferBatchAllocFailed)
{
	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);
	struct BufferInfo buffer_info[2];
	EXPECT_EQ(BmBufferBatchAlloc(&bm_, kBufferTableTypeNoaRx, 2, buffer_info), -ENOMEM);
	BmDeinit(&bm_);
}

TEST_F(BmTest, BmBufferBatchAllocSuccess)
{
	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);

	(reinterpret_cast<uint32_t *>(table_info_[kBufferTableTypeNoaRx].refill_bitmap_addr))[0] =
		3;

	struct BufferManagementEntry *entries = reinterpret_cast<struct BufferManagementEntry *>(
		table_info_[kBufferTableTypeNoaRx].bmes_addr);
	entries[0].pktid = kPktidOffset;
	entries[0].buffer_addr_phy = 0x12345678;
	entries[0].buffer_addr_cpu = 0x87654321;
	entries[0].buffer_size = 4096;

	entries[1].pktid = kPktidOffset + 1;
	entries[1].buffer_addr_phy = 0x11111111;
	entries[1].buffer_addr_cpu = 0x22222222;
	entries[1].buffer_size = 1024;

	table_info_[kBufferTableTypeNoaRx].wlan_sw_sync_request = 1;
	BmSync(&bm_);
	EXPECT_EQ(table_info_[kBufferTableTypeNoaRx].wlan_sw_sync_request, 0);

	struct BufferInfo buffer_info[2];
	ASSERT_EQ(BmBufferBatchAlloc(&bm_, kBufferTableTypeNoaRx, 2, buffer_info), 2);

	EXPECT_EQ(buffer_info[1].buffer_addr_phy, 0x12345678U);
	EXPECT_EQ(buffer_info[1].buffer_addr_cpu, 0x87654321U);
	EXPECT_EQ(buffer_info[1].buffer_size, 4096U);
	EXPECT_EQ(buffer_info[1].pktid, kPktidOffset);

	EXPECT_EQ(buffer_info[0].buffer_addr_phy, 0x11111111U);
	EXPECT_EQ(buffer_info[0].buffer_addr_cpu, 0x22222222U);
	EXPECT_EQ(buffer_info[0].buffer_size, 1024U);
	EXPECT_EQ(buffer_info[0].pktid, kPktidOffset + 1);
	BmDeinit(&bm_);
}

TEST_F(BmTest, BmReclaim)
{
	ASSERT_EQ(BmInit(&bm_, &bm_info_), 0);
	BmReclaim(&bm_, kBufferTableTypeNoaRx, 100);
	EXPECT_EQ(TEST_BIT(bm_.tables[kBufferTableTypeNoaRx].refill_bitmap, 0), 1U);
	EXPECT_EQ(bm_.tables[kBufferTableTypeNoaRx].wlan_fw_sync_request, true);
	BmDeinit(&bm_);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
