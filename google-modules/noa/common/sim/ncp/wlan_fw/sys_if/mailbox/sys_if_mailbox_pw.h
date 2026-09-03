#pragma once

#include <cstdint>

#include "sys_if_mailbox.h"
#include "notifier/notifier_mailbox.h"
#include "pw_function/function.h"

namespace noa::service::wlan_service
{

constexpr uint32_t kMaxMailboxHandlerNum = 32;

using MailboxHandlerHelper =
	::pw::Function< ::noa::module::interrupt::NoaIrqReturn(int32_t, void *)>;

class SysIfMailboxHelper {
    public:
	/// @brief Constructs a SysIfMailboxHelper object.
	///
	/// @param[in] mailbox_id The ID of the WiFi mailbox.
	SysIfMailboxHelper(::noa::driver::mailbox::MailboxClientId mailbox_id);

	/// @brief Destroys the SysIfMailboxHelper object.
	~SysIfMailboxHelper();

	/// @brief Gets the singleton instance of the
	/// SysIfMailboxHelper class.
	///
	/// @return A pointer to the SysIfMailboxHelper instance.
	static SysIfMailboxHelper *GetInstance(NcpWifiMailboxType type);

	/// @brief Registers a handler for the WiFi mailbox.
	///
	/// @param[in] doorbell_num The doorbell number
	/// @param[in] handler The callback function to be invoked when the
	/// mailbox receives a message.
	/// @param[in] ctx A user-defined context pointer that will be
	/// passed to the handler function.
	///
	/// @return 0 on success, a error code otherwise.
	int32_t RegisterMailboxHandler(uint32_t doorbell_num, MailboxHandlerHelper handler,
				       void *ctx);

	/// @brief Unregisters the WiFi mailbox handler.
	///
	/// @param[in] doorbell_num The doorbell number
	void Unregister(uint32_t doorbell_num);

	/// @brief Notifies the WiFi mailbox that a message is
	/// available.
	///
	/// @param[in] doorbell_num The doorbell number
	void Notify(uint32_t doorbell_num);

	/// @brief Initiates the registered handler.
	///
	/// This function triggers the registered handler to process any
	/// pending messages in the WiFi mailbox.
	///
	/// @param[in] doorbell_num The doorbell number
	void InitiateHandler(uint32_t doorbell_num);

    private:
	::noa::driver::mailbox::MailboxClientId mailbox_id_;
	::noa::module::notifier::NotifierMailbox notifier_;
	struct {
		MailboxHandlerHelper handler_;
		void *context_;
	} mailbox_handler_info_[kMaxMailboxHandlerNum];
};

} // namespace noa::service::wlan_service
