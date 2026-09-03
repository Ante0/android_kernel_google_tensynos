/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Util module of the ring operation.
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifndef __NOA_NEP_UTIL_RING_UTIL_H__
#define __NOA_NEP_UTIL_RING_UTIL_H__

#include "ring_manager.h"

/* Uses it to check if ring is full */
uint16_t get_available_slot(struct noa_ring *ring);

/* Uses it to check if ring is empty */
uint16_t get_unhandled_desc(struct noa_ring *ring);

void notify_ring_manager(unsigned long src_port_id);
void notify_port(uint8_t dst_port_id);

#endif /* __NOA_NEP_UTIL_RING_UTIL_H__ */
