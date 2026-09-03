/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM goog_mba_ctrl

#if !defined(_GOOG_MBA_CTRL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _GOOG_MBA_CTRL_H

#include <linux/tracepoint.h>

#include "goog-mba-ctrl.h"

TRACE_EVENT(
	goog_mba_ctrl_handle_doorbell_isr,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info, u32 irq_status),

	TP_ARGS(mbox_info, irq_status),

	TP_STRUCT__entry(
		__array(char, dev_name, MAX_MBOX_CTRL_NAME)
		__field(u32, irq_status)
	),

	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_MBOX_CTRL_NAME, "%s", dev_name(mbox_info->dev));
		__entry->irq_status = irq_status;
	),

	TP_printk("%s irq_status=0x%x", __entry->dev_name, __entry->irq_status)
);

TRACE_EVENT(
	goog_mba_ctrl_process_nq_txdone,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info),

	TP_ARGS(mbox_info),

	TP_STRUCT__entry(
		__array(char, dev_name, MAX_MBOX_CTRL_NAME)
	),

	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_MBOX_CTRL_NAME, "%s", dev_name(mbox_info->dev));
	),

	TP_printk("%s", __entry->dev_name)
);

TRACE_EVENT(
	goog_mba_ctrl_process_q_txdone,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info, u32 reqs_completed, u32 outstanding_msgs),

	TP_ARGS(mbox_info, reqs_completed, outstanding_msgs),

	TP_STRUCT__entry(
		__field(u32, tx_q_rd_ptr)
		__field(u32, outstanding_msgs)
		__field(u32, reqs_completed)
	),

	TP_fast_assign(
		__entry->tx_q_rd_ptr = mbox_info->tx_q_rd_ptr;
		__entry->outstanding_msgs = outstanding_msgs;
		__entry->reqs_completed = reqs_completed;
	),

	TP_printk("tx_q_rd_ptr=%u reqs_completed=%u outstanding_msgs=%u",
		  __entry->tx_q_rd_ptr,
		  __entry->reqs_completed, __entry->outstanding_msgs)
);

TRACE_EVENT(
	goog_mba_ctrl_send_data_nq,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info, u32 *payload,
		 unsigned int payload_words),

	TP_ARGS(mbox_info, payload, payload_words),

	TP_STRUCT__entry(
		__array(char, dev_name, MAX_MBOX_CTRL_NAME)
		__field(u32, payload_words)
		__dynamic_array(u32, payload, payload_words)
	),

	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_MBOX_CTRL_NAME, "%s", dev_name(mbox_info->dev));
		__entry->payload_words = payload_words;
		memcpy(__get_dynamic_array(payload), payload,
		       payload_words * sizeof(u32));
	),

	TP_printk("%s: %s", __entry->dev_name,
		  __print_array(__get_dynamic_array(payload),
				__entry->payload_words, sizeof(u32)))
);

TRACE_EVENT(
	goog_mba_ctrl_send_data_q,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info, u32 *payload,
		 unsigned int payload_words),

	TP_ARGS(mbox_info, payload, payload_words),

	TP_STRUCT__entry(
		__field(u32, tx_q_wr_ptr)
		__field(u32, payload_words)
		__dynamic_array(u32, payload, payload_words)
	),

	TP_fast_assign(
		__entry->tx_q_wr_ptr = mbox_info->tx_q_wr_ptr;
		__entry->payload_words = payload_words;
		memcpy(__get_dynamic_array(payload), payload,
		       payload_words * sizeof(u32));
	),

	TP_printk("%s tx_q_wr_ptr=%u",
		  __print_array(__get_dynamic_array(payload),
				__entry->payload_words, sizeof(u32)),
		  __entry->tx_q_wr_ptr)
);

TRACE_EVENT(
	goog_mba_ctrl_process_nq_rx,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info, u32 *payload),

	TP_ARGS(mbox_info, payload),

	TP_STRUCT__entry(
		__field(u32, rx_q_rd_ptr)
		__field(u32, payload_words)
		__dynamic_array(u32, payload, mbox_info->payload_size)
		__array(char, dev_name, MAX_MBOX_CTRL_NAME)
	),

	TP_fast_assign(
		__entry->rx_q_rd_ptr = mbox_info->rx_q_rd_ptr;
		__entry->payload_words = mbox_info->payload_size;
		memcpy(__get_dynamic_array(payload), payload,
		       mbox_info->payload_size * sizeof(u32));
		scnprintf(__entry->dev_name, MAX_MBOX_CTRL_NAME, "%s", dev_name(mbox_info->dev));
	),

	TP_printk("%s: %s", __entry->dev_name,
		  __print_array(__get_dynamic_array(payload),
				__entry->payload_words, sizeof(u32)))
);

TRACE_EVENT(
	goog_mba_ctrl_process_q_rx,

	TP_PROTO(struct goog_mba_ctrl_info *mbox_info, u32 *payload),

	TP_ARGS(mbox_info, payload),

	TP_STRUCT__entry(
		__field(u32, rx_q_rd_ptr)
		__field(u32, payload_words)
		__dynamic_array(u32, payload, mbox_info->payload_size)
	),

	TP_fast_assign(
		__entry->rx_q_rd_ptr = mbox_info->rx_q_rd_ptr;
		__entry->payload_words = mbox_info->payload_size;
		memcpy(__get_dynamic_array(payload), payload,
		       mbox_info->payload_size * sizeof(u32));
	),

	TP_printk("%s rx_q_rd_ptr=%u",
		  __print_array(__get_dynamic_array(payload),
				__entry->payload_words, sizeof(u32)),
		  __entry->rx_q_rd_ptr)
);

#endif /* _GOOG_MBA_CTRL_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/mailbox
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE goog-mba-ctrl-trace
#include <trace/define_trace.h>
