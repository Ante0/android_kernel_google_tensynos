#ifndef SYS_LOG_H
#define SYS_LOG_H

#include "pw_log/log.h"

/**
 * NCP_DEBUG is used to control too many debug logs. We can consider moving it
 * to NCP_DEBUG_LOG_ENABLED in the future.
 */
#define NCP_DEBUG 0  /* 0: Disable, 1: Enable */

#define NCP_INFO_LOG_ENABLED 0  /* 0: Disable, 1: Enable */
#define NCP_DATA_LOG_ENABLED 0  /* 0: Disable, 1: Enable */
#define NCP_DEBUG_LOG_ENABLED 0  /* 0: Disable, 1: Enable */

/// @brief System logging info messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#if NCP_INFO_LOG_ENABLED
#define NCP_MD_INFO(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_TX_INFO(fmt, ...) PW_LOG_INFO("[TX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_RX_INFO(fmt, ...) PW_LOG_INFO("[RX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_IRQ_INFO(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#define APC2NCP_INFO(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#else
#define NCP_MD_INFO(fmt, ...)
#define NCP_MD_TX_INFO(fmt, ...)
#define NCP_MD_RX_INFO(fmt, ...)
#define NCP_IRQ_INFO(fmt, ...)
#define APC2NCP_INFO(fmt, ...)
#endif

#if NCP_DATA_LOG_ENABLED
#define APC2NCP_DATA(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#else
#define APC2NCP_DATA(fmt, ...)
#endif

#if NCP_DEBUG_LOG_ENABLED
#define NCP_MD_DEBUG(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_TX_DEBUG(fmt, ...) PW_LOG_INFO("[TX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_RX_DEBUG(fmt, ...) PW_LOG_INFO("[RX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_IRQ_DEBUG(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#define APC2NCP_DEBUG(fmt, ...) PW_LOG_INFO("%s:" fmt, __func__, ##__VA_ARGS__)
#else
#define NCP_MD_DEBUG(fmt, ...)
#define NCP_MD_TX_DEBUG(fmt, ...)
#define NCP_MD_RX_DEBUG(fmt, ...)
#define NCP_IRQ_DEBUG(fmt, ...)
#define APC2NCP_DEBUG(fmt, ...)
#endif

/// @brief System logging error messages.
///
/// @param[in] fmt The format string.
/// @param[in] ... The arguments to be formatted.
#define NCP_MD_ERROR(fmt, ...) PW_LOG_ERROR("%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_TX_ERROR(fmt, ...) PW_LOG_ERROR("[TX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_MD_RX_ERROR(fmt, ...) PW_LOG_ERROR("[RX]%s:" fmt, __func__, ##__VA_ARGS__)
#define NCP_IRQ_ERROR(fmt, ...) PW_LOG_ERROR("%s:" fmt, __func__, ##__VA_ARGS__)
#define APC2NCP_ERROR(fmt, ...) PW_LOG_ERROR("%s:" fmt, __func__, ##__VA_ARGS__)

#endif /* SYS_LOG_H */
