#ifndef RING_EVENT_SERVICE_CLIENT_NANOPB_H
#define RING_EVENT_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "ring_event_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RING_EVENT_SERVICE_ID (0xfb1ebe1eU)
#define RING_EVENT_SERVICE_ACTION_METHOD_ID (0xc5bb429cU)

/*
 * rpc Action(Request) returns (stream Response) {}
 */
PW_RPC_SERVER_STREAMING_METHOD(RingEventService, RING_EVENT_SERVICE_ID, Action,
                               RING_EVENT_SERVICE_ACTION_METHOD_ID,
                               noa_service_ring_event_service_Request,
                               noa_service_ring_event_service_Response);

#ifdef __cplusplus
}
#endif
#endif /* RING_EVENT_SERVICE_CLIENT_NANOPB_H */
