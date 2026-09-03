/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#ifndef _VPU_PM_H_
#define _VPU_PM_H_

#include <linux/device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "vpu_priv.h"

#define C3P_CONTROL_INPUT_OFFSET 0x1004
#define C3P_CONTROL_INPUT_SW_EN_OFFSET 0x1008
#define NS_PSM_C3P_CORE_FSM_STATUS_OFFSET 0x101c

int vpu_pm_init(struct vpu_core *core);
void vpu_pm_deinit(struct vpu_core *core);
int vpu_pm_power_on(struct vpu_core *core);
int vpu_pm_power_off(struct vpu_core *core);
int vpu_runtime_suspend(struct device *dev);
int vpu_runtime_resume(struct device *dev);
int vpu_pm_suspend(struct device *dev);
int vpu_pm_resume(struct device *dev);

#endif
