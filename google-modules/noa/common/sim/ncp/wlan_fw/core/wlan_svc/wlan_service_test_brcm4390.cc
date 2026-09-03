#include "wlan_service.h"

#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <queue>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "wlan_service_rpc_protocol.h"
#include "sys_if/log/sys_if_log_test_lib.h"
#include "wdev_if/wdev_if.h"
#include "wdev_if/hw_arch/brcm/brcm_msgbuf.h"
#include "modules/wlan_ring_manager/wlan_ring_manager.h"
#include "modules/wlan_ring_manager/wlan_wdev_ring_manager.h"
#include "interrupt/interrupt.h"
#include "mock/mock_system_clock.h"
#include "pw_log/log.h"
#include "ring_mgmt/mock_ring_shared_info.h"
#include "sys_if/mailbox/sys_if_mailbox_pw.h"
#include "core/dp/wlan_dp.h"
#include "modules/sta_table/sta_table.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"
#include "pw_toolchain/no_destructor.h"
#include "core/wlan_shared_mem/memory_map_helper.h"
#include "noa/wlan/noa_wlan.h"
#include "wlan_service_test_lib.h"

namespace noa::service::wlan_service
{
namespace testing
{

using ::noa::service::ring_service::MockRingSharedInfo;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// Number of descriptors in a fake ring.
constexpr uint32_t kFakeRingNumDesc = 2048;
// Number of TX post rings.
constexpr uint32_t kFakeWdevTxPostRingNum = NUM_WDEV_TX_POST_RING;
// Number of RX post rings.
constexpr uint32_t kFakeWdevRxPostRingNum = NUM_WDEV_RX_POST_RING;
// Number of TX completion rings.
constexpr uint32_t kFakeWdevTxCmplRingNum = NUM_WDEV_TX_CMPL_RING;
// Number of RX completion rings.
constexpr uint32_t kFakeWdevRxCmplRingNum = NUM_WDEV_RX_CMPL_RING;

class WlanServiceTestBrcm4390 : public WlanServiceTestBasic {
    public:
	WlanServiceTestBrcm4390()
		: WlanServiceTestBasic(kFakeRingNumDesc, kFakeWdevTxPostRingNum,
				       kFakeWdevRxPostRingNum, kFakeWdevTxCmplRingNum,
				       kFakeWdevRxCmplRingNum)
	{
		FakeRingPoolsInit();
	}

	void FakeRingPoolsInit()
	{
		// Reserve space for fake ring pools.
		wdev_tx_post_fake_ring_pool_.reserve(tx_post_ring_num_);
		wdev_rx_post_fake_ring_pool_.reserve(rx_post_ring_num_);
		wdev_tx_cmpl_fake_ring_pool_.reserve(tx_cmpl_ring_num_);
		wdev_rx_cmpl_fake_ring_pool_.reserve(rx_cmpl_ring_num_);
		// Initialize fake ring pools.
		for (uint32_t i = 0; i < tx_post_ring_num_; i++) {
			wdev_tx_post_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(HostTxbufPostV2));
		}

		for (uint32_t i = 0; i < rx_post_ring_num_; i++) {
			wdev_rx_post_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(HostRxbufPost));
		}

		for (uint32_t i = 0; i < tx_cmpl_ring_num_; i++) {
			wdev_tx_cmpl_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(HostTxbufCmpl));
		}

		for (uint32_t i = 0; i < rx_cmpl_ring_num_; i++) {
			wdev_rx_cmpl_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(HostRxbufCmpl));
		}
	}

	~WlanServiceTestBrcm4390() = default;

	void SetUp() override
	{
		WlanServiceTestBasic::SetUp();

		shared_memory_ = std::malloc(sizeof(struct NoaWlanSharedInfo));

		ASSERT_NE(shared_memory_, nullptr);
		memset(shared_memory_, 0, sizeof(NoaWlanSharedInfo));

		// FW initialization.
		NoaWlanSharedInfo *shared_info =
			reinterpret_cast<NoaWlanSharedInfo *>(shared_memory_);
		NoaGlobalConfig *global_config = &shared_info->global_config;
		memset(global_config, 0, sizeof(NoaGlobalConfig));
		global_config->chip_type = kWlanDeviceChipIdGem5FakeBrcm4390;
		global_config->rx_pkt_max = kFakeApcRxBufferPoolSize;
		global_config->rx_buf_size = kFakeBufferSize;
		global_config->tx_pkt_max = 0;
		global_config->tx_bm_size = kFakeNoaTxBufferPoolSize;

		WlanCmdFwInit fw_init_params;
		std::memset(&fw_init_params, 0, sizeof(fw_init_params));
		fw_init_params.noa_shared_mem_size = sizeof(struct NoaWlanSharedInfo);
		fw_init_params.noa_shared_mem_addr = reinterpret_cast<uint64_t>(shared_memory_);
		ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdFwInit,
					     reinterpret_cast<void *>(&fw_init_params), 0, nullptr),
			  0);

		struct WlanCmdIrqReuest irq_request = {
			.irq_nums = 1,
		};
		irq_request.irqs[0] = 0xCAFE;
		ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdIsrRegister,
					     reinterpret_cast<void *>(&irq_request), 0, nullptr),
			  0);

		// Update WiFi device's rings.
		WdevRingUpdate(wdev_tx_post_fake_ring_pool_, kWdevTxPostRingGroup);
		WdevRingUpdate(wdev_rx_cmpl_fake_ring_pool_, kWdevRxCmplRingGroup);
		WdevRingUpdate(wdev_tx_cmpl_fake_ring_pool_, kWdevTxCmplRingGroup);
		WdevRingUpdate(wdev_rx_post_fake_ring_pool_, kWdevRxPostRingGroup);
		ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdFwStart, nullptr, 0, nullptr), 0);

		// Refill buffers.
		BufferRefill(apc_rx_buffer_pool_, kVendorRxBufferManager);
		BufferRefill(noa_tx_buffer_pool_, kNoaTxBufferManager);

		// Activate TX post rings.
		WdevTxPostRingActivate();

		// Update station table.
		FakeStationSetup();
	}

	void TearDown() override
	{
		// FW stop.
		ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdFwStop, nullptr, 0, nullptr), 0);
		// FW exit.
		ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdFwExit, nullptr, 0, nullptr), 0);
		// Tear down NOA system environment.
		NoaSystemEnvironmentTearDown();
		free(shared_memory_);
	}

	// Update WiFi device rings.
	void WdevRingUpdate(std::vector<FakeRing> &ring_pool, RingGroupType ring_type)
	{
		const uint32_t kPoolSize = ring_pool.size();
		struct NoaWlanWiFiRingConfig *ring_config =
			reinterpret_cast<struct NoaWlanWiFiRingConfig *>(
				GetNoaWlanConfigSectionAddr(&wlan_svc_.memory_map_helper,
							    kWdevRingConfig));
		struct NoaWlanRingPoolConfig *ring_pool_config = ring_config->wifi_ring_pool_config;
		struct NoaWlanRingInfo *wlan_ring_pool =
			GetNoaWLanWdevRingPool(&wlan_svc_.memory_map_helper, ring_type);

		ring_pool_config[ring_type].count = kPoolSize;
		ring_pool_config[ring_type].ring_type = ring_type;

		for (uint32_t i = 0; i < kPoolSize; i++) {
			struct NoaWlanRingInfo &ring_update_info = wlan_ring_pool[i];
			FakeRing &ring = ring_pool[i];
			ring_update_info.ndesc = ring.ndesc;
			ring_update_info.desc_sz = ring.desc_size;
			ring_update_info.offset = 0;
			ring_update_info.hw_idx = 0;
			ring_update_info.stride = 1;
			ring_update_info.dma_va =
				static_cast<uint64_t>(reinterpret_cast<intptr_t>(ring.ring_buffer));
			ring_update_info.dma_pa =
				static_cast<uint64_t>(reinterpret_cast<intptr_t>(ring.ring_buffer));
			ring_update_info.regs.base = reinterpret_cast<uintptr_t>(&ring.base);
			ring_update_info.regs.len = reinterpret_cast<uintptr_t>(&ring.len);
			ring_update_info.regs.max_item =
				reinterpret_cast<uintptr_t>(&ring.max_item);
			ring_update_info.regs.read = reinterpret_cast<uintptr_t>(&ring.read);
			ring_update_info.regs.write = reinterpret_cast<uintptr_t>(&ring.write);
		}
	}

	// Activate TX post rings.
	void WdevTxPostRingActivate()
	{
		const uint32_t kRingPoolSize = wdev_tx_post_fake_ring_pool_.size();

		for (uint32_t i = 0; i < kRingPoolSize; i++) {
			WlanCmdTxRingActivate ring_activate;
			ring_activate.enable = 1;
			ring_activate.ring_id = i;
			ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdTxRingActivate,
						     reinterpret_cast<void *>(&ring_activate), 0,
						     nullptr),
				  0);
		}
	}
};

TEST_F(WlanServiceTestBrcm4390, FeedthroughTx)
{
	constexpr uint16_t kTestPktId = 1;
	constexpr uint32_t kTestRingId = 2;
	constexpr uint32_t kTestL3HdrLen = 3;
	constexpr uint32_t kTestL4HdrLen = 4;
	constexpr uint32_t kTestCurrentPhase = 5;
	constexpr uint16_t kTestEtherTypeIpv4 = 0x800;
	constexpr char kTestMsg[] = "Test Message";
	constexpr uint32_t kTestHeadOffset = sizeof(kTestMsg);
	struct noa_ring_regs nep_ring_regs;
	NoaDesc *noa_desc;
	NoaBrcmTxD *brcm_txd;

	// Copy test message to the test data buffer.
	std::memcpy(test_data_buf_, kTestMsg, sizeof(kTestMsg));
	// Get NEP ring registers.
	ASSERT_EQ(NoaRingSharedRegsGet(&nep_ring_regs, kNoaNetworkInterfaceWlan,
				       kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData,
				       kNoaRingNepOutput),
		  0);
	ASSERT_NE(nep_ring_regs.read, 0);
	// Set the read index of the NEP ring to 0.
	SysIfIoWritel(0U, reinterpret_cast<void *>(nep_ring_regs.read));
	// Get the NOA descriptor from the NEP ring.
	noa_desc = reinterpret_cast<NoaDesc *>(*reinterpret_cast<uintptr_t *>(nep_ring_regs.base));
	// Get the NOA extended Broadcom TX descriptor from the NOA descriptor.
	brcm_txd = reinterpret_cast<NoaBrcmTxD *>(noa_desc->ext_data);

	// Initialize NOA descriptor and Broadcom TX descriptor.
	std::memset(noa_desc, 0, sizeof(NoaDesc));
	std::memset(brcm_txd, 0, sizeof(NoaBrcmTxD));
	// Fill NOA descriptor fields.
	noa_desc->ver = 0;
	noa_desc->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
					     kNoaWlanRingTxData);
	noa_desc->mode = NOAD_MODE_DATA;
	noa_desc->cp = 0;
	noa_desc->fk = 0;
	noa_desc->dp_low =
		static_cast<uint64_t>(reinterpret_cast<uintptr_t>(test_data_buf_)) & 0xFFFFFFFF;
	noa_desc->dp_high =
		(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(test_data_buf_)) >> 32) &
		0xFFFFFFFF;
	noa_desc->head_offset = kTestHeadOffset;
	noa_desc->dl = sizeof(kTestMsg) + kTestHeadOffset;
	noa_desc->tkid = kTestPktId;
	noa_desc->ddone = 0;
	noa_desc->dv = reinterpret_cast<uintptr_t>(test_data_buf_);
	noa_desc->desc_type = NOA_DESC_WLAN_TX_BRCM;
	// Fill Broadcom TX descriptor fields.
	brcm_txd->current_phase = kTestCurrentPhase;
	brcm_txd->ext_flags = 0;
	brcm_txd->ring_id = kTestRingId;
	brcm_txd->flags = 0;
	brcm_txd->ifidx = 0;
	brcm_txd->ethertype = kTestEtherTypeIpv4;
	brcm_txd->pkt_csum_type = kPktCsumTypeIpV4Mask;
	brcm_txd->l3_hdr_len = kTestL3HdrLen;
	brcm_txd->l4_hdr_len = kTestL4HdrLen;

	// Update write index of the NEP ring.
	SysIfIoWritel(1U, reinterpret_cast<void *>(nep_ring_regs.write));

	// Initiate the NEP handler and process the ring service RX task.
	SysIfMailboxHelper::GetInstance(kNcpWifiMailboxTypeNep)->InitiateHandler(kDoorbellRxEvent);
	WlanDpRingSvcRxTask(&wlan_svc_.dp.ring_svc_dp_worker, 1U);

	// Get the fake ring corresponding to the test ring ID.
	FakeRing &fake_ring = wdev_tx_post_fake_ring_pool_[kTestRingId];
	//Verify that the write index of the fake ring is updated.
	ASSERT_EQ(SysIfIoReadl(reinterpret_cast<void *>(&fake_ring.write)), 1U);
	// Get the BRCM Host TX post descriptor from the fake ring.
	HostTxbufPostV2 *txd = reinterpret_cast<HostTxbufPostV2 *>(fake_ring.ring_buffer);
	// Verify the fields of the BRCM Host TX post descriptor.
	EXPECT_EQ(txd->v1.cmn_hdr.msg_type, kBrcmPcieMsgTypeTxPost);
	EXPECT_EQ(txd->v1.cmn_hdr.if_id, 0);
	EXPECT_EQ(txd->v1.cmn_hdr.request_id, kTestPktId);
	EXPECT_EQ(txd->v1.data_buf_addr,
		  static_cast<uint64_t>(reinterpret_cast<uintptr_t>(test_data_buf_)) +
			  kTestHeadOffset);
	EXPECT_EQ(txd->v1.data_len, sizeof(kTestMsg));
	EXPECT_EQ(txd->v2_ext.pkt_info_cso.pkt_csum_type, kPktCsumTypeIpV4Mask);
	EXPECT_EQ(txd->v2_ext.pkt_info_cso.nwk_hdr_len, kTestL3HdrLen);
	EXPECT_EQ(txd->v2_ext.pkt_info_cso.trans_hdr_len, kTestL4HdrLen);
}

TEST_F(WlanServiceTestBrcm4390, ForwardingTx)
{
	// Create a forwarding TX packet for the fake station.
	constexpr uint16_t kTestPktId = 2;
	constexpr uint32_t kTestRingId = 2;
	constexpr uint32_t kTestL3HdrLen = 3;
	constexpr uint32_t kTestL4HdrLen = 4;
	constexpr uint16_t kTestEtherTypeIpv4 = 0x800;
	constexpr char kTestMsg[] = "Test Message";
	constexpr uint32_t kTestHeadOffset = sizeof(kTestMsg);
	struct noa_ring_regs nep_ring_regs;
	NoaDesc *noa_desc;
	NoaNetworkTxD *network_txd;

	// Copy test message to the test data buffer.
	std::memcpy(test_data_buf_, kTestMsg, sizeof(kTestMsg));
	// Get NEP ring registers.
	ASSERT_EQ(NoaRingSharedRegsGet(&nep_ring_regs, kNoaNetworkInterfaceWlan,
				       kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData,
				       kNoaRingNepOutput),
		  0);
	// Set the read index of the NEP ring to 0.
	SysIfIoWritel(0U, reinterpret_cast<void *>(nep_ring_regs.read));
	// Get the NOA descriptor from the NEP ring.
	noa_desc = reinterpret_cast<NoaDesc *>(*reinterpret_cast<uintptr_t *>(nep_ring_regs.base));
	// Get the NOA extended Network TX descriptor from the NOA descriptor.
	network_txd = reinterpret_cast<NoaNetworkTxD *>(noa_desc->ext_data);

	// Initialize NOA descriptor and Network TX descriptor.
	std::memset(noa_desc, 0, sizeof(NoaDesc));
	std::memset(network_txd, 0, sizeof(NoaNetworkTxD));
	// Add the NOA descriptor to the ring service NEP-WiFi output ring.
	noa_desc->ver = 0;
	noa_desc->dst = NOA_PORT_WLAN_FW;
	noa_desc->mode = NOAD_MODE_DATA;
	noa_desc->cp = 1;
	noa_desc->fk = 0;
	noa_desc->dp_low =
		static_cast<uint64_t>(reinterpret_cast<uintptr_t>(test_data_buf_)) & 0xFFFFFFFF;
	noa_desc->dp_high =
		(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(test_data_buf_)) >> 32) &
		0xFFFFFFFF;
	noa_desc->head_offset = kTestHeadOffset;
	noa_desc->dl = sizeof(kTestMsg) + kTestHeadOffset;
	noa_desc->tkid = kTestPktId;
	noa_desc->ddone = 0;
	noa_desc->dv = reinterpret_cast<uintptr_t>(test_data_buf_);
	noa_desc->desc_type = NOA_DESC_NETENG_PKT_FLOW;
	// Fill in NOA Network TX descriptor field.
	std::memcpy(network_txd->dest, fake_sta_.mac_addr,
		    kMacAddressPartEnd - kMacAddressPartStart);
	network_txd->info.flowid = kTestRingId;
	network_txd->info.ethertype = kTestEtherTypeIpv4;
	network_txd->info.oif = 0;
	network_txd->info.l3_len = kTestL3HdrLen;
	network_txd->info.l4_len = kTestL4HdrLen;

	// Update write index of the NEP ring.
	SysIfIoWritel(1U, reinterpret_cast<void *>(nep_ring_regs.write));

	// Initiate the mailbox handler to trigger the ring service RX top-half
	// handler.
	SysIfMailboxHelper::GetInstance(kNcpWifiMailboxTypeNep)->InitiateHandler(kDoorbellRxEvent);
	// Trigger the ring service RX bottom-half handler.
	WlanDpRingSvcRxTask(&wlan_svc_.dp.ring_svc_dp_worker, 1U);

	// 5. Confirm that the packet is correctly sent to the WiFi device ring.
	// Get the fake ring corresponding to the test ring ID.
	FakeRing &fake_ring = wdev_tx_post_fake_ring_pool_[kTestRingId];
	// Verify that the write index of the fake ring is updated.
	ASSERT_EQ(SysIfIoReadl(reinterpret_cast<void *>(&fake_ring.write)), 1U);
	// Get the BRCM Host TX post descriptor from the fake ring.
	HostTxbufPostV2 *txd = reinterpret_cast<HostTxbufPostV2 *>(fake_ring.ring_buffer);
	// Verify the fields of the BRCM Host TX post descriptor.
	EXPECT_EQ(txd->v1.cmn_hdr.msg_type, kBrcmPcieMsgTypeTxPost);
	EXPECT_EQ(txd->v1.cmn_hdr.if_id, 0);
	EXPECT_EQ(txd->v1.cmn_hdr.request_id, kTestPktId);
	EXPECT_EQ(txd->v1.data_buf_addr,
		  static_cast<uint64_t>(reinterpret_cast<uintptr_t>(test_data_buf_)) +
			  kTestHeadOffset);
	EXPECT_EQ(txd->v1.data_len, sizeof(kTestMsg));
	EXPECT_EQ(txd->v2_ext.pkt_info_cso.nwk_hdr_len, kTestL3HdrLen);
	EXPECT_EQ(txd->v2_ext.pkt_info_cso.trans_hdr_len, kTestL4HdrLen);
}

TEST_F(WlanServiceTestBrcm4390, NepBufferFeedback)
{
	constexpr uint16_t kTestPktId = 1;
	struct noa_ring_regs nep_ring_regs;
	NoaDesc *noa_desc;
	const BmTkidItem *bm_locker;

	// Simulate an initial state where a buffer is being managed.
	// Acquire a slot in the buffer manager for our test TKID.
	BmAcquire(kVendorRxBufferManager, kTestPktId, 0, 0, 0);

	// Move the buffer to the 'OnHold' state, simulating it being processed.
	BmPlaceOnHold(kVendorRxBufferManager, kTestPktId);

	// Get the NEP ring registers for the WLAN FW output port.
	ASSERT_EQ(NoaRingSharedRegsGet(&nep_ring_regs, kNoaNetworkInterfaceWlan,
				       kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData,
				       kNoaRingNepOutput),
		  0);
	// Get the Noa descriptor from the NEP ring base address.
	noa_desc = reinterpret_cast<NoaDesc *>(*reinterpret_cast<uintptr_t *>(nep_ring_regs.base));
	ASSERT_NE(noa_desc, nullptr);

	// Fill the Noa descriptor with test data.
	noa_desc->tkid = kTestPktId;
	noa_desc->mode = NOAD_MODE_FEEDBACK;

	// Update read/write index of the NEP ring.
	SysIfIoWritel(0U, reinterpret_cast<void *>(nep_ring_regs.read));
	SysIfIoWritel(1U, reinterpret_cast<void *>(nep_ring_regs.write));

	// Initiate the NEP handler and process the ring service RX task.
	SysIfMailboxHelper::GetInstance(kNcpWifiMailboxTypeNep)->InitiateHandler(kDoorbellRxEvent);
	// Process the ring service RX task to handle the NEP feedback.
	WlanDpRingSvcRxTask(&wlan_svc_.dp.ring_svc_dp_worker, 1U);

	// Check that the buffer has been moved back to the 'Occupied' state.
	ASSERT_EQ(BmFind(kVendorRxBufferManager, kTestPktId, &bm_locker), 0);
}

TEST_F(WlanServiceTestBrcm4390, RxBufferRefillPosting)
{
	constexpr uint16_t kTestPktId = 1;
	FakeRing &rx_post_ring = wdev_rx_post_fake_ring_pool_[0];
	FakeBuffer &fake_buffer = apc_rx_buffer_pool_[kTestPktId - 1];
	// BufferManagementTable *bmt = &wlan_svc_.buffer_manager.tables[kBufferTableTypeApcRx];
	// BufferInfo buffer_info;
	HostRxbufPost *rx_post_desc;
	uint32_t budget = 1;
	struct noa_ring_regs rx_refill_ring_regs;
	NoaWlanBufferReplnDesc *bm_repln_desc;

	// Get NEP ring registers.
	ASSERT_EQ(NoaRingSharedRegsGet(&rx_refill_ring_regs, kNoaNetworkInterfaceWlanDirect,
				       kNoaNetworkFlowHostToDevice,
				       kNoaWlanDirectH2DRingVendorRxBufferReplenish,
				       kNoaNepRingAnyDirection),
		  0);
	bm_repln_desc = reinterpret_cast<NoaWlanBufferReplnDesc *>(
		*reinterpret_cast<uintptr_t *>(rx_refill_ring_regs.base));
	bm_repln_desc->tkid = kTestPktId;
	bm_repln_desc->len = fake_buffer.buffer_size;
	bm_repln_desc->dpa_va = reinterpret_cast<uintptr_t>(fake_buffer.buffer);
	bm_repln_desc->pa = reinterpret_cast<uintptr_t>(fake_buffer.buffer);
	bm_repln_desc->to_dev = true;
	// Reset the fake RX buffer replenishment ring indices.
	SysIfIoWritel(0U, reinterpret_cast<void *>(rx_refill_ring_regs.read));
	SysIfIoWritel(1U, reinterpret_cast<void *>(rx_refill_ring_regs.write));
	BmRelease(kVendorRxBufferManager, kTestPktId);

	// Reset the fake RX post ring indices.
	SysIfIoWritel(0U, reinterpret_cast<void *>(&rx_post_ring.read));
	SysIfIoWritel(0U, reinterpret_cast<void *>(&rx_post_ring.write));

	// Trigger the RX buffer refill process.
	TriggerWlanDpBufferRefill(&wlan_svc_.dp);
	WlanDpWdevRxBufferRefillTask(&wlan_svc_.dp, &budget);

	// Verify that the RX buffer is posted for refill.
	// Check if the write index of the RX post ring is incremented, indicating
	// a posted buffer.
	ASSERT_EQ(SysIfIoReadl(reinterpret_cast<void *>(&rx_post_ring.write)), 1U);
	rx_post_desc = reinterpret_cast<HostRxbufPost *>(rx_post_ring.ring_buffer);
	ASSERT_NE(rx_post_desc, nullptr);
	// Validate the contents of the RX post descriptor.
	EXPECT_EQ(rx_post_desc->cmn_hdr.request_id, kTestPktId);
	EXPECT_EQ(rx_post_desc->data_buf_len, fake_buffer.buffer_size - WLAN_PKT_PAD);
	EXPECT_EQ(rx_post_desc->data_buf_addr,
		  static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fake_buffer.buffer) +
					WLAN_PKT_PAD));
}

TEST_F(WlanServiceTestBrcm4390, FeedthroughTxCompletion)
{
	constexpr uint16_t kTestPktId = 1;
	FakeRing &tx_cmpl_ring = wdev_tx_cmpl_fake_ring_pool_[0];
	HostTxbufCmpl *tx_cmpl_desc = reinterpret_cast<HostTxbufCmpl *>(tx_cmpl_ring.ring_buffer);
	int32_t wdev_rx_irq_num =
		static_cast<int32_t>(wlan_svc_.dp.wlan_dev_intr_ctx_group[0].irq_num);

	ASSERT_NE(tx_cmpl_desc, nullptr);
	// Set the packet ID in the descriptor.
	tx_cmpl_desc->cmn_hdr.request_id = kTestPktId;
	// Reset the fake WLAN RPC service event queue.
	FakeWlanRpcService::GetInstance().ResetEventService();

	// Set the read and write pointers in the fake ring to simulate a packet
	// arrival.
	SysIfIoWritel(0U, reinterpret_cast<void *>(&tx_cmpl_ring.read));
	SysIfIoWritel(1U, reinterpret_cast<void *>(&tx_cmpl_ring.write));
	// Trigger the WLAN device RX interrupt handler.
	noa::module::interrupt::InterruptController::Instance()->ExecuteInterruptHandler(
		wdev_rx_irq_num);
	// Process the received packet in the WLAN DP RX task.
	WlanDpWdevRxTask(&wlan_svc_.dp.wlan_dev_dp_worker, 1U);

	// TODO: b/422030846: Add result check for feedthrough TX completion.
}

TEST_F(WlanServiceTestBrcm4390, ForwardingTxCompletion)
{
	constexpr uint16_t kTestPktId = 1;
	FakeRing &tx_cmpl_ring = wdev_tx_cmpl_fake_ring_pool_[0];
	HostTxbufCmpl *tx_cmpl_desc = reinterpret_cast<HostTxbufCmpl *>(tx_cmpl_ring.ring_buffer);
	int32_t wdev_rx_irq_num =
		static_cast<int32_t>(wlan_svc_.dp.wlan_dev_intr_ctx_group[0].irq_num);
	const BmTkidItem *bm_locker;

	ASSERT_EQ(BmPlaceOnHold(kNoaTxBufferManager, kTestPktId), 0);

	ASSERT_NE(tx_cmpl_desc, nullptr);
	// Set the packet ID in the descriptor.
	tx_cmpl_desc->cmn_hdr.request_id = kTestPktId + WLAN_NEP_PKTID_MASK;

	// Set the read and write pointers in the fake ring to simulate a packet
	// arrival.
	SysIfIoWritel(0U, reinterpret_cast<void *>(&tx_cmpl_ring.read));
	SysIfIoWritel(1U, reinterpret_cast<void *>(&tx_cmpl_ring.write));
	// Trigger the WLAN device RX interrupt handler.
	noa::module::interrupt::InterruptController::Instance()->ExecuteInterruptHandler(
		wdev_rx_irq_num);
	// Process the received packet in the WLAN DP RX task.
	WlanDpWdevRxTask(&wlan_svc_.dp.wlan_dev_dp_worker, 1U);

	// Verify that the TX completion is handled correctly.
	ASSERT_EQ(BmFind(kNoaTxBufferManager, kTestPktId, &bm_locker), 0);
}

TEST_F(WlanServiceTestBrcm4390, FeedthroughRx)
{
	// The captured packet used in this test can be found in:
	// tools/wlan/brcm4390/feedthrough_rx_4390_one_shot.txt
	constexpr uint8_t kFeedthroughRxCmplRaw[] = {
		0x12, 0x0, 0x0,	 0x3d, 0x20, 0x1e, 0x0, 0x0, 0x0,  0x0,	 0x0,  0x0, 0x0, 0x0,
		0x43, 0x0, 0x42, 0x0,  0x1,  0x8,  0x0, 0x0, 0x0,  0x0,	 0x0,  0x0, 0x0, 0x0,
		0x0,  0x0, 0x0,	 0x0,  0x0,  0x0,  0x0, 0x0, 0x70, 0x1e, 0x42, 0x35
	};
	constexpr uint8_t kFeedthroughRxPayloadRaw[] = {
		0x43, 0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x16, 0x6c, 0x2a, 0x11,
		0xae, 0x7a, 0x4,  0x42, 0x1a, 0xbc, 0xef, 0xa0, 0x8,  0x0,  0x45, 0x0,	0x0,  0x35,
		0x0,  0x0,  0x40, 0x0,	0x39, 0x11, 0x94, 0xe9, 0x8e, 0xfb, 0x2a, 0xe4, 0xc0, 0xa8,
		0x32, 0x47, 0x1,  0xbb, 0x8d, 0x99, 0x0,  0x21, 0x65, 0x5d, 0x59, 0x76, 0xc8, 0x7,
		0xa1, 0x18, 0xa0, 0x96, 0xeb, 0x86, 0x17, 0x8a, 0xe,  0x7d, 0x38, 0xb,	0xd4, 0x7e,
		0xc9, 0xf3, 0x2,  0xdd, 0x73, 0x14, 0x9d
	};
	constexpr uint16_t kTestPktId = 2;
	constexpr uint32_t kWlanPktPad = 80;
	FakeBuffer &fake_buffer = apc_rx_buffer_pool_[kTestPktId - 1];
	FakeRing &fake_ring = wdev_rx_cmpl_fake_ring_pool_[0];
	HostRxbufCmpl *rxd = reinterpret_cast<HostRxbufCmpl *>(fake_ring.ring_buffer);
	int32_t wdev_rx_irq_num =
		static_cast<int32_t>(wlan_svc_.dp.wlan_dev_intr_ctx_group[0].irq_num);

	// Verify the fake buffer has the expected packet ID.
	ASSERT_EQ(fake_buffer.pktid, kTestPktId);
	// Verify the size of the HostRxbufCmpl structure matches the raw data.
	ASSERT_EQ(sizeof(HostRxbufCmpl), sizeof(kFeedthroughRxCmplRaw));
	// Copy the raw RX completion data into the RX completion descriptor.
	std::memcpy(rxd, kFeedthroughRxCmplRaw, sizeof(HostRxbufCmpl));
	// Set the request ID in the RX completion header to match the fake buffer.
	rxd->cmn_hdr.request_id = fake_buffer.pktid;
	// Verify the fake buffer is large enough to hold the payload.
	ASSERT_GE(fake_buffer.buffer_size, sizeof(kFeedthroughRxPayloadRaw));
	// Verify the fake buffer has a valid memory address.
	ASSERT_NE(fake_buffer.ori_buffer, nullptr);
	// Copy the raw payload data into the fake buffer.
	std::memcpy(fake_buffer.buffer, kFeedthroughRxPayloadRaw, sizeof(kFeedthroughRxPayloadRaw));

	// Set the read and write pointers in the fake ring to simulate a packet
	// arrival.
	SysIfIoWritel(0U, reinterpret_cast<void *>(&fake_ring.read));
	SysIfIoWritel(1U, reinterpret_cast<void *>(&fake_ring.write));
	// Trigger the WLAN device RX interrupt handler.
	noa::module::interrupt::InterruptController::Instance()->ExecuteInterruptHandler(
		wdev_rx_irq_num);
	// Process the received packet in the WLAN DP RX task.
	WlanDpWdevRxTask(&wlan_svc_.dp.wlan_dev_dp_worker, 1U);

	struct noa_ring_regs nep_ring_regs;
	// Get the NOA ring registers for the NetEngine input port.
	ASSERT_EQ(NoaRingSharedRegsGet(&nep_ring_regs, kNoaNetworkInterfaceWlan,
				       kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData,
				       kNoaRingNepInput),
		  0);
	// Verify that the NetEngine input ring's write pointer has been updated.
	ASSERT_EQ(SysIfIoReadl(reinterpret_cast<void *>(nep_ring_regs.write)), 1U);
	// Get the NOA descriptor from the NetEngine input ring.
	NoaDesc *noa_desc =
		reinterpret_cast<NoaDesc *>(*reinterpret_cast<uintptr_t *>(nep_ring_regs.base));
	// Verify that the NOA descriptor is valid.
	ASSERT_NE(noa_desc, nullptr);
	// Verify the NOA descriptor fields are set correctly for a feedthrough RX
	// packet.
	EXPECT_EQ(noa_desc->dst,
		  NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost,
				       kNoaWlanRingRxData));
	EXPECT_EQ(noa_desc->cp, NOAD_NO_COPY);
	EXPECT_EQ(noa_desc->fk, NOAD_FEEDBACK_DISABLE);
	EXPECT_EQ(noa_desc->mode, NOAD_MODE_DATA);
	EXPECT_EQ(noa_desc->desc_type, NOA_DESC_BASIC);
}

TEST_F(WlanServiceTestBrcm4390, ForwardingRx)
{
	// The captured packet used in this test can be found in:
	// tools/wlan/brcm4390/forwarding_4390_raw.txt
	constexpr uint8_t kForwardingRxCmplRaw[] = { 0x12, 0x2, 0x0, 0x44, 0xe0, 0x15, 0x0,  0x0,
						     0x0,  0x0, 0x0, 0x0,  0x0,	 0x0,  0x42, 0x0,
						     0x42, 0x0, 0x1, 0x8,  0x0,	 0x0,  0x0,  0x0,
						     0x0,  0x0, 0x0, 0x0,  0x0,	 0x0,  0x0,  0x0,
						     0x0,  0x0, 0x0, 0x0,  0xb0, 0x17, 0x43, 0x4c };
	constexpr uint8_t kForwardingRxPayloadRaw[] = {
		0x42, 0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,
		0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0x0,  0x0,	0x0,  0x0,  0xe6, 0x38, 0xf4, 0xcf,
		0x5c, 0x45, 0xca, 0x8e, 0xd3, 0x84, 0x87, 0x2c, 0x8,  0x0,  0x45, 0x0,	0x0,  0x34,
		0x0,  0x0,  0x40, 0x0,	0x40, 0x6,  0x44, 0x4c, 0xc0, 0xa8, 0x9b, 0x62, 0x97, 0x65,
		0x3,  0x8,  0xd4, 0x9d, 0x1,  0xbb, 0x96, 0x99, 0x2d, 0x51, 0xd9, 0x3a, 0x62, 0xa5,
		0x80, 0x10, 0xcb, 0x63, 0x36, 0x41, 0x0,  0x0,	0x1,  0x1,  0x8,  0xa,	0x95, 0xec,
		0x5c, 0xf8, 0xc4, 0xee, 0xf0, 0xa8
	};
	constexpr uint16_t kTestPktId = 3;
	FakeBuffer &fake_buffer = apc_rx_buffer_pool_[kTestPktId - 1];
	FakeRing &fake_ring = wdev_rx_cmpl_fake_ring_pool_[0];
	HostRxbufCmpl *rxd = reinterpret_cast<HostRxbufCmpl *>(fake_ring.ring_buffer);
	int32_t wdev_rx_irq_num =
		static_cast<int32_t>(wlan_svc_.dp.wlan_dev_intr_ctx_group[0].irq_num);

	// Verify the fake buffer has the expected packet ID.
	ASSERT_EQ(fake_buffer.pktid, kTestPktId);
	// Verify the size of the HostRxbufCmpl structure matches the raw data.
	ASSERT_EQ(sizeof(HostRxbufCmpl), sizeof(kForwardingRxCmplRaw));
	// Copy the raw RX completion data into the RX completion descriptor.
	std::memcpy(rxd, kForwardingRxCmplRaw, sizeof(HostRxbufCmpl));
	// Set the request ID in the RX completion header to match the fake buffer.
	rxd->cmn_hdr.request_id = fake_buffer.pktid;
	// Verify the fake buffer is large enough to hold the payload.
	ASSERT_GE(fake_buffer.buffer_size, sizeof(kForwardingRxPayloadRaw));
	// Verify the fake buffer has a valid memory address.
	ASSERT_NE(fake_buffer.ori_buffer, nullptr);
	// Copy the raw payload data into the fake buffer.
	std::memcpy(fake_buffer.buffer, kForwardingRxPayloadRaw, sizeof(kForwardingRxPayloadRaw));

	// Set the read and write pointers in the fake ring to simulate a packet
	// arrival.
	SysIfIoWritel(0U, reinterpret_cast<void *>(&fake_ring.read));
	SysIfIoWritel(1U, reinterpret_cast<void *>(&fake_ring.write));
	// Trigger the WLAN device RX interrupt handler.
	noa::module::interrupt::InterruptController::Instance()->ExecuteInterruptHandler(
		wdev_rx_irq_num);
	// Process the received packet in the WLAN DP RX task.
	WlanDpWdevRxTask(&wlan_svc_.dp.wlan_dev_dp_worker, 1U);

	struct noa_ring_regs nep_ring_regs;
	// Get the NOA ring registers for the NetEngine input port.
	ASSERT_EQ(NoaRingSharedRegsGet(&nep_ring_regs, kNoaNetworkInterfaceWlan,
				       kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData,
				       kNoaRingNepInput),
		  0);
	// Verify that the NetEngine input ring's write pointer has been updated.
	ASSERT_EQ(SysIfIoReadl(reinterpret_cast<void *>(nep_ring_regs.write)), 1U);
	// Get the NOA descriptor from the NetEngine input ring.
	NoaDesc *noa_desc =
		reinterpret_cast<NoaDesc *>(*reinterpret_cast<uintptr_t *>(nep_ring_regs.base));
	// Verify that the NOA descriptor is valid.
	ASSERT_NE(noa_desc, nullptr);
	// Verify the NOA descriptor fields are set correctly for a forwarding RX
	// packet.
	EXPECT_EQ(noa_desc->dst, NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
						      kNoaNetengineTunnel, kNoaNetengineRingData));
	EXPECT_EQ(noa_desc->cp, NOAD_COPY_DATA);
	EXPECT_EQ(noa_desc->fk, NOAD_FEEDBACK_ENABLE);
	EXPECT_EQ(noa_desc->mode, NOAD_MODE_DATA);
	EXPECT_EQ(noa_desc->desc_type, NOA_DESC_BASIC);
}
} // namespace testing
} // namespace noa::service::wlan_service
