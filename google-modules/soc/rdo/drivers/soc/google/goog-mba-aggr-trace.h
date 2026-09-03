/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM goog_mba_aggr

#if !defined(_GOOG_MBA_AGGR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GOOG_MBA_AGGR_H

#include <linux/tracepoint.h>

#include <soc/google/goog-mba-aggr.h>

/*
 * TODO(b/354067254): use macro instead __dynamic_array to support trace-cmd.
 * Fix this if flexible payload size is required.
 */
#define MAX_TRACE_PAYLOAD_WORDS 4

TRACE_EVENT(
	goog_mba_aggr_queue_req,

	TP_PROTO(struct goog_mba_aggr_service *gservice, struct goog_mba_aggr_session *session),

	TP_ARGS(gservice, session),

	TP_STRUCT__entry(
		__field(int, req_queue_head)
		__field(int, req_queue_tail)
		__field(int, req_queue_size)
		__field(int, session_idx)
	),

	TP_fast_assign(
		__entry->req_queue_head = gservice->req_queue_head;
		__entry->req_queue_tail = gservice->req_queue_tail;
		__entry->req_queue_size = gservice->req_queue_size;
		__entry->session_idx = goog_mba_aggr_get_session_idx(gservice, session);
	),

	TP_printk("head=%d tail=%d size=%d session_idx=%d",
		  __entry->req_queue_head, __entry->req_queue_tail, __entry->req_queue_size,
		  __entry->session_idx)
);

TRACE_EVENT(
	goog_mba_aggr_submit,

	TP_PROTO(struct goog_mba_aggr_mbox_chan_info *mbox_chan_info,
		 struct goog_mba_aggr_service *gservice),

	TP_ARGS(mbox_chan_info, gservice),

	TP_STRUCT__entry(
		__field(int, index)
		__field(int, req_queue_head)
		__field(int, req_queue_tail)
		__field(int, req_queue_size)
		__array(u32, req_buf, MAX_TRACE_PAYLOAD_WORDS)
	),

	TP_fast_assign(
		__entry->index = goog_mba_aggr_index_mbox_chan_info(mbox_chan_info);
		__entry->req_queue_head = gservice->req_queue_head;
		__entry->req_queue_tail = gservice->req_queue_tail;
		__entry->req_queue_size = gservice->req_queue_size;
		memcpy(__entry->req_buf, mbox_chan_info->req->req_buf,
		       MAX_TRACE_PAYLOAD_WORDS * sizeof(*__entry->req_buf));
	),

	TP_printk("client_tx_chan[%d]: %#010x %#010x %#010x %#010x head=%d tail=%d size=%d",
		  __entry->index,
		  __entry->req_buf[0], __entry->req_buf[1],
		  __entry->req_buf[2], __entry->req_buf[3],
		  __entry->req_queue_head, __entry->req_queue_tail, __entry->req_queue_size)
);

TRACE_EVENT(
	goog_mba_aggr_send_message,

	TP_PROTO(int ret),

	TP_ARGS((ret)),

	TP_STRUCT__entry(
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->ret = ret;
	),

	TP_printk("ret=%d", __entry->ret)
);

TRACE_EVENT(
	goog_mba_aggr_client_tx_done,

	TP_PROTO(struct goog_mba_aggr_mbox_chan_info *mbox_chan_info),

	TP_ARGS(mbox_chan_info),

	TP_STRUCT__entry(
		__field(int, index)
		__field(int, oneway)
	),

	TP_fast_assign(
		__entry->index = goog_mba_aggr_index_mbox_chan_info(mbox_chan_info);
		__entry->oneway = mbox_chan_info->req->oneway;
	),

	TP_printk("client_tx_chan[%d]: oneway=%d",
		  __entry->index, __entry->oneway)
);

TRACE_EVENT(
	goog_mba_aggr_client_rx_cb,

	TP_PROTO(struct goog_mba_aggr_mbox_chan_info *mbox_chan_info, u32 *resp_buf),

	TP_ARGS(mbox_chan_info, resp_buf),

	TP_STRUCT__entry(
		__field(bool, async)
		__field(int, index)
		__array(u32, resp_buf, MAX_TRACE_PAYLOAD_WORDS)
	),

	TP_fast_assign(
		struct goog_mba_aggr_request *req = mbox_chan_info->req;

		__entry->async = req->async;
		__entry->index = goog_mba_aggr_index_mbox_chan_info(mbox_chan_info);
		memcpy(__entry->resp_buf, resp_buf,
		       MAX_TRACE_PAYLOAD_WORDS * sizeof(*resp_buf));
	),

	TP_printk("client_tx_chan[%d]: %#010x %#010x %#010x %#010x async=%d",
		  __entry->index,
		  __entry->resp_buf[0], __entry->resp_buf[1],
		  __entry->resp_buf[2], __entry->resp_buf[3],
		  __entry->async)
);

TRACE_EVENT(
	goog_mba_aggr_host_tx_cb,

	TP_PROTO(struct goog_mba_aggr_mbox_chan_info *mbox_chan_info, u32 *resp_buf,
		 bool has_handler),

	TP_ARGS(mbox_chan_info, resp_buf, has_handler),

	TP_STRUCT__entry(
		__field(int, index)
		__field(bool, has_handler)
		__array(u32, resp_buf, MAX_TRACE_PAYLOAD_WORDS)
	),

	TP_fast_assign(
		__entry->index = goog_mba_aggr_index_mbox_chan_info(mbox_chan_info);
		__entry->has_handler = has_handler;
		memcpy(__entry->resp_buf, resp_buf,
		       MAX_TRACE_PAYLOAD_WORDS * sizeof(*resp_buf));
	),

	TP_printk("host_tx_chan[%d]: %#010x %#010x %#010x %#010x has_handler=%d",
		  __entry->index,
		  __entry->resp_buf[0], __entry->resp_buf[1],
		  __entry->resp_buf[2], __entry->resp_buf[3],
		  __entry->has_handler)
);

TRACE_EVENT(
	goog_mba_aggr_host_rx_done,

	TP_PROTO(int index),

	TP_ARGS(index),

	TP_STRUCT__entry(
		__field(int, index)
	),

	TP_fast_assign(
		__entry->index = index;
	),

	TP_printk("host_tx_chan[%d]", __entry->index)
);

#endif /* _GOOG_MBA_AGGR_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/soc/google
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE goog-mba-aggr-trace
#include <trace/define_trace.h>
