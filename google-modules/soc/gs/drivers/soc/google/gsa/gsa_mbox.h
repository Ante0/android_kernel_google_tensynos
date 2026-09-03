/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020 Google LLC
 */

/* This header is internal only.
 *
 * Public APIs are in //private/google-modules/soc/gs/include/linux/gsa/
 *
 * Include via //private/google-modules/soc/gs:gs_soc_headers
 */

#ifndef __LINUX_GSA_MBOX_H
#define __LINUX_GSA_MBOX_H

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/mailbox_client.h>

#define GSA_PAGE_SIZE (4096U)
#define GSA_PQ_IMAGE_HEADER_SIZE (20480U)

/* Number of shared registers mailbox */
#define MBOX_SR_NUM 16
#define MBOX_MAX_ARGS_NUM (MBOX_SR_NUM - 3)

/**
 * enum gsa_mbox_cmd - mailbox commands
 */
enum gsa_mbox_cmd {
	/* Inherited ROM commands */
	GSA_MB_CMD_AUTH_IMG = 1,
	GSA_MB_CMD_LOAD_FW_IMG = 2,
	GSA_MB_CMD_GET_CHIP_ID = 3,
	GSA_MB_CMD_RESERVED_CMD_4 = 4,
	GSA_MB_CMD_DBG_GET_BOOT_CRUMBS = 5,
	GSA_MB_CMD_RESERVED_CMD_6 = 6,

	/* GSA mailbox test commands */
	GSA_MB_TEST_CMD_ECHO = 34,
	GSA_MB_TEST_CMD_RESERVED_35,
	GSA_MB_TEST_CMD_RESERVED_36,
	GSA_MB_TEST_CMD_START_UNITTEST,
	GSA_MB_TEST_CMD_RUN_UNITTEST,
	GSA_MB_TEST_CMD_UNHANDLED,

	/* TPU management */
	GSA_MB_CMD_LOAD_TPU_FW_IMG = 50,
	GSA_MB_CMD_TPU_CMD = 51,
	GSA_MB_CMD_UNLOAD_TPU_FW_IMG = 52,

	/* GSC commands */
	GSA_MB_CMD_GSC_HARD_RESET = 60,
	GSA_MB_CMD_GSC_TPM_DATAGRAM = 61,
	GSA_MB_CMD_GSC_NOS_CALL = 65,

	/* KDN */
	GSA_MB_CMD_KDN_GENERATE_KEY = 70,
	GSA_MB_CMD_KDN_EPHEMERAL_WRAP_KEY = 71,
	GSA_MB_CMD_KDN_DERIVE_RAW_SECRET = 72,
	GSA_MB_CMD_KDN_PROGRAM_KEY = 73,
	GSA_MB_CMD_KDN_RESTORE_KEYS = 74,
	GSA_MB_CMD_KDN_SET_OP_MODE = 75,

	/* AOC management */
	GSA_MB_CMD_LOAD_AOC_FW_IMG = 90,
	GSA_MB_CMD_AOC_CMD = 91,
	GSA_MB_CMD_UNLOAD_AOC_FW_IMG = 92,

	/* SJTAG */
	GSA_MB_CMD_SJTAG_GET_STATUS = 100,
	GSA_MB_CMD_SJTAG_GET_CHIP_ID = 101,
	GSA_MB_CMD_SJTAG_GET_PUB_KEY_HASH = 102,
	GSA_MB_CMD_SJTAG_SET_PUB_KEY = 103,
	GSA_MB_CMD_SJTAG_GET_CHALLENGE = 104,
	GSA_MB_CMD_SJTAG_ENABLE = 105,
	GSA_MB_CMD_SJTAG_FINISH = 106,

	/* gsa misc */
	GSA_MB_CMD_GET_GSA_VERSION = 130,
	GSA_MB_CMD_GET_GSA_ROM_PATCH_VERSION = 140,

	/* PM commands */
	GSA_MB_CMD_WAKELOCK_ACQUIRE = 150,
	GSA_MB_CMD_WAKELOCK_RELEASE = 151,
	GSA_MB_CMD_AP_SUSPEND_HINT = 152,
	GSA_MB_CMD_AP_RESUME_HINT = 153,
	GSA_MB_CMD_GET_PM_STATS = 154,

	/* App Loading */
	GSA_MB_CMD_LOAD_APP_PKG = 180,

	/* DSP management */
	GSA_MB_CMD_LOAD_DSP_FW_IMG = 240,
	GSA_MB_CMD_DSP_CMD = 241,
	GSA_MB_CMD_UNLOAD_DSP_FW_IMG = 242,

	/* Retrieve trace data */
	GSA_MB_CMD_RUN_TRACE_DUMP = 270,
	GSA_MB_CMD_RUN_TRACE_PING = 271,

	/* Post quantum enabled image auth */
	GSA_MB_CMD_PQ_AUTH_IMG = 0x1A7D0010U,

	/* FFA Interface */
	GSA_MB_CMD_MEM_PROTECT = 420,
	GSA_MB_CMD_MEM_UNPROTECT,

	/* IPC interface */
	GSA_MB_CMD_IPC_CONNECT = 440,
	GSA_MB_CMD_IPC_GET_EVENT,
	GSA_MB_CMD_IPC_SEND,
	GSA_MB_CMD_IPC_RECV,
	GSA_MB_CMD_IPC_DISCONNECT,
};

/**
 * enum img_loader_args - parameter layout for image loading mbox commands
 * @IMG_LOADER_HEADER_ADDR_LO_IDX: index of image header low address parameter
 * @IMG_LOADER_HEADER_ADDR_HI_IDX: index of image header high address parameter
 * @IMG_LOADER_BODY_ADDR_LO_IDX:   index of image body low address parameter
 * @IMG_LOADER_BODY_ADDR_HI_IDX:   index of image body high address parameter
 * @IMG_LOADER_ARG_CNT:            total number of parameters
 *
 * This layout is applicable for the fallowing mailbox commands:
 *     %GSA_MB_CMD_AUTH_IMG
 *     %GSA_MB_CMD_LOAD_FW_IMG
 *     %GSA_MB_CMD_LOAD_TPU_FW_IMG
 *     %GSA_MB_CMD_LOAD_AOC_FW_IMG
 */
enum img_loader_args {
	IMG_LOADER_HEADER_ADDR_LO_IDX = 0,
	IMG_LOADER_HEADER_ADDR_HI_IDX = 1,
	IMG_LOADER_BODY_ADDR_LO_IDX = 2,
	IMG_LOADER_BODY_ADDR_HI_IDX = 3,
	IMG_LOADER_ARGC = 4,
};

/**
 * enum pq_img_loader_args - parameter layout for image loading mbox commands
 * using the PQ-enabled image header
 * @PQ_IMG_LOADER_HEADER_ADDR_LO_IDX: index of image header low address
 * parameter
 * @PQ_IMG_LOADER_HEADER_ADDR_HI_IDX: index of image header high address
 * parameter
 * @PQ_IMG_LOADER_BODY_ADDR_LO_IDX:   index of image body low address parameter
 * @PQ_IMG_LOADER_BODY_ADDR_HI_IDX:   index of image body high address
 * parameter
 * @PQ_IMG_LOADER_BODY_SIZE_IDX:      index of image body length parameter
 * @PQ_IMG_LOADER_HEADER_SIZE_IDX:    index of image header size parameter
 * @PQ_IMG_LOADER_ARGC:               total number of parameters
 *
 * This layout is applicable for the fallowing mailbox commands:
 *     %GSA_MB_CMD_PQ_AUTH_IMG
 */
enum pq_img_loader_args {
	PQ_IMG_LOADER_HEADER_ADDR_LO_IDX = IMG_LOADER_HEADER_ADDR_LO_IDX,
	PQ_IMG_LOADER_HEADER_ADDR_HI_IDX = IMG_LOADER_HEADER_ADDR_HI_IDX,
	PQ_IMG_LOADER_BODY_ADDR_LO_IDX = IMG_LOADER_BODY_ADDR_LO_IDX,
	PQ_IMG_LOADER_BODY_ADDR_HI_IDX = IMG_LOADER_BODY_ADDR_HI_IDX,
	PQ_IMG_LOADER_BODY_SIZE_IDX = 4,
	PQ_IMG_LOADER_HEADER_SIZE_IDX = 5,
	PQ_IMG_LOADER_ARGC = 6,
};

enum gsc_tpm_datagram_args {
	GSC_TPM_CMD_IDX = 0,
	GSC_TPM_LEN_IDX,
	GSC_TPM_ADDR_LO_IDX,
	GSC_TPM_ADDR_HI_IDX,
	GSC_TPM_ARGC,
};

/**
 * enum kdn_data_req_args - parameters layout for KDN related calls
 * @KDN_DATA_BUF_ADDR_LO_IDX: index of low word of KDN data buffer address
 * @KDN_DATA_BUF_ADDR_HI_IDX: index of high word of KDN data buffer address
 * @KDN_DATA_BUF_SIZE_IDX: index of KDN data buffer size parameter
 * @KDN_DATA_LEN_IDX: index of KDN data length parameter
 * @KDN_OPTION_IDX: index of KDN request option parameter
 * @KDN_REQ_ARGC: total number of parameters expected by KDN service
 */
enum kdn_data_req_args {
	KDN_DATA_BUF_ADDR_LO_IDX = 0,
	KDN_DATA_BUF_ADDR_HI_IDX,
	KDN_DATA_BUF_SIZE_IDX,
	KDN_DATA_LEN_IDX,
	KDN_OPTION_IDX,
	KDN_REQ_ARGC,
};

/**
 * enum kdn_data_rsp_args - parameters layout for KDN response calls
 * @KDN_RSP_DATA_LEN_IDX: number of bytes returned in KDN data buffer by GSA
 * @KDN_RSP_ARGC: total number of parameters expected by KDN service
 */
enum kdn_data_rsp_args {
	KDN_RSP_DATA_LEN_IDX = 0,
	KDN_RSP_ARGC,
};

/**
 * enum kdn_set_mode_req_args - parameter layout for KDN set mode command
 * @KDN_SET_OP_MODE_MODE_IDX: index of operating mode parameter
 * @KDN_SET_OP_MODE_UFS_DESCR_IDX: index of UFS descriptor format parameter
 * @KDN_SET_OP_MODE_ARGC: total number of parameters
 */
enum kdn_set_op_mode_req_args {
	KDN_SET_OP_MODE_MODE_IDX = 0,
	KDN_SET_OP_MODE_UFS_DESCR_IDX = 1,
	KDN_SET_OP_MODE_ARGC,
};

/**
 * enum gsa_version_req_args - parameter layout for gsa version command
 * @GSA_VERSION_ARG_ADDR_LO_IDX: index of low word of data buffer address
 * @GSA_VERSION_ARG_ADDR_HI_IDX: index of high word of data buffer address
 * @GSA_VERSION_ARG_BUFF_SIZE: index of data buffer size
 * @GSA_VERSION_ARG_COUNT: total argument count
 **/
enum gsa_version_req_args {
	GSA_VERSION_ARG_ADDR_LO_IDX = 0,
	GSA_VERSION_ARG_ADDR_HI_IDX,
	GSA_VERSION_ARG_BUFF_SIZE,
	GSA_VERSION_ARG_COUNT
};

/**
 * enum gsa_rom_patch_version_req_args - parameter layout for gsa rom patch
 * version command.
 * @READ_GSA_ROM_PATCH_VERSION_IDX: index of rom patch version parameter
 * @READ_LCS_RSP_ARGC: number of parameters
 */
enum gsa_rom_patch_version_req_args {
    READ_GSA_ROM_PATCH_VERSION_IDX = 0,
    READ_GSA_ROM_PATCH_VERSION_ARGC,
};

/**
 * enum sjtag_data_req_args - parameters layout for SJTAG related calls
 * @SJTAG_DATA_BUF_ADDR_LO_IDX: index of low word of SJTAG data buffer address
 * @SJTAG_DATA_BUF_ADDR_HI_IDX: index of high word of SJTAG data buffer address
 * @SJTAG_DATA_BUF_SIZE_IDX: index of SJTAG data buffer size parameter
 * @SJTAG_DATA_LEN_IDX: index of SJTAG data length parameter
 * @SJTAG_DATA_REQ_ARGC: total number of parameters expected by SJTAG service
 */
enum sjtag_data_req_args {
	SJTAG_DATA_BUF_ADDR_LO_IDX = 0,
	SJTAG_DATA_BUF_ADDR_HI_IDX,
	SJTAG_DATA_BUF_SIZE_IDX,
	SJTAG_DATA_LEN_IDX,
	SJTAG_DATA_REQ_ARGC,
};

/**
 * enum sjtag_data_rsp_args - parameters layout for SJTAG response calls
 * @SJTAG_DATA_RSP_STATUS_IDX: index of status word returned by SJTAG service
 * @SJTAG_DATA_RSP_DATA_LEN_IDX: index of parameters contining number of bytes
 *                               returned in SJTAG data buffer by GSA
 * @SJTAG_DATA_RSP_DATA_ARGC: total number of parameters returned by SJTAG
 *                            data request
 */
enum sjtag_data_rsp_args {
	SJTAG_DATA_RSP_STATUS_IDX = 0,
	SJTAG_DATA_RSP_DATA_LEN_IDX,
	SJTAG_DATA_RSP_ARGC,
};

/**
 * enum sjtag_status_rsp_args - parameter layout for SJTAG get status command
 * @SJTAG_STATUS_RSP_DEBUG_ALLOWED_IDX: Index of debug allowed parameter
 * @SJTAG_STATUS_RSP_HW_STATUS_IDX: index of hardware status parameter
 * @SJTAG_STATUS_RSP_DEBUG_TIME_IDX: index of DEBUG time parameter
 */
enum sjtag_status_rsp_args {
	SJTAG_STATUS_RSP_DEBUG_ALLOWED_IDX = 0,
	SJTAG_STATUS_RSP_HW_STATUS_IDX = 1,
	SJTAG_STATUS_RSP_DEBUG_TIME_IDX = 2,
	SJTAG_STATUS_RSP_ARGC,
};

/**
 * enum get_pm_stats_req_args - parameter layout to get pm_stats request
 * @GET_PM_STATS_REQ_DATA_BUF_ADDR_LO_IDX: low word of data buffer address
 * @GET_PM_STATS_REQ_DATA_BUF_ADDR_HI_IDX: high word of data buffer address
 * @GET_PM_STATS_REQ_DATA_BUF_SIZE_IDX:- size of data buffer
 * @GET_PM_STATS_REQ_ARGC: number of arguments expected in request
 */
enum get_pm_stats_req_args {
	GET_PM_STATS_REQ_DATA_BUF_ADDR_LO_IDX = 0,
	GET_PM_STATS_REQ_DATA_BUF_ADDR_HI_IDX = 1,
	GET_PM_STATS_REQ_DATA_BUF_SIZE = 2,
	GET_PM_STATS_REQ_ARGC,
};

/**
 * enum get_pm_stats_req_args - param layout to get pm_stats request
 * @GET_PM_STATS_RSP_DATA_LEN_IDX: data length returned by call
 * @GET_PM_STATS_RSP_ARGC: number of arguments expected in response
 */
enum get_pm_stats_rsp_args {
	GET_PM_STATS_RSP_DATA_LEN_IDX = 0,
	GET_PM_STATS_RSP_ARGC,
};

enum gsa_pm_stats_ver {
	GSA_PM_STATS_V1 = 1,
	GSA_PM_STATS_V2,
};

/**
 * MAX_WAKELOCK_NUM  - max number of wakelock sources supported
 */
#define MAX_WAKELOCK_NUM 8

/**
 * struct gsa_pm_stats - GSA PM stats
 * @ver:            current version (@enum gsa_pm_stats_ver)
 * @tick_freq:      timer tick frequency.
 * @uptime_ticks:   uptime (ticks)
 * @idle_ticks:     total time (ticks) spent in idle state
 * @idle_cnt:       number of times GSA entered idle state
 * @pg_ticks:       total time (ticks) spent in power gated (PG) state
 * @pg_cnt:         number of times GSA attempted entering PG state
 * @pg_wakeup_cnt:  number of times GSA woke up from PG state
 * @pg_abort_cnt:   number of times GSA aborted entering PG state
 * @longest_awake:  longest time GSA has not attempted PG transition
 * @longest_sleep:  longest time GSA has stayed in low power mode
 * @ap_suspend_cnt: number of times GSA has received AP suspend hint
 * @ap_resume_cnt:  number of times GSA has received AP resume hint
 * @wakelock_state: current aggregate wakelock state
 * @wakelock_acquire_cnt: number of times each wakelock has been acquired
 * @wakelock_release_cnt: number of times each wakelock has been release
 *
 * Note: Each GSA wakelock is associated with GSA mailbox
 */
struct gsa_pm_stats {
	uint32_t ver;
	uint32_t tick_freq;
	uint64_t uptime_ticks;
	uint64_t idle_ticks;
	uint64_t idle_cnt;
	uint64_t pg_ticks;
	uint64_t pg_cnt;
	uint64_t pg_wakeup_cnt;
	uint64_t pg_abort_cnt;
	uint64_t rg_ticks;
	uint64_t rg_cnt;
	uint64_t rg_wakeup_cnt;
	uint64_t rg_abort_cnt;
	uint64_t longest_awake;
	uint64_t longest_sleep;
	uint32_t ap_suspend_cnt;
	uint32_t ap_resume_cnt;
	uint32_t wakelock_state;
	uint32_t wakelock_acquire_cnt[MAX_WAKELOCK_NUM];
	uint32_t wakelock_release_cnt[MAX_WAKELOCK_NUM];
};

/**
 * enum app_pkg_load_req_args - parameters layout for APP package request
 * @APP_PKG_ADDR_LO_IDX: index of low word of APP package address
 * @APP_PKG_ADDR_HI_IDX: index of high word of APP package address
 * @APP_PKG_SIZE_IDX: index of App package size parameter
 * @LOAD_APP_REQ_ARGC: total number of parameters expected by apploader
 *                     service
 */
enum app_pkg_load_req_args {
	APP_PKG_ADDR_LO_IDX = 0,
	APP_PKG_ADDR_HI_IDX,
	APP_PKG_SIZE_IDX,
	APP_PKG_LOAD_REQ_ARGC,
};

/**
 * enum gsa_trace_dump_req_args - parameters layout for SJTAG related calls
 * @GSA_TRACE_DUMP_BUF_ADDR_LO_IDX: index of low word of SJTAG data buffer address
 * @GSA_TRACE_DUMP_BUF_ADDR_HI_IDX: index of high word of SJTAG data buffer address
 * @GSA_TRACE_DUMP_BUF_BUF_SIZE_IDX: index of SJTAG data buffer size parameter
 * @GSA_TRACE_DUMP_REQ_ARGC: total number of parameters expected by trace dump service
 */
enum gsa_trace_dump_req_args {
	GSA_TRACE_DUMP_BUF_ADDR_LO_IDX = 0,
	GSA_TRACE_DUMP_BUF_ADDR_HI_IDX,
	GSA_TRACE_DUMP_BUF_SIZE_IDX,
	GSA_TRACE_DUMP_REQ_ARGC,
};

/**
 * enum gsa_trace_dump_rsp_args - parameters layout for SJTAG response calls
 * @GSA_TRACE_DUMP_RSP_SENT_DATA_IDX: index of status word returned by SJTAG service
 * @GSA_TRACE_DUMP_RSP_TOTAL_DATA_IDX: index of parameters contining number of bytes
 *                               returned in SJTAG data buffer by GSA
 * @GSA_TRACE_DUMP_RSP_DATA_ARGC: total number of parameters returned by SJTAG
 *                            data request
 */
enum gsa_trace_dump_rsp_args {
	GSA_TRACE_DUMP_RSP_SENT_DATA_IDX = 0,
	GSA_TRACE_DUMP_RSP_TOTAL_DATA_IDX,
	GSA_TRACE_DUMP_RSP_ARGC,
};

/* Shared memory */
#define PROT_ID_GENERIC_MEM_LEND 0x103
#define PROT_ID_GENERIC_MEM_SHARE 0x133

enum ext_mem_protect_req_args {
	GSA_EXT_MEM_PROTECT_REQ_PA_BASE_ADDR_LO_IDX,
	GSA_EXT_MEM_PROTECT_REQ_PA_BASE_ADDR_HI_IDX,
	GSA_EXT_MEM_PROTECT_REQ_VA_BASE_ADDR_LO_IDX,
	GSA_EXT_MEM_PROTECT_REQ_VA_BASE_ADDR_HI_IDX,
	GSA_EXT_MEM_PROTECT_REQ_SIZE_IDX,
	GSA_EXT_MEM_PROTECT_REQ_PROT_ID_IDX,
	GSA_EXT_MEM_PROTECT_REQ_ARGC,
};

enum ext_mem_protect_rsp_args {
	GSA_EXT_MEM_PROTECT_RSP_HANDLE_IDX,
	GSA_EXT_MEM_PROTECT_RSP_ARGC,
};

enum ext_mem_unprotect_req_args {
	GSA_EXT_MEM_UNPROTECT_REQ_HANDLE_IDX,
	GSA_EXT_MEM_UNPROTECT_REQ_ARGC,
};

/*
 * GSA IPC
 */
enum gsa_ipc_err {
	GSA_IPC_ERR_NONE = 0,
	GSA_IPC_ERR_PORT_NOT_FOUND = 2,
	GSA_IPC_ERR_CHANNEL_NOT_READY = 3,
	GSA_IPC_ERR_CHANNEL_NO_MSG = 4,
	GSA_IPC_ERR_CHANNEL_SEND_BLOCKED = 9,
	GSA_IPC_ERR_CHANNEL_CLOSED = 15,
	GSA_IPC_ERR_NO_RESOURCES = 41,
};

/* Connect */
enum gsa_ipc_connect_req_args {
	GSA_IPC_CONNECT_REQ_ADDR_LO_IDX = 0,
	GSA_IPC_CONNECT_REQ_ADDR_HI_IDX,
	GSA_IPC_CONNECT_REQ_LEN_IDX,
	GSA_IPC_CONNECT_REQ_COOKIE_LO_IDX,
	GSA_IPC_CONNECT_REQ_COOKIE_HI_IDX,
	GSA_IPC_CONNECT_REQ_ARGC,
};

enum gsa_ipc_connect_rsp_args {
	GSA_IPC_CONNECT_RSP_ERR_IDX = 0,
	GSA_IPC_CONNECT_RSP_HANDLE_IDX,
	GSA_IPC_CONNECT_RSP_ARGC,
};

/* Disconnect */
enum gsa_ipc_disconnect_req_args {
	GSA_IPC_DISCONNECT_REQ_HANDLE_IDX = 0,
	GSA_IPC_DISCONNECT_REQ_ARGC,
};

/* Get event */
enum gsa_ipc_event_enum {
	GSA_IPC_EVENT_NONE = 0x0,
	GSA_IPC_EVENT_CONNECTED = 0x1,
	GSA_IPC_EVENT_ERROR = 0x2,
	GSA_IPC_EVENT_HUP = 0x4,
	GSA_IPC_EVENT_MSG = 0x8,
	GSA_IPC_EVENT_SEND_UNBLOCKED = 0x10,
};

enum gsa_ipc_get_event_req_args {
	GSA_IPC_GET_EVENT_REQ_HANDLE_IDX = 0,
	GSA_IPC_GET_EVENT_REQ_ARGC,
};

enum gsa_ipc_get_event_rsp_args {
	GSA_IPC_GET_EVENT_RSP_HANDLE_IDX = 0,
	GSA_IPC_GET_EVENT_RSP_EVENT_IDX,
	GSA_IPC_GET_EVENT_RSP_COOKIE_LO_IDX,
	GSA_IPC_GET_EVENT_RSP_COOKIE_HI_IDX,
	GSA_IPC_GET_EVENT_RSP_ARGC,
};

/* Send Msg */
#define SHM_INF_PG_SZ_SHIFT 12

#define SHM_INF_MEM_ID_SHIFT 56
#define SHM_INF_MEM_ID_MASK 0xFFUL

#define SHM_INF_PROT_SHIFT 52
#define SHM_INF_PROT_MASK 0xFUL

#define SHM_INF_PROT_RD (0x1)
#define SHM_INF_PROT_WR (0x2)
#define SHM_INF_PROT_RW (0x3)

#define SHM_INF_SIZE_SHIFT 32
#define SHM_INF_SIZE_MASK 0x000FFFFFUL

#define SHM_INF_OBJ_ID_SHIFT 24
#define SHM_INF_OBJ_ID_MASK 0xFFUL

#define SHM_INF_OFFSET_SHIFT 0
#define SHM_INF_OFFSET_MASK 0x000FFFFFU

enum gsa_ipc_send_msg_req_args {
	GSA_IPC_SEND_REQ_HANDLE_IDX = 0,
	GSA_IPC_SEND_REQ_MSG_BUF_ADDR_LO_IDX,
	GSA_IPC_SEND_REQ_MSG_BUF_ADDR_HI_IDX,
	GSA_IPC_SEND_REQ_MSG_LEN_IDX,
	GSA_IPC_SEND_REQ_MIN_ARGC,
	GSA_IPC_SEND_REQ_MSG_SHM_BASE_IDX = GSA_IPC_SEND_REQ_MIN_ARGC,
	GSA_IPC_SEND_REQ_MAX_ARGC = MBOX_MAX_ARGS_NUM,
};

enum gsa_ipc_send_msg_rsp_args {
	GSA_IPC_SEND_RSP_ERR_IDX = 0,
	GSA_IPC_SEND_RSP_MSG_LEN_IDX,
	GSA_IPC_SEND_RSP_ARGC,
};

/* Recv Msg */
enum gsa_ipc_recv_msg_req_args {
	GSA_IPC_RECV_REQ_HANDLE_IDX = 0,
	GSA_IPC_RECV_REQ_MSG_BUF_ADDR_LO_IDX,
	GSA_IPC_RECV_REQ_MSG_BUF_ADDR_HI_IDX,
	GSA_IPC_RECV_REQ_MSG_BUF_SIZE_IDX,
	GSA_IPC_RECV_REQ_ARGC,
};

enum gsa_ipc_recv_msg_rsp_args {
	GSA_IPC_RECV_RSP_ERR_IDX = 0,
	GSA_IPC_RECV_RSP_MSG_LEN_IDX,
	GSA_IPC_RECV_RSP_ARGC,
};

/*
 * GSA specific mailbox protocol support
 */

/* Command response bit */
#define GSA_MB_CMD_RSP	BIT(31)

/* Mailbox error codes */
enum gsa_mb_error {
	/* Defined by GSA ROM */
	GSA_MB_OK = 0U,
	GSA_MB_ERR_INVALID_ARGS = 1U,
	GSA_MB_ERR_AUTH_FAILED = 2U,
	GSA_MB_ERR_BUSY = 3U,
	GSA_MB_ERR_ALREADY_RUNNING = 4U,
	GSA_MB_ERR_OUT_OF_RESOURCES = 5U,
	GSA_MB_ERR_BAD_HANDLE = 6U,

	/* Extended by GSA firmware */
	GSA_MB_ERR_GENERIC = 128U,
	GSA_MB_ERR_INTERNAL = 129U,
	GSA_MB_ERR_TIMED_OUT = 130U,
	GSA_MB_ERR_BAD_STATE = 131U,

	/* Success value use by GSA ROM on GSP */
	GSA_MB_GSP_ROM_SUCCESS = 0x4FD1398BU,
};

#if IS_ENABLED(CONFIG_GSA_LINUX_MAILBOX)

#define MBOX_MSG_SLOT_IDX 0
#define MBOX_DOORBELL_IRQ_SLOT_IDX 1

struct mbox_slot {
	struct mbox_client client; /* Linux mailbox framework client */
	struct mbox_chan *channel; /* Linux mailbox framework channel */
	u32 *rsp_buffer; /* Buffer for responses.  See rx_callback. */
	/* Signals response received.  See rx_callback. */
	struct completion mbox_cmd_completion;
};

struct gsa_mbox {
	struct device *dev; /* back-reference to device */
	struct mutex share_reg_lock; /* protects access to SRs */
	/* Wake lock must be held until all GSA commands have completed. */
#if IS_ENABLED(CONFIG_GSA_PKVM)
	u32 wake_ref_cnt;
	struct device *s2mpu;
#endif
	/* Message slot */
	struct mbox_slot msg_slot;
	int (*send_mbox_msg)(struct mbox_chan *chan, void *mssg);

#if IS_ENABLED(CONFIG_GSA_IPC)
	/* Doorbell interrupt slot */
	void (*ipc_irq_handler)(void *args);
	void *ipc_irq_handler_args;

	struct mbox_slot doorbell_irq_slot;
#endif
};

#else
struct gsa_mbox {
	struct device *dev;
	void __iomem *base;
	int irq;
	spinlock_t slock; /* protects RMW like access to some registers */
	struct mutex mbox_lock; /* protects access to SRs */
	struct completion mbox_cmd_completion;
	u32 exp_intmr0;
	u32 wake_ref_cnt;
	struct device *s2mpu;
};

#endif

struct gsa_mbox *gsa_mbox_init(struct platform_device *pdev);

void gsa_mbox_destroy(struct gsa_mbox *mb);

int gsa_send_mbox_cmd(struct gsa_mbox *mb, u32 cmd,
		      u32 *req_args, u32 req_argc,
		      u32 *rsp_args, u32 rsp_argc);

void mbox_tx_prepare(struct mbox_client *cl, void *mssg);
void mbox_tx_done(struct mbox_client *cl, void *mssg, int r);
void mbox_rx_callback(struct mbox_client *cl, void *mssg);

void mbox_set_ipc_irq_handler(struct gsa_mbox *mb,
			      void (*handler)(void *),
			      void *handler_args);

#endif /* __LINUX_GSA_MBOX_H */
