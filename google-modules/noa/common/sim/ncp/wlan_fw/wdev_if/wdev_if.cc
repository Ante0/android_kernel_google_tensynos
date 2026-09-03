#include "wdev_if.h"

#include "wlan_log/wlan_log.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
#include "hw_arch/brcm/wdev_if_hw_arch_brcm.h"
#include "chip/brcm4390/wdev_if_chip_brcm_4390.h"
#if IS_ENABLED(CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT)
#include "chip/fake_brcm/wdev_if_chip_fake_brcm.h"
#endif /* CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT */
#endif /* CONFIG_NOA_WLAN_BRCM_SUPPORT */
#if IS_ENABLED(CONFIG_NOA_WLAN_QCA_SUPPORT)
#include "hw_arch/wcn7760/wdev_if_hw_arch_wcn7760.h"
#include "chip/wcn7760/wdev_if_chip_wcn_7760.h"
#endif /* CONFIG_NOA_WLAN_QCA_SUPPORT */

static WlanDeviceArchType GetArchTypeByChipId(const WlanDeviceChipId chip_id)
{
	switch (chip_id) {
#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
	case kWlanDeviceChipIdBrcm4389:
#if IS_ENABLED(CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT)
	case kWlanDeviceChipIdGem5FakeBrcm4389:
#endif /* CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT */
		return kWlanDeviceArchTypeBrcmV0;
	case kWlanDeviceChipIdBrcm4390:
#if IS_ENABLED(CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT)
	case kWlanDeviceChipIdGem5FakeBrcm4390:
#endif /* CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT */
		return kWlanDeviceArchTypeBrcmV1;
#endif /* CONFIG_NOA_WLAN_BRCM_SUPPORT */
#if IS_ENABLED(CONFIG_NOA_WLAN_QCA_SUPPORT)
	case kWlanDeviceChipIdWcn7760:
		return kWlanDeviceArchTypeWcn7760;
#endif /* CONFIG_NOA_WLAN_QCA_SUPPORT */
	default:
		WLAN_LOG_ERROR(Wdev, "%s(): unknown chip id: %" PRIu32, __func__, chip_id);
		break;
	}

	return kWlanDeviceArchTypeUnknown;
}

static int32_t WdevIfAttachChipOps(WdevIf *const wdev_if, enum WlanDeviceChipId chip_id)
{
	wdev_if->chip_id = chip_id;

	switch (chip_id) {
#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
	case kWlanDeviceChipIdBrcm4389:
	case kWlanDeviceChipIdBrcm4390:
		wdev_if->chip_ops.Init = WdevChipBrcm4390Init;
		wdev_if->chip_ops.Deinit = WdevChipBrcm4390Deinit;
		wdev_if->chip_ops.AcknowledgeInterrupt = WdevChipBrcm4390AcknowledgeInterrupt;
		wdev_if->chip_ops.RingTxPostDoorbell = WdevChipBrcm4390RingTxPostDoorbell;
		wdev_if->chip_ops.PcieCheckCmplTimeOut = WdevChipBrcm4390CheckPcieCmplTimeOut;
		wdev_if->chip_ops.FwTrapCheck = WdevChipBrcm4390FwTrapCheck;
		break;
#if IS_ENABLED(CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT)
	case kWlanDeviceChipIdGem5FakeBrcm4389:
	case kWlanDeviceChipIdGem5FakeBrcm4390:
		wdev_if->chip_ops.Init = WdevChipFakeBrcmInit;
		wdev_if->chip_ops.Deinit = WdevChipFakeBrcmDeinit;
		wdev_if->chip_ops.AcknowledgeInterrupt = WdevChipFakeBrcmAcknowledgeInterrupt;
		wdev_if->chip_ops.RingTxPostDoorbell = WdevChipFakeBrcmRingTxPostDoorbell;
		wdev_if->chip_ops.PcieCheckCmplTimeOut = NULL;
		break;
#endif /* CONFIG_NOA_WLAN_FAKE_BRCM_SUPPORT */
#endif /* CONFIG_NOA_WLAN_BRCM_SUPPORT */
#if IS_ENABLED(CONFIG_NOA_WLAN_QCA_SUPPORT)
	case kWlanDeviceChipIdWcn7760:
		wdev_if->chip_ops.Init = WdevChipWcn7760Init;
		wdev_if->chip_ops.Deinit = WdevChipWcn7760Deinit;
		wdev_if->chip_ops.AcknowledgeInterrupt = WdevChipWcn7760AcknowledgeInterrupt;
		wdev_if->chip_ops.RingTxPostDoorbell = WdevChipWcn7760RingTxPostDoorbell;
		wdev_if->chip_ops.PcieCheckCmplTimeOut = WdevChipWcn7760CheckPcieCmplTimeOut;
		wdev_if->chip_ops.FwTrapCheck = NULL;
		break;
#endif /* CONFIG_NOA_WLAN_QCA_SUPPORT */

	default:
		WLAN_LOG_ERROR(Wdev, "%s(): unknown chip id: %" PRIu32, __func__, chip_id);
		return -EINVAL;
	}

	return 0;
}

static int32_t WdevIfAttachHwArchOps(WdevIf *const wdev_if, WlanDeviceArchType arch_type)
{
	wdev_if->arch_type = arch_type;

	switch (arch_type) {
#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
	case kWlanDeviceArchTypeBrcmV0:
	case kWlanDeviceArchTypeBrcmV1:
		wdev_if->hw_arch_ops.GetTxFlowRingId = WdevArchGetTxFlowRingIdBrcm;
		wdev_if->hw_arch_ops.PrepareTxPostDesc = WdevArchPrepareTxPostDescBrcm;
		wdev_if->hw_arch_ops.PrepareRxPostDesc = WdevArchPrepareRxPostDescBrcm;
		wdev_if->hw_arch_ops.PrepareNoaWlanExtendTxD = WdevIfPrepareNoaWlanExtendTxDBrcm;
		wdev_if->hw_arch_ops.HandleTxCplDesc = WdevArchProcessTxCplDescBrcm;
		wdev_if->hw_arch_ops.HandleRxCplDesc = WdevArchProcessRxCplDescBrcm;
		break;
#endif /* CONFIG_NOA_WLAN_BRCM_SUPPORT */
#if IS_ENABLED(CONFIG_NOA_WLAN_QCA_SUPPORT)
	case kWlanDeviceArchTypeWcn7760:
		wdev_if->hw_arch_ops.GetTxFlowRingId = WdevArchGetTxFlowRingIdWcn7760;
		wdev_if->hw_arch_ops.PrepareTxPostDesc = WdevArchPrepareTxPostDescWcn7760;
		wdev_if->hw_arch_ops.PrepareRxPostDesc = WdevArchPrepareRxPostDescWcn7760;
		wdev_if->hw_arch_ops.PrepareNoaWlanExtendTxD = WdevIfPrepareNoaWlanExtendTxDWcn7760;
		wdev_if->hw_arch_ops.HandleTxCplDesc = WdevArchProcessTxCplDescWcn7760;
		wdev_if->hw_arch_ops.HandleRxCplDesc = WdevArchProcessRxCplDescWcn7760;
		break;
#endif /* CONFIG_NOA_WLAN_QCA_SUPPORT */

	default:
		WLAN_LOG_ERROR(Wdev, "%s(): unknown arch type: %" PRIu32, __func__, arch_type);
		return -EINVAL;
	}

	return 0;
}

int32_t WdevIfInit(WdevIf *const wdev_if, WlanDeviceChipId chip_id, uint32_t rx_pkt_tlv_size,
		  ExternalServices *ext_svc)
{
	memset(wdev_if, 0, sizeof(WdevIf));

	wdev_if->post_desc_val_method = kWdevPostDescCoherenceValidationMethodNone;
	wdev_if->cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodNone;
	wdev_if->rx_pkt_tlv_size = rx_pkt_tlv_size;
	wdev_if->ext_svc = ext_svc;

	if (WdevIfAttachChipOps(wdev_if, chip_id) != 0) {
		WdevIfDeinit(wdev_if);
		return -ENODEV;
	}

	if (WdevIfAttachHwArchOps(wdev_if, GetArchTypeByChipId(chip_id)) != 0) {
		WdevIfDeinit(wdev_if);
		return -ENODEV;
	}

	if (wdev_if->chip_ops.Init) {
		return wdev_if->chip_ops.Init(wdev_if);
	}

	return -ENODEV;
}

int32_t WdevIfSetDoorbellAddr(WdevIf *const wdev_if, uint64_t doorbell_addr)
{
	if (wdev_if) {
		wdev_if->doorbell_addr = doorbell_addr;
		return 0;
	}

	return -EINVAL;
}
