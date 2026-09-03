/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Max77779 Usecase State machine
 *
 * Copyright 2024 Google LLC
 *
 */

#ifndef MAX77779_USECASE_H_
#define MAX77779_USECASE_H_

#include <linux/platform_device.h>

enum max77779_usecase_version {
	MAX77779_USECASE_VERSION_DEFAULT = 1,
	MAX77779_USECASE_VERSION_2
};

struct max77779_usecase_data {
	struct device *dev;
	struct device *core;

	enum max77779_usecase_version ver;
	void *uc_data;

	bool mode_cb_debounce;			/* debounce mode callback */
};

int max77779_usecase_common_data_init(struct max77779_usecase_data *data, struct device *dev);
bool max77779_usecase_cb_data_is_chgr_on(const struct bms_usecase_foreach_cb_data *cb_data);
int max77779_usecase_wlc_fw_update_enable(struct max77779_usecase_data *uc_data, bool enable);
bool max77779_usecase_is_debounce(struct max77779_usecase_data *data,
				  struct bms_usecase_foreach_cb_data *cb_data);
#endif /* MAX77779_USECASE_H_ */
