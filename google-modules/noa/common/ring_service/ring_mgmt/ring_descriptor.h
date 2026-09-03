/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ring operation modeule.
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifndef __NOA_NEP_RING_DESCRIPTOR_H__
#define __NOA_NEP_RING_DESCRIPTOR_H__
#ifdef linux
#include <common/core.h>
#include "ring_manager.h"
#else
#include "common/defs.h"
#include "common/core.h"
#include "ring_mgmt/ring_manager.h"
#endif

ssize_t noa_ring_manager_desc_transfer(void *buf, size_t buf_len, const void *data,
				       size_t data_len);
ssize_t noa_ring_manager_payload_parser(const void *data);
int noa_ring_manager_noop_payload(void *buf, size_t buf_len);

#endif /* __NOA_NEP_RING_DESCRIPTOR_H_ */
