#ifndef PW_RPC_SERVICE_CLIENT_H
#define PW_RPC_SERVICE_CLIENT_H

#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

#ifdef __cplusplus
extern "C" {
#endif

///
/// @brief Generates a function declaration for an unary RPC method.
///
/// Assume that there is a EchoService.Echo RPC method.
/// DECLARE_UNARY_RPC_METHOD(EchoService, Echo, some_namespace_EchoRequest);
/// The generated function looks like:
///
///   PwStatus EchoServiceEcho(
///     PwRpcClient* client,
///     some_namespace_EchoRequest* request,
///     PwRpcCallOnCompleteCallback on_complete,
///     PwRpcCallOnErrorCallback on_error,
///     void* call_context, uint32_t* call_id);
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed except the type of the request,
/// which is taken from the macro's request_type argument.
///
/// @param[in] client The client context holder.
/// @param[in] request The method request data
/// @param[in] on_complete The callback for RPC response. The signature is
///            void OnComplete(struct PwRpcCallStruct* call,
///                             const uint8_t* payload,size_t payload_size,
///                             PwStatus status);
/// @param[in] on_error The callback for RPC error. The signature is
///            void OnError(struct PwRpcCallStruct* call,
///                             PwStatus error);
/// @param[in] call_context The context for the call. This will be passed to
///            on_complete and on_error.
/// @param[out] call_id The call id for this call and can be null.
/// @return The invoking result
///            void OnError(struct PwRpcCallStruct* call,
///                             PwStatus error);
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_UNARY_RPC_METHOD(service_name, method_name, request_type) \
  __attribute__((weak)) PwStatus service_name##method_name(               \
      PwRpcClient* client, request_type* request,                         \
      PwRpcCallOnCompleteCallback on_complete,                            \
      PwRpcCallOnErrorCallback on_error, void* call_context,              \
      uint32_t* call_id)

///
/// Generate a default implementation for an unary method.
///
#define DEFINE_DEFAULT_UNARY_RPC_METHOD(service_name, service_id, method_name, \
                                        method_id, request_type)               \
  inline DECLARE_UNARY_RPC_METHOD(service_name, method_name, request_type) {   \
    if (!client || !request) {                                                 \
      return kPwStatusInvalidArgument;                                         \
    }                                                                          \
    return PwRpcClientInvokeUnaryPb(                                           \
        client, service_id, method_id, request_type##_fields, request,         \
        on_complete, on_error, call_context, call_id);                         \
  }

///
/// @brief Generates a function declaration for a server-streaming RPC method.
///
/// Example:
///
/// * Server-streaming RPC service definition:
/// @code{.proto}
///   service SomeService {
///     rpc SomeMethod(Request) returns (stream Response);
///   }
/// @endcode
///
/// * Macro usage (with the nanopb-generated message struct `some_namespace_Request`):
/// @code{.c}
///   DECLARE_SERVER_STREAMING_RPC_METHOD(SomeService, SomeMethod, some_namespace_Request);
/// @endcode
///
/// * Generated function signature:
/// @code{.c}
///   PwStatus SomeServiceSomeMethod(
///     PwRpcClient* client,
///     const some_namespace_Request* request,
///     PwRpcCallOnNextCallback on_next,
///     PwRpcCallOnCompleteCallback on_complete,
///     PwRpcCallOnErrorCallback on_error,
///     void* call_context,
///     uint32_t* call_id);
/// @endcode
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed except the type of the request,
/// which is taken from the macro's request_type argument.
///
/// This function initiates a server-streaming RPC call from the client
/// instance to the associated server service.
///
/// @param[in] client The client context holder.
/// @param[in] request The method request data
/// @param[in] on_next The callback for RPC server streaming response.
///            The signature is
///            void OnNext(struct PwRpcCallStruct* call,
///                             const uint8_t* payload,size_t payload_size);
/// @param[in] on_complete The callback for RPC response. The signature is
///            void OnComplete(struct PwRpcCallStruct* call,
///                             const uint8_t* payload,size_t payload_size,
///                             PwStatus status);
/// @param[in] on_error The callback for RPC error. The signature is
///            void OnError(struct PwRpcCallStruct* call,
///                             PwStatus error);
/// @param[in] call_context The context for the call. This will be passed to
///            on_complete and on_error.
/// @param[out] call_id The call id for this call and can be null.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_SERVER_STREAMING_RPC_METHOD(service_name, method_name, \
                                            request_type)              \
  __attribute__((weak)) PwStatus service_name##method_name(            \
      PwRpcClient* client, const request_type* request,                \
      PwRpcCallOnNextCallback on_next,                                 \
      PwRpcCallOnCompleteCallback on_complete,                         \
      PwRpcCallOnErrorCallback on_error, void* call_context,           \
      uint32_t* call_id)

///
/// @brief Generates a function declaration for a client-streaming RPC method.
///
/// Example:
///
/// * Client-streaming RPC service definition:
/// @code{.proto}
///   service SomeService {
///     rpc SomeMethod(stream Request) returns (Response);
///   }
/// @endcode
///
/// * Macro usage:
/// @code{.c}
///   DECLARE_CLIENT_STREAMING_RPC_METHOD(SomeService, SomeMethod);
/// @endcode
///
/// * Generated function signature:
/// @code{.c}
///   PwStatus SomeServiceSomeMethod(
///     PwRpcClient* client,
///     PwRpcCallOnCompleteCallback on_complete,
///     PwRpcCallOnErrorCallback on_error,
///     void* call_context,
///     uint32_t* call_id);
/// @endcode
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed except the type of the request,
/// which is taken from the macro's request_type argument.
///
/// This function initiates a client-streaming RPC call from the client
/// instance to the associated server service.
///
/// @param[in] client The client context holder.
/// @param[in] on_complete The callback for RPC response. The signature is
///            void OnComplete(struct PwRpcCallStruct* call,
///                             const uint8_t* payload,size_t payload_size,
///                             PwStatus status);
/// @param[in] on_error The callback for RPC error. The signature is
///            void OnError(struct PwRpcCallStruct* call,
///                             PwStatus error);
/// @param[in] call_context The context for the call. This will be passed to
///            on_complete and on_error.
/// @param[out] call_id The call id for this streaming call and can be null for
///             a non-client streaming method.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_CLIENT_STREAMING_RPC_METHOD(service_name, method_name) \
  __attribute__((weak)) PwStatus service_name##method_name(            \
      PwRpcClient* client, PwRpcCallOnCompleteCallback on_complete,    \
      PwRpcCallOnErrorCallback on_error, void* call_context,           \
      uint32_t* call_id)

///
/// @brief Generates a function declaration for a bidirectional-streaming method.
///
/// Example:
///
/// * Bidirectional-streaming RPC service definition:
/// @code{.proto}
///   service SomeService {
///     rpc SomeMethod(stream Request) returns (stream Response);
///   }
/// @endcode
///
/// * Macro usage:
/// @code{.c}
///   DECLARE_BIDIRECTIONAL_STREAMING_RPC_METHOD(SomeService, SomeMethod);
/// @endcode
///
/// * Generated function signature:
/// @code{.c}
///   PwStatus SomeServiceSomeMethod(
///     PwRpcClient* client,
///     PwRpcCallOnNextCallback on_next,
///     PwRpcCallOnCompleteCallback on_complete,
///     PwRpcCallOnErrorCallback on_error,
///     void* call_context,
///     uint32_t* call_id);
/// @endcode
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed except the type of the request,
/// which is taken from the macro's request_type argument.
///
/// This function initiates a bidirectional-streaming RPC call from the client
/// instance to the associated server service.
///
/// @param[in] client The client context holder.
/// @param[in] on_next The callback for RPC server streaming response.
///            The signature is
///            void OnNext(struct PwRpcCallStruct* call,
///                             const uint8_t* payload,size_t payload_size);
/// @param[in] on_complete The callback for RPC response. The signature is
///            void OnComplete(struct PwRpcCallStruct* call,
///                             const uint8_t* payload,size_t payload_size,
///                             PwStatus status);
/// @param[in] on_error The callback for RPC error. The signature is
///            void OnError(struct PwRpcCallStruct* call,
///                             PwStatus error);
/// @param[in] call_context The context for the call. This will be passed to
///            on_complete and on_error.
/// @param[out] call_id The call id for this streaming call and can be null for
///             a non-client streaming method.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_BIDIRECTIONAL_STREAMING_RPC_METHOD(service_name, method_name) \
  __attribute__((weak)) PwStatus service_name##method_name(                   \
      PwRpcClient* client, PwRpcCallOnNextCallback on_next,                   \
      PwRpcCallOnCompleteCallback on_complete,                                \
      PwRpcCallOnErrorCallback on_error, void* call_context,                  \
      uint32_t* call_id)

// Generate a default implementation for a server-streaming method.
#define DEFINE_DEFAULT_SERVER_STREAMING_RPC_METHOD(                     \
    service_name, service_id, method_name, method_id, request_type)     \
  inline DECLARE_SERVER_STREAMING_RPC_METHOD(service_name, method_name, \
                                             request_type) {            \
    if (!client || !request) {                                          \
      return kPwStatusInvalidArgument;                                  \
    }                                                                   \
    return PwRpcClientInvokeStreamingPb(                                \
        client, service_id, method_id, request_type##_fields, request,  \
        on_next, on_complete, on_error, call_context, call_id);         \
  }

// Generate a default implementation for a client-streaming method.
#define DEFINE_DEFAULT_CLIENT_STREAMING_RPC_METHOD(service_name, service_id,  \
                                                   method_name, method_id)    \
  inline DECLARE_CLIENT_STREAMING_RPC_METHOD(service_name, method_name) {     \
    if (!client) {                                                            \
      return kPwStatusInvalidArgument;                                        \
    }                                                                         \
    return PwRpcClientInvokeStreaming(client, service_id, method_id, NULL, 0, \
                                      NULL, on_complete, on_error,            \
                                      call_context, call_id);                 \
  }

// Generate a default implementation for a bidirectional-streaming method.
#define DEFINE_DEFAULT_BIDIRECTIONAL_STREAMING_RPC_METHOD(                    \
    service_name, service_id, method_name, method_id)                         \
  inline DECLARE_BIDIRECTIONAL_STREAMING_RPC_METHOD(service_name,             \
                                                    method_name) {            \
    if (!client) {                                                            \
      return kPwStatusInvalidArgument;                                        \
    }                                                                         \
    return PwRpcClientInvokeStreaming(client, service_id, method_id, NULL, 0, \
                                      on_next, on_complete, on_error,         \
                                      call_context, call_id);                 \
  }

///
/// @brief Generates a function declaration for a streaming RPC next method.
///
/// Assume that there is a EchoService.Hail RPC method.
/// DECLARE_STREAMING_NEXT_RPC_METHOD(EchoService, Hail, some_namespace_HailRequest);
/// The generated function looks like:
///
///   PwStatus EchoServiceHailNext(
///     PwRpcClient* client,
///     uint32_t call_id,
///     some_namespace_HailRequest* request);
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed except the type of the request,
/// which is taken from the macro's request_type argument.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @param[in] request The method request data
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusNotFound No call which is associated to the call id.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_STREAMING_NEXT_RPC_METHOD(service_name, method_name, \
                                          request_type)              \
  __attribute__((weak)) PwStatus service_name##method_name##Next(    \
      PwRpcClient* client, uint32_t call_id, const request_type* request)

///
/// Generate a default implementation for a streaming next method.
///
#define DEFINE_DEFAULT_STREAMING_NEXT_RPC_METHOD(service_name, method_name,  \
                                                 request_type)               \
  inline DECLARE_STREAMING_NEXT_RPC_METHOD(service_name, method_name,        \
                                           request_type) {                   \
    if (!client || !request) {                                               \
      return kPwStatusInvalidArgument;                                       \
    }                                                                        \
    return PwRpcClientInvokeStreamingNextPb(client, call_id,                 \
                                            request_type##_fields, request); \
  }

///
/// @brief Generates a function declaration for a streaming RPC complete method.
///
/// Assume that there is a EchoService.Hail RPC method.
/// DECLARE_STREAMING_RPC_COMPLETE_METHOD(EchoService, Hail, some_namespace_HailRequest);
/// The generated function looks like:
///
///   PwStatus EchoServiceHailComplete(
///     PwRpcClient* client,
///     uint32_t call_id);
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusNotFound No call which is associated to the call id.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_STREAMING_COMPLETE_RPC_METHOD(service_name, method_name) \
  __attribute__((weak)) PwStatus service_name##method_name##Complete(    \
      PwRpcClient* client, uint32_t call_id)

///
/// @brief Generates a function declaration for a streaming RPC cancel method.
///
/// Assume that there is a EchoService.Hail RPC method.
/// DECLARE_STREAMING_RPC_COMPLETE_METHOD(EchoService, Hail, some_namespace_HailRequest);
/// The generated function looks like:
///
///   PwStatus EchoServiceHailCancel(
///     PwRpcClient* client,
///     uint32_t call_id);
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusNotFound No call which is associated to the call id.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_STREAMING_CANCEL_RPC_METHOD(service_name, method_name) \
  __attribute__((weak)) PwStatus service_name##method_name##Cancel(    \
      PwRpcClient* client, uint32_t call_id)

///
/// Generate a default implementation for a streaming complete method.
///
#define DEFINE_DEFAULT_STREAMING_COMPLETE_RPC_METHOD(service_name,          \
                                                     method_name)           \
  inline DECLARE_STREAMING_COMPLETE_RPC_METHOD(service_name, method_name) { \
    return PwRpcClientInvokeStreamingComplete(client, call_id);             \
  }

///
/// Generate a default implementation for a streaming cancellation method.
///
#define DEFINE_DEFAULT_STREAMING_CANCEL_RPC_METHOD(service_name, method_name) \
  inline DECLARE_STREAMING_CANCEL_RPC_METHOD(service_name, method_name) {     \
    return PwRpcClientInvokeCancel(client, call_id);                          \
  }

///
/// @brief Generates a function declaration for a response deserialization method.
///
/// Assume that there is a EchoService.Echo RPC method.
/// DECLARE_DESERIALIZE_RESPONSE_METHOD(EchoService, Echo, some_namespace_EchoResponse);
/// The generated function looks like:
///
///   PwStatus EchoServiceEchoDeserializeResponse(
///     const uint8_t* bytes, size_t len,
///     some_namespace_EchoResponse* response);
///
/// The function name consists of the method name appended to the service name.
/// All of the function's parameters are fixed except the type of the response,
/// which is taken from the macro's response_type argument.
///
/// @param[in] bytes The packet raw bytes.
/// @param[in] len The data length.
/// @param[out] response The response holder pointer.
/// @return The deserialization result.
/// @retval kPwStatusInvalidArgument The client, or bytes are empty.
/// @retval kPwStatusOk Success.
/// @retval kPwStatusAborted Cannot decode the packet bytes.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
#define DECLARE_DESERIALIZE_RESPONSE_METHOD(service_name, method_name, \
                                            response_type)             \
  __attribute__((weak)) PwStatus                                       \
      service_name##method_name##DeserializeResponse(                  \
          const uint8_t* bytes, size_t len, response_type* response)

///
/// Generate a default implementation for a response deserialization method.
///
#define DEFINE_DEFAULT_DESERIALIZE_RESPONSE_METHOD(service_name, method_name, \
                                                   response_type)             \
  inline DECLARE_DESERIALIZE_RESPONSE_METHOD(service_name, method_name,       \
                                             response_type) {                 \
    if (!response) {                                                          \
      return kPwStatusInvalidArgument;                                        \
    }                                                                         \
    memset(response, 0, sizeof(response_type));                               \
    if (!bytes || !len) {                                                     \
      return kPwStatusOk;                                                     \
    }                                                                         \
    return PwRpcClientDeserializeResponse(bytes, len, response_type##_fields, \
                                          response);                          \
  }

///
/// @brief Generate a series of APIs for an unary RPC method.
///
#define PW_RPC_UNARY_METHOD(service_name, service_id, method_name, method_id, \
                            request_type, response_type)                      \
  DEFINE_DEFAULT_UNARY_RPC_METHOD(service_name, service_id, method_name,      \
                                  method_id, request_type);                   \
  DEFINE_DEFAULT_DESERIALIZE_RESPONSE_METHOD(service_name, method_name,       \
                                             response_type);

///
/// @brief Generate a series of APIs for a server-streaming RPC method.
///
#define PW_RPC_SERVER_STREAMING_METHOD(service_name, service_id, method_name,  \
                                       method_id, request_type, response_type) \
  DEFINE_DEFAULT_SERVER_STREAMING_RPC_METHOD(                                  \
      service_name, service_id, method_name, method_id, request_type);         \
  DEFINE_DEFAULT_STREAMING_CANCEL_RPC_METHOD(service_name, method_name);       \
  DEFINE_DEFAULT_DESERIALIZE_RESPONSE_METHOD(service_name, method_name,        \
                                             response_type);

///
/// @brief Generate a series of APIs for a client-streaming RPC method.
///
#define PW_RPC_CLIENT_STREAMING_METHOD(service_name, service_id, method_name,  \
                                       method_id, request_type, response_type) \
  DEFINE_DEFAULT_CLIENT_STREAMING_RPC_METHOD(service_name, service_id,         \
                                             method_name, method_id);          \
  DEFINE_DEFAULT_STREAMING_NEXT_RPC_METHOD(service_name, method_name,          \
                                           request_type);                      \
  DEFINE_DEFAULT_STREAMING_COMPLETE_RPC_METHOD(service_name, method_name);     \
  DEFINE_DEFAULT_DESERIALIZE_RESPONSE_METHOD(service_name, method_name,        \
                                             response_type);

///
/// @brief Generate a series of APIs for a bidirectional-streaming RPC method.
///
#define PW_RPC_BIDIRECTIONAL_STREAMING_METHOD(service_name, service_id,       \
                                              method_name, method_id,         \
                                              request_type, response_type)    \
  DEFINE_DEFAULT_BIDIRECTIONAL_STREAMING_RPC_METHOD(service_name, service_id, \
                                                    method_name, method_id);  \
  DEFINE_DEFAULT_STREAMING_NEXT_RPC_METHOD(service_name, method_name,         \
                                           request_type);                     \
  DEFINE_DEFAULT_STREAMING_COMPLETE_RPC_METHOD(service_name, method_name);    \
  DEFINE_DEFAULT_DESERIALIZE_RESPONSE_METHOD(service_name, method_name,       \
                                             response_type);

#ifdef __cplusplus
}
#endif
#endif /* PW_RPC_SERVICE_CLIENT_H */
