/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform data for the PE26100 usecase driver.
 */

#ifndef _PE26100_CHG_USECASE_DRIVER_H_
#define _PE26100_CHG_USECASE_DRIVER_H_

struct pe26100_uc_data {
	struct device *dev;
	struct device *core;

	int cur_usecase;
	int retry_count;
	struct mutex uc_lock;
	struct delayed_work init_work;
	struct delayed_work finish_usecase_work;
	struct delayed_work reset_work;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry	*de;
#endif
};

#endif /* _PE26100_CHG_USECASE_DRIVER_H_ */
