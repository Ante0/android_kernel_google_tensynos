/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2022-2023 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#pragma once

void mba_signal(struct pixel_gpu_device *pixel_dev, u32 payload);

int mba_init(struct pixel_gpu_device *pixel_dev);

void mba_term(struct pixel_gpu_device *pixel_dev);
