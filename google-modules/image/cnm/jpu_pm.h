/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC.
 *
 * Author: Anastasia Young <anastasiayoung@google.com>
 */

#ifndef _JPU_PM_H_
#define _JPU_PM_H_

#include "jpu_priv.h"

void jpu_hw_reset(struct jpu_core *core);
int jpu_pm_init(struct jpu_core *core);
void jpu_pm_deinit(struct jpu_core *core);
int jpu_pm_power_on(struct jpu_core *core);
int jpu_pm_power_off(struct jpu_core *core);
int jpu_runtime_suspend(struct device *dev);
int jpu_runtime_resume(struct device *dev);
int jpu_pm_suspend(struct device *dev);
int jpu_pm_resume(struct device *dev);

#endif
