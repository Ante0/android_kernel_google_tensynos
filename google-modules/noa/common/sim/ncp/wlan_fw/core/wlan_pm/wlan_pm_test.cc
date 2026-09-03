#include "ext_svc/ext_svc.h"
#include "wlan_pm.h"

#include <cstddef>
#include <string.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

class MockNoaSystemService {
    public:
	MOCK_METHOD(int32_t, noa_power_vote, (bool));
};

class MockWlanRpcService {
    public:
	MOCK_METHOD(int32_t, send_event_to_apc, (uint32_t, void *, uint32_t));
};

class WlanPowerManagementTest : public ::testing::Test {
    public:
	static int noa_power_vote_wrapper(bool active)
	{
		return p_mock_noa_sys_svc->noa_power_vote(active);
	}

	static int32_t send_event_to_apc_wrapper(uint32_t event, void *msg, uint32_t msg_len)
	{
		return p_mock_wlan_rpc_svc->send_event_to_apc(event, msg, msg_len);
	}

	static MockNoaSystemService *p_mock_noa_sys_svc;
	static MockWlanRpcService *p_mock_wlan_rpc_svc;

    protected:
	void SetUp() override
	{
		memset(&default_ext_svc_, 0, sizeof(default_ext_svc_));

		default_ext_svc_.noa_system_service.noa_power_vote =
			WlanPowerManagementTest::noa_power_vote_wrapper;
		default_ext_svc_.wlan_rpc_service.send_event_to_apc =
			WlanPowerManagementTest::send_event_to_apc_wrapper;

		p_mock_noa_sys_svc = &mock_noa_sys_svc;
		p_mock_wlan_rpc_svc = &mock_wlan_rpc_svc;

		default_init_params_.ext_svc = &default_ext_svc_;
		default_init_params_.client.on_power_evt_cb = [](WlanPowerEvent, void *) {};
		default_init_params_.client.context = NULL;
	}

	void TearDown() override
	{
		::testing::Mock::VerifyAndClearExpectations(&mock_wlan_rpc_svc);
		::testing::Mock::VerifyAndClearExpectations(&mock_noa_sys_svc);
	}
	NiceMock<MockNoaSystemService> mock_noa_sys_svc;
	NiceMock<MockWlanRpcService> mock_wlan_rpc_svc;
	ExternalServices default_ext_svc_;
	WlanPmInitParams default_init_params_;
};

MockWlanRpcService *WlanPowerManagementTest::p_mock_wlan_rpc_svc = nullptr;
MockNoaSystemService *WlanPowerManagementTest::p_mock_noa_sys_svc = nullptr;

TEST_F(WlanPowerManagementTest, ShouldNotAllowNullInitParams)
{
	EXPECT_NE(WlanPmInit(NULL, NULL), 0);
}

TEST_F(WlanPowerManagementTest, DeinitShouldReturnWhenRpcToApFailed)
{
	WlanPmIface pm_iface;
	ASSERT_EQ(WlanPmInit(&default_init_params_, &pm_iface), 0);
	ASSERT_EQ(pm_iface.vote_bus_power(), 0);

	EXPECT_CALL(WlanPowerManagementTest::mock_wlan_rpc_svc, send_event_to_apc(_, _, _))
		.Times(1)
		.WillOnce(Return(-1));

	WlanPmDeinit(&pm_iface);
}

TEST_F(WlanPowerManagementTest, CastingAVoteToPowerOnBus)
{
	WlanPmIface pm_iface;
	ASSERT_EQ(WlanPmInit(&default_init_params_, &pm_iface), 0);

	EXPECT_EQ(pm_iface.vote_bus_power(), 0);
	EXPECT_EQ(pm_iface.get_wlan_power_bus_state(), kWlanBusPowerStatePoweringOn);

	pm_iface.notify_bus_on();
	EXPECT_EQ(pm_iface.get_wlan_power_bus_state(), kWlanBusPowerStatePowerOn);

	WlanPmDeinit(&pm_iface);
}

TEST_F(WlanPowerManagementTest, ShouldNotSendRpcToApFor2nd3rdVote)
{
	WlanPmIface pm_iface;
	ASSERT_EQ(WlanPmInit(&default_init_params_, &pm_iface), 0);
	ASSERT_EQ(pm_iface.vote_bus_power(), 0);
	pm_iface.notify_bus_on();
	ASSERT_EQ(pm_iface.get_wlan_power_bus_state(), kWlanBusPowerStatePowerOn);

	// The 2nd, 3nd vote should not trigger any rpc call to ap.
	// However, the deinit should trigger the rpc once to reset the fw state.
	EXPECT_CALL(WlanPowerManagementTest::mock_wlan_rpc_svc, send_event_to_apc(_, _, _))
		.Times(1)
		.WillOnce(Return(0));
	EXPECT_EQ(pm_iface.vote_bus_power(), 0);
	EXPECT_EQ(pm_iface.vote_bus_power(), 0);
	EXPECT_EQ(pm_iface.vote_bus_power(), 0);

	WlanPmDeinit(&pm_iface);
}

TEST_F(WlanPowerManagementTest, RemovingVoteShouldPowerDownBus)
{
	WlanPmIface pm_iface;
	ASSERT_EQ(WlanPmInit(&default_init_params_, &pm_iface), 0);

	ASSERT_EQ(pm_iface.vote_bus_power(), 0);
	pm_iface.notify_bus_on();
	ASSERT_EQ(pm_iface.get_wlan_power_bus_state(), kWlanBusPowerStatePowerOn);

	EXPECT_EQ(pm_iface.devote_bus_power(), 0);
	EXPECT_EQ(pm_iface.get_wlan_power_bus_state(), kWlanBusPowerStatePowerOff);

	WlanPmDeinit(&pm_iface);
}

} // namespace
} // namespace noa::service::wlan_service
