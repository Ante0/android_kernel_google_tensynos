#ifndef WLAN_DEBUG_CONTROLLER_CLIENT_H
#define WLAN_DEBUG_CONTROLLER_CLIENT_H

#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include "wlan/noa_wlan_client.h"

#define MAX_PRIV_SIZE 1024
#define MAX_PARAM_SIZE sizeof(struct wlan_log_system_control_param)
#define DEFAULT_CAPTURE_SIZE 128
#define MAX_DESTINATION_NAME_LEN 16
#define CASE_LOCATION_NAME(location)                                                               \
	case OUTPUT_LOCATION_##location:                                                           \
		return #location

extern struct kobj_type noa_wlan_dbg_ktype;
extern struct kobj_type noa_wlan_dbg_packet_sniffer_ktype;
extern struct kobj_type noa_wlan_dbg_log_system_ktype;

struct noa_wlan_entry {
	struct kobject kobj;
	struct noa_core_entry *wlan_core;
	char priv[MAX_PRIV_SIZE];
};

struct wlan_dbg_entry {
	struct kobject kobj;
	struct noa_wlan_entry *parent_entry;
	char priv[MAX_PRIV_SIZE];
};

enum packet_sniffer_ring_type {
	PACKET_SNIFFER_RING_TYPE_START,
	NOA_HW_TX = PACKET_SNIFFER_RING_TYPE_START,
	NOA_HW_RX,
	NEP_TX,
	NEP_RX,
	PACKET_SNIFFER_RING_TYPE_NUM,
};

enum packet_runout_action {
	UNDEFINED_RUNOUT_ACTION = 0,
	OVERWRITE,
	DISCARD,
	NOTIFY_DRIVER,
	RUNOUT_ACTION_NUM,
};

enum location {
	UNDEFINED_LOCATION = 0,
	OUTPUT_LOCATION_DRAM,
	OUTPUT_LOCATION_UART,
	OUTPUT_LOCATION_DRAM_AND_UART,
	OUTPUT_LOCATION_NUM,
};

enum log_level {
	LOG_LEVEL_START = 0,
	LOG_LEVEL_DISABLE_ALL = LOG_LEVEL_START,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_ENABLE_ALL = LOG_LEVEL_DEBUG,
	LOG_LEVEL_END,
	LOG_LEVEL_NUM = LOG_LEVEL_END,
};

enum log_module {
	LOG_MODULE_START = 0,
	LOG_MODULE_DP = LOG_MODULE_START,
	LOG_MODULE_BM,
	LOG_MODULE_SHM,
	LOG_MODULE_STATS,
	LOG_MODULE_NEP,
	LOG_MODULE_RPC,
	LOG_MODULE_CFG,
	LOG_MODULE_TXCPL,
	LOG_MODULE_RXPOST,
	LOG_MODULE_ISR,
	LOG_MODULE_NEP_BUFFER_POOL,
	LOG_MODULE_WDEV,
	LOG_MODULE_TX,
	LOG_MODULE_RX,
	LOG_MODULE_SHELL,
	LOG_MODULE_END,
	LOG_MODULE_NUM = LOG_MODULE_END,
};

enum wlan_debug_component_type {
	DBG_COMPONENT_START = 0,
	DBG_LOG_SYSTEM = DBG_COMPONENT_START,
	DBG_PACKET_SNIFFER,
	DBG_REPORT_MANAGER,
	DBG_COMPONENT_NUM,
};

enum wlan_debug_client_destination {
	DBG_CLIENT_OUTPUT_START = 0,
	DBG_CLIENT_TRACE_EVENT = DBG_CLIENT_OUTPUT_START,
	DBG_CLIENT_SYSFS,
	DBG_CLIENT_OUTPUT_NUM
};

struct wlan_packet_sniffer_control_param {
	bool enable;
	bool enable_desc;
	u16 capture_size;
	enum location location;
	enum packet_runout_action behavior;
} __attribute__((packed, aligned(4)));

struct wlan_log_system_control_cmd {
	u8 enable; // 0: disable all modules, 1: enable all modules
	u8 multi_module_ctrl; // 1: ignore the enable setting, set all modules based on the modules_level, 0: follow the enable setting and module.
	enum location location;
	enum log_level level;
	union {
		u32 module;
		enum log_level modules_level[LOG_MODULE_NUM];
	};
} __attribute__((packed, aligned(4)));

struct wlan_log_system_control_param {
	enum log_level level[LOG_MODULE_NUM];
	enum location location;
};
static_assert(MAX_PARAM_SIZE <= sizeof(struct wlan_log_system_control_param));
struct wlan_debug_component_client {
	u64 component_shared_mem_addr;
	u32 component_shared_mem_size;
	struct wlan_debug_controller_client *ctlr_client;
	char param[MAX_PARAM_SIZE];
};

struct wlan_debug_component_client_ops {
	void (*init_param)(struct wlan_debug_component_client *);
	void (*entry_register)(struct wlan_dbg_entry *, struct noa_wlan_entry *);
	int (*notify_driver)(struct wlan_debug_component_client *, u16 *);
};

struct wlan_debug_controller_client {
	enum wlan_debug_client_destination destination;
	u8 read_clear_mask;
	struct noa_wlan_client *wlan_client;
	struct wlan_dbg_entry *component_entries[DBG_COMPONENT_NUM];
	struct wlan_debug_component_client_ops *ops[DBG_COMPONENT_NUM];
};

static inline void wlan_dbg_token_to_upper(char *str)
{
	while (*str) {
		*str = toupper(*str);
		str++;
	}
}

static inline const char *wlan_dbg_get_log_location_name(enum location location)
{
	switch (location) {
		CASE_LOCATION_NAME(DRAM);
		CASE_LOCATION_NAME(UART);
		CASE_LOCATION_NAME(DRAM_AND_UART);
	default:
		break;
	}

	return "Unknown";
}

extern struct wlan_debug_component_client_ops wlan_dbg_packet_sniffer_client_ops;
extern struct wlan_debug_component_client_ops wlan_dbg_log_system_client_ops;

/**
 * @brief Allocate the debug component client
 *
 * Allocate a debug component client under the debug entry.
 *
 * @param[in] dbg_entry The parent entry that the debug component client resides.
 */
extern void wlan_debug_component_client_alloc(struct noa_wlan_entry *dbg_entry);

/**
 * @brief Deallocate the debug component client
 *
 * Allocate a debug component client under the debug entry.
 *
 * @param[in] dbg_entry The parent entry that the debug component client resides.
 */
extern void wlan_debug_component_client_dealloc(struct noa_wlan_entry *dbg_entry);

/**
 * @brief Init shared memory related information for wlan debug component client
 *
 * Initializes the shared memory address and shared memory size for every debug component client.
 *
 * @param[in] wlan_client The pointer to the client of "wlan" entry.
 */
extern void wlan_debug_component_client_init_shared_mem_info(struct noa_wlan_client *wlan_client);

/**
 * @brief Synchronize the local copy of shared memory to the actual shared memory
 */
extern void wlan_debug_sync_local_copy_to_shared_mem(struct noa_wlan_client *client);
#endif