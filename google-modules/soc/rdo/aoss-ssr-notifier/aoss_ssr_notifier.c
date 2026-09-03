// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google AOSS SSR Notifier
 *
 * Copyright (c) 2025 Google LLC
 */

#include <aoss-ssr-notifier/aoss_ssr_notifier.h>

#include <linux/module.h>

#if IS_ENABLED(CONFIG_AOSS_SSR_NOTIFIER)

static BLOCKING_NOTIFIER_HEAD(aoss_ssr_notifier);

int aoss_ssr_add_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&aoss_ssr_notifier, nb);
}
EXPORT_SYMBOL_GPL(aoss_ssr_add_notifier);

int aoss_ssr_remove_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&aoss_ssr_notifier, nb);
}
EXPORT_SYMBOL_GPL(aoss_ssr_remove_notifier);

void aoss_ssr_notify(enum aoss_ssr_notifier_event_t event)
{
	blocking_notifier_call_chain(&aoss_ssr_notifier, event, NULL);
}
EXPORT_SYMBOL_GPL(aoss_ssr_notify);

#endif

MODULE_AUTHOR("Alex Iacobucci <alexiacobucci@google.com>");
MODULE_DESCRIPTION("Google AOSS SSR Notifier");
MODULE_LICENSE("GPL v2");
