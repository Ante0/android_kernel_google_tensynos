// SPDX-License-Identifier: GPL-2.0

#ifndef _SSCD_H_
#define _SSCD_H_

#include "sysconfig.h"

int gpu_sscd_init(struct pixel_gpu_device *pixel_dev);

void gpu_sscd_deinit(struct pixel_gpu_device *pixel_dev);

void gpu_sscd_dump(struct pixel_gpu_device *pixel_dev, PVRSRV_ROBUSTNESS_NOTIFY_DATA *error);

#endif /* _SSCD_H_ */
