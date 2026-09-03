#pragma once

#include <cstdint>
namespace noa::service::wlan_service
{

/// @brief Initializes the SysIfLogTest.
///
/// @param[in] output_buffer_size The size of the output buffer in bytes.
/// @param[in] output_buffer A pointer to the buffer where log messages will
/// be written.
void SysIfLogTestInit(uint32_t output_buffer_size, char *output_buffer);

/// @brief Deinitializes the SysIfLogTest.
void SysIfLogTestDeinit(void);

/// @brief Flushes the output buffer.
void SysIfLogTestFlushOutputBuffer(void);

} // namespace noa::service::wlan_service
