// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 * Google's USB data role switch driver
 */

#include <linux/extcon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <kunit/visibility.h>
#include <misc/gvotable.h>

#include <linux/usb/google-role-sw.h>

#define ROLE_SW_POOLING_MS 100

static inline bool is_valid_data_role(int role)
{
	return role == USB_ROLE_NONE || role == USB_ROLE_HOST || role == USB_ROLE_DEVICE;
}

/**
 * update_data_role - update usb data role based on collected votes.
 * @el: the election associated with this voting event.
 * @reason: the tag to identify the voter who casted the vote.
 * @value: the value the voter casted during this voting attempt.
 *
 * The callback which will be executed after each time a voter casts a
 * vote. Everytime a voter casts a vote, it will first determine the vote
 * result by a pre-defined truth-table, and then pass the result to
 * downstream device.
 *
 * Returns 0 on success, negative error otherwise.
 */
VISIBLE_IF_KUNIT int update_data_role(struct gvotable_election *el, const char *reason, void *value)
{
	struct google_role_sw *grole_sw = gvotable_get_data(el);
	int aoc_vote, tcpci_comb_vote, disable_usb_data_vote;
	int offload_vote;
	int ret = 0;
	enum usb_role desired_role;

	mutex_lock(&grole_sw->update_role_lock);

	aoc_vote = gvotable_get_int_vote(el, AOC_VOTER);
	tcpci_comb_vote = gvotable_get_int_vote(el, TCPCI_COMB_VOTER);
	disable_usb_data_vote = gvotable_get_int_vote(el, DISABLE_USB_DATA_VOTER);
	offload_vote = gvotable_get_int_vote(el, OFFLOAD_VOTER);

	if (disable_usb_data_vote == USB_DATA_DISABLED) {
		desired_role = USB_ROLE_NONE;
	} else {
		desired_role = is_valid_data_role(tcpci_comb_vote) ?
			       tcpci_comb_vote : USB_ROLE_NONE;
		if ((aoc_vote != USB_ROLE_HOST && desired_role == USB_ROLE_HOST) ||
		    (desired_role == USB_ROLE_HOST && offload_vote == USB_ROLE_NONE)) {
			desired_role = USB_ROLE_NONE;
		}
	}

	if (desired_role == grole_sw->curr_role) {
		dev_info(grole_sw->dev, "%s: role unchanged, skip downstream.\n", __func__);
		goto done;
	}

	switch (grole_sw->downstream) {
		case USB_ROLE_SWITCH:
			ret = usb_role_switch_set_role(grole_sw->role_sw, desired_role);
			if (ret) {
				dev_err(grole_sw->dev, "Failed to set role_sw (ret = %d)\n", ret);
				goto done;
			}
			break;
		case EXTCON_DEV:
			if (desired_role == USB_ROLE_NONE)
				ret = extcon_set_state_sync(grole_sw->extcon,
					grole_sw->curr_role == USB_ROLE_HOST ?
					EXTCON_USB_HOST : EXTCON_USB, 0);
			else
				ret = extcon_set_state_sync(grole_sw->extcon,
					desired_role == USB_ROLE_HOST ?
					EXTCON_USB_HOST : EXTCON_USB, 1);

			if (ret) {
				dev_err(grole_sw->dev, "Failed to set extcon (ret = %d)\n", ret);
				goto done;
			}
			break;
		default:
			dev_err(grole_sw->dev, "Missing downstream device\n");
			ret = -EAGAIN;
			goto done;
	}

	if (grole_sw->eusb_role_sw) {
		ret = usb_role_switch_set_role(grole_sw->eusb_role_sw, desired_role);
		if (ret) {
			dev_err(grole_sw->dev, "Failed to update eUSB (ret = %d)\n", ret);
			goto done;
		}
	}

	grole_sw->curr_role = desired_role;
done:
	dev_info(grole_sw->dev,
		"[votes] %s: %s, %s: %s, %s: %s [result] %s [downstream] %s (ret = %d)\n",
		TCPCI_COMB_VOTER, usb_role_string(tcpci_comb_vote),
		AOC_VOTER, usb_role_string(aoc_vote),
		OFFLOAD_VOTER, usb_role_string(offload_vote),
		usb_role_string(desired_role),
		disable_usb_data_vote == USB_DATA_DISABLED ?
		"disabled" : downstream_sws[grole_sw->downstream], ret);

	mutex_unlock(&grole_sw->update_role_lock);

	return ret;
}
EXPORT_SYMBOL_IF_KUNIT(update_data_role);

static void init_role_sw(struct work_struct *ws)
{
	u32 sw_handle;
	struct google_role_sw *grole_sw =
				container_of(ws, struct google_role_sw, init_role_sw_work.work);

	of_property_read_u32(dev_of_node(grole_sw->dev), "role-sw-dev", &sw_handle);
	grole_sw->role_sw = usb_role_switch_find_by_fwnode(
				of_fwnode_handle(of_find_node_by_phandle(sw_handle)));

	if (grole_sw->role_sw) {
		dev_info(grole_sw->dev, "downstream rolw_sw found, update role\n");
		grole_sw->downstream = USB_ROLE_SWITCH;
		gvotable_run_election(grole_sw->usb_data_role_votable, true);
	} else {
		dev_info(grole_sw->dev, "downstream rolw_sw doesn't exist, try again later\n");
		mod_delayed_work(system_wq, &grole_sw->init_role_sw_work,
				 msecs_to_jiffies(ROLE_SW_POOLING_MS));
	}
}

static ssize_t disable_usb_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_role_sw *grole_sw = dev_get_drvdata(dev);
	int vote = gvotable_get_int_vote(grole_sw->usb_data_role_votable, DISABLE_USB_DATA_VOTER);

	return sysfs_emit(buf, "%u\n", vote == USB_DATA_DISABLED ? 1 : 0);
}

static ssize_t disable_usb_data_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct google_role_sw *grole_sw = dev_get_drvdata(dev);
	bool enable;
	int disable_usb_data_vote, ret;

	if (kstrtobool(buf, &enable) < 0)
		return -EINVAL;
	disable_usb_data_vote = gvotable_get_int_vote(grole_sw->usb_data_role_votable,
						      DISABLE_USB_DATA_VOTER);

	if (disable_usb_data_vote != enable) {
		dev_info(dev, "DISABLE_USB_DATA set to %d", enable);
		ret = gvotable_cast_vote(grole_sw->usb_data_role_votable, DISABLE_USB_DATA_VOTER,
			(void *)(long)(enable), 1);
		if (ret < 0) {
			dev_err(dev, "%s: update_data_role failed (ret = %d)\n", __func__, ret);
			return ret;
		}
	}

	return count;
}
static DEVICE_ATTR_RW(disable_usb_data);

static struct attribute *google_role_sw_device_attrs[] = {
	&dev_attr_disable_usb_data.attr,
	NULL
};
ATTRIBUTE_GROUPS(google_role_sw_device);

static int google_role_sw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct google_role_sw *grole_sw;
	u32 sw_handle;
	int ret;

	if (!node) {
		dev_err(dev, "no device node, failed to add data-role election\n");
		return -ENODEV;
	}

	grole_sw = devm_kzalloc(dev, sizeof(*grole_sw), GFP_KERNEL);
	if (!grole_sw)
		return -ENOMEM;

	if (!of_property_read_u32(dev_of_node(dev), "eusb-role-sw-dev", &sw_handle)) {
		grole_sw->eusb_role_sw = usb_role_switch_find_by_fwnode(
					 of_fwnode_handle(of_find_node_by_phandle(sw_handle)));
		if (IS_ERR_OR_NULL(grole_sw->eusb_role_sw)) {
			dev_err(dev, "probe deferred due to eusb-role-sw device is not ready");
			return -EPROBE_DEFER;
		}
	}

	grole_sw->dev = dev;
	grole_sw->downstream = NONE;
	grole_sw->curr_role = -EINVAL;
	mutex_init(&grole_sw->update_role_lock);
	INIT_DEFERRABLE_WORK(&grole_sw->init_role_sw_work, init_role_sw);

	grole_sw->usb_data_role_votable =
		gvotable_create_int_election(NULL, NULL, update_data_role, grole_sw);

	if (!grole_sw->usb_data_role_votable)
		return -EINVAL;

	gvotable_set_vote2str(grole_sw->usb_data_role_votable, gvotable_v2s_int);
	gvotable_election_set_name(grole_sw->usb_data_role_votable, VOTABLE_USB_DATA_ROLE);

	/*
	 * For systems without aoc, cast a default vote to align the logic. As a side-effect, there
	 * will be a failed update role attempt since downstream of the role switch driver is not
	 * setup at this moment.
	 */
	if (of_property_read_bool(node, "disable-aoc-voter"))
		gvotable_cast_vote(grole_sw->usb_data_role_votable,
				   AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);

	if (!of_property_read_u32(dev_of_node(dev), "role-sw-dev", &sw_handle)) {
		mod_delayed_work(system_wq, &grole_sw->init_role_sw_work, 0);
	} else {
		grole_sw->extcon = devm_extcon_dev_allocate(dev, data_role_extcon_cable);
		if (IS_ERR(grole_sw->extcon)) {
			dev_err(dev, "Error allocating extcon: %ld", PTR_ERR(grole_sw->extcon));
			return PTR_ERR(grole_sw->extcon);
		}

		ret = devm_extcon_dev_register(dev, grole_sw->extcon);
		if (ret < 0) {
			dev_err(dev, "failed to register extcon device:%d", ret);
			return ret;
		}

		grole_sw->downstream = EXTCON_DEV;
	}

	platform_set_drvdata(pdev, grole_sw);

	return 0;
}

static void google_role_sw_remove(struct platform_device *pdev)
{
	struct google_role_sw *grole_sw = platform_get_drvdata(pdev);

	if (!grole_sw) {
		dev_warn(&pdev->dev, "%s: the drvdata has already gone.\n", __func__);
		return;
	}

	cancel_delayed_work_sync(&grole_sw->init_role_sw_work);
	usb_role_switch_unregister(grole_sw->role_sw);
	gvotable_destroy_election(grole_sw->usb_data_role_votable);
	grole_sw->usb_data_role_votable = NULL;
}

static const struct of_device_id google_role_sw_match[] = {
	{ .compatible = "google,usb-role-sw" },
	{ },
};
MODULE_DEVICE_TABLE(of, google_role_sw_match);

static struct platform_driver google_usb_role_sw_driver = {
	.probe  = google_role_sw_probe,
	.remove = google_role_sw_remove,
	.driver = {
		.name = "google-usb-role-sw",
		.owner = THIS_MODULE,
		.dev_groups = google_role_sw_device_groups,
		.of_match_table = google_role_sw_match,
	},
};
module_platform_driver(google_usb_role_sw_driver);

MODULE_AUTHOR("Guan-Yu Lin <guanyulin@google.com>");
MODULE_DESCRIPTION("Google USB Data Role Switch Driver");
MODULE_LICENSE("GPL");
