/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2025 Google, LLC
 *
 */

#ifndef MAX77779_USECASE_V2_H_
#define MAX77779_USECASE_V2_H_

#include "google_bms_usecase.h"
#include "max77779_usecase.h"

struct max77779_uc_v2_data {
	u8 reg;					/* max77779 charger reg */

	struct device *dev;
	struct device *core;
	int init_done;

	struct gpio_desc *bst_on;		/* ext boost */
	struct gpio_desc *ext_bst_mode;		/* ext boost mode */
	struct gpio_desc *ext_bst_ctl;		/* SEQ VENDOR_EXTBST.EXT_BST_EN */
	struct regulator *ext_bst_supply;
	struct mutex ext_bst_lock;
	bool fs_ext_bst_state;

	struct max77779_usecase_data common_data;
};

enum max77779_uc_v2_chg_sel {
	MAX77779_UC_V2_CHG_SEL_DEFAULT,
	MAX77779_UC_V2_CHG_SEL_HYBRID,
};

int max77779_usecase_v2_setup_usecases(void **uc_d, struct device *dev);
int max77779_usecase_v2_usecase_remove(void *uc_data);
#endif /* MAX77779_USECASE_V2_H_ */
