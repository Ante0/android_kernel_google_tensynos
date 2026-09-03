// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/sched/clock.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/version.h>

#include "mtk_debug.h"
#include "mtk_except.h"
#include "mtk_frc.h"
#include "mtk_fsm.h"
#include "mtk_pci.h"
#include "mtk_pcie_memlog.h"
#include "mtk_pcie_trace.h"
#include "mtk_pcimsg.h"
#include "mtk_pm.h"
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
#include "mtk_pwrctl.h"
#endif
#include "mtk_statistics.h"
#include "mtk_utility.h"
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
#include "pcie-mediatek-gen3.h"
#endif
#ifdef CONFIG_UT_PCIE_PM
#include "ut_pm_fake.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "mtk_google.h"
#include "common/ap-awake.h"
#include "pcie/link-exception.h"
#include "pcie/mtk-pcie.h"
#include "pcie/remote-wakeup.h"
#include "pcie/mtk-pcie-pm-user.h"
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
#include "pcie/md2ap-wakemon.h"
#endif

#define TAG "PM"
#define PCIE_SUSPEND_CNT_MAGIC		(0x53550000)
#define PCIE_RESUME_CNT_MAGIC		(0x52450000)
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#define LINK_CHECK_RETRY_COUNT		3
#else
#define LINK_CHECK_RETRY_COUNT		30
#endif
#define DS_LOCK_WAIT_TIMEOUT_MS		50
#define SUSPEND_WAIT_TIMEOUT_MD_MS	1500
#define RESUME_WAIT_TIMEOUT_MD_MS	1500
#define SUSPEND_WAIT_TIMEOUT_SAP_MS	1500
#define RESUME_WAIT_TIMEOUT_SAP_MS	1500
#define DS_LOCK_POLLING_MAX_US		10000
#define DS_LOCK_POLLING_MIN_US		2000
#define DS_LOCK_POLLING_INTERVAL_US	10
#define WAKELOCK_ACTIVE_TIME_MAX_MS	10000
#define mdev_get_pm(mdev) (((struct mtk_pci_priv *)((mdev)->hw_priv))->pm)
#define PM_STATS_PERIOD_S	2

static unsigned short ds_delayed_unlock_timeout_ms = 100;
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
static unsigned int runtime_idle_delay_ms = 3000;
#else
static unsigned short runtime_idle_delay_seconds = 20;
#endif
static bool smart_suspend_enabled;
static bool must_smart_suspend;
static bool force_skip_ds_lock;
static bool force_skip_enter_d3l2;
static bool d3l2_force_dsw;

#ifdef CONFIG_MTK_MEMLOG_EVENT_SUPPORT

struct event_stats_pm {
	struct memlog_event_msg event_msg;
	struct mtk_pm_statistics pm_stats;
	u8 cnt;
} __packed;

#define MTK_DEBUG_STATS_PM(mdev, data) \
do {\
	struct event_stats_pm *event_stats_pm; \
	struct mtk_md_dev *__mdev = mdev; \
	struct mtk_pm_statistics *__pm_stats = data; \
	event_stats_pm = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_STATS, \
						   sizeof(struct event_stats_pm)); \
	if (!event_stats_pm) \
		break; \
	mtk_memlog_event_msg_init(&event_stats_pm->event_msg, STATS_PM); \
	memcpy(&event_stats_pm->pm_stats, __pm_stats, sizeof(struct mtk_pm_statistics)); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_STATS); \
} while (0)

#define MTK_DEBUG_STATS_PM_WITH_BUF(data, buf, size) \
do {\
	struct mtk_pm_statistics *__pm_stats = data; \
	struct event_stats_pm *event_stats_pm = (struct event_stats_pm *)buf; \
	mtk_memlog_add_info(&event_stats_pm->event_msg); \
	mtk_memlog_event_msg_init(&event_stats_pm->event_msg, STATS_PM); \
	memcpy(&event_stats_pm->pm_stats, __pm_stats, sizeof(struct mtk_pm_statistics)); \
	size = sizeof(struct event_stats_pm); \
} while (0)

#else
#define MTK_DEBUG_STATS_PM_WITH_BUF(data, buf, size) \
do { \
	struct mtk_pm_statistics *__pm_stats = data; \
	ssize_t __size = 0; \
	char *__buf = buf; \
	u32 __i; \
	for (__i = 0; __i < MTK_USER_MAX; __i++) { \
		__size += sprintf(__buf + __size, \
				  "[PM] DS Lock USER[%u]: cnt=%u\n", \
				  __i, \
				  __pm_stats->ds_lock_user[__i]); \
	} \
	for (__i = 0; __i < MTK_USER_MAX; __i++) { \
		__size += sprintf(__buf + __size, \
				  "[PM] DS UnLock USER[%u]: cnt=%u\n", \
				  __i, \
				  __pm_stats->ds_unlock_user[__i]); \
	} \
	size = __size; \
} while (0)

#define MTK_DEBUG_STATS_PM(mdev, data) \
do { \
	struct mtk_md_dev *__mdev = mdev; \
	struct mtk_pm_statistics *__pm_stats = data; \
	\
	u32 __i; \
	for (__i = 0; __i < MTK_USER_MAX; __i++) { \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"[PM] DS Lock USER[%u]: cnt=%u\n", \
			__i, \
			__pm_stats->ds_lock_user[__i]); \
	} \
	for (__i = 0; __i < MTK_USER_MAX; __i++) { \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"[PM] DS UnLock USER[%u]: cnt=%u\n", \
			__i, \
			__pm_stats->ds_unlock_user[__i]); \
	} \
} while (0)

#endif

static int mtk_pm_resume_device(struct mtk_md_dev *mdev, bool is_runtime, bool atr_init);
static int mtk_pm_suspend_device(struct mtk_md_dev *mdev, bool is_runtime);
static int mtk_pm_request_pewake_eint(struct mtk_md_dev *mdev);
static void mtk_pm_exit_d3l2(struct mtk_md_dev *mdev);

static int mtk_pci_save_state_and_set_d3(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	u16 pmcsr;

	/* Don't need to save state here, already saved in probe() */
	pci_disable_device(pdev);
	if (pci_set_power_state(pdev, PCI_D3hot)) {
		MTK_ERR(mdev, "Set D3hot fail.\n");
		return -EFAULT;
	}

	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &pmcsr);
	MTK_INFO(mdev, "PMCSR[0x%x], current state[0x%x]\n", pmcsr, pmcsr & PCI_PM_CTRL_STATE_MASK);
	return 0;
}

static int mtk_pci_restore_state_and_set_d0(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (pci_set_power_state(pdev, PCI_D0)) {
		MTK_ERR(mdev, "Set D0 fail.\n");
		return -EFAULT;
	}

	if (pcim_enable_device(pdev)) {
		MTK_ERR(mdev, "Enable device fail.\n");
		return -EFAULT;
	}

	pci_load_saved_state(pdev, priv->saved_state);
	pci_restore_state(pdev);
#if defined(CONFIG_PCIEASPM) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
	mtk_pci_restore_aspm_l1ss_state(mdev);
#endif
	return 0;
}

static void mtk_pm_pewake_eint_work(struct work_struct *work)
{
	struct mtk_pci_pm *pm  = container_of(work, struct mtk_pci_pm, pewake_work);
	struct mtk_md_dev *mdev = pm->mdev;

	MTK_INFO(mdev, "PEWAKE trigger to exit D3L2!\n");
	mtk_pm_exit_d3l2(mdev);
}

ssize_t exit_d3l2_store(struct device *dev, struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);

	if (unlikely(!buf || !count))
		return -EINVAL;

	if (!strncmp(buf, "exit", strlen("exit"))) {
		MTK_INFO(mdev, "Host CMD trigger EP exit D3L2 through sysfs!\n");
		mtk_pm_exit_d3l2(mdev);
	}

	return count;
}

static DEVICE_ATTR_WO(exit_d3l2);

static void mtk_pm_enter_d3l2(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct device_node *eint_node;
	struct device *parent_dev;
	struct irq_desc *desc;
	struct pci_bus *bus;

	parent_dev = pdev->bus->parent->bridge->parent;
	if (!parent_dev) {
		MTK_WARN(mdev, "Failed to find parent_dev!\n");
		return;
	}

	if (!pm->force_d3l2_init) {
		mutex_init(&pm->force_d3l2_mtx);
		eint_node = of_parse_phandle(parent_dev->of_node, "eint-irq", 0);
		if (eint_node)
			pm->pewake_irq = of_irq_get(eint_node, 0);

		INIT_WORK(&pm->pewake_work, mtk_pm_pewake_eint_work);
		pm->force_d3l2_init = true;
		if (device_create_file(mdev->dev, &dev_attr_exit_d3l2)) {
			MTK_ERR(mdev, "Unable to create exit_d3l2 entry\n");
			return;
		}
	}

	mutex_lock(&pm->force_d3l2_mtx);

	if (pm->force_d3l2_done || test_bit(PM_PREVENT_SUSPEND, &pm->state) ||
	    force_skip_enter_d3l2) {
		MTK_INFO(mdev, "Already in D3L2[%d] or fsm not ready or skip[%d]!\n",
			 pm->force_d3l2_done, force_skip_enter_d3l2);
		mutex_unlock(&pm->force_d3l2_mtx);
		return;
	}

	pm->force_d3l2_done = true;
	pm_stay_awake(mdev->dev);
	pm_runtime_get_sync(mdev->dev);

	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);

	if (mtk_pm_suspend_device(mdev, false)) {
		MTK_ERR(mdev, "EP driver suspend fail!\n");
		goto exit;
	}

	mtk_pcimsg_wait_pci_user_inactive(mdev);

	if (mtk_pci_save_state_and_set_d3(mdev))
		goto exit;

	bus = pci_find_bus(1, 0);
	if (!bus) {
		MTK_ERR(mdev, "Fail to find pci bus!\n");
		goto exit;
	}
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
	mtk_pcie_soft_off(bus);
#endif
	desc = irq_to_desc(pm->pewake_irq);
	if (desc->irq_data.chip->irq_ack)
		desc->irq_data.chip->irq_ack(&desc->irq_data);

	if (mtk_pm_request_pewake_eint(mdev)) {
		MTK_ERR(mdev, "request pewake eint fail!\n");
		goto exit;
	}

	pinctrl_pm_select_idle_state(parent_dev);
	MTK_INFO(mdev, "enter force d3l2 successfully!\n");
	mutex_unlock(&pm->force_d3l2_mtx);
	return;

exit:
	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_READY);
	pm_runtime_put(mdev->dev);
	pm_relax(mdev->dev);
	pm->force_d3l2_done = false;
	mutex_unlock(&pm->force_d3l2_mtx);
	MTK_ERR(mdev, "enter force d3l2 fail!\n");
}

static void mtk_pm_exit_d3l2(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct pci_bus *bus;

	mutex_lock(&pm->force_d3l2_mtx);
	if (!pm->force_d3l2_done) {
		MTK_INFO(mdev, "Already in active state!\n");
		mutex_unlock(&pm->force_d3l2_mtx);
		return;
	}

	bus = pci_find_bus(1, 0);
	if (!bus) {
		MTK_ERR(mdev, "Can not find pci bus!\n");
		mutex_unlock(&pm->force_d3l2_mtx);
		return;
	}
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
	if (mtk_pcie_soft_on(bus)) {
		MTK_ERR(mdev, "Failed to soft on pcie link!\n");
		mutex_unlock(&pm->force_d3l2_mtx);
		return;
	}
#endif
	mtk_pci_restore_state_and_set_d0(mdev);
	mtk_pm_resume_device(mdev, false, true);
	if (pm->pewake_irq)
		free_irq(pm->pewake_irq, mdev);
	pm_runtime_put(mdev->dev);
	pm_relax(mdev->dev);
	pm->force_d3l2_done = false;
	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_READY);
	MTK_INFO(mdev, "exit force d3l2 successfully!\n");
	mutex_unlock(&pm->force_d3l2_mtx);
}

static irqreturn_t mtk_pm_pewake_eint_handler(int irq, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_pm *pm;

	pm = mdev_get_pm(mdev);
	queue_work(system_highpri_wq, &pm->pewake_work);
	return 0;
}

static int mtk_pm_request_pewake_eint(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	int ret;

	if (pm->pewake_irq <= 0)
		return -EINVAL;

	ret = request_irq(pm->pewake_irq, mtk_pm_pewake_eint_handler, IRQF_TRIGGER_FALLING,
			  "pcie-eint", mdev);
	if (ret < 0) {
		MTK_WARN(mdev, "Failed to request pewake eint");
		return ret;
	}

	return 0;
}

/**
 * mtk_pm_debug_statistics - check pm argument value.
 * @mdev: pointer to mtk_md_dev
 * @type: the argument need to check
 * @user: user who issues lock request.
 * @is_add: perform operation
 * @caller_addr: the caller of this function
 *
 * This function record pm argument and print err info
 */
static void mtk_pm_debug_statistics(struct mtk_md_dev *mdev,
				    enum mtk_pm_dbg_type type,
				    enum mtk_user_id user, bool is_add, void *caller_addr)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	int stats_val;

	if (is_add) {
		atomic_inc(&pm->pm_dbg.stat_val[type][user]);
	} else {
		stats_val = atomic_dec_return(&pm->pm_dbg.stat_val[type][user]);
		if (stats_val < 0) {
			MTK_ERR(mdev, "PM check, type:%d, user:%d, caller:%ps, cnt:%d\n",
				type, user, caller_addr, stats_val);
			MTK_ERR(mdev, "ds_lock_refcnt = %d, runtime_usage = %d\n",
				atomic_read(&pm->ds_lock_refcnt),
				atomic_read(&mdev->dev->power.usage_count));
		}
	}
}

void mtk_pm_debug_dump(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	u8 i, j;

	for (i = 0; i < PM_TYPE_MAX; i++)
		for (j = 0; j < MTK_USER_MAX; j++)
			MTK_INFO(mdev, "PM check, type:%d, user:%d, cnt:%d\n",
				 i, j, atomic_read(&pm->pm_dbg.stat_val[i][j]));

	for (i = 0; i < MTK_USER_MAX; i++)
		MTK_INFO(mdev, "RPM check, user:%d, get:%d, put:%d\n",
			 i, atomic_read(&pm->pm_dbg.rpm_get_total[i]),
			 atomic_read(&pm->pm_dbg.rpm_put_total[i]));

	MTK_INFO(mdev, "ds_lock_refcnt = %d, runtime_usage = %d\n",
		 atomic_read(&pm->ds_lock_refcnt),
		 atomic_read(&mdev->dev->power.usage_count));
}

static void mtk_pm_debug_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	u8 i, j;

	for (i = 0; i < PM_TYPE_MAX; i++)
		for (j = 0; j < MTK_USER_MAX; j++)
			atomic_set(&pm->pm_dbg.stat_val[i][j], 0);
}

static ssize_t mtk_pm_debug_write(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;

	if (!strncmp(buf, "dump", strlen("dump")))
		mtk_pm_debug_dump(mdev);

	return cnt;
}

static ssize_t mtk_pm_failure_test(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_pm *pm;

	pm = mdev_get_pm(mdev);

	if (!strncmp(buf, "enable", strlen("enable"))) {
		pm->pm_failure_test = true;
		MTK_INFO(mdev, "Enable pm failure_test successfully!\n");
		return cnt;
	}

	pm->pm_failure_test = false;
	MTK_INFO(mdev, "Close pm failure_test!\n");
	return cnt;
}

static ssize_t mtk_pm_force_d3l2(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_pm *pm;
	struct pci_dev *pdev;

	pm = mdev_get_pm(mdev);
	pdev = to_pci_dev(mdev->dev);

#if !(IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3))
	MTK_INFO(mdev, "Current project not support force_d3l2\n");
	return cnt;
#endif
	if (!strncmp(buf, "entry", strlen("entry"))) {
		MTK_INFO(mdev, "Host CMD trigger EP enter D3L2!\n");
		mtk_pm_enter_d3l2(mdev);
	} else if (!strncmp(buf, "exit", strlen("exit"))) {
		MTK_INFO(mdev, "Host CMD trigger EP exit D3L2!\n");
		mtk_pm_exit_d3l2(mdev);
	} else {
		MTK_INFO(mdev, "Error input!\n");
	}

	return cnt;
}

#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
static ssize_t mtk_pm_smart_suspend_write(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	struct handshake_info hs_info;
	struct mtk_pci_pm *pm;

	pm = mdev_get_pm(mdev);

	if (pm->rpm_link_state != RPM_LINK_STATE_L2) {
		MTK_WARN(mdev, "rpm_link_state is not in L2 mode, it is:%d\n", pm->rpm_link_state);
		return cnt;
	}

	if (!strncmp(buf, "on", strlen("on"))) {
		smart_suspend_enabled = true;
		hs_info.feature_id = PCIE_SMART_SUSPEND;
		hs_info.data[0] = PCIE_SMART_SUSPEND_ENABLE;
		device_init_wakeup(mdev->dev, false);
	} else if (!strncmp(buf, "off", strlen("off"))) {
		smart_suspend_enabled = false;
		hs_info.feature_id = PCIE_SMART_SUSPEND;
		hs_info.data[0] = PCIE_SMART_SUSPEND_DISABLE;
		device_init_wakeup(mdev->dev, true);
	} else {
		MTK_WARN(mdev, "Invalid smart suspend parameter: %s\n", buf);
		return cnt;
	}

	mtk_pm_runtime_get(mdev, MTK_USER_PM, true);
	mtk_pcie_ep_set_info(1, &hs_info);
	MTK_INFO(mdev, "Set smart suspend to %s!\n", buf);
	mtk_pm_runtime_put(mdev, MTK_USER_PM, false);

	return cnt;
}

MTK_DBGFS(smart_suspend, NULL, mtk_pm_smart_suspend_write); /* note that */
#endif

MTK_DBGFS(pm_debug, NULL, mtk_pm_debug_write); /* note that */
MTK_DBGFS(failure_test, NULL, mtk_pm_failure_test);
MTK_DBGFS(force_d3l2, NULL, mtk_pm_force_d3l2);

static void mtk_pm_dbgfs_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct dentry *dentry;

	dentry = mtk_dbgfs_create_dir(mtk_get_dev_dentry(mdev), "pm");
	if (IS_ERR_OR_NULL(dentry))
		return;

	pm->dentry = dentry;
	mtk_dbgfs_create_file(pm->dentry, &mtk_dbgfs_pm_debug, mdev);
	mtk_dbgfs_create_file(pm->dentry, &mtk_dbgfs_failure_test, mdev);
	mtk_dbgfs_create_file(pm->dentry, &mtk_dbgfs_force_d3l2, mdev);

#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
	mtk_dbgfs_create_file(pm->dentry, &mtk_dbgfs_smart_suspend, mdev);
#endif
}

static void mtk_pm_dbgfs_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	mtk_dbgfs_remove(pm->dentry);
}

static void mtk_pm_write_suspend_resume_cnt(struct mtk_md_dev *mdev, u32 val, bool is_suspend)
{
	u32 magic_cnt = is_suspend ? PCIE_SUSPEND_CNT_MAGIC : PCIE_RESUME_CNT_MAGIC;

	mtk_pci_write_pm_cnt(mdev, magic_cnt | val);
}

void mtk_pm_set_smart_suspend_wake(struct mtk_md_dev *mdev, bool is_wake)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	pm->smart_rpm_resume = is_wake;
}

bool mtk_pm_allow_smart_suspend(struct mtk_md_dev *mdev)
{
	struct device *dev = mdev->dev;

	return smart_suspend_enabled && pm_runtime_status_suspended(dev);
}

bool mtk_pm_smart_suspend_enabled(void)
{
	return smart_suspend_enabled;
}

static int mtk_pm_wait_ds_lock_done(struct mtk_md_dev *mdev, u32 delay)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	u32 polling_time = 0;
	u32 reg;

	do {
		reg = mtk_pci_get_ds_status(mdev);
		if ((reg & pm->cfg.ds_lock_check_bitmask) == pm->cfg.ds_lock_check_val)
			return 0;

		/* Delay some time to poll the deep sleep status. */
		udelay(pm->cfg.ds_lock_polling_interval_us);
		polling_time += pm->cfg.ds_lock_polling_interval_us;
		if (!mtk_pci_mmio_check(mdev))
			break;
	} while (polling_time < delay);

	MTK_ERR(mdev, "Max polling time %u, actual polling time %u, res_state = 0x%x\n",
		delay, polling_time, reg);

	return -ETIMEDOUT;
}

static int mtk_pm_try_lock_l1ss_ds(struct mtk_md_dev *mdev, bool report)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	int ret;

	mtk_pci_disable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_PM), MAC_ACTIVE_PM_L1SS);
	ret = mtk_pm_wait_ds_lock_done(mdev, pm->cfg.ds_lock_polling_max_us);
	if (ret) {
		MTK_WARN(mdev, "Unable to lock L1ss!\n");
		if (mtk_pci_mmio_check(mdev))
			MTK_WARN(mdev, "ds_status = 0x%x\n", mtk_pci_get_ds_status(mdev));
		else if (report)
			mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);
	}

	return ret;
}

static int mtk_pm_reset(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	unsigned long flags;

	clear_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag);

	MTK_INFO(mdev, "cancel the delayed unlock workqueue.\n");
	cancel_delayed_work_sync(&pm->ds_unlock_work);
	if (!atomic_read(&pm->ds_lock_refcnt)) {
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
		if (mtk_pci_link_check_silent(mdev)) {
#endif
		spin_lock_irqsave(&pm->ds_spinlock, flags);
		mtk_pci_ds_unlock(mdev);
		spin_unlock_irqrestore(&pm->ds_spinlock, flags);
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
		}
#endif
	} else {
		MTK_WARN(mdev, "ds_lock_refcnt = %d!\n",
			 atomic_read(&pm->ds_lock_refcnt));
	}

	if (!test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
		set_bit(PM_PREVENT_SUSPEND, &pm->state);
		pm_runtime_get_noresume(mdev->dev);
	}
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (mtk_pci_link_check_silent(mdev)) {
#endif
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_PM), MAC_ACTIVE_PM_L1SS);
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	}
#endif

	return 0;
}

static int mtk_pm_init_late(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	mtk_pci_unmask_ext_evt(mdev, pm->ext_evt_chs);
	mtk_pci_unmask_irq(mdev, pm->irq_id);
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_PM), MAC_ACTIVE_PM_L1SS);
	if (!pm_runtime_set_active(mdev->dev))
		MTK_INFO(mdev, "Set runtime active!\n");

	if (test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
		clear_bit(PM_PREVENT_SUSPEND, &pm->state);
		pm_runtime_put(mdev->dev);
		pm_relax(mdev->dev);
	}

	return 0;
}

static bool mtk_pm_except_handle(struct mtk_md_dev *mdev, bool report, u32 err)
{
	if (mtk_pci_mmio_check(mdev)) {
		MTK_WARN(mdev, "PM trigger MDEE!\n");
		mtk_pci_trigger_mdee(mdev, err);
		mtk_pci_info_dump(mdev);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		if (err == E_MD_SUSPEND_TIMEOUT || err == E_SAP_SUSPEND_TIMEOUT)
			radio_google_suspend_timeout_handler(mdev->google);
		else if (err == E_MD_RESUME_TIMEOUT || err == E_SAP_RESUME_TIMEOUT)
			radio_google_resume_timeout_handler(mdev->google);
#endif
	} else {
		if (report)
			mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);

		MTK_WARN(mdev, "PM detected linkdown! report = %d\n", report);
		return false;
	}

	return true;
}

/**
 * mtk_pm_ds_lock - Lock device power state to prevent it entering deep sleep.
 * @mdev: pointer to mtk_md_dev
 * @user: user who issues lock request.
 *
 * This function locks device power state, any user who
 * needs to interact with device shall make sure that
 * device is not in deep sleep.
 */
void mtk_pm_ds_lock(struct mtk_md_dev *mdev, enum mtk_user_id user)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	unsigned long flags;
	u32 reg;

	mtk_pm_debug_statistics(mdev, PM_LOCK_REFCNT, user, 1,
				__builtin_return_address(0));

	pm->pm_stats.ds_lock_user[user]++;
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->sys_type_base + 1));

	if (force_skip_ds_lock) {
		reinit_completion(&pm->ds_lock_complete);
		complete_all(&pm->ds_lock_complete);
		atomic_inc(&pm->ds_lock_refcnt);
		return;
	}

	spin_lock_irqsave(&pm->ds_spinlock, flags);
	if (atomic_inc_return(&pm->ds_lock_refcnt) == 1) {
		reinit_completion(&pm->ds_lock_complete);
		trace_mtk_pm_ds_lock_start(user);
		mtk_pci_ds_lock(mdev);
		trace_mtk_pm_ds_lock_end(user);
		if (test_bit(PM_PREVENT_SUSPEND, &pm->state) ||
		    test_bit(PM_IN_SUSPENDED, &pm->state))
			goto complete;

		reg = mtk_pci_get_ds_status(mdev);
		trace_mtk_pm_ds_status(user, reg);
		if ((reg & pm->cfg.ds_lock_check_bitmask) == pm->cfg.ds_lock_check_val)
			goto complete;

		trace_mtk_pm_raise_wakeup_irq_to_md(pm->ds_lock_sent);
		if (priv->cfg->flag & MTK_CFG_PM_SW_IRQ)
			mtk_pci_send_sw_evt(mdev, H2D_SW_EVT_PM_LOCK);
		else
			mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_DS_LOCK);

		spin_unlock_irqrestore(&pm->ds_spinlock, flags);
		MTK_DBG_PM_DS_LOCK(mdev, user, pm->ds_lock_sent++, reg);
		mtk_frc_check_and_sync(mdev);

		return;

complete:
	complete_all(&pm->ds_lock_complete);
	}

	spin_unlock_irqrestore(&pm->ds_spinlock, flags);
	if (!test_bit(PM_PREVENT_SUSPEND, &pm->state) && !test_bit(PM_IN_SUSPENDED, &pm->state))
		mtk_frc_check_and_sync(mdev);
}

/**
 * mtk_pm_ds_try_lock - Lock and poll device power state.
 * @mdev: pointer to mtk_md_dev
 * @user: user who issues lock request.
 *
 * This function locks device power state, then poll the lock
 * status for a while.
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_ds_try_lock(struct mtk_md_dev *mdev, enum mtk_user_id user)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	unsigned long flags;
	int ret = 0;

	mtk_pm_debug_statistics(mdev, PM_LOCK_REFCNT, user, 1,
				__builtin_return_address(0));

	pm->pm_stats.ds_lock_user[user]++;
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->sys_type_base + 1));

	if (force_skip_ds_lock) {
		atomic_inc(&pm->ds_lock_refcnt);
		return 0;
	}

	spin_lock_irqsave(&pm->ds_spinlock, flags);
	if (atomic_inc_return(&pm->ds_lock_refcnt) == 1) {
		mtk_pci_ds_lock(mdev);

		if (test_bit(PM_PREVENT_SUSPEND, &pm->state) ||
		    test_bit(PM_IN_SUSPENDED, &pm->state)) {
			spin_unlock_irqrestore(&pm->ds_spinlock, flags);
			return 0;
		}

		ret = mtk_pm_wait_ds_lock_done(mdev,
					       pm->cfg.ds_lock_polling_min_us);
	}
	spin_unlock_irqrestore(&pm->ds_spinlock, flags);
	mtk_frc_check_and_sync(mdev);

	return ret;
}

/**
 * mtk_pm_ds_unlock - Unlock device power state.
 * @mdev: pointer to mtk_md_dev
 * @user: user who issues unlock request.
 *
 * This function unlocks device power state, after all users
 * unlock device power state, the device will enter deep sleep.
 */
void mtk_pm_ds_unlock(struct mtk_md_dev *mdev, enum mtk_user_id user)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	u32 unlock_timeout;

	mtk_pm_debug_statistics(mdev, PM_LOCK_REFCNT, user, 0,
				__builtin_return_address(0));

	pm->pm_stats.ds_unlock_user[user]++;
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->sys_type_base + 1));

	atomic_dec(&pm->ds_lock_refcnt);
	unlock_timeout = ds_delayed_unlock_timeout_ms;
	if (!atomic_read(&pm->ds_lock_refcnt)) {
		cancel_delayed_work(&pm->ds_unlock_work);
		schedule_delayed_work(&pm->ds_unlock_work, msecs_to_jiffies(unlock_timeout));
	}
}

void mtk_pm_ds_unlock_instant(struct mtk_md_dev *mdev, enum mtk_user_id user)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	unsigned long flags;

	mtk_pm_debug_statistics(mdev, PM_LOCK_REFCNT, user, 0,
				__builtin_return_address(0));

	pm->pm_stats.ds_unlock_user[user]++;
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->sys_type_base + 1));

	atomic_dec(&pm->ds_lock_refcnt);
	spin_lock_irqsave(&pm->ds_spinlock, flags);
	if (!atomic_read(&pm->ds_lock_refcnt))
		mtk_pci_ds_unlock(mdev);
	spin_unlock_irqrestore(&pm->ds_spinlock, flags);
}

/**
 * mtk_pm_ds_wait_complete -Try to get completion for a while.
 *
 * @mdev: pointer to mtk_md_dev
 * @user: user id
 *
 * The function is not interruptible.
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_ds_wait_complete(struct mtk_md_dev *mdev, enum mtk_user_id user)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	u32 unlock_timeout;
	int ret;

	/* 0 if timed out, and positive (at least 1,
	 * or number of jiffies left  till timeout) if completed.
	 */
	unlock_timeout = pm->cfg.ds_lock_wait_timeout_ms;
	ret = wait_for_completion_timeout(&pm->ds_lock_complete, msecs_to_jiffies(unlock_timeout));
	trace_mtk_pm_completion_sync_end(user, ret);
	if (ret > 0)
		return 0;

	ret = mtk_pci_get_ds_status(mdev);
	if ((ret & pm->cfg.ds_lock_check_bitmask) == pm->cfg.ds_lock_check_val &&
	    ret != 0xffffffff) {
		MTK_WARN(mdev, "pm wait ds_lock timeout but lock ready!\n");
		return 0;
	}

	if (mtk_pm_except_handle(mdev, true, E_DS_LOCK_WAIT_FAIL))
		return -ETIMEDOUT;

	return -EIO;
}

/**
 * mtk_pm_ds_try_wait_complete -Try to get completion
 * once without blocking.
 *
 * @mdev: pointer to mtk_md_dev
 * @user: user id
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_ds_try_wait_complete(struct mtk_md_dev *mdev, enum mtk_user_id user)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	int ret = 0;

	MTK_DBG(mdev, MTK_DBG_PM, MTK_MEMLOG_RG_COMMON, "user id = %d, caller: %ps\n",
		user, __builtin_return_address(0));

	/* try_wait_for_completion returns 0 if completion
	 * is not available,  otherwise 1.
	 */
	ret = try_wait_for_completion(&pm->ds_lock_complete);

	return (ret > 0 ? 0 : -ETIMEDOUT);
}

static void mtk_pm_ds_unlock_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;
	unsigned long flags;

	pm = container_of(dwork, struct mtk_pci_pm, ds_unlock_work);
	mdev = pm->mdev;

	spin_lock_irqsave(&pm->ds_spinlock, flags);
	if (!atomic_read(&pm->ds_lock_refcnt))
		mtk_pci_ds_unlock(mdev);
	spin_unlock_irqrestore(&pm->ds_spinlock, flags);
}

/**
 * mtk_pm_entity_register - Register pm entity into mtk_pci_pm's list entry.
 * @mdev: pointer to mtk_md_dev
 * @md_entity: user callback entity
 *
 * After registration, pm entity's related callbacks
 * could be called upon pm event happening.
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_entity_register(struct mtk_md_dev *mdev,
			   struct mtk_pm_entity *md_entity)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *pm_obj;

	MTK_INFO(mdev, "md_entity = %d\n", md_entity->user);
	mutex_lock(&pm->entity_mtx);
	list_for_each_entry(pm_obj, &pm->entities, entry) {
		if (pm_obj->user == md_entity->user) {
			MTK_WARN(mdev, "md_entity = %d already registered!\n", md_entity->user);
			mutex_unlock(&pm->entity_mtx);
			return -EALREADY;
		}
	}
	list_add_tail(&md_entity->entry, &pm->entities);
	mutex_unlock(&pm->entity_mtx);

	return 0;
}

/**
 * mtk_pm_entity_unregister - Unregister pm entity from mtk_pci_pm's list entry.
 * @mdev: pointer to mtk_md_dev
 * @md_entity: user callback entity
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_entity_unregister(struct mtk_md_dev *mdev,
			     struct mtk_pm_entity *md_entity)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *pm_obj, *cursor;

	MTK_INFO(mdev, "md_entity = %d\n", md_entity->user);
	mutex_lock(&pm->entity_mtx);
	list_for_each_entry_safe(cursor, pm_obj, &pm->entities, entry) {
		if (cursor->user == md_entity->user) {
			list_del(&cursor->entry);
			mutex_unlock(&pm->entity_mtx);
			return 0;
		}
	}
	mutex_unlock(&pm->entity_mtx);
	MTK_WARN(mdev, "md_entity = %d already deleted!\n", md_entity->user);

	return -EALREADY;
}

/**
 * mtk_pm_check_dev_reset - Check if device power off after suspended.
 * @mdev: pointer to mtk_md_dev
 *
 * Return: true indicates device is powered off after suspended,
 * false indicates device is not powered off after suspended.
 */
bool mtk_pm_check_dev_reset(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	return test_bit(PM_RESUME_FROM_L3, &pm->md_pm_flag);
}

static ssize_t mtk_pm_stats_cb(struct mtk_md_dev *mdev, void *data, char *buf)
{
	ssize_t size = 0;

	if (!buf)
		MTK_DEBUG_STATS_PM(mdev, data);
	else
		MTK_DEBUG_STATS_PM_WITH_BUF(data, buf, size);

	return size;
}

static int mtk_pm_stats_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	return mtk_stats_register_cb(mdev, mdev->utility_cfg->stats_cfg->sys_type_base + 1,
				     PM_STATS_PERIOD_S, mtk_pm_stats_cb,
				     &pm->pm_stats);
}

static void mtk_pm_stats_exit(struct mtk_md_dev *mdev)
{
	mtk_stats_unregister_cb(mdev, mdev->utility_cfg->stats_cfg->sys_type_base + 1);
}

int mtk_pm_stats_init_op(struct mtk_md_dev *mdev)
{
	return mtk_pm_stats_init(mdev);
}

void mtk_pm_stats_exit_op(struct mtk_md_dev *mdev)
{
	mtk_pm_stats_init(mdev);
}

ssize_t mtk_pm_stats_cb_op(struct mtk_md_dev *mdev, void *data, char *buf)
{
	return mtk_pm_stats_cb(mdev, data, buf);
}

static void mtk_pm_reinit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	int i;

	if (!test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
		set_bit(PM_PREVENT_SUSPEND, &pm->state);
		clear_bit(PM_IN_SUSPENDED, &pm->state);
		pm_runtime_get_noresume(mdev->dev);
		pm_wakeup_event(mdev->dev, WAKELOCK_ACTIVE_TIME_MAX_MS);
	}

	for (i = 0; i < MTK_USER_MAX; i++) {
		pm->pm_stats.ds_lock_user[i] = 0;
		pm->pm_stats.ds_unlock_user[i] = 0;
	}
}

static void mtk_pm_entity_resume_early(struct mtk_md_dev *mdev, bool is_runtime, bool link_ready)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *entity;

	list_for_each_entry(entity, &pm->entities, entry) {
		if (test_bit(PM_SUSPEND_LATE_DONE, &entity->flag) && entity->resume_early) {
			if (entity->resume_early(mdev, entity->param, is_runtime, link_ready))
				MTK_ERR(mdev, "user:%d entity_resume_early fail!\n", entity->user);
		}
		clear_bit(PM_SUSPEND_LATE_DONE, &entity->flag);
	}
}

static void mtk_pm_entity_resume(struct mtk_md_dev *mdev, bool is_runtime, bool link_ready)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *entity;

	list_for_each_entry(entity, &pm->entities, entry) {
		if (test_bit(PM_SUSPEND_DONE, &entity->flag) && entity->resume) {
			if (entity->resume(mdev, entity->param, is_runtime, link_ready))
				MTK_ERR(mdev, "user:%d entity_resume fail!\n", entity->user);
		}
		clear_bit(PM_SUSPEND_DONE, &entity->flag);
	}
}

static int mtk_pm_entity_suspend(struct mtk_md_dev *mdev, bool is_runtime)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *entity;
	int ret;

	list_for_each_entry(entity, &pm->entities, entry) {
		if (entity->suspend) {
			ret = entity->suspend(mdev, entity->param, is_runtime);
			if (ret) {
				MTK_ERR(mdev, "user:%d suspend failed!\n", entity->user);
				return ret;
			}
		}
		set_bit(PM_SUSPEND_DONE, &entity->flag);
	}
	return 0;
}

static int mtk_pm_entity_suspend_late(struct mtk_md_dev *mdev, bool is_runtime)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *entity;
	int ret;

	list_for_each_entry(entity, &pm->entities, entry) {
		if (entity->suspend_late) {
			ret = entity->suspend_late(mdev, entity->param, is_runtime);
			if (ret) {
				MTK_ERR(mdev, "user:%d suspend_late failed!\n", entity->user);
				return ret;
			}
		}
		set_bit(PM_SUSPEND_LATE_DONE, &entity->flag);
	}

	return 0;
}

static int mtk_pm_entity_prepare(struct mtk_md_dev *mdev, bool is_smart_suspend)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *entity;
	int ret;

	list_for_each_entry(entity, &pm->entities, entry) {
		if (entity->prepare) {
			ret = entity->prepare(mdev, entity->param, is_smart_suspend);
			if (ret) {
				MTK_ERR(mdev, "user:%d prepare failed!\n", entity->user);
				return ret;
			}
		}
		set_bit(PM_PREPARE_DONE, &entity->flag);
	}
	return 0;
}

static void mtk_pm_entity_complete(struct mtk_md_dev *mdev, bool is_smart_suspend)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_pm_entity *entity;

	list_for_each_entry(entity, &pm->entities, entry) {
		if (test_bit(PM_PREPARE_DONE, &entity->flag) && entity->complete) {
			if (entity->complete(mdev, entity->param, is_smart_suspend))
				MTK_ERR(mdev, "user:%d complete fail!\n", entity->user);
		}
		clear_bit(PM_PREPARE_DONE, &entity->flag);
	}
}

static void mtk_pm_timeout_resume(struct mtk_md_dev *mdev, bool is_md)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	u32 suspend_timeout;
	u32 resume_timeout;

	if (is_md) {
		reinit_completion(&pm->pm_ack);
		mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_PM_RESUME_REQ);
		resume_timeout = pm->cfg.resume_wait_timeout_ms;
		if (wait_for_completion_timeout(&pm->pm_ack,
						msecs_to_jiffies(resume_timeout)))
			MTK_INFO(mdev, "MD resume success in suspend timeout flow\n");
		else
			MTK_ERR(mdev, "MD resume timeout in suspend timeout flow\n");
	} else {
		suspend_timeout = pm->cfg.suspend_wait_timeout_sap_ms;
		if (wait_for_completion_timeout(&pm->pm_ack_sap,
						msecs_to_jiffies(suspend_timeout))) {
			reinit_completion(&pm->pm_ack_sap);
			mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_PM_RESUME_REQ_AP);
			resume_timeout = pm->cfg.resume_wait_timeout_sap_ms;
			if (wait_for_completion_timeout(&pm->pm_ack_sap,
							msecs_to_jiffies(resume_timeout)))
				MTK_INFO(mdev, "sAP resume success in suspend timeout flow\n");
			else
				MTK_ERR(mdev, "sAP resume timeout in suspend timeout flow\n");
		} else {
			MTK_ERR(mdev, "sAP suspend timeout too!\n");
		}
	}
}

static void mtk_pm_pci_reinit_err_handle(struct mtk_md_dev *mdev, bool is_runtime, u32 err)
{
	bool link_ready = false;

	if (mtk_pm_except_handle(mdev, true, err))
		link_ready = true;
	mtk_pm_entity_resume_early(mdev, is_runtime, link_ready);
	mtk_pm_entity_resume(mdev, is_runtime, link_ready);
	mtk_fsm_start(mdev);
}

static int mtk_pm_enable_wake(struct mtk_md_dev *mdev, u8 dev_state, u8 system_state, bool enable)
{
#ifdef CONFIG_ACPI
	union acpi_object in_arg[3];
	struct acpi_object_list arg_list = { 3, in_arg };
	struct pci_dev *bridge;
	acpi_status acpi_ret;
	acpi_handle handle;

	if (acpi_disabled) {
		MTK_ERR(mdev, "Unsupported, acpi function isn't enable\n");
		return -ENODEV;
	}

	bridge = pci_upstream_bridge(to_pci_dev(mdev->dev));
	if (!bridge) {
		MTK_ERR(mdev, "Unable to find bridge\n");
		return -ENODEV;
	}

	handle = ACPI_HANDLE(&bridge->dev);
	if (!handle) {
		MTK_ERR(mdev, "Unsupported, acpi handle isn't found\n");
		return -ENODEV;
	}
	if (!acpi_has_method(handle, "_DSW")) {
		MTK_ERR(mdev, "Unsupported, _DSW method isn't supported\n");
		return -ENODEV;
	}

	in_arg[0].type = ACPI_TYPE_INTEGER;
	in_arg[0].integer.value = enable;
	in_arg[1].type = ACPI_TYPE_INTEGER;
	in_arg[1].integer.value = system_state;
	in_arg[2].type = ACPI_TYPE_INTEGER;
	in_arg[2].integer.value = dev_state;
	acpi_ret = acpi_evaluate_object(handle, "_DSW", &arg_list, NULL);
	if (ACPI_FAILURE(acpi_ret)) {
		MTK_WARN(mdev, "_DSW method fail for parent: %s\n",
			 acpi_format_exception(acpi_ret));
		return acpi_ret;
	}
	MTK_INFO(mdev, "_DSW execute successfully\n");

	return 0;
#else
	MTK_ERR(mdev, "Unsupported, CONFIG ACPI hasn't been set to 'y'\n");

	return -ENODEV;
#endif
}

static int mtk_pm_suspend_device(struct mtk_md_dev *mdev, bool is_runtime)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	bool link_ready = true;
	unsigned long flags;
	u32 suspend_timeout;
	int pm_virq_id;
	int ret;

	pm_virq_id = mtk_pci_get_virq_id(mdev, pm->irq_id);

	MTK_INFO(mdev, "Suspend enter, is_runtime = %d, Suspend_cnt = %u\n",
		 is_runtime, pm->suspend_cnt);

	if (!is_runtime) {
		if (mtk_fsm_pause(mdev)) {
			MTK_ERR(mdev, "Suspend exit for fsm pause failed!\n");
			return -EAGAIN;
		}
	}

	if (test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
		if (!is_runtime)
			mtk_fsm_start(mdev);
		MTK_INFO(mdev, "Suspend exit for fsm handshake not done or in EE!\n");
		return -EBUSY;
	}

	ret = mtk_pm_try_lock_l1ss_ds(mdev, true);
	if (ret) {
		MTK_ERR(mdev, "Failed to lock l1ss when suspend!\n");
		if (!is_runtime)
			mtk_fsm_start(mdev);
		return -EAGAIN;
	}

	set_bit(PM_IN_SUSPENDED, &pm->state);

	mtk_exception_stop(mdev);

	ret = mtk_pm_entity_suspend(mdev, is_runtime);
	if (ret)
		goto err_suspend;

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	MTK_GOOGLE_INFO_RTC(mdev, "Starting suspend handshake\n");
#endif

	/* write counter of suspend */
	mtk_pm_write_suspend_resume_cnt(mdev, pm->suspend_cnt++, true);
	reinit_completion(&pm->pm_ack);
	mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_PM_SUSPEND_REQ);
	MTK_INFO(mdev, "Send MD suspend req, AP req = 0x%lx\n", pm->md_pm_flag);
	if (test_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag)) {
		reinit_completion(&pm->pm_ack_sap);
		mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_PM_SUSPEND_REQ_AP);
	}

	suspend_timeout = pm->cfg.suspend_wait_timeout_ms;
	ret = wait_for_completion_timeout(&pm->pm_ack, msecs_to_jiffies(suspend_timeout));
	if (!ret) {
		MTK_ERR(mdev, "MD suspend timeout!\n");
		if (mtk_pm_except_handle(mdev, true, E_MD_SUSPEND_TIMEOUT)) {
			if (test_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag))
				mtk_pm_timeout_resume(mdev, false);
		} else {
			link_ready = false;
		}
		ret = -ETIMEDOUT;
		goto err_suspend;
	}
	if (test_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag)) {
		suspend_timeout = pm->cfg.suspend_wait_timeout_sap_ms;
		ret = wait_for_completion_timeout(&pm->pm_ack_sap,
						  msecs_to_jiffies(suspend_timeout));
		if (!ret) {
			MTK_ERR(mdev, "sAP suspend timeout!\n");
			if (mtk_pm_except_handle(mdev, true, E_SAP_SUSPEND_TIMEOUT))
				mtk_pm_timeout_resume(mdev, true);
			else
				link_ready = false;
			ret = -ETIMEDOUT;
			goto err_suspend;
		}
	}

	mtk_pci_clear_atr_doorbell(mdev);

	ret = mtk_pm_entity_suspend_late(mdev, is_runtime);
	if (ret)
		goto err_suspend_late;

	cancel_delayed_work_sync(&pm->ds_unlock_work);
	if (!atomic_read(&pm->ds_lock_refcnt)) {
		spin_lock_irqsave(&pm->ds_spinlock, flags);
		mtk_pci_ds_unlock(mdev);
		spin_unlock_irqrestore(&pm->ds_spinlock, flags);
	} else {
		MTK_WARN(mdev, "ds_lock_refcnt = %d!\n",
			 atomic_read(&pm->ds_lock_refcnt));
	}

	if (is_runtime && d3l2_force_dsw)
		mtk_pm_enable_wake(mdev, 3, 0, true);

	mtk_pci_irq_suspend_action(mdev);
	mtk_pci_mask_irq(mdev, pm->irq_id);
	synchronize_irq(pm_virq_id);
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_PM), MAC_ACTIVE_PM_L1SS);
	MTK_INFO(mdev, "Suspend success.\n");

	return 0;

err_suspend_late:
	mtk_pm_entity_resume_early(mdev, is_runtime, link_ready);
err_suspend:
	mtk_pm_reinit(mdev);
	mtk_pm_entity_resume(mdev, is_runtime, link_ready);
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_PM), MAC_ACTIVE_PM_L1SS);
	if (!is_runtime)
		mtk_fsm_start(mdev);
	mtk_exception_start(mdev);
	clear_bit(PM_IN_SUSPENDED, &pm->state);
	return -EAGAIN;
}

static int mtk_pm_do_resume_device(struct mtk_md_dev *mdev, enum mtk_sleep_entity resume_entity,
				   bool is_runtime)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	bool link_ready = true;
	u32 resume_timeout;
	int ret = 0;

	mtk_pm_try_lock_l1ss_ds(mdev, true);
	mtk_pci_unmask_irq(mdev, pm->irq_id);
	mtk_pci_irq_resume_action(mdev);
	mtk_pci_dump_atr_doorbell(mdev);
	mtk_pm_entity_resume_early(mdev, is_runtime, link_ready);
	mtk_pm_write_suspend_resume_cnt(mdev, pm->resume_cnt, false);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	MTK_GOOGLE_INFO_RTC(mdev, "Starting resume handshake\n");
#endif

	if (resume_entity & PM_AP_REQ) {
		reinit_completion(&pm->pm_ack_sap);
		mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_PM_RESUME_REQ_AP);
		MTK_INFO(mdev, "Send AP resume req\n");
	}

	if (resume_entity & PM_MD_REQ) {
		reinit_completion(&pm->pm_ack);
		mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_PCIE_PM_RESUME_REQ);
		MTK_INFO(mdev, "Send MD resume req\n");
	}

	if (resume_entity & PM_AP_REQ) {
		resume_timeout = pm->cfg.resume_wait_timeout_sap_ms;
		if (!wait_for_completion_timeout(&pm->pm_ack_sap,
						 msecs_to_jiffies(resume_timeout))) {
			MTK_ERR(mdev, "sAP resume timeout!\n");
			link_ready = mtk_pm_except_handle(mdev, true, E_SAP_RESUME_TIMEOUT);
			ret = -ETIMEDOUT;
		}
	}

	if (resume_entity & PM_MD_REQ) {
		resume_timeout = pm->cfg.resume_wait_timeout_ms;
		if (!wait_for_completion_timeout(&pm->pm_ack, msecs_to_jiffies(resume_timeout))) {
			MTK_ERR(mdev, "MD resume timeout!\n");
			link_ready = mtk_pm_except_handle(mdev, true, E_MD_RESUME_TIMEOUT);
			ret = -ETIMEDOUT;
		}
	}

	mtk_pm_entity_resume(mdev, is_runtime, link_ready);
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_PM), MAC_ACTIVE_PM_L1SS);
	if (!is_runtime)
		mtk_fsm_start(mdev);
	mtk_exception_start(mdev);
	clear_bit(PM_IN_SUSPENDED, &pm->state);
	MTK_INFO(mdev, "Resume status = %d, Resume cnt = %u\n", ret, pm->resume_cnt++);

	return 0;
}

static int mtk_pm_resume_device(struct mtk_md_dev *mdev, bool is_runtime, bool atr_init)
{
	enum mtk_sleep_entity resume_entity = PM_NOT_REQ;
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	enum mtk_pm_resume_state resume_state;
	int ret = 0;

	if (is_runtime && d3l2_force_dsw)
		mtk_pm_enable_wake(mdev, 0, 0, false);

	if (unlikely(test_bit(PM_PREVENT_SUSPEND, &pm->state))) {
		clear_bit(PM_IN_SUSPENDED, &pm->state);
		MTK_INFO(mdev, "Resume exit for fsm handshake not done or in EE!\n");
		return 0;
	}

	resume_state = mtk_pci_get_resume_state(mdev);
	if ((resume_state == PM_RESUME_STATE_INIT && atr_init) ||
	    resume_state == PM_RESUME_STATE_L3)
		set_bit(PM_RESUME_FROM_L3, &pm->md_pm_flag);
	else
		clear_bit(PM_RESUME_FROM_L3, &pm->md_pm_flag);

	MTK_INFO(mdev, "Resume Enter: resume state = %d, is_runtime = %d, atr_init = %d\n",
		 resume_state, is_runtime, atr_init);
	mtk_pci_dump_atr_doorbell(mdev);
	switch (resume_state) {
	case PM_RESUME_STATE_INIT:
		if (!atr_init) {
			MTK_INFO(mdev, "Resume without device state change!\n");
			if (test_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag))
				resume_entity = PM_ALL_REQ;
			else
				resume_entity = PM_MD_REQ;
			break;
		}
		fallthrough;
	case PM_RESUME_STATE_L3:
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_cold_resume_handler(mdev->google);
#endif

		ret = mtk_pci_reinit(mdev, REINIT_TYPE_RESUME);
		if (ret) {
			mtk_pm_pci_reinit_err_handle(mdev, is_runtime, E_L3_RESUME_FAIL);
			MTK_ERR(mdev, "Failed to reinit HW in resume routine!\n");
			return ret;
		}

		mtk_pm_entity_resume_early(mdev, is_runtime, true);
		mtk_pm_entity_resume(mdev, is_runtime, true);

		mtk_fsm_evt_submit(mdev, FSM_EVT_COLD_RESUME,
				   FSM_F_DFLT, NULL, 0, EVT_MODE_TOHEAD);
		/* No need to start except, for hw reinit will do it later. */
		if (is_runtime) {
			/* Submit partial reinit */
			mtk_fsm_evt_submit(mdev, FSM_EVT_REINIT,
					   FSM_F_DFLT, NULL, 0, 0);
		} else {
			mtk_fsm_start(mdev);
			mtk_fsm_evt_submit(mdev, FSM_EVT_REINIT,
					   FSM_F_DFLT, NULL, 0, EVT_MODE_BLOCKING);
		}
		MTK_INFO(mdev, "Resume status = %d\n", ret);
		return 0;
	case PM_RESUME_STATE_L2_EXCEPT:
		ret = mtk_pci_reinit_mac(mdev, TRUE);
		if (ret) {
			mtk_pm_pci_reinit_err_handle(mdev, is_runtime, E_L2_EXCEPT_RESUME_FAIL);
			MTK_ERR(mdev, "Failed to reinit mac in resume routine!\n");
			return ret;
		}
		fallthrough;
	case PM_RESUME_STATE_L1_EXCEPT:
		if (!test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
			set_bit(PM_PREVENT_SUSPEND, &pm->state);
			pm_runtime_get_noresume(mdev->dev);
		}
		if (test_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag))
			resume_entity = PM_AP_REQ;
		break;
	case PM_RESUME_STATE_L2:
		ret = mtk_pci_reinit_mac(mdev, TRUE);
		if (ret) {
			mtk_pm_pci_reinit_err_handle(mdev, is_runtime, E_L2_RESUME_FAIL);
			MTK_ERR(mdev, "Failed to reinit mac in resume routine!\n");
			return ret;
		}
		fallthrough;
	case PM_RESUME_STATE_L1:
		if (test_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag))
			resume_entity = PM_ALL_REQ;
		else
			resume_entity = PM_MD_REQ;
		break;
	default:
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_link_unready_handler(mdev->google, resume_state);
#endif

		MTK_INFO(mdev, "Resume but device not ready!\n");
		if (!test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
			set_bit(PM_PREVENT_SUSPEND, &pm->state);
			pm_runtime_get_noresume(mdev->dev);
		}
		mtk_pci_irq_resume_action(mdev);
		mtk_pm_entity_resume_early(mdev, is_runtime, false);
		mtk_pm_entity_resume(mdev, is_runtime, false);
		if (!is_runtime)
			mtk_fsm_start(mdev);
		cancel_delayed_work_sync(&pm->resume_work);
		schedule_delayed_work(&pm->resume_work, HZ);
		return 0;
	}

	return mtk_pm_do_resume_device(mdev, resume_entity, is_runtime);
}

static void mtk_pm_resume_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;
	int cnt = 0;
	bool ret;

	pm = container_of(dwork, struct mtk_pci_pm, resume_work);
	mdev = pm->mdev;

	do {
		ret = mtk_pci_link_check(mdev);
		if (ret)
			break;
		/* Wait for 1 second to check link state. */
		msleep(1000);
		cnt++;
	} while (cnt < LINK_CHECK_RETRY_COUNT);

	if (!ret) {
		mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);
		return;
	}
	mtk_fsm_evt_submit(mdev, FSM_EVT_COLD_RESUME, FSM_F_DFLT, NULL, 0, EVT_MODE_TOHEAD);
	/* FSM_EVT_REINIT is full reinit */
	mtk_fsm_evt_submit(mdev, FSM_EVT_REINIT, FSM_F_FULL_REINIT, NULL, 0, 0);
	MTK_INFO(mdev, "Resume success from L3 within delayed work.\n");
}

static int mtk_pm_prepare_smart_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	bool is_smart_suspend = true;
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;

	mdev = pci_get_drvdata(pdev);
	pm = mdev_get_pm(mdev);

	if (mtk_fsm_pause(mdev)) {
		MTK_ERR(mdev, "FSM pause failed!\n");
		goto exit_fsm_err;
	}

	if (!pm_runtime_status_suspended(dev)) {
		MTK_INFO(mdev, "FSM wakeup!\n");
		goto exit_fsm_wake;
	}

	if (mtk_pm_entity_prepare(mdev, is_smart_suspend) || !pm_runtime_status_suspended(dev)) {
		MTK_ERR(mdev, "Smart suspend_entity failed or user wakeup!\n");
		goto exit_user_wake;
	}

	must_smart_suspend = true;
	dev_pm_set_driver_flags(mdev->dev, 0);

	return 1;

exit_user_wake:
	mtk_pm_entity_complete(mdev, is_smart_suspend);
exit_fsm_wake:
	mtk_fsm_start(mdev);
exit_fsm_err:
	must_smart_suspend = false;

	return -EPERM;
}

int mtk_pm_prepare(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;

	mdev = pci_get_drvdata(pdev);
	pm = mdev_get_pm(mdev);

	if (mtk_pm_allow_smart_suspend(mdev))
		return mtk_pm_prepare_smart_suspend(dev);

	must_smart_suspend = false;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	dev_pm_set_driver_flags(mdev->dev, DPM_FLAG_NO_DIRECT_COMPLETE);
#else
	dev_pm_set_driver_flags(mdev->dev, DPM_FLAG_NEVER_SKIP);
#endif

	if (pm->pm_failure_test) {
		MTK_INFO(mdev, "Add pm wakelock to wakeup system suspend.\n");
		pm_wakeup_event(mdev->dev, WAKELOCK_ACTIVE_TIME_MAX_MS);
	}

	return 0;
}

void mtk_pm_complete(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	bool is_smart_suspend = true;
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;

	mdev = pci_get_drvdata(pdev);
	pm = mdev_get_pm(mdev);

	if (must_smart_suspend) {
		mtk_pm_entity_complete(mdev, is_smart_suspend);
		mtk_fsm_start(mdev);
		must_smart_suspend = false;
		if (pm->smart_rpm_resume) {
			MTK_INFO(mdev, "REROOT_INT wake up smart suspend!");
			pm_runtime_resume(mdev->dev);
			pm->smart_rpm_resume = false;
		}
	}
}

int mtk_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;
	int ret;

	mdev = pci_get_drvdata(pdev);
	pm = mdev_get_pm(mdev);

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
	md2ap_wakemon_suspend(mdev->google);
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	if (mtk_pcimsg_pci_user_is_busy(mdev, true)) {
#else
	if (mtk_pcimsg_pci_user_is_busy(mdev)) {
#endif
		MTK_INFO(mdev, "pci user is busy.\n");
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		/* This prevents the PCI subsystem from sending us to D3. */
		pci_save_state(pdev);
		ap_awake_gpio_set(0);
		return 0;
#else
		dev_pm_set_driver_flags(mdev->dev, DPM_FLAG_SMART_SUSPEND);
		/* Ensure RPM is disabled before setting runtime_status! */
		__pm_runtime_disable(dev, false);
		ret = __pm_runtime_set_status(dev, RPM_SUSPENDED);
		if (ret)
			MTK_ERR(mdev, "Set runtime status fail! ret=%d, status=%d, depth=%d\n",
				ret, dev->power.runtime_status, dev->power.disable_depth);

		return ret;
#endif
	}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	ret = remote_wakeup_enable(mdev->google);
	if (ret)
		return ret;

	ret = mtk_pm_suspend_device(mdev, false);
	if (ret)
		remote_wakeup_disable(mdev->google);

	if (!ret)
		ap_awake_gpio_set(0);

	return ret;
#else
	return mtk_pm_suspend_device(mdev, false);
#endif
}

int mtk_pm_resume(struct device *dev, bool atr_init)
{
	struct mtk_md_dev *mdev;
	struct pci_dev *pdev;
	int ret;

	pdev = to_pci_dev(dev);
	mdev = pci_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
	md2ap_wakemon_resume(mdev->google);
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	if (mtk_pcimsg_pci_user_is_busy(mdev, false)) {
#else
	if (mtk_pcimsg_pci_user_is_busy(mdev)) {
#endif
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
		md2ap_wakemon_resume_link_on(mdev->google, mtk_google_get_active_irqs(mdev));
#endif /* CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR */
		pci_restore_state(pdev);
		(void)ret; /* Suppress unused warning */
		ap_awake_gpio_set(1);
		return 0;
#else
		dev_pm_set_driver_flags(mdev->dev, 0);
		ret = __pm_runtime_set_status(dev, RPM_ACTIVE);
		if (ret)
			MTK_ERR(mdev, "Set runtime status fail! ret = %d, status = %d\n",
				ret, dev->power.runtime_status);
		pm_runtime_enable(dev);
		MTK_INFO(mdev, "pci user is busy. disable_depth=%d\n", dev->power.disable_depth);

		return ret;
#endif
	}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	remote_wakeup_disable(mdev->google);
	ap_awake_gpio_set(1);
#endif

	return mtk_pm_resume_device(mdev, false, atr_init);
}

int mtk_pm_freeze(struct device *dev)
{
	struct mtk_md_dev *mdev;
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);
	mdev = pci_get_drvdata(pdev);

	MTK_INFO(mdev, "Enter freeze.\n");

	return mtk_pm_suspend_device(mdev, false);
}

int mtk_pm_restore(struct device *dev, bool atr_init)
{
	struct mtk_md_dev *mdev;
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);
	mdev = pci_get_drvdata(pdev);
	MTK_INFO(mdev, "Enter restore.\n");

	return mtk_pm_resume_device(mdev, false, atr_init);
}

static void mtk_pm_pme_setting(struct mtk_md_dev *mdev, bool is_enable)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	int pmcsr_pos = 0;
	u16 pmcsr;

	if (pdev->pm_cap && pm->rpm_link_state == RPM_LINK_STATE_L2) {
		if (is_enable) {
			pdev->pme_support = pm->pme_support;
		} else {
			pmcsr_pos = pdev->pm_cap + PCI_PM_CTRL;
			pci_read_config_word(pdev, pmcsr_pos, &pmcsr);
			pmcsr |= PCI_PM_CTRL_PME_STATUS;
			pmcsr &= ~PCI_PM_CTRL_PME_ENABLE;
			pci_write_config_word(pdev, pmcsr_pos, pmcsr);
			/* Clear pme_support to avoid kernel re-enabling PME# */
			pm->pme_support = pdev->pme_support;
			pdev->pme_support = 0;
		}
	}
}

int mtk_pm_runtime_suspend(struct device *dev)
{
	struct mtk_md_dev *mdev;
	struct pci_dev *pdev;
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	int ret;
#endif

	pdev = to_pci_dev(dev);
	mdev = pci_get_drvdata(pdev);
	mtk_pm_pme_setting(mdev, false);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	ret = remote_wakeup_enable(mdev->google);
	if (ret)
		return ret;

	ret = mtk_pm_suspend_device(mdev, true);
	if (ret)
		remote_wakeup_disable(mdev->google);

	return ret;
#else
	return mtk_pm_suspend_device(mdev, true);
#endif
}

int mtk_pm_runtime_resume(struct device *dev, bool atr_init)
{
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;
	struct pci_dev *pdev;
	int i, stat, cnt = 0;

	pdev = to_pci_dev(dev);
	mdev = pci_get_drvdata(pdev);
	pm = mdev_get_pm(mdev);

	for (i = 0; i < MTK_USER_MAX; i++) {
		stat = atomic_read(&pm->pm_dbg.stat_val[PM_RUNTIME_USAGE][i]);
		if (stat)
			MTK_INFO(mdev, "RPM user:%d, stat:%d\n", i, stat);
		else
			cnt++;
	}
	mtk_pm_pme_setting(mdev, true);

	if (cnt == MTK_USER_MAX)
		MTK_INFO(mdev, "Last get async by user %d at %10llu\n",
			 pm->pm_dbg.last_get_async_user, pm->pm_dbg.last_get_async_time);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	remote_wakeup_disable(mdev->google);
#endif

	return mtk_pm_resume_device(mdev, true, atr_init);
}

int mtk_pm_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev;

	mdev = pci_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	MTK_DBG_PM_RT_IDLE(mdev, runtime_idle_delay_ms);
	pm_schedule_suspend(dev, runtime_idle_delay_ms);
#else
	MTK_DBG_PM_RT_IDLE(mdev, runtime_idle_delay_seconds);
	pm_schedule_suspend(dev, runtime_idle_delay_seconds * MSEC_PER_SEC);
#endif

	return -EBUSY;
}

void mtk_pm_shutdown(struct mtk_md_dev *mdev)
{
	MTK_INFO(mdev, "Enter shutdown\n");
	mtk_pm_suspend_device(mdev, false);
}

int mtk_pm_runtime_get(struct mtk_md_dev *mdev, enum mtk_user_id user, bool sync)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	if (unlikely(user >= MTK_USER_MAX))
		return -EINVAL;

	atomic_inc(&pm->pm_dbg.stat_val[PM_RUNTIME_USAGE][user]);
	atomic_inc(&pm->pm_dbg.rpm_get_total[user]);

	if (sync)
		return pm_runtime_get_sync(mdev->dev);

	pm->pm_dbg.last_get_async_user = user;
	pm->pm_dbg.last_get_async_time = local_clock();

	return pm_runtime_get(mdev->dev);
}

int mtk_pm_runtime_put(struct mtk_md_dev *mdev, enum mtk_user_id user, bool sync)
{
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);

	if (unlikely(user >= MTK_USER_MAX))
		return -EINVAL;

	atomic_dec(&pm->pm_dbg.stat_val[PM_RUNTIME_USAGE][user]);
	atomic_inc(&pm->pm_dbg.rpm_put_total[user]);

	if (sync)
		return pm_runtime_put_sync(mdev->dev);

	return pm_runtime_put(mdev->dev);
}

static void mtk_pm_fsm_state_pre_handler(struct mtk_fsm_param *fsm_param, void *data)
{
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;

	pm = data;
	mdev = pm->mdev;
	switch (fsm_param->to) {
	case FSM_STATE_OFF:
		if (mtk_pci_mmio_check(mdev)) {
			mtk_pm_reinit(mdev);
			mtk_pm_try_lock_l1ss_ds(mdev, false);
		} else if (!test_bit(PM_PREVENT_SUSPEND, &pm->state)) {
			set_bit(PM_PREVENT_SUSPEND, &pm->state);
			pm_runtime_get_noresume(mdev->dev);
		}
		cancel_delayed_work_sync(&pm->resume_work);
		break;
	case FSM_STATE_BOOTUP:
		if (fsm_param->fsm_flag & FSM_F_MD_REBOOT) {
			mtk_pm_reinit(mdev);
			mtk_pm_try_lock_l1ss_ds(mdev, false);
		}
		break;
	case FSM_STATE_EXCEPTION:
		if (fsm_param->fsm_flag == FSM_F_MDEE_INIT ||
		    fsm_param->fsm_flag == FSM_F_EXCEPT_INT) {
			mtk_pm_reinit(mdev);
			mtk_pm_try_lock_l1ss_ds(mdev, false);
		}

		if (fsm_param->fsm_flag == FSM_F_LINK_EXCEPTION)
			mtk_pm_reinit(mdev);
		break;
	default:
		break;
	}
}

static void mtk_pm_fsm_state_post_handler(struct mtk_fsm_param *fsm_param, void *data)
{
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;

	pm = data;
	mdev = pm->mdev;
	switch (fsm_param->to) {
	case FSM_STATE_ON:
		if (fsm_param->evt_id == FSM_EVT_REINIT) {
			mtk_pm_reinit(mdev);
			mtk_pm_try_lock_l1ss_ds(mdev, false);
		}
		break;
	case FSM_STATE_BOOTUP:
		if (fsm_param->fsm_flag & FSM_F_SAP_HS2_DONE)
			set_bit(PM_NEED_SUSPEND_SAP, &pm->md_pm_flag);
		break;
	case FSM_STATE_READY:
		mtk_pm_init_late(mdev);
		break;
	case FSM_STATE_OFF:
		mtk_pm_reset(mdev);
		break;
	case FSM_STATE_EXCEPTION:
		cancel_delayed_work_sync(&pm->ds_unlock_work);
		cancel_delayed_work_sync(&pm->resume_work);
		if (pm->force_d3l2_init)
			cancel_work_sync(&pm->pewake_work);
		break;
	default:
		break;
	}
}

static int mtk_pm_irq_handler(int irq_id, void *data)
{
	bool pm_sw_irq_en = true;
	struct mtk_md_dev *mdev;
	struct mtk_pci_pm *pm;

	pm = data;
	mdev = pm->mdev;
	trace_mtk_pm_receive_wakeup_irq_from_md(pm_sw_irq_en);
	mtk_pci_clear_sw_evt(mdev, D2H_SW_EVT_PM_LOCK_ACK);
	mtk_pci_clear_irq(mdev, irq_id);
	complete_all(&pm->ds_lock_complete);
	mtk_pci_unmask_irq(mdev, irq_id);

	return IRQ_HANDLED;
}

static int mtk_pm_ext_evt_handler(u32 status, void *data)
{
	struct mtk_pci_pm *pm = (struct mtk_pci_pm *)data;
	struct mtk_md_dev *mdev = pm->mdev;
	int ret;

	if (status & EXT_EVT_D2H_PCIE_DS_LOCK_ACK) {
		MTK_INFO(mdev, "EXT_EVT_D2H_PCIE_DS_LOCK_ACK received, ds_lock_recv = %d!\n",
			 pm->ds_lock_recv++);
		complete_all(&pm->ds_lock_complete);
	}

	if (status & EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK) {
		MTK_INFO(mdev, "EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK received! md_ack_user = 0x%x\n",
			 mtk_pci_get_md_ack_user(mdev));
		complete_all(&pm->pm_ack);
	}

	if (status & EXT_EVT_D2H_PCIE_PM_RESUME_ACK) {
		MTK_INFO(mdev, "EXT_EVT_D2H_PCIE_PM_RESUME_ACK received!, reg_1/5:0x%x, 0x%x\n",
			 mtk_pci_get_md_ack_user(mdev), mtk_pci_get_resume_user(mdev));
		complete_all(&pm->pm_ack);
	}

	if (status & EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK_AP) {
		MTK_INFO(mdev, "EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK_AP received!\n");
		complete_all(&pm->pm_ack_sap);
	}

	if (status & EXT_EVT_D2H_PCIE_PM_RESUME_ACK_AP) {
		MTK_INFO(mdev, "EXT_EVT_D2H_PCIE_PM_RESUME_ACK_AP received!\n");
		complete_all(&pm->pm_ack_sap);
	}

	if (status & EXT_EVT_D2H_SOFT_OFF_NOTIFY) {
		MTK_INFO(mdev, "EXT_EVT_D2H_SOFT_OFF_NOTIFY received!\n");
		ret = mtk_fsm_evt_submit(mdev, FSM_EVT_SOFT_OFF, 0, NULL, 0, 0);
		if (ret)
			MTK_ERR(mdev, "Failed to submit SOFT OFF event: %d.\n", ret);
	}

	mtk_pci_clear_ext_evt(mdev, status);
	mtk_pci_unmask_ext_evt(mdev, status);

	return IRQ_HANDLED;
}

/**
 * mtk_pm_init - Initialize pm fields of struct mtk_md_dev.
 * @mdev: pointer to mtk_md_dev
 *
 * This function initializes pm fields of struct mtk_md_dev,
 * after that the driver is capable of performing pm related
 * functions.
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *pci_hw;
	struct mtk_pci_pm *pm;
	int irq_id = -1;
	u32 reg, cfg;
	int ret;

	pm = devm_kzalloc(mdev->dev, sizeof(*pm), GFP_KERNEL);
	if (!pm) {
		MTK_ERR(mdev, "Fail to allocate pm!\n");
		return -ENOMEM;
	}

	pm->mdev = mdev;

	if (!mdev->hw_priv) {
		MTK_ERR(mdev, "Invalid parameter hw_priv is NULL!\n");
		devm_kfree(mdev->dev, pm);
		return -EINVAL;
	}

	reg = mtk_pci_get_dev_state(mdev);
	cfg = reg >> FIRMWARE_CFG_SHIFT & 0x1;
	if (cfg)
		pm->rpm_link_state = RPM_LINK_STATE_L2;
	else
		pm->rpm_link_state = RPM_LINK_STATE_INVALID;

	pci_hw = mdev->hw_priv;
	pci_hw->pm = pm;

	INIT_LIST_HEAD(&pm->entities);
	spin_lock_init(&pm->ds_spinlock);
	mutex_init(&pm->entity_mtx);

	init_completion(&pm->ds_lock_complete);
	init_completion(&pm->pm_ack);
	init_completion(&pm->pm_ack_sap);

	INIT_DELAYED_WORK(&pm->ds_unlock_work, mtk_pm_ds_unlock_work);
	INIT_DELAYED_WORK(&pm->resume_work, mtk_pm_resume_work);

	pm->cfg.ds_lock_wait_timeout_ms = DS_LOCK_WAIT_TIMEOUT_MS;
	pm->cfg.suspend_wait_timeout_ms = SUSPEND_WAIT_TIMEOUT_MD_MS;
	pm->cfg.resume_wait_timeout_ms = RESUME_WAIT_TIMEOUT_MD_MS;
	pm->cfg.suspend_wait_timeout_sap_ms = SUSPEND_WAIT_TIMEOUT_SAP_MS;
	pm->cfg.resume_wait_timeout_sap_ms = RESUME_WAIT_TIMEOUT_SAP_MS;
	pm->cfg.ds_lock_polling_max_us = DS_LOCK_POLLING_MAX_US;
	pm->cfg.ds_lock_polling_min_us = DS_LOCK_POLLING_MIN_US;
	pm->cfg.ds_lock_polling_interval_us = DS_LOCK_POLLING_INTERVAL_US;
	pm->cfg.ds_lock_check_bitmask = pci_hw->cfg->ds_lock_check_bitmask;
	pm->cfg.ds_lock_check_val = pci_hw->cfg->ds_lock_check_val;

	mtk_pm_try_lock_l1ss_ds(mdev, false);
	set_bit(PM_PREVENT_SUSPEND, &pm->state);
	mtk_pm_debug_init(mdev);
	mtk_pm_dbgfs_init(mdev);
	device_init_wakeup(mdev->dev, true);

	/* register sw irq for ds lock. */
	if (pci_hw->cfg->flag & MTK_CFG_PM_SW_IRQ) {
		irq_id = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_PM_LOCK);
		if (irq_id < 0) {
			MTK_ERR(mdev, "Failed to allocate Irq id!\n");
			ret = -EFAULT;
			goto err_free_dev;
		}

		ret = mtk_pci_register_irq(mdev, irq_id, mtk_pm_irq_handler, pm);
		if (ret) {
			MTK_ERR(mdev, "Failed to register irq!\n");
			goto err_free_dev;
		}
		mtk_pci_unmask_irq(mdev, irq_id);
		pm->irq_id = irq_id;
	}

	/* register mhccif interrupt handler. */
	pm->ext_evt_chs = EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK |
			  EXT_EVT_D2H_PCIE_PM_RESUME_ACK |
			  EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK_AP |
			  EXT_EVT_D2H_PCIE_PM_RESUME_ACK_AP |
			  EXT_EVT_D2H_PCIE_DS_LOCK_ACK |
			  EXT_EVT_D2H_SOFT_OFF_NOTIFY;

	ret = mtk_pci_register_ext_evt(mdev, pm->ext_evt_chs, mtk_pm_ext_evt_handler, pm);
	if (ret) {
		MTK_ERR(mdev, "Failed to register ext event!\n");
		goto err_reg_ext_evt;
	}

	/* register fsm notify callback */
	ret = mtk_fsm_notifier_register(mdev, MTK_USER_PM,
					mtk_pm_fsm_state_pre_handler, pm, FSM_PRIO_1, true);
	if (ret) {
		MTK_ERR(mdev, "Failed to register fsm pre notifier!\n");
		goto err_reg_fsm_pre_notifier;
	}

	ret = mtk_fsm_notifier_register(mdev, MTK_USER_PM,
					mtk_pm_fsm_state_post_handler, pm, FSM_PRIO_0, false);
	if (ret) {
		MTK_ERR(mdev, "Failed to register fsm  post notifier!\n");
		goto err_reg_fsm_post_notifier;
	}

	ret = mtk_pm_stats_init(mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to register pm stats init\n");
		goto err_pm_stats_free;
	}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	// TODO: b/473984596 - Workaround to lock DS during VoLTE/VoNR.
	struct mtk_pci_user_pm_ops ops = {
		.lock = mtk_pm_ds_lock,
		.unlock = mtk_pm_ds_unlock,
		.wait_complete = mtk_pm_ds_wait_complete,
	};
	mtk_pci_user_register_pm_ops(&ops);
#endif

	return 0;

err_pm_stats_free:
	mtk_pm_stats_exit(mdev);
err_reg_fsm_post_notifier:
	mtk_fsm_notifier_unregister(mdev, MTK_USER_PM);
err_reg_fsm_pre_notifier:
	mtk_pci_unregister_ext_evt(mdev, pm->ext_evt_chs);
err_reg_ext_evt:
	if (irq_id >= 0)
		mtk_pci_unregister_irq(mdev, irq_id);
err_free_dev:
	device_init_wakeup(mdev->dev, false);
	devm_kfree(mdev->dev, pm);
	return ret;
}

/**
 * mtk_pm_exit_early - Acquire device ds lock at the beginning
 *                     of driver exit routine.
 * @mdev: pointer to mtk_md_dev
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_exit_early(struct mtk_md_dev *mdev)
{
	/* In kernel device_del, system pm is already removed from pm entry list
	 * and runtime pm is forbidden as well, thus no need to disable
	 * PM here.
	 */

	return mtk_pm_try_lock_l1ss_ds(mdev, false);
}

/**
 * mtk_pm_exit - PM exit cleanup routine.
 * @mdev: pointer to mtk_md_dev
 *
 * Return: return value is 0 on success, a negative error
 * code on failure.
 */
int mtk_pm_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *pci_hw;
	struct mtk_pci_pm *pm;

	if (!mdev)
		return -EINVAL;

	pm = mdev_get_pm(mdev);
	pci_hw = mdev->hw_priv;

	mtk_pm_stats_exit(mdev);

	cancel_delayed_work_sync(&pm->ds_unlock_work);
	cancel_delayed_work_sync(&pm->resume_work);
	if (pm->force_d3l2_init) {
		cancel_work_sync(&pm->pewake_work);
		device_remove_file(mdev->dev, &dev_attr_exit_d3l2);
	}

	mtk_fsm_notifier_unregister(mdev, MTK_USER_PM);
	device_init_wakeup(mdev->dev, false);
	mtk_pci_unregister_ext_evt(mdev, pm->ext_evt_chs);

	if (pci_hw->cfg->flag & MTK_CFG_PM_SW_IRQ)
		mtk_pci_unregister_irq(mdev, pm->irq_id);

	mtk_pm_dbgfs_exit(mdev);
	devm_kfree(mdev->dev, pm);
	return 0;
}

module_param(d3l2_force_dsw, bool, 0644);
MODULE_PARM_DESC(d3l2_force_dsw, "Force send _DSW in driver");

module_param(ds_delayed_unlock_timeout_ms, ushort, 0644);
MODULE_PARM_DESC(ds_delayed_unlock_timeout_ms, "Ds unlock work delay time");

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
module_param(runtime_idle_delay_ms, uint, 0644);
MODULE_PARM_DESC(runtime_idle_delay_ms, "RPM idle delay time");
#else
module_param(runtime_idle_delay_seconds, ushort, 0644);
MODULE_PARM_DESC(runtime_idle_delay_seconds, "RPM idle delay time");
#endif

module_param(force_skip_ds_lock, bool, 0644);
MODULE_PARM_DESC(force_skip_ds_lock, "Force skip ds lock");

module_param(force_skip_enter_d3l2, bool, 0644);
MODULE_PARM_DESC(force_skip_enter_d3l2, "Force skip enter d3l2");

module_param(smart_suspend_enabled, bool, 0644);
MODULE_PARM_DESC(smart_suspend_enabled, "Enable smart suspend feature");
