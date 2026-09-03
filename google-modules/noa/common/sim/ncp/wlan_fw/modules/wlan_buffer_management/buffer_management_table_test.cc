#include "buffer_management_table.h"

#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uint16_t kEntryNum = 128;
constexpr uint16_t kPktidOffset = 100;

class BufferManagementTableTest : public ::testing::Test {
    public:
	void SetUp() override
	{
		CreateMockBufferManagementTable();
	}

	void TearDown() override
	{
		DestroyMockBufferManagementTable();
	}

    protected:
	struct BufferManagementTable tbl_;
	struct BufferManagementTableInfo tbl_info_;

	void CreateMockBufferManagementTable()
	{
		tbl_info_.entry_num = kEntryNum;
		tbl_info_.pktid_offset = kPktidOffset;

		// Allocate memory for pending bitmap and buffer management entries
		uint32_t *refill_bitmap = new uint32_t[BITS_TO_UINT32S(kEntryNum)];
		struct BufferManagementEntry *bmes = new struct BufferManagementEntry[kEntryNum];

		ASSERT_NE(refill_bitmap, nullptr);
		ASSERT_NE(bmes, nullptr);

		memset(refill_bitmap, 0, BITS_TO_UINT32S(kEntryNum) * sizeof(uint32_t));
		memset(bmes, 0, sizeof(struct BufferManagementEntry) * kEntryNum);

		tbl_info_.refill_bitmap_addr = reinterpret_cast<uint64_t>(refill_bitmap);
		tbl_info_.refill_bitmap_size = BITS_TO_UINT32S(kEntryNum) * sizeof(uint32_t);
		tbl_info_.bmes_addr = reinterpret_cast<uint64_t>(bmes);
		tbl_info_.bmes_size = sizeof(struct BufferManagementEntry) * kEntryNum;

		// Initialize the BufferManagementTable
		ASSERT_EQ(static_cast<int32_t>(0), BmtInit(&tbl_, &tbl_info_));
	}

	void DestroyMockBufferManagementTable()
	{
		BmtDeinit(&tbl_);

		// Free the allocated memory
		delete[] reinterpret_cast<uint32_t *>(tbl_info_.refill_bitmap_addr);
		delete[] reinterpret_cast<struct BufferManagementEntry *>(tbl_info_.bmes_addr);
	}
};

TEST_F(BufferManagementTableTest, BmtInitTest)
{
	// Verify the table is initialized correctly
	ASSERT_EQ(tbl_.entry_num, kEntryNum);
	ASSERT_EQ(tbl_.pktid_offset, kPktidOffset);
	ASSERT_NE(tbl_.refill_bitmap, nullptr);
	ASSERT_NE(tbl_.bmes, nullptr);
}

TEST_F(BufferManagementTableTest, BmtSyncTest)
{
	// Set some entries as pending and owned by NCP
	SET_BIT(tbl_.refill_bitmap, 5);
	tbl_.bmes[5].pktid = 105;
	SET_BIT(tbl_.refill_bitmap, 12);
	tbl_.bmes[12].pktid = 112;

	tbl_.table_info->wlan_sw_sync_request = 1;

	// Call BmtSync
	uint32_t sync_num;
	bool more;
	ASSERT_EQ(BmtSync(&tbl_, &sync_num, &more), 0);
	EXPECT_EQ(more, false);
	EXPECT_EQ(sync_num, 2U);

	// Verify the entries are added to the available pool
	ASSERT_EQ(tbl_.free_buf_pool.top, 2); // 2 entries

	// Verify the pending bits are cleared
	ASSERT_EQ(tbl_.table_info->wlan_sw_sync_request, 0U);
}

TEST_F(BufferManagementTableTest, BmtReclaimTest)
{
	// Check free buffer pool is empty after init
	ASSERT_EQ(tbl_.free_buf_pool.top, 0);

	// Add a entry to the available pool
	tbl_.bmes[8].pktid = 108;

	// Replenish a buffer
	BmtReclaim(&tbl_, 108);

	// Verify the entry is added to the available pool
	ASSERT_EQ(tbl_.free_buf_pool.top, 1);
	uint16_t entry_idx = tbl_.free_buf_pool.free_buf_idx[tbl_.free_buf_pool.top - 1];
	ASSERT_EQ(entry_idx, 8);
	struct BufferManagementEntry *entry = &tbl_.bmes[entry_idx];
	ASSERT_EQ(entry->pktid, 108);
}

TEST_F(BufferManagementTableTest, BmtBufferAllocTest)
{
	// Add some entries to the available pool
	tbl_.bmes[5].pktid = 105;
	tbl_.bmes[5].buffer_addr_cpu = 0x12345678U;
	BmtReclaim(&tbl_, tbl_.bmes[5].pktid);
	tbl_.bmes[12].pktid = 112;
	tbl_.bmes[12].buffer_addr_cpu = 0x87654321U;
	BmtReclaim(&tbl_, tbl_.bmes[12].pktid);

	struct BufferInfo buf;

	// Get a free buffer
	ASSERT_EQ(BmtBufferAlloc(&tbl_, &buf), 0);

	// Verify the returned values
	ASSERT_EQ(buf.pktid, 112);
	ASSERT_EQ(buf.buffer_addr_cpu, 0x87654321U);

	// Get another free buffer
	ASSERT_EQ(BmtBufferAlloc(&tbl_, &buf), 0);

	// Verify the returned values
	ASSERT_EQ(buf.pktid, 105); // next entry
	ASSERT_EQ(buf.buffer_addr_cpu, 0x12345678U);

	// Fail to get free buffer again
	ASSERT_NE(BmtBufferAlloc(&tbl_, &buf), 0);
}

TEST_F(BufferManagementTableTest, BmtGetBufferInfoTest)
{
	// Add a entry to the available pool
	tbl_.bmes[8].pktid = 108;
	tbl_.bmes[8].buffer_addr_cpu = 0x12345678U;
	tbl_.bmes[8].buffer_addr_phy = 0x87654321U;

	// Get the buffer
	struct BufferInfo buf;
	ASSERT_EQ(BmtGetBufferInfo(&tbl_, 108, &buf), 0);

	// Verify the buffer address
	ASSERT_EQ(buf.buffer_addr_cpu, 0x12345678U);
	ASSERT_EQ(buf.buffer_addr_phy, 0x87654321U);
}

TEST_F(BufferManagementTableTest, BmtDeinitTest)
{
	// Set some entries as owned by NCP
	SET_BIT(tbl_.refill_bitmap, 5);
	SET_BIT(tbl_.refill_bitmap, 12);

	// Sync BMT first
	uint32_t sync_num;
	bool more;
	ASSERT_EQ(BmtSync(&tbl_, &sync_num, &more), 0);

	// Call BmtDeinit
	BmtDeinit(&tbl_);

	// Verify the entries are reset to APC ownership
	for (uint32_t i = 0; i < tbl_.entry_num; i++) {
		EXPECT_NE(tbl_.bmes[i].ownership, kBufferOwnershipWlanFw);
	}

	// Verify the table is cleared
	ASSERT_EQ(tbl_.entry_num, 0);
	ASSERT_EQ(tbl_.pktid_offset, 0);
	ASSERT_EQ(tbl_.refill_bitmap, nullptr);
	ASSERT_EQ(tbl_.bmes, nullptr);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
