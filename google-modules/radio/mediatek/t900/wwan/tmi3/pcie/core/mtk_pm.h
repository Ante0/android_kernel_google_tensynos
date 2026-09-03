/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_PM_H__
#define __MTK_PM_H__

#include <linux/device.h>
#include <linux/list.h>

#include "mtk_dev.h"

#define RPM_LINK_STATE_INVALID	-1
#define RPM_LINK_STATE_L2	0
#define RPM_LINK_STATE_L12	1
#define E_DS_LOCK_WAIT_FAIL		0x44534C4B
#define E_L3_RESUME_FAIL		0x4C335246
#define E_L2_RESUME_FAIL		0x4C325246
#define E_L2_EXCEPT_RESUME_FAIL		0x4C324552
#define E_MD_SUSPEND_TIMEOUT		0x4D445354
#define E_MD_RESUME_TIMEOUT		0x4D445254
#define E_SAP_SUSPEND_TIMEOUT		0x41505354
#define E_SAP_RESUME_TIMEOUT		0x41505254

enum mtk_pm_flag {
	PM_PREVENT_SUSPEND        = 0,
	PM_IN_SUSPENDED           = 1,
	PM_F_MAX
};

enum mtk_pm_resume_state {
	PM_RESUME_STATE_L3 = 0,
	PM_RESUME_STATE_L1,
	PM_RESUME_STATE_INIT,
	PM_RESUME_STATE_L1_EXCEPT,
	PM_RESUME_STATE_L2,
	PM_RESUME_STATE_L2_EXCEPT,
	PM_RESUME_STATE_MAX
};

enum mtk_md_pm_flag {
	PM_RESUME_FROM_L3	= 0,
	PM_NEED_SUSPEND_SAP	= 1,
	PM_FLAG_MAX
};

enum mtk_sleep_entity {
	PM_NOT_REQ = 0,
	PM_AP_REQ = BIT(0),
	PM_MD_REQ = BIT(1),
	PM_ALL_REQ = BIT(0) | BIT(1),
	PM_MAX_REQ
};

enum mtk_pm_entity_flag {
	PM_PREPARE_DONE = 1,
	PM_SUSPEND_DONE = 2,
	PM_SUSPEND_LATE_DONE = 3,
	PM_MAX
};

struct mtk_pm_entity {
	struct list_head entry;
	enum mtk_user_id user;
	unsigned long flag;
	void *param;

	int (*prepare)(struct mtk_md_dev *mdev, void *param, bool is_smart_suspend);
	int (*suspend)(struct mtk_md_dev *mdev, void *param, bool is_runtime);
	int (*suspend_late)(struct mtk_md_dev *mdev, void *param, bool is_runtime);
	int (*resume_early)(struct mtk_md_dev *mdev, void *param, bool is_runtime, bool link_ready);
	int (*resume)(struct mtk_md_dev *mdev, void *param, bool is_runtime, bool link_ready);
	int (*complete)(struct mtk_md_dev *mdev, void *param, bool is_smart_suspend);
};

enum mtk_pm_dbg_type {
	PM_LOCK_REFCNT,
	PM_RUNTIME_USAGE,
	PM_TYPE_MAX
};

struct mtk_pm_dbg {
	enum mtk_pm_dbg_type pm_dbg_type;
	atomic_t stat_val[PM_TYPE_MAX][MTK_USER_MAX];
	atomic_t rpm_get_total[MTK_USER_MAX];
	atomic_t rpm_put_total[MTK_USER_MAX];
	u64 last_get_async_time;
	int last_get_async_user;
};

struct mtk_pm_cfg {
	u32 ds_lock_wait_timeout_ms;
	u32 suspend_wait_timeout_ms;
	u32 resume_wait_timeout_ms;
	u32 suspend_wait_timeout_sap_ms;
	u32 resume_wait_timeout_sap_ms;
	u32 ds_lock_polling_max_us;
	u32 ds_lock_polling_min_us;
	u32 ds_lock_polling_interval_us;
	u32 ds_lock_check_bitmask;
	u32 ds_lock_check_val;
	unsigned short runtime_idle_delay;
};

struct mtk_pm_statistics {
	u32 ds_lock_user[MTK_USER_MAX];
	u32 ds_unlock_user[MTK_USER_MAX];
} __packed;

struct mtk_pci_pm {
	struct mtk_md_dev *mdev;
	struct list_head entities;
	/* entity_mtx is to protect concurrently
	 * read or write of pm entity list.
	 */
	struct mutex entity_mtx;
	/* force_d3l2_mtx is to protect concurrently
	 * enter and exit force d3l2 flow.
	 */
	struct mutex force_d3l2_mtx;
	bool force_d3l2_done;
	bool force_d3l2_init;
	int irq_id;
	int pewake_irq;
	u32 ext_evt_chs;
	unsigned int pme_support;
	unsigned long state;

	/* ds_spinlock is to protect concurrently
	 * ds lock or unlock procedure.
	 */
	spinlock_t ds_spinlock;
	struct completion ds_lock_complete;
	atomic_t ds_lock_refcnt;
	struct delayed_work ds_unlock_work;
	u64 ds_lock_sent;
	u64 ds_lock_recv;

	struct completion pm_ack;
	struct completion pm_ack_sap;
	struct delayed_work resume_work;
	struct work_struct pewake_work;
	/* suspend counter for debug purpose. */
	u32 suspend_cnt;
	u32 resume_cnt;

	bool pm_failure_test;
	unsigned long md_pm_flag;
	int rpm_link_state;
	struct mtk_pm_cfg cfg;
	struct mtk_pm_dbg pm_dbg;
	struct dentry *dentry;
	struct mtk_pm_statistics pm_stats;
	bool smart_rpm_resume;
};

int mtk_pm_init(struct mtk_md_dev *mdev);
int mtk_pm_exit(struct mtk_md_dev *mdev);
int mtk_pm_entity_register(struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity);
int mtk_pm_entity_unregister(struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity);
void mtk_pm_ds_lock(struct mtk_md_dev *mdev, enum mtk_user_id user);
int mtk_pm_ds_try_lock(struct mtk_md_dev *mdev, enum mtk_user_id user);
void mtk_pm_ds_unlock(struct mtk_md_dev *mdev, enum mtk_user_id user);
void mtk_pm_ds_unlock_instant(struct mtk_md_dev *mdev, enum mtk_user_id user);
int mtk_pm_ds_wait_complete(struct mtk_md_dev *mdev, enum mtk_user_id user);
int mtk_pm_ds_try_wait_complete(struct mtk_md_dev *mdev, enum mtk_user_id user);
int mtk_pm_exit_early(struct mtk_md_dev *mdev);
bool mtk_pm_check_dev_reset(struct mtk_md_dev *mdev);
void mtk_pm_debug_dump(struct mtk_md_dev *mdev);

int mtk_pm_runtime_idle(struct device *dev);
int mtk_pm_runtime_suspend(struct device *dev);
int mtk_pm_runtime_resume(struct device *dev, bool atr_init);
int mtk_pm_prepare(struct device *dev);
void mtk_pm_complete(struct device *dev);
int mtk_pm_suspend(struct device *dev);
int mtk_pm_resume(struct device *dev, bool atr_init);
int mtk_pm_freeze(struct device *dev);
int mtk_pm_restore(struct device *dev, bool atr_init);
void mtk_pm_shutdown(struct mtk_md_dev *mdev);
int mtk_pm_runtime_get(struct mtk_md_dev *mdev, enum mtk_user_id user, bool sync);
int mtk_pm_runtime_put(struct mtk_md_dev *mdev, enum mtk_user_id user, bool sync);
int mtk_pm_stats_init_op(struct mtk_md_dev *mdev);
void mtk_pm_stats_exit_op(struct mtk_md_dev *mdev);
bool mtk_pm_allow_smart_suspend(struct mtk_md_dev *mdev);
void mtk_pm_set_smart_suspend_wake(struct mtk_md_dev *mdev, bool is_wake);
bool mtk_pm_smart_suspend_enabled(void);
ssize_t mtk_pm_stats_cb_op(struct mtk_md_dev *mdev, void *data, char *buf);
ssize_t exit_d3l2_store(struct device *dev, struct device_attribute *attr, const char *buf,
			size_t count);
#endif /* __MTK_PM_H__ */
