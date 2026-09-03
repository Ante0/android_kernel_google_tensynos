#include "noa_ring_svc.h"

#include "ext_svc/ext_svc.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "wlan_cast.h"
#include "wlan_log/wlan_log.h"

#define RING_MAX_NAME (24U)

static_assert(NUM_RING_SVC_TX_POST_RING > 0U);
static_assert(NUM_RING_SVC_RX_CMPL_RING > 0U);

// This ring buffer must reside in SRAM:
// 1. NEP cannot access DTCM.
// 2. Avoids the performance overhead of DRAM access.
static uint8_t tx_post_ring_buffer[NOA_DESC_WLAN_RX_BYTE * WLAN_FW_NEP_INPUT_RING_SIZE];
static uint8_t rx_cmpl_ring_buffer[NOA_DESC_MAX_BYTE * WLAN_FW_NEP_INPUT_RING_SIZE];

static ssize_t NoaRingReadCallback(const void *data, size_t data_len, struct noa_iovec *iov)
{
	if (data && iov) {
		*((uintptr_t *)iov->base) = (uintptr_t)data;
		iov->len = data_len;
		return data_len;
	}

	return 0;
}

static ssize_t NoaRingWriteCallback(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	if (buf && data && data_len <= buf_len) {
		memcpy(buf, data, data_len);
		return data_len;
	} else {
		WLAN_LOG_ERROR(Cfg, "%s(): invalid inputs.", __func__);
	}

	return 0;
}

static struct noa_ring_ops g_noa_ring_ops = {
	.payload_len = NULL,
	.read_payload = NoaRingReadCallback,
	.fill_noop = NULL,
	.write_payload = NoaRingWriteCallback,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

static int32_t NoaRingTxPostRingsInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	struct noa_ring_info ring_info;
	const uint8_t flow = kNoaNetworkFlowDeviceToHost;
	const uint8_t type = kNoaWlanRingRxData;
	const uint8_t nep_direction = kNoaRingNepInput;
	ring = &ring_svc->ring_svc_tx_post_ring_pool[0];

	if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlan, flow, type, nep_direction) !=
	    0) {
		WLAN_LOG_ERROR(Cfg,
			       "%s(): failed to get wlan fw noa ring regs with "
			       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
			       __func__, flow, type, nep_direction);
		return -ENODEV;
	}

	if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_PRODUCER, &g_noa_ring_ops, &ring_regs,
				       ring, "wlan_fw_txpost_ring_0", 0) < 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
		return -ENODEV;
	}

	// Ring sw initial.
	memset(&ring_info, 0, sizeof(ring_info));
	ring_info.base = WLAN_REINTERPRET_CAST(char *, tx_post_ring_buffer);
	ring_info.dpa_base = ring_info.base;
	ring_info.item_len = NOA_DESC_WLAN_RX_BYTE;
	ring_info.size = WLAN_FW_NEP_INPUT_RING_SIZE;
	noa_ring_info_setup(ring, &ring_info);
	noa_ring_activate(ring);

	return 0;
}

static void NoaRingTxPostRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring = &ring_svc->ring_svc_tx_post_ring_pool[0];

	noa_ring_deactivate(ring);
	noa_ring_info_clean(ring);
}

static int32_t NoaRingRxCmplRingsInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	struct noa_ring_info ring_info;
	const uint8_t flow = kNoaNetworkFlowHostToDevice;
	const uint8_t type = kNoaWlanRingTxData;
	const uint8_t nep_direction = kNoaRingNepOutput;
	ring = &ring_svc->ring_svc_rx_cmpl_ring_pool[0];

	if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlan, flow, type, nep_direction) !=
	    0) {
		WLAN_LOG_ERROR(Cfg,
			       "%s(): failed to get wlan fw noa ring regs with "
			       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
			       __func__, flow, type, nep_direction);
		return -ENODEV;
	}

	if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_CONSUMER, &g_noa_ring_ops, &ring_regs,
				       ring, "wlan_fw_rxcmpl_ring_0", 0) < 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
		return -ENODEV;
	}

	// Ring sw initial.
	memset(&ring_info, 0, sizeof(ring_info));
	ring_info.base = WLAN_REINTERPRET_CAST(char *, rx_cmpl_ring_buffer);
	ring_info.dpa_base = ring_info.base;
	ring_info.item_len = NOA_DESC_MAX_BYTE;
	ring_info.size = WLAN_FW_NEP_OUTPUT_RING_SIZE;
	noa_ring_info_setup(ring, &ring_info);
	noa_ring_activate(ring);

	return 0;
}

static void NoaRingRxCmplRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring = &ring_svc->ring_svc_rx_cmpl_ring_pool[0];

	noa_ring_deactivate(ring);
	noa_ring_info_clean(ring);
}

static int32_t NoaRingDirectTxPostRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowHostToDevice;
	const uint8_t type = kNoaWlanRingTxData;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_TX_POST_RING; i++) {
		ring = &ring_svc->ring_svc_direct_tx_ring_pool[0];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type, nep_direction);
			return -ENODEV;
		}

		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_CONSUMER, &g_noa_ring_ops,
					       &ring_regs, ring, "wlan_fw_direct_tx_ring_0",
					       0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectTxPostRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_TX_POST_RING; i++) {
		ring = &ring_svc->ring_svc_direct_tx_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

static int32_t NoaRingDirectRxCmplRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowDeviceToHost;
	const uint8_t type = kNoaWlanDirectD2HRingRxData;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_RX_CMPL_RING; i++) {
		ring = &ring_svc->ring_svc_direct_rx_ring_pool[i];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type, nep_direction);
			return -ENODEV;
		}

		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_PRODUCER, &g_noa_ring_ops,
					       &ring_regs, ring, "wlan_fw_direct_rx_ring_0",
					       0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectRxCmplRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_RX_CMPL_RING; i++) {
		ring = &ring_svc->ring_svc_direct_rx_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

static int32_t NoaRingDirectTxCmplRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowDeviceToHost;
	const uint8_t type = kNoaWlanDirectD2HRingTxCpl;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;
	char name[RING_MAX_NAME];

	for (i = 0; i < NUM_RING_SVC_DIRECT_TX_CMPL_RING; i++) {
		ring = &ring_svc->ring_svc_direct_tx_cpl_ring_pool[i];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type + i,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type + i, nep_direction);
			return -ENODEV;
		}

		snprintf(name, RING_MAX_NAME, "wlan_fw_txcpl_%d", i);
		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_PRODUCER, &g_noa_ring_ops,
					       &ring_regs, ring, name, 0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectTxCmplRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_TX_CMPL_RING; i++) {
		ring = &ring_svc->ring_svc_direct_tx_cpl_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

static int32_t NoaRingDirectRxFallbackRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowDeviceToHost;
	const uint8_t type = kNoaWlanDirectD2HRingRxFallback;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_RX_FALLBACK_RING; i++) {
		ring = &ring_svc->ring_svc_direct_rx_fallback_ring_pool[i];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type, nep_direction);
			return -ENODEV;
		}

		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_PRODUCER, &g_noa_ring_ops,
					       &ring_regs, ring, "wlan_fw_rx_fallback_ring",
					       0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectRxFallbackRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_RX_FALLBACK_RING; i++) {
		ring = &ring_svc->ring_svc_direct_rx_fallback_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

static int32_t NoaRingDirectVendorRxReplenishRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowHostToDevice;
	const uint8_t type = kNoaWlanDirectH2DRingVendorRxBufferReplenish;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_VENDOR_RX_REPLENISH_RING; i++) {
		ring = &ring_svc->ring_svc_direct_vendor_rx_replenish_ring_pool[0];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type, nep_direction);
			return -ENODEV;
		}

		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_CONSUMER, &g_noa_ring_ops,
					       &ring_regs, ring, "wlan_apc_vendor_rx_buf_repln",
					       0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectVendorRxReplenishRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_VENDOR_RX_REPLENISH_RING; i++) {
		ring = &ring_svc->ring_svc_direct_vendor_rx_replenish_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

static int32_t NoaRingDirectNoaTxReplenishRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowHostToDevice;
	const uint8_t type = kNoaWlanDirectH2DRingNoaTxBufferReplenish;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_NOA_TX_REPLENISH_RING; i++) {
		ring = &ring_svc->ring_svc_direct_noa_tx_replenish_ring_pool[i];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type, nep_direction);
			return -ENODEV;
		}

		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_CONSUMER, &g_noa_ring_ops,
					       &ring_regs, ring, "wlan_apc_noa_tx_buf_repln",
					       0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectNoaTxReplenishRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_NOA_TX_REPLENISH_RING; i++) {
		ring = &ring_svc->ring_svc_direct_noa_tx_replenish_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

static int32_t NoaRingDirectFeedbackRingInit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	struct noa_ring_regs ring_regs;
	const uint8_t flow = kNoaNetworkFlowHostToDevice;
	const uint8_t type = kNoaWlanDirectH2DRingFeedback;
	const uint8_t nep_direction = kNoaNepRingAnyDirection;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_FEEDBACK_RING; i++) {
		ring = &ring_svc->ring_svc_feedback_ring_pool[i];

		if (NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlanDirect, flow, type,
					 nep_direction) != 0) {
			WLAN_LOG_ERROR(Cfg,
				       "%s(): failed to get wlan fw noa ring regs with "
				       "flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16,
				       __func__, flow, type, nep_direction);
			return -ENODEV;
		}

		if (noa_ring_regs_wrapper_init(ring, NOA_RING_TYPE_CONSUMER, &g_noa_ring_ops,
					       &ring_regs, ring, "wlan_apc_feedback", 0) < 0) {
			WLAN_LOG_ERROR(Cfg, "%s(): failed to init noa regs wrapper.", __func__);
			return -ENODEV;
		}

		noa_ring_activate(ring);
		ring->basic.base = ring->basic.dpa_base;
	}

	return 0;
}

static void NoaRingDirectFeedbackRingDeinit(NoaRingSvc *const ring_svc)
{
	NepRing *ring;
	uint32_t i = 0;

	for (i = 0; i < NUM_RING_SVC_DIRECT_FEEDBACK_RING; i++) {
		ring = &ring_svc->ring_svc_feedback_ring_pool[i];
		noa_ring_deactivate(ring);
	}
}

int32_t NoaRingSvcInit(NoaRingSvc *const ring_svc, NoaRingSvcInitParams *const params)
{
	if (!ring_svc || !params) {
		return -EINVAL;
	}

	ring_svc->ext_svc = params->ext_svc;

	if (NoaRingTxPostRingsInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingTxPostRingsInit failed.", __func__);
		goto TX_POST_RING_INIT_FAILED;
	}

	if (NoaRingRxCmplRingsInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingRxCmplRingsInit failed.", __func__);
		goto RX_CMPL_RING_INIT_FAILED;
	}

	if (NoaRingDirectTxPostRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectTxPostRingsInit failed.", __func__);
		goto DIRECT_TX_RING_INIT_FAILED;
	}

	if (NoaRingDirectRxCmplRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectRxCmplRingsInit failed.", __func__);
		goto DIRECT_RX_RING_INIT_FAILED;
	}

	if (NoaRingDirectTxCmplRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectTxCmplRingInit failed.", __func__);
		goto DIRECT_TX_CMPL_RING_INIT_FAILED;
	}

	if (NoaRingDirectRxFallbackRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectRxFallbackRingInit failed.", __func__);
		goto DIRECT_RX_FALLBACK_RING_INIT_FAILED;
	}

	if (NoaRingDirectVendorRxReplenishRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectVendorRxReplenishRingInit failed.",
			       __func__);
		goto DIRECT_VENDOR_RX_REPLENISH_RING_INIT_FAILED;
	}

	if (NoaRingDirectNoaTxReplenishRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectVendorRxReplenishRingInit failed.",
			       __func__);
		goto DIRECT_NOA_TX_REPLENISH_RING_INIT_FAILED;
	}

	if (NoaRingDirectFeedbackRingInit(ring_svc) != 0) {
		WLAN_LOG_ERROR(Cfg, "%s(): NoaRingDirectFeedbackRingInit failed.", __func__);
		goto FEEDBACK_RING_INIT_FAILED;
	}

	return 0;

FEEDBACK_RING_INIT_FAILED:
	NoaRingDirectNoaTxReplenishRingDeinit(ring_svc);
DIRECT_NOA_TX_REPLENISH_RING_INIT_FAILED:
	NoaRingDirectVendorRxReplenishRingDeinit(ring_svc);
DIRECT_VENDOR_RX_REPLENISH_RING_INIT_FAILED:
	NoaRingDirectRxFallbackRingDeinit(ring_svc);
DIRECT_RX_FALLBACK_RING_INIT_FAILED:
	NoaRingDirectTxCmplRingDeinit(ring_svc);
DIRECT_TX_CMPL_RING_INIT_FAILED:
	NoaRingDirectRxCmplRingDeinit(ring_svc);
DIRECT_RX_RING_INIT_FAILED:
	NoaRingDirectTxPostRingDeinit(ring_svc);
DIRECT_TX_RING_INIT_FAILED:
	NoaRingRxCmplRingDeinit(ring_svc);
RX_CMPL_RING_INIT_FAILED:
	NoaRingTxPostRingDeinit(ring_svc);
TX_POST_RING_INIT_FAILED:
	return -ENODEV;
}

void NoaRingSvcDeinit(NoaRingSvc *const ring_svc)
{
	if (ring_svc) {
		NoaRingDirectFeedbackRingDeinit(ring_svc);
		NoaRingDirectNoaTxReplenishRingDeinit(ring_svc);
		NoaRingDirectVendorRxReplenishRingDeinit(ring_svc);
		NoaRingDirectRxFallbackRingDeinit(ring_svc);
		NoaRingDirectTxCmplRingDeinit(ring_svc);
		NoaRingDirectRxCmplRingDeinit(ring_svc);
		NoaRingDirectTxPostRingDeinit(ring_svc);
		NoaRingRxCmplRingDeinit(ring_svc);
		NoaRingTxPostRingDeinit(ring_svc);
	}
}
