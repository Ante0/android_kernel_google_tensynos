#include "flow_id_table.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "gtest/gtest.h"
#include "ext_svc/ext_svc.h"

namespace noa::service::wlan_service
{
namespace
{

class FlowIdTableTest : public ::testing::Test {
    protected:
	// This function is called before each test.
	void SetUp() override
	{
		// Initialize the table to a clean state for every test.
		FlowIdTableInit(&table_, nullptr);

		// Prepare some mock data.
		ifindex_ = 0;
		oif_ = 15;
		role_ = IF_ROLE_AP; // Assuming AP mode for most tests
		memcpy(da1_, "\x00\x11\x22\x33\x44\x55", ETH_MAC_LEN);
		memcpy(da2_, "\xAA\xBB\xCC\xDD\xEE\xFF", ETH_MAC_LEN);
	}

	// This function is called after each test.
	void TearDown() override
	{
		FlowIdTableDeinit(&table_);
	}

	FlowIdTable table_{};
	uint8_t ifindex_{};
	uint8_t role_{};
	uint32_t oif_{};
	uint8_t da1_[ETH_MAC_LEN]{};
	uint8_t da2_[ETH_MAC_LEN]{};
};

TEST_F(FlowIdTableTest, AddAndVerifySingleFlow)
{
	const uint16_t flowid = 101;
	const uint8_t prio = 3;

	int32_t ret = AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, flowid, prio, da1_, role_);

	ASSERT_EQ(ret, 0);

	// Find the station info node (should be the first one).
	const StaFlowInfoNode *sta_node = &table_.if_flow_lk_up[ifindex_].sta_flow_info[0];
	EXPECT_TRUE(sta_node->enable);
	EXPECT_EQ(memcmp(sta_node->da, da1_, ETH_MAC_LEN), 0);

	// Check the specific priority slot.
	EXPECT_EQ(sta_node->flow_info[prio].flowid, flowid);
	EXPECT_EQ(sta_node->flow_info[prio].prio, prio);
	EXPECT_EQ(sta_node->ifindex, ifindex_);
}

TEST_F(FlowIdTableTest, AddMultipleFlowsForSameStation)
{
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 101, 3, da1_, role_);
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 102, 5, da1_, role_);

	const StaFlowInfoNode *sta_node = &table_.if_flow_lk_up[ifindex_].sta_flow_info[0];
	EXPECT_TRUE(sta_node->enable);
	EXPECT_EQ(memcmp(sta_node->da, da1_, ETH_MAC_LEN), 0);

	EXPECT_EQ(sta_node->flow_info[3].flowid, 101);
	EXPECT_EQ(sta_node->flow_info[5].flowid, 102);

	// Make sure the second station slot was not used.
	const StaFlowInfoNode *second_sta_node = &table_.if_flow_lk_up[ifindex_].sta_flow_info[1];
	EXPECT_FALSE(second_sta_node->enable);
}

TEST_F(FlowIdTableTest, DeleteSingleFlow)
{
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 101, 3, da1_, role_);
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 102, 5, da1_, role_);

	int32_t ret = DeleteFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 101);

	ASSERT_EQ(ret, 0);

	const StaFlowInfoNode *sta_node = &table_.if_flow_lk_up[ifindex_].sta_flow_info[0];
	EXPECT_TRUE(sta_node->enable);
	EXPECT_EQ(sta_node->flow_info[3].flowid, INVALID_FLOWID);
	EXPECT_EQ(sta_node->flow_info[5].flowid, 102);
}

TEST_F(FlowIdTableTest, DeleteLastFlowForStation)
{
	uint8_t zero_mac[ETH_MAC_LEN] = { 0 };

	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 101, 3, da1_, role_);
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 105, 7, da2_, role_);

	int32_t ret = DeleteFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 101);

	ASSERT_EQ(ret, 0);
	const StaFlowInfoNode *sta_node = &table_.if_flow_lk_up[ifindex_].sta_flow_info[0];
	EXPECT_FALSE(sta_node->enable);
	EXPECT_EQ(memcmp(sta_node->da, zero_mac, ETH_MAC_LEN), 0);
	const StaFlowInfoNode *second_sta_node = &table_.if_flow_lk_up[ifindex_].sta_flow_info[1];
	EXPECT_TRUE(second_sta_node->enable);
	EXPECT_EQ(memcmp(second_sta_node->da, da2_, ETH_MAC_LEN), 0);
	EXPECT_EQ(second_sta_node->flow_info[7].flowid, 105);
	EXPECT_EQ(second_sta_node->flow_info[7].prio, 7);
	EXPECT_EQ(table_.if_flow_lk_up[ifindex_].role, role_);
}

TEST_F(FlowIdTableTest, DeleteFailsForNonExistentFlow)
{
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 101, 3, da1_, role_);

	int32_t ret = DeleteFlowIdLookUpTableEntry(&table_, oif_, ifindex_, 999);

	EXPECT_NE(ret, 0);
	EXPECT_EQ(ret, -ENOENT);
}

TEST_F(FlowIdTableTest, FullFlowIdLookUp)
{
	// ARRANGE:
	// 1. Create a custom mapping: User Priority 5 -> Flow Priority 3.
	uint8_t custom_map[NUMPRIO] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	custom_map[5] = 3;
	UpdateUp2FlowPriorityTable(&table_, 0, custom_map);

	// 2. Add a flow entry for Flow Priority 3 with a known flowid.
	const uint16_t expected_flowid = 202;
	AddFlowIdLookUpTableEntry(&table_, oif_, ifindex_, expected_flowid, 3, da1_, role_);

	// ACT: Perform a full lookup using User Priority 5.
	int32_t found_flowid = FlowIdLookUp(&table_, ETH_P_IP, 5, ifindex_, da1_);

	// ASSERT: The final translated flowid should be correct.
	EXPECT_EQ(found_flowid, expected_flowid);
}

} // namespace
} // namespace noa::service::wlan_service