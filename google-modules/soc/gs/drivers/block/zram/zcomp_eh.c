// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/blkdev.h>

#include "zram_drv.h"
#include "zcomp_eh.h"

static int zcomp_flush(struct zcomp_eh *zcomp_eh)
{
	int err = 0;
	LIST_HEAD(req_list);

	spin_lock(&zcomp_eh->request_lock);
	list_splice_init(&zcomp_eh->request_list, &req_list);
	zcomp_eh->pend_request = 0;
	spin_unlock(&zcomp_eh->request_lock);

	while (!list_empty(&req_list)) {
		struct zcomp_cookie *cookie;

		cookie = list_last_entry(&req_list, struct zcomp_cookie, list);
		list_del(&cookie->list);
		eh_compress_page(zcomp_eh->eh_dev, cookie->page, cookie);
	}

	return err;
}

static void zcomp_unplug(struct blk_plug_cb *cb, bool from_schedule)
{
	zcomp_flush((struct zcomp_eh *)(cb->data));
	kfree(cb);
}

/*
 * If the comp is plugged, append the cookie to request list and return true
 * otherwise, return false.
 */
static void zcomp_append_request(struct zcomp_eh *zcomp_eh,
				 struct zcomp_cookie *cookie)
{
	spin_lock(&zcomp_eh->request_lock);
	list_add(&cookie->list, &zcomp_eh->request_list);
	zcomp_eh->pend_request++;
	spin_unlock(&zcomp_eh->request_lock);
}

static unsigned long nr_pend_request(struct zcomp_eh *zcomp_eh)
{
	unsigned long ret;

	spin_lock(&zcomp_eh->request_lock);
	ret = zcomp_eh->pend_request;
	spin_unlock(&zcomp_eh->request_lock);

	return ret;
}

/*
 * The caller needs to hold cookie_pool.lock
 */
static bool refill_zcomp_cookie(struct zcomp_eh *zcomp_eh)
{
	int i;
	struct zcomp_cookie *cookie;

	WARN_ON(zcomp_eh->cookie_pool.count != 0);

	for (i = 0; i < BATCH_ZCOMP_REQUEST; i++) {
		cookie = kmalloc(sizeof(struct zcomp_cookie), GFP_ATOMIC);
		if (!cookie)
			break;
		list_add(&cookie->list, &zcomp_eh->cookie_pool.head);
		zcomp_eh->cookie_pool.count++;
	}

	return !zcomp_eh->cookie_pool.count;
}

static struct zcomp_cookie *alloc_zcomp_cookie(struct zcomp_eh *zcomp_eh)
{
	struct zcomp_cookie *cookie = NULL;

	WARN_ON(in_interrupt());

	spin_lock(&zcomp_eh->cookie_pool.lock);
	if (list_empty(&zcomp_eh->cookie_pool.head)) {
		if (refill_zcomp_cookie(zcomp_eh))
			goto out;
	}

	cookie = list_first_entry(&zcomp_eh->cookie_pool.head,
					struct zcomp_cookie, list);
	list_del(&cookie->list);
	zcomp_eh->cookie_pool.count--;
out:
	spin_unlock(&zcomp_eh->cookie_pool.lock);

	return cookie;
}

static void free_zcomp_cookie(struct zcomp_eh *zcomp_eh, struct zcomp_cookie *cookie)
{
	spin_lock(&zcomp_eh->cookie_pool.lock);
	list_add(&cookie->list, &zcomp_eh->cookie_pool.head);
	zcomp_eh->cookie_pool.count++;

	if (zcomp_eh->cookie_pool.count >= BATCH_ZCOMP_REQUEST * 2) {
		int i;

		for (i = 0; i < BATCH_ZCOMP_REQUEST; i++) {
			cookie = list_last_entry(&zcomp_eh->cookie_pool.head,
						struct zcomp_cookie, list);
			list_del(&cookie->list);
			kfree(cookie);
			zcomp_eh->cookie_pool.count--;
		}
	}
	spin_unlock(&zcomp_eh->cookie_pool.lock);
}

static void init_zcomp_cookie_pool(struct zcomp_eh *zcomp_eh)
{
	INIT_LIST_HEAD(&zcomp_eh->cookie_pool.head);
	spin_lock_init(&zcomp_eh->cookie_pool.lock);
	zcomp_eh->cookie_pool.count = 0;
}

static void destroy_zcomp_cookie_pool(struct zcomp_eh *zcomp_eh)
{
	struct zcomp_cookie *cookie;

	spin_lock(&zcomp_eh->cookie_pool.lock);
	while (!list_empty(&zcomp_eh->cookie_pool.head)) {
		cookie = list_first_entry(&zcomp_eh->cookie_pool.head,
					struct zcomp_cookie, list);
		list_del(&cookie->list);
		kfree(cookie);
		zcomp_eh->cookie_pool.count--;
	}
	spin_unlock(&zcomp_eh->cookie_pool.lock);
}

static void zcomp_eh_compress_done(int error, void *buffer,
				   unsigned int size, void *priv)
{
	struct zcomp_cookie *cookie = priv;
	struct zram *zram = cookie->zram;
	struct zcomp_eh *zcomp_eh = cookie->zcomp_eh;
	u32 index = cookie->index;
	struct bio *bio = cookie->bio;
	struct page *page = cookie->page;
	bool is_write = bio_op(bio) == REQ_OP_WRITE;

	if (!error)
		error = zcomp_copy_buffer(buffer, size, zram, page, index,
					  zcomp_eh->prio);

	if (unlikely(error)) {
		if (is_write)
			atomic64_inc(&zram->stats.failed_writes);
		else
			atomic64_inc(&zram->stats.failed_reads);
		bio_io_error(bio);
	} else
		bio_endio(bio);
	free_zcomp_cookie(zcomp_eh, cookie);
}

static int zcomp_eh_compress(struct zcomp *comp, u32 index, struct page *page,
				struct bio *bio)
{
	struct zcomp_eh *zcomp_eh = comp->private;
	struct zcomp_cookie *cookie;

	cookie = alloc_zcomp_cookie(zcomp_eh);
	if (!cookie)
		return -ENOMEM;

	cookie->zram = comp->zram;
	cookie->zcomp_eh = zcomp_eh;
	cookie->index = index;
	cookie->page = page;
	cookie->bio = bio;

	bio_inc_remaining(bio);

	if (blk_check_plugged(zcomp_unplug, zcomp_eh, sizeof(struct blk_plug_cb)) &&
	    nr_pend_request(zcomp_eh) < ZCOMP_BLK_MAX_REQUEST_COUNT) {
		zcomp_append_request(zcomp_eh, cookie);
		return 0;
	}

	zcomp_flush(zcomp_eh);
	return eh_compress_page(zcomp_eh->eh_dev, page, cookie);
}

static void zcomp_eh_prepare_decompress(struct zcomp *comp)
{
	struct zcomp_eh *zcomp_eh = comp->private;

	eh_prepare_decompress(zcomp_eh->eh_dev);
}

static int zcomp_eh_decompress(struct zcomp *comp, void *src,
			unsigned int src_len, struct page *page)
{
	struct zcomp_eh *zcomp_eh = comp->private;

	return eh_decompress_page(zcomp_eh->eh_dev, src, src_len, page);
}

static void zcomp_eh_destroy(struct zcomp *comp)
{
	struct zcomp_eh *zcomp_eh = comp->private;

	eh_destroy(zcomp_eh->eh_dev);
	destroy_zcomp_cookie_pool(zcomp_eh);
	kfree(zcomp_eh);
	module_put(THIS_MODULE);
}

static int zcomp_eh_create(struct zcomp *comp, const char *name)
{
	struct zcomp_eh *zcomp_eh;

	zcomp_eh = kmalloc(sizeof(*zcomp_eh), GFP_KERNEL);
	if (!zcomp_eh)
		return -ENOMEM;

	zcomp_eh->eh_dev = eh_create(zcomp_eh_compress_done);
	if (IS_ERR(zcomp_eh->eh_dev)) {
		kfree(zcomp_eh);
		return -ENODEV;
	}

	init_zcomp_cookie_pool(zcomp_eh);
	INIT_LIST_HEAD(&zcomp_eh->request_list);
	spin_lock_init(&zcomp_eh->request_lock);
	zcomp_eh->pend_request = 0;
	zcomp_eh->prio = comp->prio;

	comp->private = zcomp_eh;
	__module_get(THIS_MODULE);

	return 0;
}

const struct zcomp_operation zcomp_eh_op = {
	.create = zcomp_eh_create,
	.destroy = zcomp_eh_destroy,
	.compress_async = zcomp_eh_compress,
	.prepare_decompress = zcomp_eh_prepare_decompress,
	.decompress = zcomp_eh_decompress,
};

static int __init zcomp_eh_init(void)
{
	return zcomp_register("lz77eh", &zcomp_eh_op);
}

static void __exit zcomp_eh_exit(void)
{
	zcomp_unregister("lz77eh");
}

module_init(zcomp_eh_init);
module_exit(zcomp_eh_exit);
MODULE_LICENSE("GPL");
