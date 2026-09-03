#include "memory_map_helper.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "common/wlan/memory_map.h"
#include "core/dp/wlan_dp.h"
#include "sys_if/log/sys_if_log_test_lib.h"

namespace noa::service::wlan_service
{
namespace
{

constexpr uint64_t kChiptype = 0x12345678;
constexpr uint32_t kTxPktMax = 300;
constexpr uint64_t kSharedAddress = 0xABCDEF01;

class FwMemoryMapHelperTest : public ::testing::Test {
    protected:
	void SetUp() override
	{
		buffer = malloc(sizeof(NoaWlanSharedInfo));
		ASSERT_NE(buffer, nullptr);

		params.base_addr = reinterpret_cast<uint64_t>(buffer);
		params.size = sizeof(NoaWlanSharedInfo);

		MemoryMapHelperInit(&helper, &params);
		WlanLogModuleSetLogLevel(kWlanLogModuleShell, kWlanLogLevelInfo);
	}

	void TearDown() override
	{
		free(buffer);
	}

	MemoryMapHelper helper;
	void *buffer;
	MemoryMapHelperInitParams params;
};

TEST_F(FwMemoryMapHelperTest, MemoryMapHelperInit)
{
	EXPECT_EQ(helper.base_addr, params.base_addr);
	EXPECT_EQ(helper.size, params.size);
}

TEST_F(FwMemoryMapHelperTest, WlanGlobalConfigRead)
{
	NoaGlobalConfig *config = &reinterpret_cast<NoaWlanSharedInfo *>(buffer)->global_config;
	config->chip_type = kChiptype;
	config->tx_pkt_max = kTxPktMax;
	config->share_addr = kSharedAddress;

	// Test read value of uint64_t type
	uint64_t chip_type;
	ASSERT_EQ(WlanGlobalConfigRead(&helper, kChipType, sizeof(chip_type), &chip_type), 0);
	EXPECT_EQ(chip_type, kChiptype);
	// Test read value of uint32_t type
	uint32_t tx_pkt_max;
	ASSERT_EQ(WlanGlobalConfigRead(&helper, kVendorTxPacketIdMax, sizeof(tx_pkt_max),
				       &tx_pkt_max),
		  0);
	EXPECT_EQ(tx_pkt_max, kTxPktMax);
	// Test read an address
	uint64_t share_addr;
	ASSERT_EQ(WlanGlobalConfigRead(&helper, kShareAddress, sizeof(share_addr), &share_addr), 0);
	EXPECT_EQ(share_addr, kSharedAddress);
}

TEST_F(FwMemoryMapHelperTest, UpdateNoaWlanMIBInfo)
{
	WlanDpStats dp_stats = {
		.rx = 10,
		.rx_forward = 10,
		.tx = 11,
		.tx_forward = 10,
		.tx_err = 1,
	};

	dp_stats.nep_fw_output[0] = 5;
	dp_stats.nep_fw_output[1] = 10;

	struct NoaMib *mib = UpdateNoaWlanMIBInfo(&helper, &dp_stats);

	ASSERT_NE(mib, nullptr);
	EXPECT_EQ(10, mib->total_rx_pkt_cnt);
	EXPECT_EQ(10, mib->total_rx_forward_pkt_cnt);
	EXPECT_EQ(11, mib->total_tx_pkt_cnt);
	EXPECT_EQ(10, mib->total_tx_forward_pkt_cnt);
	EXPECT_EQ(1, mib->total_tx_pkt_err_cnt);
	EXPECT_EQ(5, mib->nep_fw_output_ring_pkt_cnt[0]);
	EXPECT_EQ(10, mib->nep_fw_output_ring_pkt_cnt[1]);
}

TEST_F(FwMemoryMapHelperTest, MemoryMapoffsetDump)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	MemoryMapoffsetDump(&helper);

	char *result = std::strstr(test_buf, "(NCP) NOA WLAN Shared Memory Map Offset");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "GLOBAL_CONFIG size: ");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "BUFFER_MGMT size: ");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(FwMemoryMapHelperTest, DumpMemoryMapGlobalConfigSection)
{
	char test_buf[1024];
	char str_buf[15];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	NoaGlobalConfig *config = &reinterpret_cast<NoaWlanSharedInfo *>(buffer)->global_config;
	config->chip_type = kChiptype;
	config->tx_pkt_max = kTxPktMax;
	config->share_addr = kSharedAddress;

	DumpMemoryMapGlobalConfigSection(&helper);

	char *result = std::strstr(test_buf, "chip_type value:");
	EXPECT_NE(result, nullptr);
	sprintf(str_buf, "%u", kChipType & 0xFFFFFFFF);
	result = std::strstr(test_buf, str_buf);
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(FwMemoryMapHelperTest, DumpMemoryMapNepRingConfigSection)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	DumpMemoryMapNepRingConfigSection(&helper);

	char *result = std::strstr(test_buf, "NEP Ring TX 0 value");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "NEP Ring RX 0 value");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(FwMemoryMapHelperTest, DumpMemoryMapStaInfoSection)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);
	NoaWlanStaInfo *sta_infos = WLAN_REINTERPRET_CAST(
		NoaWlanStaInfo *, (helper.base_addr + offsetof(NoaWlanSharedInfo, sta_info)));

	sta_infos[0].enable = 1;
	sta_infos[0].oif = 12;
	sta_infos[0].bss_idx = 10;

	DumpMemoryMapStaInfoSection(&helper);

	char *result = std::strstr(test_buf, "Station Info 0 pointer address:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "oif value:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "bss_idx value:");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}

TEST_F(FwMemoryMapHelperTest, DumpMemoryMapBufferMgmtSection)
{
	char test_buf[4096];
	std::memset(test_buf, 0, sizeof(test_buf));
	SysIfLogTestInit(sizeof(test_buf), test_buf);

	struct BufferInfo buf_info = {
		.pktid = 11,
		.buffer_addr_cpu = kSharedAddress,
	};

	struct BufferManagementTableInfo table_info = { .version = 9, .refill_bitmap_size = 300 };

	DumpMemoryMapBufferMgmtSection(&helper, &buf_info, &table_info, true);

	char *result = std::strstr(test_buf, "Shared BM Table Info NOA View Addr:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "version value: 9");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "refill_bitmap_size value: 300");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "Shared BM Entry NOA View Addr:");
	EXPECT_NE(result, nullptr);
	result = std::strstr(test_buf, "pktid value: 11");
	EXPECT_NE(result, nullptr);

	SysIfLogTestDeinit();
}
} // namespace
} // namespace noa::service::wlan_service
