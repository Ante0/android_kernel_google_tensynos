// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google radio bridge interface
 *
 * The radio bridge interface provides a vendor independent way
 * to communicate and notify events across different subsystems
 * related to the radio (for example: userspace rild, aoc, pcie etc.)
 *
 * This also ensures that the events from userspace and kernel are
 * synchronized. For example, one of the primary use-case here is to
 * prevent the speech on command to the modem from rild until the
 * audio path between the modem and AoC is setup.
 *
 * Furthermore, this module is also used to co-ordinate between the
 * AoC and the modem to avoid PCIe access in case of a subsystem reset
 * or crash.
 *
 * Copyright 2025 Google LLC
 */

#define pr_fmt(fmt) "radio_bridge: " fmt

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/sysfs.h>
#include <aoss-ssr-notifier/aoss_ssr_notifier.h>

#include "radio-bridge.h"

enum audio_path_values {
	RADIO_BR_AUDIO_OFF,
	RADIO_BR_AUDIO_ON,
};

/*
 * The desired_audio_path_status indicates whether we are in a VoLTE call or not. This
 * is the value that RIL expects. This is also what we export as a sysfs to RIL.
 *
 * The current_audio_path_status is the current state of the audio channel between
 * the AoC and the Modem. This can be different from the desired state during an AoC
 * reset, for example. When the AoC recovers, and the audio path gets established again,
 * the current state will match the desired state. All the recovery happens under the
 * hood, without having to bother the RIL.
 */
static int current_audio_path_status;
static int desired_audio_path_status;
static DEFINE_MUTEX(audio_path_mutex);

static struct kobject *radio_bridge_kobj;
static struct notifier_block aoss_ssr_nb;
static BLOCKING_NOTIFIER_HEAD(radio_br_notifier);

int radio_br_notifier_register(struct notifier_block *nb)
{
	if (!nb)
		return -EINVAL;

	return blocking_notifier_chain_register(&radio_br_notifier, nb);
}
EXPORT_SYMBOL_GPL(radio_br_notifier_register);

int radio_br_notifier_unregister(struct notifier_block *nb)
{
	if (!nb)
		return -EINVAL;

	return blocking_notifier_chain_unregister(&radio_br_notifier, nb);
}
EXPORT_SYMBOL_GPL(radio_br_notifier_unregister);

static void radio_br_notifier_unregister_void(void *ptr)
{
	radio_br_notifier_unregister(ptr);
}

int devm_radio_br_notifier_register(struct device *dev, struct notifier_block *nb)
{
	int ret;

	ret = radio_br_notifier_register(nb);
	if (ret)
		return ret;
	return devm_add_action_or_reset(dev, radio_br_notifier_unregister_void, nb);
}
EXPORT_SYMBOL_GPL(devm_radio_br_notifier_register);

static ssize_t audio_path_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	guard(mutex)(&audio_path_mutex);
	return sysfs_emit(buf, "%s\n", desired_audio_path_status ? "ON" : "OFF");
}

static int audio_call_chain(enum radio_br_event_t event)
{
	bool responded = false;
	int ret;

	ret = blocking_notifier_call_chain(&radio_br_notifier, event, &responded);

	/* Listeners can respond with errors */
	ret = notifier_to_errno(ret);
	if (ret)
		return ret;

	/* If nobody signaled that they took action, it's also an error */
	if (!responded)
		return -ENODEV;

	return 0;
}

static int aoss_ssr_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	int ret;

	if ((event != AOSS_SSR_AMBSS_DOWN) && (event != AOSS_SSR_ONLINE))
		return NOTIFY_DONE;

	guard(mutex)(&audio_path_mutex);
	pr_info("aoss_ssr_notifier (event: %lu, audio_path: %d)!\n",
			event, desired_audio_path_status);

	if (!desired_audio_path_status)
		return NOTIFY_OK;

	switch (event) {
	case AOSS_SSR_AMBSS_DOWN:
		current_audio_path_status = false;
		break;
	case AOSS_SSR_ONLINE:
		/*
		 * Even if the cmd fails, we decided to continue without a fatal
		 * error in order to avoid dropping the call. The end result might
		 * be a VoLTE call without audio. But this might as well get fixed
		 * on the next VoLTE -> VoWiFi transition and/or back.
		 */
		ret = audio_call_chain(RADIO_BR_MODEM_AUDIO_PATH_ENABLE);
		if (ret)
			pr_warn("Failed to enable audio after AoC crash (rc: %d)\n", ret);
		else
			current_audio_path_status = true;
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int audio_path_on(void)
{
	int ret;

	/*
	 * Expected to be handled by the modem driver, which will ensure the
	 * modem (and PCIe link) stays on.
	 */
	ret = audio_call_chain(RADIO_BR_MODEM_AUDIO_PATH_PREPARE);
	if (ret) {
		pr_err("Failed to prepare audio path: %d\n", ret);
		return ret;
	}

	/*
	 * Expected to be handled by the AOC audio driver, which will start
	 * accessing the PCIe link to transfer data.
	 */
	ret = audio_call_chain(RADIO_BR_MODEM_AUDIO_PATH_ENABLE);
	if (ret) {
		int ret2;

		pr_err("Failed to enable audio path: %d\n", ret);

		/*
		 * Attempt to unprepare; not much we can do about failure
		 * except print a warning, though.
		 */
		ret2 = audio_call_chain(RADIO_BR_MODEM_AUDIO_PATH_UNPREPARE);
		if (ret2)
			pr_warn("Failed to unprepare audio path: %d\n", ret2);

		return ret;
	}

	desired_audio_path_status = true;
	current_audio_path_status = true;
	pr_info("audio_path is ON!\n");

	return 0;
}

static int audio_path_off(void)
{
	int ret;

	/*
	 * Expected to be handled by the AOC audio driver, which will stop
	 * accessing the PCIe link.
	 */
	ret = audio_call_chain(RADIO_BR_MODEM_AUDIO_PATH_DISABLE);
	if (ret) {
		pr_err("Failed to disable audio path: %d\n", ret);
		return ret;
	}

	/*
	 * Expected to be handled by the modem driver, which will release the
	 * request for the modem (and PCIe link) to stay on.
	 *
	 * If this fails, we will print a warning but still consider the
	 * function a success, which is better than trying to turn the audio
	 * path back on.
	 */
	ret = audio_call_chain(RADIO_BR_MODEM_AUDIO_PATH_UNPREPARE);
	if (ret)
		pr_warn("Failed to unprepare audio path: %d\n", ret);

	desired_audio_path_status = false;
	current_audio_path_status = false;
	pr_info("audio_path is OFF!\n");

	return 0;
}

/**
 * audio_path_store() - Handle writes to the audio_path sysfs file.
 * @class: The class.
 * @attr: The attr.
 * @buf: The buf to read/write from. Parsed with kstrtobool().
 * @count: Number of bytes.
 *
 * Setting this to "ON":
 * 1. Turns on power to the modem audio path.
 * 2. Starts the modem audio path running.
 *
 * Setting this to "OFF":
 * 1. Stops the modem audio path.
 * 2. Releases power from the modem audio path.
 *
 * This calls is blocking--if it returns with no error then the write was
 * successful.
 *
 * Returns: count upon no error or a negative error code.
 */
static ssize_t audio_path_store(struct kobject *class,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	bool new_state;

	ret = kstrtobool(buf, &new_state);
	if (ret) {
		pr_err("Invalid input\n");
		return ret;
	}

	pr_info("Request to change audio_path to %s!", new_state ? "ON" : "OFF");
	guard(mutex)(&audio_path_mutex);

	/*
	 * The desired state can only be changed by RIL and a modem exception.
	 * Add this check here to avoid taking action in case the state already
	 * reflects the right one. For example, if RIL tries to tear down a
	 * VoLTE call, but the state was already off due to a modem exception.
	 */
	if (new_state == desired_audio_path_status)
		return count;

	if (new_state)
		ret = audio_path_on();
	else
		ret = audio_path_off();

	sysfs_notify(radio_bridge_kobj, NULL, "audio_path");

	return ret ? ret : count;
}

void radio_br_modem_exception(void)
{
	guard(mutex)(&audio_path_mutex);
	pr_info("Received modem exception (audio_path: %d)!\n", desired_audio_path_status);

	if (!desired_audio_path_status)
		return;

	audio_path_off();

	/*
	 * Force the status off. Normally audio_path_off() will not update the status
	 * if the AOC failed to respond (ex: an on-going AoC reset). However, in the
	 * case of a modem crash there's not much else we can do to recover the call.
	 * In short, this fixes issues that come up if a modem crash and AOC crash
	 * happen at the same time.
	 */
	desired_audio_path_status = false;
	current_audio_path_status = false;
}
EXPORT_SYMBOL_GPL(radio_br_modem_exception);

static struct kobj_attribute audio_path_attribute = __ATTR(audio_path, 0664, audio_path_show, audio_path_store);

static int __init radio_bridge_init(void)
{
	int ret;

	radio_bridge_kobj = kobject_create_and_add("radio_bridge", kernel_kobj);
	if (!radio_bridge_kobj) {
		ret = -ENOMEM;
		pr_err("kobject_create_and_add() failed (rc: %d)\n", ret);
		goto exit;
	}

	ret = sysfs_create_file(radio_bridge_kobj, &audio_path_attribute.attr);
	if (ret) {
		pr_err("sysfs_create_file() failed (rc: %d)\n", ret);
		goto kobj_put;
	}

	/*
	 * Get notified about SubSystemReset (SSR) on AOC. The AOC kernel driver request
	 * that we re-notify it about the state of the audio path after it resets.
	 * If we don't do this and we have an active call, call audio will not be
	 * restored after AOC restarts and eventually the call will drop.
	 */
	aoss_ssr_nb.notifier_call = aoss_ssr_notifier;
	ret = aoss_ssr_add_notifier(&aoss_ssr_nb);
	if (ret) {
		pr_err("Failed to add aoss notifier (rc: %d)\n", ret);
		goto sysfs_remove;
	}

	return 0;

sysfs_remove:
	sysfs_remove_file(radio_bridge_kobj, &audio_path_attribute.attr);
kobj_put:
	kobject_put(radio_bridge_kobj);
exit:
	return ret;
}

static void __exit radio_bridge_exit(void)
{
	aoss_ssr_remove_notifier(&aoss_ssr_nb);
	sysfs_remove_file(radio_bridge_kobj, &audio_path_attribute.attr);
	kobject_put(radio_bridge_kobj);
}

module_init(radio_bridge_init);
module_exit(radio_bridge_exit);

MODULE_AUTHOR("Mahesh Kallelil <kallelil@google.com>");
MODULE_DESCRIPTION("Google Radio Bridge interface");
MODULE_LICENSE("GPL v2");
