#include "wdev_if_hw_arch_brcm.h"

#include <cstdint>
#include <cerrno>
#include <cstring>

#include "gtest/gtest.h"
#include "noa_desc.h"
#include "wdev_if/wdev_if.h"
#include "brcm_msgbuf.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"

namespace noa::service::wlan_service
{
namespace
{

TEST(WdevIfHwArchBrcmTest, WdevArchGetTxFlowRingIdBrcm)
{
	WdevIf wdev_if;
	NoaBrcmTxD brcm_txd;
	uint32_t ring_id;

	// Test with invalid input parameters (NULL pointers)
	EXPECT_EQ(WdevArchGetTxFlowRingIdBrcm(nullptr, &brcm_txd, &ring_id), -EINVAL);
	EXPECT_EQ(WdevArchGetTxFlowRingIdBrcm(&wdev_if, nullptr, &ring_id), -EINVAL);
	EXPECT_EQ(WdevArchGetTxFlowRingIdBrcm(&wdev_if, &brcm_txd, nullptr), -EINVAL);

	brcm_txd.ring_id = 3;
	ASSERT_EQ(WdevArchGetTxFlowRingIdBrcm(&wdev_if, &brcm_txd, &ring_id), 0);
	EXPECT_EQ(ring_id, 3U);
}

TEST(WdevIfHwArchBrcmTest, WdevIfPrepareNoaWlanExtendTxDBrcmFailed)
{
	struct {
		NoaDesc noa_desc;
		NoaNetworkTxD nwk_txd;
	} test_desc;
	NoaWlanExtendTxD ext_txd;
	StaInfo sta_info;
	WdevIf wdev_if;

	EXPECT_EQ(WdevIfPrepareNoaWlanExtendTxDBrcm(
			  nullptr, reinterpret_cast<NoaDesc *>(&test_desc), &sta_info, &ext_txd),
		  -EINVAL);
	EXPECT_EQ(WdevIfPrepareNoaWlanExtendTxDBrcm(&wdev_if, nullptr, &sta_info, &ext_txd),
		  -EINVAL);
	EXPECT_EQ(WdevIfPrepareNoaWlanExtendTxDBrcm(
			  &wdev_if, reinterpret_cast<NoaDesc *>(&test_desc), nullptr, &ext_txd),
		  -EINVAL);
	EXPECT_EQ(WdevIfPrepareNoaWlanExtendTxDBrcm(
			  &wdev_if, reinterpret_cast<NoaDesc *>(&test_desc), &sta_info, nullptr),
		  -EINVAL);

	// Test with an invalid descriptor type.
	test_desc.noa_desc.desc_type = 0;
	EXPECT_EQ(WdevIfPrepareNoaWlanExtendTxDBrcm(
			  &wdev_if, reinterpret_cast<NoaDesc *>(&test_desc), &sta_info, &ext_txd),
		  -EINVAL);
}

TEST(WdevIfHwArchBrcmTest, WdevIfPrepareNoaWlanExtendTxDBrcmSuccess)
{
	struct {
		NoaDesc noa_desc;
		NoaNetworkTxD nwk_txd;
	} test_desc;
	NoaWlanExtendTxD ext_txd{};
	StaInfo sta_info;
	WdevIf wdev_if;
	constexpr uint8_t kTestMacAddr[BCM_ETHER_ADDR_LEN] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

	// Initialize station info with valid data.
	std::memset(&sta_info, 0, sizeof(StaInfo));
	sta_info.enable = 1;
	sta_info.bss_idx = 3;
	std::memcpy(sta_info.mac_addr, kTestMacAddr, BCM_ETHER_ADDR_LEN);

	// Initialize network TX descriptor with test data.
	std::memset(&test_desc.nwk_txd, 0, sizeof(NoaNetworkTxD));
	test_desc.nwk_txd.info.ipv4 = 1;
	test_desc.nwk_txd.info.l3_len = 1;
	test_desc.nwk_txd.info.l4_len = 1;
	test_desc.nwk_txd.info.flowid = 2;
	std::memcpy(test_desc.nwk_txd.dest, kTestMacAddr, BCM_ETHER_ADDR_LEN);

	test_desc.noa_desc.desc_type = NOA_DESC_NETENG_PKT_FLOW;

	// Compose the WLAN Extend TX Descriptor.
	ASSERT_EQ(WdevIfPrepareNoaWlanExtendTxDBrcm(
			  &wdev_if, reinterpret_cast<NoaDesc *>(&test_desc), &sta_info, &ext_txd),
		  0);

	// Verify the fields of the generated descriptor.
	EXPECT_EQ(ext_txd.brcm_txd.ring_id, 2U);
	EXPECT_EQ(ext_txd.brcm_txd.l3_hdr_len, 1U);
	EXPECT_EQ(ext_txd.brcm_txd.l4_hdr_len, 1U);
	EXPECT_EQ(ext_txd.brcm_txd.pkt_csum_type, kPktCsumTypeIpV4Mask);
	EXPECT_EQ(ext_txd.brcm_txd.ifidx, 3U);
}

TEST(WdevIfHwArchBrcmTest, WdevArchPrepareTxPostDescBrcm)
{
	WdevIf wdev_if;
	NoaDesc noa_desc;
	NoaBrcmTxD brcm_txd;
	uint32_t wdev_desc_buf_size = sizeof(HostTxbufPostV2);
	HostTxbufPostV2 tx_post;
	char txhdr_buf[BCM_ETHER_HDR_LEN];

	std::memset(&wdev_if, 0, sizeof(wdev_if));
	wdev_if.arch_type = kWlanDeviceArchTypeBrcmV1;

	std::memset(&noa_desc, 0, sizeof(noa_desc));
	noa_desc.desc_type = NOA_DESC_WLAN_TX_BRCM;
	noa_desc.dp_low = 0x12345678;
	noa_desc.dp_high = 0x9abc;
	noa_desc.dl = 1024;
	noa_desc.head_offset = 0;
	noa_desc.tkid = 3;
	noa_desc.dv = reinterpret_cast<uint32_t>(&txhdr_buf);

	std::memset(&brcm_txd, 0, sizeof(brcm_txd));
	brcm_txd.ring_id = 3;
	brcm_txd.current_phase = 0xFF;
	brcm_txd.pkt_csum_type = kPktCsumTypeIpV4Mask;

	// Test with valid input parameters
	ASSERT_EQ(WdevArchPrepareTxPostDescBrcm(&wdev_if, &noa_desc, &brcm_txd, wdev_desc_buf_size,
						nullptr, &tx_post),
		  0);
	EXPECT_EQ(tx_post.v1.data_buf_addr, 0x9abc12345678ULL);
	EXPECT_EQ(tx_post.v1.data_len, 1024U);
	EXPECT_EQ(tx_post.v1.cmn_hdr.request_id, 3);
	EXPECT_EQ(tx_post.v1.cmn_hdr.flags, 0xFF);
	EXPECT_EQ(tx_post.v2_ext.pkt_info_cso.pkt_csum_type,
		  static_cast<uint32_t>(kPktCsumTypeIpV4Mask));

	// Test with unsupported desc_type
	noa_desc.desc_type = NOA_DESC_MODEM_LASSEN; // Assuming this is a different type
	EXPECT_EQ(WdevArchPrepareTxPostDescBrcm(&wdev_if, &noa_desc, &brcm_txd, wdev_desc_buf_size,
						nullptr, &tx_post),
		  -EINVAL);

	// Test with invalid input parameters (NULL pointers)
	noa_desc.desc_type = NOA_DESC_WLAN_TX_BRCM; // Reset to valid type
	EXPECT_EQ(WdevArchPrepareTxPostDescBrcm(nullptr, &noa_desc, &brcm_txd, wdev_desc_buf_size,
						nullptr, &tx_post),
		  -EINVAL);
	EXPECT_EQ(WdevArchPrepareTxPostDescBrcm(&wdev_if, nullptr, &brcm_txd, wdev_desc_buf_size,
						nullptr, &tx_post),
		  -EINVAL);
	EXPECT_EQ(WdevArchPrepareTxPostDescBrcm(&wdev_if, &noa_desc, nullptr, wdev_desc_buf_size,
						nullptr, &tx_post),
		  -EINVAL);
	EXPECT_EQ(WdevArchPrepareTxPostDescBrcm(&wdev_if, &noa_desc, &brcm_txd, wdev_desc_buf_size,
						nullptr, nullptr),
		  -EINVAL);
}

TEST(WdevIfHwArchBrcmTest, WdevArchPrepareRxPostDescBrcm)
{
	WdevIf wdev_if;
	uint32_t wdev_desc_buf_size = sizeof(HostRxbufPost);
	WdevPostDescCoherenceInfo coherence_info;
	HostRxbufPost rxbuf_post;
	const uint16_t kTestPktid = 123;
	const uint16_t kTestBufferSize = 1024;
	const uint64_t kTestPa = 0x123456789abcdef0;
	const uint64_t kTestVa = 0x9abcdef012345678;
	const uint32_t kWlanPktPad = 60;

	ASSERT_EQ(BmInit(kVendorRxBufferManager), 0);
	ASSERT_EQ(BmAcquire(kVendorRxBufferManager, kTestPktid, kTestBufferSize, kTestPa, kTestVa),
		  0);

	// Test with valid input parameters
	wdev_if.post_desc_val_method = kWdevPostDescCoherenceValidationMethodNone;
	coherence_info.brcm_sn_cks.current_sn = 5;
	ASSERT_EQ(WdevArchPrepareRxPostDescBrcm(&wdev_if, kTestPktid, wdev_desc_buf_size,
						&coherence_info, &rxbuf_post),
		  0);
	EXPECT_EQ(rxbuf_post.cmn_hdr.msg_type, kBrcmPcieMsgTypeRxPost);
	EXPECT_EQ(rxbuf_post.cmn_hdr.request_id, kTestPktid);
	EXPECT_EQ(rxbuf_post.data_buf_len, kTestBufferSize - kWlanPktPad);
	EXPECT_EQ(rxbuf_post.data_buf_addr, kTestPa + kWlanPktPad);

	// Test with BrcmSn coherence validation
	wdev_if.post_desc_val_method = kWdevPostDescCoherenceValidationMethodBrcmSn;
	ASSERT_EQ(WdevArchPrepareRxPostDescBrcm(&wdev_if, kTestPktid, wdev_desc_buf_size,
						&coherence_info, &rxbuf_post),
		  0);
	EXPECT_EQ(rxbuf_post.cmn_hdr.epoch, 5U);
	EXPECT_EQ(coherence_info.brcm_sn_cks.next_sn, 6U); // Check SN update

	// Test with insufficient buffer size
	wdev_desc_buf_size = sizeof(HostRxbufPost) - 1;
	EXPECT_EQ(WdevArchPrepareRxPostDescBrcm(&wdev_if, kTestPktid, wdev_desc_buf_size,
						&coherence_info, &rxbuf_post),
		  -EINVAL);

	// Test with invalid input parameters (NULL pointers)
	wdev_desc_buf_size = sizeof(HostRxbufPost); // Reset to valid size
	EXPECT_EQ(WdevArchPrepareRxPostDescBrcm(nullptr, kTestPktid, wdev_desc_buf_size,
						&coherence_info, &rxbuf_post),
		  -EINVAL);
	EXPECT_EQ(WdevArchPrepareRxPostDescBrcm(&wdev_if, kTestPktid, wdev_desc_buf_size, nullptr,
						&rxbuf_post),
		  -EINVAL);
	EXPECT_EQ(WdevArchPrepareRxPostDescBrcm(&wdev_if, kTestPktid, wdev_desc_buf_size,
						&coherence_info, nullptr),
		  -EINVAL);

	BmDeinit(kVendorRxBufferManager);
}

TEST(WdevIfHwArchBrcmTest, WdevArchProcessTxCplDescBrcm)
{
	WdevIf wdev_if;
	wdev_if.cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodNone; // For simplicity
	HostTxbufCmpl tx_cmpl;
	WdevCmplDescCoherenceInfo coherence_info;
	WdevTxCmplDescriptorInfo desc_info;

	// Test with valid pktid
	tx_cmpl.cmn_hdr.request_id = 246;
	ASSERT_EQ(WdevArchProcessTxCplDescBrcm(&wdev_if, &tx_cmpl, &coherence_info, &desc_info), 0);
	EXPECT_EQ(desc_info.pktid, 246);

	// Test with invalid pktid (0)
	tx_cmpl.cmn_hdr.request_id = 0;
	EXPECT_EQ(WdevArchProcessTxCplDescBrcm(&wdev_if, &tx_cmpl, &coherence_info, &desc_info),
		  -EINVAL);

	// Test with invalid input parameters (NULL pointers)
	EXPECT_EQ(WdevArchProcessTxCplDescBrcm(nullptr, &tx_cmpl, &coherence_info, &desc_info),
		  -EINVAL);
	EXPECT_EQ(WdevArchProcessTxCplDescBrcm(&wdev_if, nullptr, &coherence_info, &desc_info),
		  -EINVAL);
	EXPECT_EQ(WdevArchProcessTxCplDescBrcm(&wdev_if, &tx_cmpl, &coherence_info, nullptr),
		  -EINVAL);

	// Test with coherence validation failure (using a mock)
	wdev_if.cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodBrcmSnCsum;
	coherence_info.brcm_sn_cks.current_sn = 3;
	tx_cmpl.cmn_hdr.epoch = 1;
	coherence_info.brcm_sn_cks.ring_desc_size = sizeof(HostTxbufCmpl);
	// Assuming BrcmCmplDescCoherenceValidation will return an error in this case
	EXPECT_EQ(WdevArchProcessTxCplDescBrcm(&wdev_if, &tx_cmpl, &coherence_info, &desc_info),
		  -EINVAL);
}

TEST(WdevIfHwArchBrcmTest, WdevArchProcessRxCplDescBrcm)
{
	WdevIf wdev_if;
	wdev_if.cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodNone; // For simplicity
	HostRxbufCmpl rx_cmpl;
	WdevCmplDescCoherenceInfo coherence_info;
	WdevRxCmplDescriptorInfo desc_info;

	// Test with valid input parameters
	rx_cmpl.cmn_hdr.request_id = 123;
	rx_cmpl.data_offset = 8;
	rx_cmpl.data_len = 1016;
	rx_cmpl.cmn_hdr.if_id = 0; // STA mode
	rx_cmpl.flags = BCMPCIE_PKT_FLAGS_FRAME_802_11; // 802.11 frame

	ASSERT_EQ(WdevArchProcessRxCplDescBrcm(&wdev_if, &rx_cmpl, &coherence_info, &desc_info), 0);
	EXPECT_EQ(desc_info.pktid, 123U);
	EXPECT_EQ(desc_info.head_offset, 8U);
	EXPECT_EQ(desc_info.data_len, 1024U);
	EXPECT_EQ(desc_info.flags, kWdevPacketFlagStationMode | kWdevPacketFlag802dot11);

	// Test with non-STA mode and non-802.11 frame
	rx_cmpl.cmn_hdr.if_id = 1;
	rx_cmpl.flags = 0;
	ASSERT_EQ(WdevArchProcessRxCplDescBrcm(&wdev_if, &rx_cmpl, &coherence_info, &desc_info), 0);
	EXPECT_EQ(desc_info.flags, 0);

	// Test with invalid input parameters (NULL pointers)
	EXPECT_EQ(WdevArchProcessRxCplDescBrcm(nullptr, &rx_cmpl, &coherence_info, &desc_info),
		  -EINVAL);
	EXPECT_EQ(WdevArchProcessRxCplDescBrcm(&wdev_if, nullptr, &coherence_info, &desc_info),
		  -EINVAL);
	EXPECT_EQ(WdevArchProcessRxCplDescBrcm(&wdev_if, &rx_cmpl, &coherence_info, nullptr),
		  -EINVAL);

	// Test with coherence validation failure (using a mock)
	wdev_if.cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodBrcmSnCsum;
	coherence_info.brcm_sn_cks.current_sn = 3;
	rx_cmpl.cmn_hdr.epoch = 1;
	coherence_info.brcm_sn_cks.ring_desc_size = sizeof(HostRxbufCmpl);
	// Assuming BrcmCmplDescCoherenceValidation will return an error in this case
	ASSERT_EQ(WdevArchProcessRxCplDescBrcm(&wdev_if, &rx_cmpl, &coherence_info, &desc_info),
		  -EINVAL);
}

} // anonymous namespace
} // namespace noa::service::wlan_service
