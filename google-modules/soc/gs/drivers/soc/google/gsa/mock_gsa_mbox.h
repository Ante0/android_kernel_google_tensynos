/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2024 Google LLC.
 */

/* This header is internal only.
 *
 * Public APIs are in //private/google-modules/soc/gs/include/linux/gsa/
 *
 * Include via //private/google-modules/soc/gs:gs_soc_headers
 */

#ifndef __LINUX_MOCK_GSA_MBOX_H
#define __LINUX_MOCK_GSA_MBOX_H

#include <kunit/test.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "gsa_mbox.h"

/*
 * When gsa calls the mock send_mbox_msg, the mock will populate these
 * values as the arguments it received.
 */
struct mock_gsa_recvd {
	u32 cmd;
	u32 argc;
	u32 *args;
};

/*
 * When gsa calls the mock send_mbox_msg, the mock will return these values.
 * These should be set for each test.
 */
struct mock_gsa_resp {
	u32 cmd;
	u32 err;
	u32 argc;
	u32 *args;
};

struct mock_gsa_mbox;

struct mock_gsa_mbox *mock_gsa_mbox_init(struct kunit *test);

/* Returns the underlying gsa device that can be passed to tested functions. */
struct device *mock_gsa_get_device(struct mock_gsa_mbox *mock_mbox);

/* Returns true after the mock send_mbox_cmd has been called. */
bool gsa_mock_was_called(struct mock_gsa_mbox *mock_mbox);

/*
 * Returns a struct used to set the return values for the mock send_mbox_cmd.
 * When values in the returned struct are set before calling send_mbox_cmd,
 * the mocked call to gsa will return the set values.
 * This can be used to test how code reacts to certain returned values from send_mbox_cmd.
 */
struct mock_gsa_resp *gsa_mock_resp(struct mock_gsa_mbox *mock_mbox);

/*
 * Returns the args that were received by send_mbox_msg.
 * This can be used to confirm that send_mbox_cmd received the appropriate
 * args from the test function.
 */
const struct mock_gsa_recvd *gsa_mock_args_recvd(struct mock_gsa_mbox *mock_mbox);

#endif /* __LINUX_MOCK_GSA_MBOX_H */
