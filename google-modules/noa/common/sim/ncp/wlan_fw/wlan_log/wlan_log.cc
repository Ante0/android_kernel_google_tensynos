#include "wlan_log.h"

#include "wlan_cast.h"
#include "sys_if/log/sys_if_log.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "common/compiler.h"

#define LOG_BASIC_ENTRY_SIZE 200

#define WLAN_LOG_CASE_MODULE_NAME(module)                                                          \
	case kWlanLogModule##module:                                                               \
		return #module

static const char *GetWlanLogLevelName(WlanLogLevel level)
{
	switch (level) {
	case kWlanLogLevelError:
		return "E";
	case kWlanLogLevelWarn:
		return "W";
	case kWlanLogLevelInfo:
		return "I";
	case kWlanLogLevelDebug:
		return "D";
	default:
		break;
	}

	return "U";
}

static const char *GetWlanLogModuleName(WlanLogModule module)
{
	switch (module) {
		WLAN_LOG_CASE_MODULE_NAME(Dp);
		WLAN_LOG_CASE_MODULE_NAME(Bm);
		WLAN_LOG_CASE_MODULE_NAME(Shm);
		WLAN_LOG_CASE_MODULE_NAME(Stats);
		WLAN_LOG_CASE_MODULE_NAME(Nep);
		WLAN_LOG_CASE_MODULE_NAME(Rpc);
		WLAN_LOG_CASE_MODULE_NAME(Cfg);
		WLAN_LOG_CASE_MODULE_NAME(TxCpl);
		WLAN_LOG_CASE_MODULE_NAME(RxPost);
		WLAN_LOG_CASE_MODULE_NAME(Isr);
		WLAN_LOG_CASE_MODULE_NAME(NepBufferPool);
		WLAN_LOG_CASE_MODULE_NAME(Wdev);
		WLAN_LOG_CASE_MODULE_NAME(Tx);
		WLAN_LOG_CASE_MODULE_NAME(Rx);
		WLAN_LOG_CASE_MODULE_NAME(Fsm);
		WLAN_LOG_CASE_MODULE_NAME(Shell);
	default:
		break;
	}

	return "Unknown";
}

struct WlanLogCtrl {
	enum WlanLogLevel log_level;
	enum Location location;
};

struct WlanLogDramCtrl {
	uint64_t dram_addr;
	uint32_t dram_size;
	char *write_ptr;
	spinlock_t lock;
};

SEC_FAST_DATA static struct WlanLogCtrl g_wlan_log_ctrl[kWlanLogModuleNum];
SEC_FAST_DATA static struct WlanLogDramCtrl g_wlan_log_dram_ctrl;

void WlanLogFormat(const enum WlanLogModule module, const enum WlanLogLevel level,
		   uint32_t result_size, char *result)
{
	// Only fill in the value of module name and level name,
	// the actual timestamp value should be left for sys_if_log.h
	const char *module_name = GetWlanLogModuleName(module);
	const char *level_name = GetWlanLogLevelName(level);
	char time_buffer[40];

	SysIfLogTimestamp(sizeof(time_buffer), time_buffer);
	snprintf(result, result_size, "[%s][%s][%s]: ", time_buffer, level_name, module_name);
}

void WlanLogDramLog(char *logs, uint32_t log_size)
{
	static uint32_t log_idx = 1;
	struct SyncLogEntryHeader {
		uint32_t log_size;
		uint32_t log_idx;
	} header = {
		.log_size = log_size,
		.log_idx = log_idx++,
	};
	uint64_t write_addr = WLAN_REINTERPRET_CAST(uint64_t, g_wlan_log_dram_ctrl.write_ptr);

	if (g_wlan_log_dram_ctrl.dram_addr == 0 || g_wlan_log_dram_ctrl.dram_size == 0) {
		return;
	}

	spin_lock(&g_wlan_log_dram_ctrl.lock);

	if (write_addr + log_size + sizeof(struct SyncLogEntryHeader) >=
	    g_wlan_log_dram_ctrl.dram_addr + g_wlan_log_dram_ctrl.dram_size) {
		write_addr = g_wlan_log_dram_ctrl.dram_addr;
	}

	memcpy(WLAN_REINTERPRET_CAST(void *, write_addr), &header,
	       sizeof(struct SyncLogEntryHeader));
	write_addr += sizeof(struct SyncLogEntryHeader);
	memcpy(WLAN_REINTERPRET_CAST(char *, write_addr), logs, log_size);
	write_addr += log_size;
	g_wlan_log_dram_ctrl.write_ptr = WLAN_REINTERPRET_CAST(char *, write_addr);
	SysIfFlushDCache(g_wlan_log_dram_ctrl.dram_addr, g_wlan_log_dram_ctrl.dram_size);

	spin_unlock(&g_wlan_log_dram_ctrl.lock);
}

void WlanLogInit(void)
{
	uint8_t module;

	memset(g_wlan_log_ctrl, 0, kWlanLogModuleNum * sizeof(struct WlanLogCtrl));

	for (module = kWlanLogModuleStart; module < kWlanLogModuleEnd; module++) {
		g_wlan_log_ctrl[module].location = kUart;
		g_wlan_log_ctrl[module].log_level = DEFAULT_LOG_LEVEL;
	}

	spin_lock_init(&g_wlan_log_dram_ctrl.lock);
}

void WlanLogSetDramCtrl(uint64_t dram_addr, uint32_t dram_size)
{
	g_wlan_log_dram_ctrl.dram_addr = dram_addr;
	g_wlan_log_dram_ctrl.dram_size = dram_size;
	g_wlan_log_dram_ctrl.write_ptr = WLAN_REINTERPRET_CAST(char *, dram_addr);
}

bool WlanLogModuleIsEnabled(const enum WlanLogModule module, const enum WlanLogLevel level)
{
	if (module >= kWlanLogModuleNum) {
		return false;
	}

	return g_wlan_log_ctrl[module].log_level >= level;
}

enum Location WlanLogModuleGetLocation(const enum WlanLogModule module)
{
	if (module < kWlanLogModuleNum) {
		return WLAN_STATIC_CAST(enum Location, g_wlan_log_ctrl[module].location);
	}
	return kUart;
}

void WlanLogModuleSetLogLocation(const enum WlanLogModule module, const enum Location location)
{
	if (module < kWlanLogModuleNum) {
		g_wlan_log_ctrl[module].location = WLAN_STATIC_CAST(enum Location, location);
	}
}

void WlanLogSetLocation(const enum Location location)
{
	int i;

	for (i = kWlanLogModuleStart; i < kWlanLogModuleEnd; i++) {
		WlanLogModuleSetLogLocation(WLAN_STATIC_CAST(WlanLogModule, i), location);
	}
}

void WlanLogModuleSetLogLevelForMultiModules(
	const enum WlanLogLevel categories_level[kWlanLogModuleNum])
{
	uint8_t module;

	for (module = kWlanLogModuleStart; module < kWlanLogModuleEnd; module++) {
		g_wlan_log_ctrl[module].log_level = categories_level[module];
	}
}

void WlanLogModuleSetLogLevel(const enum WlanLogModule module, const enum WlanLogLevel level)
{
	if (module < kWlanLogModuleNum) {
		g_wlan_log_ctrl[module].log_level = level;
	}
}

void WlanLogModuleClearLogLevel(const enum WlanLogModule module, const enum WlanLogLevel level)
{
	if (module < kWlanLogModuleNum) {
		if (level > kWlanLogLevelStart) {
			g_wlan_log_ctrl[module].log_level =
				WLAN_STATIC_CAST(enum WlanLogLevel, level - 1);
		} else {
			g_wlan_log_ctrl[module].log_level = kWlanLogLevelDisableAll;
		}
	}
}

void WlanLogEnableAllModules(void)
{
	uint8_t module;

	for (module = 0; module < kWlanLogModuleNum; module++) {
		g_wlan_log_ctrl[module].log_level = kWlanLogLevelEnableAll;
	}
}

void WlanLogDisableAllModules(void)
{
	uint8_t module;

	for (module = 0; module < kWlanLogModuleNum; module++) {
		g_wlan_log_ctrl[module].log_level = kWlanLogLevelDisableAll;
	}
}

void WlanLog(WlanLogModule module, WlanLogLevel level, const char *fmt, ...)
{
	int cnt = 0;
	static char combined_log[LOG_BASIC_ENTRY_SIZE];
	static char result[LOG_PREFIX_MAX_SIZE];

	va_list args;
	va_start(args, fmt);

	// Gen log prefix
	WlanLogFormat(module, level, LOG_PREFIX_MAX_SIZE, result);
	cnt += snprintf(combined_log, LOG_BASIC_ENTRY_SIZE - 1, "%s", result);
	// Gen log contents
	cnt += vsnprintf(combined_log + cnt, LOG_BASIC_ENTRY_SIZE - 1 - cnt, fmt, args);

	va_end(args);

	if (WlanLogModuleIsEnabled(module, level)) {
		if (WlanLogModuleGetLocation(module) != kDram) {
			switch (level) {
			case kWlanLogLevelError:
				SYS_IF_LOG_ERROR("%s", combined_log);
				break;
			case kWlanLogLevelWarn:
				SYS_IF_LOG_WARN("%s", combined_log);
				break;
			case kWlanLogLevelInfo:
				SYS_IF_LOG_INFO("%s", combined_log);
				break;
			case kWlanLogLevelDebug:
				SYS_IF_LOG_DEBUG("%s", combined_log);
				break;
			default:
				break;
			}
		} else if (WlanLogModuleGetLocation(module) != kUart) {
			WlanLogDramLog(combined_log, cnt);
		}
	}
}
