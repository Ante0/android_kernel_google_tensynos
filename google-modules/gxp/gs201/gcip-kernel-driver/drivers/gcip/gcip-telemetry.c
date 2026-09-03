// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP telemetry: logging and tracing.
 *
 * Copyright (C) 2022-2026 Google LLC
 */

#include <linux/cleanup.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/eventfd.h>
#include <linux/jiffies.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <gcip/gcip-memory.h>
#include <gcip/gcip-telemetry.h>

int gcip_telemetry_kci(struct gcip_telemetry *tel,
		       int (*send_kci)(const struct gcip_telemetry_kci_args *),
		       struct gcip_kci *kci)
{
	struct gcip_memory *mem = &tel->memory;
	const struct gcip_telemetry_kci_args args = {
		.kci = kci,
		.addr = mem->dma_addr,
		.size = mem->size,
	};
	int err;

	dev_dbg(tel->dev, "Sending KCI %s", tel->name);
	err = send_kci(&args);

	if (err < 0) {
		dev_err(tel->dev, "KCI %s failed - %d", tel->name, err);
		return err;
	}

	if (err > 0) {
		dev_err(tel->dev, "KCI %s returned %d", tel->name, err);
		return -EBADMSG;
	}

	dev_dbg(tel->dev, "KCI %s Succeeded", tel->name);

	return 0;
}

int gcip_telemetry_set_event(struct gcip_telemetry *tel, u32 eventfd)
{
	struct eventfd_ctx *ctx, *prev_ctx;

	ctx = eventfd_ctx_fdget(eventfd);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	scoped_guard(mutex, &tel->state_ctx_lock) {
		prev_ctx = tel->ctx;
		tel->ctx = ctx;
	}

	if (prev_ctx)
		eventfd_ctx_put(prev_ctx);

	return 0;
}

void gcip_telemetry_unset_event(struct gcip_telemetry *tel)
{
	struct eventfd_ctx *prev_ctx;

	scoped_guard(mutex, &tel->state_ctx_lock) {
		prev_ctx = tel->ctx;
		tel->ctx = NULL;
	}

	if (prev_ctx)
		eventfd_ctx_put(prev_ctx);
}

/**
 * copy_with_wrap() - The helper function to copy data out of the log buffer with wrapping.
 * @dest: The buffer to copy the data to.
 * @start: The start address of the telemetry circular buffer.
 * @raw_head: The current raw head value containing the wrap bit and real head.
 * @length: The length of the data to be copied.
 * @size: The size of telemetry circular buffer.
 *
 * Return: the new head value on success, U32_MAX on failure (e.g. head out of bounds).
 */
static u32 copy_with_wrap(void *dest, void *start, u32 raw_head, u32 length, u32 size)
{
	const u32 max_entry_body = size - sizeof(struct gcip_log_entry_header);
	const u32 wrap_bit = size + sizeof(struct gcip_telemetry_header);
	u32 wrap_bit_val = raw_head & wrap_bit;
	u32 head = raw_head & (wrap_bit - 1);

	if (unlikely(length == 0 || length > max_entry_body || head >= size))
		return U32_MAX;

	if (head + length < size) {
		memcpy(dest, start + head, length);
		head = raw_head + length;
	} else {
		u32 remaining = size - head;
		u32 wrapped_len = length - remaining;

		memcpy(dest, start + head, remaining);
		memcpy(dest + remaining, start, wrapped_len);
		head = (wrap_bit_val ^ wrap_bit) | wrapped_len;
	}

	return head;
}

/**
 * gcip_telemetry_read_log_entry() - Read a log entry from telemetry buffer.
 * @header: Pointer to the telemetry buffer header.
 * @queue_size: Size of the telemetry queue.
 * @buffer: Destination buffer to copy log entry payload.
 * @code: Pointer to output the log entry level/code.
 *
 * This function extracts the log entry header and copies the log payload into the buffer.
 * If corruption is detected, head will be reseted to tail.
 *
 * Return: 0 on success, or a negative errno on otherwise.
 */
static int gcip_telemetry_read_log_entry(struct gcip_telemetry_header *header, size_t queue_size,
					 char *buffer, s16 *code)
{
	void *start = (void *)header + sizeof(*header);
	u32 head = READ_ONCE(header->head);
	struct gcip_log_entry_header entry;
	int ret = 0;

	head = copy_with_wrap(&entry, start, head, sizeof(entry), queue_size);
	if (head == U32_MAX) {
		ret = -EIO;
		goto out;
	}

	head = copy_with_wrap(buffer, start, head, entry.length, queue_size);
	if (head == U32_MAX) {
		ret = -EIO;
		goto out;
	}

	buffer[entry.length] = 0;

	*code = entry.code;

out:
	WRITE_ONCE(header->head, ret ? READ_ONCE(header->tail) : head);

	return ret;
}

/**
 * gcip_telemetry_fw_log() - The fallback function to consume the log buffer.
 * @log: The log telemetry object.
 *
 * This function will consume the log buffer and print it to dmesg from the host CPU. The logging
 * level depends on the code in the header entry.
 */
static void gcip_telemetry_fw_log(const struct gcip_telemetry *log)
{
	struct device *dev = log->dev;
	struct gcip_telemetry_header *header = log->header;
	size_t queue_size = log->memory.size - sizeof(*header);
	const size_t max_entry_body = queue_size - sizeof(struct gcip_log_entry_header);
	char *buffer;
	s16 code;
	int ret;

	buffer = kvmalloc(max_entry_body + 1, GFP_KERNEL);
	if (!buffer) {
		WRITE_ONCE(header->head, READ_ONCE(header->tail));
		return;
	}

	while (READ_ONCE(header->head) != READ_ONCE(header->tail)) {
		ret = gcip_telemetry_read_log_entry(header, queue_size, buffer, &code);
		if (ret) {
			dev_err(dev, "log queue is corrupted");
			break;
		}

		switch (code) {
		case GCIP_FW_LOG_LEVEL_VERBOSE:
		case GCIP_FW_LOG_LEVEL_DEBUG:
		case GCIP_FW_LOG_LEVEL_INFO:
			dev_info(dev, "%s", buffer);
			break;
		case GCIP_FW_LOG_LEVEL_WARN:
			dev_warn(dev, "%s", buffer);
			break;
		case GCIP_FW_LOG_LEVEL_FATAL:
		case GCIP_FW_LOG_LEVEL_ERROR:
			dev_err(dev, "%s", buffer);
			break;
		}
	}

	kvfree(buffer);
}

/**
 * gcip_telemetry_fw_trace() - The fallback function to consume the trace buffer.
 * @trace: The trace telemetry object.
 *
 * This function will do nothing but update the value of the head in the header.
 */
static void gcip_telemetry_fw_trace(const struct gcip_telemetry *trace)
{
	struct gcip_telemetry_header *header = trace->header;

	WRITE_ONCE(header->head, READ_ONCE(header->tail));
}

void gcip_telemetry_irq_handler(struct gcip_telemetry *tel)
{
	/* Early return if we know there is no pending data. */
	if (tel->header && (READ_ONCE(tel->header->head) == READ_ONCE(tel->header->tail)))
		return;

	schedule_work(&tel->work);
}

/**
 * gcip_telemetry_inc_mmap_count() - Increases the telemetry mmap count.
 * @tel: The telemetry to add the mmapped_count.
 * @dif: The number to add the mmapped_count.
 */
static void gcip_telemetry_inc_mmap_count(struct gcip_telemetry *tel, int dif)
{
	guard(mutex)(&tel->mmap_lock);

	tel->mmapped_count += dif;
}

/**
 * gcip_telemetry_vma_ops_open() - The callback function to trigger when VMA is being mapped.
 * @vma: The VM area to be opened.
 *
 * Increses the mmap count of the retrieved telemetry.
 */
static void gcip_telemetry_vma_ops_open(struct vm_area_struct *vma)
{
	struct gcip_telemetry *tel = vma->vm_private_data;

	gcip_telemetry_inc_mmap_count(tel, 1);
}

/**
 * gcip_telemetry_vma_ops_close() - The callback function to trigger when VMA is being unmapped.
 * @vma: The VM area to be closed.
 *
 * Decreses the mmap count of the retrieved telemetry.
 */
static void gcip_telemetry_vma_ops_close(struct vm_area_struct *vma)
{
	struct gcip_telemetry *tel = vma->vm_private_data;

	gcip_telemetry_inc_mmap_count(tel, -1);
}

static const struct vm_operations_struct gcip_telemetry_vma_ops = {
	.open = gcip_telemetry_vma_ops_open,
	.close = gcip_telemetry_vma_ops_close,
};

/**
 * gcip_telemetry_mmap_sgt() - Maps telemetry memory buffer using scatter-gather list.
 * @tel: Pointer to the telemetry structure.
 * @vma: Pointer to the VM area structure representing the mapping.
 * @size: Size of the memory to map.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
static int gcip_telemetry_mmap_sgt(struct gcip_telemetry *tel, struct vm_area_struct *vma,
				   unsigned long size)
{
	struct scatterlist *sg;
	int i;
	unsigned long vm_next = vma->vm_start;

	for_each_sgtable_sg(tel->memory.sgt, sg, i) {
		unsigned long pfn = page_to_pfn(sg_page(sg));
		int ret;

		ret = remap_pfn_range(vma, vm_next, pfn, sg->length, vma->vm_page_prot);
		if (ret) {
			dev_err(tel->dev, "cannot remap log/trace segment#%d size=%u ret=%d\n", i,
				sg->length, ret);
			/* zap_page_range* not exported to modules, leave partial map in place. */
			return ret;
		}

		vm_next += sg->length;
	}

	return 0;
}

int gcip_telemetry_mmap(struct gcip_telemetry *tel, struct vm_area_struct *vma)
{
	struct gcip_memory *mem = &tel->memory;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long orig_pgoff = vma->vm_pgoff;
	int ret;

	size = min(size, mem->size);
	if (!size) {
		dev_err(tel->dev, "The size of the telemetry buffer to be mapped cannot be 0");
		return -EINVAL;
	}

	guard(mutex)(&tel->mmap_lock);

	if (tel->mmapped_count) {
		ret = -EBUSY;
		dev_warn(tel->dev, "%s is already mmapped %ld times", tel->name,
			 tel->mmapped_count);
		return ret;
	}

	vm_flags_set(vma, VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_pgoff = 0;

	if (mem->sgt) {
		dev_dbg(tel->dev, "%s: virt = %pK sgt\n", __func__, mem->virt_addr);
		ret = gcip_telemetry_mmap_sgt(tel, vma, size);
	} else {
		dev_dbg(tel->dev, "%s: virt = %pK phys = %pap\n", __func__, mem->virt_addr,
			&mem->phys_addr);
		ret = remap_pfn_range(vma, vma->vm_start, mem->phys_addr >> PAGE_SHIFT, size,
				      vma->vm_page_prot);
	}

	vma->vm_pgoff = orig_pgoff;
	if (ret)
		return ret;

	vma->vm_ops = &gcip_telemetry_vma_ops;
	vma->vm_private_data = tel;
	tel->mmapped_count = 1;
	mem->host_addr = vma->vm_start;

	return 0;
}

/**
 * gcip_telemetry_warn_no_eventfd() - Prints a warning if no eventfd is registered for consumption.
 * @tel: Pointer to the telemetry structure.
 */
static void gcip_telemetry_warn_no_eventfd(const struct gcip_telemetry *tel)
{
	dev_warn(tel->dev, "No eventfd registered, not consuming the telemetry buffer");
}

/**
 * gcip_telemetry_worker() - The worker for processing log/trace or opaque buffers.
 * @work: The work_struct of the telemetry.
 */
static void gcip_telemetry_worker(struct work_struct *work)
{
	struct gcip_telemetry *tel = container_of(work, struct gcip_telemetry, work);

	scoped_guard(mutex, &tel->state_ctx_lock) {
		if (tel->state != GCIP_TELEMETRY_ENABLED)
			return;

		/*
		 * The runtime side handler and the fallback function should consume all the
		 * available data until head == tail.
		 */
		if (tel->ctx)
			eventfd_signal(tel->ctx);
		else if (tel->fallback_fn)
			tel->fallback_fn(tel);
	}
}

int gcip_telemetry_init(struct gcip_telemetry *tel, enum gcip_telemetry_type type,
			struct device *dev)
{
	struct gcip_memory *mem = &tel->memory;
	const char *name;
	void (*fallback_fn)(const struct gcip_telemetry *tel);
	struct gcip_telemetry_header *header;

	/* The memory have to be set before telemetry init. */
	if (!mem->virt_addr || !mem->size) {
		dev_err(dev, "The telemetry memory should be set before initializing");
		return -EINVAL;
	}

	if (!is_power_of_2(mem->size) || mem->size <= sizeof(struct gcip_telemetry_header)) {
		dev_err(dev,
			"Size of GCIP telemetry buffer must be a power of 2 and greater than %zu.",
			sizeof(struct gcip_telemetry_header));
		return -EINVAL;
	}

	switch (type) {
	case GCIP_TELEMETRY_TYPE_LOG:
		name = GCIP_TELEMETRY_NAME_LOG;
		fallback_fn = gcip_telemetry_fw_log;
		header = mem->virt_addr;
		break;
	case GCIP_TELEMETRY_TYPE_TRACE:
		name = GCIP_TELEMETRY_NAME_TRACE;
		fallback_fn = gcip_telemetry_fw_trace;
		header = mem->virt_addr;
		break;
	case GCIP_TELEMETRY_TYPE_OPAQUE:
		name = GCIP_TELEMETRY_NAME_OPAQUE;
		fallback_fn = gcip_telemetry_warn_no_eventfd;
		header = NULL;
		break;
	default:
		dev_err(dev, "Unrecognized GCIP telemetry type: %d", type);
		return -EINVAL;
	}

	tel->name = name;
	tel->dev = dev;
	tel->type = type;
	tel->fallback_fn = fallback_fn;
	tel->header = header;
	tel->ctx = NULL;
	tel->state = GCIP_TELEMETRY_ENABLED;
	tel->mmapped_count = 0;

	if (tel->header) {
		tel->header->head = 0;
		tel->header->tail = 0;
		tel->header->size = mem->size;
		tel->header->entries_dropped = 0;
	}

	INIT_WORK(&tel->work, gcip_telemetry_worker);
	mutex_init(&tel->mmap_lock);
	mutex_init(&tel->state_ctx_lock);

	return 0;
}

void gcip_telemetry_exit(struct gcip_telemetry *tel)
{
	struct eventfd_ctx *prev_ctx;

	/* Prevents racing with the worker. */
	scoped_guard(mutex, &tel->state_ctx_lock) {
		prev_ctx = tel->ctx;
		tel->ctx = NULL;
		tel->state = GCIP_TELEMETRY_INVALID;
	}

	if (prev_ctx)
		eventfd_ctx_put(prev_ctx);

	cancel_work_sync(&tel->work);
}
