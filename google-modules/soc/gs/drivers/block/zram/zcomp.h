/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZCOMP_H_
#define _ZCOMP_H_

#include <linux/local_lock.h>

struct zcomp;
struct bio;

#define ZCOMP_ALGO_NAME_MAX 64

#define ZCOMP_PARAM_NO_LEVEL	INT_MIN

/*
 * Immutable driver (backend) parameters. The driver may attach private
 * data to it (e.g. driver representation of the dictionary, etc.).
 *
 * This data is kept per-comp and is shared among execution contexts.
 */
struct zcomp_params {
	void *dict;
	size_t dict_sz;
	s32 level;

	void *drv_data;
};

struct zcomp_operation {
	int (*compress)(struct zcomp *comp, u32 index, struct page *page, struct bio *bio);
	int (*compress_async)(struct zcomp *comp, u32 index, struct page *page, struct bio *bio);
	void (*prepare_decompress)(struct zcomp *comp);
	int (*decompress)(struct zcomp *comp, void *src, unsigned int src_len, struct page *page);
	int (*recompress)(struct zcomp *comp, u32 index, struct page *page, u32 prio, u32 threshold);

	int (*create)(struct zcomp *comp, const char *name);
	void (*destroy)(struct zcomp *comp);
};

/* dynamic per-device compression frontend */
struct zcomp {
	struct zram *zram;
	void *private;
	u32 prio;
	const struct zcomp_operation *op;
	struct zcomp_params *params;
	struct list_head list;

	struct hlist_node node;
	char algo_name[ZCOMP_ALGO_NAME_MAX];
};

ssize_t zcomp_available_show(const char *comp, char *buf);
bool zcomp_available_algorithm(const char *comp);

struct zcomp *zcomp_create(const char *comp, struct zcomp_params *params,
			   struct zram *zram, u32 prio);
void zcomp_destroy(struct zcomp *comp);

int zcomp_compress(struct zcomp *comp, u32 index, struct page *page,
			struct bio *bio);
void zcomp_prepare_decompress(struct zcomp *comp);
int zcomp_decompress_buf(struct zcomp *comp, u32 index, void *src,
			 unsigned int size, struct page *page);
int zcomp_decompress(struct zcomp *comp, u32 index, struct page *page);

bool zcomp_has_recompress(const char *algo_name);
int zcomp_recompress(struct zcomp *comp, u32 index, struct page *page,
		     u32 prio, u32 threshold);

int zcomp_register(const char *algo_name, const struct zcomp_operation *operation);
int zcomp_unregister(const char *algo_name);

int zcomp_copy_buffer(void *buffer, int comp_len, struct zram *zram,
		      struct page *page, u32 index, u32 prio);
int zcomp_recompress_copy_buffer(void *buffer, int comp_len_new, struct zram *zram,
				 u32 index, u32 prio, u32 threshold);
size_t get_huge_class_size(void);
#endif /* _ZCOMP_H_ */
