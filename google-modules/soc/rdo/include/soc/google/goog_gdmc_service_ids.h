/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2023, Google LLC
 */

#ifndef __GOOG_GDMC_SERVICE_ID_H
#define __GOOG_GDMC_SERVICE_ID_H

enum gdmc_mba_service_id {
	GDMC_MBA_SERVICE_ID_RESERVED = 0,
	GDMC_MBA_SERVICE_ID_PING = 1,
	GDMC_MBA_SERVICE_ID_MEM_READ = 2,
	GDMC_MBA_SERVICE_ID_MEM_WRITE = 3,
	GDMC_MBA_SERVICE_ID_CONSOLE_CMD = 4,
	GDMC_MBA_SERVICE_ID_DER_TRIGGER = 5,
	GDMC_MBA_SERVICE_ID_DER_CONFIGURE = 6,
	/* Get a register dump */
	GDMC_MBA_SERVICE_ID_GET_REGDUMP = 7,
	GDMC_MBA_SERVICE_ID_SKIP_SCANDUMP = 8,
	GDMC_MBA_SERVICE_ID_MEMAP_READ = 9,
	GDMC_MBA_SERVICE_ID_MEMAP_WRITE = 10,
	GDMC_MBA_SERVICE_ID_MEMAP_CONFIG = 11,
	GDMC_MBA_SERVICE_ID_SCAN_GPCM = 12,
	GDMC_MBA_SERVICE_ID_INT_CNT = 14,
	/* Reboot service */
	GDMC_MBA_SERVICE_ID_REBOOT = 15,
	GDMC_MBA_SERVICE_ID_INTR_OP = 16,
	/* DHUB UART service */
	GDMC_MBA_SERVICE_ID_DHUB = 17,
	GDMC_MBA_SERVICE_ID_GEM_CTRL = 18,
	GDMC_MBA_SERVICE_ID_SKIP_SMMU_DUMP = 19,
	/* Crash reset service */
	GDMC_MBA_SERVICE_ID_CRASH_RESET = 20,
	/* Power notifications from CPM */
	GDMC_MBA_SERVICE_ID_CPM_LPB = 21,
	GDMC_MBA_SERVICE_ID_FWTP = 22,
	/* GMC service */
	GDMC_MBA_SERVICE_ID_GMC_INIT_INFO = 23,
	/* EHLD service */
	GDMC_MBA_SERVICE_ID_EHLD = 24,
	/* Volatile Debug Unlock service */
	GDMC_MBA_SERVICE_ID_VDU = 25,
	/* SEM service */
	GDMC_MBA_SERVICE_ID_SEM_CTRL = 26,
	/* FWMT service */
	GDMC_MBA_SERVICE_ID_FWMT = 27,
	/* Modem UART service */
	GDMC_MBA_SERVICE_ID_MODEM_UART = 28,
	GDMC_NUM_MBA_SERVICES
};

/*
 * Services provided by APC which are unique to the GDMC_AP_CRITICAL channel.
 * Each new value should be added with a value one less than the current lowest
 * service ID value. If the service ID value becomes equal to SERVICE_LAST,
 * adjust the highest common value for this mailbox array or update the protocol
 * to include more services.
 *
 * Service IDs:
 * APC_CRITICAL_GDMC_AOC_WATCHDOG_SERVICE:	AOC watchdog service.
 * APC_CRITICAL_GDMC_EHLD_SERVICE:		EHLD service.
 * APC_CRITICAL_GDMC_DPA_WATCHDOG_SERVICE:	DPA watchdog service.
 * APC_CRITICAL_GDMC_FWTP_SERVICE:		FWTP service.
 */
enum apc_critical_gdmc_mba_service_id {
	APC_CRITICAL_GDMC_MAILBOX_SERVICE_LAST = 0x38,
	APC_CRITICAL_GDMC_FWTP_SERVICE = 0x6D,
	APC_CRITICAL_GDMC_DPA_WATCHDOG_SERVICE = 0x6E,
	APC_CRITICAL_GDMC_EHLD_SERVICE = 0x6F,
	APC_CRITICAL_GDMC_AOC_WATCHDOG_SERVICE = 0x70,
	APC_CRITICAL_GDMC_MAILBOX_SERVICE_FIRST = 0x70,
};

static inline bool goog_mba_gdmc_is_valid_service_id(int service_id)
{
	return (service_id <= APC_CRITICAL_GDMC_MAILBOX_SERVICE_FIRST &&
		service_id >= APC_CRITICAL_GDMC_MAILBOX_SERVICE_LAST);
}

#define MAX_APC_CRITICAL_GDMC_MAILBOX_SERVICE_NUM (APC_CRITICAL_GDMC_MAILBOX_SERVICE_FIRST - \
						   APC_CRITICAL_GDMC_MAILBOX_SERVICE_LAST + 1)

/**
 *  Valid values for the reboot type.
 */
enum gdmc_mba_reboot_type {
	GDMC_MBA_REBOOT_TYPE_WARM = 0,
	GDMC_MBA_REBOOT_TYPE_COLD = 1,
	GDMC_MBA_REBOOT_TYPE_COLD_DDR_RETENTION = 2,
	GDMC_MBA_REBOOT_TYPE_POWER_OFF = 3,
	GDMC_MBA_REBOOT_TYPE_WARM_WITH_REASON = 4,
};

/**
 * %GDMC_MBA_DHUB_PAYLOAD_SIZE - Size of payload for GDMC_MBA_SERVICE_ID_DHUB.
 *
 * Size includes 32-bit common mba header.
 */
#define GDMC_MBA_DHUB_PAYLOAD_SIZE	4

/**
 * enum gdmc_mba_dhub_cmd_id - DHUB service Command (CMD) identifiers
 *
 * The GDMC_MBA_SERVICE_ID_DHUB message is used to configure the legacy UART MUX
 * and UART Virtualization.
 *
 * The CMD field in the header identifies the action to perform.  The other three
 * words pass command-specific parameters.
 */
enum gdmc_mba_dhub_cmd_id {
	GDMC_MBA_DHUB_CMD_UART_MUX_GET = 0,
	GDMC_MBA_DHUB_CMD_UART_MUX_SET = 1,
	GDMC_MBA_DHUB_CMD_UART_BAUDRATE_SET = 2,
	GDMC_MBA_DHUB_CMD_UART_BAUDRATE_GET = 3,
	GDMC_MBA_DHUB_CMD_VIRTUALIZED_UARTS_SET = 4,
	GDMC_MBA_DHUB_CMD_VIRTUALIZED_UARTS_GET = 5,
};

/**
 * enum gdmc_mba_mbu_dhub_uart_id - UART identifiers.
 */
enum gdmc_mba_mbu_dhub_uart_id {
	GDMC_MBA_MBU_DHUB_UART_ID_AOSS_CPM_M55 = 1,
	GDMC_MBA_MBU_DHUB_UART_ID_APC = 2,
	GDMC_MBA_MBU_DHUB_UART_ID_AOSS_A32 = 3,
	GDMC_MBA_MBU_DHUB_UART_ID_BMSM = 4,
	GDMC_MBA_MBU_DHUB_UART_ID_GSA = 5,
	GDMC_MBA_MBU_DHUB_UART_ID_AOSS_CPM_M0P = 6,
	GDMC_MBA_MBU_DHUB_UART_ID_AURDSP = 7,
	GDMC_MBA_MBU_DHUB_UART_ID_TPU = 8,
	GDMC_MBA_MBU_DHUB_UART_ID_MODEM = 9,
	GDMC_MBA_MBU_DHUB_UART_ID_GSC = 10,
	GDMC_MBA_MBU_DHUB_UART_ID_GDMC = 11,
	GDMC_MBA_MBU_DHUB_UART_ID_AOSS_SENSOR_CORE = 12,
	GDMC_MBA_MBU_DHUB_UART_ID_NANO_TPU = 13,
	GDMC_MBA_MBU_DHUB_UART_ID_CPUACC_SSU = 14,
	GDMC_MBA_MBU_DHUB_UART_ID_HSION_EBU_M0P = 15,
	GDMC_MBA_MBU_DHUB_UART_ID_PCIE_DPA0 = 16,
	GDMC_MBA_MBU_DHUB_UART_ID_PCIE_DPA1 = 17,
	GDMC_MBA_MBU_DHUB_UART_ID_MAX,
};

/**
 * enum gdmc_mba_lga_dhub_uart_id - UART identifiers.
 */
enum gdmc_mba_lga_dhub_uart_id {
	GDMC_MBA_LGA_DHUB_UART_ID_CPM = 1,
	GDMC_MBA_LGA_DHUB_UART_ID_APC = 2,
	GDMC_MBA_LGA_DHUB_UART_ID_AOC = 3,
	GDMC_MBA_LGA_DHUB_UART_ID_BMSM = 4,
	GDMC_MBA_LGA_DHUB_UART_ID_GSA = 5,
	GDMC_MBA_LGA_DHUB_UART_ID_ISPFE = 6,
	GDMC_MBA_LGA_DHUB_UART_ID_AURDSP = 7,
	GDMC_MBA_LGA_DHUB_UART_ID_TPU = 8,
	GDMC_MBA_LGA_DHUB_UART_ID_MODEM = 9,
	GDMC_MBA_LGA_DHUB_UART_ID_GSC = 10,
	GDMC_MBA_LGA_DHUB_UART_ID_GDMC = 11,
	GDMC_MBA_LGA_DHUB_UART_ID_MAX,
};

/**
 * enum gdmc_mba_dhub_uart_mux_id- UART MUX identifiers.
 */
enum gdmc_mba_dhub_uart_mux_id {
	GDMC_MBA_DHUB_UART_MUX_ID_NONE = 0,
	GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED = 0xff,
};

#define GDMC_MBA_DHUB_VIRT_MASK_EN	BIT(0)

/**
 * enum gdmc_mba_ehld_cmd - EHLD service Commands (CMD)
 *
 * The GDMC_MBA_SERVICE_ID_EHLD message is used to configure a periodic timer in GDMC
 * that is used for EHLD CPU checks.
 *
 * The CMD field in the header identifies the action to perform.  The other three
 * words are currently not used.
 */
enum gdmc_mba_ehld_cmd {
	GDMC_MBA_EHLD_CMD_TIMER_ENABLE = 0,
	GDMC_MBA_EHLD_CMD_TIMER_DISABLE = 1,
	GDMC_MBA_EHLD_CMD_TIMER_PARAM = 2,
	GDMC_MBA_EHLD_CMD_PMU_COUNTER_ID = 3,
};

/**
 * enum apc_critical_gdmc_mba_ehld_cpu_state - APC CRITICAL EHLD service Messages (Word 1)
 *
 * The APC_CRITICAL_GDMC_EHLD_SERVICE message is used to notify kernel of CPU states after
 * EHLD check has been performed by GDMC.
 *
 * This enum is used as Word 1 of message.
 */
enum apc_critical_gdmc_mba_ehld_cpu_state {
	EHLD_STATE_NORMAL = 0x0,
	EHLD_STATE_WARN = 0x1,
	EHLD_STATE_LOCKUP_SW = 0x2,
	EHLD_STATE_LOCKUP_HW = 0x3,
};

/**
 * enum gdmc_mba_crash_reset_cmd - Crash reset service Commands (CMD)
 *
 * The GDMC_MBA_SERVICE_ID_CRASH_RESET is used to query or configure crash reset parameters.
 *
 * The CMD field in the header identifies the action to perform
 * and the three remaining words are command-specific parameters.
 */

enum gdmc_mba_crash_reset_cmd {
	GDMC_MBA_CRASH_RESET_CMD_GET_STATUS = 0,
	GDMC_MBA_CRASH_RESET_CMD_GET_BREADCRUMB = 1,
	GDMC_MBA_CRASH_RESET_CMD_GET_REBOOT_REASON = 2,
	GDMC_MBA_CRASH_RESET_CMD_SET_SKIP_S2M_VOTE = 3,
};

#endif /* __GOOG_GDMC_SERVICE_ID_H */
