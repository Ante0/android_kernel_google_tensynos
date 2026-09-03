/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP helper functions to check Android build type.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __GCIP_ANDROID_BUILD_H__
#define __GCIP_ANDROID_BUILD_H__

#include <linux/device.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/types.h>

/**
 * gcip_is_android_user_build() - Check if running on an Android user build.
 * @dev: Device pointer for logging.
 * @fallback_to_user: If true, assume it is a user build when the build type cannot be
 *                    definitively detected from the Device Tree. If false, assume it is
 *                    not a user build in those unclear cases.
 *
 * This function checks the 'variant' property in the 'dpm' node in the Device Tree.
 * If the property is found and valid, it returns true for "user" builds and false for
 * debug builds ("userdebug", "eng").
 * If the dpm node or variant property is missing, the return value is determined by the
 * @fallback_to_user parameter. For instance, features with secondary safety nets (like
 * debug dump with SSCD) may pass false to avoid unnecessarily disabling functionality on
 * unconfirmed builds, while sensitive features without backup protection can pass true
 * to remain fail-safe.
 *
 * Returns: true if confirmed to be an Android user build (or if unclear and @fallback_to_user
 * is true), false otherwise.
 */
static inline bool gcip_is_android_user_build(struct device *dev, bool fallback_to_user)
{
	struct device_node *dpm = NULL;
	const char *variant = NULL;
	bool is_user = fallback_to_user;

	dpm = of_find_node_by_name(NULL, "dpm");
	if (!dpm) {
		dev_warn(dev, "'dpm' node not found in DT. Assuming %s build.\n",
			 fallback_to_user ? "user" : "non-user");
		return fallback_to_user;
	}

	if (of_property_read_string(dpm, "variant", &variant)) {
		dev_warn(dev, "'variant' property not found in DPM node. Assuming %s build.\n",
			 fallback_to_user ? "user" : "non-user");
		goto out;
	}

	dev_dbg(dev, "Android build variant: %s.\n", variant);

	if (strcmp(variant, "user") == 0) {
		is_user = true;
	} else if (strcmp(variant, "userdebug") == 0 || strcmp(variant, "eng") == 0) {
		is_user = false;
	} else {
		dev_warn(dev, "Unknown build type '%s'. Assuming %s build.\n", variant,
			 fallback_to_user ? "user" : "non-user");
		is_user = fallback_to_user;
	}

out:
	of_node_put(dpm);
	return is_user;
}

#endif /* __GCIP_ANDROID_BUILD_H__ */
