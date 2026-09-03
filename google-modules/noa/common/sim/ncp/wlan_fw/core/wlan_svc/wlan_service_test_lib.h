#pragma once

#include "wlan_service.h"

#include <cstdint>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <queue>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "wlan_service_rpc_protocol.h"
#include "wdev_if/wdev_if.h"
#include "modules/wlan_ring_manager/wlan_ring_manager.h"
#include "modules/wlan_ring_manager/wlan_wdev_ring_manager.h"
#include "interrupt/interrupt.h"
#include "ring_mgmt/mock_ring_shared_info.h"
#include "core/dp/wlan_dp.h"
#include "modules/sta_table/sta_table.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"
#include "pw_toolchain/no_destructor.h"
#include "core/wlan_shared_mem/memory_map_helper.h"
#include "noa/wlan/noa_wlan.h"

namespace noa::service::wlan_service
{

namespace testing
{

using ::noa::service::ring_service::MockRingSharedInfo;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// Size of the APC RX buffer pool.
constexpr uint32_t kFakeApcRxBufferPoolSize = 4096;
// Size of the NOA TX buffer pool.
constexpr uint32_t kFakeNoaTxBufferPoolSize = 4096;
// Size of a fake buffer
constexpr uint32_t kFakeBufferSize = 2048;
// Size of a headroom
constexpr uint32_t kBufferHeadRoom = 128;
const uint32_t kRingSvcMaxItem = 8;
const uint32_t kRingSvcItemSize = 64;

static_assert(NOA_DESC_MAX_BYTE <= kRingSvcItemSize);

typedef std::pair<uint32_t, std::vector<uint8_t> > RpcEntry;

class FakeWlanRpcService {
    public:
	FakeWlanRpcService() = default;
	~FakeWlanRpcService() = default;

	void ResetEventService()
	{
		while (!event_queue_.empty()) {
			event_queue_.pop();
		}
	}

	int32_t SendEvent(uint32_t event, void *msg, const uint32_t msg_len)
	{
		std::vector<uint8_t> payload(msg_len);

		if (msg != nullptr && msg_len > 0) {
			std::memcpy(payload.data(), msg, msg_len);
		}

		event_queue_.push(std::make_pair(event, payload));

		return 0;
	}

	static FakeWlanRpcService &GetInstance()
	{
		static pw::NoDestructor<FakeWlanRpcService> svc;
		return *svc;
	}

	std::queue<RpcEntry> &GetEventQueue()
	{
		return event_queue_;
	}

    private:
	std::queue<RpcEntry> event_queue_;
};

struct FakeRing {
	FakeRing(uint32_t ndesc, uint32_t desc_size) : ndesc(ndesc), desc_size(desc_size)
	{
		ring_buffer = std::malloc(ndesc * desc_size);
		base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ring_buffer));
		len = desc_size;
		max_item = ndesc;
		read = 0;
		write = 0;
	}

	~FakeRing()
	{
		if (ring_buffer) {
			std::free(ring_buffer);
		}
	}

	uint32_t ndesc;
	uint32_t desc_size;
	void *ring_buffer;
	uint32_t base;
	uint32_t len;
	uint32_t max_item;
	uint32_t read;
	uint32_t write;
};

struct FakeBuffer {
	FakeBuffer(uint16_t pktid, uint32_t buffer_size) : pktid(pktid), buffer_size(buffer_size)
	{
		ori_buffer = std::malloc(buffer_size);
		buffer = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(ori_buffer) +
						  kBufferHeadRoom);
	}

	~FakeBuffer()
	{
		if (ori_buffer) {
			std::free(ori_buffer);
		}
	}

	uint16_t pktid;
	uint32_t buffer_size;
	void *ori_buffer;
	void *buffer;
};

class WlanServiceTestBasic : public ::testing::Test {
    protected:
	WlanServiceTestBasic(uint32_t ring_desc_num, uint32_t tx_post_ring_num,
			     uint32_t rx_post_ring_num, uint32_t tx_cmpl_ring_num,
			     uint32_t rx_cmpl_ring_num)
	{
		// Reserve space for APC RX buffer pool.
		apc_rx_buffer_pool_.reserve(kFakeApcRxBufferPoolSize);
		// Reserve space for NOA TX buffer pool.
		noa_tx_buffer_pool_.reserve(kFakeNoaTxBufferPoolSize);
		// Initialize APC RX buffer pool.
		for (uint32_t i = 0; i < kFakeApcRxBufferPoolSize; i++) {
			apc_rx_buffer_pool_.emplace_back(i + 1, kFakeBufferSize);
		}
		// Initialize NOA TX buffer pool.
		for (uint32_t i = 0; i < kFakeNoaTxBufferPoolSize; i++) {
			noa_tx_buffer_pool_.emplace_back(i + 1, kFakeBufferSize);
		}

		// Initialize WlanService.
		std::memset(&wlan_svc_, 0, sizeof(wlan_svc_));

		ring_desc_num_ = ring_desc_num;
		tx_post_ring_num_ = tx_post_ring_num;
		rx_post_ring_num_ = rx_post_ring_num;
		tx_cmpl_ring_num_ = tx_cmpl_ring_num;
		rx_cmpl_ring_num_ = rx_cmpl_ring_num;
	}

	~WlanServiceTestBasic() = default;

	void SetUp() override
	{
		// Setup NOA system environment.
		NoaSystemEnvironmentSetup();

		// Platform initialization.
		WlanSvcPlatformInit();
	}

	// Initialize the WLAN service platform.
	void WlanSvcPlatformInit()
	{
		WlanServicePlatformInitParams svc_init_params;
		std::memset(&svc_init_params, 0, sizeof(svc_init_params));
		svc_init_params.send_event_to_apc = [](uint32_t event, void *msg,
						       const uint32_t msg_len) -> int32_t {
			return FakeWlanRpcService::GetInstance().SendEvent(event, msg, msg_len);
		};
		svc_init_params.nep_ring_activate = [](void) -> int32_t { return 0; };
		svc_init_params.nep_input_ring_deactivate = [](void) -> int32_t { return 0; };
		svc_init_params.nep_output_ring_deactivate = [](void) -> int32_t { return 0; };
		svc_init_params.send_command_to_net_engine = [](int32_t, void *,
								size_t) -> int32_t { return 0; };
		svc_init_params.nep_ring_buffer_pool_activate = [](void) -> int32_t { return 0; };
		svc_init_params.nep_ring_buffer_pool_deactivate = [](void) -> int32_t { return 0; };
		svc_init_params.noa_power_vote = [](bool) -> int32_t { return 0; };
		svc_init_params.nep_ring_buffer_pool_deactivate = [](void) -> int32_t { return 0; };
		ASSERT_EQ(WlanServicePlatInit(&wlan_svc_, &svc_init_params), 0);
	}

	// Update NEP device rings.
	void NepRingUpdate(std::vector<FakeRing> &ring_pool, RingGroupType ring_type)
	{
		const uint32_t kPoolSize = ring_pool.size();
		struct NoaWlanNepRingConfig *ring_config =
			reinterpret_cast<struct NoaWlanNepRingConfig *>(GetNoaWlanConfigSectionAddr(
				&wlan_svc_.memory_map_helper, kNepRingConfig));
		struct NoaWlanRingPoolConfig *ring_pool_config = ring_config->nep_ring_pool_config;
		uint32_t memory_map_ring_type = 0;

		switch (ring_type) {
		case kNepTxCmplRingGroup:
			memory_map_ring_type = kTxCmplRingPool;
			break;
		default:
			ASSERT_EQ(1, 0);
			break;
		}
		struct NoaWlanRingInfo *nep_ring_pool =
			(struct NoaWlanRingInfo *)ring_pool_config[memory_map_ring_type].ring_pool;

		ring_pool_config[memory_map_ring_type].count = kPoolSize;
		ring_pool_config[memory_map_ring_type].ring_type = memory_map_ring_type;

		for (uint32_t i = 0; i < kPoolSize; i++) {
			struct NoaWlanRingInfo &ring_update_info = nep_ring_pool[i];
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

	// Refill buffers.
	void BufferRefill(std::vector<FakeBuffer> &buffer_pool, BufferManagerType bm_type)
	{
		const uint32_t kBufferPoolSize = buffer_pool.size();

		for (uint32_t i = 0; i < kBufferPoolSize; i++) {
			FakeBuffer &fake_buffer = buffer_pool[i];
			ASSERT_EQ(BmAcquire(bm_type, fake_buffer.pktid, fake_buffer.buffer_size,
					    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
						    fake_buffer.buffer)),
					    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
						    fake_buffer.buffer))),
				  0);
		}
	}

	void FakeStationSetup()
	{
		std::memset(&fake_sta_, 0, sizeof(fake_sta_));

		fake_sta_.enable = 1;
		for (uint16_t tid = kWlanTidStart; tid < kWlanTidEnd; tid++) {
			uint16_t ring_id = tid;
			fake_sta_.qos_txq_map[tid] = ring_id;
		}

		for (uint8_t mac_addr_idx = kMacAddressPartStart; mac_addr_idx < kMacAddressPartEnd;
		     mac_addr_idx++) {
			fake_sta_.mac_addr[mac_addr_idx] = mac_addr_idx;
		}

		// Update to the shared memory
		struct NoaWlanStaInfo *sta_info_list = reinterpret_cast<NoaWlanStaInfo *>(
			GetNoaWlanConfigSectionAddr(&wlan_svc_.memory_map_helper, kStaInfo));
		sta_info_list[0].enable = fake_sta_.enable;
		memcpy(sta_info_list[0].mac_addr, fake_sta_.mac_addr, sizeof(fake_sta_.mac_addr));
		memcpy(sta_info_list[0].qos_txq_map, fake_sta_.qos_txq_map,
		       sizeof(fake_sta_.qos_txq_map));

		WlanCmdStaActivate sync_cmd = { .enable = 1, .sta_table_idx = 0 };

		ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdStaActivate,
					     reinterpret_cast<void *>(&sync_cmd), 0, nullptr),
			  0);
	}

	void ApcDirectRingSetup(struct noa_ring &noa_ring, void *buffer, uint32_t desc_size,
				uint32_t max_item)
	{
		noa_ring.ctrl = max_item;
		noa_ring.len = desc_size;
		noa_ring.read = 0;
		noa_ring.write = 0;
		noa_ring.read = 0;
		noa_ring.dpa_base = reinterpret_cast<uintptr_t>(buffer);
		noa_ring.base = reinterpret_cast<uintptr_t>(buffer);
	}

	// Setup NOA system environment.
	void NoaSystemEnvironmentSetup()
	{
		// System interrupt setup.
		noa::module::interrupt::InterruptController::Instance()->Deinit();
		noa::module::interrupt::InterruptController::Instance()->Init(UINT16_MAX);
		// Ring service setup.
		std::memset(&nep_txpost_ring_, 0, sizeof(nep_txpost_ring_));
		ON_CALL(info_, GetRing(_, kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData, _))
			.WillByDefault(Return(&nep_txpost_ring_));
		std::memset(&nep_rxcmpl_ring_, 0, sizeof(nep_rxcmpl_ring_));
		ON_CALL(info_, GetRing(_, kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData, _))
			.WillByDefault(Return(&nep_rxcmpl_ring_));
		// APC D2H ring
		for (uint32_t i = 0; i < kNoaWlanDirectD2HRingMax; i++) {
			ON_CALL(info_, GetRing(_, kNoaNetworkFlowDeviceToHost, i, _))
				.WillByDefault(Return(&direct_d2h_ring_pool_[i]));
		}
		// APC H2D ring
		for (uint32_t i = 0; i < kNoaWlanDirectH2DRingMax; i++) {
			ON_CALL(info_, GetRing(_, kNoaNetworkFlowHostToDevice, i, _))
				.WillByDefault(Return(&direct_h2d_ring_pool_[i]));
		}
		ASSERT_EQ(
			NoaRingSharedInfoRootRegister(static_cast<NoaRingSharedInfoRoot *>(&info_)),
			0);
		// APC D2H ring
		for (uint32_t i = 0; i < kNoaWlanDirectD2HRingMax; i++) {
			ApcDirectRingSetup(direct_d2h_ring_pool_[i],
					   direct_d2h_ring_buffer_pool_[i], kRingSvcItemSize,
					   kRingSvcMaxItem);
		}
		// APC H2D ring
		for (uint32_t i = 0; i < kNoaWlanDirectH2DRingMax; i++) {
			ApcDirectRingSetup(direct_h2d_ring_pool_[i],
					   direct_h2d_ring_buffer_pool_[i], kRingSvcItemSize,
					   kRingSvcMaxItem);
		}
	}

	// Tear down NOA system environment.
	void NoaSystemEnvironmentTearDown()
	{
		// Deinitialize interrupt controller.
		noa::module::interrupt::InterruptController::Instance()->Deinit();
		// Unregister ring services.
		NoaRingSharedInfoRootRegister(nullptr);
	}

	std::vector<FakeBuffer> apc_rx_buffer_pool_;
	std::vector<FakeBuffer> noa_tx_buffer_pool_;
	std::vector<FakeRing> wdev_tx_post_fake_ring_pool_;
	std::vector<FakeRing> wdev_rx_post_fake_ring_pool_;
	std::vector<FakeRing> wdev_tx_cmpl_fake_ring_pool_;
	std::vector<FakeRing> wdev_rx_cmpl_fake_ring_pool_;
	struct noa_ring nep_rxcmpl_ring_;
	struct noa_ring nep_txpost_ring_;
	struct noa_ring direct_d2h_ring_pool_[kNoaWlanDirectD2HRingMax];
	struct noa_ring direct_h2d_ring_pool_[kNoaWlanDirectH2DRingMax];
	NiceMock<MockRingSharedInfo> info_;
	uint8_t test_data_buf_[kFakeBufferSize];
	StaInfo fake_sta_;
	WlanService wlan_svc_;
	void *shared_memory_;
	uint32_t ring_desc_num_;
	uint32_t tx_post_ring_num_;
	uint32_t rx_post_ring_num_;
	uint32_t tx_cmpl_ring_num_;
	uint32_t rx_cmpl_ring_num_;
	uint8_t direct_d2h_ring_buffer_pool_[kNoaWlanDirectD2HRingMax]
					    [kRingSvcMaxItem * kRingSvcItemSize];
	uint8_t direct_h2d_ring_buffer_pool_[kNoaWlanDirectH2DRingMax]
					    [kRingSvcMaxItem * kRingSvcItemSize];
};

} // namespace testing
} // namespace noa::service::wlan_service
