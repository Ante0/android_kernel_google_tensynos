// SPDX-License-Identifier: GPL-2.0-only

#include <dt-bindings/interconnect/google,icc.h>

#include <linux/interconnect.h>
#include <linux/limits.h>
#include <linux/mutex.h>

#include <interconnect/google_icc_helper.h>

#include "google_icc_vote.h"
#include "google_icc_provider.h"

#include "soc/google/google_gtc.h"

#define CREATE_TRACE_POINTS
#include "google_icc_trace.h"

struct google_icc_path {
	struct icc_path *path;
	/* protect calling sequences in google_icc_*() */
	struct mutex mutex;
	struct icc_vote vote;
};

struct google_icc_path *google_devm_of_icc_get(struct device *dev, const char *name)
{
	struct google_icc_path *p;
	struct google_icc_path *err_ret;
	long errno;
	struct icc_vote_block *block;
	size_t type, rw, vc;
	struct icc_path *path;
	u32 num_vc = google_icc_get_num_vc();

	path = devm_of_icc_get(dev, name);
	if (IS_ERR(path)) {
		errno = PTR_ERR(path);
		dev_err(dev, "devm_of_icc_get(%s) failed, ret = %ld\n", name, errno);
		err_ret = ERR_PTR(errno);
		goto err;
	} else if (!path) {
		err_ret = NULL;
		goto err;
	}

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p) {
		err_ret = ERR_PTR(-ENOMEM);
		goto err_icc_put;
	}

	p->path = path;

	for (type = ICC_TYPE_GMC; type < NUM_ICC_TYPE; type++) {
		for (rw = ICC_READ; rw < NUM_ICC_RW; rw++) {
			block = &p->vote.block[type][rw];

			block->latency = U32_MAX;
			block->ltv = U32_MAX;

			for (vc = 0; vc < num_vc; vc++) {
				block->latency_vc[vc] = U32_MAX;
				block->ltv_vc[vc] = U32_MAX;
			}
		}
	}

	mutex_init(&p->mutex);

	return p;

	devm_kfree(dev, p);
err_icc_put:
	icc_put(path);
err:
	return err_ret;
}
EXPORT_SYMBOL_GPL(google_devm_of_icc_get);

int google_icc_set_read_latency_gmc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GMC][ICC_READ];

	block->latency_vc[vc] = latency;
	block->ltv_vc[vc] = ltv;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_read_latency_gmc);

int google_icc_set_read_latency_gslc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GSLC][ICC_READ];

	block->latency_vc[vc] = latency;
	block->ltv_vc[vc] = ltv;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_read_latency_gslc);

int google_icc_set_write_latency_gmc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GMC][ICC_WRITE];

	block->latency_vc[vc] = latency;
	block->ltv_vc[vc] = ltv;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_write_latency_gmc);

int google_icc_set_write_latency_gslc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GSLC][ICC_WRITE];

	block->latency_vc[vc] = latency;
	block->ltv_vc[vc] = ltv;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_write_latency_gslc);

int google_icc_set_read_bw_gmc(struct google_icc_path *p, u32 avg_bw,
			       u32 peak_bw, u32 rt_bw, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GMC][ICC_READ];

	block->avg_bw_vc[vc] = (u64)avg_bw;
	block->peak_bw_vc[vc] = (u64)peak_bw;
	block->rt_bw_vc[vc] = (u64)rt_bw;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_read_bw_gmc);

int google_icc_set_read_bw_gslc(struct google_icc_path *p, u32 avg_bw,
				u32 peak_bw, u32 rt_bw, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GSLC][ICC_READ];

	block->avg_bw_vc[vc] = (u64)avg_bw;
	block->peak_bw_vc[vc] = (u64)peak_bw;
	block->rt_bw_vc[vc] = (u64)rt_bw;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_read_bw_gslc);

int google_icc_set_write_bw_gmc(struct google_icc_path *p, u32 avg_bw,
				u32 peak_bw, u32 rt_bw, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GMC][ICC_WRITE];

	block->avg_bw_vc[vc] = (u64)avg_bw;
	block->peak_bw_vc[vc] = (u64)peak_bw;
	block->rt_bw_vc[vc] = (u64)rt_bw;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_write_bw_gmc);

int google_icc_set_write_bw_gslc(struct google_icc_path *p, u32 avg_bw,
				 u32 peak_bw, u32 rt_bw, u8 vc)
{
	struct icc_vote_block *block;

	if (!p)
		return -EINVAL;

	if (vc >= google_icc_get_num_vc())
		return -EINVAL;

	mutex_lock(&p->mutex);

	block = &p->vote.block[ICC_TYPE_GSLC][ICC_WRITE];

	block->avg_bw_vc[vc] = (u64)avg_bw;
	block->peak_bw_vc[vc] = (u64)peak_bw;
	block->rt_bw_vc[vc] = (u64)rt_bw;

	mutex_unlock(&p->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(google_icc_set_write_bw_gslc);

static inline void addr_encode(void *ptr, u32 *hi, u32 *lo)
{
	u64 addr = (u64)ptr;

	*hi = (u32)((addr & 0xffffffff00000000) >> 32);
	*lo = (u32)((addr & 0x00000000ffffffff) >> 0);
}

static int __google_icc_update_constraint(struct google_icc_path *p,
					  u32 _vote_sync_type)
{
	u32 hi, lo;
	int ret;

	if (!p || !p->path)
		return -EINVAL;

	addr_encode(&p->vote, &hi, &lo);

	mutex_lock(&p->mutex);

	p->vote.vote_sync_type = _vote_sync_type;

	ret = icc_set_bw(p->path, hi, lo);

	p->vote.vote_sync_type = 0;

	mutex_unlock(&p->mutex);

	return ret;
}

int google_icc_update_constraint_async(struct google_icc_path *p)
{
	int ret = 0;

	trace_google_icc_event(TPS("async_update begin"), goog_gtc_get_counter());

	ret = __google_icc_update_constraint(p, GOOGLE_ICC_UPDATE_ASYNC);

	trace_google_icc_event_with_ret(TPS("async_update end"), ret, goog_gtc_get_counter());

	return ret;
}
EXPORT_SYMBOL_GPL(google_icc_update_constraint_async);


int google_icc_update_constraint(struct google_icc_path *p)
{
	int ret = 0;

	trace_google_icc_event(TPS("sync_update begin"), goog_gtc_get_counter());

	ret = __google_icc_update_constraint(p, GOOGLE_ICC_UPDATE_SYNC);

	trace_google_icc_event_with_ret(TPS("sync_update end"), ret, goog_gtc_get_counter());

	return ret;
}
EXPORT_SYMBOL_GPL(google_icc_update_constraint);
