#include "wlan_wdev_ring_manager.h"
#include "wlan_ring_manager.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "sys_if/io/sys_if_io.h"

namespace noa::service::wlan_service
{
namespace
{

TEST(WlanWdevRingManagerTest, GetWlanWdevRingManager)
{
	WlanWdevRingManager wdev_ring_manager;
	WlanRingManager *manager = &wdev_ring_manager.manager;
	EXPECT_EQ(GetWlanWdevRingManager(manager), &wdev_ring_manager);
}

TEST(WlanWdevRingManagerTest, WlanWdevRingManagerInit)
{
	WlanWdevRingManager wdev_ring_manager;
	WlanRingManager *manager = &wdev_ring_manager.manager;
	ASSERT_EQ(WlanWdevRingManagerInit(manager), 0);
	EXPECT_EQ(manager->ring_groups[kTxPostRingGroup].num_ring, NUM_WDEV_TX_POST_RING);
	EXPECT_EQ(manager->ring_groups[kTxPostRingGroup].ring_pool,
		  wdev_ring_manager.tx_post_ring_pool);
	EXPECT_EQ(manager->ring_groups[kRxPostRingGroup].num_ring, NUM_WDEV_RX_POST_RING);
	EXPECT_EQ(manager->ring_groups[kRxPostRingGroup].ring_pool,
		  wdev_ring_manager.rx_post_ring_pool);
	EXPECT_EQ(manager->ring_groups[kTxCmplRingGroup].num_ring, NUM_WDEV_TX_CMPL_RING);
	EXPECT_EQ(manager->ring_groups[kTxCmplRingGroup].ring_pool,
		  wdev_ring_manager.tx_cmpl_ring_pool);
	EXPECT_EQ(manager->ring_groups[kRxCmplRingGroup].num_ring, NUM_WDEV_RX_CMPL_RING);
	EXPECT_EQ(manager->ring_groups[kRxCmplRingGroup].ring_pool,
		  wdev_ring_manager.rx_cmpl_ring_pool);
}

TEST(WlanWdevRingManagerTest, WlanRingManagerAddRingAndRemoveRing)
{
	WlanWdevRingManager nep_ring_manager;
	WlanRingManager *manager = &nep_ring_manager.manager;
	ASSERT_EQ(WlanWdevRingManagerInit(manager), 0);
	WlanRingInitParams params = {
		.name = "test_ring_0",
	};

	ASSERT_EQ(WlanRingManagerAddRing(manager, kTxPostRingGroup, 0, &params), 0);
	EXPECT_STREQ(manager->ring_groups[kTxPostRingGroup].ring_pool[0].name, "test_ring_0");

	ASSERT_EQ(WlanRingManagerRemoveRing(manager, kTxPostRingGroup, 0), 0);
	EXPECT_STRNE(manager->ring_groups[kTxPostRingGroup].ring_pool[0].name, "test_ring_0");
}

TEST(WlanWdevRingManagerTest, WlanWdevRingManagerDeinit)
{
	WlanWdevRingManager wdev_ring_manager;
	WlanRingManager *manager = &wdev_ring_manager.manager;
	ASSERT_EQ(WlanWdevRingManagerInit(manager), 0);
	WlanWdevRingManagerDeinit(manager);
	// Check if all members are zeroed
	EXPECT_EQ(manager->ring_groups[kTxPostRingGroup].num_ring, 0U);
	EXPECT_EQ(manager->ring_groups[kRxPostRingGroup].num_ring, 0U);
	EXPECT_EQ(manager->ring_groups[kTxCmplRingGroup].num_ring, 0U);
	EXPECT_EQ(manager->ring_groups[kRxCmplRingGroup].num_ring, 0U);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
