/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pcie_trace

#if !defined(_TRACE_PCI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PCI_H

#include <linux/device.h>
#include <linux/tracepoint.h>
#include <linux/api-compat.h>

TRACE_EVENT(pci_suspend_start,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));
TRACE_EVENT(pci_suspend_end,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));
TRACE_EVENT(pci_resume_start,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));
TRACE_EVENT(pci_reg_enable,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));
TRACE_EVENT(pci_reset_complete,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));
TRACE_EVENT(pci_phy_fw_write,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));
TRACE_EVENT(pci_resume_end,
	    TP_PROTO(struct device *dev),
	    TP_ARGS(dev),
	    TP_STRUCT__entry(__string(name, dev_name(dev))),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));

#endif /* _TRACE_PCI_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pcie_trace
#include <trace/define_trace.h>
