#ifndef SYS_LOG_H
#define SYS_LOG_H

#include "pw_log/log.h"

/// @brief System logging info messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#define NCP_MD_INFO(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_TX_INFO(fmt, ...) PW_LOG_INFO("[TX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_RX_INFO(fmt, ...) PW_LOG_INFO("[RX]%s:" fmt, __func__, ##__VA_ARGS__)

/// @brief System logging error messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#define NCP_MD_ERROR(fmt, ...) PW_LOG_ERROR("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_TX_ERROR(fmt, ...) PW_LOG_ERROR("[TX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_RX_ERROR(fmt, ...) PW_LOG_ERROR("[RX]%s:" fmt, __func__, ##__VA_ARGS__)

/**
 * @brief System logging for high-frequency data path messages.
 *
 * This macro is intended for logging within performance-critical data paths
 * (e.g., interrupt handlers, per-packet processing). To avoid performance
 * impact, these logs are completely compiled out and generate no code.
 */
#define NCP_MD_DATA_LIMIT(fmt, ...) do {} while (0)

#endif /* SYS_LOG_H */
