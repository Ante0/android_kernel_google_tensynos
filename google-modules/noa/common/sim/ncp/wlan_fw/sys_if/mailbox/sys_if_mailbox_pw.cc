#include "sys_if_mailbox_pw.h"

#include <inttypes.h>

#include "pw_log/log.h"
#include "pw_assert/assert.h"
#include "common/compiler.h"

namespace noa::service::wlan_service
{

static const char *GetMailboxName(::noa::driver::mailbox::MailboxClientId mailbox_id)
{
#ifdef HOST_SIMULATOR
	return "wlan_test_mailbox";
#else
	switch (mailbox_id) {
	case driver::mailbox::MailboxClientId::kMbaNepWifiRing:
		return "wlan_nep_wifi_mailbox";
	case driver::mailbox::MailboxClientId::kMbaApModem:
		return "wlan_ap_wifi_mailbox";
	case driver::mailbox::MailboxClientId::kMbaNepApWifi:
		return "wlan_nep_to_ap_wifi_mailbox";
	default:
		PW_LOG_ERROR("Invalid mailbox type");
		return nullptr;
	};
#endif
	return nullptr;
}

#define REGISTER_NOTIFICATION_HANDLER(doorbell_num)                                                \
	notifier_.RegisterNotificationHandler(                                                     \
		[](void *context) -> int32_t {                                                     \
			SysIfMailboxHelper *helper = static_cast<SysIfMailboxHelper *>(context);   \
			helper->InitiateHandler(doorbell_num);                                     \
			return 0;                                                                  \
		},                                                                                 \
		doorbell_num, this);

SysIfMailboxHelper::SysIfMailboxHelper(::noa::driver::mailbox::MailboxClientId mailbox_id)
	: mailbox_id_(mailbox_id)
{
	int32_t ret = 0;
	uint32_t i = 0;

	do {
#ifndef HOST_SIMULATOR
		if (mailbox_id == driver::mailbox::MailboxClientId::kMbaNepApWifi) {
			ret = notifier_.Init(GetMailboxName(mailbox_id),
					     ::noa::module::notifier::kSender, mailbox_id_);
			if (ret) {
				PW_LOG_WARN("%s(): failed to init mailbox(%" PRIu32
					    ") handler, err: %" PRId32,
					    __func__, mailbox_id_, ret);
				break;
			}
			return;
		}
#endif

		ret = notifier_.Init(GetMailboxName(mailbox_id),
				     ::noa::module::notifier::kSender |
					     ::noa::module::notifier::kReceiver,
				     mailbox_id_);

		if (ret) {
			PW_LOG_WARN("%s(): failed to init mailbox(%" PRIu32
				    ") handler, err: %" PRId32,
				    __func__, mailbox_id_, ret);
			break;
		}

		for (i = 0; i < kMaxMailboxHandlerNum; i++) {
			notifier_.EnableDoorbell(i);
		}

		REGISTER_NOTIFICATION_HANDLER(0);
		REGISTER_NOTIFICATION_HANDLER(1);
		REGISTER_NOTIFICATION_HANDLER(2);
		REGISTER_NOTIFICATION_HANDLER(3);
		REGISTER_NOTIFICATION_HANDLER(4);
		REGISTER_NOTIFICATION_HANDLER(5);
		REGISTER_NOTIFICATION_HANDLER(6);
		REGISTER_NOTIFICATION_HANDLER(7);
		REGISTER_NOTIFICATION_HANDLER(8);
		REGISTER_NOTIFICATION_HANDLER(9);
		REGISTER_NOTIFICATION_HANDLER(10);
		REGISTER_NOTIFICATION_HANDLER(11);
		REGISTER_NOTIFICATION_HANDLER(12);
		REGISTER_NOTIFICATION_HANDLER(13);
		REGISTER_NOTIFICATION_HANDLER(14);
		REGISTER_NOTIFICATION_HANDLER(15);
		REGISTER_NOTIFICATION_HANDLER(16);
		REGISTER_NOTIFICATION_HANDLER(17);
		REGISTER_NOTIFICATION_HANDLER(18);
		REGISTER_NOTIFICATION_HANDLER(19);
		REGISTER_NOTIFICATION_HANDLER(20);
		REGISTER_NOTIFICATION_HANDLER(21);
		REGISTER_NOTIFICATION_HANDLER(22);
		REGISTER_NOTIFICATION_HANDLER(23);
		REGISTER_NOTIFICATION_HANDLER(24);
		REGISTER_NOTIFICATION_HANDLER(25);
		REGISTER_NOTIFICATION_HANDLER(26);
		REGISTER_NOTIFICATION_HANDLER(27);
		REGISTER_NOTIFICATION_HANDLER(28);
		REGISTER_NOTIFICATION_HANDLER(29);
		REGISTER_NOTIFICATION_HANDLER(30);
		REGISTER_NOTIFICATION_HANDLER(31);

	} while (false);
};

SysIfMailboxHelper::~SysIfMailboxHelper()
{
	uint32_t i;

	for (i = 0; i < kMaxMailboxHandlerNum; i++) {
		notifier_.DisableDoorbell(i);
	}
	notifier_.UnregisterNotificationHandler();
	notifier_.Deinit();
}

SysIfMailboxHelper *SysIfMailboxHelper::GetInstance(NcpWifiMailboxType type)
{
#ifdef HOST_SIMULATOR
	SEC_FAST_DATA static SysIfMailboxHelper ap_wifi_helper{
		::noa::driver::mailbox::MailboxClientId::kMbaApNs1
	};
	SEC_FAST_DATA static SysIfMailboxHelper nep_wifi_helper{
		::noa::driver::mailbox::MailboxClientId::kMbaApNs2
	};
	static SysIfMailboxHelper &nep_to_ap_wifi_helper = ap_wifi_helper;
#else
	SEC_FAST_DATA static SysIfMailboxHelper nep_wifi_helper{
		::noa::driver::mailbox::MailboxClientId::kMbaNepWifiRing
	};
	SEC_FAST_DATA static SysIfMailboxHelper ap_wifi_helper{
		::noa::driver::mailbox::MailboxClientId::kMbaApModem
	};
	SEC_FAST_DATA static SysIfMailboxHelper nep_to_ap_wifi_helper{
		::noa::driver::mailbox::MailboxClientId::kMbaNepApWifi
	};
#endif // HOST_SIMULATOR
	switch (type) {
	case kNcpWifiMailboxTypeNep:
		return &nep_wifi_helper;
	case kNcpWifiMailboxTypeAp:
		return &ap_wifi_helper;
	case kNcpWifiMailboxTypeNepToAp:
		return &nep_to_ap_wifi_helper;
	default:
		PW_LOG_ERROR("Invalid mailbox type");
		return nullptr;
	}
}

int32_t SysIfMailboxHelper::RegisterMailboxHandler(uint32_t doorbell_num,
						   MailboxHandlerHelper handler, void *ctx)
{
	if (doorbell_num < kMaxMailboxHandlerNum) {
		mailbox_handler_info_[doorbell_num].handler_ = std::move(handler);
		mailbox_handler_info_[doorbell_num].context_ = ctx;
	}

	return 0;
}

void SysIfMailboxHelper::Unregister(uint32_t doorbell_num)
{
	if (doorbell_num < kMaxMailboxHandlerNum) {
		mailbox_handler_info_[doorbell_num].handler_ = nullptr;
		mailbox_handler_info_[doorbell_num].context_ = nullptr;
	}
}

void SysIfMailboxHelper::Notify(uint32_t doorbell_num)
{
	notifier_.Notify(doorbell_num);
}

void SysIfMailboxHelper::InitiateHandler(uint32_t doorbell_num)
{
	if (mailbox_handler_info_[doorbell_num].handler_) {
		mailbox_handler_info_[doorbell_num].handler_(
			static_cast<int32_t>(mailbox_id_),
			mailbox_handler_info_[doorbell_num].context_);
	}
}

} // namespace noa::service::wlan_service

int32_t SysIfRegisterMailbox(NcpWifiMailboxType type, uint32_t doorbell_num, MailboxHandler handler,
			     void *ctx)
{
	::noa::service::wlan_service::SysIfMailboxHelper *helper =
		::noa::service::wlan_service::SysIfMailboxHelper::GetInstance(type);

	if (helper) {
		return helper->RegisterMailboxHandler(doorbell_num, std::move(handler), ctx);
	}

	return 0;
}

void SysIfUnregisterMailbox(NcpWifiMailboxType type, uint32_t doorbell_num)
{
	::noa::service::wlan_service::SysIfMailboxHelper *helper =
		::noa::service::wlan_service::SysIfMailboxHelper::GetInstance(type);

	if (helper) {
		helper->Unregister(doorbell_num);
	}
}

void SysIfNotifyMailbox(NcpWifiMailboxType type, uint32_t doorbell_num)
{
	::noa::service::wlan_service::SysIfMailboxHelper *helper =
		::noa::service::wlan_service::SysIfMailboxHelper::GetInstance(type);

	if (helper) {
		helper->Notify(doorbell_num);
	}
}
