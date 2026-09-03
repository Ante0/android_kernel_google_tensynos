/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles Pixel Packet Filter related information.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef NOA_PPF_MANAGER_H_
#define NOA_PPF_MANAGER_H_

bool noa_ppf_manager_add_filter(uint8_t *program, uint32_t program_len);
bool noa_ppf_manager_delete_filter(void);

bool noa_ppf_manager_enable_filter(void);
bool noa_ppf_manager_disable_filter(void);

bool noa_ppf_manager_read_filter_data(uint8_t *buf, uint32_t buf_len);

#endif  // NOA_PPF_MANAGER_H_
