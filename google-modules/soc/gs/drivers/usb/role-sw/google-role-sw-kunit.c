// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 * Unit test for Google's USB data role switch driver
 */

#include <linux/property.h>
#include <kunit/test.h>
#include <kunit/device.h>
#include <kunit/visibility.h>
#include <misc/gvotable.h>

#include <linux/usb/google-role-sw.h>

struct role_sw_test_data {
	struct google_role_sw *grole_sw;
	enum usb_role role_set_by_downstream;
	bool downstream_did_set_role;
};

static void tcpm_only_test(struct kunit *test)
{
	struct role_sw_test_data *test_data = (struct role_sw_test_data *)test->priv;
	struct google_role_sw *grole_sw = test_data->grole_sw;
	struct gvotable_election *data_role_election = grole_sw->usb_data_role_votable;

	// Default vote on systems without AOC
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);

	// Normal role change without AOC
	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_DEVICE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_DEVICE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_DEVICE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_NONE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	// Downstream should not set role if not necessary
	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	// TCPCI casting an invalid vote (b/403361763)
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)-1, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

}

static void aoc_ssr_test(struct kunit *test)
{
	struct role_sw_test_data *test_data = (struct role_sw_test_data *)test->priv;
	struct google_role_sw *grole_sw = test_data->grole_sw;
	struct gvotable_election *data_role_election = grole_sw->usb_data_role_votable;

	// SSR when device is in HOST
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_NONE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	// SSR when device is in DEVICE
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_DEVICE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_DEVICE, grole_sw->curr_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_NONE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_DEVICE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_DEVICE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	// SSR when device is in NONE
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_NONE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_NONE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);
}

static void disable_usb_data_test(struct kunit *test)
{
	struct role_sw_test_data *test_data = (struct role_sw_test_data *)test->priv;
	struct google_role_sw *grole_sw = test_data->grole_sw;
	struct gvotable_election *data_role_election = grole_sw->usb_data_role_votable;

	gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);

	// Disabling USB Data when device is in DEVICE
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_DEVICE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_DEVICE, grole_sw->curr_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER, (void *)(long)true, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	// Role switch while USB Data disabled should not take effect
	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	// Previous role switch should take effect after enabling USB Data again
	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER, (void *)(long)false, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	// Disabling USB Data when device is in HOST
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER, (void *)(long)true, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER, (void *)(long)false, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, USB_ROLE_HOST, test_data->role_set_by_downstream);
	KUNIT_EXPECT_EQ(test, true, test_data->downstream_did_set_role);

	// Disabling USB Data when device is in NONE
	gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_NONE, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER, (void *)(long)true, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER, (void *)(long)false, 1);
	KUNIT_EXPECT_EQ(test, USB_ROLE_NONE, grole_sw->curr_role);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);
}

static void none_downstream_test(struct kunit *test)
{
	struct role_sw_test_data *test_data = (struct role_sw_test_data *)test->priv;
	struct google_role_sw *grole_sw = test_data->grole_sw;
	struct gvotable_election *data_role_election = grole_sw->usb_data_role_votable;
	int ret;

	usb_role_switch_unregister(grole_sw->role_sw);
	grole_sw->role_sw = NULL;
	grole_sw->downstream = NONE;

	test_data->downstream_did_set_role = false;
	ret = gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)(long)USB_ROLE_HOST, 1);
	KUNIT_EXPECT_NE(test, 0, ret);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);

	test_data->downstream_did_set_role = false;
	ret = gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)(long)USB_ROLE_DEVICE, 1);
	KUNIT_EXPECT_NE(test, 0, ret);
	KUNIT_EXPECT_EQ(test, false, test_data->downstream_did_set_role);
}

static void all_vote_combinations_test(struct kunit *test)
{
	// This test iterates over all the possible combinations of the votes. It does not reflect
	// real world scenarios, and might not be possible if the number of voters keep increasing.
	struct role_sw_test_data *test_data = (struct role_sw_test_data *)test->priv;
	struct google_role_sw *grole_sw = test_data->grole_sw;
	struct gvotable_election *data_role_election = grole_sw->usb_data_role_votable;

	// AOC, USB data disabled, TCPC, result
	long data[][4] = {
		{USB_ROLE_HOST, false,   USB_ROLE_HOST,   USB_ROLE_HOST},
		{USB_ROLE_HOST, false,   USB_ROLE_NONE,   USB_ROLE_NONE},
		{USB_ROLE_HOST, false, USB_ROLE_DEVICE, USB_ROLE_DEVICE},
		{USB_ROLE_HOST,  true,   USB_ROLE_HOST,   USB_ROLE_NONE},
		{USB_ROLE_HOST,  true,   USB_ROLE_NONE,   USB_ROLE_NONE},
		{USB_ROLE_HOST,  true, USB_ROLE_DEVICE,   USB_ROLE_NONE},
		{USB_ROLE_NONE, false,   USB_ROLE_HOST,   USB_ROLE_NONE},
		{USB_ROLE_NONE, false,   USB_ROLE_NONE,   USB_ROLE_NONE},
		{USB_ROLE_NONE, false, USB_ROLE_DEVICE, USB_ROLE_DEVICE},
		{USB_ROLE_NONE,  true,   USB_ROLE_HOST,   USB_ROLE_NONE},
		{USB_ROLE_NONE,  true,   USB_ROLE_NONE,   USB_ROLE_NONE},
		{USB_ROLE_NONE,  true, USB_ROLE_DEVICE,   USB_ROLE_NONE},
	};
	int it;

	for (it = 0; it < 12; it++) {
		gvotable_cast_vote(data_role_election, AOC_VOTER, (void *)data[it][0], 1);
		gvotable_cast_vote(data_role_election, DISABLE_USB_DATA_VOTER,
			(void *)data[it][1], 1);
		gvotable_cast_vote(data_role_election, TCPCI_COMB_VOTER, (void *)data[it][2], 1);
		KUNIT_EXPECT_EQ(test, data[it][3], grole_sw->curr_role);
	}
}

static int downstream_role_switch_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct role_sw_test_data *test_data = usb_role_switch_get_drvdata(sw);

	test_data->role_set_by_downstream = role;
	test_data->downstream_did_set_role = true;
	return 0;
}

static struct google_role_sw *grole_sw_init(struct kunit *test)
{
	struct google_role_sw *grole_sw;
	struct device_driver *grole_sw_drv;

	grole_sw = kunit_kzalloc(test, sizeof(*grole_sw), GFP_KERNEL);

	// Mock the related driver/device with a fake downstream
	grole_sw_drv = kunit_driver_create(test, "fake-grole_sw-drv");
	grole_sw->dev = kunit_device_register_with_driver(test, "fake-role_sw", grole_sw_drv);

	grole_sw->usb_data_role_votable =
		gvotable_create_int_election(NULL, NULL, update_data_role, grole_sw);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, grole_sw->usb_data_role_votable);

	struct usb_role_switch_desc role_sw_desc = {0};

	role_sw_desc.fwnode = dev_fwnode(grole_sw->dev);
	role_sw_desc.set = downstream_role_switch_set;
	role_sw_desc.allow_userspace_control = false;
	role_sw_desc.driver_data = test->priv;
	grole_sw->role_sw = usb_role_switch_register(grole_sw->dev, &role_sw_desc);
	grole_sw->curr_role = -EINVAL;
	grole_sw->downstream = USB_ROLE_SWITCH;
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, grole_sw->role_sw);

	return grole_sw;
}

static int google_role_sw_update_data_role_test_init(struct kunit *test)
{
	struct role_sw_test_data *test_data = kunit_kzalloc(test, sizeof(*test_data), GFP_KERNEL);

	test->priv = test_data;
	test_data->grole_sw = grole_sw_init(test);
	return 0;
}

static void google_role_sw_update_data_role_test_exit(struct kunit *test)
{
	struct role_sw_test_data *test_data = (struct role_sw_test_data *)test->priv;
	struct google_role_sw *grole_sw = test_data->grole_sw;

	if (grole_sw) {
		if (grole_sw->usb_data_role_votable)
			gvotable_destroy_election(grole_sw->usb_data_role_votable);
		if (grole_sw->role_sw)
			usb_role_switch_unregister(grole_sw->role_sw);
	}
}

static struct kunit_case google_role_sw_update_data_role_test_cases[] = {
	KUNIT_CASE(tcpm_only_test),
	KUNIT_CASE(aoc_ssr_test),
	KUNIT_CASE(disable_usb_data_test),
	KUNIT_CASE(none_downstream_test),
	KUNIT_CASE(all_vote_combinations_test),
	{}
};

static struct kunit_suite google_role_sw_update_data_role_test_suite = {
	.name = "google-role-sw-update-data-role-tests",
	.test_cases = google_role_sw_update_data_role_test_cases,
	.init = google_role_sw_update_data_role_test_init,
	.exit = google_role_sw_update_data_role_test_exit,
};
kunit_test_suite(google_role_sw_update_data_role_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
