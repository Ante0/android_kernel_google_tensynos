/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * APF program for NEP
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef NEP_APF_H
#define NEP_APF_H

#define NEP_MAX_APF_PROGRAM_LEN  4096

struct apf_info {
	uint8_t apf_program[NEP_MAX_APF_PROGRAM_LEN];
	uint64_t apf_program_time_ms;
	uint32_t apf_program_len;
	bool is_apf_filter_enabled;
};

void nep_apf_init(void);
struct apf_info *nep_apf_get_info(void);
bool nep_apf_add_filter(const uint8_t *program, uint32_t program_len);
bool nep_apf_append_filter(const uint8_t *program, uint32_t program_len);
bool nep_apf_delete_filter(void);
bool nep_apf_enable_filter(void);
bool nep_apf_disable_filter(void);
bool nep_apf_read_filter_data(uint8_t *buf, uint32_t buf_len);

#endif
