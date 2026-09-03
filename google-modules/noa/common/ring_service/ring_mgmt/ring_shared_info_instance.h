/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Ring shared info instance
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_NEP_RING_SHARED_INFO_INSTANCE_H__
#define __NOA_NEP_RING_SHARED_INFO_INSTANCE_H__
#ifdef linux
#include <linux/kernel.h>

#include "ring_shared_info.h"
#else /* linux */
#include <cstdint>

#include "ring_mgmt/ring_shared_info.h"
#endif /* linux */

struct ring_shared_info *noa_ring_service_shared_info_instance(void);

#endif /* __NOA_NEP_RING_SHARED_INFO_INSTANCE_H__ */
