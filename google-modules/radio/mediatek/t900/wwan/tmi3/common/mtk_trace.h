/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mtk_trace

#if !defined(__MTK_TRACE_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MTK_TRACE_EVENTS_H

#include <linux/device.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>
#include <linux/trace_events.h>

#define MTK_MSG_MAX 110

DECLARE_EVENT_CLASS(mtk_log_event,

	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__dynamic_array(char, msg, MTK_MSG_MAX)
	),

	TP_fast_assign(
		__assign_str(device);
		WARN_ON_ONCE(vsnprintf(__get_dynamic_array(msg),
				       MTK_MSG_MAX,
				       vaf->fmt,
				       *vaf->va) >= MTK_MSG_MAX);
	),

	TP_printk("%s %s", __get_str(device), __get_str(msg))
);

DEFINE_EVENT(mtk_log_event, mtk_debug,

	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

DEFINE_EVENT(mtk_log_event, mtk_info,

	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

DEFINE_EVENT(mtk_log_event, mtk_warn,

	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

DEFINE_EVENT(mtk_log_event, mtk_err,

	TP_PROTO(struct device *dev, struct va_format *vaf),

	TP_ARGS(dev, vaf)
);

TRACE_EVENT(mtk_debug_dump,

	TP_PROTO(struct device *dev, const char *msg,
		 const void *buf, size_t buf_len),

	TP_ARGS(dev, msg, buf, buf_len),

	TP_STRUCT__entry(
		__string(device, dev_name(dev))
		__string(msg, msg)
		__field(size_t, buf_len)
		__dynamic_array(u8, buf, buf_len)
	),

	TP_fast_assign(
		__assign_str(device);
		__assign_str(msg);
		__entry->buf_len = buf_len;
		memcpy(__get_dynamic_array(buf), buf, buf_len);
	),

	TP_printk("%s %s len=%zu, contents=[%s]\n",
		  __get_str(device),
		  __get_str(msg),
		  __entry->buf_len,
		  __print_hex(__get_dynamic_array(buf),
			      __entry->buf_len)
	)
);

TRACE_EVENT(mtk_wwan_data_tx,

	TP_PROTO(int q_id, void *str, u32 id),

	TP_ARGS(q_id, str, id),

	TP_STRUCT__entry(
		__field(int, q_id)
		__string(str, str)
		__field(u32, id)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__assign_str(str);
		__entry->id = id;
	),

	TP_printk("%d, %s, %u", __entry->q_id, __get_str(str), __entry->id)
);

TRACE_EVENT(mtk_wwan_data_rx,

	TP_PROTO(int q_id, void *str, u32 id),

	TP_ARGS(q_id, str, id),

	TP_STRUCT__entry(
		__field(int, q_id)
		__string(str, str)
		__field(u32, id)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__assign_str(str);
		__entry->id = id;
	),

	TP_printk("%d, %s, %u", __entry->q_id, __get_str(str), __entry->id)
);

TRACE_EVENT(mtk_ctrl_write,

	TP_PROTO(int tx_ch, void *skb_ptr, void *skb_data, int len),

	TP_ARGS(tx_ch, skb_ptr, skb_data, len),

	TP_STRUCT__entry(
		__field(int, tx_ch)
		__field(void *, skb_ptr)
		__field(void *, skb_data)
		__field(int, len)
	),

	TP_fast_assign(
		__entry->tx_ch = tx_ch;
		__entry->skb_ptr = skb_ptr;
		__entry->skb_data = skb_data;
		__entry->len = len;
	),

	TP_printk("%d, %p, %p, %d",
		  __entry->tx_ch, __entry->skb_ptr,
		  __entry->skb_data, __entry->len)
);

TRACE_EVENT(mtk_ctrl_write_done,

	TP_PROTO(int tx_ch, void *skb_ptr, void *skb_data, int ret),

	TP_ARGS(tx_ch, skb_ptr, skb_data, ret),

	TP_STRUCT__entry(
		__field(int, tx_ch)
		__field(void *, skb_ptr)
		__field(void *, skb_data)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->tx_ch = tx_ch;
		__entry->skb_ptr = skb_ptr;
		__entry->skb_data = skb_data;
		__entry->ret = ret;
	),

	TP_printk("%d, %p, %p, %d",
		  __entry->tx_ch, __entry->skb_ptr,
		  __entry->skb_data, __entry->ret)
);

TRACE_EVENT(mtk_ctrl_dispatch,

	TP_PROTO(int rx_ch, void *skb_ptr, void *skb_data),

	TP_ARGS(rx_ch, skb_ptr, skb_data),

	TP_STRUCT__entry(
		__field(int, rx_ch)
		__field(void *, skb_ptr)
		__field(void *, skb_data)
	),

	TP_fast_assign(
		__entry->rx_ch = rx_ch;
		__entry->skb_ptr = skb_ptr;
		__entry->skb_data = skb_data
	),

	TP_printk("%d, %p, %p",
		  __entry->rx_ch, __entry->skb_ptr,
		  __entry->skb_data)
);

TRACE_EVENT(mtk_ctrl_dispatch_err,

	TP_PROTO(int rx_ch, void *skb_ptr, int ret),

	TP_ARGS(rx_ch, skb_ptr, ret),

	TP_STRUCT__entry(
		__field(int, rx_ch)
		__field(void *, skb_ptr)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->rx_ch = rx_ch;
		__entry->skb_ptr = skb_ptr;
		__entry->ret = ret;
	),

	TP_printk("%d, %p, %d",
		  __entry->rx_ch, __entry->skb_ptr,
		  __entry->ret)
);

TRACE_EVENT(mtk_ctrl_read,

	TP_PROTO(int rx_ch, void *skb_ptr, void *skb_data, int skb_len, int read_len),

	TP_ARGS(rx_ch, skb_ptr, skb_data, skb_len, read_len),

	TP_STRUCT__entry(
		__field(int, rx_ch)
		__field(void *, skb_ptr)
		__field(void *, skb_data)
		__field(int, skb_len)
		__field(int, read_len)
	),

	TP_fast_assign(
		__entry->rx_ch = rx_ch;
		__entry->skb_ptr = skb_ptr;
		__entry->skb_data = skb_data;
		__entry->skb_len = skb_len;
		__entry->read_len = read_len;
	),

	TP_printk("%d, %p, %p, %d, %d",
		  __entry->rx_ch, __entry->skb_ptr,
		  __entry->skb_data, __entry->skb_len,
		  __entry->read_len)
);

#endif /* __MTK_TRACE_EVENTS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE mtk_trace
#include <trace/define_trace.h>

