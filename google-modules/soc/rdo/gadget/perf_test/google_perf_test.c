// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * Function driver to measure bulk transfer throughput
 */

#include <linux/module.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/device.h>

/**
 * Descriptor for an interface with one bulk IN and one bulk OUT EP
 */
static struct usb_interface_descriptor bulk_perf_intf_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 2,
	.bInterfaceClass        = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass     = USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol     = 0,
};

static struct usb_endpoint_descriptor bulk_perf_ss_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor bulk_perf_ss_in_comp_desc = {
	.bLength                = sizeof(bulk_perf_ss_in_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor bulk_perf_ss_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor bulk_perf_ss_out_comp_desc = {
	.bLength                = sizeof(bulk_perf_ss_out_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor bulk_perf_hs_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_endpoint_descriptor bulk_perf_hs_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_descriptor_header *bulk_perf_ss_descriptors[] = {
	(struct usb_descriptor_header *)&bulk_perf_intf_desc,
	(struct usb_descriptor_header *)&bulk_perf_ss_in_desc,
	(struct usb_descriptor_header *)&bulk_perf_ss_in_comp_desc,
	(struct usb_descriptor_header *)&bulk_perf_ss_out_desc,
	(struct usb_descriptor_header *)&bulk_perf_ss_out_comp_desc,
	NULL
};

static struct usb_descriptor_header *bulk_perf_hs_descriptors[] = {
	(struct usb_descriptor_header *)&bulk_perf_intf_desc,
	(struct usb_descriptor_header *)&bulk_perf_hs_in_desc,
	(struct usb_descriptor_header *)&bulk_perf_hs_out_desc,
	NULL
};

/**
 * struct bulk_perf_function
 * @usb_func: This function
 * @usb_gadget: USB Gadget this function is bound to
 * @bulk_in: Allocated Bulk IN EP
 * @bulk_out: Allocated Bulk OUT EP
 * @req_in: Allocated request in IN direction
 * @req_out: Allocated request in OUT direction
 */
struct bulk_perf_function {
	struct usb_function usb_func;
	struct usb_gadget *gadget;
	struct usb_ep *bulk_in, *bulk_out;
	struct usb_request *req_in, *req_out;
};

struct bulk_perf_finst {
	struct usb_function_instance func_inst;
};

static const struct config_item_type bulk_perf_func_type = {
	.ct_owner   = THIS_MODULE,
};

static void complete_callback(struct usb_ep *ep, struct usb_request *req)
{
	int err;
	struct bulk_perf_function *perf = ep->driver_data;

	switch (req->status) {
	case -EPIPE:
		dev_dbg(&perf->gadget->dev, "Request returned EPIPE\n");
		fallthrough;
	case 0:
		err = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (err) {
			dev_err(&perf->gadget->dev, "EPin Queue failed: %d\n", err);
		}
		break;
	default:
		dev_err(&perf->gadget->dev, "Request returned %d\n", req->status);
	}
}

static int set_alt_handler(struct usb_function *f, unsigned int intf, unsigned int alt)
{
	struct bulk_perf_function *perf;
	int ret;

	perf = container_of(f, struct bulk_perf_function, usb_func);
	dev_dbg(&perf->gadget->dev, "Perf Set Alt %u %u\n", intf, alt);

	ret = usb_ep_disable(perf->bulk_in);
	if (ret)
		return ret;

	ret = config_ep_by_speed(perf->gadget, f, perf->bulk_in);
	if (ret)
		return ret;

	ret = config_ep_by_speed(perf->gadget, f, perf->bulk_out);
	if (ret)
		return ret;

	perf->req_in = usb_ep_alloc_request(perf->bulk_in, GFP_KERNEL);
	if (!perf->req_in)
		goto disable;
	perf->req_in->buf = kmalloc(4096, GFP_KERNEL);
	perf->req_in->length = 4096;
	perf->req_in->complete = complete_callback;

	perf->bulk_in->driver_data = perf;

	perf->req_out = usb_ep_alloc_request(perf->bulk_out, GFP_KERNEL);
	if (!perf->req_out)
		goto disable;
	perf->req_out->buf = kmalloc(4096, GFP_KERNEL);
	perf->req_out->length = 4096;
	perf->req_out->complete = complete_callback;

	perf->bulk_out->driver_data = perf;

	ret = usb_ep_enable(perf->bulk_in);
	if (ret)
		goto disable;

	ret = usb_ep_queue(perf->bulk_in, perf->req_in, GFP_ATOMIC);
	if (ret) {
		goto disable;
	}

	ret = usb_ep_enable(perf->bulk_out);
	if (ret)
		goto disable;

	ret = usb_ep_queue(perf->bulk_out, perf->req_out, GFP_ATOMIC);
	if (ret) {
		goto disable;
	}

	return 0;
disable:
	usb_ep_disable(perf->bulk_in);
	perf->bulk_in->driver_data = NULL;
	if (perf->req_in) {
		kfree(perf->req_in->buf);
		perf->req_in->buf = NULL;
		usb_ep_free_request(perf->bulk_in, perf->req_in);
	}

	perf->req_in = NULL;
	if (perf->req_out) {
			kfree(perf->req_out->buf);
			perf->req_out->buf = NULL;
			usb_ep_free_request(perf->bulk_out, perf->req_out);
  }

	perf->req_out = NULL;

	return ret;
}

static void disable_handler(struct usb_function *f)
{
	struct bulk_perf_function *perf;

	perf = container_of(f, struct bulk_perf_function, usb_func);
	dev_dbg(&perf->gadget->dev, "Perf Disable\n");

	usb_ep_disable(perf->bulk_in);
	perf->bulk_in->driver_data = NULL;
	if (perf->req_in)
		usb_ep_free_request(perf->bulk_in, perf->req_in);

	perf->req_in = NULL;
}

static int bind_handler(struct usb_configuration *c, struct usb_function *f)
{
	int ret = 0;
	struct usb_gadget *gadget = c->cdev->gadget;
	struct bulk_perf_function *perf = container_of(f, struct bulk_perf_function, usb_func);

	dev_dbg(&gadget->dev, "Perf Bind\n");
	perf->gadget = gadget;
	ret = usb_interface_id(c, f);
	if (ret < 0)
		return ret;

	bulk_perf_intf_desc.bInterfaceNumber = ret;

	perf->bulk_in = usb_ep_autoconfig_ss(perf->gadget, &bulk_perf_ss_in_desc,
			&bulk_perf_ss_in_comp_desc);
	if (!perf->bulk_in)
		return -ENODEV;

	bulk_perf_hs_in_desc.bEndpointAddress = bulk_perf_ss_in_desc.bEndpointAddress;

	perf->bulk_out = usb_ep_autoconfig_ss(perf->gadget, &bulk_perf_ss_out_desc,
			&bulk_perf_ss_out_comp_desc);
	if (!perf->bulk_in)
		return -ENODEV;

	bulk_perf_hs_out_desc.bEndpointAddress = bulk_perf_ss_out_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, NULL, bulk_perf_hs_descriptors, bulk_perf_ss_descriptors, NULL);
	return ret;
}

static void unbind_handler(struct usb_configuration *c,
		struct usb_function *f)
{
	struct usb_gadget *gadget = c->cdev->gadget;
	// struct bulk_perf_function *perf = container_of(f, struct bulk_perf_function, usb_func);

	dev_dbg(&gadget->dev, "Perf Unbind\n");
	usb_free_all_descriptors(f);
}

static void free_instance(struct usb_function_instance *func_inst)
{
	struct bulk_perf_finst *finst;

	finst = container_of(func_inst, struct bulk_perf_finst, func_inst);
	kfree(finst);
}

static struct usb_function_instance *alloc_instance(void)
{
	struct bulk_perf_finst *finst;

	finst = kzalloc(sizeof(*finst), GFP_KERNEL);
	finst->func_inst.free_func_inst = free_instance;
	config_group_init_type_name(&finst->func_inst.group, "", &bulk_perf_func_type);
	return &finst->func_inst;
}

static void function_free(struct usb_function *usb_func)
{
	struct bulk_perf_function *perf = container_of(usb_func, struct bulk_perf_function, usb_func);

	kfree(perf);
}

static struct usb_function *function_alloc(struct usb_function_instance *func_inst)
{
	struct bulk_perf_function *perf_func;

	perf_func = kzalloc(sizeof(*perf_func), GFP_KERNEL);
	perf_func->usb_func.bind = bind_handler;
	perf_func->usb_func.unbind = unbind_handler;
	perf_func->usb_func.set_alt = set_alt_handler;
	perf_func->usb_func.disable = disable_handler;
	perf_func->usb_func.free_func = function_free;
	return &perf_func->usb_func;
}

DECLARE_USB_FUNCTION_INIT(google_perf, alloc_instance, function_alloc);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google USB performance test function");
MODULE_LICENSE("GPL");
