#pragma once
#include <cstddef>
#include <cstdint>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

struct TestTransport {
  uint8_t tx_buf[512] = {0};
  size_t tx_bytes = 0;
};

PwStatus TestRpcChannelOutputHandler(PwRpcClient* client, const uint8_t* buf,
                                     size_t len);

PwStatus TestRpcChannelInputWorker(PwRpcClient* client);
