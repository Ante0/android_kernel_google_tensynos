// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC.
 *
 */

#ifndef GOOGLE_CDD_LOCAL_H
#define GOOGLE_CDD_LOCAL_H
#include <linux/device.h>
#include <soc/google/google-cdd-log.h>

struct google_cdd_param {
	void *cdd_log_misc;
	void *cdd_items;
	void *cdd_log_items;
	void *cdd_log;
	void *hook_func;
};

struct google_cdd_base {
	size_t size;
	phys_addr_t vaddr;
	phys_addr_t paddr;
	unsigned int enabled;
	unsigned int version;
	struct google_cdd_param *param;
};

struct google_cdd_info {
	size_t size;
	phys_addr_t vaddr;
	phys_addr_t paddr;
	unsigned int enabled;
};

struct google_cdd_item {
	char *name;
	struct google_cdd_info entry;
	unsigned int persist;
	unsigned char *head_ptr;
	unsigned char *curr_ptr;
};

struct google_cdd_log_item {
	char *name;
	struct google_cdd_info entry;
};

struct google_cdd_ctx {
	struct device *dev;
	raw_spinlock_t ctrl_lock;
	int sjtag_status;
	bool in_reboot;
	bool in_panic;
	bool in_warm;
	bool long_press_power;
	int panic_action;
	unsigned int reset_reason;
	void __iomem *apc_wdt_sub_reason;
};

struct google_cdd_dpm_feature {
	unsigned int dump_mode_enabled;
	bool dump_mode_file_support;
};

struct google_cdd_dpm_policy {
	unsigned int pre_log;

	unsigned int el1_da;
	unsigned int el1_sp_pc;
	unsigned int el1_ia;
	unsigned int el1_undef;
	unsigned int el1_inv;
	unsigned int el1_serror;
};

struct google_cdd_dpm {
	bool enabled;
	unsigned int version;

	struct google_cdd_dpm_feature feature;
	struct google_cdd_dpm_policy policy;
};

void google_cdd_register_vh_log(void);
void google_cdd_init_log(void);
void google_cdd_init_utils(struct device *dev);
void google_cdd_start_log(struct device *dev);
void __iomem *google_cdd_get_header_vaddr(void);
unsigned long google_cdd_get_header_paddr(void);
void google_cdd_print_log_report(void);
struct google_cdd_item *google_cdd_get_item_by_index(int index);
int google_cdd_get_num_items(void);
int google_cdd_get_dpm_none_dump_mode(void);
void google_cdd_set_dpm_none_dump_mode(unsigned int mode);
int google_cdd_suspend_init(void);
int google_cdd_dpm_scand_dt(struct device *dev);

extern struct google_cdd_log *cdd_log;
extern struct google_cdd_ctx cdd_ctx;
extern struct google_cdd_item cdd_items[];
extern struct google_cdd_log_item cdd_log_items[];
extern struct google_cdd_log_misc cdd_log_misc;

/* Sign domain */
#define CDD_SIGN_RESET			0x0
#define CDD_SIGN_ALIVE			0xFACE
#define CDD_SIGN_DEAD			0xDEAD
#define CDD_SIGN_PANIC			0xBABA
#define CDD_SIGN_UNKNOWN_REBOOT		0xCACA
#define CDD_SIGN_EMERGENCY_REBOOT	0xCACB
#define CDD_SIGN_WARM_REBOOT		0xCACC
#define CDD_SIGN_NORMAL_REBOOT		0xCAFE
#define CDD_SIGN_WATCHDOG_APC		0xCBCA
#define CDD_SIGN_REBOOT_LONGKEY_POWER_WARM	0xCCC1
#define CDD_SIGN_MAGIC			(0xDB9 << 16)

/* Size parameters */
#define CDD_CORE_NUM				(8)
#define CDD_SYSREG_PER_CORE_SZ			SZ_512
#define CDD_COREREG_PER_CORE_SZ			SZ_512

/* CDD Header contents */
#define CDD_HDR_INFO_TOTAL_SZ			(SZ_4K)
#define CDD_HDR_INFO_BLOCK_SZ			(CDD_HDR_INFO_TOTAL_SZ / 4)
#define CDD_HDR_SYSREG_SZ			(CDD_SYSREG_PER_CORE_SZ * CDD_CORE_NUM)
#define CDD_HDR_COREREG_SZ			(CDD_COREREG_PER_CORE_SZ * CDD_CORE_NUM)
#define CDD_HDR_DPM_NS_SZ			SZ_8K

#define CDD_HDR_INFO_BLOCK_OFFS			(0x0)
#define CDD_HDR_SYSREG_OFFS			(CDD_HDR_INFO_BLOCK_OFFS + CDD_HDR_INFO_TOTAL_SZ)
#define CDD_HDR_COREREG_OFFS			(CDD_HDR_SYSREG_OFFS + CDD_HDR_SYSREG_SZ)
#define CDD_HDR_DPM_NS_OFFS			(CDD_HDR_COREREG_OFFS + CDD_HDR_COREREG_SZ)
/* unused */

/* CDD Header Info Block contents */
/* Temp block 0 and 1 offset: 0x0 - 0x7FF */
#define CDD_OFFSET_NONE_DPM_DUMP_MODE		(0x100)
#define CDD_OFFSET_DEBUG_TEST_BUFFER(n)		(0x104 + (0x8 * (n))) /* n=8 */
#define CDD_OFFSET_BL31_PSCI_CRUMBS(n)		(0x144 + (0x4 * (n))) /* n=8 */
#define CDD_OFFSET_EMERGENCY_REASON		(0x164)
#define CDD_OFFSET_WDT_CALLER			(0x168)
#define CDD_OFFSET_CORE_POWER_STAT		(0x170)
#define CDD_OFFSET_PANIC_STAT			(0x190)
#define CDD_OFFSET_CFLUSH_STAT			(0x1B0)
#define CDD_OFFSET_HARDLOCKUP_MASK		(0x1B4)
#define CDD_OFFSET_CORE_LAST_PC(n)		(0x1B8 + (0x8 * (n))) /* n=8 */
#define CDD_OFFSET_CORE_HARDLOCKUP_MAGIC(n)	(0x1F8 + (0x4 * (n))) /* n=8 */
#define CDD_OFFSET_SYSTEM_STATUS		(0x218) /* uint32_t system_status[32] */
#define CDD_OFFSET_CORE_FIQ_PENDING		(0x298)
#define CDD_OFFSET_WDT_PET_TIME_IDX		(0x29C)
#define CDD_OFFSET_WDT_PET_TIME_STAT(n)		(0x2A0 + (0x10 * (n))) /* n=4 */
#define CDD_WDT_PET_TIME_STAT_NUM		(4)
/* 0x54 bytes unused, next free: offset 0x2E0 */
#define CDD_OFFSET_PCSR				(0x334)
#define CDD_OFFSET_MEM_PROTECT_REASON		(0x534)
/* 0xB bytes unused, next free: offset 0x538 */
#define CDD_OFFSET_MAIN_PMIC_REASON		(0x544)
#define CDD_OFFSET_SUB_PMIC_REASON		(0x548)
#define CDD_OFFSET_CORE_PMU_VAL			(0x54C)
#define CDD_OFFSET_EHLD_STAT			(0x56C)
/* 0x274 bytes unused, next free: offset 0x58C */

#define CDD_EHLD_CARVEOUT_SIZE          (0x20)

/* Keep block 2 offset: 0x800 - 0xBFF */
#define CDD_OFFSET_BL31_LOG_BUF_RSVD_STAT	(0x800)
#define CDD_OFFSET_ABL_DUMP_STAT		(0x804)
#define CDD_OFFSET_BL31_DUMP_RSVD_STAT		(0x808)
/* 0x3F4 bytes unused, next free: offset 0x80C */

/* Temp block 3: 0xC00 - 0xFFF */
#define CDD_OFFSET_PANIC_STRING			(0xC00)
#define CDD_PANIC_STRING_SZ			CDD_HDR_INFO_BLOCK_SZ
/* 0x0 bytes unused */

#define CDD_HDR_INFO_BLOCK_MAX_INDEX		(0x4)
#define CDD_HDR_INFO_BLOCK_KEEP_INDEX		(0x2)

/* CDD ITEM ID */
#define CDD_ITEM_HEADER				"cdd_header"
#define CDD_ITEM_KEVENTS			"log_kevents"

#define CDD_LOG_TASK		"task_log"
#define CDD_LOG_WORK		"work_log"
#define CDD_LOG_CPUIDLE		"cpuidle_log"
#define CDD_LOG_SUSPEND		"suspend_log"
#define CDD_LOG_IRQ		"irq_log"
#define CDD_LOG_HRTIMER		"hrtimer_log"
#define CDD_LOG_CLK		"clk_log"
#define CDD_LOG_LPM		"lpm_log"
#define CDD_LOG_FREQ		"freq_log"

/* DPM DT ENTRY NAME */
#define DPM_ROOT		"dpm"
#define DPM_VERSION		"version"
#define DPM_FEATURE		"feature"
#define DPM_POLICY		"policy"

#define DPM_DUMP_MODE		"dump-mode"
#define DPM_ENABLED		"enabled"
#define DPM_FILE_SUPPORT	"file-support"
#define DPM_EVENT		"event"
#define DPM_DEBUG_KINFO		"debug-kinfo"

#define DPM_EXCEPTION		"exception"
#define DPM_PRE_LOG		"pre_log"
#define DPM_EL1_DA		"el1_da"
#define DPM_EL1_IA		"el1_ia"
#define DPM_EL1_UNDEF		"el1_undef"
#define DPM_EL1_SP_PC		"el1_sp_pc"
#define DPM_EL1_INV		"el1_inv"
#define DPM_EL1_SERROR		"el1_serror"

#endif
