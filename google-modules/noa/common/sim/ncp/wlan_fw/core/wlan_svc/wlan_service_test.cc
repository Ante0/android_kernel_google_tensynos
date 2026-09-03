#include "wlan_service.h"

#include <cstring>

#include "modules/memory_map/memory_map.h"
#include "core/wlan_shared_mem/memory_map_helper.h"
#include "gtest/gtest.h"
#include "wlan_service_rpc_protocol.h"
#include "sys_if/log/sys_if_log_test_lib.h"
#include "wlan_service_test_lib.h"
#include "wdev_if/hw_arch/brcm/brcm_msgbuf.h"

namespace noa::service::wlan_service
{
namespace testing
{

// Number of descriptors in a fake ring.
constexpr uint32_t kFakeRingNumDesc = 10;
// Number of TX post rings.
constexpr uint32_t kFakeWdevTxPostRingNum = 1;
// Number of RX post rings.
constexpr uint32_t kFakeWdevRxPostRingNum = 1;
// Number of TX completion rings.
constexpr uint32_t kFakeWdevTxCmplRingNum = 1;
// Number of RX completion rings.
constexpr uint32_t kFakeWdevRxCmplRingNum = 1;
// Sequence number of a Wdev Ring
constexpr uint32_t kFakeRingSn = 133;

typedef struct FakeDesc {
	uint8_t txhdr[BCM_ETHER_HDR_LEN];
	uint64_t data_buf_addr;
	uint16_t data_len;
} FakeDesc;

class WlanServiceTest : public WlanServiceTestBasic {
    public:
	WlanServiceTest()
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
		for (uint32_t i = 0; i < kFakeWdevTxPostRingNum; i++) {
			wdev_tx_post_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(FakeDesc));
		}

		for (uint32_t i = 0; i < kFakeWdevRxPostRingNum; i++) {
			wdev_rx_post_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(FakeDesc));
		}

		for (uint32_t i = 0; i < kFakeWdevTxCmplRingNum; i++) {
			wdev_tx_cmpl_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(FakeDesc));
		}

		for (uint32_t i = 0; i < kFakeWdevRxCmplRingNum; i++) {
			wdev_rx_cmpl_fake_ring_pool_.emplace_back(kFakeRingNumDesc,
								  sizeof(FakeDesc));
		}
	}

	~WlanServiceTest() = default;

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

TEST_F(WlanServiceTest, PlatInitSuccess)
{
	EXPECT_EQ(wlan_svc_.state, kWlanServiceStateStart);
}

TEST_F(WlanServiceTest, WlanServiceShellRequestUnknownCommand)
{
	WlanCmdShellRequest shell_request;

	std::memset(&shell_request, 0, sizeof(shell_request));
	std::strncpy(shell_request.cmd, "Unknown cmd", sizeof(shell_request.cmd));

	EXPECT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_request), 0, nullptr),
		  -EINVAL);
}

TEST_F(WlanServiceTest, WlanServiceSyncIrqInfo)
{
	char test_buf[1024];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_request;
	std::memset(&shell_request, 0, sizeof(shell_request));
	std::strncpy(shell_request.cmd, "irq-info", sizeof(shell_request.cmd));
	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_request), 0, nullptr),
		  0);

	// Check if the expected output is present in the buffer
	char *result = std::strstr(test_buf, "==== NOA Wlan Interrupt Statistic ===");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "Ring Service NEP to NCP Total Interrupt Count:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "PCIe MSI NCP Total Interrupt Count:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "PCIe MSI APC Total Interrupt Count:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "Ring Service NEP to APC Total Interrupt Count:");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, CmdDumpStaInfo)
{
	FakeStationSetup();

	char test_buf[1024];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "sta-info", sizeof(shell_req.cmd));

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_req), 0, nullptr),
		  0);

	PW_LOG_INFO("%s\n", test_buf);

	// Check if the expected output is present in the buffer
	char *result = std::strstr(test_buf, "==== NOA Wlan STA Info ===");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "mac_addr: 00:01:02:03:04:05");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "qos_txq_map: 0 1 2 3 4 5 6 7");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, CmdSyncNepRingInfo)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "nep-ring-info", sizeof(shell_req.cmd));

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_req), 0, nullptr),
		  0);

	char *result = std::strstr(test_buf, "==== NOA NEP Ring Info ===");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "wlan_fw_txpost_ring_0");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "wlan_fw_rxcmpl_ring_0");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, WlanServiceFwStop)
{
	struct NoaWlanRingInfo *ring_pool =
		GetNoaWLanWdevRingPool(&wlan_svc_.memory_map_helper, kTxPostRingPool);
	WlanRing *ring;
	WlanRingManagerGetRing(&wlan_svc_.ring_manager, kWdevTxPostRingGroup, 0, &ring);
	ring->sn = kFakeRingSn;
	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdFwStop, nullptr, 0, nullptr), 0);

	EXPECT_EQ(ring_pool[0].sn, kFakeRingSn);
}

TEST_F(WlanServiceTest, WlanServiceShellSyncMibInfo)
{
	WlanDpStats *dp_stats;
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "mib-info", sizeof(shell_req.cmd));

	dp_stats = &wlan_svc_.dp.dp_stats;
	dp_stats->rx = 13;
	dp_stats->tx = 15;
	dp_stats->dev_tx[0] = 15;
	dp_stats->nep_fw_input = 13;

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_req), 0, nullptr),
		  0);

	char *result = std::strstr(test_buf, "total_rx_pkt_cnt value: 13");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "total_tx_pkt_cnt value: 15");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "device_tx_ring_pkt_cnt[0] value: 15");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "nep_fw_input_ring_pkt_cnt value: 13");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, WlanServiceShellSetDpMode)
{
	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "set-mode", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "vpn", std::strlen("vpn"));
	shell_req.argv_len = std::strlen("vpn");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_req), 0, nullptr),
		  0);

	EXPECT_EQ(wlan_svc_.dp.mode, kWlanDpVpnForceFwdToNetEngineMode);
}

TEST_F(WlanServiceTest, WlanServiceShellDumpMemoryMapSection)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "memory-map-section", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "WDEV_RING_CONFIG 1 0", std::strlen("WDEV_RING_CONFIG 1 0"));
	shell_req.argv_len = std::strlen("WDEV_RING_CONFIG 1 0");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
				     reinterpret_cast<void *>(&shell_req), 0, nullptr),
		  0);

	char *result = std::strstr(
		test_buf,
		"=============== (NCP) NOA WLAN WDEV_RING_CONFIG 1 0 Value ===============");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, WlanServiceShellWakeLockAcquire)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "wake-lock", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "acquire", std::strlen("acquire"));
	shell_req.argv_len = std::strlen("acquire");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
		reinterpret_cast<void *>(&shell_req), 0, nullptr),
		0);

	char *result = std::strstr(test_buf, "Acquire wake lock");
	EXPECT_NE(result, nullptr);

	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "wake-lock", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "state", std::strlen("state"));
	shell_req.argv_len = std::strlen("state");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
		reinterpret_cast<void *>(&shell_req), 0, nullptr),
		0);

	result = std::strstr(test_buf, "Wake lock is acquired");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, WlanServiceShellWakeLockRelease)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "wake-lock", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "release", std::strlen("release"));
	shell_req.argv_len = std::strlen("release");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
		reinterpret_cast<void *>(&shell_req), 0, nullptr),
		0);

	char *result = std::strstr(test_buf, "Release wake lock");
	EXPECT_NE(result, nullptr);

	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "wake-lock", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "state", std::strlen("state"));
	shell_req.argv_len = std::strlen("state");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
		reinterpret_cast<void *>(&shell_req), 0, nullptr),
		0);

	result = std::strstr(test_buf, "Wake lock is released");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(WlanServiceTest, WlanServiceShellWakeLockUnknownCommand)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	WlanCmdShellRequest shell_req;
	std::memset(&shell_req, 0, sizeof(shell_req));
	std::strncpy(shell_req.cmd, "wake-lock", sizeof(shell_req.cmd));
	std::strncpy(shell_req.argv, "get", std::strlen("get"));
	shell_req.argv_len = std::strlen("get");

	ASSERT_EQ(WlanServiceCommand(&wlan_svc_, kWlanCmdShellRequest,
		reinterpret_cast<void *>(&shell_req), 0, nullptr),
		-EINVAL);

	char *result = std::strstr(test_buf, "Unknown command");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

} // namespace testing
} // namespace noa::service::wlan_service
