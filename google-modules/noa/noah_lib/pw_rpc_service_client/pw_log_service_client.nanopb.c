#include "pw_rpc_service_client/pw_log_service_client.nanopb.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/slab.h>
#include <linux/types.h>

#define PRId32 "d"
#define PRIu32 "u"
#define malloc(x) kmalloc(x, GFP_KERNEL)
#define free(x) kfree(x)
#else
#include <stdio.h>
#endif

#include "pb_decode.h"
#include "pw_log/proto/log.pb.h"
#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

struct LogStringCallbackInfo {
  LogStringCallback callback;
  void* context;
};

/**
 * @brief Decode a string from the encoded bytes.
 *
 * @param[in] stream nanopb buffer.
 * @param[in] field The encoded field.
 * @param[out] arg The buffer which contains the decoded string.
 * @return The result of decoding the string from the bytes.
 * @retval true A string is read from the buffer successfully.
 * @retval false The string cannot be decoded normally.
 */
static bool ReadString(pb_istream_t* stream, const pb_field_t* field,
                       void** arg) {
  (void)field;
  // The funcs arg is the pointer of a char pointer to store the allocated
  // buffer. The handler arg is the pointer of the funcs arg. As a result,
  // deferencing arg will get a pointer of a char pointer.
  uint8_t** str = (uint8_t**)*arg;
  // Add trailing zero.
  uint8_t* tmp = (uint8_t*)malloc(stream->bytes_left + 1);
  if (!tmp) {
    return false;
  }
  memset(tmp, 0, stream->bytes_left + 1);

  if (!pb_read(stream, tmp, stream->bytes_left)) {
    free(tmp);
    return false;
  }
  *str = tmp;
  return true;
}

static bool ReadEntries(pb_istream_t* stream, const pb_field_t* field,
                        void** arg) {
  (void)field;
  struct LogStringCallbackInfo* callback_info =
      (struct LogStringCallbackInfo*)*arg;
  uint8_t* message = NULL;
  uint8_t* module = NULL;
  uint8_t* file = NULL;
  uint8_t* thread = NULL;

  pw_log_LogEntry entry;
  memset(&entry, 0, sizeof(pw_log_LogEntry));
  entry.message.funcs.decode = ReadString;
  entry.message.arg = &message;
  entry.module.funcs.decode = ReadString;
  entry.module.arg = &module;
  entry.file.funcs.decode = ReadString;
  entry.file.arg = &file;
  entry.thread.funcs.decode = ReadString;
  entry.thread.arg = &thread;

  char buf[512] = {0};
  bool decode_result = pb_decode(stream, pw_log_LogEntry_fields, &entry);
  if (decode_result) {
    uint32_t line = entry.line_level >> 3;
    uint32_t level = entry.line_level & 0x7;
    snprintf(buf, sizeof(buf),
             "%08" PRId32 " <%" PRIu32 "> [%s] %s (%s:%" PRIu32 ")",
             (int32_t)entry.time.timestamp, level,
             (module ? (char*)module : ""), message, file, line);
    callback_info->callback(callback_info->context, buf);
  }

  if (message) {
    free(message);
  }
  if (module) {
    free(module);
  }
  if (file) {
    free(file);
  }
  if (thread) {
    free(thread);
  }
  return decode_result;
}

PwStatus PwLogServiceListenOnNext(const uint8_t* bytes, size_t len,
                                  LogStringCallback callback, void* context) {
  pw_log_LogEntries entries = {0};
  if (!bytes || !len) {
    return kPwStatusOk;
  }

  struct LogStringCallbackInfo info = {
      .callback = callback,
      .context = context,
  };

  entries.entries.funcs.decode = ReadEntries;
  entries.entries.arg = (void*)&info;
  return PwRpcClientDeserializeResponse(bytes, len, pw_log_LogEntries_fields,
                                        &entries);
}
