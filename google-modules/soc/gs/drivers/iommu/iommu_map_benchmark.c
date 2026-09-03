// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 HiSilicon Limited.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include "iommu_map_benchmark.h"

struct map_benchmark_data {
	struct iommu_map_benchmark bparam;
	struct device *dev;
	struct dentry  *debugfs;
	atomic64_t sum_map_ns;
	atomic64_t sum_unmap_ns;
	atomic64_t sum_sq_map;
	atomic64_t sum_sq_unmap;
	atomic64_t loops;
};

struct thread_data {
	struct map_benchmark_data *map;
	unsigned long iova_start;
	unsigned int thread_num;
};

unsigned long iova_start;
unsigned long iova_end;
unsigned long iova_ptr;

static int map_benchmark_thread(void *data)
{
	struct thread_data *t_data = data;
	struct map_benchmark_data *map = t_data->map;
	struct sg_table sgt;
	struct scatterlist *sg;
	struct page *page;
	dma_addr_t iova;
	int prot = IOMMU_READ | IOMMU_WRITE;
	int npages = map->bparam.num_pages;
	int nmappings = map->bparam.num_mappings;
	struct iommu_domain *domain = iommu_get_domain_for_dev(map->dev);
	int ret = 0, i;
	size_t size = npages * PAGE_SIZE;
	size_t aligned_size;
	unsigned long align_mask = ~0UL;
	unsigned long shift;

	shift = fls_long(size - 1);
	align_mask <<= shift;
	aligned_size = 1UL << shift;

	iova = t_data->iova_start;
	dev_info(map->dev, "iova starting address of thread %u: %pad", t_data->thread_num, &iova);

	if (sg_alloc_table(&sgt, npages, GFP_KERNEL))
		return -ENOMEM;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	for_each_sgtable_sg(&sgt, sg, i) {
		sg_set_page(sg, page, page_size(page), 0);
	}

	while (!kthread_should_stop())  {
		u64 map_sq, unmap_sq;
		ktime_t map_stime, map_etime, unmap_stime, unmap_etime;
		ktime_t map_delta, unmap_delta;
		ssize_t map_size;
		size_t unmapped_size;

		map_stime = ktime_get();
		for (i = 0; i < nmappings; i++) {
			map_size = iommu_map_sg(domain, iova + i * aligned_size, sgt.sgl,
						sgt.orig_nents, prot, GFP_KERNEL);
			if (map_size < 0) {
				ret = map_size;
				pr_err("iommu_map_sg failed on %s: %d\n", dev_name(map->dev), ret);
				goto out;
			}
			if (map_size < (npages << PAGE_SHIFT)) {
				ret = -ENOMEM;
				pr_err("iommu_map_sg not all memory are mapped %s (%zd vs %zu)\n",
				       dev_name(map->dev), map_size, (size_t)npages << PAGE_SHIFT);
				goto out;
			}
		}
		map_etime = ktime_get();
		map_delta = ktime_sub(map_etime, map_stime);

		/* Pretend DMA is transmitting */
		ndelay(map->bparam.dma_trans_ns);

		unmap_stime = ktime_get();
		for (i = 0; i < nmappings; i++) {
			unmapped_size = iommu_unmap(domain, iova + i * aligned_size, map_size);
			if (unmapped_size != (size_t)map_size) {
				ret = -EIO;
				pr_err("iommu_unmap reported different size than iommu_map_sg (%zu vs %zd)\n",
				       unmapped_size, map_size);
				goto out;
			}
		}
		unmap_etime = ktime_get();
		unmap_delta = ktime_sub(unmap_etime, unmap_stime);

		/* calculate sum and sum of squares */


		map_delta = div64_ul(map_delta,  nmappings);
		unmap_delta = div64_ul(unmap_delta,  nmappings);
		map_sq = map_delta * map_delta;
		unmap_sq = unmap_delta * unmap_delta;

		atomic64_add(map_delta, &map->sum_map_ns);
		atomic64_add(unmap_delta, &map->sum_unmap_ns);
		atomic64_add(map_sq, &map->sum_sq_map);
		atomic64_add(unmap_sq, &map->sum_sq_unmap);
		atomic64_inc(&map->loops);

		/*
		 * We may test for a long time so periodically check whether
		 * we need to schedule to avoid starving the others. Otherwise
		 * we may hangup the kernel in a non-preemptible kernel when
		 * the test kthreads number >= CPU number, the test kthreads
		 * will run endless on every CPU since the thread resposible
		 * for notifying the kthread stop (in do_map_benchmark())
		 * could not be scheduled.
		 *
		 * Note this may degrade the test concurrency since the test
		 * threads may need to share the CPU time with other load
		 * in the system. So it's recommended to run this benchmark
		 * on an idle system.
		 */
		cond_resched();
	}

out:
	if (page)
		__free_page(page);
	sg_free_table(&sgt);
	return ret;
}

static int do_map_benchmark(struct map_benchmark_data *map)
{
	struct task_struct **tsk = NULL;
	struct thread_data *t_data = NULL;
	int threads = map->bparam.threads;
	size_t size = map->bparam.num_pages * PAGE_SIZE;
	size_t aligned_size;
	u64 loops;
	int ret = 0;
	int i;
	unsigned long align_mask = ~0UL;
	u64 dma_size;
	unsigned long shift;
	cpumask_t cpu_mask;

	shift = fls_long(size - 1);
	align_mask <<= shift;
	aligned_size = 1UL << shift;
	aligned_size *= map->bparam.num_mappings;

	dma_size = iova_end - iova_start + 1;

	if ((aligned_size * threads) > dma_size) {
		ret = -EINVAL;
		pr_err("aligned size too large %zu\n", aligned_size);
		goto out;
	}

	tsk = kcalloc(threads, sizeof(*tsk), GFP_KERNEL);
	if (!tsk) {
		ret = -ENOMEM;
		goto out;
	}

	t_data = kcalloc(threads, sizeof(*t_data), GFP_KERNEL);
	if (!t_data) {
		ret = -ENOMEM;
		goto out;
	}

	get_device(map->dev);

	iova_ptr = iova_end;

	cpumask_copy(&cpu_mask, &current->cpus_mask);

	for (i = 0; i < threads; i++) {
		t_data[i].map = map;
		t_data[i].thread_num = i;
		if ((iova_ptr - iova_start + 1) < aligned_size)
			t_data[i].iova_start = (iova_end - aligned_size + 1) & align_mask;
		else
			t_data[i].iova_start = (iova_ptr - aligned_size + 1) & align_mask;

		tsk[i] = kthread_create_on_node(map_benchmark_thread, &t_data[i], map->bparam.node,
						"iommu-map-benchmark/%d", i);
		if (IS_ERR(tsk[i])) {
			pr_err("create iommu_map thread failed\n");
			ret = PTR_ERR(tsk[i]);
			while (--i >= 0)
				kthread_stop(tsk[i]);
			goto out;
		}

		kthread_bind_mask(tsk[i], &cpu_mask);

		if (t_data[i].iova_start == 0)
			iova_ptr = iova_end;
		else
			iova_ptr = t_data[i].iova_start - 1;
	}

	/* clear the old value in the previous benchmark */
	atomic64_set(&map->sum_map_ns, 0);
	atomic64_set(&map->sum_unmap_ns, 0);
	atomic64_set(&map->sum_sq_map, 0);
	atomic64_set(&map->sum_sq_unmap, 0);
	atomic64_set(&map->loops, 0);

	for (i = 0; i < threads; i++) {
		get_task_struct(tsk[i]);
		wake_up_process(tsk[i]);
	}

	msleep_interruptible(map->bparam.seconds * 1000);

	/* wait for the completion of all started benchmark threads */
	for (i = 0; i < threads; i++) {
		int kthread_ret = kthread_stop_put(tsk[i]);

		if (kthread_ret)
			ret = kthread_ret;
	}

	if (ret)
		goto out;

	loops = atomic64_read(&map->loops);
	if (likely(loops > 0)) {
		u64 map_variance, unmap_variance;
		u64 sum_map = atomic64_read(&map->sum_map_ns);
		u64 sum_unmap = atomic64_read(&map->sum_unmap_ns);
		u64 sum_sq_map = atomic64_read(&map->sum_sq_map);
		u64 sum_sq_unmap = atomic64_read(&map->sum_sq_unmap);

		/* average latency */
		map->bparam.avg_map_ns = div64_u64(sum_map, loops);
		map->bparam.avg_unmap_ns = div64_u64(sum_unmap, loops);

		/* standard deviation of latency */
		map_variance = div64_u64(sum_sq_map, loops) -
				map->bparam.avg_map_ns *
				map->bparam.avg_map_ns;
		unmap_variance = div64_u64(sum_sq_unmap, loops) -
				map->bparam.avg_unmap_ns *
				map->bparam.avg_unmap_ns;
		map->bparam.map_stddev = int_sqrt64(map_variance);
		map->bparam.unmap_stddev = int_sqrt64(unmap_variance);
	}

out:
	kfree(tsk);
	kfree(t_data);
	put_device(map->dev);
	return ret;
}

static long map_benchmark_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct map_benchmark_data *map = file->private_data;
	void __user *argp = (void __user *)arg;
	u64 old_dma_mask;
	int ret;

	if (copy_from_user(&map->bparam, argp, sizeof(map->bparam)))
		return -EFAULT;

	switch (cmd) {
	case IOMMU_MAP_BENCHMARK:
		if (map->bparam.threads == 0 ||
		    map->bparam.threads > IOMMU_MAP_MAX_THREADS) {
			pr_err("invalid thread number\n");
			return -EINVAL;
		}

		if (map->bparam.seconds == 0 ||
		    map->bparam.seconds > IOMMU_MAP_MAX_SECONDS) {
			pr_err("invalid duration seconds\n");
			return -EINVAL;
		}

		if (map->bparam.dma_trans_ns > IOMMU_MAP_MAX_TRANS_DELAY) {
			pr_err("invalid transmission delay\n");
			return -EINVAL;
		}

		if (map->bparam.node != NUMA_NO_NODE &&
		    (map->bparam.node < 0 || map->bparam.node >= MAX_NUMNODES ||
		     !node_possible(map->bparam.node))) {
			pr_err("invalid numa node\n");
			return -EINVAL;
		}

		if (map->bparam.num_pages < 1) {
			pr_err("invalid num_pages\n");
			return -EINVAL;
		}

		if (map->bparam.num_mappings < 1)
			map->bparam.num_mappings = 1;

		old_dma_mask = dma_get_mask(map->dev);

		ret = dma_set_mask(map->dev,
				   DMA_BIT_MASK(map->bparam.dma_bits));
		if (ret) {
			pr_err("failed to set dma_mask on device %s\n",
				dev_name(map->dev));
			return -EINVAL;
		}

		ret = do_map_benchmark(map);

		/*
		 * restore the original dma_mask as many devices' dma_mask are
		 * set by architectures, acpi, busses. When we bind them back
		 * to their original drivers, those drivers shouldn't see
		 * dma_mask changed by benchmark
		 */
		dma_set_mask(map->dev, old_dma_mask);

		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user(argp, &map->bparam, sizeof(map->bparam)))
		return -EFAULT;

	return ret;
}

static const struct file_operations map_benchmark_fops = {
	.open			= simple_open,
	.unlocked_ioctl		= map_benchmark_ioctl,
};

static void map_benchmark_remove_debugfs(void *data)
{
	struct map_benchmark_data *map = (struct map_benchmark_data *)data;

	debugfs_remove(map->debugfs);
}

static int __map_benchmark_probe(struct device *dev)
{
	struct dentry *entry;
	struct map_benchmark_data *map;
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	u64 dma_limit;
	int ret;

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	dma_limit = dma_get_mask(dev);
	if (domain->geometry.force_aperture)
		dma_limit = min_t(u64, dma_limit, (u64)domain->geometry.aperture_end);

	if (!domain) {
		dev_err(dev, "iommu_domain is NULL!\n");
		return -EINVAL;
	}

	map = devm_kzalloc(dev, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;
	map->dev = dev;

	ret = devm_add_action(dev, map_benchmark_remove_debugfs, map);
	if (ret) {
		dev_err(dev, "Can't add debugfs remove action\n");
		return ret;
	}

	/*
	 * we only permit a device bound with this driver, 2nd probe
	 * will fail
	 */
	entry = debugfs_create_file("iommu_map_benchmark", 0600, NULL, map,
			&map_benchmark_fops);
	if (IS_ERR(entry))
		return PTR_ERR(entry);
	map->debugfs = entry;

	iova_start = 0;
	if (domain->geometry.force_aperture)
		iova_start = domain->geometry.aperture_start;
	iova_end = dma_limit;
	iova_ptr = iova_end;
	dev_info(dev, "iova range: 0x%lx to 0x%lx\n", iova_start, iova_end);

	return 0;
}

static int map_benchmark_platform_probe(struct platform_device *pdev)
{
	return __map_benchmark_probe(&pdev->dev);
}

static const struct of_device_id map_benchmark_of_match[] = {
	{ .compatible = "iommu-map-benchmark", },
	{ },
};
MODULE_DEVICE_TABLE(of, map_benchmark_of_match);

static struct platform_driver map_benchmark_platform_driver = {
	.driver		= {
		.name	= "iommu_map_benchmark",
		.of_match_table = map_benchmark_of_match,
	},
	.probe = map_benchmark_platform_probe,
};

static int
map_benchmark_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return __map_benchmark_probe(&pdev->dev);
}

static struct pci_driver map_benchmark_pci_driver = {
	.name	= "iommu_map_benchmark",
	.probe	= map_benchmark_pci_probe,
};

static int __init map_benchmark_init(void)
{
	int ret;

	ret = pci_register_driver(&map_benchmark_pci_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&map_benchmark_platform_driver);
	if (ret) {
		pci_unregister_driver(&map_benchmark_pci_driver);
		return ret;
	}

	return 0;
}

static void __exit map_benchmark_cleanup(void)
{
	platform_driver_unregister(&map_benchmark_platform_driver);
	pci_unregister_driver(&map_benchmark_pci_driver);
}

module_init(map_benchmark_init);
module_exit(map_benchmark_cleanup);

MODULE_AUTHOR("Barry Song <song.bao.hua@hisilicon.com>");
MODULE_DESCRIPTION("iommu map benchmark driver");
MODULE_LICENSE("GPL");
