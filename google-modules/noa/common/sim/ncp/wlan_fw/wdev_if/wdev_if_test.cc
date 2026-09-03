#include "wdev_if.h"

#include <cstdint>
#include <cerrno>

#include "gtest/gtest.h"
#include "noa_desc.h"

namespace noa::service::wlan_service
{
namespace
{

TEST(WdevIfTest, InitWithInvalidChipId)
{
	WdevIf wdev_if;
	ASSERT_EQ(WdevIfInit(&wdev_if, kWlanDeviceChipIdEnd, 0, nullptr), -ENODEV);
	EXPECT_EQ(WdevIfGetCmplValidateDescriptorMethod(&wdev_if),
		  kWdevCmplDescCoherenceValidationMethodNone);
	EXPECT_EQ(WdevIfGetPostValidateDescriptorMethod(&wdev_if),
		  kWdevPostDescCoherenceValidationMethodNone);
	WdevIfDeinit(&wdev_if);
}

TEST(WdevIfTest, InitiateCallbacks)
{
	static bool deinit_is_called = false;
	static bool ack_interrupt_is_called = false;
	static bool ring_tx_post_doorbell_is_called = false;
	static bool get_tx_flow_ring_id_is_called = false;
	static bool prepare_noa_wlan_ext_desc_is_called = false;
	static bool prepare_tx_post_desc_is_called = false;
	static bool prepare_rx_post_desc_is_called = false;
	static bool handle_tx_cpl_desc_is_called = false;
	static bool handle_rx_cpl_desc_is_called = false;
	WdevIf wdev_if;

	wdev_if.chip_ops.Deinit = [](WdevIf *const) -> void { deinit_is_called = true; };
	wdev_if.chip_ops.AcknowledgeInterrupt = [](WdevIf *const, int32_t) -> void {
		ack_interrupt_is_called = true;
	};
	wdev_if.chip_ops.RingTxPostDoorbell = [](WdevIf *const, void *) -> void {
		ring_tx_post_doorbell_is_called = true;
	};
	wdev_if.hw_arch_ops.GetTxFlowRingId = [](WdevIf *const, const void *const,
						 uint32_t *) -> int32_t {
		get_tx_flow_ring_id_is_called = true;
		return 0;
	};
	wdev_if.hw_arch_ops.PrepareNoaWlanExtendTxD = [](WdevIf *const, const NoaDesc *const,
							 const StaInfo *const,
							 NoaWlanExtendTxD *) -> int32_t {
		prepare_noa_wlan_ext_desc_is_called = true;
		return 0;
	};
	wdev_if.hw_arch_ops.PrepareTxPostDesc =
		[](WdevIf *const, const NoaDesc *const, const void *const, uint32_t,
		   WdevPostDescCoherenceInfo *const, void *const) -> int32_t {
		prepare_tx_post_desc_is_called = true;
		return 0;
	};
	wdev_if.hw_arch_ops.PrepareRxPostDesc = [](WdevIf *const, uint16_t, uint32_t,
						   WdevPostDescCoherenceInfo *const,
						   void *const) -> int32_t {
		prepare_rx_post_desc_is_called = true;
		return 0;
	};
	wdev_if.hw_arch_ops.HandleTxCplDesc = [](WdevIf *const, const void *const,
						 WdevCmplDescCoherenceInfo *const,
						 WdevTxCmplDescriptorInfo *const) -> int32_t {
		handle_tx_cpl_desc_is_called = true;
		return 0;
	};
	wdev_if.hw_arch_ops.HandleRxCplDesc = [](WdevIf *const, const void *const,
						 WdevCmplDescCoherenceInfo *const,
						 WdevRxCmplDescriptorInfo *const) -> int32_t {
		handle_rx_cpl_desc_is_called = true;
		return 0;
	};

	ASSERT_EQ(WdevIfGetTxFlowRingId(&wdev_if, 0, nullptr), 0);
	EXPECT_TRUE(get_tx_flow_ring_id_is_called);
	ASSERT_EQ(WdevIfPrepareNoaWlanExtendTxD(&wdev_if, nullptr, nullptr, nullptr), 0);
	EXPECT_TRUE(prepare_noa_wlan_ext_desc_is_called);
	ASSERT_EQ(WdevIfPrepareTxPostDesc(&wdev_if, nullptr, nullptr, 0, nullptr, nullptr), 0);
	EXPECT_TRUE(prepare_tx_post_desc_is_called);
	ASSERT_EQ(WdevIfPrepareRxPostDesc(&wdev_if, 0, 0, nullptr, nullptr), 0);
	EXPECT_TRUE(prepare_rx_post_desc_is_called);
	ASSERT_EQ(WdevIfHandleTxCplDesc(&wdev_if, nullptr, nullptr, nullptr), 0);
	EXPECT_TRUE(handle_tx_cpl_desc_is_called);
	ASSERT_EQ(WdevIfHandleRxCplDesc(&wdev_if, nullptr, nullptr, nullptr), 0);
	EXPECT_TRUE(handle_rx_cpl_desc_is_called);
	WdevIfAcknowledgeInterrupt(&wdev_if, 0);
	EXPECT_TRUE(ack_interrupt_is_called);
	WdevIfRingTxPostDoorbell(&wdev_if, nullptr);
	EXPECT_TRUE(ring_tx_post_doorbell_is_called);
	WdevIfDeinit(&wdev_if);
	EXPECT_TRUE(deinit_is_called);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
