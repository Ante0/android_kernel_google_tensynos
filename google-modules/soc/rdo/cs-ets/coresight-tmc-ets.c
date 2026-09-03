// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/atomic.h>
#include <linux/coresight.h>
#include <linux/types.h>
#include <linux/version.h>

#include "coresight-priv.h"
#include "coresight-tmc.h"

static void __tmc_ets_enable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI |
		       TMC_FFCR_FON_FLIN | TMC_FFCR_FON_TRIG_EVT |
		       TMC_FFCR_TRIGON_TRIGIN,
		       drvdata->base + TMC_FFCR);
	writel_relaxed(drvdata->trigger_cntr, drvdata->base + TMC_TRG);
	tmc_enable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static int tmc_ets_enable_hw(struct tmc_drvdata *drvdata,
			     struct etr_buf *etr_buf)
{
	int rc;

	rc = coresight_claim_device(drvdata->csdev);
	if (!rc)
		__tmc_ets_enable_hw(drvdata);

	return rc;
}

static void __tmc_ets_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);

	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

void tmc_ets_disable_hw(struct tmc_drvdata *drvdata)
{
	__tmc_ets_disable_hw(drvdata);
	coresight_disclaim_device(drvdata->csdev);
	/* Reset the ETR buf used by hardware */
	drvdata->etr_buf = NULL;
}

static int tmc_enable_ets_sink_sysfs(struct coresight_device *csdev)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * If we are enabling the ETR from disabled state, we need to make
	 * sure we have a buffer with the right size. The etr_buf is not reset
	 * immediately after we stop the tracing in SYSFS mode as we wait for
	 * the user to collect the data. We may be able to reuse the existing
	 * buffer, provided the size matches. Any allocation has to be done
	 * with the lock released.
	 */
	spin_lock_irqsave(&drvdata->spinlock, flags);

	/*
	 * In sysFS mode we can have multiple writers per sink.  Since this
	 * sink is already enabled no memory is needed and the HW need not be
	 * touched, even if the buffer size has changed.
	 */
	if (drvdata->mode == CS_MODE_SYSFS) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		atomic_inc(&csdev->refcnt);
#else
		atomic_inc(csdev->refcnt);
#endif
		goto out;
	}

	ret = tmc_ets_enable_hw(drvdata, drvdata->sysfs_buf);
	if (!ret) {
		drvdata->mode = CS_MODE_SYSFS;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		atomic_inc(&csdev->refcnt);
#else
		atomic_inc(csdev->refcnt);
#endif
	}
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	if (!ret)
		dev_dbg(&csdev->dev, "TMC-ETR enabled\n");

	return ret;
}

static int tmc_enable_ets_sink(struct coresight_device *csdev,
			       enum cs_mode mode, void *data)
{
	switch (mode) {
	case CS_MODE_SYSFS:
		return tmc_enable_ets_sink_sysfs(csdev);
	default:
		return -EINVAL;
	}
}

static int tmc_disable_ets_sink(struct coresight_device *csdev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);

	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	if (atomic_dec_return(&csdev->refcnt)) {
#else
	if (atomic_dec_return(csdev->refcnt)) {
#endif
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	/* Complain if we (somehow) got out of sync */
	WARN_ON_ONCE(drvdata->mode == CS_MODE_DISABLED);
	tmc_ets_disable_hw(drvdata);
	/* Dissociate from monitored process. */
	drvdata->pid = -1;
	drvdata->mode = CS_MODE_DISABLED;
	/* Reset perf specific data */
	drvdata->perf_buf = NULL;

	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return 0;
}

static const struct coresight_ops_sink tmc_ets_sink_ops = {
	.enable		= tmc_enable_ets_sink,
	.disable	= tmc_disable_ets_sink,
};

const struct coresight_ops tmc_ets_cs_ops = {
	.sink_ops	= &tmc_ets_sink_ops,
};
