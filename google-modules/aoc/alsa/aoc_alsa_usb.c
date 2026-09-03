// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google Whitechapel AoC ALSA  Driver on PCM
 *
 * Copyright (c) 2023 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sound/core.h>
#include <linux/notifier.h>
#include <linux/usb.h>
#include <linux/usb/xhci-sideband.h>

#include "aoc_alsa.h"
#include "card.h"
#include "usbaudio.h"

struct endpoint_info {
	unsigned int data_ep_pipe;
	unsigned int sync_ep_pipe;
	unsigned int pcm_card_num;
	unsigned int pcm_device;
	unsigned int reference_count;
};

struct uaudio_dev {
	int card_num;
	struct snd_usb_audio *chip;
	struct xhci_sideband *sb;
	struct usb_device *udev;
	struct endpoint_info ep_info[2];
};

struct aoc_audio_dev {
	struct aoc_chip *achip;
	struct notifier_block nb;
};

static struct uaudio_dev uadev[SNDRV_CARDS];
static struct aoc_audio_dev *aocdev;
static BLOCKING_NOTIFIER_HEAD(aoc_alsa_usb_notifier_list);

static bool snd_usb_is_implicit_feedback(struct snd_usb_endpoint *ep)
{
	return  ep->implicit_fb_sync && usb_pipeout(ep->pipe);
}

static struct snd_usb_substream *find_substream(unsigned int card_num,
	unsigned int device, unsigned int direction)
{
	struct snd_usb_stream *as;
	struct snd_usb_substream *subs = NULL;
	struct snd_usb_audio *chip;

	chip = uadev[card_num].chip;

	if (!chip || atomic_read(&chip->shutdown))
		goto done;

	if (device >= chip->pcm_devs)
		goto done;

	if (direction > SNDRV_PCM_STREAM_CAPTURE)
		goto done;

	list_for_each_entry(as, &chip->pcm_list, list) {
		if (as->pcm_index == device) {
			subs = &as->substream[direction];
			goto done;
		}
	}

done:
	return subs;
}

static int aoc_usb_notifier_call(struct notifier_block *notifier,
	unsigned long val, void *data)
{
	struct aoc_audio_dev *adev =
			container_of(notifier, struct aoc_audio_dev, nb);

	switch (val) {
	case USB_CONNECT:
		aoc_set_usb_offload_state(adev->achip, adev->achip->offload_state);
		break;
	case USB_DISCONNECT:
		aoc_set_usb_offload_state(adev->achip, USB_OFFLOAD_STATE_DISABLED);
		break;
	case USB_SUSPEND:
		break;
	case USB_RESUME:
		break;
	default:
		pr_err("%s status not supported %lu", __func__, val);
		break;
	}

	return NOTIFY_OK;
}

static int aoc_usb_sideband_event(struct usb_interface *intf, struct xhci_sideband_event *evt)
{
	switch (evt->type) {
	case XHCI_SIDEBAND_XFER_RING_FREE:
		pr_info("%s sideband ring free", __func__);
		break;
	default:
		pr_err("%s invalid event type %d", __func__, evt->type);
		break;
	}
	return 0;
}

void usb_audio_offload_connect(struct snd_usb_audio *chip)
{
	int card_num;
	struct xhci_sideband *sb;
	struct usb_interface *intf;

	if (!chip)
		return;

	card_num = chip->card->number;
	if (card_num >= SNDRV_CARDS)
		return;

	if (chip->num_interfaces < 1) {
		pr_err("chip->num_interfaces = %d is invalid\n", chip->num_interfaces);
		return;
	}

	intf = chip->intf[chip->num_interfaces - 1];

	if (!uadev[card_num].chip) {
		sb = xhci_sideband_register(intf, XHCI_SIDEBAND_VENDOR, aoc_usb_sideband_event);
		if (!sb) {
			pr_err("xhci_sideband_register fail");
			return;
		}
	} else {
		sb = uadev[chip->card->number].sb;
	}

	pr_info("%s card%d connected", __func__, card_num);
	mutex_lock(&chip->mutex);
	uadev[card_num].chip = chip;
	uadev[card_num].card_num = card_num;
	uadev[card_num].sb = sb;
	chip->quirk_flags &= ~QUIRK_FLAG_GENERIC_IMPLICIT_FB;
	chip->quirk_flags |= QUIRK_FLAG_SKIP_IMPLICIT_FB;
	mutex_unlock(&chip->mutex);

	blocking_notifier_call_chain(&aoc_alsa_usb_notifier_list,
				     USB_CONNECT, NULL);
}

void usb_audio_offload_disconnect(struct snd_usb_audio *chip)
{
	struct uaudio_dev *dev;
	struct usb_device *udev;
	struct endpoint_info *info = NULL;
	int card_num;
	int i;

	if (!chip)
		return;

	card_num = chip->card->number;
	if (card_num >= SNDRV_CARDS)
		return;

	pr_info("%s card%d disconnected", __func__, card_num);
	blocking_notifier_call_chain(&aoc_alsa_usb_notifier_list,
				     USB_DISCONNECT, NULL);
	mutex_lock(&chip->mutex);
	dev = &uadev[card_num];
	udev = chip->dev;
	dev->udev = NULL;
	dev->card_num = 0;

	if (chip->num_interfaces == 1) {
		/* unregister sideband, will remove interrupter and endpoint*/
		if (dev->sb) {
			xhci_sideband_unregister(dev->sb);
			if (udev && (dev->ep_info[SNDRV_PCM_STREAM_PLAYBACK].reference_count > 0 ||
				     dev->ep_info[SNDRV_PCM_STREAM_CAPTURE].reference_count > 0))
				usb_offload_put(udev);
			pr_info("%s xhci sideband unregister", __func__);
		}
		dev->chip = NULL;
		dev->sb = NULL;
		for (i = 0; i <= SNDRV_PCM_STREAM_CAPTURE; i++) {
			info = &dev->ep_info[i];
			info->data_ep_pipe = 0;
			info->sync_ep_pipe = 0;
			info->pcm_card_num = 0;
			info->pcm_device = 0;
			info->reference_count = 0;
		}
	}
	mutex_unlock(&chip->mutex);
}

int usb_get_endpoint_dir(int aoc_dir)
{
	if (aoc_dir == SNDRV_PCM_STREAM_PLAYBACK)
		return EP_OUT;
	else if (aoc_dir == SNDRV_PCM_STREAM_CAPTURE)
		return EP_IN;

	return EP_NONE;
}

int aoc_setup_event_ring(struct aoc_chip *achip, struct usb_device *udev, int intr_num, bool enable)
{
	int ret = 0;

	if (enable) {
		ret = usb_offload_get(udev);
		if (ret < 0) {
			pr_err("usb_offload_get return with ret = %d", ret);
			return ret;
		}

		if (achip->usb_skip_event_ring)
			return 0;

		ret = xhci_sideband_create_interrupter(uadev[achip->usb_card].sb, 1,
						       false, 0, intr_num);
		if (ret < 0) {
			usb_offload_put(udev);
			pr_err("%s IRQ create fail with ret %d", __func__, ret);
		} else {
			pr_info("%s IRQ create with ret %d", __func__, ret);
		}
	} else {
		if (!achip->usb_skip_event_ring)
			xhci_sideband_remove_interrupter(uadev[achip->usb_card].sb);

		ret = usb_offload_put(udev);
		if (ret < 0)
			pr_err("usb_offload_put return with ret = %d", ret);

		pr_info("%s IRQ remove", __func__);
	}
	return ret;
}

int aoc_setup_transfer_ring(struct aoc_chip *achip, int card_num, int aoc_dir,
	struct usb_host_endpoint *ep)
{
	struct xhci_virt_ep *virt_ep;
	struct xhci_ring *ep_ring;
	unsigned int ep_index;
	u16 dir;

	ep_index = xhci_get_endpoint_index(&ep->desc);
	virt_ep = uadev[card_num].sb->eps[ep_index];
	dir = usb_get_endpoint_dir(aoc_dir);

	if (virt_ep->new_ring) {
		pr_info("%s deliver transfer ring from new_ring", __func__);
		ep_ring = virt_ep->new_ring;
	} else if (virt_ep->ring) {
		pr_info("%s deliver transfer ring from ring", __func__);
		ep_ring = virt_ep->ring;
	} else {
		pr_err("%s transfer ring not found!", __func__);
		return -EINVAL;
	}

	aoc_set_isoc_tr_info(achip, 0, dir, ep_ring);

	return 0;
}

static struct snd_usb_platform_ops ops = {
	.connect_cb = usb_audio_offload_connect,
	.disconnect_cb = usb_audio_offload_disconnect,
};

int aoc_usb_setup_config(struct aoc_chip *achip, unsigned int reference_count,
			 unsigned int direction)
{
	struct usb_host_endpoint *ep = NULL;
	struct usb_host_endpoint *fb_ep = NULL;
	struct snd_usb_substream *subs = NULL;
	struct snd_usb_audio *chip = NULL;
	struct endpoint_info *info = NULL;
	struct endpoint_info *playback_info = NULL;
	struct endpoint_info *capture_info = NULL;
	unsigned int card_num;
	unsigned int device;
	unsigned int old_ref_count;
	bool implicit_fb = false;
	int ret = 0;

	if (!achip)
		return -ENODEV;

	card_num = achip->usb_card;
	device = achip->usb_device;
	if (card_num < SNDRV_CARDS)
		chip = uadev[card_num].chip;

	if (!chip) {
		pr_err("%s no device connected (card %u device %u, direction %u)", __func__,
		       card_num, device, direction);
		return -ENODEV;
	}

	ret = snd_usb_lock_shutdown(chip);
	if (ret < 0) {
		pr_err("%s: setup skipped. chip is shutting down (shutdown=%d, usage_count=%d)",
		       __func__, atomic_read(&chip->shutdown), atomic_read(&chip->usage_count));
		return -ENODEV;
	}

	pr_debug("%s card %u device %u, direction %u ref_count %d", __func__,
		 card_num, device, direction, reference_count);
	if (direction > SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("%s direction is unexpected %d", __func__, direction);
		ret = -EINVAL;
		goto unlock_shutdown;
	}

	mutex_lock(&chip->mutex);
	if (uadev[card_num].chip != chip || !uadev[card_num].sb) {
		ret = -ENODEV;
		goto unlock;
	}

	playback_info = &uadev[card_num].ep_info[SNDRV_PCM_STREAM_PLAYBACK];
	capture_info = &uadev[card_num].ep_info[SNDRV_PCM_STREAM_CAPTURE];

	info = &uadev[card_num].ep_info[direction];
	old_ref_count = info->reference_count;
	info->reference_count = reference_count;
	if (info->reference_count != 1) {
		pr_debug("%s skip", __func__);
		ret = 0;
		goto unlock;
	}

	subs = find_substream(card_num, device, direction);
	if (!subs || !subs->dev) {
		pr_err("%s subsream dev not found", __func__);
		ret = -ENODEV;
		goto err_ref_count;
	}

	if (!subs->data_endpoint) {
		pr_err("%s data or sync_endpoint is null", __func__);
		ret = -ENODEV;
		goto err_ref_count;
	}

	if (playback_info->reference_count + capture_info->reference_count == 1) {
		ret = aoc_setup_event_ring(achip, subs->dev, 1, true);
		if (ret < 0)
			goto err_ref_count;
	}

	aoc_set_usb_offload_state(achip, achip->offload_state);

	ep = usb_pipe_endpoint(subs->dev, subs->data_endpoint->pipe);
	if (!ep) {
		pr_err("%s data ep # %d context is null",
		       __func__, subs->data_endpoint->ep_num);
		ret = -ENODEV;
		goto err_data_ep;
	}

	ret = xhci_sideband_add_endpoint(uadev[card_num].sb, ep);
	if (ret < 0 && ret != -EBUSY) {
		pr_info("%s sideband add data ep with ret %d", __func__, ret);
		goto err_data_ep;
	}
	ret = aoc_setup_transfer_ring(achip, card_num, direction, ep);
	if (ret < 0)
		goto err_data_ring;

	implicit_fb = snd_usb_is_implicit_feedback(subs->data_endpoint);
	if (subs->sync_endpoint && !implicit_fb) {
		fb_ep = usb_pipe_endpoint(subs->dev, subs->sync_endpoint->pipe);
		if (!fb_ep) {
			pr_info("%s sync ep # %d context is null",
			        __func__, subs->sync_endpoint->ep_num);
			/* TODO: need refine this after enable implicit feedback b/289153968*/
			ret = -ENODEV;
			goto err_sync_ep;
		}

		ret = xhci_sideband_add_endpoint(uadev[card_num].sb, fb_ep);
		if (ret < 0 && ret != -EBUSY) {
			pr_info("%s sideband add sync ep with ret %d", __func__, ret);
			goto err_sync_ep;
		}

		ret = aoc_setup_transfer_ring(achip, card_num,
					SNDRV_PCM_STREAM_CAPTURE, fb_ep);
		if (ret < 0)
			goto err_sync_ring;

		aoc_set_usb_feedback_endpoint(achip, subs->dev, fb_ep);
	}

	uadev[card_num].udev = subs->dev;
	info->data_ep_pipe = subs->data_endpoint->pipe;
	info->sync_ep_pipe = subs->sync_endpoint ? subs->sync_endpoint->pipe : 0;
	info->pcm_card_num = card_num;
	info->pcm_device = device;
	mutex_unlock(&chip->mutex);
	snd_usb_unlock_shutdown(chip);
	return 0;

err_sync_ring:
	xhci_sideband_remove_endpoint(uadev[card_num].sb, fb_ep);
err_sync_ep:
err_data_ring:
	xhci_sideband_remove_endpoint(uadev[card_num].sb, ep);
err_data_ep:
	if (playback_info->reference_count + capture_info->reference_count == 1) {
		aoc_set_usb_offload_state(achip, USB_OFFLOAD_STATE_DISABLED);
		aoc_setup_event_ring(achip, subs->dev, 1, false);
	}
err_ref_count:
	info->reference_count = old_ref_count;
unlock:
	mutex_unlock(&chip->mutex);
unlock_shutdown:
	snd_usb_unlock_shutdown(chip);
	return ret;
}

int aoc_usb_cleanup_config(struct aoc_chip *achip, unsigned int reference_count,
			   unsigned int direction)
{
	struct snd_usb_audio *chip = NULL;
	struct endpoint_info *info = NULL;
	struct endpoint_info *playback_info = NULL;
	struct endpoint_info *capture_info = NULL;
	struct usb_host_endpoint *ep = NULL;
	struct usb_host_endpoint *fb_ep = NULL;
	unsigned int card_num;
	unsigned int device;
	int ret = 0;

	if (!achip)
		return -ENODEV;

	card_num = achip->usb_card;
	device = achip->usb_device;
	if (card_num < SNDRV_CARDS)
		chip = uadev[card_num].chip;

	if (!chip) {
		pr_info("%s skip due to disconnect", __func__);
		return 0;
	}

	ret = snd_usb_lock_shutdown(chip);
	if (ret < 0) {
		pr_info("%s: cleanup skipped. chip is shutting down (shutdown=%d, usage_count=%d)",
			__func__, atomic_read(&chip->shutdown), atomic_read(&chip->usage_count));
		return 0;
	}

	pr_debug("%s card %u device %u, direction %u ref_count %u", __func__,
		card_num, device, direction, reference_count);

	mutex_lock(&chip->mutex);
	info = &uadev[card_num].ep_info[direction];
	info->reference_count = reference_count;
	if (info->reference_count != 0) {
		pr_debug("%s skip", __func__);
		goto unlock_mutex;
	}

	if (!uadev[card_num].udev || !uadev[card_num].sb) {
		pr_info("%s usb device or sideband already removed", __func__);
		goto unlock_mutex;
	}

	if (info->data_ep_pipe) {
		ep = usb_pipe_endpoint(uadev[card_num].udev, info->data_ep_pipe);
		if (ep) {
			xhci_sideband_remove_endpoint(uadev[card_num].sb, ep);
			info->data_ep_pipe = 0;
			pr_info("%s card %u dir %d data endpoint remove", __func__,
				card_num, direction);
		} else {
			pr_info("%s data endpoint not found no need to remove",
				__func__);
		}
	}
	if (info->sync_ep_pipe) {
		fb_ep = usb_pipe_endpoint(uadev[card_num].udev, info->sync_ep_pipe);
		if (fb_ep) {
			xhci_sideband_remove_endpoint(uadev[card_num].sb, fb_ep);
			info->sync_ep_pipe = 0;
			pr_info("%s card %u dir %d sync endpoint remove", __func__,
				card_num, direction);
		} else {
			pr_info("%s sync endpoint not found no need to remove",
				__func__);
		}
	}

	playback_info = &uadev[card_num].ep_info[SNDRV_PCM_STREAM_PLAYBACK];
	capture_info = &uadev[card_num].ep_info[SNDRV_PCM_STREAM_CAPTURE];
	if (playback_info->reference_count == 0 && capture_info->reference_count == 0)
		aoc_setup_event_ring(achip, uadev[card_num].udev, 1, false);

unlock_mutex:
	mutex_unlock(&chip->mutex);
	snd_usb_unlock_shutdown(chip);
	return 0;
}

static void aoc_usb_parse_dts(struct device *dev, struct aoc_chip *achip)
{
	int use_legacy_offload = 0;

	use_legacy_offload = device_property_present(dev, "aoc,usb-use-legacy-offload");
	if (use_legacy_offload) {
		achip->offload_state = USB_OFFLOAD_STATE_LEGACY;
		pr_info("%s use USB legacy offload", __func__);
	} else {
		achip->offload_state = USB_OFFLOAD_STATE_SIDEBAND;
	}

	achip->usb_skip_event_ring = device_property_present(dev, "aoc,usb-disable-event-ring");
	if (achip->usb_skip_event_ring)
		pr_info("%s AoC handle interrupt skip event ring", __func__);
}

static int aoc_usb_component_probe(struct snd_soc_component *component)
{
	int ret;
	struct aoc_chip *achip =
			(struct aoc_chip *)snd_soc_card_get_drvdata(component->card);

	aocdev = kzalloc(sizeof(*aocdev), GFP_KERNEL);
	if (!aocdev)
		return -ENOMEM;

	aocdev->nb.notifier_call = aoc_usb_notifier_call;
	aocdev->achip = achip;

	aoc_usb_parse_dts(component->dev, achip);

	ret = blocking_notifier_chain_register(&aoc_alsa_usb_notifier_list,
					       &aocdev->nb);
	if (ret) {
		pr_info("%s Failed to register aoc usb notifier %d", __func__, ret);
		kfree(aocdev);
		return ret;
	}

	ret = snd_usb_register_platform_ops(&ops);
	if (ret < 0)
		pr_err("%s snd_usb_register_platform_ops fail, ret %d", __func__, ret);
	else
		pr_info("%s registered usb callback", __func__);

	return 0;
}

static const struct snd_soc_component_driver aoc_usb_component = {
	.name = "AoC USB",
	.probe = aoc_usb_component_probe,
};

static int aoc_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	ret = devm_snd_soc_register_component(dev, &aoc_usb_component, NULL, 0);
	if (ret)
		pr_err("%s fail to reigster aoc usb component %d", __func__, ret);

	return ret;
}

static const struct of_device_id aoc_usb_of_match[] = {
	{
		.compatible = "google-aoc-snd-usb",
	},
	{},
};
MODULE_DEVICE_TABLE(of, aoc_usb_of_match);

static struct platform_driver aoc_usb_drv = {
	.driver = {
			.name = "google-aoc-snd-usb",
			.of_match_table = aoc_usb_of_match,
		},
	.probe = aoc_usb_probe,
};

int aoc_usb_init(void)
{
	int ret = 0;

	pr_debug("%s", __func__);
	ret = platform_driver_register(&aoc_usb_drv);
	if (ret)
		pr_err("ERR:%d in registering aoc usb drv", ret);

	return ret;
}

void aoc_usb_exit(void)
{
	pr_info("%s unregister usb callback", __func__);
	snd_usb_unregister_platform_ops();
	blocking_notifier_chain_unregister(&aoc_alsa_usb_notifier_list,
					   &aocdev->nb);
	kfree(aocdev);
}

