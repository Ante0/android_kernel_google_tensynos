// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC.
 *
 */

#ifndef GOOGLE_CDD_H
#define GOOGLE_CDD_H

#include <dt-bindings/soc/google/google-cdd-def.h>

enum cdd_log_freq_domain {
	CDD_FREQ_DOMAIN_APC_LIT,
	CDD_FREQ_DOMAIN_APC_MID0,
	CDD_FREQ_DOMAIN_APC_MID1,
	CDD_FREQ_DOMAIN_APC_BIG,
};

#if IS_ENABLED(CONFIG_GOOGLE_CRASH_DEBUG_DUMP)

#define CDD_FREQ_MAX_NAME_SIZE		SZ_8

/* google-cdd functions */
extern int google_cdd_get_sjtag_status(void);
extern bool google_cdd_get_reboot_status(void);
extern bool google_cdd_get_panic_status(void);
extern bool google_cdd_get_warm_status(void);
void google_cdd_set_debug_test_buffer_addr(u64 paddr, unsigned int cpu);
unsigned int google_cdd_get_debug_test_buffer_addr(unsigned int cpu);
extern uint64_t google_cdd_get_last_pc(unsigned int cpu);
extern uint32_t google_cdd_get_hardlockup_mask(void);
extern uint32_t google_cdd_get_hardlockup_magic(int cpu);
extern unsigned int google_cdd_get_fiq_pending_core(void);
extern unsigned int google_cdd_get_item_size(const char *name);
extern unsigned long google_cdd_get_item_vaddr(const char *name);
extern unsigned int google_cdd_get_item_paddr(const char *name);
extern int google_cdd_get_item_enable(const char *name);
extern void google_cdd_set_item_enable(const char *name, int en);
extern int google_cdd_get_enable(void);
extern void google_cdd_output(void);
extern unsigned int google_cdd_get_max_core_num(void);
extern void google_cdd_set_powerkey_status(bool val);

/* google-cdd-dpm functions */
extern void google_cdd_do_dpm_policy(unsigned int policy, const char *str);

/* google-cdd-utils functions */
extern void google_cdd_update_psci_crumbs(void);
extern void google_cdd_set_core_cflush_stat(unsigned int val);
extern int google_cdd_get_system_dev_stat(uint32_t dev, uint32_t *val);
extern void google_cdd_set_system_dev_stat(uint32_t dev, uint32_t val);
extern int google_cdd_emergency_reboot(const char *str);
extern int google_cdd_emergency_reboot_timeout(const char *str, unsigned int level,
					       unsigned int timeout);
extern void google_cdd_register_debug_ops(int (*halt)(void), int (*cachedump)(void),
					  int (*scandump)(void));
extern void google_cdd_save_context(struct pt_regs *regs, bool stack_dump);
extern int google_cdd_get_core_ehld_stat(uint32_t cpu, uint32_t *val);
extern int google_cdd_get_core_pmu_val(uint32_t cpu, uint32_t *val);
extern int google_cdd_set_core_ehld_stat(uint32_t cpu, uint32_t val);
extern int google_cdd_set_core_pmu_val(uint32_t cpu, uint32_t val);

/* google-cdd-log functions */
extern void google_cdd_cpuidle_mod_by_id(int cpu, char *modes, unsigned int
					 state, s64 diff, int en);
extern void google_cdd_freq(enum cdd_log_freq_domain domain, unsigned int *old_freq,
			    unsigned int *target_freq);
extern void google_cdd_freq_by_name(const char *name, unsigned int *old_freq,
			    unsigned int *target_freq);
extern void google_cdd_log_output(void);

#else /* CONFIG_GOOGLE_CRASH_DEBUG_DUMP */

/* google-cdd functions */
static inline int google_cdd_get_sjtag_status(void) { return 0; }
static inline bool google_cdd_get_reboot_status(void) { return false; }
static inline bool google_cdd_get_panic_status(void) { return false; }
static inline unsigned long google_cdd_get_last_pc_paddr(void) { return 0; }
static inline unsigned long google_cdd_get_last_pc(unsigned int cpu) { return 0; }
static inline unsigned int google_cdd_get_hardlockup_magic(int cpu) { return 0; }
static inline unsigned int google_cdd_get_item_size(const char *name) { return 0; }
static inline unsigned long google_cdd_get_item_vaddr(const char *name) { return 0; }
static inline unsigned int google_cdd_get_item_paddr(const char *name) { return 0; }
static inline int google_cdd_get_item_enable(const char *name) { return 0; }
static inline void google_cdd_set_item_enable(const char *name, int en) {}
static inline int google_cdd_get_enable(void) { return 0; }
static inline void google_cdd_output(void) {}
static inline unsigned int google_cdd_get_max_core_num(void) { return 0; }
static inline void google_cdd_set_powerkey_status(bool val) {}

/* google-cdd-dpm functions */
static inline void google_cdd_do_dpm_policy(unsigned int policy, const char *str) {}

/* google-cdd-utils functions */
static inline void google_cdd_update_psci_crumbs(void) {}
static inline void google_cdd_set_core_cflush_stat(unsigned int val) {}
static inline int google_cdd_emergency_reboot(const char *str) { return -ENODEV; }
static inline int google_cdd_emergency_reboot_timeout(const char *str, unsigned int level,
						      unsigned int timeout) { return -ENODEV; }
static inline void google_cdd_register_debug_ops(int (*halt)(void), int (*cachedump)(void),
						 int (*scandump)(void)) {}
static inline void google_cdd_save_context(struct pt_regs *regs, bool stack_dump) {}

/* google-cdd-log functions */
static inline void google_cdd_cpuidle_mod_by_id(int cpu, char *modes, unsigned
						int state, s64 diff, int en) {}
static inline void google_cdd_freq(enum cdd_log_freq_domain domain, unsigned int *old_freq,
				   unsigned int *target_freq) {}
static inline void google_cdd_log_output(void) {}

#endif /* CONFIG_GOOGLE_CRASH_DEBUG_DUMP */

enum cddlog_flag {
	CDD_FLAG_IN			= 1,
	CDD_FLAG_ON			= 2,
	CDD_FLAG_OUT			= 3,
	CDD_FLAG_SOFTIRQ		= 10000,
	CDD_FLAG_CALL_TIMER_FN		= 20000,
	CDD_FLAG_SMP_CALL_FN		= 30000,
};

enum cdd_item_index {
	CDD_ITEM_HEADER_ID = 0,
	CDD_ITEM_KEVENTS_ID,
};

struct google_cdd_helper_ops {
	int (*stop_all_cpus)(void);
	int (*run_cachedump)(void);
	int (*run_scandump_mode)(void);
};

enum google_cdd_system_device {
	CDD_SYSTEM_DEVICE_AUDIO = 0,
	CDD_SYSTEM_DEVICE_BT,
	CDD_SYSTEM_DEVICE_CAMERA,
	CDD_SYSTEM_DEVICE_CHARGER,
	CDD_SYSTEM_DEVICE_DISPLAY,
	CDD_SYSTEM_DEVICE_FINGERPRINT,
	CDD_SYSTEM_DEVICE_GNSS,
	CDD_SYSTEM_DEVICE_MODEM,
	CDD_SYSTEM_DEVICE_TEMP,
	CDD_SYSTEM_DEVICE_USB,
	CDD_SYSTEM_DEVICE_VPU,
	CDD_SYSTEM_DEVICE_WIFI,
	CDD_SYSTEM_DEVICE_BOOT,
	CDD_SYSTEM_DEVICE_DEV_MAX = 32,
};

#endif /* GOOGLE_CDD_H */
