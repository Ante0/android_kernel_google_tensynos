#include "wlan_nep_ring_manager.h"
#include "wlan_ring_manager.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "sys_if/io/sys_if_io.h"

namespace noa::service::wlan_service
{
namespace
{

TEST(WlanNepRingManagerTest, GetWlanNepRingManager)
{
	WlanNepRingManager nep_ring_manager;
	WlanRingManager *manager = &nep_ring_manager.manager;
	EXPECT_EQ(GetWlanNepRingManager(manager), &nep_ring_manager);
}

TEST(WlanNepRingManagerTest, WlanNepRingManagerInit)
{
	WlanNepRingManager nep_ring_manager;
	WlanRingManager *manager = &nep_ring_manager.manager;
	ASSERT_EQ(WlanNepRingManagerInit(manager), 0);
	EXPECT_EQ(manager->ring_groups[kTxPostRingGroup].num_ring, NUM_NEP_TX_POST_RING);
	EXPECT_EQ(manager->ring_groups[kTxPostRingGroup].ring_pool,
		  nep_ring_manager.tx_post_ring_pool);
	EXPECT_EQ(manager->ring_groups[kRxPostRingGroup].num_ring, NUM_NEP_RX_POST_RING);
	EXPECT_EQ(manager->ring_groups[kTxCmplRingGroup].num_ring, NUM_NEP_TX_CMPL_RING);
	EXPECT_EQ(manager->ring_groups[kRxCmplRingGroup].num_ring, NUM_NEP_RX_CMPL_RING);
	EXPECT_EQ(manager->ring_groups[kRxCmplRingGroup].ring_pool,
		  nep_ring_manager.rx_cmpl_ring_pool);
}

TEST(WlanNepRingManagerTest, WlanRingManagerAddRingAndRemoveRing)
{
	WlanNepRingManager nep_ring_manager;
	WlanRingManager *manager = &nep_ring_manager.manager;
	ASSERT_EQ(WlanNepRingManagerInit(manager), 0);
	WlanRingInitParams params = {
		.name = "test_ring_0",
	};

	ASSERT_EQ(WlanRingManagerAddRing(manager, kTxPostRingGroup, 0, &params), 0);
	EXPECT_STREQ(manager->ring_groups[kTxPostRingGroup].ring_pool[0].name, "test_ring_0");

	ASSERT_EQ(WlanRingManagerRemoveRing(manager, kTxPostRingGroup, 0), 0);
	EXPECT_STRNE(manager->ring_groups[kTxPostRingGroup].ring_pool[0].name, "test_ring_0");
}

TEST(WlanNepRingManagerTest, WlanNepRingManagerDeinit)
{
	WlanNepRingManager nep_ring_manager;
	WlanRingManager *manager = &nep_ring_manager.manager;
	ASSERT_EQ(WlanNepRingManagerInit(manager), 0);
	WlanNepRingManagerDeinit(manager);
	// Check if all members are zeroed
	EXPECT_EQ(manager->ring_groups[kTxPostRingGroup].num_ring, 0U);
	EXPECT_EQ(manager->ring_groups[kRxPostRingGroup].num_ring, 0U);
	EXPECT_EQ(manager->ring_groups[kTxCmplRingGroup].num_ring, 0U);
	EXPECT_EQ(manager->ring_groups[kRxCmplRingGroup].num_ring, 0U);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
