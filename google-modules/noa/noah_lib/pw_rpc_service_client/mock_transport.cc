#include "mock_transport.h"

#include <cstddef>
#include <cstdint>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

PwStatus TestRpcChannelOutputHandler(PwRpcClient* client, const uint8_t* buf,
                                     size_t len) {
  struct TestTransport* transport_info =
      reinterpret_cast<struct TestTransport*>(client->transport_info);
  transport_info->tx_bytes = 0;

  if (sizeof(transport_info->tx_buf) < len) {
    return kPwStatusResourceExhausted;
  }
  transport_info->tx_bytes = len;
  memcpy(transport_info->tx_buf, buf, len);
  return kPwStatusOk;
}

PwStatus TestRpcChannelInputWorker(PwRpcClient* /* client */) {
  return kPwStatusOk;
}
