#ifndef SYS_IF_LOG_SYS_IF_LOG_H
#define SYS_IF_LOG_SYS_IF_LOG_H

#if defined(__KERNEL__)
#include <linux/printk.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/rtc.h>
#else
#include <chrono>
#include "pw_log/log.h"
#include "pw_chrono/system_clock.h"
#include "pw_hex_dump/hex_dump.h"
#endif
#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "sys_if/memory/sys_if_memory.h"

#define MAX_PREFIX_LEN 30
#define MAX_LOG_ENTRY_SIZE 150

/// @brief System interface for logging debug messages.
///
/// This macro provides a unified way to log debug messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#if defined(__KERNEL__)
#define SYS_IF_LOG_DEBUG(fmt, ...) pr_debug(fmt, ##__VA_ARGS__)
#else
#define SYS_IF_LOG_DEBUG(fmt, ...) PW_LOG_DEBUG(fmt, ##__VA_ARGS__)
#endif

/// @brief System interface for logging info messages.
///
/// This macro provides a unified way to log info messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#if defined(__KERNEL__)
#define SYS_IF_LOG_INFO(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
#define SYS_IF_LOG_INFO(fmt, ...) PW_LOG_INFO(fmt, ##__VA_ARGS__)
#endif

/// @brief System interface for logging warning messages.
///
/// This macro provides a unified way to log warning messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#if defined(__KERNEL__)
#define SYS_IF_LOG_WARN(fmt, ...) pr_warn(fmt, ##__VA_ARGS__)
#else
#define SYS_IF_LOG_WARN(fmt, ...) PW_LOG_WARN(fmt, ##__VA_ARGS__)
#endif

/// @brief System interface for logging error messages.
///
/// This macro provides a unified way to log error messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#if defined(__KERNEL__)
#define SYS_IF_LOG_ERROR(fmt, ...) pr_err(fmt, ##__VA_ARGS__)
#else
#define SYS_IF_LOG_ERROR(fmt, ...) PW_LOG_ERROR(fmt, ##__VA_ARGS__)
#endif

#if defined(__KERNEL__)
#define SYS_IF_HEX_DUMP(prefix, pkt_addr, pkt_len)                                                 \
	do {                                                                                       \
		char fmt_prefix[MAX_PREFIX_LEN];                                           \
		sprintf(fmt_prefix, "[WLAN] %s ", prefix);                                                \
		print_hex_dump(KERN_INFO, fmt_prefix, DUMP_PREFIX_NONE, 16, 1, (void *)pkt_addr,   \
			       pkt_len, false);                                                    \
	} while (0);
#else
#define SYS_IF_HEX_DUMP(prefix, pkt_addr, pkt_len)                                                 \
	do {                                                                                       \
		pw::ConstByteSpan pkt_span(reinterpret_cast<const std::byte *>(pkt_addr),          \
					   pkt_len);                                               \
		std::array<char, MAX_LOG_ENTRY_SIZE> temp;                                    \
		pw::dump::FormattedHexDumper::Flags config_flags = { .bytes_per_line = 16,         \
								     .group_every = 1,             \
								     .show_ascii = false,          \
								     .show_header = false };       \
		pw::dump::FormattedHexDumper hex_dumper(temp, config_flags);                       \
		hex_dumper.BeginDump(pkt_span);                                                    \
		while (hex_dumper.DumpLine().ok()) {                                               \
			PW_LOG_INFO("[WLAN] %s "                                                          \
				    "%s\n",                                                        \
				    prefix, temp.data());                                          \
		}                                                                                  \
	} while (0)
#endif

/// @brief Get the current timestamp in string.
///
/// @param[in] buf_len The buffer length of time_buffer.
/// @param[out] time_buffer The time string is formatted within this time_buffer.
///
/// @note This function is only available in the kernel environment.
/// In the user space environment, it is not defined.

static inline void SysIfLogTimestamp(uint32_t buf_len, char *time_buffer)
{
#if defined(__KERNEL__)
	struct timespec64 ts = ktime_to_timespec64(ktime_get_boottime());
	struct rtc_time tm;
	rtc_time64_to_tm(ts.tv_sec, &tm);
	snprintf(time_buffer, buf_len, "%d:%d:%d", tm.tm_hour, tm.tm_min, tm.tm_sec);
#else
	uint32_t time_num =
		static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
					      pw::chrono::SystemClock::now().time_since_epoch())
					      .count());
	time_t raw_time = static_cast<time_t>(time_num / 1000);
	struct tm *time_info = localtime(&raw_time);
	strftime(time_buffer, buf_len, "%H:%M:%S", time_info);
#endif
}

#endif /* SYS_IF_LOG_SYS_IF_LOG_H */
