#ifndef SYS_IF_MAILBOX_SYS_IF_MAILBOX_H
#define SYS_IF_MAILBOX_SYS_IF_MAILBOX_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"

#if defined(__KERNEL__)
typedef irq_handler_t MailboxHandler;
#define kMailboxIrqHandled IRQ_HANDLED
typedef irqreturn_t MailboxReturn;
#else
constexpr noa::module::interrupt::NoaIrqReturn kMailboxIrqHandled =
	noa::module::interrupt::NoaIrqReturn::kNoaIrqHandled;
typedef ::noa::module::interrupt::NoaIrqReturn MailboxReturn;
typedef ::noa::module::interrupt::NoaIrqHandler MailboxHandler;
#endif

typedef enum NcpWifiMailboxType {
	kNcpWifiMailboxTypeStart = 0,
	kNcpWifiMailboxTypeNep = kNcpWifiMailboxTypeStart,
	kNcpWifiMailboxTypeAp,
	kNcpWifiMailboxTypeNepToAp,
	kNcpWifiMailboxTypeEnd,
} NcpWifiMailboxType;

typedef enum WifiNcp2ApcDoorbellType {
	kWifiNcp2ApcDoorbellTypeStart = 0,
	kDoorbellRxEvent = kWifiNcp2ApcDoorbellTypeStart,
	kDoorbellFwTrapEvent,
	kPacketSnifferFullEvent,
	kWifiNcp2ApcDoorbellTypeEnd,
	kWifiNcp2ApcDoorbellTypeNum = kWifiNcp2ApcDoorbellTypeEnd,
} WifiNcp2ApcDoorbellType;

typedef enum WifiApc2NcpDoorbellType {
	kWifiApc2NcpDoorbellTypeStart = 0,
	kDoorbellBmUpdate = kWifiApc2NcpDoorbellTypeStart,
	kDoorbellStationInfoSync,
	kDoorbellTxRingInfoSync,
	kDoorbellDirectSubEvent,
	kDoorbellPacketSnifferReset,
	kDoorbellPcieOwnershipSwitch,
	kWifiApc2NcpDoorbellTypeEnd,
	kWifiApc2NcpDoorbellTypeNum = kWifiApc2NcpDoorbellTypeEnd,
} WifiApc2NcpDoorbellType;

/// @brief Registers a handler for the WiFi mailbox.
///
/// This function registers a callback function that will be invoked
/// when the WiFi mailbox receives a message. The handler function
/// should have the following signature:
///
/// @param[in] type mailbox type that we intend to register handler for
/// @param[in] doorbell_num The doorbell number
/// @param[in] handler The callback function to be invoked when the
/// mailbox receives a message.
/// @param[in] ctx A user-defined context pointer that will be passed
/// to the handler function.
///
/// @return 0 on success, a error code otherwise.
extern int32_t SysIfRegisterMailbox(NcpWifiMailboxType type, uint32_t doorbell_num,
				    MailboxHandler handler, void *ctx);

/// @brief Unregisters the WiFi mailbox handler.
///
/// This function unregisters the callback function that was previously
/// registered with SysIfRegisterMailbox.
/// @param[in] type mailbox type that we intend to be unregistered
/// @param[in] doorbell_num The doorbell number
extern void SysIfUnregisterMailbox(NcpWifiMailboxType type, uint32_t doorbell_num);

/// @brief Notifies the WiFi mailbox that a message is available.
///
/// This function triggers the side registered handler to process
/// pending messages in the WiFi mailbox.
/// @param[in] type mailbox type that we intend to notify
/// @param[in] doorbell_num The doorbell number
extern void SysIfNotifyMailbox(NcpWifiMailboxType type, uint32_t doorbell_num);

#endif /* SYS_IF_MAILBOX_SYS_IF_MAILBOX_H */
