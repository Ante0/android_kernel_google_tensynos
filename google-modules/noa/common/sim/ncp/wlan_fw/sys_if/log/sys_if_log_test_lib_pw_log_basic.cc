#include "sys_if_log_test_lib.h"

#include <cstring>
#include <cstdint>

#include <string_view>
#include <string>

#include "pw_log_basic/log_basic.h"
#include "pw_sys_io/sys_io.h"
#include "pw_toolchain/no_destructor.h"

namespace noa::service::wlan_service
{

typedef struct LogTestContext {
	char *output_buffer;
	uint32_t output_buffer_size;
} LogTestContext;

static LogTestContext &GetLogTestContext(void)
{
	static pw::NoDestructor<LogTestContext> context;
	return *context;
}

void SysIfLogTestInit(uint32_t output_buffer_size, char *output_buffer)
{
	LogTestContext &context = GetLogTestContext();

	context.output_buffer = output_buffer;
	context.output_buffer_size = output_buffer_size;
	SysIfLogTestFlushOutputBuffer();
	pw::log_basic::SetOutput([](std::string_view log) {
		LogTestContext &context = GetLogTestContext();
		size_t current_len = strlen(context.output_buffer);
		size_t available_space = (context.output_buffer_size > current_len + 1) ?
						 (context.output_buffer_size - current_len - 1) :
						 0;
		size_t copy_len = std::min(log.length(), available_space);

		if (copy_len > 0) {
			std::strncat(context.output_buffer, log.data(), copy_len);
		}

		if (available_space == 0 || available_space < log.length()) {
			const char *overflow_msg = "[LOG BUFFER FULL - FURTHER LOGS DROPPED]\n";
			pw::sys_io::WriteLine(std::string_view(overflow_msg)).IgnoreError();
			pw::log_basic::SetOutput([](std::string_view log) {
				std::string drop_msg = "[DROPPED] ";
				std::string result;
				result.reserve(drop_msg.length() + log.length() + 1);
				result.append(drop_msg);
				result.append(log);
				pw::sys_io::WriteLine(result).IgnoreError();
			});
		} else {
			pw::sys_io::WriteLine(log).IgnoreError();
		}
	});
}

void SysIfLogTestDeinit(void)
{
	LogTestContext &context = GetLogTestContext();

	pw::log_basic::SetOutput(
		[](std::string_view log) { pw::sys_io::WriteLine(log).IgnoreError(); });
	std::memset(&context, 0, sizeof(context));
}

void SysIfLogTestFlushOutputBuffer(void)
{
	LogTestContext &context = GetLogTestContext();
	if (context.output_buffer != nullptr && context.output_buffer_size != 0) {
		std::strncpy(context.output_buffer, "\0", context.output_buffer_size);
	}
}

} // namespace noa::service::wlan_service
