// NOLINTBEGIN
//
// Linux kernel shall share this header to access IPC information.
// Keep coding style being consistent with Linux kernel.
//
#ifndef IPC_IPC_H
#define IPC_IPC_H
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
namespace noa::module::ipc {
#endif
#define kIpcInfoMagic (0x5cc5)
#define kIpcVersion (0x2)

struct IpcInfo {
  uint16_t magic;
  uint8_t version;
  uint8_t channel_num;
  uint32_t channel_info_addr;
} __attribute__((aligned(4)));

#define NOA_IPC_INFO(_channel_info_addr, _channel_num) \
  {                                                    \
      .magic = kIpcInfoMagic,                          \
      .version = kIpcVersion,                          \
      .channel_num = _channel_num,                     \
      .channel_info_addr = _channel_info_addr,         \
  }

enum ChannelEndpoint {
  kChannelEndpointNcp,
  kChannelEndpointNep,
  kChannelEndpointApc,
  kChannelEndpointCount,
};

enum ChannelType {
  kChannelTypeSram,
  kChannelTypeDram,
  kChannelTypeCount,
};

#define kIpcInvalidChannelId (0)
#define kChannelInfoMagic (0xBEEF)

struct IpcChannelInfo {
  struct {
    uint32_t size;
    uint8_t id;
    uint8_t source : 2;
    uint8_t sink : 2;
    uint8_t type : 2;
  } __attribute__((aligned(4))) config;

  union {
    // memory transport
    struct {
      uint32_t buffer_base;
      int16_t source_mba_client_id;
      int16_t sink_mba_client_id;
    } __attribute__((aligned(4))) memory;

    // mailbox transport
    struct {
      int16_t source_client_id;
      int16_t sink_client_id;
    } __attribute__((aligned(4))) mailbox;
  } __attribute__((aligned(4))) transport_info;

  struct {
    uint16_t magic;
    uint8_t producer_ready : 1;
    uint8_t consumer_ready : 1;
  } __attribute__((aligned(4))) status;
} __attribute__((aligned(4)));

#define IPC_CHANNEL_INFO_MEMORY_WITH_MBA(_id, _size, _source, _sink, _type,   \
                                         _buffer_base, _source_mba_client_id, \
                                         _sink_mba_client_id)                 \
  {                                                                           \
    .config = {.size = _size,                                                 \
               .id = _id,                                                     \
               .source = _source,                                             \
               .sink = _sink,                                                 \
               .type = _type},                                                \
    .transport_info = {.memory =                                              \
                           {                                                  \
                               .buffer_base = _buffer_base,                   \
                               .source_mba_client_id = _source_mba_client_id, \
                               .sink_mba_client_id = _sink_mba_client_id,     \
                           }},                                                \
    .status = {                                                               \
      .magic = 0x0,                                                           \
      .producer_ready = false,                                                \
      .consumer_ready = false,                                                \
    }                                                                         \
  }

#define IPC_CHANNEL_INFO_MEMORY(_id, _size, _source, _sink, _type,    \
                                _buffer_base)                         \
  IPC_CHANNEL_INFO_MEMORY_WITH_MBA(_id, _size, _source, _sink, _type, \
                                   _buffer_base, -1, -1)

#ifdef __cplusplus
}  // namespace noa::module::ipc
#endif
#endif /* IPC_IPC_H */
// NOLINTEND
