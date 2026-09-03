// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/base64.h>
#include <linux/of_reserved_mem.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/byteorder/generic.h>
#include <linux/atomic.h>
#include <kunit/visibility.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "vdu_service.h"
#include "google-vdu.h"

VISIBLE_IF_KUNIT
int64_t gvdu_handle_mailbox_error(struct device *dev, int message_res,
				  uint32_t *header)
{
	if (goog_mba_nq_xport_get_error(header)) {
		/* GDMC firmware error code is int16_t, alignment is required */
		int16_t gdmc_error =
			(int16_t)(goog_mba_nq_xport_get_data(header) &
				  GENMASK(15, 0));

		return gdmc_error;
	}

	return message_res;
}
EXPORT_SYMBOL_IF_KUNIT(gvdu_handle_mailbox_error);

static bool gvdu_get_user_consent(struct device *dev)
{
	return of_find_property(dev->of_node, "consent-granted", NULL) != NULL;
}

static bool gvdu_is_joint_gdmc_mba_type(struct device *dev)
{
	return of_find_property(dev->of_node, "joint-directive-mba", NULL) != NULL;
}

static int gvdu_parse_dt_properties(struct device *dev)
{
	struct gvdu_base *base = dev_get_drvdata(dev);
	struct property *prop;
	const char *policy;
	size_t total_len = 0;
	char *buf;
	int ret;

	ret = of_property_read_u32(dev->of_node, "vdu-directive-max-size",
				   &base->vdu_directive_max_size);
	if (ret != 0) {
		dev_warn(dev, "Failed to read vdu-directive-max-size node.\n");
		base->vdu_directive_max_size = 0;
	}

	ret = of_property_read_string(dev->of_node, "default-debug-vector",
				      &base->default_debug_vector);
	if (ret) {
		dev_warn(dev, "Failed to read default-debug-vector node.\n");
		base->default_debug_vector = "00000000000000000000000000000000";
	}

	of_property_for_each_string(dev->of_node, "default-policy-type", prop, policy) {
		total_len += strlen(policy) + 1; /* +1 for comma or null terminator */
	}

	if (total_len > 0) {
		const char *sep = "";
		size_t len = 0;

		buf = devm_kzalloc(dev, total_len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		base->default_policy_type = buf;

		of_property_for_each_string(dev->of_node, "default-policy-type", prop, policy) {
			len += scnprintf(buf + len, total_len - len, "%s%s", sep, policy);
			sep = ",";
		}
	} else {
		dev_warn(dev, "Failed to read default-policy-type node.\n");
		base->default_policy_type = "";
	}

	return 0;
}

static void gvdu_notify_status_change(struct device *dev)
{
	kobject_uevent(&dev->kobj, KOBJ_CHANGE);
}

static int64_t gvdu_send_buffer_request(struct device *dev,
					dma_addr_t buffer_pa,
					uint32_t buffer_size,
					enum gdmc_mba_vdu_op_type request)
{
	struct gdmc_mba_vdu_msg msg;
	int message_res;
	struct gvdu_base *base = dev_get_drvdata(dev);

	msg.header =
		goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_VDU, request);
	msg.payload.send_buffer_req.pa_low = (uint32_t)buffer_pa;
	msg.payload.send_buffer_req.pa_high = (uint32_t)(buffer_pa >> 32);
	msg.payload.send_buffer_req.size = buffer_size;

	message_res = gdmc_send_message(base->gdmc_iface, &msg);

	return gvdu_handle_mailbox_error(dev, message_res, &msg.header);
}

static int64_t gvdu_get_timer_request(struct device *dev,
				      struct gdmc_mba_vdu_msg *msg)
{
	struct gvdu_base *base = dev_get_drvdata(dev);
	int message_res;

	msg->header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_VDU,
						   GDMC_MBA_VDU_GET_TIMER);

	message_res = gdmc_send_message(base->gdmc_iface, msg);

	return gvdu_handle_mailbox_error(dev, message_res, &msg->header);
}

static int64_t gvdu_process_directive_request(struct device *dev,
					      dma_addr_t buffer_pa,
					      uint16_t grant_size,
					      uint16_t delegate_size)
{
	struct gdmc_mba_vdu_msg msg;
	int message_res;
	struct gvdu_base *base = dev_get_drvdata(dev);

	msg.header =
		goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_VDU,
					     GDMC_MBA_VDU_PROCESS_DIRECTIVE);
	msg.payload.process_directive_req.pa_low = (uint32_t)buffer_pa;
	msg.payload.process_directive_req.pa_high = (uint32_t)(buffer_pa >> 32);
	msg.payload.process_directive_req.size.grant = grant_size;
	msg.payload.process_directive_req.size.delegate = delegate_size;

	message_res = gdmc_send_message(base->gdmc_iface, &msg);

	return gvdu_handle_mailbox_error(dev, message_res, &msg.header);
}

static ssize_t gvdu_process_directive_separate(struct device *dev)
{
	struct gvdu_base *base = dev_get_drvdata(dev);
	dma_addr_t buffer_pa;
	char *buffer;
	ssize_t buffer_size = max(base->grant_length, base->delegate_length);
	ssize_t ret;

	if (!base->grant_buffer || !base->delegate_buffer)
		return -EINVAL;

	if (base->grant_length + base->delegate_length >
	    base->vdu_directive_max_size)
		return -EINVAL;

	buffer = dma_alloc_coherent(dev, buffer_size, &buffer_pa, GFP_KERNEL);
	if (!buffer || !buffer_pa)
		return -ENOMEM;

	memcpy(buffer, base->grant_buffer, base->grant_length);
	ret = gvdu_send_buffer_request(dev, buffer_pa, base->grant_length,
				       GDMC_MBA_VDU_PROCESS_GRANT);

	if (ret) {
		dev_dbg(dev, "Grant processing request failed: %zd.\n", ret);
		goto cleanup;
	}

	memcpy(buffer, base->delegate_buffer, base->delegate_length);
	ret = gvdu_send_buffer_request(dev, buffer_pa, base->delegate_length,
				       GDMC_MBA_VDU_PROCESS_DELEGATE);

	if (ret)
		dev_dbg(dev, "Delegate processing request failed: %zd.\n", ret);

cleanup:
	dma_free_coherent(dev, buffer_size, buffer, buffer_pa);
	return ret;
}

static ssize_t gvdu_process_directive_joint(struct device *dev)
{
	struct gvdu_base *base = dev_get_drvdata(dev);
	dma_addr_t buffer_pa;
	char *buffer;
	const size_t buffer_size = base->grant_length + base->delegate_length;
	ssize_t ret;

	if (!base->grant_buffer || !base->delegate_buffer)
		return -EINVAL;

	if (buffer_size > base->vdu_directive_max_size)
		return -EINVAL;

	buffer = dma_alloc_coherent(dev, buffer_size, &buffer_pa, GFP_KERNEL);
	if (!buffer || !buffer_pa)
		return -ENOMEM;

	memcpy(buffer, base->grant_buffer, base->grant_length);
	memcpy(buffer + base->grant_length, base->delegate_buffer,
	       base->delegate_length);

	ret = gvdu_process_directive_request(dev, buffer_pa, base->grant_length,
					     base->delegate_length);
	if (ret)
		dev_dbg(dev, "Mailbox request failed: %zd.\n", ret);

	dma_free_coherent(dev, buffer_size, buffer, buffer_pa);
	return ret;
}

/* Caller must free memory */
VISIBLE_IF_KUNIT
ssize_t gvdu_decode_base64_buffer(struct device *dev, const char *source,
				  size_t count, char **dest)
{
	/* 4 Base64 characters -> 3 binary data bytes */
	ssize_t decoded_length_upper_bound = count * 3 / 4;
	int len;
	void *buf;
	struct gvdu_base *base = dev_get_drvdata(dev);

	if (decoded_length_upper_bound > base->vdu_directive_max_size)
		return -EINVAL;

	buf = kmalloc(decoded_length_upper_bound, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = base64_decode(source, count, buf);
	if (len < 0) {
		kfree(buf);
		return -EINVAL;
	}

	*dest = buf;
	return len;
}
EXPORT_SYMBOL_IF_KUNIT(gvdu_decode_base64_buffer);

static ssize_t gvdu_read_base64_buffer(struct device *dev, const char *buf,
				       size_t count, char **dest,
				       ssize_t *length)
{
	int ret;

	ret = gvdu_decode_base64_buffer(dev, buf, count, dest);

	if (ret < 0)
		return ret;

	*length = ret;
	return 0;
}

VISIBLE_IF_KUNIT
ssize_t gvdu_decode_base64_cdev_directive(struct device *dev)
{
	struct gvdu_base *base = dev_get_drvdata(dev);
	uint8_t *buf = base->char_dev.data_buffer;
	size_t remaining_length = base->char_dev.buffer_length;
	uint32_t grant_base64_length;
	uint32_t delegate_base64_length;
	ssize_t ret;

	if (remaining_length < sizeof(grant_base64_length))
		return -EINVAL;
	grant_base64_length = le32_to_cpup((const __le32 *)buf);

	remaining_length -= sizeof(grant_base64_length);
	buf += sizeof(grant_base64_length);

	if (remaining_length < grant_base64_length)
		return -EINVAL;
	ret = gvdu_read_base64_buffer(dev, buf, grant_base64_length,
				      &base->grant_buffer, &base->grant_length);
	if (ret < 0)
		return ret;
	remaining_length -= grant_base64_length;
	buf += grant_base64_length;

	if (remaining_length < sizeof(delegate_base64_length))
		return -EINVAL;
	delegate_base64_length = le32_to_cpup((const __le32 *)buf);
	remaining_length -= sizeof(delegate_base64_length);
	buf += sizeof(delegate_base64_length);

	if (remaining_length != delegate_base64_length)
		return -EINVAL;
	ret = gvdu_read_base64_buffer(dev, buf, delegate_base64_length,
				      &base->delegate_buffer,
				      &base->delegate_length);

	return ret;
}
EXPORT_SYMBOL_IF_KUNIT(gvdu_decode_base64_cdev_directive);

static int gvdu_cdev_open(struct inode *inode, struct file *filp)
{
	struct gvdu_character_device *char_dev =
		container_of(inode->i_cdev, struct gvdu_character_device, cdev);
	struct gvdu_base *base =
		container_of(char_dev, struct gvdu_base, char_dev);

	if (!gvdu_get_user_consent(base->dev)) {
		dev_dbg(base->dev,
			"User consent is not granted, cannot open VDU device\n");
		return -EACCES;
	}

	if (atomic_cmpxchg(&char_dev->is_open, 0, 1) != 0)
		return -EBUSY;
	mutex_lock(&char_dev->data_lock);

	char_dev->buffer_length = 0;
	char_dev->data_buffer = kmalloc(char_dev->max_buffer_size, GFP_KERNEL);
	if (!char_dev->data_buffer) {
		mutex_unlock(&char_dev->data_lock);
		atomic_set(&char_dev->is_open, 0);
		return -ENOMEM;
	}

	kfree(base->grant_buffer);
	base->grant_buffer = NULL;

	kfree(base->delegate_buffer);
	base->delegate_buffer = NULL;

	filp->private_data = base;
	mutex_unlock(&char_dev->data_lock);
	return 0;
}

static int gvdu_cdev_release(struct inode *inode, struct file *filp)
{
	struct gvdu_base *base = filp->private_data;
	ssize_t ret;

	mutex_lock(&base->char_dev.data_lock);

	ret = gvdu_decode_base64_cdev_directive(base->dev);
	if (ret < 0) {
		dev_err(base->dev, "Failed to decode base64 directive, %zd\n",
			ret);
		goto cleanup;
	}

	if (gvdu_is_joint_gdmc_mba_type(base->dev))
		ret = gvdu_process_directive_joint(base->dev);
	else
		ret = gvdu_process_directive_separate(base->dev);

	if (ret < 0)
		dev_err(base->dev, "Failed to process directive, %zd\n", ret);
	else
		gvdu_notify_status_change(base->dev);

cleanup:
	kfree(base->char_dev.data_buffer);
	base->char_dev.data_buffer = NULL;
	base->char_dev.buffer_length = 0;

	kfree(base->grant_buffer);
	base->grant_buffer = NULL;

	kfree(base->delegate_buffer);
	base->delegate_buffer = NULL;

	mutex_unlock(&base->char_dev.data_lock);
	atomic_set(&base->char_dev.is_open, 0);
	return ret;
}

static ssize_t gvdu_cdev_write(struct file *filp, const char __user *buf,
			       size_t len, loff_t *off)
{
	struct gvdu_base *base = filp->private_data;
	struct gvdu_character_device *char_dev = &base->char_dev;
	size_t bytes_to_write;
	size_t space_available;
	ssize_t ret;

	mutex_lock(&char_dev->data_lock);

	if (char_dev->data_buffer == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	space_available = char_dev->max_buffer_size - char_dev->buffer_length;
	if (space_available == 0) {
		dev_err(base->dev, "Data buffer is full\n");
		ret = -ENOSPC;
		goto exit;
	}

	bytes_to_write = min(len, space_available);
	if (copy_from_user(char_dev->data_buffer + char_dev->buffer_length, buf,
			   bytes_to_write) != 0) {
		dev_err(base->dev, "Failed to copy data from user\n");
		ret = -EFAULT;
		goto exit;
	}
	char_dev->buffer_length += bytes_to_write;
	*off += bytes_to_write;
	ret = bytes_to_write;

exit:
	mutex_unlock(&char_dev->data_lock);
	return ret;
}

static const struct file_operations gvdu_chardev_fops = {
	.owner = THIS_MODULE,
	.open = gvdu_cdev_open,
	.release = gvdu_cdev_release,
	.write = gvdu_cdev_write,
};

static void gvdu_destroy_chardev(struct device *dev)
{
	struct gvdu_base *base = dev_get_drvdata(dev);

	if (base->char_dev.class) {
		device_destroy(base->char_dev.class, base->char_dev.devt);
		cdev_del(&base->char_dev.cdev);
		class_destroy(base->char_dev.class);
		unregister_chrdev_region(base->char_dev.devt, 1);
	}
}

static int gvdu_create_chardev(struct device *dev)
{
	struct gvdu_base *base = dev_get_drvdata(dev);
	struct device *cdev_node;
	int ret;

	/*
	 * Base64 directive size <= 4 * ceil(vdu_directive_max_size / 3)
	 * The buffer also holds the grant and delegate sizes, each as a uint32_t
	 */
	base->char_dev.max_buffer_size =
		4 * DIV_ROUND_UP(base->vdu_directive_max_size, 3) +
		2 * sizeof(uint32_t);

	ret = alloc_chrdev_region(&base->char_dev.devt, 0, 1, KBUILD_MODNAME);
	if (ret) {
		dev_err(dev, "Failed to allocate char device region\n");
		return ret;
	}

	base->char_dev.class = class_create(KBUILD_MODNAME);
	if (IS_ERR(base->char_dev.class)) {
		ret = PTR_ERR(base->char_dev.class);
		dev_err(dev, "Failed to create device class\n");
		goto err_class_create;
	}

	cdev_init(&base->char_dev.cdev, &gvdu_chardev_fops);
	base->char_dev.cdev.owner = THIS_MODULE;

	ret = cdev_add(&base->char_dev.cdev, base->char_dev.devt, 1);
	if (ret) {
		dev_err(dev, "Failed to add gvdu cdev\n");
		goto err_cdev_add;
	}

	cdev_node = device_create(base->char_dev.class, dev,
				  base->char_dev.devt, NULL, KBUILD_MODNAME);
	if (IS_ERR(cdev_node)) {
		ret = PTR_ERR(cdev_node);
		dev_err(dev, "Failed to create character device file\n");
		goto err_device_create;
	}

	atomic_set(&base->char_dev.is_open, 0);
	mutex_init(&base->char_dev.data_lock);

	return 0;

err_device_create:
	cdev_del(&base->char_dev.cdev);
err_cdev_add:
	class_destroy(base->char_dev.class);
err_class_create:
	unregister_chrdev_region(base->char_dev.devt, 1);
	return ret;
}

static int64_t gvdu_emit_retrieve_nonce_request(struct device *dev,
						char *sysfs_buf,
						bool write_nonce)
{
	const uint32_t buffer_size =
		sizeof(struct gdmc_mba_vdu_msg_nonce_buffer);
	dma_addr_t buffer_pa;
	struct gdmc_mba_vdu_msg_nonce_buffer *buffer =
		dma_alloc_coherent(dev, buffer_size, &buffer_pa, GFP_KERNEL);
	ssize_t ret;

	if (!buffer || !buffer_pa)
		return -ENOMEM;

	ret = gvdu_send_buffer_request(dev, buffer_pa, buffer_size,
				       GDMC_MBA_VDU_RETRIEVE_NONCE);
	if (ret) {
		dev_dbg(dev, "Mailbox request failed: %zd.\n", ret);
		goto exit;
	}

	if (write_nonce)
		ret = sysfs_emit(sysfs_buf, "%*phN\n", GDMC_MBA_VDU_NONCE_LEN,
				 buffer->nonce);
	else
		ret = sysfs_emit(sysfs_buf, "%*phN\n", GDMC_MBA_VDU_CHIPID_LEN,
				 buffer->chipid);

exit:
	dma_free_coherent(dev, buffer_size, buffer, buffer_pa);
	return ret;
}

static int64_t gvdu_emit_get_timer_request(struct device *dev, char *buf,
					   bool write_timer)
{
	struct gdmc_mba_vdu_msg msg;
	ssize_t ret;

	ret = gvdu_get_timer_request(dev, &msg);
	if (ret) {
		dev_dbg(dev, "Mailbox request failed: %zd.\n", ret);
		return ret;
	}

	if (write_timer)
		ret = sysfs_emit(buf, "%04x\n",
				 msg.payload.get_timer_rsp.remaining_mins);
	else
		ret = sysfs_emit(buf, "%u\n", msg.payload.get_timer_rsp.status);

	return ret;
}

static ssize_t chip_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return gvdu_emit_retrieve_nonce_request(dev, buf, false);
}

static DEVICE_ATTR_RO(chip_id);

static ssize_t default_debug_vector_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct gvdu_base *base = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", base->default_debug_vector);
}

static DEVICE_ATTR_RO(default_debug_vector);

static ssize_t default_policy_type_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct gvdu_base *base = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", base->default_policy_type);
}

static DEVICE_ATTR_RO(default_policy_type);

static ssize_t nonce_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return gvdu_emit_retrieve_nonce_request(dev, buf, true);
}

static DEVICE_ATTR_RO(nonce);

static ssize_t consent_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%c\n", gvdu_get_user_consent(dev) ? '1' : '0');
}

static DEVICE_ATTR_RO(consent);

static ssize_t vector_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	const int buffer_size = sizeof(struct gdmc_mba_vdu_msg_vector_buffer);
	dma_addr_t buffer_pa;
	struct gdmc_mba_vdu_msg_vector_buffer *buffer =
		dma_alloc_coherent(dev, buffer_size, &buffer_pa, GFP_KERNEL);
	ssize_t ret;

	if (!buffer || !buffer_pa)
		return -ENOMEM;

	ret = gvdu_send_buffer_request(dev, buffer_pa, buffer_size,
				       GDMC_MBA_VDU_GET_VECTOR);
	if (ret) {
		dev_dbg(dev, "Mailbox request failed: %zd.\n", ret);
		goto exit;
	}

	ret = sysfs_emit(buf, "%*phN\n", GDMC_MBA_VDU_VECTOR_LEN, buffer->data);

exit:
	dma_free_coherent(dev, buffer_size, buffer, buffer_pa);
	return ret;
}

static DEVICE_ATTR_RO(vector);

static ssize_t remaining_mins_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return gvdu_emit_get_timer_request(dev, buf, true);
}

static DEVICE_ATTR_RO(remaining_mins);

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return gvdu_emit_get_timer_request(dev, buf, false);
}

static DEVICE_ATTR_RO(enabled);

static struct attribute *gvdu_info_attrs[] = {
	&dev_attr_chip_id.attr, &dev_attr_default_debug_vector.attr,
	&dev_attr_default_policy_type.attr, NULL
};

static struct attribute *gvdu_interface_attrs[] = { &dev_attr_nonce.attr,
						    NULL };

static struct attribute *gvdu_status_attrs[] = { &dev_attr_consent.attr,
						 &dev_attr_vector.attr,
						 &dev_attr_remaining_mins.attr,
						 &dev_attr_enabled.attr, NULL };

static const struct attribute_group gvdu_info_group = {
	.name = "info",
	.attrs = gvdu_info_attrs,
};

static const struct attribute_group gvdu_interface_group = {
	.name = "interface",
	.attrs = gvdu_interface_attrs,
};

static const struct attribute_group gvdu_status_group = {
	.name = "status",
	.attrs = gvdu_status_attrs,
};

static const struct attribute_group *gvdu_groups[] = {
	&gvdu_info_group,
	&gvdu_interface_group,
	&gvdu_status_group,
	NULL,
};

static int google_vdu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gvdu_base *base;
	int ret;

	base = devm_kzalloc(dev, sizeof(struct gvdu_base), GFP_KERNEL);
	if (!base)
		return -ENOMEM;
	platform_set_drvdata(pdev, base);

	/* Store device pointer for use in char device context */
	base->dev = dev;

	ret = gvdu_parse_dt_properties(dev);
	if (ret < 0)
		return ret;

	base->gdmc_iface = gdmc_iface_get(dev);

	if (IS_ERR(base->gdmc_iface)) {
		dev_err(dev, "Failed to get GDMC interface");
		return PTR_ERR(base->gdmc_iface);
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get reserved memory region.\n");
		goto err_mem_alloc;
	}

	ret = gvdu_create_chardev(dev);
	if (ret != 0)
		goto err_create_chardev;

	return 0;

err_create_chardev:
	of_reserved_mem_device_release(dev);
err_mem_alloc:
	gdmc_iface_put(base->gdmc_iface);
	return ret;
}

static void google_vdu_remove(struct platform_device *pdev)
{
	struct gvdu_base *base = platform_get_drvdata(pdev);

	gvdu_destroy_chardev(&pdev->dev);

	kfree(base->grant_buffer);
	kfree(base->delegate_buffer);

	of_reserved_mem_device_release(&pdev->dev);
	gdmc_iface_put(base->gdmc_iface);
}

static const struct of_device_id google_vdu_of_match[] = {
	{ .compatible = "google,volatile-debug-unlock" },
	{},
};
MODULE_DEVICE_TABLE(of, google_vdu_of_match);

static struct platform_driver google_vdu_driver = {
	.probe = google_vdu_probe,
	.remove = google_vdu_remove,
	.driver = {
		.name  = "google-vdu",
		.of_match_table = of_match_ptr(google_vdu_of_match),
		.dev_groups = gvdu_groups,
	},
};
module_platform_driver(google_vdu_driver);

MODULE_AUTHOR("Kanstantsin Yarmash <kyarmash@google.com>");
MODULE_DESCRIPTION("Google Volatile Debug Unlock");
MODULE_LICENSE("GPL");
