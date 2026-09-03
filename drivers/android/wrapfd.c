/* SPDX-License-Identifier: GPL-2.0-only */
/* wrapfd.c
 *
 * Wrapfd
 *
 * Copyright (C) 2025 Google, Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/bvec.h>
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/wrapfd.h>
#include <uapi/linux/wrapfd.h>

/* 1 buffer for the start, and at most 2 at the end. */
#define MAX_NR_BOUNCE_BUFS 3

/* 1 per bounce page (3), and 1 for content read directly into the buffer. */
#define MAX_NR_KVECS (MAX_NR_BOUNCE_BUFS + 1)

struct wrap_ctx;
struct wrap_content;
static const struct file_operations wrap_fops;

struct wrap_content_operations {
	int (*create_wrap)(struct wrap_content *content, struct wrap_ctx *ctx);
	int (*load)(struct wrap_content *content, struct file *file,
		    loff_t file_offs, loff_t buf_offs, loff_t len);
	loff_t (*llseek)(struct wrap_content *content, loff_t offs, int whence);
	int (*mmap_prepare)(struct wrap_content *content,
			    struct vm_area_struct *vma);
	int (*mmap)(struct wrap_content *content, struct vm_area_struct *vma);
	void (*free)(struct wrap_content *content);
	struct wrap_content *(*make_writable)(struct wrap_content *content,
			      bool writable);
	bool (*is_writable)(struct wrap_content *content);
	void (*show_fdinfo)(struct wrap_content *content, struct seq_file *m);
	int (*get_mappable)(struct wrap_content *content, struct device *dev,
			    union wrapfd_mappable *mappable);
	void (*put_mappable)(struct wrap_content *content,
			    union wrapfd_mappable *mappable);
	int (*ioctl)(struct wrap_content *content,
		     unsigned int cmd, unsigned long arg);
};

/* Abstract wrap content to be embedded in a concrete content object. */
struct wrap_content {
	struct wrap_content_operations *ops;
	bool close_on_exec;
};

/* dmabuf content */
struct wrap_content_dmabuf {
	struct wrap_content content;
	struct dma_buf *dmabuf;
	bool writable;
};

static int dmabuf_content_create_wrap(struct wrap_content *content,
				      struct wrap_ctx *ctx)
{
	struct wrap_content_dmabuf *dmabuf_content;
	struct file *file;
	int fd;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;

	file = anon_inode_create_getfile("[wrapfd]", &wrap_fops, ctx,
					 dmabuf_content->writable ? O_RDWR : O_RDONLY, NULL);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}

	/*
	 * Anonymous inodes are created with size == 0. To ensure that calls
	 * like fstat() work as expected, copy the size from the buffer we are
	 * wrapping.
	 */
	i_size_write(file_inode(file), dmabuf_content->dmabuf->size);
	fd_install(fd, file);

	return fd;
}

struct wrap_io_ctx {
	u8 *bounce_bufs[MAX_NR_BOUNCE_BUFS];
	u8 *dst_buf;
	size_t start_bounce_len;
	size_t end_bounce_len;
	loff_t file_offs;
	loff_t buf_offs;
	size_t len;
	loff_t direct_read_buf_offs;
	size_t direct_read_len;
	size_t bytes_read;
	size_t file_read_len;
	struct kiocb kiocb;
	struct iov_iter iter;
	struct kvec iov[MAX_NR_KVECS];
};

struct dmabuf_load_param {
	struct dma_buf *dmabuf;
	struct iosys_map map;
	struct wrap_io_ctx io_ctx;
};

static void prepare_iocb_request(struct wrap_io_ctx *io_ctx, struct file *file)
{
	unsigned int nr_segs = 0;
	unsigned int cur_bounce_page = 0;
	struct kiocb *kiocb = &io_ctx->kiocb;
	int i;

	if (io_ctx->start_bounce_len) {
		io_ctx->iov[nr_segs].iov_base = io_ctx->bounce_bufs[cur_bounce_page];
		io_ctx->iov[nr_segs].iov_len = io_ctx->start_bounce_len;
		nr_segs++;
		cur_bounce_page++;
	}

	if (io_ctx->direct_read_len) {
		io_ctx->iov[nr_segs].iov_base = io_ctx->dst_buf + io_ctx->direct_read_buf_offs;
		io_ctx->iov[nr_segs].iov_len = io_ctx->direct_read_len;
		nr_segs++;
	}

	for (i = 0; i < io_ctx->end_bounce_len / PAGE_SIZE; i++) {
		io_ctx->iov[nr_segs].iov_base = io_ctx->bounce_bufs[cur_bounce_page];
		io_ctx->iov[nr_segs].iov_len = PAGE_SIZE;
		nr_segs++;
		cur_bounce_page++;
	}

	init_sync_kiocb(kiocb, file);
	/* File offset must be page aligned for direct I/O requests. */
	kiocb->ki_pos = PAGE_ALIGN_DOWN(io_ctx->file_offs);
	kiocb->ki_flags |= IOCB_DIRECT;
	iov_iter_kvec(&io_ctx->iter, ITER_DEST, io_ctx->iov, nr_segs, io_ctx->file_read_len);
}

/*
 * Splits an I/O request into at most 4 segments: a start bounce page for when the file or
 * buffer offsets are unaligned to avoid overwriting any of the existing data in the first page,
 * a middle segment that writes directly into the buffer, and a final segment that covers the
 * remainder with at most 2 bounce pages.
 *
 * When using bounce pages, a full page is copied from the file as part of the I/O request, and only
 * the parts that are needed are copied into the buffer later. Since the first page of the I/O
 * request may also need to be bounced, the middle segment of the I/O request, which is read
 * directly into the buffer if there is enough space, may need to be moved back to its final
 * destination, which also happens later.
 *
 * All of the logic to shuttle the buffer contents to their final destination is in
 * wrap_io_complete().
 */
static int wrap_io_prepare(struct file *file, loff_t file_offs, u8 *dst_buf, loff_t buf_offs,
			   loff_t len, struct wrap_io_ctx *io_ctx)
{
	u8 *bounce_bufs[MAX_NR_BOUNCE_BUFS] = {};
	size_t start_bounce_len = 0;
	loff_t direct_read_buf_offs = buf_offs;
	size_t direct_read_len = 0;
	size_t end_bounce_len;
	size_t nr_bounce_pages;
	size_t file_read_len;
	loff_t buf_end = buf_offs + len;
	int i, ret = 0;

	/*
	 * File offset and read length must be page aligned for direct I/O requests, so page align
	 * the start and end of the read to figure out how much we'll actually be reading from the
	 * file.
	 */
	file_read_len = PAGE_ALIGN(file_offs + len) - PAGE_ALIGN_DOWN(file_offs);

	/*
	 * If either the file or buffer offset are unaligned, then using direct I/O into the first
	 * page of the buffer will overwrite some of the data that is already there. Allocate a
	 * bounce page for that scenario, and later only copy the amount of data that belongs in the
	 * first page.
	 */
	if (!PAGE_ALIGNED(file_offs | buf_offs)) {
		/*
		 * The contents of interest in this bounce page will be copied to the buffer
		 * starting at buf_offs after the direct I/O request completes. The amount of data
		 * copied will be everything in the bounce page after file_offset bytes.
		 *
		 * Therefore, the next address where data needs to be read into is buf_offs +
		 * (PAGE_SIZE - offset_in_page(file_offs)). However, this address may not be
		 * page aligned, and therefore not suitable for direct I/O, so page align it.
		 *
		 * This means that the data will need to be shifted backwards if it is read into
		 * the buffer directly.
		 */
		direct_read_buf_offs = PAGE_ALIGN(buf_offs + PAGE_SIZE -
						    offset_in_page(file_offs));
		start_bounce_len = PAGE_SIZE;
	}

	/*
	 * Read as much as possible directly into the buffer without causing any overwrites beyond
	 * the range we're reading into, and since direct I/O is done in units of pages,
	 * ensure that there is at least a page to read.
	 */
	if (direct_read_buf_offs <= (buf_end - PAGE_SIZE))
		direct_read_len = PAGE_ALIGN_DOWN(buf_end) - direct_read_buf_offs;

	/*
	 * Bounce the remainder, which is capped at 2 pages, since we may have shifted the data
	 * earlier, because of the buffer offset by at most one page, and then any other data
	 * at the tail which may cross into another page.
	 */
	end_bounce_len = file_read_len - start_bounce_len - direct_read_len;
	WARN_ON(end_bounce_len > PAGE_SIZE * 2);

	nr_bounce_pages = (start_bounce_len + end_bounce_len) / PAGE_SIZE;
	for (i = 0; i < nr_bounce_pages; i++) {
		bounce_bufs[i] = (u8 *)__get_free_page(GFP_KERNEL);
		if (!bounce_bufs[i]) {
			ret = -ENOMEM;
			goto err_free_bounce_pages;
		}
	}

	memset(io_ctx, 0, sizeof(*io_ctx));
	memcpy(io_ctx->bounce_bufs, bounce_bufs, sizeof(io_ctx->bounce_bufs));
	io_ctx->dst_buf = dst_buf;
	io_ctx->start_bounce_len = start_bounce_len;
	io_ctx->end_bounce_len = end_bounce_len;
	io_ctx->file_offs = file_offs;
	io_ctx->buf_offs = buf_offs;
	io_ctx->direct_read_buf_offs = direct_read_buf_offs;
	io_ctx->direct_read_len = direct_read_len;
	io_ctx->len = len;
	io_ctx->file_read_len = file_read_len;
	prepare_iocb_request(io_ctx, file);
	return 0;

err_free_bounce_pages:
	/* free_page() checks that the provided address is not NULL. */
	for (i = 0; i < nr_bounce_pages; i++)
		free_page((unsigned long)bounce_bufs[i]);
	return ret;

}

static ssize_t wrap_io_read(struct wrap_io_ctx *io_ctx, struct file *file)
{
	ssize_t bytes_read;

	while (io_ctx->bytes_read < io_ctx->file_read_len) {
		io_ctx->iter.count = min_t(size_t, MAX_RW_COUNT,
					   PAGE_ALIGN(io_ctx->file_read_len - io_ctx->bytes_read));

		bytes_read = vfs_iocb_iter_read(file, &io_ctx->kiocb, &io_ctx->iter);
		if (bytes_read <= 0)
			return bytes_read;

		io_ctx->bytes_read += bytes_read;
	}

	/* File was too short / early EOF. */
	return io_ctx->bytes_read < offset_in_page(io_ctx->file_offs) + io_ctx->len ? -EINVAL : 0;
}

static void wrap_io_complete(struct wrap_io_ctx *io_ctx)
{
	u8 *dst, *src, *direct_read_start;
	size_t tot_len, len;
	unsigned int cur_bounce_page = 0;
	int i;

	if (io_ctx->bytes_read < (offset_in_page(io_ctx->file_offs) + io_ctx->len))
		goto out;

	tot_len = io_ctx->len;
	dst = io_ctx->dst_buf + io_ctx->buf_offs;

	if (io_ctx->start_bounce_len) {
		src = io_ctx->bounce_bufs[cur_bounce_page] + offset_in_page(io_ctx->file_offs);
		/* Handle the case where all of the requested data is in the first bounce page. */
		len = min(tot_len, PAGE_SIZE - offset_in_page(io_ctx->file_offs));
		memcpy(dst, src, len);
		dst += len;
		tot_len -= len;
		cur_bounce_page++;
	}

	if (io_ctx->direct_read_len) {
		direct_read_start = io_ctx->dst_buf + io_ctx->direct_read_buf_offs;
		len = min(tot_len, io_ctx->direct_read_len);

		/*
		 * If there's anything that was copied directly into the dmabuf, check to make sure
		 * it's in the right place. Shift it back if it's not.
		 */
		if (direct_read_start != dst)
			memmove(dst, direct_read_start, len);

		dst += len;
		tot_len -= len;
	}

	for (i = 0; i < io_ctx->end_bounce_len / PAGE_SIZE; i++) {
		src = io_ctx->bounce_bufs[cur_bounce_page];
		len = min_t(size_t, tot_len, PAGE_SIZE);
		memcpy(dst, src, len);
		dst += len;
		tot_len -= len;
		cur_bounce_page++;
	}

	WARN_ON(tot_len);

out:
	for (i = 0; i < ((io_ctx->start_bounce_len + io_ctx->end_bounce_len) / PAGE_SIZE); i++)
		free_page((unsigned long)io_ctx->bounce_bufs[i]);

}

static int dmabuf_content_load_prepare(struct file *file, struct dma_buf *dmabuf, loff_t file_offs,
				       loff_t buf_offs, loff_t len, struct dmabuf_load_param *param)
{
	struct iosys_map map;
	loff_t buf_end;
	int ret;

	/* We will only write into buf_offs + len, so no need to page-align the length here. */
	if (check_add_overflow(buf_offs, len, &buf_end))
		return -EINVAL;

	if (buf_end > dmabuf->size)
		return -EINVAL;

	ret = dma_buf_begin_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
	if (ret)
		return ret;

	ret = dma_buf_vmap(dmabuf, &map);
	if (ret)
		goto err_end_access;

	if (map.is_iomem) {
		ret = -EINVAL;
		goto err_unmap;
	}

	ret = wrap_io_prepare(file, file_offs, map.vaddr, buf_offs, len, &param->io_ctx);
	if (ret < 0)
		goto err_unmap;

	param->dmabuf = dmabuf;
	param->map = map;
	return 0;

err_unmap:
	dma_buf_vunmap(dmabuf, &map);
err_end_access:
	dma_buf_end_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
	return ret;
}

static void dmabuf_content_load_complete(struct dmabuf_load_param *param)
{
	wrap_io_complete(&param->io_ctx);
	dma_buf_vunmap(param->dmabuf, &param->map);
	dma_buf_end_cpu_access(param->dmabuf, DMA_BIDIRECTIONAL);
}

static int dmabuf_content_load(struct wrap_content *content, struct file *file,
			       loff_t file_offs, loff_t buf_offs, loff_t len)
{
	struct wrap_content_dmabuf *dmabuf_content;
	struct dmabuf_load_param param;
	int ret;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	ret = dmabuf_content_load_prepare(file, dmabuf_content->dmabuf, file_offs, buf_offs, len,
					  &param);
	if (ret < 0)
		return ret;

	ret = wrap_io_read(&param.io_ctx, file);
	dmabuf_content_load_complete(&param);
	return ret;
}

static struct wrap_content *
dmabuf_content_make_writable(struct wrap_content *content, bool writable)
{
	struct wrap_content_dmabuf *dmabuf_content;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	if (writable && !(dmabuf_content->dmabuf->file->f_mode & FMODE_WRITE))
		return ERR_PTR(-EACCES);

	dmabuf_content->writable = writable;

	return content;
}

static bool dmabuf_content_is_writable(struct wrap_content *content)
{
	struct wrap_content_dmabuf *dmabuf_content;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);

	return dmabuf_content->writable && !!(dmabuf_content->dmabuf->file->f_mode & FMODE_WRITE);
}

static loff_t dmabuf_content_llseek(struct wrap_content *content, loff_t offs, int whence)
{
	struct wrap_content_dmabuf *dmabuf_content;
	struct file *file;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	file = dmabuf_content->dmabuf->file;

	return file->f_op->llseek(file, offs, whence);
}

static int dmabuf_content_mmap(struct wrap_content *content,
			       struct vm_area_struct *vma)
{
	struct wrap_content_dmabuf *dmabuf_content;
	int ret;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);

	ret = dma_buf_mmap(dmabuf_content->dmabuf, vma, vma->vm_pgoff);
	if (ret)
		return ret;

	return 0;
}

static void dmabuf_content_free(struct wrap_content *content)
{
	struct wrap_content_dmabuf *dmabuf_content;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	if (dmabuf_content->dmabuf)
		dma_buf_put(dmabuf_content->dmabuf);
	kfree(dmabuf_content);
}

static void dmabuf_content_show_fdinfo(struct wrap_content *content,
				       struct seq_file *m)
{
	struct wrap_content_dmabuf *dmabuf_content;
	struct dma_buf *dmabuf;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	seq_printf(m, "type:\tdmabuf\n");

	dmabuf = dmabuf_content->dmabuf;
	seq_printf(m, "size:\t%zu\n", dmabuf->size);
	/* Don't count the temporary reference taken inside procfs seq_show */
	seq_printf(m, "count:\t%ld\n", file_count(dmabuf->file) - 1);
	seq_printf(m, "exp_name:\t%s\n", dmabuf->exp_name);
	spin_lock(&dmabuf->name_lock);
	if (dmabuf->name)
		seq_printf(m, "name:\t%s\n", dmabuf->name);
	spin_unlock(&dmabuf->name_lock);
}

static int
dmabuf_content_get_mappable(struct wrap_content *content, struct device *dev,
			    union wrapfd_mappable *mappable)
{
	struct wrap_content_dmabuf *dmabuf_content;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	mappable->dmabuf = dmabuf_content->dmabuf;

	return 0;
}

static int dmabuf_content_ioctl(struct wrap_content *content,
				unsigned int cmd, unsigned long arg)
{
	struct wrap_content_dmabuf *dmabuf_content;
	struct file *file;

	dmabuf_content = container_of(content, struct wrap_content_dmabuf,
				      content);
	file = dmabuf_content->dmabuf->file;

	if (in_compat_syscall())
		return file->f_op->compat_ioctl(file, cmd, arg);

	return file->f_op->unlocked_ioctl(file, cmd, arg);

}

static struct wrap_content_operations dmabuf_content_ops = {
	.create_wrap		= dmabuf_content_create_wrap,
	.load			= dmabuf_content_load,
	.llseek			= dmabuf_content_llseek,
	.mmap			= dmabuf_content_mmap,
	.make_writable		= dmabuf_content_make_writable,
	.is_writable		= dmabuf_content_is_writable,
	.free			= dmabuf_content_free,
	.show_fdinfo		= dmabuf_content_show_fdinfo,
	.get_mappable		= dmabuf_content_get_mappable,
	.ioctl			= dmabuf_content_ioctl,
};

static struct wrap_content *alloc_dmabuf_content(struct dma_buf *dmabuf,
						 bool writable)
{
	struct wrap_content_dmabuf *dmabuf_content;

	dmabuf_content = kmalloc(sizeof(*dmabuf_content), GFP_KERNEL);
	if (!dmabuf_content)
		return ERR_PTR(-ENOMEM);

	get_dma_buf(dmabuf);
	dmabuf_content->dmabuf = dmabuf;
	dmabuf_content->writable = writable;
	dmabuf_content->content.ops = &dmabuf_content_ops;

	return &dmabuf_content->content;
}

/* Generic wrapfd */
struct wrap_owner {
	struct task_struct *task;
	struct device *dev;
};

struct wrap_ctx_mapping {
	refcount_t refcnt;
	struct file *file;
	struct wrap_ctx *ctx;
	const struct vm_operations_struct *content_vm_ops;
	struct vm_operations_struct vm_ops;
};

struct wrap_ctx {
	struct wrap_content *content;
	spinlock_t lock; /* protects all fields below */
	struct wrap_owner owner;
	bool allow_guests;
	unsigned long map_count;
	unsigned long use_count;
	bool unusable;
};

static struct wrap_ctx *create_wrap_ctx(void)
{
	struct wrap_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	spin_lock_init(&ctx->lock);

	return ctx;
}

static inline void reset_owner(struct wrap_ctx *ctx)
{
	assert_spin_locked(&ctx->lock);
	put_task_struct(ctx->owner.task);
	ctx->owner.task = NULL;
}

static inline struct task_struct *get_valid_owner(struct wrap_ctx *ctx)
{
	assert_spin_locked(&ctx->lock);

	if (!ctx->owner.task)
		return NULL;

	/* If task exits (passed exit_mm), reset the owner. */
	if (!ctx->owner.task->mm)
		reset_owner(ctx);

	return ctx->owner.task;
}

static inline bool has_owner(struct wrap_ctx *ctx)
{
	return get_valid_owner(ctx) || ctx->owner.dev;
}

static inline bool is_owner_task(struct wrap_ctx *ctx,
				 struct task_struct *task)
{
	struct task_struct *owner_task = get_valid_owner(ctx);

	return owner_task && task->group_leader == owner_task;
}

static inline bool is_owner_dev(struct wrap_ctx *ctx,
				struct device *dev)
{
	assert_spin_locked(&ctx->lock);
	return ctx->owner.dev == dev;
}

static inline int publish_wrap(struct wrap_ctx *ctx,
			       struct wrap_content *content)
{
	int ret;

	ctx->content = content;
	ret = content->ops->create_wrap(content, ctx);
	/* Set FD_CLOEXEC flag for wrapfd the same as its content */
	if (ret >= 0)
		set_close_on_exec(ret, content->close_on_exec ? 1 : 0);

	return ret;
}

static bool context_use(struct wrap_ctx *ctx)
{
	assert_spin_locked(&ctx->lock);
	if (ctx->unusable)
		return false;
	ctx->use_count++;
	return true;
}

static void context_unuse(struct wrap_ctx *ctx)
{
	assert_spin_locked(&ctx->lock);
	if (WARN_ON(ctx->unusable))
		return;
	ctx->use_count--;
}

static int can_modify(struct wrap_ctx *ctx, struct task_struct *task,
		      bool check_content)
{
	assert_spin_locked(&ctx->lock);
	if (!is_owner_task(ctx, task))
		return -EBUSY;

	if (ctx->map_count > 0 || ctx->use_count > 0)
		return -EINVAL;

	if (check_content && !ctx->content)
		return -ENOENT;

	return 0;
}

static int block_usage(struct wrap_ctx *ctx)
{
	int ret;

	assert_spin_locked(&ctx->lock);
	ret = can_modify(ctx, current, true);
	if (ret)
		return ret;

	/*
	 * The task is the owner, the content can't be modified by other
	 * processes but racing threads of the owner process can still
	 * modify it. Use unusable to prevent that.
	 */
	if (ctx->unusable)
		return -EAGAIN;

	ctx->unusable = true;

	return 0;
}

static void unblock_usage(struct wrap_ctx *ctx)
{
	assert_spin_locked(&ctx->lock);
	if (WARN_ON(!ctx->unusable))
		return;

	ctx->unusable = false;
}

static void wrap_vm_open(struct vm_area_struct *vma)
{
	struct wrap_ctx_mapping *mapping;

	mapping = container_of(vma->vm_ops, struct wrap_ctx_mapping, vm_ops);
	if (mapping->content_vm_ops && mapping->content_vm_ops->open)
		mapping->content_vm_ops->open(vma);

	spin_lock(&mapping->ctx->lock);
	mapping->ctx->map_count++;
	refcount_inc(&mapping->refcnt);
	spin_unlock(&mapping->ctx->lock);
}

static void wrap_vm_close(struct vm_area_struct *vma)
{
	struct wrap_ctx_mapping *mapping;
	struct file *file = NULL;
	struct wrap_ctx *ctx;

	mapping = container_of(vma->vm_ops, struct wrap_ctx_mapping, vm_ops);
	if (mapping->content_vm_ops && mapping->content_vm_ops->close)
		mapping->content_vm_ops->close(vma);

	ctx = mapping->ctx;
	spin_lock(&ctx->lock);
	if (ctx->map_count > 0)
		ctx->map_count--;
	else
		pr_warn("wrapfd map count underflow\n");
	if (refcount_dec_and_test(&mapping->refcnt)) {
		if (mapping->file)
			file = mapping->file;
		kfree(mapping);
	}
	spin_unlock(&ctx->lock);
	if (file)
		fput(file);
}

static int wrap_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct wrap_ctx *ctx = file->private_data;
	struct wrap_ctx_mapping *mapping;
	struct wrap_content *content;
	bool make_rdonly = false;
	int ret = 0;

	spin_lock(&ctx->lock);
	if (!ctx->allow_guests && has_owner(ctx) &&
	    !is_owner_task(ctx, current)) {
		ret = -EBUSY;
		goto unlock;
	}

	/*
	 * If usage is blocked, the content is being rewrapped or emptied.
	 * Treat this as if the wrap is already empty.
	 */
	if (!context_use(ctx)) {
		ret = -ENOENT;
		goto unlock;
	}

	content = ctx->content;
	if (!content) {
		ret = -ENOENT;
		goto put_ctx;
	}

	/* Handle read-only content */
	if (content->ops->is_writable &&
	    !content->ops->is_writable(content)) {
		if (vma->vm_flags & VM_WRITE) {
			ret = -EACCES;
			goto put_ctx;
		}
		make_rdonly = !!(vma->vm_flags & VM_MAYWRITE);
	}

	if (content->ops->mmap_prepare) {
		ret = content->ops->mmap_prepare(content, vma);
		if (ret) {
			ret = -EINVAL;
			goto put_ctx;
		}
	}
	/*
	 * Increased map_count prevents changes in the
	 * ownership, rewrapping or emptying the content.
	 * Content is stable.
	 */
	ctx->map_count++;
put_ctx:
	context_unuse(ctx);
unlock:
	spin_unlock(&ctx->lock);

	if (ret)
		goto err;

	/* If we reached here then ctx->map_count has been incremented */
	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		ret = -ENOMEM;
		goto err_dec;
	}

	ret = content->ops->mmap(content, vma);
	if (ret)
		goto err_free_mapping;

	if (make_rdonly) {
		/*
		 * content->ops->mmap should not be mapping read-only content
		 * as writable. Either content->ops->is_writable reports
		 * incorrect value or content->ops->mmap is misbehaving.
		 */
		if (unlikely(vma->vm_flags & VM_WRITE)) {
			pr_warn("wrapfd read-only content was mapped as writable\n");
			ret = -EACCES;
			goto err_free_mapping;
		}
		vm_flags_clear(vma, VM_MAYWRITE);
	}

	spin_lock(&ctx->lock);
	mapping->content_vm_ops = vma->vm_ops;
	if (vma->vm_ops)
		mapping->vm_ops = *vma->vm_ops;
	mapping->vm_ops.open = wrap_vm_open;
	mapping->vm_ops.close = wrap_vm_close;
	vma->vm_ops = &mapping->vm_ops;
	mapping->ctx = ctx;
	refcount_set(&mapping->refcnt, 1);
	/*
	 * content->ops->mmap might replace original vma->vm_file and vma will
	 * lose its association with the wrapfd file. In such cases we need to
	 * take a reference on the wrapfd file and store it to drop the
	 * refcount mapping is removed.
	 */
	if (vma->vm_file != file)
		mapping->file = get_file(file);
	spin_unlock(&ctx->lock);

	return 0;
err_free_mapping:
	kfree(mapping);
err_dec:
	spin_lock(&ctx->lock);
	ctx->map_count--;
	spin_unlock(&ctx->lock);
err:
	return ret;
}

static loff_t wrap_llseek(struct file *file, loff_t offs, int whence)
{
	struct wrap_ctx *ctx = file->private_data;
	struct wrap_content *content;
	loff_t ret = 0;

	spin_lock(&ctx->lock);
	/*
	 * If usage is blocked, the content is being rewrapped or emptied.
	 * Treat this as if the wrap is already empty.
	 */
	if (!context_use(ctx)) {
		ret = -ENOENT;
		goto unlock;
	}

	content = ctx->content;
	if (!content) {
		ret = -ENOENT;
		goto unlock;
	}

	if (!content->ops->llseek) {
		ret = -ESPIPE;
		goto unlock;
	}
unlock:
	spin_unlock(&ctx->lock);

	if (ret)
		return ret;

	ret = content->ops->llseek(content, offs, whence);

	spin_lock(&ctx->lock);
	context_unuse(ctx);
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_release(struct inode *ignored, struct file *file)
{
	struct wrap_ctx *ctx = file->private_data;

	if (ctx->content)
		ctx->content->ops->free(ctx->content);
	kfree(ctx);

	return 0;
}

static int get_wrap_state(struct wrap_ctx *ctx,
			  struct wrapfd_get_state __user *user_wrapfd_get_state)
{
	struct wrapfd_get_state wrapfd_get_state;

	if (copy_from_user(&wrapfd_get_state, user_wrapfd_get_state,
			   sizeof(wrapfd_get_state)))
		return -EFAULT;

	if (wrapfd_get_state.reserved || wrapfd_get_state.pad)
		return -EINVAL;

	spin_lock(&ctx->lock);
	/*
	 * If usage is blocked, the content is being rewrapped or emptied.
	 * Treat this as if the wrap is already empty.
	 */
	if (ctx->content && context_use(ctx)) {
		if (ctx->content->ops->is_writable(ctx->content))
			wrapfd_get_state.state = WRAPFD_CONTENT_RDWR;
		else
			wrapfd_get_state.state = WRAPFD_CONTENT_RDONLY;
		context_unuse(ctx);
	} else {
		wrapfd_get_state.state = WRAPFD_CONTENT_EMPTY;
	}
	spin_unlock(&ctx->lock);

	if (copy_to_user(user_wrapfd_get_state, &wrapfd_get_state,
			 sizeof(wrapfd_get_state)))
		return -EFAULT;

	return 0;
}

static int wrap_file_acquire_ownership(struct wrap_ctx *ctx)
{
	int ret = 0;

	spin_lock(&ctx->lock);

	if (is_owner_task(ctx, current))
		goto unlock;

	if (has_owner(ctx)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (ctx->map_count > 0 || ctx->use_count > 0) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!ctx->content) {
		ret = -ENOENT;
		goto unlock;
	}

	ctx->owner.task = get_task_struct(current->group_leader);
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_file_release_ownership(struct wrap_ctx *ctx)
{
	int ret = 0;

	spin_lock(&ctx->lock);

	ret = can_modify(ctx, current, false);
	if (ret)
		goto unlock;

	reset_owner(ctx);
	ctx->allow_guests = false;
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_file_load(struct wrap_ctx *ctx,
			  struct wrapfd_load __user *user_wrapfd_load)
{
	struct wrapfd_load wrapfd_load;
	struct wrap_content *content;
	struct file *file;
	loff_t file_offs;
	loff_t buf_offs;
	loff_t len;
	loff_t end;
	int ret = 0;

	if (copy_from_user(&wrapfd_load, user_wrapfd_load,
			   sizeof(wrapfd_load)))
		return -EFAULT;

	file_offs = wrapfd_load.file_offs;
	buf_offs = wrapfd_load.buf_offs;
	len = wrapfd_load.len;

	if (file_offs < 0 || buf_offs < 0 || len < 0)
		return -EINVAL;

	if (wrapfd_load.reserved || wrapfd_load.pad)
		return -EINVAL;

	file = fget(wrapfd_load.fd);
	if (!file)
		return -EBADF;

	if (!(file->f_mode & FMODE_READ)) {
		ret = -EBADF;
		goto put_file;
	}

	if (!file->f_op->read_iter) {
		ret = -EINVAL;
		goto put_file;
	}

	if (!(file->f_mode & FMODE_CAN_READ)) {
		ret = -EINVAL;
		goto put_file;
	}

	if (!(file->f_mode & FMODE_CAN_ODIRECT)) {
		ret = -EINVAL;
		goto put_file;
	}

	if (check_add_overflow(file_offs, len, &end)) {
		ret = -EINVAL;
		goto put_file;
	}

	if (end > i_size_read(file_inode(file))) {
		ret = -EINVAL;
		goto put_file;
	}

	spin_lock(&ctx->lock);
	ret = context_use(ctx) ? 0 : -ENOENT;
	spin_unlock(&ctx->lock);

	if (ret)
		goto put_file;

	content = ctx->content;
	if (!content) {
		ret = -ENOENT;
		goto put_ctx;
	}

	if (content->ops->is_writable && !content->ops->is_writable(content)) {
		ret = -EACCES;
		goto put_ctx;
	}

	ret = content->ops->load(content, file, file_offs, buf_offs, len);
put_ctx:
	spin_lock(&ctx->lock);
	context_unuse(ctx);
	spin_unlock(&ctx->lock);
put_file:
	fput(file);

	return ret;
}

static int wrap_file_rewrap(struct wrap_ctx *ctx,
			    struct wrapfd_rewrap __user *user_wrapfd_rewrap)
{
	struct wrapfd_rewrap wrapfd_rewrap;
	struct wrap_content *new_content;
	struct wrap_content *content;
	struct wrap_ctx *new_ctx;
	int ret = 0;

	if (copy_from_user(&wrapfd_rewrap, user_wrapfd_rewrap,
			   sizeof(wrapfd_rewrap)))
		return -EFAULT;

	if (wrapfd_rewrap.prot & ~(PROT_WRITE | PROT_READ))
		return -EINVAL;

	if (wrapfd_rewrap.reserved || wrapfd_rewrap.pad)
		return -EINVAL;

	spin_lock(&ctx->lock);
	ret = block_usage(ctx);
	if (!ret) {
		content = ctx->content;
		ctx->content = NULL;
	}
	spin_unlock(&ctx->lock);

	if (ret)
		goto out;

	new_content = content->ops->make_writable(content,
				(wrapfd_rewrap.prot & PROT_WRITE) != 0);
	if (IS_ERR(new_content)) {
		ret = PTR_ERR(new_content);
		goto restore_content;
	}
	new_content->close_on_exec = content->close_on_exec;

	new_ctx = create_wrap_ctx();
	if (!new_ctx) {
		ret = -ENOMEM;
		goto free_new_content;
	}

	ret = publish_wrap(new_ctx, new_content);
	if (ret < 0)
		goto free_new_ctx;

	if (new_content != content)
		content->ops->free(content);

	spin_lock(&ctx->lock);
	unblock_usage(ctx);
	spin_unlock(&ctx->lock);

	return ret;

free_new_ctx:
	kfree(new_ctx);
free_new_content:
	if (new_content != content)
		new_content->ops->free(new_content);
restore_content:
	/*
	 * Restore original wrap. We are the owner and the wrap
	 * is empty, so it could not have changed from under us.
	 */
	spin_lock(&ctx->lock);
	ctx->content = content;
	unblock_usage(ctx);
	spin_unlock(&ctx->lock);
out:
	return ret;
}

static int wrap_file_empty(struct wrap_ctx *ctx)
{
	struct wrap_content *content;
	int ret = 0;

	spin_lock(&ctx->lock);

	ret = block_usage(ctx);
	if (ret)
		goto unlock;

	content = ctx->content;
	ctx->content = NULL;
	unblock_usage(ctx);
unlock:
	spin_unlock(&ctx->lock);

	if (!ret)
		content->ops->free(content);

	return ret;
}

static int wrap_file_allow_guests(struct wrap_ctx *ctx, bool allow)
{
	int ret = 0;

	spin_lock(&ctx->lock);

	ret = can_modify(ctx, current, true);
	if (ret)
		goto unlock;

	ctx->allow_guests = allow;
unlock:
	spin_unlock(&ctx->lock);

	return ret;
}

static int wrap_file_ioctl(struct wrap_ctx *ctx,
			   unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	spin_lock(&ctx->lock);
	if (!ctx->allow_guests && has_owner(ctx) &&
	    !is_owner_task(ctx, current)) {
		ret = -EBUSY;
		goto unlock;
	}

	/*
	 * If usage is blocked, the content is being rewrapped or emptied.
	 * Treat this as if the wrap is already empty.
	 */
	if (!context_use(ctx)) {
		ret = -ENOENT;
		goto unlock;
	}

	if (!ctx->content) {
		context_unuse(ctx);
		ret = -ENOENT;
		goto unlock;
	}

	if (!ctx->content->ops->ioctl) {
		context_unuse(ctx);
		ret = -ENOIOCTLCMD;
		goto unlock;
	}
unlock:
	spin_unlock(&ctx->lock);

	if (ret)
		return ret;

	ret = ctx->content->ops->ioctl(ctx->content, cmd, arg);

	spin_lock(&ctx->lock);
	context_unuse(ctx);
	spin_unlock(&ctx->lock);

	return ret;
}

static long wrap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wrap_ctx *ctx = file->private_data;
	long ret;

	switch (cmd) {
	case WRAPFD_DEV_IOC_GET_STATE:
		ret = get_wrap_state(ctx,
				     (struct wrapfd_get_state __user *)arg);
		break;
	case WRAPFD_DEV_IOC_ACQUIRE_OWNERSHIP:
		ret = wrap_file_acquire_ownership(ctx);
		break;
	case WRAPFD_DEV_IOC_RELEASE_OWNERSHIP:
		ret = wrap_file_release_ownership(ctx);
		break;
	case WRAPFD_DEV_IOC_LOAD:
		ret = wrap_file_load(ctx, (struct wrapfd_load __user *)arg);
		break;
	case WRAPFD_DEV_IOC_REWRAP:
		ret = wrap_file_rewrap(ctx,
				       (struct wrapfd_rewrap __user *)arg);
		break;
	case WRAPFD_DEV_IOC_EMPTY:
		ret = wrap_file_empty(ctx);
		break;
	case WRAPFD_DEV_IOC_ALLOW_GUESTS:
		ret = wrap_file_allow_guests(ctx, true);
		break;
	case WRAPFD_DEV_IOC_PROHIBIT_GUESTS:
		ret = wrap_file_allow_guests(ctx, false);
		break;
	default:
		ret = wrap_file_ioctl(ctx, cmd, arg);
		break;
	}

	return ret;
}

static long wrap_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These commands are associated with pointers as arguments, so use compat_ptr() on them. */
	switch (cmd) {
	case WRAPFD_DEV_IOC_GET_STATE:
	case WRAPFD_DEV_IOC_LOAD:
	case WRAPFD_DEV_IOC_REWRAP:
		arg = (unsigned long)compat_ptr(arg);
		break;
	}

	return wrap_ioctl(file, cmd, arg);
}

#ifdef CONFIG_PROC_FS
static void wrap_show_fdinfo(struct seq_file *m, struct file *file)
{
	struct wrap_ctx *ctx = file->private_data;
	struct task_struct *owner_task;

	spin_lock(&ctx->lock);
	owner_task = get_valid_owner(ctx);
	if (owner_task) {
		seq_printf(m, "owner:\t%d\n", owner_task->pid);
	} else {
		if (ctx->owner.dev)
			seq_printf(m, "owner:\t<device>\n");
		else
			seq_printf(m, "owner:\t<none>\n");
	}
	seq_printf(m, "guests:\t%s\n", ctx->allow_guests ? "yes" : "no");
	seq_printf(m, "maps:\t%lu\n", ctx->map_count);
	seq_printf(m, "empty:\t%s\n", ctx->content ? "no" : "yes");
	if (ctx->content) {
		struct wrap_content *content = ctx->content;

		seq_printf(m, "rdonly:\t%s\n",
			   content->ops->is_writable(content) ? "no" : "yes");
		content->ops->show_fdinfo(content, m);
	}
	spin_unlock(&ctx->lock);
}
#endif

bool is_wrapfd_vma(struct vm_area_struct *vma)
{
	return vma && vma->vm_ops && (vma->vm_ops->open == wrap_vm_open);
}

int wrapfd_get_mappable(struct file *file, struct device *dev,
			union wrapfd_mappable *mappable)
{
	struct wrap_ctx *ctx;
	int ret;

	if (WARN_ON(!dev))
		return -ENODEV;

	if (file->f_op != &wrap_fops)
		return -EBADF;

	ctx = file->private_data;

	spin_lock(&ctx->lock);

	if (has_owner(ctx) && !is_owner_dev(ctx, dev)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (ctx->map_count > 0 || ctx->use_count > 0) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!ctx->content) {
		ret = -ENOENT;
		goto unlock;
	}

	ctx->owner.dev = dev;
	/* Device is the owner, context can't change from under us. */
	ret = 0;
unlock:
	spin_unlock(&ctx->lock);

	if (!ret) {
		ret = ctx->content->ops->get_mappable(ctx->content, dev,
						      mappable);
		if (ret) {
			spin_lock(&ctx->lock);
			ctx->owner.dev = NULL;
			spin_unlock(&ctx->lock);
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wrapfd_get_mappable);

int wrapfd_put_mappable(struct file *file, struct device *dev,
			union wrapfd_mappable *mappable)
{
	struct wrap_ctx *ctx;
	int ret;

	if (WARN_ON(!dev))
		return -ENODEV;

	if (file->f_op != &wrap_fops)
		return -EBADF;

	ctx = file->private_data;

	spin_lock(&ctx->lock);

	if (!is_owner_dev(ctx, dev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ctx->owner.dev = NULL;
	ret = 0;
unlock:
	spin_unlock(&ctx->lock);

	if (!ret && ctx->content->ops->put_mappable)
		ctx->content->ops->put_mappable(ctx->content, mappable);

	return ret;
}
EXPORT_SYMBOL_GPL(wrapfd_put_mappable);

static const struct file_operations wrap_fops = {
	.owner		= THIS_MODULE,
	.llseek		= wrap_llseek,
	.mmap		= wrap_mmap,
	.release	= wrap_release,
	.unlocked_ioctl	= wrap_ioctl,
	.compat_ioctl	= wrap_compat_ioctl,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= wrap_show_fdinfo,
#endif
};

static struct wrap_content *create_content_for(int fd, unsigned long prot)
{
	bool is_file_writable, writable;
	struct wrap_content *content;
	struct dma_buf *dmabuf;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return ERR_PTR(PTR_ERR(dmabuf));

	writable = !!(prot & PROT_WRITE);
	is_file_writable = !!(dmabuf->file->f_mode & FMODE_WRITE);
	if (writable && !is_file_writable)
		content = ERR_PTR(-EACCES);
	else
		content = alloc_dmabuf_content(dmabuf, writable);
	dma_buf_put(dmabuf);

	return content;
}

static int wrap_file(struct wrap_ctx *ctx,
		     struct wrapfd_wrap __user *user_wrapfd_wrap)
{
	struct wrapfd_wrap wrapfd_wrap;
	struct wrap_content *content;
	int wrapfd;

	if (copy_from_user(&wrapfd_wrap, user_wrapfd_wrap,
			   sizeof(wrapfd_wrap)))
		return -EFAULT;

	if (wrapfd_wrap.prot & ~(PROT_WRITE | PROT_READ))
		return -EINVAL;

	if (wrapfd_wrap.reserved)
		return -EINVAL;

	content = create_content_for(wrapfd_wrap.fd, wrapfd_wrap.prot);
	if (IS_ERR(content))
		return PTR_ERR(content);

	content->close_on_exec = get_close_on_exec(wrapfd_wrap.fd);
	wrapfd = publish_wrap(ctx, content);
	if (wrapfd < 0) {
		ctx->content = NULL;
		content->ops->free(content);
	}

	return wrapfd;
}

static long wrapfd_dev_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct wrap_ctx *ctx;
	int ret;

	VM_WARN_ON_ONCE(!current->mm);

	switch (cmd) {
	case WRAPFD_DEV_IOC_WRAP:
		ctx = create_wrap_ctx();
		if (!ctx)
			return -ENOMEM;

		ret = wrap_file(ctx, (struct wrapfd_wrap __user *)arg);
		if (ret < 0)
			kfree(ctx);

		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static const struct file_operations wrapfd_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = wrapfd_dev_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice wrapfd_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wrapfd",
	.fops = &wrapfd_dev_fops,
};

static int __init wrapfd_init(void)
{
	int ret;

	ret = misc_register(&wrapfd_misc);
	if (ret) {
		pr_err("failed to register misc device!\n");
		return ret;
	}

	return 0;
}
device_initcall(wrapfd_init);
