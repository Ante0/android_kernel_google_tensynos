#include "noa_ring_svc.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ext_svc/ext_svc.h"
#include "ring_mgmt/mock_ring_shared_info.h"

namespace noa::service::wlan_service
{
namespace
{

using ::noa::service::ring_service::MockRingSharedInfo;
using ::testing::Return;

TEST(NoaRingSvcTest, InitAndDeinit)
{
	MockRingSharedInfo info;
	struct noa_ring rx_cmpl;
	std::memset(&rx_cmpl, 0, sizeof(rx_cmpl));
	EXPECT_CALL(info, GetRing(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost,
				  kNoaWlanRingRxData, kNoaRingNepInput))
		.WillOnce(Return(&rx_cmpl));
	struct noa_ring tx_post;
	std::memset(&tx_post, 0, sizeof(tx_post));
	EXPECT_CALL(info, GetRing(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
				  kNoaWlanRingTxData, kNoaRingNepOutput))
		.WillOnce(Return(&tx_post));
	struct noa_ring direct_h2d_ring_pool[kNoaWlanDirectH2DRingMax];
	for (uint32_t i = 0; i < kNoaWlanDirectH2DRingMax; i++) {
		std::memset(&direct_h2d_ring_pool[i], 0, sizeof(direct_h2d_ring_pool[i]));
		EXPECT_CALL(info, GetRing(kNoaNetworkInterfaceWlanDirect,
					  kNoaNetworkFlowHostToDevice, i, kNoaNepRingAnyDirection))
			.WillOnce(Return(&direct_h2d_ring_pool[i]));
	}
	struct noa_ring direct_d2h_ring_pool[kNoaWlanDirectD2HRingMax];
	for (uint32_t i = 0; i < kNoaWlanDirectD2HRingMax; i++) {
		std::memset(&direct_d2h_ring_pool[i], 0, sizeof(direct_d2h_ring_pool[i]));
		EXPECT_CALL(info, GetRing(kNoaNetworkInterfaceWlanDirect,
					  kNoaNetworkFlowDeviceToHost, i, kNoaNepRingAnyDirection))
			.WillOnce(Return(&direct_d2h_ring_pool[i]));
	}

	ASSERT_EQ(NoaRingSharedInfoRootRegister(static_cast<NoaRingSharedInfoRoot *>(&info)), 0);

	ExternalServices ext_svc;
	ExternalServicesInitParams ext_svc_init_params;

	std::memset(&ext_svc_init_params, 0, sizeof(ext_svc_init_params));
	ext_svc_init_params.activate_wlan_fw_ring = [](void) -> int32_t { return 0; };
	ext_svc_init_params.deactivate_wlan_fw_input_ring = [](void) -> int32_t { return 0; };
	ext_svc_init_params.deactivate_wlan_fw_output_ring = [](void) -> int32_t { return 0; };

	ASSERT_EQ(ExtSvcInit(&ext_svc, &ext_svc_init_params), 0);

	NoaRingSvc ring_svc;
	NoaRingSvcInitParams ring_svc_init_params;

	std::memset(&ring_svc_init_params, 0, sizeof(ring_svc_init_params));
	ring_svc_init_params.ext_svc = &ext_svc;

	ASSERT_EQ(NoaRingSvcInit(&ring_svc, &ring_svc_init_params), 0);

	EXPECT_TRUE(is_noa_ring_activate(&ring_svc.ring_svc_tx_post_ring_pool[0]));
	EXPECT_TRUE(is_noa_ring_activate(&ring_svc.ring_svc_rx_cmpl_ring_pool[0]));

	NoaRingSvcDeinit(&ring_svc);

	EXPECT_FALSE(is_noa_ring_activate(&ring_svc.ring_svc_tx_post_ring_pool[0]));
	EXPECT_FALSE(is_noa_ring_activate(&ring_svc.ring_svc_rx_cmpl_ring_pool[0]));
}

} // anonymous namespace
} // namespace noa::service::wlan_service
