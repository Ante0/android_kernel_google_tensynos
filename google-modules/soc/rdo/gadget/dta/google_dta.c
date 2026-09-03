// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * Platform driver for Google's EBU IP
 */

#include <linux/module.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <ebu/ebu.h>

static struct usb_string strings_dta[] = {
	[0].s = "Debug Trace Interface",
	{ },
};

static struct usb_gadget_strings str_dta_en = {
	.language = 0x0409,	/* en-us */
	.strings = strings_dta,
};

static struct usb_gadget_strings *dta_strings[] = {
	&str_dta_en,
	NULL,
};
/**
 * Descriptor for an interface with one bulk IN EP
 */
static struct usb_interface_descriptor dta_intf_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 1,
	.bInterfaceClass        = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass     = USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol     = 0,
};

static struct usb_endpoint_descriptor dta_ss_bulk_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor dta_ss_bulk_in_comp_desc = {
	.bLength                = sizeof(dta_ss_bulk_in_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor dta_hs_bulk_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_descriptor_header *dta_ss_descriptors[] = {
	(struct usb_descriptor_header *)&dta_intf_desc,
	(struct usb_descriptor_header *)&dta_ss_bulk_in_desc,
	(struct usb_descriptor_header *)&dta_ss_bulk_in_comp_desc,
	NULL
};

static struct usb_descriptor_header *dta_hs_descriptors[] = {
	(struct usb_descriptor_header *)&dta_intf_desc,
	(struct usb_descriptor_header *)&dta_hs_bulk_in_desc,
	NULL
};

/**
 * struct dta_dev
 * @usb_func: This function
 * @usb_gadget: USB Gadget this function is bound to
 * @bulk_in: Allocated Bulk IN EP for trace data
 * @ebu_controller: Handle for the EBU connected to the UDC this function is
 *	bound to
 * @fifo_dma: DMA address where EBU makes the next MPS sized chunk of data
 *	available. Use this address for bulk transfers
 * @req: Allocated request
 */
struct dta_dev {
	struct usb_function usb_func;
	struct usb_gadget *gadget;
	struct usb_ep *bulk_in;
	struct ebu_controller *ebu;
	dma_addr_t fifo_dma;
	struct usb_request *req;
	struct workqueue_struct *wq;
	struct usb_composite_dev *cdev;
};

/**
 * struct dta_work - the non-atomic work in dta_set_alt or disable
 * @work: Pushed into the function's ordered workqueue
 * @dta: Context pointer
 */
struct dta_work {
	struct work_struct work;
	struct dta_dev *dta;
};

struct dta_opts {
	struct usb_function_instance func_inst;
};

static const struct config_item_type dta_func_type = {
	.ct_owner   = THIS_MODULE,
};

static void dta_complete(struct usb_ep *ep, struct usb_request *req)
{
	int err;
	struct dta_dev *dta = ep->driver_data;

	switch (req->status) {
	case -EPIPE:
		dev_dbg(&dta->gadget->dev, "Request returned EPIPE\n");
		fallthrough;
	case 0:
		err = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (err)
			dev_err(&dta->gadget->dev, "EPin Queue failed: %d\n", err);
		break;
	default:
		dev_err(&dta->gadget->dev, "Request returned %d\n", req->status);
	}
}

static void dta_delayed_set_alt(struct work_struct *w)
{
	struct dta_dev *dta;
	struct dta_work *dw = container_of(w, struct dta_work, work);

	dta = dw->dta;
	kfree(dw);

	if (dta->ebu->enable_data(dta->ebu, dta->gadget->speed))
		return;

	if (usb_ep_queue(dta->bulk_in, dta->req, GFP_ATOMIC))
		return;

	usb_composite_setup_continue(dta->cdev);
}

static int dta_set_alt(struct usb_function *f, unsigned int intf, unsigned int alt)
{
	struct dta_dev *dta;
	struct dta_work *work;
	int ret;

	dta = container_of(f, struct dta_dev, usb_func);
	dev_dbg(&dta->gadget->dev, "DTA Set Alt %u %u\n", intf, alt);

	ret = usb_ep_disable(dta->bulk_in);
	if (ret)
		return ret;

	ret = config_ep_by_speed(dta->gadget, f, dta->bulk_in);
	if (ret)
		return ret;

	ret = usb_ep_enable(dta->bulk_in);
	if (ret)
		return ret;

	dta->req = usb_ep_alloc_request(dta->bulk_in, GFP_KERNEL);
	if (!dta->req)
		goto disable;

	dta->req->buf = NULL;
	dta->req->length = 4096;
	dta->req->dma = dta->fifo_dma;
#ifdef CONFIG_USB_EP_FIFO_MODE_SUPPORT
	dta->req->pre_mapped = 1;
#endif
	dta->req->complete = dta_complete;

	dta->bulk_in->driver_data = dta;
	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		goto disable;

	work->dta = dta;
	INIT_WORK(&work->work, dta_delayed_set_alt);
	queue_work(dta->wq, &work->work);
	return USB_GADGET_DELAYED_STATUS;

disable:
	usb_ep_disable(dta->bulk_in);
	dta->bulk_in->driver_data = NULL;
	if (dta->req)
		usb_ep_free_request(dta->bulk_in, dta->req);

	dta->req = NULL;
	return ret;
}

static void dta_disable(struct usb_function *f)
{
	struct dta_dev *dta = container_of(f, struct dta_dev, usb_func);

	dev_dbg(&dta->gadget->dev, "DTA Disable\n");
	usb_ep_disable(dta->bulk_in);
	dta->bulk_in->driver_data = NULL;
	if (dta->req)
		usb_ep_free_request(dta->bulk_in, dta->req);

	dta->req = NULL;
	dta->ebu->disable_data(dta->ebu);
}

static struct ebu_controller *get_trace_source(struct device_node *usb_node)
{
	struct platform_device *ebu_plat;
	struct device_node *ebu_node = of_parse_phandle(usb_node, "ebu", 0);

	if (!ebu_node)
		return NULL;

	ebu_plat = of_find_device_by_node(ebu_node);
	if (!ebu_plat)
		return NULL;

	return (struct ebu_controller *)platform_get_drvdata(ebu_plat);
}

static int dta_bind(struct usb_configuration *c, struct usb_function *f)
{
	int ret = 0;
	struct usb_string *us;
	struct usb_gadget *gadget = c->cdev->gadget;
	struct dta_dev *dta = container_of(f, struct dta_dev, usb_func);
	struct device *p_dev = gadget->dev.parent;

	dev_dbg(&gadget->dev, "DTA Bind\n");

	us = usb_gstrings_attach(c->cdev, dta_strings, ARRAY_SIZE(strings_dta));
	if (IS_ERR(us))
		return PTR_ERR(us);

	dta_intf_desc.iInterface = us[0].id;

	dta->gadget = gadget;
	dta->cdev = c->cdev;
	ret = usb_interface_id(c, f);
	if (ret < 0)
		return ret;

	dta_intf_desc.bInterfaceNumber = ret;

	if (!p_dev->of_node)
		return -ENODEV;

	dta->ebu = get_trace_source(p_dev->of_node);
	if (!dta->ebu || !dta->ebu->get_fifo)
		return -ENODEV;

	dta->fifo_dma = dta->ebu->get_fifo(dta->ebu, EBU_TRACE_CHANNEL);

	dta->bulk_in = usb_ep_autoconfig_ss(dta->gadget, &dta_ss_bulk_in_desc,
			&dta_ss_bulk_in_comp_desc);
	if (!dta->bulk_in)
		return -ENODEV;

#ifdef CONFIG_USB_EP_FIFO_MODE_SUPPORT
	dta->bulk_in->fifo_mode = 1;
#endif
	dta_hs_bulk_in_desc.bEndpointAddress = dta_ss_bulk_in_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, NULL, dta_hs_descriptors, dta_ss_descriptors, NULL);
	if (ret)
		return ret;

	return dta->ebu->add_mapping(dta->ebu, EBU_TRACE_CHANNEL, dta->bulk_in->address);
}

static void dta_unbind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct usb_gadget *gadget = c->cdev->gadget;
	struct dta_dev *dta = container_of(f, struct dta_dev, usb_func);

	dev_dbg(&gadget->dev, "DTA Unbind\n");
#ifdef CONFIG_USB_EP_FIFO_MODE_SUPPORT
	dta->bulk_in->fifo_mode = 0;
#endif
	usb_free_all_descriptors(f);
	dta->ebu->release_mapping(dta->ebu, EBU_TRACE_CHANNEL);
}

static void dta_free_instance(struct usb_function_instance *func_inst)
{
	struct dta_opts *opts;

	opts = container_of(func_inst, struct dta_opts, func_inst);
	kfree(opts);
}

static struct usb_function_instance *dta_alloc_instance(void)
{
	struct dta_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	opts->func_inst.free_func_inst = dta_free_instance;
	config_group_init_type_name(&opts->func_inst.group, "", &dta_func_type);
	return &opts->func_inst;
}

static void dta_free(struct usb_function *usb_func)
{
	struct dta_dev *dta = container_of(usb_func, struct dta_dev, usb_func);
	kfree(dta);
}

static struct usb_function *dta_alloc(struct usb_function_instance *func_inst)
{
	struct dta_dev *dta;

	dta = kzalloc(sizeof(*dta), GFP_KERNEL);
	dta->usb_func.bind = dta_bind;
	dta->usb_func.unbind = dta_unbind;
	dta->usb_func.set_alt = dta_set_alt;
	dta->usb_func.disable = dta_disable;
	dta->usb_func.free_func = dta_free;
	dta->wq = alloc_ordered_workqueue("google_dta", 0);
	return &dta->usb_func;
}

DECLARE_USB_FUNCTION_INIT(google_dta, dta_alloc_instance, dta_alloc);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google USB debug and trace function");
MODULE_LICENSE("GPL");
