#include "sta_table.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "gtest/gtest.h"
#include "ext_svc/ext_svc.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uint8_t kTestMacAddr[kMacAddressLen] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
constexpr uint8_t kTestOif = 1;
constexpr uint8_t kTestBssId = 2;
constexpr uint8_t kTestEncryptType = 3;
constexpr uint8_t kTestEncapType = 1;
constexpr uint8_t kTestLmacId = 1;
constexpr uint8_t kTestBmid = 6;
constexpr uint8_t kTestSearchIdx = 7;
constexpr uint8_t kTestSearchType = 2;
constexpr uint8_t kTestDscpTidMapId = 9;
constexpr uint8_t kTestAddrYEn = 1;
constexpr uint8_t kTestAddrXEn = 1;

class StaTableTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		ExternalServicesInitParams ext_svc_init_params = {
			.send_event_to_apc = nullptr,
			.activate_wlan_fw_ring = nullptr,
			.deactivate_wlan_fw_input_ring = nullptr,
			.deactivate_wlan_fw_output_ring = nullptr,
			.activate_nep_tx_buffer_pool = nullptr,
			.deactivate_nep_tx_buffer_pool = nullptr,
			.send_command_to_net_engine = [](int32_t, void *, size_t) -> int32_t {
				return 0;
			},
		};

		ExtSvcInit(&ext_svc_, &ext_svc_init_params);
	}

	void TearDown() override
	{
		ExtSvcDeinit(&ext_svc_);
	}

	ExternalServices ext_svc_;
	StaTable sta_table_;
};

TEST_F(StaTableTest, InitAndDeinit)
{
	StaTableInit(&sta_table_, &ext_svc_);

	// Assertions for initialized values.
	ASSERT_EQ(sta_table_.num_sta_info, MAX_NUM_STA_SUPPORT);
	ASSERT_EQ(sta_table_.ext_svc, &ext_svc_);

	StaTableDeinit(&sta_table_);

	// Verify deinitialized values.
	EXPECT_EQ(sta_table_.num_sta_info, 0U);
	EXPECT_EQ(sta_table_.ext_svc, nullptr);
}

TEST_F(StaTableTest, GetStationNotInTheTable)
{
	StaTableInit(&sta_table_, &ext_svc_);

	const StaInfo *retrieved_sta_info;
	int32_t ret = StaTableGetStaInfo(&sta_table_, kTestMacAddr, kTestOif, &retrieved_sta_info);
	EXPECT_EQ(ret, -ENODEV);

	StaTableDeinit(&sta_table_);
}

TEST_F(StaTableTest, AddAndGetStation)
{
	StaTableInit(&sta_table_, &ext_svc_);
	StaInfo sta_info;

	std::memset(&sta_info, 0, sizeof(StaInfo));
	sta_info.oif = kTestOif;
	sta_info.bss_idx = kTestBssId;
	sta_info.encrypt_type = kTestEncryptType;
	sta_info.encap_type = kTestEncapType;
	sta_info.lmac_id = kTestLmacId;
	sta_info.bmid = kTestBmid;
	sta_info.search_idx = kTestSearchIdx;
	sta_info.search_type = kTestSearchType;
	sta_info.dscp_tid_map_id = kTestDscpTidMapId;
	sta_info.addry_en = kTestAddrYEn;
	sta_info.addrx_en = kTestAddrXEn;
	sta_info.enable = 1;
	std::memcpy(sta_info.mac_addr, kTestMacAddr, kMacAddressLen);

	// Add the station
	int32_t ret = StaTableAddStation(&sta_table_, 0, &sta_info);
	ASSERT_EQ(ret, 0);

	// Get the station info
	const StaInfo *retrieved_sta_info;
	ret = StaTableGetStaInfo(&sta_table_, kTestMacAddr, kTestOif, &retrieved_sta_info);
	ASSERT_EQ(ret, 0);

	// Verity that the retrieved info matches the added info
	EXPECT_EQ(retrieved_sta_info->oif, sta_info.oif);
	EXPECT_EQ(retrieved_sta_info->bss_idx, sta_info.bss_idx);
	EXPECT_EQ(retrieved_sta_info->encrypt_type, sta_info.encrypt_type);
	EXPECT_EQ(retrieved_sta_info->encap_type, sta_info.encap_type);
	EXPECT_EQ(retrieved_sta_info->lmac_id, sta_info.lmac_id);
	EXPECT_EQ(retrieved_sta_info->bmid, sta_info.bmid);
	EXPECT_EQ(retrieved_sta_info->search_idx, sta_info.search_idx);
	EXPECT_EQ(retrieved_sta_info->search_type, sta_info.search_type);
	EXPECT_EQ(retrieved_sta_info->dscp_tid_map_id, sta_info.dscp_tid_map_id);
	EXPECT_EQ(retrieved_sta_info->addry_en, sta_info.addry_en);
	EXPECT_EQ(retrieved_sta_info->addrx_en, sta_info.addrx_en);
	EXPECT_EQ(retrieved_sta_info->enable, sta_info.enable);

	StaTableDeinit(&sta_table_);
}

TEST_F(StaTableTest, AddExistingStation)
{
	StaTableInit(&sta_table_, &ext_svc_);

	StaInfo sta_info;
	sta_info.enable = 1;
	sta_info.oif = kTestOif;
	sta_info.bss_idx = kTestBssId;
	std::memcpy(sta_info.mac_addr, kTestMacAddr, kMacAddressLen);

	// Add the station
	int32_t ret = StaTableAddStation(&sta_table_, 0, &sta_info);
	ASSERT_EQ(ret, 0);
	EXPECT_EQ(sta_table_.num_active_sta, 1U);

	// Try to add the same station again
	ret = StaTableAddStation(&sta_table_, 0, &sta_info);
	// Should still succeed, but reuse the existing entry
	ASSERT_EQ(ret, 0);
	EXPECT_EQ(sta_table_.num_active_sta, 1U);

	StaTableDeinit(&sta_table_);
}

TEST_F(StaTableTest, RemoveStation)
{
	StaTableInit(&sta_table_, &ext_svc_);

	StaInfo sta_info;
	sta_info.enable = 1;
	sta_info.oif = kTestOif;
	sta_info.bss_idx = kTestBssId;
	std::memcpy(sta_info.mac_addr, kTestMacAddr, kMacAddressLen);

	// Add the station
	int32_t ret = StaTableAddStation(&sta_table_, 0, &sta_info);
	ASSERT_EQ(ret, 0);

	// Try to get the station info (should success)
	const StaInfo *retrieved_sta_info;
	ret = StaTableGetStaInfo(&sta_table_, kTestMacAddr, kTestOif, &retrieved_sta_info);
	ASSERT_EQ(ret, 0);

	// Remove the station
	ret = StaTableRemoveStation(&sta_table_, 0);
	ASSERT_EQ(ret, 0);

	// Try to get the station info (should fail)
	ret = StaTableGetStaInfo(&sta_table_, kTestMacAddr, kTestOif, &retrieved_sta_info);
	ASSERT_EQ(ret, -ENODEV);

	StaTableDeinit(&sta_table_);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
