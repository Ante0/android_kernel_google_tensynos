// NOLINTBEGIN
#include "modem_fw_mailbox.h"

struct noa_md_sys {
	::noa::module::notifier::NotifierMailbox nep_mailbox_notifier;
};

static struct noa_md_sys *md_sys_get_instance()
{
	static struct noa_md_sys md_sys_ins;
	return &md_sys_ins;
}

void modem_fw_notify_nep_mailbox()
{
	struct noa_md_sys *md_sys_ins = md_sys_get_instance();
	md_sys_ins->nep_mailbox_notifier.Notify();
};

void modem_fw_free_nep_mailbox()
{
	struct noa_md_sys *md_sys_ins = md_sys_get_instance();
	md_sys_ins->nep_mailbox_notifier.UnregisterNotificationHandler();
}

int modem_fw_register_nep_mailbox(struct noa_md_fw *md_fw)
{
	int ret = 0;
	struct noa_md_sys *md_sys_ins = md_sys_get_instance();
	auto mailbox_id = ::noa::module::noa_configs::NoaConfigs::Instance().MailboxId(
		NOA_PORT_MODEM_FW);

	if (!mailbox_id.has_value()) {
		pr_err("%s(): cannot get mailbox id\n", __func__);
		return -EINVAL;
	}

	ret = md_sys_ins->nep_mailbox_notifier.Init(
		"modem",
		::noa::module::notifier::kSender | ::noa::module::notifier::kReceiver,
		*mailbox_id);

	if (ret) {
		pr_err("%s(): failed to init mailbox handler, err: %d", __func__, ret);
		return ret;
	}

	ret = md_sys_ins->nep_mailbox_notifier.RegisterNotificationHandler(
		[](void *ctx) -> int {
			struct noa_md_fw *md_fw = (struct noa_md_fw *)ctx;
			modem_fw_nep_interrupt_handler(0, md_fw);
			return 0;
		},
		(void *)md_fw);
	if (ret) {
		pr_err("%s(): failed to register handler in mailbox handler, err: %d",
		       __func__, ret);
		return ret;
	}
	return ret;
}
// NOLINTEND
