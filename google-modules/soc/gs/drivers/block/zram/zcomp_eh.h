/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZCOMP_EH_H_
#define _ZCOMP_EH_H_

#include <linux/eh.h>
#include <linux/bio.h>

#define BATCH_ZCOMP_REQUEST (128)

/* The 32 is align with SWAP_CLUSTER_MAX and BLK_MAX_REQUEST_COUNT */
#define ZCOMP_BLK_MAX_REQUEST_COUNT 32

/*
 * For compression request, zcomp generates a cookie and pass it to
 * the zcomp instance. The zcomp instance need to call zcomp_copy_buffer
 * with this cookie when it completes the compression.
 */
struct zcomp_cookie {
	struct zram *zram; /* zram instance generated the cookie */
	struct zcomp_eh *zcomp_eh;
	u32 index; /* requested page-sized block index in zram block */
	struct page *page; /* requested page for compression */
	struct bio *bio;
	struct list_head list; /* list for page pool at idle */
			       /* list for pended io at active */
};

struct zcomp_cookie_pool {
	struct list_head head;
	int count;
	spinlock_t lock;
};

struct zcomp_eh {
	struct eh_device *eh_dev;
	struct zcomp_cookie_pool cookie_pool;
	unsigned long pend_request;
	struct list_head request_list;
	u32 prio;
	spinlock_t request_lock;
};

#endif /* _ZCOMP_EH_H_ */
