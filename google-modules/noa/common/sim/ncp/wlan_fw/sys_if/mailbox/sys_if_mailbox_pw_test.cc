#include "sys_if/mailbox/sys_if_mailbox_pw.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr ::noa::driver::mailbox::MailboxClientId kTestMailboxId =
	::noa::driver::mailbox::kMbaApNs1;
class WlanSysIfMailboxHelperTest : public ::testing::Test {
    public:
	void SetUp() override
	{
	}

	void TearDown() override
	{
	}

    protected:
	::noa::service::wlan_service::SysIfMailboxHelper mbx_helper_{ kTestMailboxId };
};

class MockMailboxHandlerTemplate {
    public:
	virtual ~MockMailboxHandlerTemplate() = default;
	virtual ::noa::module::interrupt::NoaIrqReturn MailboxHandler(int32_t, void *) = 0;
};

class MockMailboxHandler : public MockMailboxHandlerTemplate {
    public:
	MOCK_METHOD(::noa::module::interrupt::NoaIrqReturn, MailboxHandler, (int32_t, void *),
		    (override));
};

TEST_F(WlanSysIfMailboxHelperTest, MailboxHandlerLifecycleSuccess)
{
	constexpr uint32_t kTestDoorbellNum = 0;
	MockMailboxHandler handler;
	EXPECT_CALL(handler, MailboxHandler(kTestMailboxId, &handler))
		.Times(1)
		.WillOnce(testing::Return(::noa::module::interrupt::kNoaIrqHandled));

	EXPECT_EQ(0, mbx_helper_.RegisterMailboxHandler(
			     kTestDoorbellNum,
			     [&handler](int32_t mailbox_id,
					void *ctx) -> ::noa::module::interrupt::NoaIrqReturn {
				     return handler.MailboxHandler(mailbox_id, ctx);
			     },
			     &handler));

	mbx_helper_.InitiateHandler(kTestDoorbellNum);
	mbx_helper_.Unregister(kTestDoorbellNum);

	// Initiate again, and expect no handler callback will be called
	mbx_helper_.InitiateHandler(kTestDoorbellNum);
}

} // anonymous namespace
} // namespace noa::service::wlan_service