/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __GOOGLE_RADIO_BRIDGE_H__
#define __GOOGLE_RADIO_BRIDGE_H__

struct device;
struct notifier_block;

/**
 * enum radio_br_event_t - Events that clients can register for
 *
 * @RADIO_BR_MODEM_AUDIO_PATH_PREPARE: Prepare for the audio path between
 *	the modem and the AP by turning on resources.
 * @RADIO_BR_MODEM_AUDIO_PATH_ENABLE: Start transferring audio with the modem.
 * @RADIO_BR_MODEM_AUDIO_PATH_DISABLE: Stop transferring audio with the modem.
 * @RADIO_BR_EVT_MODEM_AUDIO_PATH_UNPREPARE: The modem audio path no longer
 *	needs resources to be powered on.
 */
enum radio_br_event_t {
	RADIO_BR_MODEM_AUDIO_PATH_PREPARE,
	RADIO_BR_MODEM_AUDIO_PATH_ENABLE,
	RADIO_BR_MODEM_AUDIO_PATH_DISABLE,
	RADIO_BR_MODEM_AUDIO_PATH_UNPREPARE,
};

#if IS_ENABLED(CONFIG_GOOGLE_RADIO_BRIDGE)

int radio_br_notifier_register(struct notifier_block *nb);
int devm_radio_br_notifier_register(struct device *dev, struct notifier_block *nb);
int radio_br_notifier_unregister(struct notifier_block *nb);
void radio_br_modem_exception(void);

#else

static inline int radio_br_notifier_register(struct notifier_block *nb)
{
	return 0;
}
static inline int devm_radio_br_notifier_register(struct device *dev, struct notifier_block * nb)
{
	return 0;
}
static inline int radio_br_notifier_unregister(struct notifier_block *nb)
{
	return 0;
}
static inline void radio_br_modem_exception(void)
{
	return;
}

#endif /* #if IS_ENABLED(CONFIG_GOOGLE_RADIO_BRIDGE) */

#endif /* __GOOGLE_RADIO_BRIDGE_H__ */
