#include "wlan_debug_packet_sniffer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <cstdint>

#include "gtest/gtest.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "wlan_log/wlan_log.h"
#include "sys_if/log/sys_if_log_test_lib.h"
#include "pw_hex_dump/hex_dump.h"

typedef pw::ConstByteSpan ConstByteSpan;
typedef pw::dump::FormattedHexDumper FormattedHexDumper;
typedef pw::dump::FormattedHexDumper::Flags Flags;

namespace noa::service::wlan_service
{
namespace
{

constexpr uint32_t kTestLogBufferSize = 2048;

class WlanDebugPacketSnifferTest : public ::testing::Test {
    protected:
	WlanDebugPacketSnifferTest()
	{
		std::memset(g_test_log_buffer, 0, sizeof(g_test_log_buffer));
		SysIfLogTestInit(kTestLogBufferSize, g_test_log_buffer);
	}

	~WlanDebugPacketSnifferTest()
	{
		SysIfLogTestDeinit();
	}

	char g_test_log_buffer[kTestLogBufferSize];
};

static uint8_t fake_dram[PACKET_SNIFFER_PARTITION_SIZE];

void FillFakePacket(size_t size, uint8_t *packet)
{
	// Fill the packet with random data
	for (size_t i = 0; i < size; i++) {
		packet[i] = rand() % (UINT8_MAX + 1);
	}
}

TEST(WlanDebugPacketSniffer, RegularSniffing)
{
	uint8_t fake_packet[DEFAULT_PACKET_CAPTURE_SIZE];
	WlanPacketSnifferInit(nullptr, reinterpret_cast<uintptr_t>(fake_dram),
			      PACKET_SNIFFER_PARTITION_SIZE);
	WlanPacketSnifferSwitch(true);
	WlanPackerSnifferSetLocation(kDram);
	FillFakePacket(DEFAULT_PACKET_CAPTURE_SIZE, fake_packet);
	SnifferEntryHeader entry_header = { true, static_cast<uint32_t>(kNepRx),
					    DEFAULT_PACKET_CAPTURE_SIZE };

	WLAN_PACKET_SNIFF(reinterpret_cast<uintptr_t>(fake_packet), DEFAULT_PACKET_CAPTURE_SIZE,
			  NULL, NULL, NepRx);

	SnifferEntryHeader *header = reinterpret_cast<SnifferEntryHeader *>(fake_dram);
	EXPECT_TRUE(header->ring_type == entry_header.ring_type);
	EXPECT_TRUE(header->pkt_len == entry_header.pkt_len);
	EXPECT_TRUE(std::strncmp(reinterpret_cast<const char *>(fake_dram) +
					 sizeof(entry_header) * MAX_PACKET_ENTRY_NUM,
				 reinterpret_cast<const char *>(fake_packet),
				 DEFAULT_PACKET_CAPTURE_SIZE) == 0);
}

TEST(WlanDebugPacketSniffer, OverwriteSniffing)
{
	uint8_t fake_packet[DEFAULT_PACKET_CAPTURE_SIZE];
	WlanPacketSnifferInit(nullptr, reinterpret_cast<uintptr_t>(fake_dram),
			      PACKET_SNIFFER_PARTITION_SIZE);
	WlanPacketSnifferSwitch(true);
	WlanPackerSnifferSetLocation(kDram);
	WlanPacketSnifferSetRunoutAction(kOverwrite);
	SnifferEntryHeader entry_header = { true, static_cast<uint32_t>(kNepRx),
					    DEFAULT_PACKET_CAPTURE_SIZE };

	for (size_t i = 0; i < ((PACKET_SNIFFER_PARTITION_SIZE - PACKET_SNIFFER_BUFFER_OFFSET) /
					DEFAULT_PACKET_CAPTURE_SIZE +
				1);
	     i++) {
		FillFakePacket(DEFAULT_PACKET_CAPTURE_SIZE, fake_packet);
		WLAN_PACKET_SNIFF(reinterpret_cast<uintptr_t>(fake_packet),
				  DEFAULT_PACKET_CAPTURE_SIZE, NULL, NULL, NepRx);
	}

	SnifferEntryHeader *header = reinterpret_cast<SnifferEntryHeader *>(fake_dram);
	EXPECT_TRUE(header->ring_type == entry_header.ring_type);
	EXPECT_TRUE(header->pkt_len == entry_header.pkt_len);
	EXPECT_TRUE(std::strncmp(reinterpret_cast<const char *>(fake_dram) +
					 sizeof(entry_header) * MAX_PACKET_ENTRY_NUM,
				 reinterpret_cast<const char *>(fake_packet),
				 DEFAULT_PACKET_CAPTURE_SIZE) == 0);
}

TEST(WlanDebugPacketSniffer, DiscardSniffing)
{
	uint8_t fake_packet[DEFAULT_PACKET_CAPTURE_SIZE],
		fake_packet_first[DEFAULT_PACKET_CAPTURE_SIZE];
	WlanPacketSnifferInit(nullptr, reinterpret_cast<uintptr_t>(fake_dram),
			      PACKET_SNIFFER_PARTITION_SIZE);
	WlanPacketSnifferSwitch(true);
	WlanPackerSnifferSetLocation(kDram);
	WlanPacketSnifferSetRunoutAction(kDiscard);
	FillFakePacket(DEFAULT_PACKET_CAPTURE_SIZE, fake_packet);
	memcpy(fake_packet_first, fake_packet, DEFAULT_PACKET_CAPTURE_SIZE);
	SnifferEntryHeader entry_header = { true, static_cast<uint32_t>(kNepRx),
					    DEFAULT_PACKET_CAPTURE_SIZE };

	for (size_t i = 0; i < ((PACKET_SNIFFER_PARTITION_SIZE - PACKET_SNIFFER_BUFFER_OFFSET) /
					DEFAULT_PACKET_CAPTURE_SIZE +
				1);
	     i++) {
		WLAN_PACKET_SNIFF(reinterpret_cast<uintptr_t>(fake_packet),
				  DEFAULT_PACKET_CAPTURE_SIZE, NULL, NULL, NepRx);
		FillFakePacket(DEFAULT_PACKET_CAPTURE_SIZE, fake_packet);
	}

	SnifferEntryHeader *header = reinterpret_cast<SnifferEntryHeader *>(fake_dram);
	EXPECT_TRUE(header->ring_type == entry_header.ring_type);
	EXPECT_TRUE(header->pkt_len == entry_header.pkt_len);
	EXPECT_TRUE(std::strncmp(reinterpret_cast<const char *>(fake_dram) +
					 sizeof(entry_header) * MAX_PACKET_ENTRY_NUM,
				 reinterpret_cast<const char *>(fake_packet_first),
				 DEFAULT_PACKET_CAPTURE_SIZE) == 0);
}

TEST(WlanDebugPacketSniffer, SmallPacketSize)
{
	uint8_t fake_packet[DEFAULT_PACKET_CAPTURE_SIZE / 2];
	WlanPacketSnifferInit(nullptr, reinterpret_cast<uintptr_t>(fake_dram),
			      PACKET_SNIFFER_PARTITION_SIZE);
	WlanPacketSnifferSwitch(true);
	WlanPackerSnifferSetLocation(kDram);
	FillFakePacket(DEFAULT_PACKET_CAPTURE_SIZE / 2, fake_packet);
	SnifferEntryHeader entry_header = { true, static_cast<uint32_t>(kNepRx),
					    DEFAULT_PACKET_CAPTURE_SIZE / 2 };

	WLAN_PACKET_SNIFF(reinterpret_cast<uintptr_t>(fake_packet), DEFAULT_PACKET_CAPTURE_SIZE / 2,
			  NULL, NULL, NepRx);

	SnifferEntryHeader *header = reinterpret_cast<SnifferEntryHeader *>(fake_dram);
	EXPECT_TRUE(header->ring_type == entry_header.ring_type);
	EXPECT_TRUE(header->pkt_len == entry_header.pkt_len);
	EXPECT_TRUE(std::strncmp(reinterpret_cast<const char *>(fake_dram) +
					 sizeof(entry_header) * MAX_PACKET_ENTRY_NUM,
				 reinterpret_cast<const char *>(fake_packet),
				 DEFAULT_PACKET_CAPTURE_SIZE / 2) == 0);
}

// Redefine the WLAN_LOG to output to a buffer, so that the result can be
// verified in the unit test.
TEST_F(WlanDebugPacketSnifferTest, LogSniffing)
{
	uint8_t fake_packet[DEFAULT_PACKET_CAPTURE_SIZE];
	WlanPacketSnifferInit(nullptr, reinterpret_cast<uintptr_t>(fake_dram),
			      PACKET_SNIFFER_PARTITION_SIZE);
	WlanPacketSnifferSwitch(true);
	WlanLogInit();
	WlanPackerSnifferSetLocation(kUart);
	FillFakePacket(DEFAULT_PACKET_CAPTURE_SIZE, fake_packet);

	WLAN_PACKET_SNIFF(reinterpret_cast<uintptr_t>(fake_packet), DEFAULT_PACKET_CAPTURE_SIZE,
			  NULL, NULL, NepRx);

	// generate expected hex dump result
	ConstByteSpan pkt_span(reinterpret_cast<const std::byte *>(fake_packet),
			       DEFAULT_PACKET_CAPTURE_SIZE);
	std::array<char, MAX_LOG_ENTRY_SIZE> temp;
	Flags config_flags = {
		.bytes_per_line = 16, .group_every = 1, .show_ascii = false, .show_header = false
	};
	FormattedHexDumper hex_dumper(temp, config_flags);
	hex_dumper.BeginDump(pkt_span);
	while (hex_dumper.DumpLine().ok()) {
		EXPECT_TRUE(strstr(g_test_log_buffer, temp.data()) != 0);
	}
}
} // namespace
} // namespace noa::service::wlan_service