/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "sysconfig.h"

int init_dvfs_gov(struct pixel_gpu_device *pixel_dev);

void deinit_dvfs_gov(struct pixel_gpu_device *pixel_dev);
