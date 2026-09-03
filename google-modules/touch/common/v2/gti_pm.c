// SPDX-License-Identifier: GPL
/*
 * Google Touch Interface - Power Management
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "gti_pm.h"
#include "gti_log.h"

int gti_pm_wake_lock_nosync_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type,
				     bool skip_pm_resume)
{
	if ((pm == NULL) || !pm->enabled)
		return -ENODEV;

	mutex_lock(&pm->lock_mutex);

	if (pm->locks & type) {
		GOOG_DBG(pm, "unexpectedly lock: locks=0x%04X, type=0x%04X\n", pm->locks, type);
		mutex_unlock(&pm->lock_mutex);
		return -EINVAL;
	}

	/*
	 * If NON_WAKE_UP is set and the pm is suspend, we should ignore it.
	 * For example, IRQs should only keep the bus active. IRQs received
	 * while the pm is suspend should be ignored.
	 */
	if (skip_pm_resume && pm->locks == 0) {
		mutex_unlock(&pm->lock_mutex);
		return -EAGAIN;
	}

	pm->locks |= type;

	if (skip_pm_resume) {
		mutex_unlock(&pm->lock_mutex);
		return 0;
	}

	pm->new_state = GTI_PM_RESUME;
	pm->update_state = true;
	if (pm->event_wq != NULL)
		queue_work(pm->event_wq, &pm->state_update_work);
	mutex_unlock(&pm->lock_mutex);
	return 0;
}

int gti_pm_wake_lock_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type,
			      bool skip_pm_resume)
{
	int ret = 0;

	if ((pm == NULL) || !pm->enabled)
		return -ENODEV;

	ret = gti_pm_wake_lock_nosync_internal(pm, type, skip_pm_resume);
	if (ret < 0)
		return ret;
	if (pm->event_wq != NULL)
		flush_workqueue(pm->event_wq);
	return ret;
}

int gti_pm_wake_unlock_nosync_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type)
{
	int ret = 0;

	if ((pm == NULL) || !pm->enabled)
		return -ENODEV;

	mutex_lock(&pm->lock_mutex);

	if (!(pm->locks & type)) {
		GOOG_DBG(pm, "unexpectedly unlock: locks=0x%04X, type=0x%04X\n", pm->locks, type);
		mutex_unlock(&pm->lock_mutex);
		return -EINVAL;
	}

	pm->locks &= ~type;

	if (pm->locks == 0) {
		if (pm->state == GTI_PM_SUSPEND && pm->new_state == GTI_PM_RESUME) {
			GOOG_WARN(
				pm,
				"Wakelock unlocked during active resume process (unlocked type=0x%04X)\n",
				type);
		}

		pm->new_state = GTI_PM_SUSPEND;
		pm->update_state = true;
		if (pm->event_wq != NULL)
			queue_work(pm->event_wq, &pm->state_update_work);
	}
	mutex_unlock(&pm->lock_mutex);

	return ret;
}

int gti_pm_wake_unlock_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type)
{
	int ret = 0;

	if ((pm == NULL) || !pm->enabled)
		return -ENODEV;

	ret = gti_pm_wake_unlock_nosync_internal(pm, type);
	if (ret < 0)
		return ret;
	if (pm->event_wq != NULL)
		flush_workqueue(pm->event_wq);
	return ret;
}

bool gti_pm_wake_check_locked_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type)
{
	if ((pm == NULL) || !pm->enabled)
		return false;

	return pm->locks & type ? true : false;
}

u32 gti_pm_wake_get_locks_internal(struct gti_pm *pm)
{
	if ((pm == NULL) || !pm->enabled)
		return 0;

	return pm->locks;
}

static void gti_pm_suspend(struct gti_pm *pm)
{
	/* exit directly if device is already in suspend state */
	if (pm->state == GTI_PM_SUSPEND) {
		GOOG_WARN(pm, "GTI already suspended!\n");
		return;
	}

	pm->state = GTI_PM_SUSPEND;

	if (pm->ops && pm->ops->suspend)
		pm->ops->suspend(pm->private_data);

	pm_relax(pm->dev);
}

static void gti_pm_resume(struct gti_pm *pm)
{
	int ret = 0;

	/* exit directly if device isn't in suspend state */
	if (pm->state == GTI_PM_RESUME) {
		GOOG_WARN(pm, "GTI already resumed!\n");
		return;
	}

	pm_stay_awake(pm->dev);

	if (pm->ops && pm->ops->resume) {
		ret = pm->ops->resume(pm->private_data);
		if (ret) {
			pm_relax(pm->dev);
			return;
		}
	}

	pm->state = GTI_PM_RESUME;
	return;
}

static void gti_pm_state_update_work(struct work_struct *work)
{
	struct gti_pm *pm = container_of(work, struct gti_pm, state_update_work);

	mutex_lock(&pm->lock_mutex);
	while (pm->update_state) {
		pm->update_state = false;

		if (pm->new_state != pm->state) {
			if (pm->new_state == GTI_PM_RESUME) {
				pm->locks |= GTI_PM_WAKELOCK_TYPE_RESUME_RUNNING;

				mutex_unlock(&pm->lock_mutex);
				gti_pm_resume(pm);
				mutex_lock(&pm->lock_mutex);

				pm->locks &= ~GTI_PM_WAKELOCK_TYPE_RESUME_RUNNING;
				if (pm->locks == 0) {
					pm->new_state = GTI_PM_SUSPEND;
					pm->update_state = true;
				}
			} else {
				mutex_unlock(&pm->lock_mutex);
				gti_pm_suspend(pm);
				mutex_lock(&pm->lock_mutex);
			}
		}
	}
	mutex_unlock(&pm->lock_mutex);
}

int gti_pm_state_update(struct gti_pm *pm)
{
	if ((pm == NULL) || !pm->enabled)
		return -ENODEV;

	mutex_lock(&pm->lock_mutex);

	pm->update_state = true;
	if (pm->event_wq != NULL)
		queue_work(pm->event_wq, &pm->state_update_work);

	mutex_unlock(&pm->lock_mutex);
	return 0;
}

int gti_pm_probe(struct gti_pm *pm, struct device *dev, struct gti_pm_ops *ops, void *private_data)
{
	if (pm == NULL || dev == NULL || ops == NULL || private_data == NULL)
		return -EINVAL;

	int ret = 0;

	pm->event_wq = alloc_workqueue("gti_wq", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!pm->event_wq) {
		GOOG_ERR(pm, "alloc_workqueue failed!\n");
		ret = -ENOMEM;
		return ret;
	}

	pm->state = GTI_PM_RESUME;
	pm->new_state = pm->state;
	pm->locks = GTI_PM_WAKELOCK_TYPE_SCREEN_ON;
	pm->dev = dev;
	pm->ops = ops;
	pm->private_data = private_data;

	mutex_init(&pm->lock_mutex);
	INIT_WORK(&pm->state_update_work, gti_pm_state_update_work);

	device_init_wakeup(pm->dev, true);
	pm_stay_awake(pm->dev);

	pm->enabled = true;

	return ret;
}

int gti_pm_remove(struct gti_pm *pm)
{
	if (pm == NULL)
		return 0;

	if (pm->event_wq) {
		destroy_workqueue(pm->event_wq);
		pm->event_wq = NULL;
	}

	if (pm->enabled) {
		pm->enabled = false;

		pm_relax(pm->dev);
		device_init_wakeup(pm->dev, false);
	}

	return 0;
}
