/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#pragma once

#include <common/include/pixel_ioctl.h>


int pixel_ioctl_init(struct pixel_gpu_device *pixel_dev);

void pixel_ioctl_term(struct pixel_gpu_device *pixel_dev);
