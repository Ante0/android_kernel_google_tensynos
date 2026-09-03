#ifndef WLAN_LOG_WLAN_LOG_H
#define WLAN_LOG_WLAN_LOG_H

#include "sys_if/log/sys_if_log.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "sys_if/types/types.h"

#define DEFAULT_LOG_LEVEL kWlanLogLevelDebug

typedef enum WlanLogLevel {
	kWlanLogLevelStart = 0,
	kWlanLogLevelDisableAll = kWlanLogLevelStart,
	kWlanLogLevelError,
	kWlanLogLevelWarn,
	kWlanLogLevelInfo,
	kWlanLogLevelDebug,
	kWlanLogLevelEnableAll = kWlanLogLevelDebug,
	kWlanLogLevelEnd,
	kWlanLogLevelNum = kWlanLogLevelEnd,
} WlanLogLevel;

typedef enum WlanLogModule {
	kWlanLogModuleStart = 0,
	kWlanLogModuleDp = kWlanLogModuleStart,
	kWlanLogModuleBm,
	kWlanLogModuleShm,
	kWlanLogModuleStats,
	kWlanLogModuleNep,
	kWlanLogModuleRpc,
	kWlanLogModuleCfg,
	kWlanLogModuleTxCpl,
	kWlanLogModuleRxPost,
	kWlanLogModuleIsr,
	kWlanLogModuleNepBufferPool,
	kWlanLogModuleWdev,
	kWlanLogModuleTx,
	kWlanLogModuleRx,
	kWlanLogModuleFsm,
	kWlanLogModuleShell,
	kWlanLogModulePm,
	kWlanLogModuleEnd,
	kWlanLogModuleNum = kWlanLogModuleEnd,
} WlanLogModule;

#define LOG_PREFIX_MAX_SIZE 100

#define WLAN_LOG_DEBUG(module, fmt, ...)                                                           \
	WlanLog(kWlanLogModule##module, kWlanLogLevelDebug, fmt, ##__VA_ARGS__);

#define WLAN_LOG_INFO(module, fmt, ...)                                                            \
	WlanLog(kWlanLogModule##module, kWlanLogLevelInfo, fmt, ##__VA_ARGS__);

#define WLAN_LOG_WARN(module, fmt, ...)                                                            \
	WlanLog(kWlanLogModule##module, kWlanLogLevelWarn, fmt, ##__VA_ARGS__)

#define WLAN_LOG_ERROR(module, fmt, ...)                                                           \
	WlanLog(kWlanLogModule##module, kWlanLogLevelError, fmt, ##__VA_ARGS__)

#define WLAN_LOG_DEBUG_HEX64_VALUE(module, prefix, value)                                          \
	WLAN_LOG_DEBUG(module, "%s value upper: %x lower: %x\n", prefix, (u32)((value) >> 32),     \
		       (u32)((value) & 0xFFFFFFFF))

#define WLAN_LOG_INFO_HEX64_VALUE(module, prefix, value)                                           \
	WLAN_LOG_INFO(module, "%s value upper: %x lower: %x\n", prefix, (u32)((value) >> 32),      \
		      (u32)((value) & 0xFFFFFFFF))

#define WLAN_LOG_WARN_HEX64_VALUE(module, prefix, value)                                           \
	WLAN_LOG_WARN(module, "%s value upper: %x lower: %x\n", prefix, (u32)((value) >> 32),      \
		      (u32)((value) & 0xFFFFFFFF))

#define WLAN_LOG_ERROR_HEX64_VALUE(module, prefix, value)                                          \
	WLAN_LOG_ERROR(module, "%s value upper: %x lower: %x\n", prefix, (u32)((value) >> 32),     \
		       (u32)((value) & 0xFFFFFFFF))

/// @brief Initializes the WLAN log module.
///
/// This function initializes the WLAN log module with default
/// settings. It should be called once at system startup before any
/// logging functions are used.
extern void WlanLogInit(void);

/// @brief Set up DRAM related configurations
///
/// @param[in] dram_addr the start address of the Log System in the DRAM
/// @param[in] dram_size the size of the Log System in the DRAM
extern void WlanLogSetDramCtrl(uint64_t dram_addr, uint32_t dram_size);

/// @brief Checks if a log module is enabled for a specific log level.
///
/// @param[in] module The WLAN log module to check.
/// @param[in] level The log level to check.
///
/// @return true if the log module is enabled for the specified level,
/// false otherwise.
extern bool WlanLogModuleIsEnabled(const enum WlanLogModule module, const enum WlanLogLevel level);

/// @brief Get the log location for a WLAN log module.
///
/// @param[in] module The WLAN log module.
///
/// @return The log location for the specified module.
extern enum Location WlanLogModuleGetLocation(const enum WlanLogModule module);

/// @brief Set the log location for a WLAN log module.
///
/// @param[in] module The WLAN log module.
/// @param[in] location The log location to set.
extern void WlanLogModuleSetLogLocation(const enum WlanLogModule module,
					const enum Location location);

/// @brief Set the log location for ALL WLAN log module.
///
/// @param[in] location The log location to set.
extern void WlanLogSetLocation(const enum Location location);

/// @brief Format a log message for a WLAN log module.
///
/// @param[in] module The WLAN log module.
/// @param[in] level The log level.
/// @param[in] result_size The size of result buffer.
/// @param[out] result The formatted result.
extern void WlanLogFormat(const enum WlanLogModule module, const enum WlanLogLevel level,
			  uint32_t result_size, char *result);

/// @brief Dump the formatted log to the DRAM Log System section
///
/// @param[in] logs the formatted log
/// @param[in] log_size the size of the log
extern void WlanLogDramLog(char *logs, uint32_t log_size);

/// @brief Sets the log level for all WLAN log modules.
///
/// @param[in] categories_level the level array indicates log level for each category
void WlanLogModuleSetLogLevelForMultiModules(
	const enum WlanLogLevel categories_level[kWlanLogModuleNum]);

/// @brief Sets the log level for a WLAN log module.
///
/// This function enables logging for the specified module up to the
/// given log level. For example, if the level is set to
/// kWlanLogLevelWarn, then messages with levels kWlanLogLevelWarn,
/// kWlanLogLevelError, and kWlanLogLevelFatal will be logged.
///
/// @param[in] module The WLAN log module to configure.
/// @param[in] level The log level to set.
extern void WlanLogModuleSetLogLevel(const enum WlanLogModule module,
				     const enum WlanLogLevel level);

/// @brief Clears the log level for a WLAN log module.
///
/// This function disables logging for the specified module at the
/// given log level.
///
/// @param[in] module The WLAN log module to configure.
/// @param[in] level The log level to clear.
extern void WlanLogModuleClearLogLevel(const enum WlanLogModule module,
				       const enum WlanLogLevel level);

/// @brief Enables all WLAN log modules for all log levels.
extern void WlanLogEnableAllModules(void);

/// @brief Disables all WLAN log modules for all log levels.
extern void WlanLogDisableAllModules(void);

extern void WlanLog(WlanLogModule module, WlanLogLevel level, const char *fmt, ...);

#endif /* WLAN_LOG_WLAN_LOG_H */
