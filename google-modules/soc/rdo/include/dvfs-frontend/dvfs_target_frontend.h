/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024 Google LLC */

#include <linux/types.h>

#ifndef _DVFS_FRONTEND_H
#define _DVFS_FRONTEND_H

void dvfs_fe_set_pf_level(int domain_id, u8 pf_level);

int dvfs_fe_get_pf_level(int domain_id);

int dvfs_fe_prepare_domain(int domain_id);

#endif /* _DVFS_FRONTEND_H */
