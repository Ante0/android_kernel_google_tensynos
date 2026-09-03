// NOLINTBEGIN
#include "common/compiler.h"
#include "common/core.h"
#include "common/ring.h"

#include "../../wlan_service.h"
#include "../../wlan_service_system.h"
#include "../../wlan_service_mediator.h"
#include "../../wlan_service_option.h"
#include "wlan_nep_ring_adaptee.h"
#include "wlan_ring_adapter.h"

#define WLAN_FW_NEP_INPUT_RING_SIZE 512
#define WLAN_FW_NEP_OUTPUT_RING_SIZE 512

struct noa_input_act {
	uint32_t dst;
	uint32_t head_offset;
	uint32_t data_len;
	uint32_t pktid;
};

static struct {
	int (*nep_ring_activate)(struct noa_ring_wrapper *ring);
	int (*nep_ring_deactivate)(struct noa_ring_wrapper *ring);
	void *(*malloc)(size_t size, dma_addr_t *const dma_addr);
	void (*free)(size_t size, void *const cpu_addr, dma_addr_t *const dma_addr);
	bool (*is_disable_cp)(void);
} plat_ops;

static int wlan_nep_ring_plat_nep_ring_activate(struct noa_ring_wrapper *ring)
{
	if (plat_ops.nep_ring_activate) {
		return plat_ops.nep_ring_activate(ring);
	}
	return -ENODEV;
}

static int wlan_nep_ring_plat_nep_ring_deactivate(struct noa_ring_wrapper *ring)
{
	if (plat_ops.nep_ring_deactivate) {
		return plat_ops.nep_ring_deactivate(ring);
	}
	return -ENODEV;
}

static void *wlan_nep_ring_plat_dma_malloc(struct wlan_ring_wrapper *const adaptee, size_t size)
{
	struct wlan_ring_buffer_info *buffer_info = &adaptee->ext.buffer_info;

	if (plat_ops.malloc) {
		buffer_info->desc_buff_vbase = plat_ops.malloc(size, &buffer_info->desc_buff_pbase);
		if (buffer_info->desc_buff_vbase) {
			buffer_info->size = size;
		}
	}

	return buffer_info->desc_buff_vbase;
}

static void wlan_nep_ring_plat_dma_free(struct wlan_ring_wrapper *const adaptee)
{
	struct wlan_ring_buffer_info *buffer_info = &adaptee->ext.buffer_info;
	if (plat_ops.free) {
		plat_ops.free(buffer_info->size, buffer_info->desc_buff_vbase,
			      &buffer_info->desc_buff_pbase);
		memset((void *)buffer_info, 0, sizeof(struct wlan_ring_buffer_info));
	}
}

__attribute__((unused)) static bool wlan_nep_ring_plat_is_disable_cp(void)
{
	if (plat_ops.is_disable_cp) {
		return plat_ops.is_disable_cp();
	}
	return true;
}

// common
static int wlan_nep_ring_adaptee_init_helper(struct wlan_ring_wrapper *const adaptee,
					     uint32_t nep_ring_type, const uint8_t flow_of_ring,
					     const uint8_t type_of_ring,
					     const uint8_t direction_of_ring,
					     const struct noa_ring_ops *const ops, uint32_t size,
					     uint32_t item_len, const char *const name,
					     void *buff_vbase)
{
	struct noa_ring_regs regs = { 0 };
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.item_len = item_len,
		.size = size,
	};

	if (NoaRingSharedRegsGet(&regs, kNoaNetworkInterfaceWlan, flow_of_ring, type_of_ring,
				 direction_of_ring)) {
		pr_err("Failed to get wlan fw noa ring regs with "
		       "flow %" PRIu8 ", type %" PRIu8 ", direction %" PRIu8 "\n",
		       flow_of_ring, type_of_ring, direction_of_ring);
		return -ENODEV;
	}

	if (noa_ring_regs_wrapper_init(&adaptee->noa_ring, nep_ring_type, ops, &regs, adaptee, name,
				       0) < 0) {
		pr_err("Failed to init noa regs wrapper\n");
		return -ENODEV;
	}

	/*ring sw initial*/
	info.base = (char *)buff_vbase;
	info.dpa_base = info.base;
	spin_lock_init(&adaptee->ext.lock);
	noa_ring_info_setup(&adaptee->noa_ring, &info);

	return 0;
}

SEC_FAST static int wlan_nep_ring_adaptee_begin_processing(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	if (noa_ring_begin_processing(ring) <= 0) {
		return -EAGAIN;
	}

	return 0;
}

SEC_FAST static int
wlan_nep_ring_adaptee_complete_processing(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	noa_ring_complete_processing(ring);

	return 0;
}

static int wlan_nep_ring_adaptee_activate(struct wlan_ring_wrapper *const adaptee, bool active)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	if (active) {
		if (wlan_nep_ring_plat_nep_ring_activate(ring)) {
			pr_err("%s(): Failed to do activate ring", __func__);
			return -ENOEXEC;
		} else {
			noa_ring_activate(ring);
		}
	} else {
		if (wlan_nep_ring_plat_nep_ring_deactivate(ring)) {
			pr_err("%s(): Failed to do deactivate ring", __func__);
			return -ENOEXEC;
		} else {
			noa_ring_deactivate(ring);
		}
	}

	return 0;
}

static void wlan_nep_ring_adaptee_exit(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	// deactivating the ring
	wlan_nep_ring_adaptee_activate(adaptee, false);

	spin_lock(&adaptee->ext.lock);
	noa_ring_info_clean(ring);
	wlan_nep_ring_plat_dma_free(adaptee);
	spin_unlock(&adaptee->ext.lock);
}

static const char *wlan_nep_ring_get_name(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;
	return ring->name;
}

// tx
SEC_FAST static int wlan_nep_tx_ring_adaptee_write(struct wlan_ring_wrapper *const adaptee,
						   void *data, size_t len)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	noa_ring_write(ring, data, len);

	return 0;
}

SEC_FAST static ssize_t wlan_nep_input_desc_write(void *d, size_t buf_len, const void *data,
						  size_t data_len)
{
	const uint8_t neteng_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
	const struct wlan_input_act *input_act = (const struct wlan_input_act *)data;
	struct noa_desc *desc = (struct noa_desc *)d;

	memset((void *)desc, 0, sizeof(struct noa_desc));

	desc->ver = 0;
	desc->ddone = 0;
	desc->dst = input_act->dst;
	desc->dp_low = (uint32_t)((uint32_t)input_act->pa);
	desc->dp_high = (uint32_t)((uint64_t)input_act->pa >> 32 & 0xFFFFFFFF);
	desc->dv = input_act->va;
	desc->tkid = input_act->pktid;
	desc->mode = NOAD_MODE_DATA;
	desc->desc_type = NOA_DESC_BASIC;
	/* define the 802.3 header offset */
	desc->head_offset = input_act->head_offset;
	/* extend data len to include header offset */
	desc->dl = input_act->data_len;
	/* write ring manager control fields depends on dest */
	if (desc->dst == neteng_path) {
		/* request to copy to netengine SRAM */
		desc->cp = wlan_svc_option_get_disable_cp() ? 0 : 1;
		/* request to send feedback event */
		desc->fk = 1;
		desc->reason = FWD_REASON_NETENGINE;
	} else {
		desc->cp = 0;
		desc->fk = 0;
		desc->reason = FWD_REASON_FEEDTHROUGH;
	}

	return NOA_DESC_BASIC_BYTE;
}

static void wlan_nep_trigger_doorbell(struct noa_ring_wrapper *ring)
{
	wlan_svc_med_notify_nep_mailbox(ring);
}

const static struct noa_ring_ops nep_ring_tx_ops = {
	.payload_len = NULL,
	.read_payload = NULL,
	.fill_noop = NULL,
	.write_payload = wlan_nep_input_desc_write,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = wlan_nep_trigger_doorbell,
};

static int wlan_nep_tx_ring_adaptee_init(struct wlan_ring_wrapper *const adaptee,
					 const void *const params)
{
	int ret = -EINVAL;
	void *buf = NULL;

	do {
#ifdef linux
		buf = wlan_nep_ring_plat_dma_malloc(adaptee, NOA_DESC_BASIC_BYTE *
								     WLAN_FW_NEP_INPUT_RING_SIZE);
#else
		static char tx_buffer[NOA_DESC_BASIC_BYTE * WLAN_FW_NEP_INPUT_RING_SIZE] = { 0 };
		buf = (void *)tx_buffer;
#endif /* linux */

		if (!buf) {
			pr_err("%s(): no memory", __func__);
			ret = -ENOMEM;
			break;
		}

		ret = wlan_nep_ring_adaptee_init_helper(
			adaptee, NOA_RING_TYPE_PRODUCER, kNoaNetworkFlowDeviceToHost,
			kNoaWlanRingRxData, kNoaRingNepInput, &nep_ring_tx_ops,
			WLAN_FW_NEP_INPUT_RING_SIZE, NOA_DESC_BASIC_BYTE, "noa_input_ring_2", buf);

		if (ret) {
			pr_err("%s(): wlan_nep_ring_adaptee_init_helper return failed", __func__);
			break;
		}

		ret = wlan_nep_ring_adaptee_activate(adaptee, true);
	} while (0);

	return ret;
}

// rx
SEC_FAST static int wlan_nep_rx_ring_adaptee_read(struct wlan_ring_wrapper *const adaptee,
						  void *data, size_t len)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	return noa_ring_read(ring, data, len);
}

SEC_FAST static int wlan_nep_rx_ring_adaptee_tail_inc(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	noa_ring_tail_inc(ring);

	return 0;
}

SEC_FAST static bool wlan_nep_rx_ring_adaptee_is_empty(struct wlan_ring_wrapper *const adaptee)
{
	struct noa_ring_wrapper *ring = &adaptee->noa_ring;

	return noa_ring_is_empty(ring);
}

const static struct noa_ring_ops nep_ring_rx_ops = {
	.payload_len = NULL,
	.read_payload = noa_generic_read_raw_pointer,
	.fill_noop = NULL,
	.write_payload = NULL,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

static int wlan_nep_rx_ring_adaptee_init(struct wlan_ring_wrapper *const adaptee,
					 const void *const params)
{
	int ret = -EINVAL;
	void *buf = NULL;

	do {
#ifdef linux
		buf = wlan_nep_ring_plat_dma_malloc(adaptee, NOA_DESC_MAX_BYTE *
								     WLAN_FW_NEP_OUTPUT_RING_SIZE);
#else
		static char rx_buffer[NOA_DESC_MAX_BYTE * WLAN_FW_NEP_OUTPUT_RING_SIZE] = { 0 };
		buf = (void *)rx_buffer;
#endif /* linux */

		if (!buf) {
			pr_err("%s(): no memory", __func__);
			ret = -ENOMEM;
			break;
		}

		ret = wlan_nep_ring_adaptee_init_helper(
			adaptee, NOA_RING_TYPE_CONSUMER, kNoaNetworkFlowHostToDevice,
			kNoaWlanRingTxData, kNoaRingNepOutput, &nep_ring_rx_ops,
			WLAN_FW_NEP_OUTPUT_RING_SIZE, NOA_DESC_MAX_BYTE, "noa_output_ring_2", buf);

		if (ret) {
			pr_err("%s(): wlan_nep_ring_adaptee_init_helper return failed", __func__);
			break;
		}

		ret = wlan_nep_ring_adaptee_activate(adaptee, true);
	} while (0);

	return ret;
}

const struct wlan_ring_adaptee_ops *wlan_nep_ring_adaptee_get_tx_ops(void)
{
	SEC_FAST_CONST static struct wlan_ring_adaptee_ops wlan_nep_tx_ring_adaptee_ops = {
		.begin_processing = wlan_nep_ring_adaptee_begin_processing,
		.complete_processing = wlan_nep_ring_adaptee_complete_processing,
		.read = NULL,
		.write = wlan_nep_tx_ring_adaptee_write,
		.tail_inc = NULL,
		.is_empty = NULL,
		.init = wlan_nep_tx_ring_adaptee_init,
		.exit = wlan_nep_ring_adaptee_exit,
		.activate = wlan_nep_ring_adaptee_activate,
		.get_name = wlan_nep_ring_get_name,
		.is_active = NULL,
		.get_hw_idx = NULL,
		.get_write_idx = NULL,
		.get_sn = NULL,
		.sn_inc = NULL,
	};

	return &wlan_nep_tx_ring_adaptee_ops;
}

const struct wlan_ring_adaptee_ops *wlan_nep_ring_adaptee_get_rx_ops(void)
{
	SEC_FAST_CONST static struct wlan_ring_adaptee_ops wlan_nep_rx_ring_adaptee_ops = {
		.begin_processing = wlan_nep_ring_adaptee_begin_processing,
		.complete_processing = wlan_nep_ring_adaptee_complete_processing,
		.read = wlan_nep_rx_ring_adaptee_read,
		.write = NULL,
		.tail_inc = wlan_nep_rx_ring_adaptee_tail_inc,
		.is_empty = wlan_nep_rx_ring_adaptee_is_empty,
		.init = wlan_nep_rx_ring_adaptee_init,
		.exit = wlan_nep_ring_adaptee_exit,
		.activate = wlan_nep_ring_adaptee_activate,
		.get_name = wlan_nep_ring_get_name,
		.is_active = NULL,
		.get_hw_idx = NULL,
		.get_write_idx = NULL,
		.get_sn = NULL,
		.sn_inc = NULL,
	};

	return &wlan_nep_rx_ring_adaptee_ops;
}

int wlan_nep_ring_adaptee_plat_init(struct wlan_nep_ring_adaptee_plat_params *params)
{
	memset((void *)&plat_ops, 0, sizeof(plat_ops));

	plat_ops.nep_ring_activate = params->nep_ring_activate;
	plat_ops.nep_ring_deactivate = params->nep_ring_deactivate;
	plat_ops.malloc = params->malloc;
	plat_ops.free = params->free;

	return 0;
}

// NOLINTEND
