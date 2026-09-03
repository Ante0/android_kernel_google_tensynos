// NOLINTBEGIN
#ifndef __MODEM_FW_MAILBOX_H__
#define __MODEM_FW_MAILBOX_H__

#include <cstddef>
#include <cstdint>
#include "ncp_modem_data.h"
#include "noa_configs/noa_configs.h"
#include "noa_configs/noa_ipc.h"
#include "notifier/notifier.h"
#include "notifier/notifier_mailbox.h"

extern void modem_fw_notify_nep_mailbox();
extern void modem_fw_free_nep_mailbox();
extern int modem_fw_nep_interrupt_handler(int32_t id, void *data);
extern int modem_fw_register_nep_mailbox(struct noa_md_fw *md_fw);

#endif /* __MODEM_FW_MAILBOX_H__ */
// NOLINTEND
