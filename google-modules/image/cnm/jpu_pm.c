// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 *
 * Author: Anastasia Young <anastasiayoung@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "jpu_pm.h"
#include "codaj_regdefine.h"

#define JPU_IDLE_CHECK_TIMEOUT_MS 500
#define INST_CTRL_IDLE 0

static void jpu_wait_idle(struct jpu_core *core)
{
	unsigned int data_flag_reg;
	int retries_ms = 0;

	while (retries_ms < JPU_IDLE_CHECK_TIMEOUT_MS) {
		data_flag_reg = READ_JPU_REGISTER(core, MJPEG_INST_CTRL_STATUS_REG);
		if (((data_flag_reg >> 4) & 0xf) == INST_CTRL_IDLE)
			return;
		usleep_range(900, 1100);
		retries_ms++;
	}
	dev_warn(core->dev,
			"wait idle timeout: MJPEG_INST_CTRL_STATUS_REG=%#x\n",
			data_flag_reg);
}

void jpu_hw_reset(struct jpu_core *core)
{
	reset_control_assert(core->jpu_reset); /* system is in reset state */
	reset_control_deassert(core->jpu_reset); /* reset state is lifted */
	/* Starts Instance Controller FSM */
	WRITE_JPU_REGISTER(core, MJPEG_INST_CTRL_START_REG, BIT(0));
}

static int _jpu_power_off(struct jpu_core *core)
{
	int rc;
	uint32_t val;

	jpu_wait_idle(core);
	val = READ_JPU_REGISTER(core, MJPEG_DPB_CONFIG_REG);
	val |= BIT(16); /* jpu_idle enable */
	WRITE_JPU_REGISTER(core, MJPEG_DPB_CONFIG_REG, val);
	rc = pm_runtime_put_sync(core->pd_dev);
	if (rc)
		dev_err(core->dev, "failed to power off %d\n", rc);
	return rc;
}

static int _jpu_power_on(struct jpu_core *core)
{
	int rc = 0;

	rc = pm_runtime_resume_and_get(core->pd_dev);
	if (rc) {
		dev_err(core->dev, "failed to power on %d", rc);
		return rc;
	}
	jpu_hw_reset(core);
	return rc;
}

int jpu_pm_power_on(struct jpu_core *core)
{
	int rc = 0;

	mutex_lock(&core->lock);
	if (core->power_status == POWER_ON) {
		dev_warn(core->dev, "already powered on\n");
		goto out;
	}
	rc = _jpu_power_on(core);
	if (!rc)
		core->power_status = POWER_ON;

out:
	mutex_unlock(&core->lock);
	return rc;
}

int jpu_pm_power_off(struct jpu_core *core)
{
	int rc = 0;

	mutex_lock(&core->lock);
	if (core->power_status == POWER_OFF_RELEASED) {
		dev_warn(core->dev, "already powered off %d\n", core->power_status);
		goto out;
	} else if (core->power_status == POWER_OFF_SLEEP) {
		core->power_status = POWER_OFF_RELEASED;
		goto out;
	}
	rc = _jpu_power_off(core);
	if (!rc)
		core->power_status = POWER_OFF_RELEASED;

out:
	mutex_unlock(&core->lock);
	return rc;
}

int jpu_runtime_suspend(struct device *dev)
{
	return 0;
}

int jpu_runtime_resume(struct device *dev)
{
	return 0;
}

int jpu_pm_suspend(struct device *dev)
{
	int rc = 0;
	struct jpu_core *core = platform_get_drvdata(to_platform_device(dev));

	mutex_lock(&core->lock);
	if (core->power_status != POWER_ON)
		goto out;
	rc = _jpu_power_off(core);
	if (!rc)
		core->power_status = POWER_OFF_SLEEP;

out:
	mutex_unlock(&core->lock);
	if (!rc)
		dev_info_ratelimited(core->dev, "suspended\n");
	return rc;
}

int jpu_pm_resume(struct device *dev)
{
	int rc = 0;
	struct jpu_core *core = platform_get_drvdata(to_platform_device(dev));

	mutex_lock(&core->lock);
	if (core->power_status != POWER_OFF_SLEEP)
		goto out;
	rc = _jpu_power_on(core);
	if (!rc)
		core->power_status = POWER_ON;

out:
	mutex_unlock(&core->lock);
	if (!rc)
		dev_info_ratelimited(core->dev, "resumed\n");
	return rc;
}

int jpu_pm_init(struct jpu_core *core)
{
	struct device *pd_dev;

	/* There will be multiple pd-domains in dtsi even if we only
	 * use one (c3p_core), so pd-domain is powered off by default
	 */
	pd_dev = dev_pm_domain_attach_by_id(core->dev, 0);
	if (pd_dev == NULL) {
		dev_err(core->dev, "pm domain not specified\n");
		return -EINVAL;
	} else if (IS_ERR(pd_dev)) {
		dev_err(core->dev, "failed to attach power domain\n");
		return PTR_ERR(pd_dev);
	}

	core->pd_dev = pd_dev;

	return 0;
}

void jpu_pm_deinit(struct jpu_core *core)
{
	if (!core->pd_dev)
		return;

	dev_pm_domain_detach(core->pd_dev, true);
}
