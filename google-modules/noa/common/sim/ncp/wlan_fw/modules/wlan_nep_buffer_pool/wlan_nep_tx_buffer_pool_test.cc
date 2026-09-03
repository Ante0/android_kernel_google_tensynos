#include "wlan_nep_tx_buffer_pool.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "ring_mgmt/ring_manager.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uint32_t kWlanFwOutputBufferPoolId = 0;
constexpr uint16_t kTestPktid = 1;
constexpr uint64_t kTestPhyAddr = 0x12345678;
constexpr uintptr_t kTestCpuAddr = 0x87654321;

class WlanNepTxBufferPoolTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		root_.get_ring = [](const struct NoaRingSharedInfoRoot *root, uint8_t interface,
				    uint8_t flow, uint8_t category, uint8_t direction) {
			return &static_cast<WlanNepTxBufferPoolTest *>(root->memory_context)
					->fake_ring_;
		};
		// The 'memory_context' field is temporarily used to store the 'this' pointer
		// so we can retrieve the 'fake_ring' structure later.
		root_.memory_context = this;
		NoaRingSharedInfoRootRegister(&root_);
	}

	void TearDown() override
	{
		NoaRingSharedInfoRootRegister(nullptr);
	}

	struct WlanNepTxBufferPool tx_buffer_pool_;
	struct NoaRingSharedInfoRoot root_;
	struct noa_ring fake_ring_;
};

TEST_F(WlanNepTxBufferPoolTest, InitSuccess)
{
	// Call the init function and assert that the initialization was successful
	ASSERT_EQ(WlanNepBufferPoolInit(&tx_buffer_pool_.base, kNepTxBufferPool), 0);

	// Check ring setting
	EXPECT_EQ(static_cast<uintptr_t>(fake_ring_.base),
		  reinterpret_cast<uintptr_t>(tx_buffer_pool_.refill_ring_buffer));
	EXPECT_EQ(fake_ring_.len, sizeof(NoaBufferPoolDesc));

	WlanNepBufferPoolDeinit(&tx_buffer_pool_.base);
}

TEST_F(WlanNepTxBufferPoolTest, DeinitSuccess)
{
	// Call the init function and assert that the initialization was successful
	ASSERT_EQ(WlanNepBufferPoolInit(&tx_buffer_pool_.base, kNepTxBufferPool), 0);

	WlanNepBufferPoolDeinit(&tx_buffer_pool_.base);

	// Check table setting
	EXPECT_EQ(tx_buffer_pool_.base.buffer_pool_size, 0U);
}



TEST_F(WlanNepTxBufferPoolTest, ReplenishSuccess)
{
	// Call the init function and assert that the initialization was successful
	ASSERT_EQ(WlanNepBufferPoolInit(&tx_buffer_pool_.base, kNepTxBufferPool), 0);

	NoaBufferPoolDesc item;
	item.tkid = kTestPktid + WLAN_NEP_PKTID_MASK;
	item.dp_high = 0U;
	item.dp_low = kTestPhyAddr;
	item.dv = kTestCpuAddr;

	// Update buffer pool table and assert that is failed
	ASSERT_EQ(WlanNepBufferPoolReplenish(&tx_buffer_pool_.base, &item), 0);

	// Assert that the write index is 1 after a successful write.
	ASSERT_EQ(*reinterpret_cast<uint32_t *>(tx_buffer_pool_.base.ring_regs.write), 1U);

	auto *buffer_desc = reinterpret_cast<NoaBufferPoolDesc *>(
		*reinterpret_cast<char **>(tx_buffer_pool_.base.ring_regs.base));
	EXPECT_EQ(buffer_desc->tkid, kTestPktid + WLAN_NEP_PKTID_MASK);
	EXPECT_EQ(buffer_desc->dp_high, 0U);
	EXPECT_EQ(buffer_desc->dp_low, kTestPhyAddr);
	EXPECT_EQ(buffer_desc->dv, kTestCpuAddr);

	WlanNepBufferPoolDeinit(&tx_buffer_pool_.base);
}

TEST_F(WlanNepTxBufferPoolTest, SetPktidOffset)
{
	// Call the init function and assert that the initialization was successful
	ASSERT_EQ(WlanNepBufferPoolInit(&tx_buffer_pool_.base, kNepTxBufferPool), 0);

	WlanNepBufferPoolSetPktidOffset(&tx_buffer_pool_.base, 1);

	// Verify the content of the pktid offset register
	EXPECT_EQ(tx_buffer_pool_.base.pktid_offset, 1);

	WlanNepBufferPoolDeinit(&tx_buffer_pool_.base);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
