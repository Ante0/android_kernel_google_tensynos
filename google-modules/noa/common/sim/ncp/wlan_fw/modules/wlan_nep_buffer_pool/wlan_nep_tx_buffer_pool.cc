#include "wlan_nep_tx_buffer_pool.h"

#include "wlan_log/wlan_log.h"
#include "common/core.h"
#include "common/compiler.h"
#include "wlan_cast.h"
#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "sys_if/mailbox/sys_if_mailbox.h"
#include "sys_if/memory/sys_if_memory.h"

#define NEP_TX_BUFFER_REPLENISH_RING_SIZE                                                          \
	(round_up((WLAN_NEP_TX_BUFFER_POOL_SIZE + 1), NEP_BUFFER_POOL_CACHED_RING_SIZE))
#define NEP_TX_BUFFER_REPLENISH_RING_BUFFER_SIZE                                                   \
	(sizeof(noa_buffer_pool_desc) * NEP_TX_BUFFER_REPLENISH_RING_SIZE)

static struct WlanNepTxBufferPool *GetWlanNepTxBufferPool(struct WlanNepBufferPool *buffer_pool)
{
	return container_of(buffer_pool, struct WlanNepTxBufferPool, base);
}

static ssize_t WritePayload(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	if (buf_len >= data_len) {
		memcpy(buf, data, data_len);
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, data), data_len);
		return sizeof(data_len);
	}

	return 0;
}

static void TriggerDoorbell(struct noa_ring_wrapper *ring)
{
	(void)ring;
	SysIfNotifyMailbox(kNcpWifiMailboxTypeNep, 0);
}

const static struct noa_ring_ops kTxBufferRefillRingOps = {
	.payload_len = NULL,
	.read_payload = NULL,
	.fill_noop = NULL,
	.write_payload = WritePayload,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = TriggerDoorbell,
};

int32_t WlanNepTxBufferPoolInit(struct WlanNepBufferPool *const buffer_pool)
{
	struct WlanNepTxBufferPool *tx_buffer_pool = GetWlanNepTxBufferPool(buffer_pool);
	struct noa_ring_regs *ring_regs = &buffer_pool->ring_regs;
	struct noa_ring_info ring_info;
	int32_t ret = 0;
	static __attribute__((aligned(NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN))) SEC_EXRAM_DATA uint8_t
		nep_tx_buffer_replenish_ring[NEP_TX_BUFFER_REPLENISH_RING_BUFFER_SIZE];

	memset(ring_regs, 0, sizeof(struct noa_ring_regs));
	memset(&ring_info, 0, sizeof(struct noa_ring_info));

	tx_buffer_pool->refill_ring_buffer =
		WLAN_REINTERPRET_CAST(char *, nep_tx_buffer_replenish_ring);

	if (!tx_buffer_pool->refill_ring_buffer) {
		return -ENOMEM;
	}

	ring_info.head = 0;
	ring_info.tail = 0;
	ring_info.size = NEP_TX_BUFFER_REPLENISH_RING_SIZE;
	ring_info.item_len = sizeof(noa_buffer_pool_desc);
	ring_info.base = WLAN_REINTERPRET_CAST(char *, tx_buffer_pool->refill_ring_buffer);
	ring_info.dpa_base = ring_info.base;

	buffer_pool->buffer_pool_size = WLAN_NEP_TX_BUFFER_POOL_SIZE;
	buffer_pool->pktid_offset = WLAN_NEP_PKTID_MASK;

	do {
		ret = NoaRingSharedRegsGet(ring_regs, kNoaNetworkInterfaceWlan,
					   kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool,
					   kNoaRingNepInput);
		if (ret) {
			WLAN_LOG_WARN(NepBufferPool, "%s(): cannot get buffer pool registers.",
				      __func__);
			break;
		}

		ret = noa_ring_regs_wrapper_init(&buffer_pool->refill_ring, NOA_RING_TYPE_PRODUCER,
						 &kTxBufferRefillRingOps, ring_regs, buffer_pool,
						 "wlan buf", 0);
		if (ret) {
			WLAN_LOG_WARN(NepBufferPool, "%s(): ring wrapper init failed.", __func__);
			break;
		}

		noa_ring_info_setup(&buffer_pool->refill_ring, &ring_info);
		noa_ring_activate(&buffer_pool->refill_ring);
	} while (false);

	return ret;
};

void WlanNepTxBufferPoolDeinit(struct WlanNepBufferPool *const buffer_pool)
{
	struct WlanNepTxBufferPool *tx_buffer_pool = GetWlanNepTxBufferPool(buffer_pool);
	noa_ring_deactivate(&buffer_pool->refill_ring);
	memset(tx_buffer_pool, 0, sizeof(struct WlanNepTxBufferPool));
};

