// SPDX-License-Identifier: GPL-2.0-only
/* vendor_mm_init.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2022 Google LLC
 */

#include "linux/cpumask.h"
#include "linux/delay.h"
#include "linux/spinlock.h"
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/rt.h>

#include "pixel_mm_hint.h"
#include "pixel_mm.h"
#include "cma.h"
#include "deferred-free-helper.h"

#define READ_SWAP_CACHE_ASYNC_SPIN_COUNT 8
#define READ_SWAP_CACHE_ASYNC_DELAY_US 5
#define READ_SWAP_CACHE_ASYNC_SLEEP_MIN 10
#define READ_SWAP_CACHE_ASYNC_SLEEP_MAX 15

static struct task_struct *tsk_kswapd, *tsk_kcompactd;
static atomic_t gup_longterm_lock_fail_count = ATOMIC_INIT(0);

/*
 * Last requested kcompactd CPU affinity.
 * Don't access directly, use kcompactd_requested_cpu_affinity_get()/set().
 */
static cpumask_t kcompactd_requested_cpu_affinity_value = CPU_MASK_NONE;
static DEFINE_SPINLOCK(kcompactd_requested_cpu_affinity_lock);

#define VENDOR_MM_RW(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RW(_name)

static void kcompactd_requested_cpu_affinity_set(const cpumask_t* affinity) {
	spin_lock(&kcompactd_requested_cpu_affinity_lock);
	cpumask_copy(&kcompactd_requested_cpu_affinity_value, affinity);
	spin_unlock(&kcompactd_requested_cpu_affinity_lock);
}

static void kcompactd_requested_cpu_affinity_get(cpumask_t* out_affinity) {
	spin_lock(&kcompactd_requested_cpu_affinity_lock);
	cpumask_copy(out_affinity, &kcompactd_requested_cpu_affinity_value);
	spin_unlock(&kcompactd_requested_cpu_affinity_lock);
}

static int task_set_uclamp_min(struct task_struct *tsk, const char *buf)
{
	struct sched_attr sched_attr = {0};
	int ret = -EINVAL;
	u32 val = 0;

	if (kstrtou32(buf, 10, &val))
		return ret;

	sched_attr.sched_flags = SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_KEEP_ALL;
	sched_attr.sched_util_min = val;

	if (tsk)
		ret = sched_setattr_nocheck(tsk, &sched_attr);

	return ret;
}

static int task_set_uclamp_max(struct task_struct *tsk, const char *buf)
{
	struct sched_attr sched_attr = {0};
	int ret = -EINVAL;
	u32 val = 0;

	if (kstrtou32(buf, 10, &val))
		return ret;

	sched_attr.sched_flags = SCHED_FLAG_UTIL_CLAMP_MAX | SCHED_FLAG_KEEP_ALL;
	sched_attr.sched_util_max = val;

	if (tsk)
		ret = sched_setattr_nocheck(tsk, &sched_attr);

	return ret;
}

static int parse_cpu_affinity(cpumask_t *out_mask, const char *buf)
{
	cpumask_t requested_cpumask;
	int ret;

	ret = cpumask_parse(buf, &requested_cpumask);
	if (ret < 0 || cpumask_empty(&requested_cpumask))
		return -EINVAL;

	cpumask_and(out_mask, &requested_cpumask, cpu_possible_mask);
	return 0;
}

static int task_set_cpu_affinity(struct task_struct *tsk, const char *buf)
{
	cpumask_t affinity;
	int ret;

	ret = parse_cpu_affinity(&affinity, buf);
	if (ret < 0)
		return ret;

	if (tsk) {
		set_cpus_allowed_ptr(tsk, &affinity);
	}

	return 0;
}

static ssize_t kswapd_cpu_affinity_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	cpumask_t cpumask;

	if (tsk_kswapd && (tsk_kswapd->flags & PF_KSWAPD)) {
		cpumask	= tsk_kswapd->cpus_mask;
		return cpumap_print_to_pagebuf(false, buf, &cpumask);
	} else {
		/* we should never get here */
		WARN_ON(1);
		return -ESRCH;
	}
}

static ssize_t kswapd_cpu_affinity_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t len)
{
	int ret;

	if (tsk_kswapd) {
		ret = task_set_cpu_affinity(tsk_kswapd, buf);
	} else {
		WARN_ON_ONCE(1);
		return -ESRCH;
	}

	return ret ? ret : len;;
}
VENDOR_MM_RW(kswapd_cpu_affinity);

static ssize_t kswapd_uclamp_min_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	u32 val = 0;

	if (tsk_kswapd && (tsk_kswapd->flags & PF_KSWAPD)) {
		val = tsk_kswapd->uclamp_req[UCLAMP_MIN].value;
		return sysfs_emit(buf, "%u\n", val);
	} else {
		/* we should never get here */
		WARN_ON(1);
		return -ESRCH;
	}
}

static ssize_t kswapd_uclamp_min_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t len)
{
	int ret;

	if (tsk_kswapd) {
		ret = task_set_uclamp_min(tsk_kswapd, buf);
	} else {
		WARN_ON_ONCE(1);
		return -ESRCH;
	}

	return ret ? ret : len;
}
VENDOR_MM_RW(kswapd_uclamp_min);

static ssize_t kswapd_uclamp_max_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	u32 val = 0;

	if (tsk_kswapd && (tsk_kswapd->flags & PF_KSWAPD)) {
		val = tsk_kswapd->uclamp_req[UCLAMP_MAX].value;
		return sysfs_emit(buf, "%u\n", val);
	}
	/* we should never get here */
	WARN_ON(1);
	return -ESRCH;
}

static ssize_t kswapd_uclamp_max_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t len)
{
	int ret;

	if (tsk_kswapd) {
		ret = task_set_uclamp_max(tsk_kswapd, buf);
	} else {
		WARN_ON_ONCE(1);
		return -ESRCH;
	}

	return ret ? ret : len;
}
VENDOR_MM_RW(kswapd_uclamp_max);

static bool is_kcompactd(struct task_struct *tsk)
{
	char comm[TASK_COMM_LEN];

	return (tsk->flags & PF_KTHREAD) &&
		strstarts(get_task_comm(comm, tsk), "kcompactd");
}

static ssize_t kcompactd_uclamp_min_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	u32 val = 0;

	if (tsk_kcompactd && is_kcompactd(tsk_kcompactd)) {
		val = tsk_kcompactd->uclamp_req[UCLAMP_MIN].value;
		return sysfs_emit(buf, "%u\n", val);
	} else {
		/* we should never get here */
		WARN_ON(1);
		return -ESRCH;
	}
}

static ssize_t kcompactd_uclamp_min_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t len)
{
	int ret;

	if (tsk_kcompactd) {
		ret = task_set_uclamp_min(tsk_kcompactd, buf);
	} else {
		WARN_ON_ONCE(1);
		return -ESRCH;
	}

	return ret ? ret : len;
}
VENDOR_MM_RW(kcompactd_uclamp_min);

static ssize_t kcompactd_cpu_affinity_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	cpumask_t cpumask;

	if (tsk_kcompactd && is_kcompactd(tsk_kcompactd)) {
		cpumask	= tsk_kcompactd->cpus_mask;
		return cpumap_print_to_pagebuf(false, buf, &cpumask);
	} else {
		/* we should never get here */
		WARN_ON(1);
		return -ESRCH;
	}
}

static ssize_t kcompactd_cpu_affinity_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf,
					    size_t len)
{
	cpumask_t affinity;
	int ret;

	if (tsk_kcompactd) {
		ret = parse_cpu_affinity(&affinity, buf);
		if (ret == 0) {
			kcompactd_requested_cpu_affinity_set(&affinity);
			set_cpus_allowed_ptr(tsk_kcompactd, &affinity);
		}
	} else {
		WARN_ON_ONCE(1);
		return -ESRCH;
	}

	return ret ? ret : len;
}

/* kcompactd_cpu_online() resets cpumask, so we need to restore it. */
static void vh_kcompactd_cpu_online(void *data, int cpu) {
	cpumask_t affinity;

	if (tsk_kcompactd) {
		kcompactd_requested_cpu_affinity_get(&affinity);
		if (!cpumask_empty(&affinity)) {
			set_cpus_allowed_ptr(tsk_kcompactd, &affinity);
		}
	}
}

static inline void SetPageSkippedZero(struct page *page)
{
	set_bit(PG_oem_reserved_1, &page->flags);
}

static inline void ClearPageSkippedZero(struct page *page)
{
	clear_bit(PG_oem_reserved_1, &page->flags);
}

static inline bool PageSkippedZero(struct page *page)
{
	return test_bit(PG_oem_reserved_1, &page->flags);
}

static void vh_free_pages_prepare_init(void *data, struct page *page,
				       int nr_pages, bool *init)
{
	if (kasan_enabled() || !want_init_on_free())
		return;

	if (unlikely(current_is_kswapd() ||
		     current->flags & PF_MEMALLOC ||
		     (current->mm && test_bit(MMF_UNSTABLE, &current->mm->flags)))) {
		int i;

		*init = false;
		for (i = 0; i < nr_pages; i++)
			SetPageSkippedZero(page + i);
	}
}

static void vh_swap_writepage(void *data,
			      unsigned long *sis_flag,
			      struct page *page)
{
	*sis_flag &= ~SWP_SYNCHRONOUS_IO;
}

static void vh_kvmalloc_high_order_skip_direct_reclaim(void *data,
		unsigned int order, gfp_t *gfp_flags)
{
	/*
	 * If the allocation order exceeds the threshold, clear
	 * __GFP_DIRECT_RECLAIM to prevent potential latency spikes from
	 * direct reclaim.
	 */
	if (order > PAGE_ALLOC_COSTLY_ORDER)
		*gfp_flags &= ~__GFP_DIRECT_RECLAIM;
}

static void rvh_gup_longterm_locked(void *data,
				    long rc,
				    long nr_pinned_pages,
				    unsigned long start,
				    unsigned long nr_pages,
				    struct page **pages)
{
	/* increment fail cnt while gup longterm vh is called */
	atomic_inc(&gup_longterm_lock_fail_count);

	/*
	 * Determine the final return code. If rc is set, it indicates a failure
	 * after the initial pinning (e.g., migration failure). Otherwise, the
	 * result is from the initial pinning attempt itself.
	 */
	pr_warn_ratelimited("FOLL_LONGTERM failed for %s PID:%d start=0x%lx, nr_pages=%lu error: %ld nr_pinned_pages: %ld\n",
			    current->comm, task_pid_nr(current),
			    start, nr_pages, rc, nr_pinned_pages);

	/*
	 * Only case rc == -ENOMEM indicates migration fail
	 * Dump only the first page to avoid log spam. Dumping the full list
	 * doesn't reveal the elevated refcount holder regardless.
	 * To debug refcounts, we should eventually enable page_pinnder.
	 * For now, this hook focuses on failure statistics and error codes
	 */
	if (rc == -ENOMEM && pages && pages[0]) {
		dump_page(pages[0],
			  "FOLL_LONGTERM migration fail - pinned page");
		pr_warn_ratelimited("dump_page call completed for page[0]\n");
	}
}

static void reset_page_oem_fields(unsigned long *check_flags)
{
	*check_flags = *check_flags & ~(1UL << PG_oem_reserved_1);
}

#if IS_ENABLED(CONFIG_DEBUG_VM)
static void vh_free_one_page_flag_check(void *data, unsigned long *check_flags)
{
	if (kasan_enabled() || !want_init_on_free())
		return;

	reset_page_oem_fields(check_flags);
}
#endif

static void vh_post_alloc_hook(void *data, struct page *page,
			       unsigned int order, bool *init)
{
	int i, nr_pages;

	if (kasan_enabled() || !want_init_on_free())
		return;

	nr_pages = 1 << order;
	for (i = 0; i < nr_pages; i++) {
		if (PageSkippedZero(page + i)) {
			ClearPageSkippedZero(page + i);
			*init = true;
		}
	}
}

static void vh_check_new_page(void *data, unsigned long *check_flags)
{
	if (kasan_enabled() || !want_init_on_free())
		return;

	reset_page_oem_fields(check_flags);
}

VENDOR_MM_RW(kcompactd_cpu_affinity);

static ssize_t fail_of_gup_longterm_locked_show(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	return sysfs_emit(buf, "%u\n", atomic_read(&gup_longterm_lock_fail_count));
}

static ssize_t fail_of_gup_longterm_locked_store(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 const char *buf,
						 size_t len)
{
	int ret = -EINVAL;
	u32 val = 0;

	if (kstrtou32(buf, 10, &val))
		return ret;
	atomic_set(&gup_longterm_lock_fail_count, val);
	return len;
}

VENDOR_MM_RW(fail_of_gup_longterm_locked);

static ssize_t deferred_free_thread_nice_show(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	if (freelist_task)
		return sysfs_emit(buf, "%d\n", task_nice(freelist_task));

	return -ESRCH;
}

static ssize_t deferred_free_thread_nice_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf,
						size_t len)
{
	int nice;

	if (kstrtoint(buf, 10, &nice))
		return -EINVAL;

	if (nice < MIN_NICE)
		nice = MIN_NICE;
	if (nice > MAX_NICE)
		nice = MAX_NICE;

	if (freelist_task) {
		sched_set_normal(freelist_task, nice);
	} else {
		WARN_ON_ONCE(1);
		return -ESRCH;
	}

	return len;
}

VENDOR_MM_RW(deferred_free_thread_nice);

static void rvh_read_swap_cache_async_schedule_timeout(void *data, size_t *count, bool *skip)
{
	*skip = true;

	//spin before sleep
	if (*count < READ_SWAP_CACHE_ASYNC_SPIN_COUNT) {
		*count = *count + 1;
		udelay(READ_SWAP_CACHE_ASYNC_DELAY_US);
		return;
	}

	usleep_range(READ_SWAP_CACHE_ASYNC_SLEEP_MIN, READ_SWAP_CACHE_ASYNC_SLEEP_MAX);
}

static bool rt_disallow_wmark_bypass;

static ssize_t rt_disallow_wmark_bypass_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	return sysfs_emit(buf, "%d\n", rt_disallow_wmark_bypass);
}

static ssize_t rt_disallow_wmark_bypass_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf,
					      size_t len)
{
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rt_disallow_wmark_bypass = val;
	return len;
}

VENDOR_MM_RW(rt_disallow_wmark_bypass);

#ifndef ALLOC_OOM
#define ALLOC_OOM		0x08
#endif
#ifndef ALLOC_NON_BLOCK
#define ALLOC_NON_BLOCK		0x10
#endif
#ifndef ALLOC_MIN_RESERVE
#define ALLOC_MIN_RESERVE	0x20
#endif
#ifndef ALLOC_HIGHATOMIC
#define ALLOC_HIGHATOMIC	0x200
#endif
#ifndef ALLOC_RESERVES
#define ALLOC_RESERVES		(ALLOC_NON_BLOCK|ALLOC_MIN_RESERVE|ALLOC_HIGHATOMIC|ALLOC_OOM)
#endif

static void vh_calc_alloc_flags(void *data, gfp_t gfp_mask,
				unsigned int *alloc_flags, bool *bypass)
{
	if (rt_disallow_wmark_bypass && rt_task(current) && in_task() &&
	    !(current->flags & PF_KTHREAD))
		*alloc_flags &= ~ALLOC_RESERVES;
}

static struct attribute *vendor_mm_attrs[] = {
	&kswapd_cpu_affinity_attr.attr,
	&rt_disallow_wmark_bypass_attr.attr,
	&kswapd_uclamp_min_attr.attr,
	&kswapd_uclamp_max_attr.attr,
	&kcompactd_cpu_affinity_attr.attr,
	&kcompactd_uclamp_min_attr.attr,
	&fail_of_gup_longterm_locked_attr.attr,
	&deferred_free_thread_nice_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vendor_mm);

static int init_get_kswapd_kcompactd_tasks(void)
{
	struct task_struct *tsk;
	int ret = -ESRCH;

	rcu_read_lock();
	for_each_process(tsk) {
		/*
		 * assume we only have 1 kcompactd and kswapd
		 * and they won't change later.
		 */
		if (tsk->flags & PF_KSWAPD) {
			tsk_kswapd = tsk;
		} else if (is_kcompactd(tsk)) {
			tsk_kcompactd = tsk;
		}

		if (tsk_kswapd && tsk_kcompactd) {
			ret = 0;
			break;
		}
	}
	rcu_read_unlock();

	return ret;
}

struct kobject *vendor_mm_kobj;
EXPORT_SYMBOL_GPL(vendor_mm_kobj);

static int vh_mm_init(void)
{
	int ret;

	ret = init_get_kswapd_kcompactd_tasks();
	if (ret)
		goto out_err;

	vendor_mm_kobj = kobject_create_and_add("vendor_mm", kernel_kobj);
	if (!vendor_mm_kobj)
		return -ENOMEM;

	ret = sysfs_create_groups(vendor_mm_kobj, vendor_mm_groups);
	if (ret)
		goto out_err;

	ret = pixel_mm_cma_sysfs(vendor_mm_kobj);
	if (ret)
		goto out_err;

	ret = pixel_mm_filemap_sysfs(vendor_mm_kobj);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_mm_kcompactd_cpu_online(
		vh_kcompactd_cpu_online, NULL);
	if (ret)
		goto out_err;

	/* Do not reorder this three functions */
	ret = register_trace_android_vh_check_new_page(
		vh_check_new_page, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_post_alloc_hook(
		vh_post_alloc_hook, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_free_pages_prepare_init(
		vh_free_pages_prepare_init, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_swap_writepage(
		vh_swap_writepage, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_adjust_kvmalloc_flags(
		vh_kvmalloc_high_order_skip_direct_reclaim, NULL);
	if (ret)
		goto out_err;

#if IS_ENABLED(CONFIG_DEBUG_VM)
	ret = register_trace_android_vh_free_one_page_flag_check(
		vh_free_one_page_flag_check, NULL);
	if (ret)
		goto out_err;
#endif

	ret = register_trace_android_vh_tune_swappiness(vh_vmscan_tune_swappiness, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_calculate_totalreserve_pages(vh_update_lmkd_watermark, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_do_async_mmap_readahead(vh_do_async_mmap_readahead, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_do_sync_mmap_readahead(vh_do_sync_mmap_readahead, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_rvh_gup_longterm_locked(rvh_gup_longterm_locked, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_page_cache_readahead_start(
				vh_page_cache_readahead_start, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_page_cache_ra_order_bypass(
				vh_page_cache_ra_order_bypass, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_ra_alloc_retry(
				vh_ra_alloc_retry, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_rvh_read_swap_cache_async_timeout
			(rvh_read_swap_cache_async_schedule_timeout, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_android_vh_calc_alloc_flags(
				vh_calc_alloc_flags, NULL);
	if (ret)
		goto out_err;

	return ret;

out_err:
	kobject_put(vendor_mm_kobj);
	return ret;
}
module_init(vh_mm_init);
MODULE_LICENSE("GPL v2");
