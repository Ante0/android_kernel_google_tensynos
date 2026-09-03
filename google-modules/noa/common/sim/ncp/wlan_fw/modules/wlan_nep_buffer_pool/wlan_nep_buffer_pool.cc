#include "wlan_nep_buffer_pool.h"

#include "wlan_nep_tx_buffer_pool.h"
#include "wlan_log/wlan_log.h"
#include "wlan_cast.h"
#include "sys_if/types/types.h"
#include "sys_if/common.h"

int32_t WlanNepBufferPoolInit(struct WlanNepBufferPool *const buffer_pool,
			      const enum NepBufferPoolType type)
{
	memset(buffer_pool, 0, sizeof(struct WlanNepBufferPool));

	buffer_pool->buffer_pool_type = type;

	// Hook ops
	if (type == kNepTxBufferPool) {
		buffer_pool->ops.init = WlanNepTxBufferPoolInit;
		buffer_pool->ops.deinit = WlanNepTxBufferPoolDeinit;
	}

	if (buffer_pool->ops.init) {
		return buffer_pool->ops.init(buffer_pool);
	}

	return -EINVAL;
};

void WlanNepBufferPoolDeinit(struct WlanNepBufferPool *const buffer_pool)
{
	noa_ring_info_clean(&buffer_pool->refill_ring);

	if (buffer_pool->ops.deinit) {
		buffer_pool->ops.deinit(buffer_pool);
	}

	memset(buffer_pool, 0, sizeof(struct WlanNepBufferPool));
};

int32_t WlanNepBufferPoolReplenish(struct WlanNepBufferPool *const buffer_pool,
				   const NoaBufferPoolDesc *noa_buffer_desc)
{
	int32_t ret;
	struct noa_ring_wrapper *ring = &buffer_pool->refill_ring;

	do {
		ret = noa_ring_begin_processing(ring);

		if (ret <= 0) {
			WLAN_LOG_WARN(NepBufferPool, "%s(): initiate ring processing failed.",
				      __func__);
			break;
		}

		ret = noa_ring_write(ring, noa_buffer_desc, sizeof(*noa_buffer_desc));

		if (ret < 0) {
			WLAN_LOG_WARN(NepBufferPool, "%s(): write failed with error code: %" PRId32,
				      __func__, ret);
			break;
		}
	} while (false);

	noa_ring_complete_processing(ring);

	return ret < 0 ? -EAGAIN : 0;
};

int32_t WlanNepBufferPoolBatchReplenish(struct WlanNepBufferPool *const buffer_pool,
					uint32_t bm_buffer_num,
					const NoaBufferPoolDesc *noa_buffer_desc_array)
{
	int32_t ret;
	uint32_t i;
	struct noa_ring_wrapper *ring = &buffer_pool->refill_ring;

	ret = noa_ring_begin_processing(ring);

	if (ret <= 0) {
		WLAN_LOG_WARN(NepBufferPool, "%s(): initiate ring processing failed.", __func__);
		goto DONE;
	}

	for (i = 0; i < bm_buffer_num; i++) {
		const NoaBufferPoolDesc *noa_buffer_desc = &noa_buffer_desc_array[i];
		ret = noa_ring_write(ring, noa_buffer_desc, sizeof(*noa_buffer_desc));

		if (ret < 0) {
			WLAN_LOG_WARN(NepBufferPool, "%s(): write failed with error code: %" PRId32,
				      __func__, ret);
		}
	}

	noa_ring_complete_processing(ring);
DONE:
	return ret < 0 ? -EAGAIN : 0;
}

uint32_t WlanNepBufferPoolGetPoolSize(const struct WlanNepBufferPool *const buffer_pool)
{
	return buffer_pool->buffer_pool_size;
}

void WlanNepBufferPoolSetPktidOffset(struct WlanNepBufferPool *const buffer_pool,
				     const uint16_t pktid_offset)
{
	buffer_pool->pktid_offset = pktid_offset;
}

int32_t WlanNepBufferPoolRetrieveBmPktid(struct WlanNepBufferPool *const buffer_pool,
					 uint16_t nep_pktid, uint16_t *bm_pktid)
{
	if (nep_pktid > buffer_pool->pktid_offset) {
		*bm_pktid = nep_pktid - buffer_pool->pktid_offset;
		return 0;
	}

	return -EINVAL;
}
