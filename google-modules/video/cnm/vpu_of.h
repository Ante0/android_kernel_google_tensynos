/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#ifndef _VPU_OF_H_
#define _VPU_OF_H_

#include "vpu_priv.h"

int vpu_of_dt_parse(struct vpu_core *core);
int vpu_of_devfreq_init(struct vpu_core *core);
void vpu_of_devfreq_deinit(struct vpu_core *core);

#endif // _VPU_OF_H_
