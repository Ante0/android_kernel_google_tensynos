#include "ext_svc.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{

// Test fixture for ExternalServices tests
class ExternalServicesTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		// Mock implementations for the function types
		params_.send_event_to_apc = [](uint32_t, void *const, uint32_t) -> int32_t {
			return 0;
		};
		params_.activate_wlan_fw_ring = []() -> int32_t { return 0; };
		params_.deactivate_wlan_fw_input_ring = []() -> int32_t { return 0; };
		params_.deactivate_wlan_fw_output_ring = []() -> int32_t { return 0; };
		params_.activate_nep_tx_buffer_pool = []() -> int32_t { return 0; };
		params_.deactivate_nep_tx_buffer_pool = []() -> int32_t { return 0; };
		params_.send_command_to_net_engine = [](int32_t, void *, size_t) -> int32_t {
			return 0;
		};

		ASSERT_EQ(ExtSvcInit(&ext_svc_, &params_), 0);
	}

	void TearDown() override
	{
		ExtSvcDeinit(&ext_svc_);
	}

	ExternalServices ext_svc_;
	ExternalServicesInitParams params_;
};

TEST_F(ExternalServicesTest, ExtSvcSendEventToApc)
{
	EXPECT_EQ(0, ExtSvcSendEventToApc(&ext_svc_, 0, nullptr, 0));
}

TEST_F(ExternalServicesTest, ExtSvcActivateWlanFwRing)
{
	EXPECT_EQ(0, ExtSvcActivateWlanFwRing(&ext_svc_));
}

TEST_F(ExternalServicesTest, ExtSvcDeactivateWlanFwInputRing)
{
	EXPECT_EQ(0, ExtSvcDeactivateWlanFwInputRing(&ext_svc_));
}

TEST_F(ExternalServicesTest, ExtSvcDeactivateWlanFwOutputRing)
{
	EXPECT_EQ(0, ExtSvcDeactivateWlanFwOutputRing(&ext_svc_));
}

TEST_F(ExternalServicesTest, ExtSvcActivateNepTxBufferPool)
{
	EXPECT_EQ(0, ExtSvcActivateNepTxBufferPool(&ext_svc_));
}

TEST_F(ExternalServicesTest, ExtSvcDeactivateNepTxBufferPool)
{
	EXPECT_EQ(0, ExtSvcDeactivateNepTxBufferPool(&ext_svc_));
}

TEST_F(ExternalServicesTest, ExtSvcSendCommandToNetEngine)
{
	EXPECT_EQ(0, ExtSvcSendCommandToNetEngine(&ext_svc_, 0, nullptr, 0));
}

// Test with null function pointers
TEST_F(ExternalServicesTest, NullFunctionPointers)
{
	ExtSvcDeinit(&ext_svc_); // Reset the external services
	ASSERT_EQ(-ENODEV, ExtSvcSendEventToApc(&ext_svc_, 0, nullptr, 0));
	ASSERT_EQ(-ENODEV, ExtSvcActivateWlanFwRing(&ext_svc_));
	ASSERT_EQ(-ENODEV, ExtSvcDeactivateWlanFwInputRing(&ext_svc_));
	ASSERT_EQ(-ENODEV, ExtSvcDeactivateWlanFwOutputRing(&ext_svc_));
	ASSERT_EQ(-ENODEV, ExtSvcActivateNepTxBufferPool(&ext_svc_));
	ASSERT_EQ(-ENODEV, ExtSvcDeactivateNepTxBufferPool(&ext_svc_));
	ASSERT_EQ(-ENODEV, ExtSvcSendCommandToNetEngine(&ext_svc_, 0, nullptr, 0));
}

} // namespace
} // namespace noa::service::wlan_service