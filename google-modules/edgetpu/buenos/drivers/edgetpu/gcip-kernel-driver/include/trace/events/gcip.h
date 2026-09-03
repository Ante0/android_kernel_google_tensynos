/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace events for GCIP
 *
 * Copyright (c) 2026 Google LLC
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gcip

#if !defined(_TRACE_GCIP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GCIP_H

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/stringify.h>
#include <linux/tracepoint.h>

#define GCIP_TRACE_SYSTEM __stringify(TRACE_SYSTEM)

TRACE_EVENT(gcip_map_dmabuf_attach,

	TP_PROTO(struct device *dev, struct dma_buf *dmabuf),

	TP_ARGS(dev, dmabuf),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u64, size)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->size = dmabuf->size;
	),

	TP_printk("dev=%s size=%#llx",
		  __get_str(devname), __entry->size)
);

TRACE_EVENT(gcip_map_dmabuf_map_attachment_start,

	TP_PROTO(struct device *dev, struct dma_buf *dmabuf),

	TP_ARGS(dev, dmabuf),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u64, size)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->size = dmabuf->size;
	),

	TP_printk("dev=%s size=%#llx",
		  __get_str(devname), __entry->size)
);

TRACE_EVENT(gcip_map_dmabuf_map_attachment_end,

	TP_PROTO(struct device *dev, struct dma_buf *dmabuf),

	TP_ARGS(dev, dmabuf),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u64, size)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->size = dmabuf->size;
	),

	TP_printk("dev=%s size=%#llx",
		  __get_str(devname), __entry->size)
);

TRACE_EVENT(gcip_iommu_map_sg_start,

	TP_PROTO(struct device *dev, __u32 pasid, __u64 iova, __u64 size),

	TP_ARGS(dev, pasid, iova, size),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u32, pasid)
		__field(__u64, iova)
		__field(__u64, size)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->pasid = pasid;
		__entry->iova = iova;
		__entry->size = size;
	),

	TP_printk("dev=%s pasid=%u iova=%#llx size=%#llx",
		  __get_str(devname), __entry->pasid, __entry->iova, __entry->size)
);

TRACE_EVENT(gcip_iommu_map_sg_end,

	TP_PROTO(struct device *dev, __u32 pasid, __u64 iova, __s64 ret),

	TP_ARGS(dev, pasid, iova, ret),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u32, pasid)
		__field(__u64, iova)
		__field(__s64, ret)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->pasid = pasid;
		__entry->iova = iova;
		__entry->ret = ret;
	),

	TP_printk("dev=%s pasid=%u iova=%#llx ret=%lld",
		  __get_str(devname), __entry->pasid, __entry->iova, __entry->ret)
);

TRACE_EVENT(gcip_sync_sg_start,

	TP_PROTO(struct device *dev, __u64 iova, __u32 for_device),

	TP_ARGS(dev, iova, for_device),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u64, iova)
		__field(__u32, for_device)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->iova = iova;
		__entry->for_device = for_device;
	),

	TP_printk("dev=%s iova=%#llx for_device=%u",
		  __get_str(devname), __entry->iova, __entry->for_device)
);

TRACE_EVENT(gcip_sync_sg_end,

	TP_PROTO(struct device *dev, __u64 iova),

	TP_ARGS(dev, iova),

	TP_STRUCT__entry(
		__string(devname, dev_name(dev))
		__field(__u64, iova)
	),

	TP_fast_assign(
		__assign_str(devname);
		__entry->iova = iova;
	),

	TP_printk("dev=%s iova=%#llx",
		  __get_str(devname), __entry->iova)
);

#endif /* _TRACE_GCIP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
