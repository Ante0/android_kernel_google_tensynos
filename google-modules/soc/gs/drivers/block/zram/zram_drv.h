/*
 * Compressed RAM block device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *               2012, 2013 Minchan Kim
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

#ifndef _ZRAM_DRV_H_
#define _ZRAM_DRV_H_

#include <linux/rwsem.h>
#include <linux/zsmalloc.h>
#include <linux/crypto.h>
#include <linux/rcupdate.h>

#if IS_ENABLED(CONFIG_ZRAM_GS_ANDROID_IOCTL)
#include <linux/xarray.h>
#endif

#include "zcomp.h"

#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define ZRAM_LOGICAL_BLOCK_SHIFT 12
#define ZRAM_LOGICAL_BLOCK_SIZE	(1 << ZRAM_LOGICAL_BLOCK_SHIFT)
#define ZRAM_SECTOR_PER_LOGICAL_BLOCK	\
	(1 << (ZRAM_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))


/*
 * ZRAM is mainly used for memory efficiency so we want to keep memory
 * footprint small and thus squeeze size and zram pageflags into a flags
 * member. The lower ZRAM_FLAG_SHIFT bits is for object size (excluding
 * header), which cannot be larger than PAGE_SIZE (requiring PAGE_SHIFT
 * bits), the higher bits are for zram_pageflags.
 *
 * We use BUILD_BUG_ON() to make sure that zram pageflags don't overflow.
 */
#define ZRAM_FLAG_SHIFT (PAGE_SHIFT + 1)

/* Only 2 bits are allowed for comp priority index */
#define ZRAM_COMP_PRIORITY_MASK	0x3

/* Flags for zram pages (table[page_no].flags) */
enum zram_pageflags {
	ZRAM_SAME = ZRAM_FLAG_SHIFT,	/* Page consists the same element */
	ZRAM_WB,	/* page is stored on backing_device */
	ZRAM_PP_SLOT,	/* Selected for post-processing */
	ZRAM_HUGE,	/* Incompressible page */
	ZRAM_IDLE,	/* not accessed page since last idle marking */
	ZRAM_INCOMPRESSIBLE, /* none of the algorithms could compress it */
	ZRAM_PROC_WB,	/* page is stored on backing_device by per-process wb */

	ZRAM_COMP_PRIORITY_BIT1, /* First bit of comp priority index */
	ZRAM_COMP_PRIORITY_BIT2, /* Second bit of comp priority index */

	__NR_ZRAM_PAGEFLAGS,
};

/*-- Data structures */

/* Allocated for each disk page */
struct zram_table_entry {
	union {
		unsigned long handle;
		unsigned long element;
	};
	unsigned int flags;
	spinlock_t lock;
#ifdef CONFIG_ZRAM_GS_TRACK_ENTRY_ACTIME
	ktime_t ac_time;
#endif
};

struct zram_stats {
	atomic64_t compr_data_size;	/* compressed size of pages stored */
	atomic64_t failed_reads;	/* can happen when memory is too low */
	atomic64_t failed_writes;	/* can happen when memory is too low */
	atomic64_t notify_free;	/* no. of swap slot free notifications */
	atomic64_t same_pages;		/* no. of same element filled pages */
	atomic64_t huge_pages;		/* no. of huge pages */
	atomic64_t huge_pages_since;	/* no. of huge pages since zram set up */
	atomic64_t pages_stored;	/* no. of pages currently stored */
	atomic_long_t max_used_pages;	/* no. of maximum pages stored */
	atomic64_t writestall;		/* no. of write slow paths */
	atomic64_t miss_free;		/* no. of missed free */
#if IS_ENABLED(CONFIG_ZRAM_GS_SLOWPATH_COMP)
	atomic64_t slowpath_compr_data_size;
	atomic64_t slowpath_pages_stored;
#endif
#ifdef	CONFIG_ZRAM_GS_WRITEBACK
	atomic64_t bd_count;		/* no. of pages in backing device */
	atomic64_t bd_reads;		/* no. of reads from backing device */
	atomic64_t bd_writes;		/* no. of writes from backing device */
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_ANDROID_IOCTL)
	atomic64_t proc_wb_stored_size;
	atomic64_t proc_wb_max_stored_size;
	atomic64_t proc_wb_compr_size;
	atomic64_t proc_wb_max_compr_size;
#endif
};

#ifdef CONFIG_ZRAM_GS_MULTI_COMP
#define ZRAM_PRIMARY_COMP	0U
#define ZRAM_SECONDARY_COMP	1U
#define ZRAM_MAX_COMPS 4U
#if IS_ENABLED(CONFIG_ZRAM_GS_SLOWPATH_COMP)
#define ZRAM_SECONDARY_COMP_END 3U
#define ZRAM_SLOWPATH_COMP	3U
#else
#define ZRAM_SECONDARY_COMP_END 4U
#endif
#else
#define ZRAM_PRIMARY_COMP	0U
#define ZRAM_SECONDARY_COMP	0U
#define ZRAM_SECONDARY_COMP_END 1U
#define ZRAM_MAX_COMPS 1U
#endif

struct zram {
	struct zram_table_entry *table;
	struct zs_pool *mem_pool;
	struct zcomp *comps[ZRAM_MAX_COMPS];
	struct zcomp_params params[ZRAM_MAX_COMPS];
	struct gendisk *disk;
	/* Prevent concurrent execution of device init */
	struct rw_semaphore init_lock;
	/*
	 * the number of pages zram can consume for storing compressed data
	 */
	unsigned long limit_pages;

	struct zram_stats stats;
	/*
	 * This is the limit on amount of *uncompressed* worth of data
	 * we can store in a disk.
	 */
	u64 disksize;	/* bytes */
	const char *comp_algs[ZRAM_MAX_COMPS];
	s8 num_active_comps;
	/*
	 * zram is claimed so open request will be failed
	 */
	bool claim; /* Protected by disk->open_mutex */
#ifdef CONFIG_ZRAM_GS_WRITEBACK
	struct file *backing_dev;
	bool wb_limit_enable;
	bool compressed_wb;
	u32 wb_batch_size;
	u64 bd_wb_limit;
	struct block_device *bdev;
	unsigned long *bitmap;
	unsigned long nr_pages;
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_ANDROID_IOCTL)
	struct xarray prefetch_cache;
#endif
#ifdef CONFIG_ZRAM_GS_MEMORY_TRACKING
	struct dentry *debugfs_dir;
#endif
#if IS_ENABLED(CONFIG_ZRAM_GS_SLOWPATH_COMP)
	u64 free_mem_threshold;
	bool algo_interleave;
	bool slowpath_comp;
#endif
	atomic_t pp_in_progress;
};

bool init_done(struct zram *zram);

#if defined CONFIG_ZRAM_GS_WRITEBACK || defined CONFIG_ZRAM_GS_MULTI_COMP

/*
 * A post-processing bucket is, essentially, a size class, this defines
 * the range (in bytes) of pp-slots sizes in particular bucket.
 */
#define PP_BUCKET_SIZE_RANGE	64
#define NUM_PP_BUCKETS		((PAGE_SIZE / PP_BUCKET_SIZE_RANGE) + 1)

struct zram_pp_ctl {
	struct list_head	pp_buckets[NUM_PP_BUCKETS];
};

struct zram_pp_ctl *init_pp_ctl(void);
void release_pp_ctl(struct zram *zram, struct zram_pp_ctl *ctl);
#endif

#ifdef CONFIG_ZRAM_GS_WRITEBACK
struct zram_wb_ctl {
	/* idle list is accessed only by the writeback task, no concurency */
	struct list_head idle_reqs;
	/* done list is accessed concurrently, protect by done_lock */
	struct list_head done_reqs;
	wait_queue_head_t done_wait;
	spinlock_t done_lock;
	atomic_t num_inflight;
	u64 processed_bytes;
	struct rcu_head rcu;
	bool proc_wb_enabled;
};

struct zram_wb_ctl *init_wb_ctl(struct zram *zram);
void release_wb_ctl(struct zram_wb_ctl *wb_ctl);
int scan_slots_for_writeback(struct zram *zram, u32 mode,
			     unsigned long lo, unsigned long hi,
			     struct zram_pp_ctl *ctl);
int zram_writeback_slots(struct zram *zram,
			 struct zram_pp_ctl *ctl,
			 struct zram_wb_ctl *wb_ctl);
int scan_slot_for_prefetch(struct zram *zram, unsigned long index,
			   struct zram_pp_ctl *ctl);
int zram_prefetch_slots(struct zram *zram, struct zram_pp_ctl *ctl);
#endif

#if IS_ENABLED(CONFIG_ZRAM_GS_ANDROID_IOCTL)
void zram_prefetch_cache_init(struct zram *zram);
void zram_prefetch_cache_destroy(struct zram *zram);
bool zram_prefetch_cache_exist(struct zram *zram, u32 index);
int zram_prefetch_cache_store(struct zram *zram, u32 index,
			      unsigned long blk_idx);
int zram_prefetch_cache_reuse(struct zram *zram, u32 index);
int zram_prefetch_cache_drop(struct zram *zram, u32 index);
#else
static inline void zram_prefetch_cache_init(struct zram *zram) {};
static inline void zram_prefetch_cache_destroy(struct zram *zram) {};
static inline bool zram_prefetch_cache_exist(struct zram *zram, u32 index)
{
	return false;
}

static inline int zram_prefetch_cache_store(struct zram *zram, u32 index,
					    unsigned long blk_idx)
{
	return 0; /* unsupported */
}

static inline int zram_prefetch_cache_reuse(struct zram *zram, u32 index)
{
	return -EOPNOTSUPP;
}

static inline int zram_prefetch_cache_drop(struct zram *zram, u32 index)
{
	return -EOPNOTSUPP;
}
#endif

void zram_slot_lock(struct zram *zram, u32 index);
void zram_slot_unlock(struct zram *zram, u32 index);
void zram_slot_update(struct zram *zram, u32 index, unsigned long handle,
			unsigned int comp_len, u32 prio);
void zram_recompress_slot_update(struct zram *zram, u32 index,
				 unsigned long handle,
				 unsigned int comp_len, u32 prio);

unsigned long zram_get_handle(struct zram *zram, u32 index);
size_t zram_get_obj_size(struct zram *zram, u32 index);
unsigned long zram_get_element(struct zram *zram, u32 index);
bool zram_test_flag(struct zram *zram, u32 index, enum zram_pageflags flag);

struct bio;
void zram_bio_endio(struct zram *zram, struct bio *bio);
void zram_page_write_endio(struct zram *zram, struct page *page, int err);

void zram_accessed(struct zram *zram, u32 index);
int zram_read_page(struct zram *zram, struct page *page, u32 index,
		   struct bio *parent);
bool zram_allocated(struct zram *zram, u32 index);
#endif
