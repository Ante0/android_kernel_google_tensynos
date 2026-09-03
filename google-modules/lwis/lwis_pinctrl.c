// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Pinctrl Interface
 *
 * Copyright (c) 2018 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-pin: " fmt

#include "lwis_device.h"
#include "lwis_pinctrl.h"

#include <linux/kernel.h>
#include <linux/pinctrl/consumer.h>

int lwis_pinctrl_set_state(struct lwis_device *lwis_dev, char *state_str)
{
	int ret;
	struct pinctrl *pc;
	struct pinctrl_state *state;

	pc = devm_pinctrl_get(lwis_dev->k_dev);
	if (IS_ERR_OR_NULL(pc)) {
		dev_err(lwis_dev->dev, "Failed to get pinctrl (%ld)\n", PTR_ERR(pc));
		return PTR_ERR(pc);
	}

	state = pinctrl_lookup_state(pc, state_str);
	if (IS_ERR_OR_NULL(state)) {
		dev_err(lwis_dev->dev, "Cannot find pinctrl state %s (%ld)\n", state_str,
			PTR_ERR(state));
		devm_pinctrl_put(pc);
		return PTR_ERR(state);
	}

	ret = pinctrl_select_state(pc, state);
	if (ret) {
		dev_err(lwis_dev->dev, "Cannot select state %s (%d)\n", state_str, ret);
		devm_pinctrl_put(pc);
		return ret;
	}

	devm_pinctrl_put(pc);
	return 0;
}
