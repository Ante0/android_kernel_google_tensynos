// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Codec3P image accelerator
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Anastasia Young <anastasiayoung@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dma-buf.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/units.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/perf_domain.h>

#include "jpu_pm.h"
#include "jpu_priv.h"
#include "codaj_regdefine.h"
#include "uapi/linux/jpu.h"

#define JPU_DEVCLASS_NAME "jpu_codec"
#define JPU_CHRDEV_NAME "jpu"

#define MAX_INTERRUPT_QUEUE 16
#define MAX_FREQ_IN_HZ 600000000
/*
 * BW values to achieve JPU performance requirements.
 * See b/434954551#comment2 for more info.
 */
#define JPU_BW_READ_MBPS 1090
#define JPU_BW_WRITE_MBPS 52

struct jpu_dmabuf_info {
	struct list_head list;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	dma_addr_t iova;
	dma_addr_t pa;
	struct iosys_map vmap;
	size_t size;
	int fd;
	uint32_t skip_cmo;
};

static int jpu_open(struct inode *inode, struct file *file)
{
	return 0;
}

static void __jpu_unmap_dma_buf(struct jpu_dmabuf_info *dma_info)
{
	if (!dma_info->attachment)
		return;

	if (dma_info->sgt)
		dma_buf_unmap_attachment(dma_info->attachment,
				dma_info->sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(dma_info->dma_buf, dma_info->attachment);
}

static void _jpu_free_dma_info(struct jpu_dmabuf_info *dma_info)
{
	__jpu_unmap_dma_buf(dma_info);
	if (dma_info->vmap.vaddr)
		dma_buf_vunmap(dma_info->dma_buf, &dma_info->vmap);
	if (dma_info->dma_buf)
		dma_buf_put(dma_info->dma_buf);
	kfree(dma_info);
}

static int jpu_set_clk_rate(struct jpu_core *core, uint32_t clk_rate)
{
	int rc;

	if (IS_ERR_OR_NULL(core->dev_freq.df)) {
		dev_err(core->dev, "set rate without dev_freq\n");
		return -EINVAL;
	}
	rc = dev_pm_qos_update_request(&core->dev_freq.qos_req, DIV_ROUND_UP(clk_rate, HZ_PER_KHZ));
	if (rc < 0)
		dev_err(core->dev, "dev_pm_qos_update_request failed: %d\n", rc);

	return rc;
}

/*
 * defined in document Quality_of_Service_PAS section Virtual Channels
 * Soft Real time(VC1) is designed for the clients that have the specific purpose and
 * thus can define the target bandwidth by scenarios, e.g. image post process, codec
 */
#define VC_SOFT_REALTIME 1

static int __jpu_set_bandwidth(struct jpu_core *core,
		const struct jpu_bandwidth_info *bandwidth_info)
{
	int rc;

	dev_dbg(core->dev,
		"set bandwidth read avg %dMBps peak %dMBps write avg %dMBps peak %dMBps",
		bandwidth_info->read_avg_bw,
		bandwidth_info->read_peak_bw,
		bandwidth_info->write_avg_bw,
		bandwidth_info->write_peak_bw);

	rc = google_icc_set_read_bw_gmc(core->jpu_icc_path, bandwidth_info->read_avg_bw,
		bandwidth_info->read_peak_bw, 0, VC_SOFT_REALTIME);
	if (rc) {
		dev_err(core->dev, "google_icc_set_read_bw_gmc failed %d\n", rc);
		return rc;
	}

	rc = google_icc_set_write_bw_gmc(core->jpu_icc_path, bandwidth_info->write_avg_bw,
		bandwidth_info->write_peak_bw, 0, VC_SOFT_REALTIME);
	if (rc) {
		dev_err(core->dev, "google_icc_set_write_bw_gmc failed %d\n", rc);
		return rc;
	}

	rc = google_icc_update_constraint_async(core->jpu_icc_path);
	if (rc) {
		dev_err(core->dev,
			"google_icc_update_constraint_async failed for jpu path %d\n", rc);
		return rc;
	}

	rc = google_icc_update_constraint_async(core->c3p_icc_path);
	if (rc) {
		dev_err(core->dev,
			"google_icc_update_constraint_async failed for c3p path %d\n", rc);
		return rc;
	}

	return rc;
}

static int jpu_set_bandwidth(struct jpu_core *core)
{
	struct jpu_bandwidth_info bandwidth_info;

	bandwidth_info.read_avg_bw = JPU_BW_READ_MBPS;
	bandwidth_info.write_avg_bw = JPU_BW_WRITE_MBPS;
	bandwidth_info.read_peak_bw = bandwidth_info.read_avg_bw;
	bandwidth_info.write_peak_bw = bandwidth_info.write_avg_bw;

	return __jpu_set_bandwidth(core, &bandwidth_info);
}

static int __jpu_close_inst(struct jpu_core *core)
{
	struct jpu_bandwidth_info zero_bandwidth = {0};

	if (core->inst_open_count <= 0) {
		dev_err(core->dev,
			"Cannot close an instance that isn't open. Instance count: %d",
			core->inst_open_count);
		return -EINVAL;
	}

	if (core->inst_open_count == 1) {
		/* Force close - ignore return values. */
		__jpu_set_bandwidth(core, &zero_bandwidth);
		jpu_set_clk_rate(core, 0);
		jpu_pm_power_off(core);
	}
	core->inst_open_count--;
	return 0;
}

static int jpu_close_inst(struct jpu_core *core)
{
	int rc;

	mutex_lock(&core->inst_count_lock);
	rc = __jpu_close_inst(core);
	mutex_unlock(&core->inst_count_lock);

	return rc;
}

static int jpu_release(struct inode *inode, struct file *file)
{
	struct jpu_dmabuf_info *curr, *temp;

	struct jpu_core *core =
		container_of(file->f_inode->i_cdev, struct jpu_core, cdev);

	/* Free the iova mappings for the stored buffers */
	struct jpu_dmabuf_list *dmabuf_list = &core->dmabuf_list;

	mutex_lock(&core->inst_count_lock);
	if (core->inst_open_count > 0) {
		dev_warn(core->dev, "Leaked %d instances, forcing close.\n",
			core->inst_open_count);
		while (core->inst_open_count > 0)
			__jpu_close_inst(core);
	}
	mutex_unlock(&core->inst_count_lock);

	mutex_lock(&dmabuf_list->lock);
	list_for_each_entry_safe(curr, temp, &dmabuf_list->mappings, list) {
		dev_warn(core->dev, "Leaked mapping fd %d iova %pad\n", curr->fd, &curr->iova);
		list_del(&curr->list);
		_jpu_free_dma_info(curr);
	}
	mutex_unlock(&dmabuf_list->lock);

	return 0;
}

static struct jpu_dmabuf_info *pull_dmabuf_info(struct list_head *l,
		struct mutex *list_lock, struct jpu_dmabuf *dmabuf)
{
	struct jpu_dmabuf_info *curr, *next;
	struct jpu_dmabuf_info *found = NULL;

	mutex_lock(list_lock);
	list_for_each_entry_safe(curr, next, l, list) {
		if (curr->fd == dmabuf->fd) {
			list_del(&curr->list);
			found = curr;
			break;
		}
	}
	mutex_unlock(list_lock);
	return found;
}

static void jpu_free_dma_buf(struct list_head *l, struct mutex *list_lock,
		struct jpu_dmabuf *dmabuf)
{
	struct jpu_dmabuf_info *dma_info;

	dma_info = pull_dmabuf_info(l, list_lock, dmabuf);
	if (!dma_info) {
		pr_err("Failed to find dmabuf fd : %d\n", dmabuf->fd);
		return;
	}
	_jpu_free_dma_info(dma_info);
}

static int __jpu_map_dma_buf(struct jpu_dmabuf_info *dma_info, struct device *dev)
{
	dma_info->attachment = dma_buf_attach(dma_info->dma_buf, dev);
	if (IS_ERR(dma_info->attachment)) {
		dev_err(dev, "Failed to dma_buf_attach: %ld\n", PTR_ERR(dma_info->attachment));
		goto err_attach;
	}

	dma_info->sgt = dma_buf_map_attachment(dma_info->attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(dma_info->sgt)) {
		dev_err(dev, "Failed to get sgt: %ld\n", PTR_ERR(dma_info->sgt));
		goto err_map_attachment;
	}

	dma_info->pa = page_to_phys(sg_page(dma_info->sgt->sgl));
	dma_info->iova = sg_dma_address(dma_info->sgt->sgl);
	if (IS_ERR_VALUE(dma_info->iova)) {
		dev_err(dev, "Failed to get iova\n");
		goto err_iova;
	}

	dev_dbg(dev, "buf iova: %pad physical address: %pap", &dma_info->iova, &dma_info->pa);
	return 0;

err_iova:
	dma_buf_unmap_attachment(dma_info->attachment, dma_info->sgt, DMA_BIDIRECTIONAL);
	dma_info->iova = 0;

err_map_attachment:
	dma_buf_detach(dma_info->dma_buf, dma_info->attachment);
	dma_info->sgt = NULL;
err_attach:
	return -ENOMEM;
}

static int jpu_map_dma_buf(struct jpu_core *core, struct jpu_dmabuf *dmabuf)
{
	struct jpu_dmabuf_info *dma_info, *temp;
	struct jpu_dmabuf_list *dmabuf_list = &core->dmabuf_list;

	mutex_lock(&dmabuf_list->lock);
	list_for_each_entry_safe(dma_info, temp, &dmabuf_list->mappings, list) {
		if (dma_info->fd == dmabuf->fd) {
			dev_err(core->dev, "fd %d already mapped at %pad\n",
				dmabuf->fd, &dma_info->iova);

			mutex_unlock(&dmabuf_list->lock);
			return -EALREADY;
		}
	}
	mutex_unlock(&dmabuf_list->lock);

	dma_info = kzalloc(sizeof(*dma_info), GFP_KERNEL);
	if (!dma_info)
		return -ENOMEM;
	dma_info->dma_buf = dma_buf_get(dmabuf->fd);
	if (IS_ERR(dma_info->dma_buf)) {
		int rc = PTR_ERR(dma_info->dma_buf);

		dev_err(core->dev, "failed to get dma buf(%d): %d\n", dmabuf->fd, rc);
		goto err_buf_get;
	}
	dma_info->skip_cmo = dmabuf->skip_cmo;
	if (__jpu_map_dma_buf(dma_info, core->dev))
		goto err_map_dma_buf;
	dma_info->fd = dmabuf->fd;
	if (dma_info->dma_buf->size != dmabuf->size) {
		dev_err(core->dev, "dma buf has incorrect size: %zu\n",
				dma_info->dma_buf->size);
		goto err_map_dma_buf;
	}
	dma_info->size = dmabuf->size;

	mutex_lock(&dmabuf_list->lock);
	list_add_tail(&dma_info->list, &dmabuf_list->mappings);
	mutex_unlock(&dmabuf_list->lock);

	dmabuf->iova = dma_info->iova;
	return 0;

err_map_dma_buf:
	dma_buf_put(dma_info->dma_buf);
err_buf_get:
	kfree(dma_info);
	return -ENOMEM;
}

static int jpu_open_inst(struct jpu_core *core)
{
	int rc = 0;

	mutex_lock(&core->inst_count_lock);
	if (core->inst_open_count == 0) {
		rc = jpu_pm_power_on(core);
		if (rc)
			goto err_power;
		rc = jpu_set_clk_rate(core, MAX_FREQ_IN_HZ);
		if (rc < 0)
			goto err_clk;
		rc = jpu_set_bandwidth(core);
		if (rc)
			goto err_bw;
	}
	core->inst_open_count++;
	mutex_unlock(&core->inst_count_lock);
	return rc;

err_bw:
	jpu_set_clk_rate(core, 0);
err_clk:
	jpu_pm_power_off(core);
err_power:
	mutex_unlock(&core->inst_count_lock);
	return rc;
}

static long jpu_unlocked_ioctl(struct file *fp, unsigned int cmd,
				unsigned long arg)
{
	int rc = 0;
	struct jpu_core *core =
		container_of(fp->f_inode->i_cdev, struct jpu_core, cdev);
	void __user *user_desc = (void __user *)arg;

	switch (cmd) {
	case JPU_IOCX_GET_REG_SZ:
		{
			if (copy_to_user(user_desc, &core->regs_size, sizeof(uint32_t))) {
				dev_err(core->dev, "Failed to copy to user\n");
				rc = -EFAULT;
			}
			break;
		}
	case JPU_IOCX_GET_IOVA:
		{
			struct jpu_dmabuf dmabuf;

			if (copy_from_user(&dmabuf, user_desc, sizeof(dmabuf))) {
				dev_err(core->dev, "Failed to copy from user\n");
				return -EFAULT;
			}
			rc = jpu_map_dma_buf(core, &dmabuf);
			if (rc)
				break;
			if (copy_to_user(user_desc, &dmabuf, sizeof(dmabuf))) {
				dev_err(core->dev, "Failed to copy to user\n");
				rc = -EFAULT;
			}
			break;
		}
	case JPU_IOCX_PUT_IOVA:
		{
			struct jpu_dmabuf dmabuf;

			if (copy_from_user(&dmabuf, user_desc, sizeof(dmabuf))) {
				dev_err(core->dev, "Failed to copy from user\n");
				return -EFAULT;
			}
			jpu_free_dma_buf(&core->dmabuf_list.mappings,
					 &core->dmabuf_list.lock,
					 &dmabuf);
			if (copy_to_user(user_desc, &dmabuf, sizeof(dmabuf))) {
				dev_err(core->dev, "Failed to copy to user\n");
				rc = -EFAULT;
			}
			break;
		}
	case JPU_IOCX_WAIT_INTERRUPT:
		{
			struct jpudrv_intr_info_t intr_info;
			long ret;
			uint32_t intr_reason;
			uint32_t num_elements;

			if (copy_from_user(&intr_info, user_desc, sizeof(intr_info))) {
				dev_err(core->dev, "Failed to copy from user\n");
				return -EFAULT;
			}
			ret = wait_event_timeout(core->wq, kfifo_len(&core->intr_pending_q),
					msecs_to_jiffies(intr_info.timeout));
			if (!ret) {
				dev_err(core->dev, "timed out waiting for job done from HW\n");
				rc = -ETIMEDOUT;
			} else {
				num_elements = kfifo_out_spinlocked(&core->intr_pending_q,
						&intr_reason, sizeof(u32), &core->kfifo_lock);
				if (num_elements > 0)
					intr_info.intr_reason = intr_reason;
				else
					intr_info.intr_reason = 0;
			}
			if (copy_to_user(user_desc, &intr_info, sizeof(intr_info))) {
				dev_err(core->dev, "Failed to copy to user\n");
				rc = -EFAULT;
			}
			break;
		}
	case JPU_IOCX_OPEN_INSTANCE:
		rc = jpu_open_inst(core);
		break;
	case JPU_IOCX_CLOSE_INSTANCE:
		rc = jpu_close_inst(core);
		break;
	case JPU_IOCX_HW_RESET:
		mutex_lock(&core->lock);
		if (core->power_status != POWER_ON) {
			dev_err(core->dev, "Cannot reset JPU while powered off\n");
			mutex_unlock(&core->lock);
			return -EPERM;
		}
		jpu_hw_reset(core);
		mutex_unlock(&core->lock);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int jpu_mmap(struct file *fp, struct vm_area_struct *vm)
{
	unsigned long pfn;
	struct jpu_core *core =
		container_of(fp->f_inode->i_cdev, struct jpu_core, cdev);
	unsigned long size = vm->vm_end - vm->vm_start;

	if (size > PAGE_ALIGN(core->regs_size)) {
		dev_err(core->dev,
			"JPU mmap: requested size 0x%lx exceeds register size 0x%x\n",
			size, PAGE_ALIGN(core->regs_size));
		return -EINVAL;
	}

	vm_flags_set(vm, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	/* This is a CSRs mapping, use pgprot_device */
	vm->vm_page_prot = pgprot_device(vm->vm_page_prot);
	pfn = core->paddr >> PAGE_SHIFT;

	return remap_pfn_range(vm, vm->vm_start, pfn, size,
			vm->vm_page_prot) ? -EAGAIN : 0;
}

static const struct file_operations jpu_fops = {
	.owner = THIS_MODULE,
	.open = jpu_open,
	.release = jpu_release,
	.unlocked_ioctl = jpu_unlocked_ioctl,
	.compat_ioctl = jpu_unlocked_ioctl,
	.mmap = jpu_mmap,
};

static int init_chardev(struct jpu_core *core)
{
	int rc;

	cdev_init(&core->cdev, &jpu_fops);
	core->cdev.owner = THIS_MODULE;
	rc = alloc_chrdev_region(&core->devno, 0, 1, JPU_CHRDEV_NAME);
	if (rc < 0) {
		dev_err(core->dev, "Failed to alloc chrdev region\n");
		goto err;
	}
	rc = cdev_add(&core->cdev, core->devno, 1);
	if (rc) {
		dev_err(core->dev, "Failed to register chrdev\n");
		goto err_cdev_add;
	}

	core->_class = class_create(JPU_DEVCLASS_NAME);
	if (IS_ERR(core->_class)) {
		dev_err(core->dev, "Failed to create device class\n");
		rc = PTR_ERR(core->_class);
		goto err_class_create;
	}

	core->svc_dev = device_create(core->_class, NULL, core->cdev.dev, core,
			JPU_CHRDEV_NAME);
	if (IS_ERR(core->svc_dev)) {
		dev_err(core->dev, "device_create err\n");
		rc = PTR_ERR(core->svc_dev);
		goto err_device_create;
	}
	return rc;

err_device_create:
	class_destroy(core->_class);
err_class_create:
	cdev_del(&core->cdev);
err_cdev_add:
	unregister_chrdev_region(core->devno, 1);
err:
	return rc;
}

static void deinit_chardev(struct jpu_core *core)
{
	device_destroy(core->_class, core->devno);
	class_destroy(core->_class);
	cdev_del(&core->cdev);
	unregister_chrdev_region(core->devno, 1);
}

static irqreturn_t jpu_isr(int irq, void *arg)
{

	uint32_t intr_reason;
	struct jpu_core *core = arg;

	/* Unblock the instance. */
	intr_reason = READ_JPU_REGISTER(core, MJPEG_PIC_STATUS_REG);
	WRITE_JPU_REGISTER(core, MJPEG_PIC_STATUS_REG, intr_reason);

	if (!kfifo_is_full(&core->intr_pending_q)) {
		kfifo_in_spinlocked(&core->intr_pending_q, &intr_reason, sizeof(u32),
				&core->kfifo_lock);
		wake_up(&core->wq);
	} else {
		dev_err(core->dev, "interrupt pending queue is full: %u. Dropping interrupt\n",
				kfifo_len(&core->intr_pending_q));
	}

	return IRQ_HANDLED;
}

static int jpu_init_io(struct jpu_core *core)
{
	struct platform_device *pdev = to_platform_device(core->dev);
	int rc;

	rc = devm_request_irq(&pdev->dev, core->irq, jpu_isr, IRQF_SHARED,
			dev_name(&pdev->dev), core);

	if (rc < 0)
		dev_err(core->dev, "failed to request irq: %d\n", rc);
	return rc;
}

static int jpu_alloc_intr_queue(struct jpu_core *core)
{
	int rc;

	rc = kfifo_alloc(&core->intr_pending_q, MAX_INTERRUPT_QUEUE * sizeof(uint32_t), GFP_KERNEL);
	if (!rc) {
		init_waitqueue_head(&core->wq);
		spin_lock_init(&core->kfifo_lock);
	}
	return rc;
}

static void jpu_free_intr_queue(struct jpu_core *core)
{
	kfifo_free(&core->intr_pending_q);
}

static int jpu_devfreq_init(struct jpu_core *core)
{
	int rc = 0;

	if (IS_ENABLED(CONFIG_GS_PERF_DOMAIN)) {
		struct devfreq *df;

		df = gs_perf_domain_find_devfreq("codec3p", GS_RECOMMENDED_DEVFREQ);
		if (IS_ERR_OR_NULL(df)) {
			rc = PTR_ERR(df);
			dev_err(core->dev, "failed to get devfreq: %d\n", rc);
			return rc;
		}
		rc = google_pm_qos_add_devfreq_request(df, &core->dev_freq.qos_req,
				DEV_PM_QOS_MIN_FREQUENCY, PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
		if (rc) {
			dev_err(core->dev, "failed to add devfreq request: %d\n", rc);
			return rc;
		}
		core->dev_freq.df = df;
	} else {
		dev_dbg(core->dev, "devfreq is not supported\n");
	}
	return rc;
}

static void jpu_devfreq_deinit(struct jpu_core *core)
{
	if (core->dev_freq.df) {
		google_pm_qos_remove_devfreq_request(core->dev_freq.df,
				&core->dev_freq.qos_req);
		core->dev_freq.df = NULL;
	}
}

static int jpu_of_get_resource(struct jpu_core *core)
{
	struct platform_device *pdev = to_platform_device(core->dev);
	struct resource *res;
	int rc = 0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "jpu");
	if (IS_ERR_OR_NULL(res)) {
		rc = PTR_ERR(res);
		dev_err(core->dev, "Failed to find jpu register base: %d\n", rc);
		goto err;
	}

	core->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(core->base)) {
		rc = PTR_ERR(core->base);
		if (rc == 0)
			rc = -EIO;
		dev_err(core->dev, "Failed to map jpu register base: %d\n", rc);
		core->base = NULL;
		goto err;
	}
	core->regs_size = res->end - res->start + 1;
	core->paddr = res->start;
	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0) {
		rc = core->irq;
		dev_err(core->dev, "platform_get_irq failed: %d\n", rc);
		goto err;
	}
	core->jpu_reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(core->jpu_reset)) {
		rc = PTR_ERR(core->jpu_reset);
		dev_err(core->dev, "Unable to get reset: %d\n", rc);
		goto err;
	}
	core->c3p_icc_path = google_devm_of_icc_get(&pdev->dev, "sswrp-codec-3p");
	if (IS_ERR_OR_NULL(core->c3p_icc_path)) {
		dev_err(core->dev, "get c3p_icc_path failed %ld\n",
			PTR_ERR(core->c3p_icc_path));
		rc = -ENODEV;
		goto err;
	}
	core->jpu_icc_path = google_devm_of_icc_get(&pdev->dev, "path_jpu");
	if (IS_ERR_OR_NULL(core->jpu_icc_path)) {
		dev_err(core->dev, "get jpu_icc_path failed %ld\n",
			PTR_ERR(core->jpu_icc_path));
		rc = -ENODEV;
		goto err;
	}
err:
	return rc;
}

static int jpu_of_dt_parse(struct jpu_core *core)
{
	int rc;

	rc = jpu_of_get_resource(core);
	if (rc)
		dev_err(core->dev, "failed to get resource: %d\n", rc);

	return rc;
}

static int jpu_probe(struct platform_device *pdev)
{
	int rc;
	struct jpu_core *core;

	core = devm_kzalloc(&pdev->dev, sizeof(struct jpu_core), GFP_KERNEL);
	if (!core) {
		rc = -ENOMEM;
		goto err;
	}

	core->dev = &pdev->dev;
	platform_set_drvdata(pdev, core);

	rc = init_chardev(core);
	if (rc)
		goto err_init_chardev;

	rc = jpu_of_dt_parse(core);
	if (rc)
		goto err_dt_parse;

	rc = jpu_init_io(core);
	if (rc < 0)
		goto err_io;

	rc = jpu_alloc_intr_queue(core);
	if (rc)
		goto err_intr_queue;

	rc = jpu_pm_init(core);
	if (rc < 0)
		goto err_pm_init;

	rc = jpu_devfreq_init(core);
	if (rc < 0)
		goto err_devfreq;

	INIT_LIST_HEAD(&core->dmabuf_list.mappings);
	mutex_init(&core->dmabuf_list.lock);
	mutex_init(&core->lock);
	mutex_init(&core->inst_count_lock);
	core->power_status = POWER_OFF_RELEASED;

	return rc;

err_devfreq:
	jpu_pm_deinit(core);
err_pm_init:
	jpu_free_intr_queue(core);
err_intr_queue:
err_io:
err_dt_parse:
	deinit_chardev(core);
err_init_chardev:
	platform_set_drvdata(pdev, NULL);
err:
	return rc;
}

static void jpu_remove(struct platform_device *pdev)
{
	struct jpu_core *core = platform_get_drvdata(pdev);

	jpu_pm_deinit(core);
	jpu_free_intr_queue(core);
	jpu_devfreq_deinit(core);
	deinit_chardev(core);
	platform_set_drvdata(pdev, NULL);
}

static const struct dev_pm_ops jpu_pm_ops = {
	SET_RUNTIME_PM_OPS(jpu_runtime_suspend, jpu_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(jpu_pm_suspend, jpu_pm_resume)
};

static const struct of_device_id jpu_dt_match[] = {
	{ .compatible = "google,jpu" },
	{}
};

static struct platform_driver jpu_driver = {
	.probe = jpu_probe,
	.remove = jpu_remove,
	.driver = {
		.name = "google,jpu",
		.owner = THIS_MODULE,
		.pm = &jpu_pm_ops,
		.of_match_table = jpu_dt_match,
	},
};

module_platform_driver(jpu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anastasia Young <anastasiayoung@google.com>");
MODULE_DESCRIPTION("Codec3P JPU driver");
MODULE_IMPORT_NS(DMA_BUF);
