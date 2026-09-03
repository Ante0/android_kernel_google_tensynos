/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP ring management
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifndef __NOA_NEP_RING_CONTROLLER_H__
#define __NOA_NEP_RING_CONTROLLER_H__

#ifdef linux
#include <common/core.h>
#include "nep.h"
#include "ring_manager.h"
#else
#include "common/core.h"
#include "ring_mgmt/ring_manager.h"
#endif

#define MAX_HANDLED_COUNT 100

void handle_desc_complete(struct noa_desc *desc);
void NoaRingServiceHandleIsr(struct noa_port *src_port);
#endif /* __NOA_NEP_RING_CONTROLLER_H__ */
