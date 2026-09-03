/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_BM_H__
#define __MTK_BM_H__

#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include "mtk_dev.h"

#define MTK_BM_LOW_PRIO		0
#define MTK_BM_HIGH_PRIO	1

#define MAX_POOL_NAME_SIZE	128
#define MTK_BUFF_RECYCLE	BIT(0)
#define MAX_BUFF_INFO_SIZE	8
#define MAX_USER_CB_SIZE	(sizeof(((struct sk_buff *)0)->cb) - sizeof(struct mtk_buff_info))
#define BUFF_INFO_SKB_CB(__skb)	((struct mtk_buff_info *)&(__skb)->cb[MAX_USER_CB_SIZE])

enum mtk_buff_type {
	MTK_BUFF_SKB = 0,
	MTK_BUFF_PAGE,
	MTK_BUFF_MAX
};

enum mtk_buff_cfg {
	MTK_BUFF_RELOAD = BIT(0),
};

struct mtk_bm_ctrl {
	unsigned int pool_cnt;
	unsigned int pool_seq;
	struct workqueue_struct *pool_reload_workqueue_h;
	struct workqueue_struct *pool_reload_workqueue_l;
	struct kmem_cache *list_cache_pool;
	struct list_head pool_list;
	struct dentry *dentry;
	struct mutex pool_list_mtx;	/* protects the pool list */
	struct mtk_buff_ops *m_ops[MTK_BUFF_MAX];
};

struct mtk_buff {
	struct list_head entry;
	void *data;
};

struct mtk_buff_info {
	u8 flag;
	u8 reserved[7];
} __packed;

union mtk_buff_list {
	struct sk_buff_head skb_list;
	struct list_head buff_list;
};

struct mtk_bm_pool {
	char pool_name[MAX_POOL_NAME_SIZE];
	unsigned int pool_id;
	enum mtk_buff_type type;
	unsigned int threshold;
	unsigned int buff_size;
	unsigned int buff_cnt;
	unsigned int curr_cnt;
	unsigned int prio;
	unsigned int cfg;
	bool dbg_enable;
	atomic_t work_stop;
	spinlock_t lock;		/* protects the buffer operation */
	union mtk_buff_list list;
	struct device *dev;
	struct dentry *pool_dentry;
	struct work_struct reload_work;
	struct workqueue_struct *reload_workqueue;
	struct list_head entry;
	struct mtk_bm_ctrl *bm_ctrl;
	struct mtk_buff_ops *ops;
};

struct mtk_buff_ops {
	int (*pool_create)(struct mtk_bm_pool *pool);
	void (*pool_destroy)(struct mtk_bm_pool *pool);
	void *(*buff_alloc)(struct mtk_bm_pool *pool, int retry_delay_ms);
	void (*buff_free)(struct mtk_bm_pool *pool, void *data);
	void (*pool_reload)(struct work_struct *work);
};

int mtk_bm_init(struct mtk_md_dev *mdev);
int mtk_bm_exit(struct mtk_md_dev *mdev);
__printf(7, 8)
struct mtk_bm_pool *mtk_bm_pool_create(struct mtk_md_dev *mdev,
				       enum mtk_buff_type type, unsigned int buff_size,
				       unsigned int buff_cnt, unsigned int prio,
				       unsigned int cfg, const char *fmt, ...);
int mtk_bm_pool_destroy(struct mtk_md_dev *mdev, struct mtk_bm_pool *pool);
void *mtk_bm_alloc(struct mtk_bm_pool *pool, int retry_delay_ms);
int mtk_bm_free(struct mtk_bm_pool *pool, void *data);
struct sk_buff *mtk_mem_alloc_skb(struct mtk_bm_pool *pool, unsigned int size, int retry_delay_ms);
int mtk_mem_free_skb(struct mtk_bm_pool *pool, struct sk_buff *skb);
#endif /* __MTK_BM_H__ */
