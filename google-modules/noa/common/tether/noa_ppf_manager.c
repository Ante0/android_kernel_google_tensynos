/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles Pixel Packet Filter related information.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */

#include <common/core.h>
#include "nep_apf.h"
#include "noa_ppf_manager.h"

bool noa_ppf_manager_add_filter(uint8_t *program, uint32_t program_len) {
	return nep_apf_add_filter(program, program_len);
}
EXPORT_SYMBOL_GPL(noa_ppf_manager_add_filter);

bool noa_ppf_manager_delete_filter(void) {
	return nep_apf_delete_filter();
}
EXPORT_SYMBOL_GPL(noa_ppf_manager_delete_filter);

bool noa_ppf_manager_enable_filter(void) {
	return nep_apf_enable_filter();
}
EXPORT_SYMBOL_GPL(noa_ppf_manager_enable_filter);

bool noa_ppf_manager_disable_filter(void) {
	return nep_apf_disable_filter();
}
EXPORT_SYMBOL_GPL(noa_ppf_manager_disable_filter);

bool noa_ppf_manager_read_filter_data(uint8_t *buf, uint32_t buf_len) {
	return nep_apf_read_filter_data(buf, buf_len);
}
EXPORT_SYMBOL_GPL(noa_ppf_manager_read_filter_data);

