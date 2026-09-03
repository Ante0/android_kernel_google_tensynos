// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/timer.h>
#include "h2omg.h"


/* should be called with mutex held */
int h2omg_timer_start(struct h2omg_info *info, enum h2omg_timer_id sensor_id)
{
	struct device *dev = info->dev;

	if (!(info->dual_trigger && info->trigger_delay_ms))
		return 0;

	if (!info->ops->irq_handler)
		return -ENODEV;

	dev_dbg(dev, "start timer for sensor %d\n", sensor_id);
	info->timer_id = sensor_id;
	return mod_timer(&info->wet_timer, jiffies + msecs_to_jiffies(info->trigger_delay_ms));
}

/* should be called with mutex held */
int h2omg_timer_stop(struct h2omg_info *info)
{
	struct device *dev = info->dev;

	if (!(info->dual_trigger && info->trigger_delay_ms))
		return 0;

	dev_dbg(dev, "stop timer\n");
	info->timer_id = TIMER_ID_SENSOR_NONE;
	return timer_delete(&info->wet_timer);
}

static void h2omg_timer_work(struct work_struct *work)
{
	struct h2omg_info *info = container_of(work, typeof(*info), timer_work);

	if (info->ops->timeout_handler)
		info->ops->timeout_handler(info);
}

static void h2omg_timer_cb(struct timer_list *t)
{
	struct h2omg_info *info = from_timer(info, t, wet_timer);

	schedule_work(&info->timer_work);
}

int h2omg_timer_init(struct h2omg_info *info)
{
	struct device *dev = info->dev;

	info->timer_id = TIMER_ID_SENSOR_NONE;

	if (of_property_read_u32(dev->of_node, "trigger_delay_ms", &info->trigger_delay_ms))
		info->trigger_delay_ms = 0;

	INIT_WORK(&info->timer_work, h2omg_timer_work);
	timer_setup(&info->wet_timer, h2omg_timer_cb, 0);

	return 0;
}

int h2omg_timer_cleanup(struct h2omg_info *info)
{
	return 0;
}
