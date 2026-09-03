// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/highmem.h>
#include <linux/cpuhotplug.h>
#include <linux/local_lock.h>
#include <linux/vmalloc.h>

#include "zcomp.h"
#include "zcomp_cpu.h"

#include "backend_lzo.h"
#include "backend_lzorle.h"
#include "backend_lz4.h"
#include "backend_lz4hc.h"
#include "backend_zstd.h"
#include "backend_deflate.h"
#include "backend_842.h"

static const struct zcomp_cpu_ops *backends[] = {
#if IS_ENABLED(CONFIG_ZRAM_GS_BACKEND_LZO)
	&backend_lzorle,
	&backend_lzo,
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_BACKEND_LZ4)
	&backend_lz4,
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_BACKEND_LZ4HC)
	&backend_lz4hc,
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_BACKEND_ZSTD)
	&backend_zstd,
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_BACKEND_DEFLATE)
	&backend_deflate,
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_BACKEND_842)
	&backend_842,
#endif
	NULL
};

static const struct zcomp_cpu_ops *lookup_backend_ops(const char *comp)
{
	int i = 0;

	while (backends[i]) {
		if (sysfs_streq(comp, backends[i]->name))
			break;
		i++;
	}
	return backends[i];
}

static struct zcomp_strm *zcomp_stream_get(struct zcomp *comp)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;
	struct zcomp_strm *zstrm = zcomp_cpu->zstrm;

	local_lock(&zstrm->lock);
	return this_cpu_ptr(zstrm);
}

static void zcomp_stream_put(struct zcomp *comp)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;
	struct zcomp_strm *zstrm = zcomp_cpu->zstrm;

	local_unlock(&zstrm->lock);
}

static int zcomp_cpu_recompress(struct zcomp *comp, u32 index, struct page *page,
				u32 prio, u32 threshold)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;
	void *src = kmap_local_page(page);
	struct zcomp_strm *zstrm = zcomp_stream_get(comp);
	struct zcomp_req req = {
		.src = src,
		.dst = zstrm->buffer,
		.src_len = PAGE_SIZE,
		.dst_len = 2 * PAGE_SIZE,
	};
	int ret;

	ret = zcomp_cpu->ops->compress(comp->params, &zstrm->ctx, &req);
	kunmap_local(src);

	if (unlikely(ret)) {
		zcomp_stream_put(comp);
		pr_err("Recompression failed! err=%d\n", ret);
		return ret;
	}

	ret = zcomp_recompress_copy_buffer(zstrm->buffer, req.dst_len, comp->zram,
					   index, prio, threshold);
	zcomp_stream_put(comp);

	return ret;
}

static int zcomp_cpu_compress(struct zcomp *comp, u32 index, struct page *page,
			      struct bio *bio)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;
	int ret;
	void *src = kmap_local_page(page);
	struct zcomp_strm *zstrm = zcomp_stream_get(comp);
	struct zcomp_req req = {
		.src = src,
		.dst = zstrm->buffer,
		.src_len = PAGE_SIZE,
		.dst_len = 2 * PAGE_SIZE,
	};

	ret = zcomp_cpu->ops->compress(comp->params, &zstrm->ctx, &req);
	kunmap_local(src);

	if (unlikely(ret)) {
		zcomp_stream_put(comp);
		pr_err("Compression failed! err=%d\n", ret);
		return ret;
	}

	ret = zcomp_copy_buffer(zstrm->buffer, req.dst_len, comp->zram, page,
				index, zcomp_cpu->prio);
	zcomp_stream_put(comp);

	return ret;
}

static int zcomp_cpu_decompress(struct zcomp *comp, void *src,
				unsigned int src_len, struct page *page)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;
	int ret;
	void *dst = kmap_local_page(page);
	struct zcomp_strm *zstrm = zcomp_stream_get(comp);
	struct zcomp_req req = {
		.src = src,
		.dst = dst,
		.src_len = src_len,
		.dst_len = PAGE_SIZE,
	};

	ret = zcomp_cpu->ops->decompress(comp->params, &zstrm->ctx, &req);

	zcomp_stream_put(comp);
	kunmap_local(dst);

	return ret;
}

static void zcomp_strm_free(struct zcomp *comp, struct zcomp_strm *zstrm)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;

	zcomp_cpu->ops->destroy_ctx(&zstrm->ctx);
	vfree(zstrm->buffer);
	zstrm->buffer = NULL;
}

static int zcomp_strm_init(struct zcomp *comp, struct zcomp_strm *zstrm)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;
	int ret;

	ret = zcomp_cpu->ops->create_ctx(comp->params, &zstrm->ctx);
	if (ret)
		goto err_out;

	/*
	 * allocate 2 pages. 1 for compressed data, plus 1 extra for the
	 * case when compressed size is larger than the original one
	 */
	zstrm->buffer = vzalloc(2 * PAGE_SIZE);
	if (!zstrm->buffer) {
		ret = -ENOMEM;
		goto err_out;
	}
	return 0;

err_out:
	zcomp_strm_free(comp, zstrm);
	return ret;
}

static int zcomp_cpu_create(struct zcomp *comp, const char *name)
{
	struct zcomp_cpu *zcomp_cpu;
	int ret = -ENOMEM;

	zcomp_cpu = kmalloc(sizeof(*zcomp_cpu), GFP_KERNEL);
	if (!zcomp_cpu)
		return -ENOMEM;

	zcomp_cpu->ops = lookup_backend_ops(comp->algo_name);
	if (!zcomp_cpu->ops) {
		ret = -EINVAL;
		goto free_zcomp_cpu;
	}

	zcomp_cpu->zstrm = alloc_percpu(struct zcomp_strm);
	if (!zcomp_cpu->zstrm) {
		ret = -ENOMEM;
		goto free_zcomp_cpu;
	}
	comp->private = zcomp_cpu;

	ret = zcomp_cpu->ops->setup_params(comp->params);
	if (ret)
		goto cleanup;

	ret = cpuhp_state_add_instance(CPUHP_ZCOMP_PREPARE, &comp->node);
	if (ret < 0)
		goto free_zstrm;

	zcomp_cpu->prio = comp->prio;
	__module_get(THIS_MODULE);
	return ret;

cleanup:
	zcomp_cpu->ops->release_params(comp->params);
free_zstrm:
	free_percpu(zcomp_cpu->zstrm);
free_zcomp_cpu:
	kfree(zcomp_cpu);
	return ret;
}

static void zcomp_cpu_destroy(struct zcomp *comp)
{
	struct zcomp_cpu *zcomp_cpu = comp->private;

	cpuhp_state_remove_instance(CPUHP_ZCOMP_PREPARE, &comp->node);
	zcomp_cpu->ops->release_params(comp->params);
	free_percpu(zcomp_cpu->zstrm);
	kfree(zcomp_cpu);
	module_put(THIS_MODULE);
}

const struct zcomp_operation zcomp_cpu_op = {
	.create = zcomp_cpu_create,
	.destroy = zcomp_cpu_destroy,
	.compress = zcomp_cpu_compress,
	.decompress = zcomp_cpu_decompress,
	.recompress = zcomp_cpu_recompress,
};

static int zcomp_cpu_up_prepare(unsigned int cpu, struct hlist_node *node)
{
	struct zcomp *comp = hlist_entry(node, struct zcomp, node);
	struct zcomp_cpu *zcomp_cpu = comp->private;
	struct zcomp_strm *zstrm;
	int ret;

	zstrm = per_cpu_ptr((struct zcomp_strm __percpu *)zcomp_cpu->zstrm,
			    cpu);
	local_lock_init(&zstrm->lock);

	ret = zcomp_strm_init(comp, zstrm);
	if (ret)
		pr_err("Can't allocate a compression stream\n");
	return ret;
}

static int zcomp_cpu_dead(unsigned int cpu, struct hlist_node *node)
{
	struct zcomp *comp = hlist_entry(node, struct zcomp, node);
	struct zcomp_cpu *zcomp_cpu = comp->private;
	struct zcomp_strm *zstrm;

	zstrm = per_cpu_ptr((struct zcomp_strm __percpu *)zcomp_cpu->zstrm,
			    cpu);
	zcomp_strm_free(comp, zstrm);
	return 0;
}

static int __init zcomp_cpu_init(void)
{
	int ret;
	int i;

	/*
	 * The backends array has a sentinel NULL value, so the minimum
	 * size is 1. In order to be valid the array, apart from the
	 * sentinel NULL element, should have at least one compression
	 * backend selected.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(backends) <= 1);

	for (i = 0; i < ARRAY_SIZE(backends); i++) {
		if (!backends[i])
			continue;
		ret = zcomp_register(backends[i]->name, &zcomp_cpu_op);
		if (ret)
			goto out;
	}

	ret = cpuhp_setup_state_multi(CPUHP_ZCOMP_PREPARE,
			"block/zram/zcomp/cpu:prepare",
			zcomp_cpu_up_prepare, zcomp_cpu_dead);
	if (ret)
		goto out;

	return ret;

out:
	for (i = i - 1; i >= 0; i--)
		zcomp_unregister(backends[i]->name);

	return ret;
}

static void __exit zcomp_cpu_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(backends); i++)
		zcomp_unregister(backends[i]->name);

	cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
}

module_init(zcomp_cpu_init);
module_exit(zcomp_cpu_exit);
MODULE_LICENSE("GPL");
