/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace module for NOA
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM noa

#if !defined(__NOA_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __NOA_TRACE_H__

#include <linux/tracepoint.h>
#include "core.h"
#include <nep/nep.h>
#include <common/map_def.h>
#include "ring.h"

DECLARE_EVENT_CLASS(ring_desc_class,
	TP_PROTO(struct noa_desc *desc),
	TP_ARGS(desc),
	TP_STRUCT__entry(
		__field(int, src)
		__field(int, dst)
		__field(u64, dp)
		__field(u64, dv)
		__field(u32, head_offset)
		__field(u16, tkid)
		__field(int, copy)
		__field(int, desc_type)
		__field(bool, feedback)
		__field(bool, rewrite)
		__field(bool, ddone)
		__field(int, mode)
		__field(int, reason)
	),
	TP_fast_assign(
		__entry->src = desc->src;
		__entry->dst = desc->dst;
		__entry->dp = ((u64)desc->dp_high << 32ULL) | (u64)desc->dp_low;
		__entry->dv = desc->dv;
		__entry->head_offset = desc->head_offset;
		__entry->tkid = desc->tkid;
		__entry->copy = desc->cp;
		__entry->desc_type = desc->desc_type;
		__entry->feedback = desc->fk;
		__entry->ddone = desc->ddone;
		__entry->mode = desc->mode;
		__entry->reason = desc->reason;
	),
	TP_printk("src: %s, dst: %s, "
		  "pa: 0x%llx, va: 0x%llx, head offset: %u, "
		  "tkid: %u, copy: %d, type: %d"
		  "feedback: %d, ddone: %d, mode: %s, reason: %s",
		  noa_port_id_to_name(__entry->src),
		  noa_port_id_to_name(__entry->dst),
		  __entry->dp, __entry->dv, __entry->head_offset,
		  __entry->tkid, __entry->copy, __entry->desc_type,
		  __entry->feedback, __entry->ddone,
		  noa_mode_to_str(__entry->mode), noa_reason_to_str(__entry->reason)
	)
);
#define DEFINE_RING_DESC_EVENT(name)		\
DEFINE_EVENT(ring_desc_class, name,		\
	TP_PROTO(struct noa_desc *desc), 	\
	TP_ARGS(desc))
DEFINE_RING_DESC_EVENT(ring_descriptor_receive);
DEFINE_RING_DESC_EVENT(ring_descriptor_transmit);
DEFINE_RING_DESC_EVENT(ring_descriptor_packet_forwarder);

DECLARE_EVENT_CLASS(ring_dma_class,
	TP_PROTO(int channel, int src, int dst),
	TP_ARGS(channel, src, dst),
	TP_STRUCT__entry(
		__field(int, channel)
		__field(int, src)
		__field(int, dst)
	),
	TP_fast_assign(
		__entry->channel = channel;
		__entry->src = src;
		__entry->dst = dst;
	),
	TP_printk("channel: %d, src: %x, dst: %x",
		  __entry->channel,
		  __entry->src,
		  __entry->dst
	)
);
#define DEFINE_RING_DMA_EVENT(name)		\
DEFINE_EVENT(ring_dma_class, name,		\
	TP_PROTO(int channel, int src, int dst), \
	TP_ARGS(channel, src, dst))
DEFINE_RING_DMA_EVENT(ring_dma_transfer_completed);
DEFINE_RING_DMA_EVENT(ring_dma_transfer_start);

DECLARE_EVENT_CLASS(ring_index_update_class,
	TP_PROTO(int ring, int index),
	TP_ARGS(ring, index),
	TP_STRUCT__entry(
		__field(int, ring)
		__field(int, index)
	),
	TP_fast_assign(
		__entry->ring = ring;
		__entry->index = index;
	),
	TP_printk("[ring %d] index: %d", __entry->ring, __entry->index)
);
#define DEFINE_RING_INDEX_UPDATE_EVENT(name)	\
DEFINE_EVENT(ring_index_update_class, name,	\
	TP_PROTO(int ring, int index), \
	TP_ARGS(ring, index))
DEFINE_RING_INDEX_UPDATE_EVENT(ring_index_update_consumer);
DEFINE_RING_INDEX_UPDATE_EVENT(ring_index_update_producer);

DECLARE_EVENT_CLASS(ring_port_class,
	TP_PROTO(int port),
	TP_ARGS(port),
	TP_STRUCT__entry(
		__field(int, port)
	),
	TP_fast_assign(
		__entry->port = port;
	),
	TP_printk("ring port: %s", noa_port_id_to_name(__entry->port))
);
#define DEFINE_RING_PORT_EVENT(name)	\
DEFINE_EVENT(ring_port_class, name,	\
	TP_PROTO(int port), 		\
	TP_ARGS(port))
DEFINE_RING_PORT_EVENT(ring_port_feedback);
DEFINE_RING_PORT_EVENT(ring_port_reschedule);
DEFINE_RING_PORT_EVENT(ring_port_trigger_interrupt);

// trace in NEP
DECLARE_EVENT_CLASS(tether_4_entry_class,
	TP_PROTO(Tether4Entry *p_v4_entry, bool is_upstream),
	TP_ARGS(p_v4_entry, is_upstream),
	TP_STRUCT__entry(
		__field(u16, key_srcPort)
		__field(u16, key_dstPort)
		__field(u16, value_manglePort)
		__field(bool, is_upstream)
	),
	TP_fast_assign(
		__entry->key_srcPort   = p_v4_entry->key.srcPort;
		__entry->key_dstPort   = p_v4_entry->key.dstPort;
		__entry->value_manglePort = p_v4_entry->value.manglePort;
		__entry->is_upstream   = is_upstream;
	),
	TP_printk("%s port %d/%d -> %d",
		__entry->is_upstream ? "UPSTREAM" : "DOWNSTREAM",
		__entry->key_srcPort,
		__entry->key_dstPort,
		__entry->value_manglePort)
);
#define DEFINE_TETHER_4_ENTRY_EVENT(name)	\
DEFINE_EVENT(tether_4_entry_class, name,	\
	TP_PROTO(Tether4Entry *p_v4_entry, bool is_upstream), 		\
	TP_ARGS(p_v4_entry, is_upstream))
DEFINE_TETHER_4_ENTRY_EVENT(nep_put_tether_4_entry);

DECLARE_EVENT_CLASS(noa_session_class,
	TP_PROTO(struct noa_session *p_session),
	TP_ARGS(p_session),
	TP_STRUCT__entry(
		__field(u8, ver)
		__field(u8, sport)
		__field(u8, dport)
		__field(u8, state)
		__field(u16, eth_type)
		__field(u8, src_mac_0)
		__field(u8, src_mac_1)
		__field(u8, src_mac_2)
		__field(u8, src_mac_3)
		__field(u8, src_mac_4)
		__field(u8, src_mac_5)
		__field(u8, dst_mac_0)
		__field(u8, dst_mac_1)
		__field(u8, dst_mac_2)
		__field(u8, dst_mac_3)
		__field(u8, dst_mac_4)
		__field(u8, dst_mac_5)
		__field(u32, vlan_info)
		__field(u32, sec_info)
		__field(u8, src_ifidx)
		__field(u8, dst_ifidx)
		__field(u16, flow_id)
		__field(u8, clat)
		__field(u16, related_id)
	),
	TP_fast_assign(
		__entry->ver        = p_session->common.ver;
		__entry->state      = p_session->common.state;
		__entry->eth_type   = p_session->common.eth_type;
		__entry->vlan_info  = p_session->common.vlan_info;
		__entry->sec_info   = p_session->common.sec_info;
		__entry->flow_id    = p_session->common.flow_id;
		__entry->clat       = p_session->common.clat;
		__entry->related_id = p_session->common.related_id;
		__entry->sport      = p_session->common.sport;
		__entry->dport      = p_session->common.dport;
		__entry->src_mac_0  = p_session->common.src_mac1[0];
		__entry->src_mac_1  = p_session->common.src_mac1[1];
		__entry->src_mac_2  = p_session->common.src_mac2[0];
		__entry->src_mac_3  = p_session->common.src_mac2[1];
		__entry->src_mac_4  = p_session->common.src_mac2[2];
		__entry->src_mac_5  = p_session->common.src_mac2[3];
		__entry->dst_mac_0  = p_session->common.dst_mac1[0];
		__entry->dst_mac_1  = p_session->common.dst_mac1[1];
		__entry->dst_mac_2  = p_session->common.dst_mac2[0];
		__entry->dst_mac_3  = p_session->common.dst_mac2[1];
		__entry->dst_mac_4  = p_session->common.dst_mac2[2];
		__entry->dst_mac_5  = p_session->common.dst_mac2[3];
		__entry->src_ifidx  = p_session->common.src_ifidx;
		__entry->dst_ifidx  = p_session->common.dst_ifidx;
	),
	TP_printk("ver(%d), state(%d), eth type(0x%02x), vlan(%d), sec(%d), flow(%d), clat(%d), related id(%d)",
		__entry->ver,
		__entry->state,
		__entry->eth_type,
		__entry->vlan_info,
		__entry->sec_info,
		__entry->flow_id,
		__entry->clat,
		__entry->related_id
		)
);
#define DEFINE_ADD_SESSION_ENTRY_EVENT(name)	\
DEFINE_EVENT(noa_session_class, name,	\
	TP_PROTO(struct noa_session *p_session), 		\
	TP_ARGS(p_session))
DEFINE_ADD_SESSION_ENTRY_EVENT(nep_add_session_entry);
// End of trace in NEP

DECLARE_EVENT_CLASS(ring_wrapper_class,
	TP_PROTO(struct noa_ring_wrapper *ring),
	TP_ARGS(ring),
	TP_STRUCT__entry(
		__field(unsigned long, base)
		__field(u32, head)
		__field(u32, tail)
		__field(u32, curr_head)
		__field(u32, curr_tail)
		__field(u32, item_len)
		__field(u32, size)
		__field(u32, flags)
		__field(int, type)
		__string(name, ring->name)
	),
	TP_fast_assign(
		__entry->base = (unsigned long) ring->basic.base;
		__entry->head = noa_ring_head_read_once(ring);
		__entry->tail = noa_ring_tail_read_once(ring);
		__entry->curr_head = ring->basic.head;
		__entry->curr_tail = ring->basic.tail;
		__entry->item_len = ring->basic.item_len;
		__entry->size = ring->basic.size;
		__entry->flags = ring->flags & ~(NOA_RING_TYPE_MASK);
		__entry->type = noa_ring_type(ring);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
		__assign_str(name);
#else
		__assign_str(name, ring->name);
#endif

	),
	TP_printk("ring %s %s, base ptr 0x%lx, head %u, tail %u, "
		  "curr head %u, curr tail %u, item_len %u, size %u, flags %u",
		  __get_str(name), noa_ring_type_to_name(__entry->type),
		  __entry->base, __entry->head, __entry->tail,
		  __entry->curr_head, __entry->curr_tail,
		  __entry->item_len, __entry->size, __entry->flags)
);
#define DEFINE_RING_WRAPPER_EVENT(name) 		\
DEFINE_EVENT(ring_wrapper_class, name, 			\
	TP_PROTO(struct noa_ring_wrapper *ring), 	\
	TP_ARGS(ring))
DEFINE_RING_WRAPPER_EVENT(ring_begin_processing);
DEFINE_RING_WRAPPER_EVENT(ring_complete_processing);

TRACE_EVENT(ring_service_scheduler_run_task,
	TP_PROTO(u32 id, u32 global_bitmap, u32 bitmap),
	TP_ARGS(id, global_bitmap, bitmap),
	TP_STRUCT__entry(
		__field(u32, id)
		__field(u32, global_bitmap)
		__field(u32, bitmap)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->global_bitmap = global_bitmap;
		__entry->bitmap = bitmap;
	),
	TP_printk("task %d global 0x%x bitmap 0x%x",
		__entry->id,
		__entry->global_bitmap,
		__entry->bitmap)
);

TRACE_EVENT(ring_service_forwarder_completed,
	TP_PROTO(int id),
	TP_ARGS(id),
	TP_STRUCT__entry(
		__field(int, id)
	),
	TP_fast_assign(
		__entry->id = id;
	),
	TP_printk("forwarder %d completed", __entry->id)
);

TRACE_EVENT(ring_service_send_completed,
	TP_PROTO(int id, int ret),
	TP_ARGS(id, ret),
	TP_STRUCT__entry(
		__field(int, id)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->ret = ret;
	),
	TP_printk("sender %d completed ret %d", __entry->id, __entry->ret)
);

TRACE_EVENT(ring_service_receive_completed,
	TP_PROTO(int id, int ret),
	TP_ARGS(id, ret),
	TP_STRUCT__entry(
		__field(int, id)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->ret = ret;
	),
	TP_printk("receiver %d completed ret %d", __entry->id, __entry->ret)
);

TRACE_EVENT(ring_service_queue_sender,
	TP_PROTO(int id, u32 ring_head, u32 ring_tail, int ret),
	TP_ARGS(id, ring_head, ring_tail, ret),
	TP_STRUCT__entry(
		__field(int, id)
		__field(u32, ring_head)
		__field(u32, ring_tail)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->ring_head = ring_head;
		__entry->ring_tail = ring_tail;
		__entry->ret = ret;
	),
	TP_printk("queue to %d head %u tail %u ret %d",
		__entry->id, __entry->ring_head, __entry->ring_tail, __entry->ret)
);
TRACE_EVENT(ring_service_queue_forwarder,
	TP_PROTO(int id, u32 ring_head, u32 ring_read_pos, u32 ring_tail, int ret),
	TP_ARGS(id, ring_head, ring_read_pos, ring_tail, ret),
	TP_STRUCT__entry(
		__field(int, id)
		__field(u32, ring_head)
		__field(u32, ring_read_pos)
		__field(u32, ring_tail)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->ring_head = ring_head;
		__entry->ring_read_pos = ring_read_pos;
		__entry->ring_tail = ring_tail;
		__entry->ret = ret;
	),
	TP_printk("queue to %d head %u read %u tail %u ret %d",
		__entry->id, __entry->ring_head,
		__entry->ring_read_pos,
		__entry->ring_tail, __entry->ret)
);

TRACE_EVENT(ring_service_dma_dispatcher,
	TP_PROTO(unsigned long src, unsigned long dst, u32 size, u16 channel),
	TP_ARGS(src, dst, size, channel),
	TP_STRUCT__entry(
		__field(unsigned long, src)
		__field(unsigned long, dst)
		__field(u32, size)
		__field(u16, channel)
	),
	TP_fast_assign(
		__entry->src = src;
		__entry->dst = dst;
		__entry->size = size;
		__entry->channel = channel;
	),
	TP_printk("dma request src 0x%lx dst 0x%lx size %u channel %u",
		__entry->src, __entry->dst, __entry->size, __entry->channel)
);

#endif /* __NOA_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH common
#define TRACE_INCLUDE_FILE noatrace
/* This part must be outside protection */
#include <trace/define_trace.h>
