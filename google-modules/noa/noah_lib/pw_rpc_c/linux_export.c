#include <linux/export.h>

#include "pw_rpc_c/pw_rpc_client.h"

EXPORT_SYMBOL_GPL(PwRpcClientInit);
EXPORT_SYMBOL_GPL(PwRpcClientDeinit);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeCancel);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeError);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeUnary);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeUnaryPb);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeStreaming);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeStreamingPb);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeStreamingNext);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeStreamingNextPb);
EXPORT_SYMBOL_GPL(PwRpcClientInvokeStreamingComplete);
EXPORT_SYMBOL_GPL(PwRpcClientDropCall);
EXPORT_SYMBOL_GPL(PwRpcClientProcessPacket);
EXPORT_SYMBOL_GPL(PwRpcClientDeserializeResponse);
