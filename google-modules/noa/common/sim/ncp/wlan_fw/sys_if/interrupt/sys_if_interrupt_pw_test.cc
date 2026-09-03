#include "sys_if_interrupt_pw.h"

#include <cerrno>
#include <cstdint>

#include "gtest/gtest.h"
#include "interrupt/interrupt.h"
#include "mock/mock_system_clock.h"

namespace noa::service::wlan_service
{
namespace
{

// TODO(b/371560507): Testing ClearIrq()

constexpr uint32_t kTotalIrqCount = 2;
constexpr uint32_t kTestIrqNumber = 1;

class WlanSysIfInterruptTest : public ::testing::Test {
    protected:
	WlanSysIfInterruptTest()
	{
		intr_controller_.Init(kTotalIrqCount);
	}

	~WlanSysIfInterruptTest() override
	{
		intr_controller_.Deinit();
	}

	noa::module::mock::MockSystemClock clock_;
	noa::module::interrupt::InterruptController intr_controller_{ clock_ };
	SysIfInterruptPwHelper sys_if_intr_noah_{ &intr_controller_ };
};

// This test verifies that requesting an IRQ with a null handler returns a
// non-zero value and the interrupt is not enabled.
TEST_F(WlanSysIfInterruptTest, RequestFailedWithNullHandler)
{
	// Attempt to request an IRQ with a null handler.
	ASSERT_EQ(sys_if_intr_noah_.RequestIrq(kTestIrqNumber, nullptr, nullptr), -EINVAL);
	// Verify that the interrupt is not enabled.
	EXPECT_FALSE(intr_controller_.IsInterruptEnabled(kTestIrqNumber));
}

// This test verifies that an IRQ can be successfully requested, enabled,
// disabled, and freed.
TEST_F(WlanSysIfInterruptTest, RequestEnableDisableAndFreeIrqSuccessfully)
{
	// Define a simple IRQ handler.
	noa::module::interrupt::NoaIrqHandler irq_handler =
		[](int32_t, void *) -> noa::module::interrupt::NoaIrqReturn {
		return noa::module::interrupt::kNoaIrqHandled;
	};

	// Request an IRQ with the handler.
	ASSERT_EQ(sys_if_intr_noah_.RequestIrq(kTestIrqNumber, irq_handler, this), 0);

	// Enable the IRQ.
	sys_if_intr_noah_.EnableIrq(kTestIrqNumber);
	// Verify that the interrupt is enabled.
	ASSERT_TRUE(intr_controller_.IsInterruptEnabled(kTestIrqNumber));

	// Disable the IRQ.
	sys_if_intr_noah_.DisableIrq(kTestIrqNumber);
	// Verify that the interrupt is disabled.
	EXPECT_FALSE(intr_controller_.IsInterruptEnabled(kTestIrqNumber));

	// Verify that freeing the IRQ returns the appropriate context pointer.
	ASSERT_EQ(sys_if_intr_noah_.FreeIrq(kTestIrqNumber, this), this);
}

} // namespace
} // namespace noa::service::wlan_service
