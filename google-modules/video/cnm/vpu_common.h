/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Ruofei Ma <ruofeim@google.com>
 */

#ifndef _VPU_COMMON_H_
#define _VPU_COMMON_H_

#include "vpu_priv.h"

void vpu_update_system_state(struct vpu_core *core);
void vpu_send_fw_cmd_locked(struct vpu_core *core, const struct vpu_fw_cmd *fw_cmd);

#endif
