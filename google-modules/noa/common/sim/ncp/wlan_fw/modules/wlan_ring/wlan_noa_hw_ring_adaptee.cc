// NOLINTBEGIN

#include "../../wlan_service_system.h"
#include "../../wlan_service.h"
#include "wlan_noa_hw_ring_adaptee.h"
#include "wlan_ring_adapter.h"
#include "common/compiler.h"

static inline uint16_t noa_dma_get_write_count_v2(struct noa_hw_ring *ring)
{
	uint16_t cnt = 0;

	if (!ring->regs.read || !ring->regs.write)
		return 0;

	assert(ring->stride > 0);

	ring->read = sys_io_read((uint32_t *)ring->regs.read);
	cnt = _noa_dma_get_write_count(ring->read / ring->stride, ring->write / ring->stride,
				       ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

static inline uint16_t noa_dma_get_read_count_v2(struct noa_hw_ring *ring)
{
	uint16_t cnt = 0;

	if (!ring->regs.read || !ring->regs.write)
		return 0;

	assert(ring->stride > 0);

	ring->write = sys_io_read((uint32_t *)ring->regs.write);
	ring->read = sys_io_read((uint32_t *)ring->regs.read);
	cnt = _noa_dma_get_read_count(ring->read / ring->stride, ring->write / ring->stride,
				      ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

static inline void noa_dma_update_sw_write_v2(struct noa_hw_ring *ring)
{
	assert(ring->stride > 0);
	ring->write = (ring->write + ring->stride) % (ring->ndesc * ring->stride);
}

static inline void noa_dma_update_sw_read_v2(struct noa_hw_ring *ring)
{
	assert(ring->stride > 0);
	ring->read = (ring->read + ring->stride) % (ring->ndesc * ring->stride);
}

static inline uint8_t *noa_dma_get_write_base_v2(struct noa_hw_ring *ring)
{
	assert(ring->stride > 0);
	return (uint8_t *)ring->desc + (ring->write / ring->stride) * ring->desc_sz;
}

static inline uint8_t *noa_dma_get_read_base_v2(struct noa_hw_ring *ring)
{
	assert(ring->stride > 0);
	return (uint8_t *)ring->desc + (ring->read / ring->stride) * ring->desc_sz;
}

static struct {
	void *(*malloc)(size_t size, dma_addr_t *const dma_addr);
	void (*free)(size_t size, void *const cpu_addr, dma_addr_t *const dma_addr);
} plat_ops;

static void *wlan_noa_hw_ring_plat_dma_malloc(struct wlan_ring_wrapper *const adaptee, size_t size)
{
	struct wlan_ring_buffer_info *buffer_info = &adaptee->ext.buffer_info;

	memset((void *)buffer_info, 0, sizeof(struct wlan_ring_buffer_info));

	if (plat_ops.malloc) {
		buffer_info->desc_buff_vbase = plat_ops.malloc(size, &buffer_info->desc_buff_pbase);
		if (buffer_info->desc_buff_vbase) {
			buffer_info->size = size;
		}
	}

	return buffer_info->desc_buff_vbase;
}

static void wlan_noa_hw_ring_plat_dma_free(struct wlan_ring_wrapper *const adaptee)
{
	struct wlan_ring_buffer_info *buffer_info = &adaptee->ext.buffer_info;
	if (plat_ops.free) {
		plat_ops.free(buffer_info->size, buffer_info->desc_buff_vbase,
			      &buffer_info->desc_buff_pbase);
		memset((void *)buffer_info, 0, sizeof(struct wlan_ring_buffer_info));
	}
}

// common
#define INIT_SN 1
static int wlan_noa_hw_ring_init(struct wlan_ring_wrapper *const adaptee, const void *const params)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;
	const struct noa_wlan_ring_info *ring_info = (const struct noa_wlan_ring_info *)params;
	uint32_t ring_buf_size = ring_info->ndesc * ring_info->desc_sz;

	ring->read = 0;
	ring->write = 0;
	ring->sn = INIT_SN;
	ring->hw_idx = ring_info->hw_idx;
	ring->ndesc = ring_info->ndesc;
	ring->desc_sz = ring_info->desc_sz;
	ring->stride = ring_info->stride;
	if (ring_info->dma_va) {
		ring->desc = (void *)ring_info->dma_va;
	} else {
		ring->desc = wlan_noa_hw_ring_plat_dma_malloc(adaptee, ring_buf_size);
	}
	memcpy(&ring->regs, &ring_info->regs, sizeof(struct noa_ring_regs));
	strncpy(ring->name, ring_info->name, sizeof(ring->name));

	if (!ring->desc) {
		pr_err("%s() desc allocate fall! %p, size %lu\n", __func__, ring->desc,
		       (unsigned long)ring_buf_size);
		return -ENOMEM;
	}

	return 0;
}

static int wlan_noa_hw_ring_activate(struct wlan_ring_wrapper *const adaptee, bool active)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	if (active) {
		ring->flags |= BIT(TX_RING_FLAG_ACTIVE);
	} else {
		ring->flags &= ~BIT(TX_RING_FLAG_ACTIVE);
		/* reinit txring index to sync hardware setting after queue reseted */
		ring->read = 0;
		ring->write = 0;
	}

	return 0;
}

static const char *wlan_noa_hw_ring_get_name(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	return ring->name;
}

static bool wlan_noa_hw_ring_is_avtive(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	return ring->flags & BIT(TX_RING_FLAG_ACTIVE);
}

static void wlan_noa_hw_ring_exit(struct wlan_ring_wrapper *const adaptee)
{
	wlan_noa_hw_ring_activate(adaptee, false);
	wlan_noa_hw_ring_plat_dma_free(adaptee);
}

// tx
SEC_FAST static int
wlan_noa_hw_tx_ring_adaptee_begin_processing(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;
	adaptee->ext.available_cnt = noa_dma_get_write_count_v2(ring);
	return 0;
}

SEC_FAST static int
wlan_noa_hw_tx_ring_adaptee_complete_processing(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	noa_dma_update_hw_write(ring);

	return 0;
}

SEC_FAST static int wlan_noa_hw_tx_ring_adaptee_write(struct wlan_ring_wrapper *const adaptee,
						      void *data, size_t len)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	if (data == NULL || ring->desc_sz < len) {
		return -EINVAL;
	}

	if (adaptee->ext.available_cnt) {
		void *desc = noa_dma_get_write_base_v2(ring);
		memcpy(desc, data, len);
		noa_dma_update_sw_write_v2(ring);
		adaptee->ext.available_cnt--;
	} else {
		return -EAGAIN;
	}

	return 0;
}

SEC_FAST static uint32_t wlan_noa_hw_tx_ring_get_hw_idx(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	return ring->hw_idx;
}

SEC_FAST static uint32_t wlan_noa_hw_tx_ring_get_write_idx(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	return ring->write;
}

// rx
SEC_FAST static int
wlan_noa_hw_rx_ring_adaptee_begin_processing(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;
	adaptee->ext.available_cnt = noa_dma_get_read_count_v2(ring);
	return 0;
}

SEC_FAST static int
wlan_noa_hw_rx_ring_adaptee_complete_processing(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	noa_dma_update_hw_read(ring);

	return 0;
}

SEC_FAST static int wlan_noa_hw_rx_ring_adaptee_tail_inc(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	noa_dma_update_sw_read_v2(ring);

	return 0;
}

SEC_FAST static int
wlan_noa_hw_rx_ring_adaptee_tail_rollback(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	ring->read = (ring->read - 1 + ring->ndesc) % ring->ndesc;

	return 0;
}

SEC_FAST static bool wlan_noa_hw_rx_ring_adaptee_is_empty(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	return !(noa_dma_get_read_count_v2(ring) > 0);
}

SEC_FAST static int wlan_noa_hw_rx_ring_adaptee_read(struct wlan_ring_wrapper *const adaptee,
						     void *data, size_t len)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;
	(void)len;

	if (data == NULL) {
		pr_err("%s(): invalid buffer\n", __func__);
		return -EINVAL;
	}

	if (adaptee->ext.available_cnt) {
		*(void **)data = (void *)noa_dma_get_read_base_v2(ring);
		noa_dma_update_sw_read_v2(ring);
		adaptee->ext.available_cnt--;
	} else {
		return 0;
	}

	return ring->desc_sz;
}

SEC_FAST static uint32_t wlan_noa_hw_ring_get_sn(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;

	return ring->sn;
}

SEC_FAST static void wlan_noa_hw_ring_sn_inc(struct wlan_ring_wrapper *const adaptee,
					     uint32_t divisor)
{
	struct noa_hw_ring *ring = &adaptee->hw_ring;
	ring->sn = (ring->sn + 1) % divisor;
}

const struct wlan_ring_adaptee_ops *wlan_noa_hw_ring_adaptee_get_tx_ops(void)
{
	SEC_FAST_CONST static struct wlan_ring_adaptee_ops wlan_noa_hw_tx_ring_adaptee_ops = {
		.begin_processing = wlan_noa_hw_tx_ring_adaptee_begin_processing,
		.complete_processing = wlan_noa_hw_tx_ring_adaptee_complete_processing,
		.read = NULL,
		.write = wlan_noa_hw_tx_ring_adaptee_write,
		.tail_inc = NULL,
		.tail_rollback = NULL,
		.is_empty = NULL,
		.init = wlan_noa_hw_ring_init,
		.exit = wlan_noa_hw_ring_exit,
		.activate = wlan_noa_hw_ring_activate,
		.get_name = wlan_noa_hw_ring_get_name,
		.is_active = wlan_noa_hw_ring_is_avtive,
		.get_hw_idx = wlan_noa_hw_tx_ring_get_hw_idx,
		.get_write_idx = wlan_noa_hw_tx_ring_get_write_idx,
		.get_sn = wlan_noa_hw_ring_get_sn,
		.sn_inc = wlan_noa_hw_ring_sn_inc,
	};

	return &wlan_noa_hw_tx_ring_adaptee_ops;
}

const struct wlan_ring_adaptee_ops *wlan_noa_hw_ring_adaptee_get_rx_ops(void)
{
	SEC_FAST_CONST static struct wlan_ring_adaptee_ops wlan_noa_hw_rx_ring_adaptee_ops = {
		.begin_processing = wlan_noa_hw_rx_ring_adaptee_begin_processing,
		.complete_processing = wlan_noa_hw_rx_ring_adaptee_complete_processing,
		.read = wlan_noa_hw_rx_ring_adaptee_read,
		.write = NULL,
		.tail_inc = wlan_noa_hw_rx_ring_adaptee_tail_inc,
		.tail_rollback = wlan_noa_hw_rx_ring_adaptee_tail_rollback,
		.is_empty = wlan_noa_hw_rx_ring_adaptee_is_empty,
		.init = wlan_noa_hw_ring_init,
		.exit = wlan_noa_hw_ring_exit,
		.activate = wlan_noa_hw_ring_activate,
		.get_name = wlan_noa_hw_ring_get_name,
		.is_active = wlan_noa_hw_ring_is_avtive,
		.get_hw_idx = NULL,
		.get_write_idx = NULL,
		.get_sn = wlan_noa_hw_ring_get_sn,
		.sn_inc = wlan_noa_hw_ring_sn_inc,
	};

	return &wlan_noa_hw_rx_ring_adaptee_ops;
}

int wlan_noa_hw_ring_adaptee_plat_init(struct wlan_noa_hw_ring_adaptee_plat_params *params)
{
	memset(&plat_ops, 0, sizeof(plat_ops));
	plat_ops.malloc = params->malloc;
	plat_ops.free = params->free;
	return 0;
}

// NOLINTEND
