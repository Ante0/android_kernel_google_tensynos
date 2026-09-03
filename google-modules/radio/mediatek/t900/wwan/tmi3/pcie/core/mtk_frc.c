// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2024, MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/pci.h>
#include <linux/sched/clock.h>

#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_frc.h"
#include "mtk_fsm.h"
#include "mtk_pci.h"
#include "mtk_pci_reg.h"
#include "mtk_pcie_trace.h"
#ifdef CONFIG_UT_PCIE_FRC_SYNC
#include "ut_frc_fake.h"
#endif

#define mdev_get_pm(mdev) (((struct mtk_pci_priv *)((mdev)->hw_priv))->pm)

#define TAG			"FRC"
#define LOCK_L1SS_RETRY_COUNT	50
#define LOCK_L1SS_DELAY		100
#define MTK_FRC_SYNC_ENABLE		(0)

static uint frc_sync_period = FRC_SYNC_PERIOD;

static int mtk_frc_sync(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_pci_pm *pm = mdev_get_pm(mdev);
	struct mtk_md_frc *frc = priv->frc;
	int loop, res, ret = 0;
	unsigned long flags;
	u64 host_ts_diff;
	u32 reg;

	/* Lock PCIe to L0 */
	mtk_pci_disable_l1ss_ds(mdev, L1SS_BIT_L0S_L1(L1SS_MD_FRC), MAC_ACTIVE_MD_FRC);
	for (loop = 0; loop < LOCK_L1SS_RETRY_COUNT; loop++) {
		reg = mtk_pci_get_ds_status(mdev);
		if ((reg & pm->cfg.ds_lock_check_bitmask) == pm->cfg.ds_lock_check_val) {
			reg = 0;
			break;
		}
		udelay(LOCK_L1SS_DELAY);
	}
	if (reg) {
		MTK_ERR(mdev, "failed to lock PCIE to L0: 0x%x\n", reg);
		ret = -EBUSY;
		goto err_lock_l0;
	}

	reinit_completion(&frc->frc_complete);

	/* Trigger PCIe SW interrupt to enter FRC sync start flow and capture FRC */
	mtk_pci_send_sw_evt(mdev, H2D_SW_EVT_FRC_SYNC_START);

	/* wait for ack interrupt */
	res = wait_for_completion_timeout(&frc->frc_complete, msecs_to_jiffies(200));
	if (!res) {
		MTK_ERR(mdev, "FRC capture timeout, res: 0x%x\n", res);
		ret = -ETIMEDOUT;
		goto err_curr_frc;
	}

	/* Read MHCCIF register to get FRC raw value */
	frc->raw_md_frc = mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr
					+ MHCCIF_EP2RC_SPARE_REG_3);

	local_irq_save(flags);

	/* Snapshot host timestamp before stopping L0 counter */
	/* use ktime_get_ns(), a higher resolution clock, in */
	/* replace of local_clock(), which uses jiffies */
	frc->curr_host_ts = ktime_get_ns();

	/* Stop L0 counter */
	mtk_pci_mac_write32(priv, REG_PWR_PROFILE_SETTING, 0);

	/* "Read after write" to make sure write was completed */
	mtk_pci_mac_read32(priv, REG_PWR_PROFILE_SETTING);

	/* Snapshot host timestamp again after stopping L0 counter and interpolate to estimate */
	/* the host timestamp when L0 counter stops; stopping L0 counter may take 3 to 5 us */
	/* and this trick help eliminate the variance of the time it takes */
	host_ts_diff = ktime_get_ns() - frc->curr_host_ts;

	/* Snapshot local clock for log usage */
	frc->curr_host_ts_local = local_clock();

	/* Get L0 counter value */
	frc->l0_count = mtk_pci_mac_read32(priv, REG_PWR_PROFILE_L0_STATE_CNT);

	/* Trigger PCIe SW interrupt to enter FRC sync end flow */
	mtk_pci_send_sw_evt(mdev, H2D_SW_EVT_FRC_SYNC_END);

	/* Unlock PCIe L0 */
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L0S_L1(L1SS_MD_FRC), MAC_ACTIVE_MD_FRC);

	local_irq_restore(flags);

	/* Calculate MD FRC when stopping L0 counter */
	frc->curr_md_frc = frc->raw_md_frc + ((frc->l0_count + 13) / 26);

	/* Calculate host timestamp when stopping L0 counter */
	frc->curr_host_ts += host_ts_diff / 2;

	frc->curr_host_ts_local -= host_ts_diff / 2;

	return 0;

err_curr_frc:
	/* Trigger PCIe SW interrupt to enter FRC sync end flow */
	mtk_pci_send_sw_evt(mdev, H2D_SW_EVT_FRC_SYNC_END);
err_lock_l0:
	/* Unlock PCIe L0 */
	mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L0S_L1(L1SS_MD_FRC), MAC_ACTIVE_MD_FRC);

	return ret;
}

static ssize_t time_sync_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	u64 host_ts = ktime_get_ns();
	u32 md_frc;
	int ret;
	/* Time sync between MD FRC and host timestamp by FRC sync timer */
	ret = mtk_frc_get_dev_us_by_host_ts(mdev, host_ts, &md_frc);
	if (ret)
		return sprintf(buf, "Time sync fail, reason: %d", ret);

	return sprintf(buf, "Host timestamp: %llu, MD FRC: %u\n", host_ts, md_frc);
}

static DEVICE_ATTR_RO(time_sync);

static int mtk_frc_cb(u32 status, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;
	struct mtk_md_frc *frc;

	priv = mdev->hw_priv;
	frc = priv->frc;

	complete_all(&frc->frc_complete);
	mtk_pci_clear_ext_evt(mdev, status);
	mtk_pci_unmask_ext_evt(mdev, status);
	return 0;
}

static u32 mtk_frc_get_diff(u32 start, u32 end)
{
	u32 ret;

	if (end >= start)
		ret = end - start;
	else
		ret = 0xFFFFFFFF - start + end + 1;
	return ret;
}

int mtk_frc_get_host_ts_by_dev_us(struct mtk_md_dev *mdev, u32 md_frc, u64 *host_ts)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_frc_rec *old_rec, rec;
	struct mtk_md_frc *frc;
	u64 exp_host_ts_diff;
	u32 dev_diff;
	u8 dir = 0;

	if (!priv->frc) {
		MTK_INFO(mdev, "Unsupport FRC Feature\n");
		return -EINVAL;
	}

	frc = priv->frc;
	rcu_read_lock();
	old_rec = rcu_dereference(frc->frc_record);
	if (unlikely(!old_rec || !old_rec->cur_host_ts)) {
		rcu_read_unlock();
		MTK_ERR(mdev, "md_frc:%u, ret:%d\n", md_frc, -EPERM);
		return -EPERM;
	}
	memcpy(&rec, old_rec, sizeof(*old_rec));
	rcu_read_unlock();

	/* Check input MD FRC is within past/future 3 mins */
	dev_diff = mtk_frc_get_diff(rec.cur_dev_us, md_frc);
	if (dev_diff > CUR_DIFF_SEC * 1000000) {
		dev_diff = 0xffffffff - dev_diff + 1;
		/* Past MD FRC */
		if (dev_diff <= CUR_DIFF_SEC * 1000000) {
			dir = 1;
		} else {
			MTK_ERR(mdev, "Invalid input md_frc:%u\n", md_frc);
			return -EINVAL;
		}
	}

	/* Calculate the target host timestamp */
	exp_host_ts_diff = rec.host_ts_diff ?
		(rec.host_ts_diff * dev_diff / rec.dev_us_diff) :
		(dev_diff * 1000);
	*host_ts = dir ? (rec.cur_host_ts - exp_host_ts_diff) :
		(rec.cur_host_ts + exp_host_ts_diff);

	MTK_INFO(mdev, "md_frc:%u, host_ts:%llu\n", md_frc, *host_ts);
	return 0;
}

int mtk_frc_get_dev_us_by_host_ts(struct mtk_md_dev *mdev, u64 host_ts, u32 *md_frc)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_frc_rec *old_rec, rec;
	struct mtk_md_frc *frc;
	u32 exp_dev_us_diff;
	u64 host_diff;

	if (!priv->frc) {
		MTK_INFO(mdev, "Unsupport FRC Feature\n");
		return -EINVAL;
	}

	frc = priv->frc;
	rcu_read_lock();
	old_rec = rcu_dereference(frc->frc_record);
	if (unlikely(!old_rec || !old_rec->cur_host_ts)) {
		rcu_read_unlock();
		MTK_ERR(mdev, "host_ts:%llu, ret:%d\n", host_ts, -EPERM);
		return -EPERM;
	}
	memcpy(&rec, old_rec, sizeof(*old_rec));
	rcu_read_unlock();

	/* Check input host timestamp is within past/future 3 mins */
	host_diff = (host_ts >= rec.cur_host_ts) ?
		(host_ts - rec.cur_host_ts) :
		(rec.cur_host_ts - host_ts);
	if (host_diff > CUR_DIFF_SEC * 1000000000) {
		MTK_ERR(mdev, "Invalid input host_ts:%llu\n", host_ts);
		return -EINVAL;
	}

	/* Calculate the target MD FRC */
	exp_dev_us_diff = rec.host_ts_diff ?
		(host_diff * rec.dev_us_diff / rec.host_ts_diff) :
		(host_diff / 1000);
	*md_frc = (host_ts >= rec.cur_host_ts) ?
		(rec.cur_dev_us + exp_dev_us_diff) :
		(rec.cur_dev_us - exp_dev_us_diff);

	MTK_INFO(mdev, "host_ts:%llu, md_frc:%u\n", host_ts, *md_frc);
	return 0;
}

int mtk_frc_get_host_dur_by_dev_dur(struct mtk_md_dev *mdev, u32 md_dur, u64 *host_dur)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_frc_rec *old_rec, rec;
	struct mtk_md_frc *frc;

	if (!priv->frc) {
		MTK_INFO(mdev, "Unsupport FRC Feature\n");
		return -EINVAL;
	}

	frc = priv->frc;
	rcu_read_lock();
	old_rec = rcu_dereference(frc->frc_record);
	if (unlikely(!old_rec || !old_rec->cur_host_ts)) {
		rcu_read_unlock();
		MTK_ERR(mdev, "md_dur:%u, ret:%d\n", md_dur, -EPERM);
		return -EPERM;
	}
	memcpy(&rec, old_rec, sizeof(*old_rec));
	rcu_read_unlock();

	/* Calculate the target host duration */
	*host_dur = rec.host_ts_diff ?
		(rec.host_ts_diff * md_dur / rec.dev_us_diff) :
		(md_dur * 1000);

	MTK_INFO(mdev, "md_dur:%u, host_dur:%llu\n", md_dur, *host_dur);
	return 0;
}

void mtk_frc_check_and_sync(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_frc *frc;

	if (!priv->frc)
		return;

	frc = priv->frc;
	if (unlikely(atomic_cmpxchg(&frc->allow_sync, 1, 0)))
		queue_work(system_wq, &frc->sync_work);
}

static void mtk_frc_clr_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_md_frc *frc;

	frc = container_of(dwork, struct mtk_md_frc, clr_work);

	atomic_set(&frc->allow_sync, 1);
}

static void mtk_frc_sync_work(struct work_struct *work)
{
	struct mtk_md_frc *frc = container_of(work, struct mtk_md_frc, sync_work);
	u32 new_dev_us_diff = 0, exp_dev_us_diff, diff;
	struct mtk_md_frc_rec *new_rec, *cur_rec;
	u64 new_host_ts_diff = 0;
	struct mtk_md_dev *mdev;
	u8 retry_cnt = 0;
	int ret;

	mdev = frc->mdev;
	cur_rec = rcu_dereference(frc->frc_record);

	do {
		ret = mtk_frc_sync(mdev);
		if (ret) {
			/* If capture FRC fail, reset FRC related record */
			frc->curr_host_ts = 0;
			frc->curr_md_frc = 0;
			frc->curr_host_ts_local = 0;
			break;
		}

		if (unlikely(!cur_rec || !cur_rec->cur_host_ts))
			break;

		new_host_ts_diff = frc->curr_host_ts - cur_rec->cur_host_ts;
		/* If too long since the last FRC capture, discard previous record */
		if (new_host_ts_diff > MAX_SYNC_PERIOD * 1000000000) {
			new_host_ts_diff = 0;
			break;
		}
		new_dev_us_diff = mtk_frc_get_diff(cur_rec->cur_dev_us, frc->curr_md_frc);

		if (unlikely(!cur_rec->host_ts_diff))
			break;
		exp_dev_us_diff = cur_rec->dev_us_diff * new_host_ts_diff / cur_rec->host_ts_diff;
		/* Calculate and check the time difference */
		diff = exp_dev_us_diff > new_dev_us_diff ?
			exp_dev_us_diff - new_dev_us_diff :
			new_dev_us_diff - exp_dev_us_diff;
		if (diff <= new_host_ts_diff >> 29)
			break;
	} while (retry_cnt++ < 2);
	trace_mtk_frc(frc->curr_host_ts, frc->curr_host_ts_local,
		      frc->raw_md_frc, frc->curr_md_frc, frc->l0_count);
	MTK_INFO(mdev, "TS: %10llu, Local TS: %10llu, Raw FRC: %u, Final FRC: %u, L0: %u\n",
		 frc->curr_host_ts, frc->curr_host_ts_local, frc->raw_md_frc,
		 frc->curr_md_frc, frc->l0_count);

	new_rec = devm_kzalloc(mdev->dev, sizeof(*new_rec), GFP_KERNEL);
	if (!new_rec) {
		MTK_ERR(mdev, "Failed to allocate new_rec\n");
		goto end_frc_sync;
	}

	new_rec->cur_host_ts = frc->curr_host_ts;
	new_rec->cur_dev_us = frc->curr_md_frc;
	new_rec->host_ts_diff = new_host_ts_diff;
	new_rec->dev_us_diff = new_dev_us_diff;
	/* Update the above variables simultaneously */
	rcu_assign_pointer(frc->frc_record, new_rec);
	if (likely(cur_rec)) {
		synchronize_rcu();
		devm_kfree(mdev->dev, cur_rec);
	}

end_frc_sync:
	queue_delayed_work(system_wq, &frc->clr_work, frc_sync_period * HZ);
}

static void mtk_frc_sync_enable(struct mtk_md_frc *frc)
{
	struct mtk_md_dev *mdev = frc->mdev;

#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (!mtk_pci_link_check_silent(frc->mdev))
		return;
#endif

	if (test_bit(MTK_FRC_SYNC_ENABLE, &frc->flag))
		return;

	if (device_create_file(mdev->dev, &dev_attr_time_sync))
		MTK_WARN(mdev, "Unable to create frc sysfs entry\n");

	mtk_pci_unmask_ext_evt(mdev, EXT_EVT_D2H_FRC_DONE_NOTIFY);
	set_bit(MTK_FRC_SYNC_ENABLE, &frc->flag);
	atomic_set(&frc->allow_sync, 1);
	MTK_INFO(mdev, "FRC Feature enable done\n");
	mtk_frc_check_and_sync(mdev);
}

static void mtk_frc_sync_disable(struct mtk_md_frc *frc)
{
	struct mtk_md_dev *mdev = frc->mdev;
	struct mtk_md_frc_rec *rec;

	if (!test_bit(MTK_FRC_SYNC_ENABLE, &frc->flag))
		return;

	device_remove_file(mdev->dev, &dev_attr_time_sync);
	cancel_work_sync(&frc->sync_work);
	cancel_delayed_work_sync(&frc->clr_work);
	atomic_set(&frc->allow_sync, 0);

	/* Clear the FRC record */
	rec = rcu_dereference(frc->frc_record);
	if (likely(rec)) {
		rcu_assign_pointer(frc->frc_record, NULL);
		synchronize_rcu();
		devm_kfree(mdev->dev, rec);
	}
	mtk_pci_mask_ext_evt(mdev, EXT_EVT_D2H_FRC_DONE_NOTIFY);
	clear_bit(MTK_FRC_SYNC_ENABLE, &frc->flag);
	MTK_INFO(mdev, "FRC Feature disable done\n");
}

static void mtk_frc_fsm_handler(struct mtk_fsm_param *param, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;
	struct mtk_md_frc *frc;

	priv = mdev->hw_priv;
	frc = priv->frc;

	switch (param->to) {
	case FSM_STATE_READY:
		mtk_frc_sync_enable(frc);
		break;
	case FSM_STATE_BOOTUP:
		if (param->fsm_flag & FSM_F_MD_REBOOT)
			mtk_frc_sync_disable(frc);
		break;
	case FSM_STATE_EXCEPTION:
	case FSM_STATE_OFF:
		mtk_frc_sync_disable(frc);
		break;
	default:
		break;
	}
}

static int mtk_frc_suspend(struct mtk_md_dev *mdev, void *param, bool is_runtime)
{
	mtk_frc_sync_disable(param);
	return 0;
}

static int mtk_frc_resume(struct mtk_md_dev *mdev, void *param, bool is_runtime, bool link_ready)
{
	mtk_frc_sync_enable(param);
	return 0;
}

static int mtk_frc_pm_init(struct mtk_md_frc *frc)
{
	struct mtk_pm_entity *pm_entity;
	int ret;

	pm_entity = &frc->pm_entity;
	INIT_LIST_HEAD(&pm_entity->entry);
	pm_entity->user = MTK_USER_FRC;
	pm_entity->param = frc;
	pm_entity->suspend = &mtk_frc_suspend;
	pm_entity->resume = &mtk_frc_resume;

	ret = mtk_pm_entity_register(frc->mdev, pm_entity);
	if (ret < 0)
		MTK_ERR(frc->mdev, "Failed to register frc pm_entity\n");

	return ret;
}

static int mtk_frc_pm_exit(struct mtk_md_frc *frc)
{
	int ret;

	ret = mtk_pm_entity_unregister(frc->mdev, &frc->pm_entity);
	if (ret < 0)
		MTK_ERR(frc->mdev, "Failed to unregister frc pm_entity\n");

	return ret;
}

int mtk_frc_sync_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_frc *frc;
	int ret = 0;

	if (!priv->cfg || !(priv->cfg->flag & MTK_CFG_FRC_SYNC)) {
		MTK_INFO(mdev, "Unsupport FRC Feature\n");
		return 0;
	}

	frc = devm_kzalloc(mdev->dev, sizeof(*frc), GFP_KERNEL);
	if (!frc) {
		MTK_ERR(mdev, "Failed to allocate frc\n");
		return -ENOMEM;
	}
	priv->frc = frc;
	frc->mdev = mdev;

	ret = mtk_pci_register_ext_evt(mdev, EXT_EVT_D2H_FRC_DONE_NOTIFY, mtk_frc_cb, mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to register frc ext event\n");
		goto free_frc;
	}

	ret = mtk_fsm_notifier_register(mdev, MTK_USER_FRC,
					mtk_frc_fsm_handler, mdev, FSM_PRIO_0, false);
	if (ret < 0) {
		MTK_ERR(mdev, "Failed to register frc fsm notifier\n");
		goto free_sw_intr;
	}

	ret = mtk_frc_pm_init(frc);
	if (ret < 0)
		goto free_fsm_notify;

	init_completion(&frc->frc_complete);
	INIT_WORK(&frc->sync_work, mtk_frc_sync_work);
	INIT_DELAYED_WORK(&frc->clr_work, mtk_frc_clr_work);
	atomic_set(&frc->allow_sync, 0);
	MTK_INFO(mdev, "FRC sync init done\n");

	return 0;

free_fsm_notify:
	mtk_fsm_notifier_unregister(mdev, MTK_USER_FRC);
free_sw_intr:
	mtk_pci_unregister_ext_evt(mdev, EXT_EVT_D2H_FRC_DONE_NOTIFY);
free_frc:
	devm_kfree(mdev->dev, frc);
	priv->frc = NULL;
	return ret;
}

int mtk_frc_sync_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_frc *frc;

	if (!priv->frc)
		return -EINVAL;

	frc = priv->frc;
	mtk_frc_pm_exit(frc);
	mtk_fsm_notifier_unregister(mdev, MTK_USER_FRC);
	mtk_pci_unregister_ext_evt(mdev, EXT_EVT_D2H_FRC_DONE_NOTIFY);
	complete_all(&frc->frc_complete);
	cancel_work_sync(&frc->sync_work);
	cancel_delayed_work_sync(&frc->clr_work);
	devm_kfree(mdev->dev, frc);
	priv->frc = NULL;
	MTK_INFO(mdev, "FRC sync exit done\n");

	return 0;
}

module_param(frc_sync_period, uint, 0644);
MODULE_PARM_DESC(frc_sync_period, "FRC SYNC Period");
