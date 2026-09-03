#include "wlan_dp.h"

#include "gtest/gtest.h"
#include "interrupt/interrupt.h"
#include "modules/wlan_ring_manager/wlan_ring_manager.h"
#include "modules/sta_table/sta_table.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"
#include "modules/wlan_nep_buffer_pool/wlan_nep_buffer_pool.h"
#include "modules/wlan_ring_manager/wlan_wdev_ring_manager.h"
#include "modules/wlan_ring_manager/wlan_nep_ring_manager.h"
#include "wdev_if/wdev_if.h"
#include "ext_svc/ext_svc.h"

namespace
{

constexpr uint32_t kIrqNumber = 25;

class WlanServiceDpTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		// Initialize test data
		memset(&params_, 0, sizeof(struct WlanDpInitParams));
		params_.num_ring_svc_irq = 1;
		params_.ring_svc_irq_info[0].irq_num = 0;
		params_.ring_svc_irq_info[0].rx_data_ring_polling_mask = 0x1;
		params_.ring_svc_irq_info[0].tx_cpl_ring_polling_mask = 0;

		params_.ring_manager = &wlan_ring_manager_;
		params_.wdev_if = &wdev_if_;
		params_.nep_tx_buffer_pool = &nep_tx_buffer_pool_.base;
		params_.ext_svc = &ext_svc_;
		params_.sta_table = &sta_table_;

		::noa::module::interrupt::InterruptController::Instance()->Deinit();
		::noa::module::interrupt::InterruptController::Instance()->Init(kIrqNumber);

		ASSERT_EQ(WlanDpInit(&wlan_dp_, &params_), 0);

		struct WlanDpInitIrqInfo irq_info[2];
		irq_info[0].irq_num = 5;
		irq_info[0].tx_cpl_ring_polling_mask = 0x2;
		irq_info[0].rx_data_ring_polling_mask = 0x1;
		irq_info[1].irq_num = 10;
		irq_info[1].tx_cpl_ring_polling_mask = 0x8;
		irq_info[1].rx_data_ring_polling_mask = 0x4;
		WlanDpWdevRequestIrqs(&wlan_dp_, 2, irq_info);
		WlanDpStart(&wlan_dp_);
	}

	void TearDown() override
	{
		WlanDpStop(&wlan_dp_);
		WlanDpDeinit(&wlan_dp_);
		::noa::module::interrupt::InterruptController::Instance()->Deinit();
	}

	WlanDp wlan_dp_;
	WlanDpInitParams params_;
	WdevIf wdev_if_;
	WlanRingManager wlan_ring_manager_;
	StaTable sta_table_;
	WlanNepTxBufferPool nep_tx_buffer_pool_;
	ExternalServices ext_svc_;
};

// Test successful initialization and deinitialization of the WLAN
// data path.
TEST_F(WlanServiceDpTest, InitDeinitSuccess)
{
	// Verify the number of WLAN device and ring service IRQs.
	EXPECT_EQ(wlan_dp_.num_wlan_dev_irq, 2);
	EXPECT_EQ(wlan_dp_.num_ring_svc_irq, 1);
	// Verify the priority of the RX worker.
	EXPECT_EQ(wlan_dp_.wlan_dev_dp_worker.priority, kWlanWorkerPriorityHigh);
}

// Test the WlanDpIsr function with RX interrupts.
TEST_F(WlanServiceDpTest, WlanDpWdevIsrRx)
{
	// Simulate an RX interrupt on the first WLAN device IRQ.
	struct WlanIntrContext *intr_ctx = &wlan_dp_.wlan_dev_intr_ctx_group[0];
	ASSERT_EQ(WlanDpWdevRxIsr(5, intr_ctx), kIrqHandled);

	// Verify the RX worker's polling masks and interrupt statistics.
	EXPECT_EQ(wlan_dp_.wlan_dev_dp_worker.intr_ctx_polling_mask, (1U << 0));
	EXPECT_EQ(intr_ctx->intr_stats.num_total_intr, 1U);
	EXPECT_EQ(intr_ctx->intr_stats.num_tx_cpl_ring[1], 1U);
	EXPECT_EQ(intr_ctx->intr_stats.num_rx_data_ring[0], 1U);
	EXPECT_EQ(intr_ctx->intr_stats.num_rx_data_ring[1], 0U);

	// Simulate an RX interrupt on the second WLAN device IRQ.
	intr_ctx = &wlan_dp_.wlan_dev_intr_ctx_group[1];
	ASSERT_EQ(WlanDpWdevRxIsr(10, intr_ctx), kIrqHandled);

	// Verify the RX worker's polling masks and interrupt statistics.
	EXPECT_EQ(wlan_dp_.wlan_dev_dp_worker.intr_ctx_polling_mask, (1U << 0) | (1U << 1));
	EXPECT_EQ(intr_ctx->intr_stats.num_total_intr, 1U);
	EXPECT_EQ(intr_ctx->intr_stats.num_tx_cpl_ring[3], 1U);
	EXPECT_EQ(intr_ctx->intr_stats.num_rx_data_ring[2], 1U);
}

// Test the WlanDpIsr function with an unknown IRQ number and interrupt
// type.
TEST_F(WlanServiceDpTest, UnknownWdevIrqNumAndType)
{
	struct WlanIntrContext *intr_ctx = &wlan_dp_.wlan_dev_intr_ctx_group[0];

	// Simulate an interrupt with an unknown IRQ number.
	ASSERT_EQ(WlanDpWdevRxIsr(kIrqNumber, intr_ctx), kIrqHandled);
	// Verify that the worker's polling masks and interrupt statistics
	// are not updated.
	EXPECT_EQ(wlan_dp_.wlan_dev_dp_worker.intr_ctx_polling_mask, 0U);
	EXPECT_EQ(intr_ctx->intr_stats.num_total_intr, 0U);
	EXPECT_EQ(intr_ctx->intr_stats.num_tx_cpl_ring[1], 0U);
	EXPECT_EQ(intr_ctx->intr_stats.num_rx_data_ring[0], 0U);

	// Verify that the ISR is triggered and executed successfully when
	// the correct IRQ number is provided.
	ASSERT_EQ(WlanDpWdevRxIsr(5, intr_ctx), kIrqHandled);
	ASSERT_EQ(intr_ctx->intr_stats.num_total_intr, 1U);
	ASSERT_EQ(intr_ctx->intr_stats.num_tx_cpl_ring[1], 1U);
	ASSERT_EQ(intr_ctx->intr_stats.num_rx_data_ring[0], 1U);
}

} // namespace
