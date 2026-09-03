/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM goog_mba_gdmc_iface

#if !defined(_GOOG_MBA_GDMC_IFACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GOOG_MBA_GDMC_IFACE_H

#include <linux/tracepoint.h>

#include <soc/google/goog-mba-aggr.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "goog-mba-gdmc-iface.h"

TRACE_EVENT(
	gdmc_register_host_cb,

	TP_PROTO(u32 service_id),

	TP_ARGS(service_id),

	TP_STRUCT__entry(
		__field(u32, service_id)
	),

	TP_fast_assign(
		__entry->service_id = service_id;
	),

	TP_printk("service_id=%u", __entry->service_id)
);

TRACE_EVENT(
	gdmc_unregister_host_cb,

	TP_PROTO(u32 service_id),

	TP_ARGS(service_id),

	TP_STRUCT__entry(
		__field(u32, service_id)
	),

	TP_fast_assign(
		__entry->service_id = service_id;
	),

	TP_printk("service_id=%u", __entry->service_id)
);

TRACE_EVENT(
	goog_mba_gdmc_iface_send_message,

	TP_PROTO(bool is_crit, struct goog_mba_aggr_request *client_req),

	TP_ARGS(is_crit, client_req),

	TP_STRUCT__entry(
		__field(bool, is_crit)
		__field(bool, is_async)
		__field(bool, is_oneway)
		__field(u32, service_id)
		__field(unsigned long, tout_ms)
		__array(u32, req_buf, GDMC_MBA_DHUB_PAYLOAD_SIZE)
	),

	TP_fast_assign(
		u32 *client_req_buf = client_req->req_buf;

		__entry->is_crit = is_crit;
		__entry->is_async = client_req->async;
		__entry->is_oneway = client_req->oneway;
		__entry->service_id = goog_mba_nq_xport_get_service_id(client_req->req_buf);
		__entry->tout_ms = client_req->tout_ms;
		memcpy(__entry->req_buf, client_req_buf,
		       GDMC_MBA_DHUB_PAYLOAD_SIZE * sizeof(*client_req_buf));
	),

	TP_printk("%#010x %#010x %#010x %#010x service_id=%u is_crit=%d is_async=%d is_oneway=%d tout_ms=%lu",
		  __entry->req_buf[0], __entry->req_buf[1],
		  __entry->req_buf[2], __entry->req_buf[3],
		  __entry->service_id, __entry->is_crit,
		  __entry->is_async, __entry->is_oneway,
		  __entry->tout_ms)
);

TRACE_EVENT(
	goog_mba_gdmc_host_tx_cb_handler,

	TP_PROTO(u32 *resp_buf, bool has_handler),

	TP_ARGS(resp_buf, has_handler),

	TP_STRUCT__entry(
		__field(bool, has_handler)
		__array(u32, resp_buf, GDMC_MBA_DHUB_PAYLOAD_SIZE)
	),

	TP_fast_assign(
		__entry->has_handler = has_handler;
		memcpy(__entry->resp_buf, resp_buf,
		       GDMC_MBA_DHUB_PAYLOAD_SIZE * sizeof(*resp_buf));
	),

	TP_printk("%010x %010x %010x %010x service_id=%u has_handler=%d",
		  __entry->resp_buf[0], __entry->resp_buf[1],
		  __entry->resp_buf[2], __entry->resp_buf[3],
		  goog_mba_nq_xport_get_service_id(&__entry->resp_buf[0]),
		  __entry->has_handler)
);

#endif /* _GOOG_MBA_GDMC_IFACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/soc/google
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE goog-mba-gdmc-iface-trace
#include <trace/define_trace.h>
