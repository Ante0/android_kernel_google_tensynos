#include "wlan_buffer_manager.h"

#include "wlan_cast.h"
#include "wlan_log/wlan_log.h"
#include "common/compiler.h"

#define VENDOR_RX_LOCKER_NUM 8193U
#define NOA_TX_LOCKER_NUM 8193U

static SEC_EXRAM_DATA struct LockerPool {
	BmTkidItem vendor_rx_locker_pool[VENDOR_RX_LOCKER_NUM];
	BmTkidItem noa_tx_locker_pool[NOA_TX_LOCKER_NUM];
} g_locker_pool;

SEC_FAST_DATA const static struct LockerInfo {
	BmTkidItem *lockers;
	uint32_t pool_size;
} g_locker_infos[kBufferManagerTypeNum] = {
	[kVendorRxBufferManager] = {
		.lockers = g_locker_pool.vendor_rx_locker_pool,
		.pool_size = VENDOR_RX_LOCKER_NUM,
	},
	[kNoaTxBufferManager] = {
		.lockers = g_locker_pool.noa_tx_locker_pool,
		.pool_size = NOA_TX_LOCKER_NUM,
	},
};

static BmTkidItem *GetLockerPool(BufferManagerType bm_type)
{
	if (bm_type < kBufferManagerTypeEnd && bm_type >= kBufferManagerTypeStart)
		return g_locker_infos[bm_type].lockers;
	return NULL;
}

static uint32_t GetLockerPoolSize(BufferManagerType bm_type)
{
	if (bm_type < kBufferManagerTypeEnd && bm_type >= kBufferManagerTypeStart)
		return g_locker_infos[bm_type].pool_size;
	return 0;
}

static BmTkidItem *GetAndValidateLocker(BufferManagerType bm_type, uint16_t tkid)
{
	BmTkidItem *lockers = GetLockerPool(bm_type);
	uint32_t pool_size = GetLockerPoolSize(bm_type);

	if (!lockers) {
		return NULL;
	}

	if (tkid >= pool_size) {
		WLAN_LOG_ERROR(Bm, "%s(): invalid tkid: %u (pool size: %u)", __func__, tkid,
			       pool_size);
		return NULL;
	}

	return &lockers[tkid];
}

int32_t BmInit(BufferManagerType bm_type)
{
	uint32_t pool_size = GetLockerPoolSize(bm_type);
	BmTkidItem *lockers = GetLockerPool(bm_type);
	uint32_t i = 0;

	if (lockers) {
		memset(lockers, 0, sizeof(BmTkidItem) * pool_size);
		for (i = 0; i < pool_size; i++) {
			lockers[i].state = kLockerStateAvailable;
		}
	}

	return 0;
}

void BmDeinit(BufferManagerType bm_type)
{
	uint32_t lockers_size_in_byte = sizeof(BmTkidItem) * GetLockerPoolSize(bm_type);
	BmTkidItem *lockers = GetLockerPool(bm_type);

	if (lockers)
		memset(lockers, 0, lockers_size_in_byte);
}

int32_t BmFind(BufferManagerType bm_type, uint16_t tkid, const BmTkidItem **buf_info)
{
	BmTkidItem *locker = GetAndValidateLocker(bm_type, tkid);

	if (!locker) {
		return -EINVAL;
	}

	if (locker->state != kLockerStateOccupied) {
		return -ENOMEM;
	}

	*buf_info = locker;
	return 0;
}

int32_t BmAcquire(BufferManagerType bm_type, uint16_t tkid, uint16_t buf_size, uint64_t pa,
		  uint64_t va)
{
	BmTkidItem *locker = GetAndValidateLocker(bm_type, tkid);

	if (locker) {
		if (locker->state == kLockerStateAvailable) {
			locker->tkid = tkid;
			locker->size = buf_size;
			locker->pa = pa;
			locker->va = va;
			locker->state = kLockerStateOccupied;
			return 0;
		} else {
			WLAN_LOG_ERROR(Bm, "%s(): duplicate TKID %u detected during registration.",
				       __func__, tkid);
		}
	} else {
		WLAN_LOG_ERROR(Bm, "%s(): invalid tkid: %u", __func__, tkid);
	}

	return -EINVAL;
}

void BmRelease(BufferManagerType bm_type, uint16_t tkid)
{
	BmTkidItem *locker = GetAndValidateLocker(bm_type, tkid);

	if (locker) {
		locker->state = kLockerStateAvailable;
	}
}

int32_t BmPlaceOnHold(BufferManagerType bm_type, uint16_t tkid)
{
	BmTkidItem *locker = GetAndValidateLocker(bm_type, tkid);

	if (!locker) {
		return -EINVAL;
	}

	if (locker->state != kLockerStateOccupied) {
		return -EINVAL;
	}

	locker->state = kLockerStateOnHold;
	return 0;
}

int32_t BmReactivate(BufferManagerType bm_type, uint16_t tkid)
{
	BmTkidItem *locker = GetAndValidateLocker(bm_type, tkid);

	if (!locker) {
		return -EINVAL;
	}

	if (locker->state != kLockerStateOnHold) {
		return -EINVAL;
	}

	locker->state = kLockerStateOccupied;
	return 0;
}
