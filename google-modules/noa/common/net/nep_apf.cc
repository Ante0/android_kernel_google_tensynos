// SPDX-License-Identifier: GPL-2.0-only
/*
 * APF program for NEP
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include "nep_apf.h"
#include "nep.h"
#else
#include <chrono>
#include "common/compiler.h"
#include "common/core.h"
#include "linux_port/types.h"
#include "net/nep_apf.h"
#include "pw_chrono/system_clock.h"
#include "pw_log/log.h"
#endif

#ifdef linux
#define LOG_ERROR(msg)               \
  do {                               \
    dev_err(&sim->dev, "%s", #msg);  \
  } while (0)

#else
#define LOG_ERROR(msg)         \
  do {                         \
    PW_LOG_ERROR("%s", #msg);  \
  } while (0)

SEC_FAST_DATA static struct apf_info nep_apf_info;

#endif

static struct apf_info *get_apf_info(void) {
#ifdef linux
	struct noa_simulator *sim = noa_sim_get();
	if (sim == NULL) {
		return NULL;
	}
	return &sim->nep_apf_info;
#else
	return &nep_apf_info;
#endif
}

static uint32_t get_boottime_ms(void) {
#ifdef linux
	return ktime_to_ms(ktime_get_boottime());
#else
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			pw::chrono::SystemClock::now().time_since_epoch()).count());
#endif
}

void nep_apf_init(void) {
	struct apf_info *apf_info = get_apf_info();
	memset(apf_info->apf_program, 0, sizeof(apf_info->apf_program));
	apf_info->apf_program_len = 0;
	apf_info->apf_program_time_ms = 0;
	apf_info->is_apf_filter_enabled = false;
}

struct apf_info *nep_apf_get_info(void) {
	return get_apf_info();
}

bool nep_apf_add_filter(const uint8_t *program, uint32_t program_len) {
	struct apf_info *apf_info = get_apf_info();
	if (apf_info == NULL || program == NULL || program_len > NEP_MAX_APF_PROGRAM_LEN) {
		return false;
	}
	memcpy(apf_info->apf_program, program, program_len);
	apf_info->apf_program_len = program_len;
	apf_info->apf_program_time_ms = get_boottime_ms();
	return true;
}

/* This function is for test only. This is a workaround for configuring the APF program via
 * the NEP shell console, due to the length limitations of RPC messages.
 */
bool nep_apf_append_filter(const uint8_t *program, uint32_t program_len) {
	struct apf_info *apf_info = get_apf_info();
	if (apf_info == NULL || program == NULL
	    || apf_info->apf_program_len + program_len > NEP_MAX_APF_PROGRAM_LEN) {
		return false;
	}
	memcpy(apf_info->apf_program + apf_info->apf_program_len, program, program_len);
	apf_info->apf_program_len += program_len;
	apf_info->apf_program_time_ms = get_boottime_ms();
	return true;
}

bool nep_apf_delete_filter(void) {
	struct apf_info *apf_info = get_apf_info();
	if (apf_info == NULL) {
		return false;
	}
	apf_info->apf_program_len = 0;
	return true;
}

bool nep_apf_enable_filter(void) {
	struct apf_info *apf_info = get_apf_info();
	if (apf_info == NULL) {
		return false;
	}
	apf_info->is_apf_filter_enabled = true;
	return true;
}

bool nep_apf_disable_filter(void) {
	struct apf_info *apf_info = get_apf_info();
	if (apf_info == NULL) {
		return false;
	}
	apf_info->is_apf_filter_enabled = false;
	return true;
}

bool nep_apf_read_filter_data(uint8_t *buf, uint32_t buf_len) {
	struct apf_info *apf_info = get_apf_info();
	if (apf_info == NULL) {
		return false;
	}
	if (buf_len > NEP_MAX_APF_PROGRAM_LEN) {
		buf_len = NEP_MAX_APF_PROGRAM_LEN;
	}
	memcpy(buf, apf_info->apf_program, buf_len);
	return true;
}

#ifdef linux
EXPORT_SYMBOL_GPL(nep_apf_init);
EXPORT_SYMBOL_GPL(nep_apf_get_info);
EXPORT_SYMBOL_GPL(nep_apf_add_filter);
EXPORT_SYMBOL_GPL(nep_apf_delete_filter);
EXPORT_SYMBOL_GPL(nep_apf_enable_filter);
EXPORT_SYMBOL_GPL(nep_apf_disable_filter);
EXPORT_SYMBOL_GPL(nep_apf_read_filter_data);
#endif
