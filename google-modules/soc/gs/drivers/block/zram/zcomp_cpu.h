/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZCOMP_CPU_H_
#define _ZCOMP_CPU_H_

#include "zcomp.h"

/*
 * Run-time driver context - scratch buffers, etc. It is modified during
 * request execution (compression/decompression), cannot be shared, so
 * it's in per-CPU area.
 */
struct zcomp_ctx {
	void *context;
};

struct zcomp_strm {
	local_lock_t lock;
	/* compression buffer */
	void *buffer;
	struct zcomp_ctx ctx;
};

struct zcomp_req {
	const unsigned char *src;
	const size_t src_len;

	unsigned char *dst;
	size_t dst_len;
};

struct zcomp_cpu_ops {
	int (*compress)(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req);
	int (*decompress)(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req);

	int (*create_ctx)(struct zcomp_params *params, struct zcomp_ctx *ctx);
	void (*destroy_ctx)(struct zcomp_ctx *ctx);

	int (*setup_params)(struct zcomp_params *params);
	void (*release_params)(struct zcomp_params *params);

	const char *name;
};

struct zcomp_cpu {
	struct zcomp_strm __percpu *zstrm;
	const struct zcomp_cpu_ops *ops;
	u32 prio;
};

#endif /* _ZCOMP_CPU_H_ */
