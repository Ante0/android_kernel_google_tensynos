/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM goog_mba_cpm_iface

#if !defined(_GOOG_MBA_CPM_IFACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GOOG_MBA_CPM_IFACE_H

#include <linux/tracepoint.h>
#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/goog_mba_q_xport.h>

#include "goog-mba-cpm-iface.h"

#define MAX_DEV_NAME 32

#ifdef CREATE_TRACE_POINTS
static inline void cpm_iface_trace_clone_payload(u32 *header, u32 *payloads,
						 struct cpm_iface_payload *payload)
{
	*header = payload->header;
	memcpy(payloads, payload->payload, GOOG_MBA_PAYLOAD_SIZE * sizeof(*payloads));
}
#endif

TRACE_EVENT(
	cpm_iface_request_client,

	TP_PROTO(struct cpm_iface_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(int, src_id)
		__field(void *, host_tx_cb)
		__array(char, dev_name, MAX_DEV_NAME)
	),

	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_DEV_NAME, "%s", dev_name(client->dev));
		__entry->src_id = client->src_id;
		__entry->host_tx_cb = (void *)client->host_tx_cb;
	),

	TP_printk("dev=%s src_id=%d host_tx_cb=%ps", __entry->dev_name, __entry->src_id,
		  __entry->host_tx_cb)
);

TRACE_EVENT(
	cpm_iface_free_client,

	TP_PROTO(struct cpm_iface_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(int, src_id)
		__array(char, dev_name, MAX_DEV_NAME)
	),

	TP_fast_assign(
		__entry->src_id = client->src_id;
		scnprintf(__entry->dev_name, MAX_DEV_NAME, "%s", dev_name(client->dev));
	),

	TP_printk("dev=%s src_id=%d", __entry->dev_name, __entry->src_id)
);

TRACE_EVENT(
	cpm_iface_req_enqueue,

	TP_PROTO(int session_idx, struct cpm_iface_session *session, int q_wr_idx, int q_size),

	TP_ARGS(session_idx, session, q_wr_idx, q_size),

	TP_STRUCT__entry(
		__field(int, session_idx)
		__field(unsigned long, tout_ms)
		__field(u32, resp_context)
		__field(struct device *, dev)
		__field(int, q_wr_idx)
		__field(int, q_size)
	),

	TP_fast_assign(
		struct cpm_iface_req *req = &session->req;

		__entry->tout_ms = req->tout_ms;
		__entry->resp_context = req->resp_context;
		__entry->session_idx = session_idx;
		__entry->q_wr_idx = q_wr_idx;
		__entry->q_size = q_size;
	),

	TP_printk("session_idx=%d q_wr_idx=%d q_size=%d tout_ms=%lu resp_ctx=%#x",
		  __entry->session_idx, __entry->q_wr_idx, __entry->q_size,
		  __entry->tout_ms, __entry->resp_context)
);

TRACE_EVENT(
	cpm_iface_submit_msg,

	TP_PROTO(int session_idx, int channel_idx, struct cpm_iface_req *req),

	TP_ARGS(session_idx, channel_idx, req),

	TP_STRUCT__entry(
		__field(int, session_idx)
		__field(int, channel_idx)
		__field(int, token)
		__field(int, type)
		__field(u32, header)
		__array(u32, payloads, GOOG_MBA_PAYLOAD_SIZE)
	),

	TP_fast_assign(
		struct cpm_iface_payload *payload = req->req_msg;

		__entry->session_idx = session_idx;
		__entry->channel_idx = channel_idx;
		__entry->token = goog_mba_q_xport_get_token(&payload->header);
		__entry->type = goog_mba_q_xport_get_type(&payload->header);
		cpm_iface_trace_clone_payload(&__entry->header, __entry->payloads, payload);
	),

	TP_printk("session_idx=%d chn#%d token#%d msg_type=%d: %#010x %#010x %#010x %#010x",
		  __entry->session_idx, __entry->channel_idx,
		  __entry->token, __entry->type,
		  __entry->header, __entry->payloads[0], __entry->payloads[1], __entry->payloads[2])
);

TRACE_EVENT(
	cpm_iface_req_tx_done,

	TP_PROTO(int session_idx, int channel_idx),

	TP_ARGS(session_idx, channel_idx),

	TP_STRUCT__entry(
		__field(int, session_idx)
		__field(int, channel_idx)
	),

	TP_fast_assign(
		__entry->session_idx = session_idx;
		__entry->channel_idx = channel_idx;
	),

	TP_printk("session_idx=%d chn#%d", __entry->session_idx, __entry->channel_idx)
);

TRACE_EVENT(
	cpm_iface_resp_cb,

	TP_PROTO(int session_idx, struct cpm_iface_payload *payload),

	TP_ARGS(session_idx, payload),

	TP_STRUCT__entry(
		__field(int, session_idx)
		__field(int, token)
		__field(u32, header)
		__array(u32, payloads, GOOG_MBA_PAYLOAD_SIZE)
	),

	TP_fast_assign(
		__entry->session_idx = session_idx;
		__entry->token = goog_mba_q_xport_get_token(&payload->header);
		cpm_iface_trace_clone_payload(&__entry->header, __entry->payloads, payload);
	),

	TP_printk("session_idx=%d token#%d: %#010x %#010x %#010x %#010x",
		  __entry->session_idx, __entry->token, __entry->header,
		  __entry->payloads[0], __entry->payloads[1], __entry->payloads[2])
);

TRACE_EVENT(
	cpm_send_message,

	TP_PROTO(int session_idx, int ret),

	TP_ARGS(session_idx, ret),

	TP_STRUCT__entry(
		__field(int, session_idx)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->session_idx = session_idx;
		__entry->ret = ret;
	),

	TP_printk("session_idx=%d ret=%d",
		  __entry->session_idx, __entry->ret)
);

TRACE_EVENT(
	cpm_iface_handle_host_tx,

	TP_PROTO(int dst_id, u32 resp_context, struct cpm_iface_payload *payload),

	TP_ARGS(dst_id, resp_context, payload),

	TP_STRUCT__entry(
		__field(int, dst_id)
		__field(u32, resp_context)
		__field(u32, header)
		__array(u32, payloads, GOOG_MBA_PAYLOAD_SIZE)
	),

	TP_fast_assign(
		__entry->dst_id = dst_id;
		__entry->resp_context = resp_context;
		cpm_iface_trace_clone_payload(&__entry->header, __entry->payloads, payload);
	),

	TP_printk("dst_id=%d resp_context=%#x: %#010x %#010x %#010x %#010x",
		  __entry->dst_id, __entry->resp_context,
		  __entry->header, __entry->payloads[0],
		  __entry->payloads[1], __entry->payloads[2])
);

#endif /* _GOOG_MBA_CPM_IFACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/soc/google/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE goog-mba-cpm-iface-trace
#include <trace/define_trace.h>
