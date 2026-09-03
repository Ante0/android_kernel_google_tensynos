#include "net/offload.h"

#include <errno.h>
#include <string.h>

#include <cinttypes>
#include <vector>

#include "common/core.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "gtest/gtest.h"
#include "net/nep_helpers.h"
#include "net/nep_tables.h"
#include "net/netengine_utils.h"
#include "net/v4_testcase.h"
#include "net/v6_testcase.h"

namespace noa::apps::nep::netengine {
class OffloadTest : public ::testing::Test {
 public:
  void SetUp() override { nep_tables_init(); }

  void TearDown() override { nep_tables_destroy(); }

 protected:
  TetherStatsValue stats_v_ = {0, 0, 0, 0, 0, 0};
  uint32_t downstreamIif = 16;
  uint32_t upstreamIif = 24;

  TetherUpstream6Key upstream6_key_ = {
      0,                                     // iif
      {0x42, 0x2C, 0x74, 0x71, 0xE8, 0x0F},  // dstMac
      {0, 0, 0, 0, 0, 0},                    // zero pad for 8 byte alignment
      htonll(0x2401E1808DE83CBD)             // Top 64-bits of the src ip
  };

  Tether6Value upstream6_value_ = {
      0,                                                        // oif
      {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, htons(0x86DD)},  // macHeader
      htons(1500)                                               // pmtu
  };

  TetherDownstream6Key downstream6_key_ = {
      0,                   // The input interface index
      {0, 0, 0, 0, 0, 0},  // dstMac (zeroed iff rawip)
      {0, 0},              // zero pad for 8 byte alignment
      {.__in6_union = {.__s6_addr = {0x24, 0x01, 0xe1, 0x80, 0x8d, 0xe8, 0x3c,
                                     0xbd, 0xa1, 0x02, 0xa4, 0x94, 0x06, 0xa2,
                                     0x8b,
                                     0x88}}}  // The destination IPv6 address
  };

  Tether6Value downstream6_value_ = {
      0,                // oif
      {{0xD6, 0xE9, 0x19, 0xD7, 0x41, 0x34},
       {0x42, 0x2C, 0x74, 0x71, 0xE8, 0x0F},
       htons(0x86DD)},  // macHeader
      htons(1500)       // pmtu
  };

  TetherFlowIdKey flow_id_k_ = {{0xD6, 0xE9, 0x19, 0xD7, 0x41, 0x34},
                                {0, 0},
                                0};

  TetherFlowIdValue flow_id_v_ = 123;

  void SetupIpv6Map() {
    int32_t put_result =
        nep_tables_upstream6_map_add(&upstream6_key_, &upstream6_value_);
    ASSERT_EQ(put_result, 0);
    put_result =
        nep_tables_downstream6_map_add(&downstream6_key_, &downstream6_value_);
    ASSERT_EQ(put_result, 0);
  }

  void SetConfiguration() {
    SetConfiguration(upstream6_key_.dstMac, downstream6_key_.dstMac);
  }

  void SetConfiguration(uint8_t *upstream_if_mac, uint8_t *downstream_if_mac) {
    struct offload_info *offload_info = get_tethering_offload_info();
    offload_info->downstreamIif = downstreamIif;
    offload_info->upstreamIif = upstreamIif;
    memcpy(offload_info->downstream_if_Mac, downstream_if_mac, ETH_ALEN);
    memcpy(offload_info->upstream_if_Mac, upstream_if_mac, ETH_ALEN);
    offload_info->pmtu = htons(1500);
    offload_info->limit_bytes = 0xffffffffffffffff;
    offload_info->stats = stats_v_;
  }

  void SetFlowIdTable() {
    int32_t put_result = nep_tables_flowid_map_add(&flow_id_k_, &flow_id_v_);
    ASSERT_EQ(put_result, 0);
  }

  static void getTetherFlowIdKey(TetherFlowIdKey *target,
                         struct ipv4_output_metadata *out_metadata) {
    memset(target, 0, sizeof(TetherFlowIdKey));
    memcpy(target->dstMac, out_metadata->dst_mac_addr, ETH_ALEN);

    const struct iphdr* iphdr = (struct iphdr*)((void*)((long)out_metadata->packet
                                                        + sizeof(struct ethhdr)));
    target->priority = nep_tos2priority(iphdr->tos);
  }
};

void VerifyTetherStats(uint64_t rx_packets,
                       uint64_t rx_bytes, uint64_t rx_errors,
                       uint64_t tx_packets, uint64_t tx_bytes,
                       uint64_t tx_errors) {
  struct offload_info *offload_info = get_tethering_offload_info();
  ASSERT_EQ(offload_info->stats.rxPackets, rx_packets);
  ASSERT_EQ(offload_info->stats.rxBytes, rx_bytes);
  ASSERT_EQ(offload_info->stats.rxErrors, rx_errors);
  ASSERT_EQ(offload_info->stats.txPackets, tx_packets);
  ASSERT_EQ(offload_info->stats.txBytes, tx_bytes);
  ASSERT_EQ(offload_info->stats.txErrors, tx_errors);
}

TEST_F(OffloadTest, TcpV6ControlPacket) {
  int32_t err_base = get_nep_error_counters(BPF_TETHER_ERR_TCPV6_CONTROL_PACKET);
  uint64_t stats;
  // Put the entry
  SetupIpv6Map();
  SetConfiguration();

  /*
   * =====  Ethernet II =========================
   * Dst 42:2c:74:71:e8:0f, Src d6:e9:19:d7:41:34
   * =====  IPv6 ================================
   * TrafficClass 0x00 (DSCP: CS0, ECN: Not-ECT)
   * FlowLabel    0x7b2c5
   * Hop Limit    63
   * Src          2401:e180:8de8:3cbd:a102:a494:6a2:8b88
   * Dst          2406:2000:e4:1504::6000
   * =====  TCP =================================
   * SrcPort      55658
   * DestPort     443
   * Seq          2833204816 (1)
   * Ack          0 (0)
   * Flags        0x002 (SYN)
   * Checksum     0x0303
   */

  void *syn_pkt = v6_input_3.packet;
  uint32_t len = sizeof(v6_input_pkt_3);
  struct network_ext_txd ext_txd;
  // Make sure no ERR_TCPV6_CONTROL_PACKET before testing.
  ASSERT_EQ(get_nep_error_counters(BPF_TETHER_ERR_TCPV6_CONTROL_PACKET), err_base);
  int32_t net_act = do_process_pkt((void **)&syn_pkt, &len, true /* is_ethernet */,
  				false /* downstream*/, (int)NOA_PORT_WLAN_SW, &ext_txd.info);
  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  // ERR_TCPV6_CONTROL_PACKET does not increase.
  ASSERT_EQ(get_nep_error_counters(BPF_TETHER_ERR_TCPV6_CONTROL_PACKET), err_base);
  stats = sizeof(v6_input_pkt_3) - sizeof(struct ethhdr);
  VerifyTetherStats(0, 0, 0, 1, stats, 0);
  /*
   * =====  Ethernet II =========================
   * Dst 42:2c:74:71:e8:0f, Src d6:e9:19:d7:41:34
   * =====  IPv6 ================================
   * TrafficClass 0x00 (DSCP: CS0, ECN: Not-ECT)
   * FlowLabel    0x63f50
   * Hop Limit    63
   * Src          2401:e180:8de8:3cbd:a102:a494:6a2:8b88
   * Dst          2404:6800:4012:1::2003
   * =====  TCP =================================
   * SrcPort      32772
   * DestPort     443
   * Seq          537211539 (592)
   * Ack          4263016087 (4493)
   * Flags        0x011 (FIN, ACK)
   * Checksum     0x500c
   */
  uint8_t fin_pkt[] = {
      0x42, 0x2c, 0x74, 0x71, 0xe8, 0x0f, 0xd6, 0xe9, 0x19, 0xd7, 0x41,
      0x34, 0x86, 0xdd, 0x60, 0x06, 0x3f, 0x50, 0x00, 0x20, 0x06, 0x3f,
      0x24, 0x01, 0xe1, 0x80, 0x8d, 0xe8, 0x3c, 0xbd, 0xa1, 0x02, 0xa4,
      0x94, 0x06, 0xa2, 0x8b, 0x88, 0x24, 0x04, 0x68, 0x00, 0x40, 0x12,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x03, 0x80,
      0x04, 0x01, 0xbb, 0x20, 0x05, 0x32, 0x93, 0xfe, 0x18, 0x76, 0x97,
      0x80, 0x11, 0x01, 0x24, 0x50, 0x0c, 0x00, 0x00, 0x01, 0x01, 0x08,
      0x0a, 0xd2, 0x90, 0xd4, 0x91, 0xe8, 0x40, 0xb9, 0x1c};
  void* fin_pkt_ptr = fin_pkt;
  len = sizeof(fin_pkt);
  net_act = do_process_pkt((void **)&fin_pkt_ptr, &len, true /* is_ethernet */,
                        false /* downstream*/, (int)NOA_PORT_WLAN_SW, &ext_txd.info);
  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  // ERR_TCPV6_CONTROL_PACKET does not increase.
  ASSERT_EQ(get_nep_error_counters(BPF_TETHER_ERR_TCPV6_CONTROL_PACKET), err_base);
  stats += sizeof(fin_pkt) - sizeof(struct ethhdr);
  VerifyTetherStats(0, 0, 0, 2, stats, 0);

  /*
   * =====  Ethernet II =========================
   * Dst 42:2c:74:71:e8:0f, Src d6:e9:19:d7:41:34
   * =====  IPv6 ================================
   * TrafficClass 0x00 (DSCP: CS0, ECN: Not-ECT)
   * FlowLabel    0xac523
   * Hop Limit    63
   * Src          2401:e180:8de8:3cbd:a102:a494:6a2:8b88
   * Dst          2406:2000:e4:1504::6000
   * =====  TCP =================================
   * SrcPort      55660
   * DestPort     443
   * Seq          1835295119 (821)
   * Ack          0 (0)
   * Flags        0x004 (RST)
   * Checksum     0xa3ed
   */
  uint8_t rst_pkt[] = {
      0x42, 0x2c, 0x74, 0x71, 0xe8, 0x0f, 0xd6, 0xe9, 0x19, 0xd7, 0x41,
      0x34, 0x86, 0xdd, 0x60, 0x0a, 0xc5, 0x23, 0x00, 0x14, 0x06, 0x3f,
      0x24, 0x01, 0xe1, 0x80, 0x8d, 0xe8, 0x3c, 0xbd, 0xa1, 0x02, 0xa4,
      0x94, 0x06, 0xa2, 0x8b, 0x88, 0x24, 0x06, 0x20, 0x00, 0x00, 0xe4,
      0x15, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0xd9,
      0x6c, 0x01, 0xbb, 0x6d, 0x64, 0x61, 0x8f, 0x00, 0x00, 0x00, 0x00,
      0x50, 0x04, 0x00, 0x00, 0xa3, 0xed, 0x00, 0x00};
  void* rst_pkt_ptr = rst_pkt;
  len = sizeof(rst_pkt);
  net_act = do_process_pkt((void **)&rst_pkt_ptr, &len, true /* is_ethernet */,
                        false /* downstream*/, (int)NOA_PORT_WLAN_SW, &ext_txd.info);
  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  // ERR_TCPV6_CONTROL_PACKET does not increase.
  ASSERT_EQ(get_nep_error_counters(BPF_TETHER_ERR_TCPV6_CONTROL_PACKET), err_base);
  stats += sizeof(rst_pkt) - sizeof(struct ethhdr);
  VerifyTetherStats(0, 0, 0, 3, stats, 0);
}

TEST_F(OffloadTest, TcpV6Upstream) {
  // Put the entry
  SetupIpv6Map();
  SetConfiguration();

  /*
   * =====  Ethernet II =========================
   * Dst 42:2c:74:71:e8:0f, Src d6:e9:19:d7:41:34
   * =====  IPv6 ================================
   * TrafficClass 0x00 (DSCP: CS0, ECN: Not-ECT)
   * FlowLabel    0x7b2c5
   * Hop Limit    63
   * Src          2401:e180:8de8:3cbd:a102:a494:6a2:8b88
   * Dst          2406:2000:e4:1504::6000
   * =====  TCP =================================
   * SrcPort      55658
   * DestPort     443
   * Seq          3145 (2833207961)
   * Ack          4787 (1499255159)
   * Flags        0x018 (PSH, ACK)
   * Checksum     0xe897
   */
  uint8_t test_pkt[] = {
      0x42, 0x2c, 0x74, 0x71, 0xe8, 0x0f, 0xd6, 0xe9, 0x19, 0xd7, 0x41, 0x34,
      0x86, 0xdd, 0x60, 0x07, 0xb2, 0xc5, 0x00, 0x3f, 0x06, 0x3f, 0x24, 0x01,
      0xe1, 0x80, 0x8d, 0xe8, 0x3c, 0xbd, 0xa1, 0x02, 0xa4, 0x94, 0x06, 0xa2,
      0x8b, 0x88, 0x24, 0x06, 0x20, 0x00, 0x00, 0xe4, 0x15, 0x04, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0xd9, 0x6a, 0x01, 0xbb, 0xa8, 0xdf,
      0x52, 0x99, 0x59, 0x5c, 0xd1, 0x77, 0x80, 0x18, 0x01, 0x33, 0xe8, 0x97,
      0x00, 0x00, 0x01, 0x01, 0x08, 0x0a, 0x6f, 0xda, 0xab, 0x06, 0xc6, 0x5a,
      0x3c, 0x27, 0x17, 0x03, 0x03, 0x00, 0x1a, 0xbe, 0x9e, 0xc4, 0xce, 0xd8,
      0xa9, 0x44, 0x93, 0xa2, 0x68, 0x59, 0x25, 0xbf, 0x9b, 0xb5, 0x91, 0xe4,
      0x33, 0xd9, 0x4a, 0x95, 0x93, 0x3f, 0x97, 0x76, 0xc8};
  uint32_t len = sizeof(test_pkt);
  struct network_ext_txd nw_txd;

  void* test_pkt_ptr = test_pkt;
  int32_t net_act = do_process_pkt((void **)&test_pkt_ptr, &len, true /* is_ethernet */,
  				false /* downstream*/, (int)NOA_PORT_WLAN_SW, &nw_txd.info);

  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  ASSERT_EQ(nw_txd.info.oif, upstreamIif);

  uint64_t l3_bytes = sizeof(test_pkt) - sizeof(struct ethhdr);
  VerifyTetherStats(0, 0, 0, 1, l3_bytes, 0);
}

TEST_F(OffloadTest, TcpV6Downstream) {
  // Put the entry
  SetupIpv6Map();
  SetConfiguration();
  SetFlowIdTable();

  /*
   * =====  IPv6 ================================
   * TrafficClass 0x00 (DSCP: CS0, ECN: Not-ECT)
   * FlowLabel    0x31198
   * Hop Limit    44
   * Src          2406:2000:e4:1504::6000
   * Dst          2401:e180:8de8:3cbd:a102:a494:6a2:8b88
   * =====  TCP =================================
   * SrctPort     443
   * DstPort      55658
   * Seq          4787 (1499255159)
   * Ack          1009 (2833205825)
   * Flags        0x010 (ACK)
   * Checksum     0xd5f7
   */
  uint8_t test_pkt[] = {
      // L3 IPv6 Header (40 bytes)
      0x60, 0x03, 0x11, 0x98, 0x00, 0x2c, 0x06, 0x2c, 0x24, 0x06, 0x20,
      0x00, 0x00, 0xe4, 0x15, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x60, 0x00, 0x24, 0x01, 0xe1, 0x80, 0x8d, 0xe8, 0x3c, 0xbd, 0xa1,
      0x02, 0xa4, 0x94, 0x06, 0xa2, 0x8b, 0x88,
      // L4 TCP Header (44 bytes)
      0x01, 0xbb, 0xd9, 0x6a,
      0x59, 0x5c, 0xd1, 0x77, 0xa8, 0xdf, 0x4a, 0x41, 0xb0, 0x10, 0x00,
      0xfa, 0xd5, 0xf7, 0x00, 0x00, 0x01, 0x01, 0x08, 0x0a, 0xc6, 0x5a,
      0x3c, 0x31, 0x6f, 0xda, 0xaa, 0xbd, 0x01, 0x01, 0x05, 0x0a, 0xa8,
      0xdf, 0x4f, 0xd5, 0xa8, 0xdf, 0x50, 0x09
  };
  // L3 length is 40 + 44 = 84 bytes.

  // This is the expected packet *with* L2 (Eth + LLC) headers prepended
  uint8_t expected_pkt[] = {
      // L2 Eth (14 bytes)
      0xd6, 0xe9, 0x19, 0xd7, 0x41, 0x34, // Dst MAC
      0x42, 0x2c, 0x74, 0x71, 0xe8, 0x0f, // Src MAC
      0x00, 0x5c, // h_proto = htons(84 (L3) + 8 (LLC)) = htons(92) = 0x005c
      // LLC/SNAP (8 bytes)
      0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x86, 0xdd, // Original EtherType (IPv6)
      // L3 IPv6 Header (40 bytes) - Hop Limit decremented to 0x2b (43)
      0x60, 0x03, 0x11, 0x98, 0x00, 0x2c, 0x06, 0x2b,
      0x24, 0x06, 0x20, 0x00, 0x00, 0xe4, 0x15, 0x04, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x60, 0x00, 0x24, 0x01, 0xe1, 0x80, 0x8d, 0xe8,
      0x3c, 0xbd, 0xa1, 0x02, 0xa4, 0x94, 0x06, 0xa2, 0x8b, 0x88,
      // L4 TCP Header (44 bytes)
      0x01, 0xbb, 0xd9, 0x6a, 0x59, 0x5c, 0xd1, 0x77, 0xa8, 0xdf, 0x4a, 0x41,
      0xb0, 0x10, 0x00, 0xfa, 0xd5, 0xf7, 0x00, 0x00, 0x01, 0x01, 0x08,
      0x0a, 0xc6, 0x5a, 0x3c, 0x31, 0x6f, 0xda, 0xaa, 0xbd, 0x01, 0x01,
      0x05, 0x0a, 0xa8, 0xdf, 0x4f, 0xd5, 0xa8, 0xdf, 0x50, 0x09
  };

  // This is the L3-only length
  uint32_t l3_len = sizeof(test_pkt);
  ASSERT_EQ(l3_len, 84U); // 40 (IP) + 44 (TCP)

  // Create a buffer with headroom using std::vector
  std::vector<uint8_t> pkt_with_headroom(MODEM_RESERVE_HEADROOM_SIZE + l3_len);
  // Copy the L3 packet data after the headroom
  memcpy(pkt_with_headroom.data() + MODEM_RESERVE_HEADROOM_SIZE, test_pkt, l3_len);

  // len is the L3 packet length
  uint32_t len = l3_len;
  struct network_ext_txd nw_txd;

  PW_LOG_DEBUG("ethhdr length: %" PRIu32 "", sizeof(struct ethhdr));

  void* test_pkt_ptr = pkt_with_headroom.data() + MODEM_RESERVE_HEADROOM_SIZE;
  int32_t net_act = do_process_pkt((void **)&test_pkt_ptr, &len, false /* is_ethernet */,
  				true /* downstream*/, (int)NOA_PORT_MODEM_SW, &nw_txd.info);

  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  ASSERT_EQ(nw_txd.info.oif, downstreamIif);

  // Check that len was updated to include headroom.
  // The new length should be the size of the full L2+LLC+L3 frame.
  ASSERT_EQ(len, sizeof(expected_pkt));
  ASSERT_EQ(len, l3_len + MODEM_RESERVE_HEADROOM_SIZE);

  // Check that pointer was moved back to start of headroom
  ASSERT_EQ(test_pkt_ptr, pkt_with_headroom.data());

  // The entire buffer should now match the new expected_pkt
  ASSERT_EQ(0, std::memcmp(test_pkt_ptr, expected_pkt, sizeof(expected_pkt)));

  // L3 bytes for stats should just be L3
  uint64_t l3_bytes = l3_len;
  VerifyTetherStats(1, l3_bytes, 0, 0, 0, 0);
}

void SetupIpv4UpstreamMap(Tether4Entry *entry,
                          struct ipv4_input_metadata *in_metadata,
                          struct ipv4_output_metadata *out_metadata) {
  getUpstreamTether4Entry(entry, in_metadata, out_metadata);

  int32_t put_result =
      nep_tables_upstream4_map_add(&entry->key, &entry->value);
  ASSERT_EQ(put_result, 0);
}

void SetupIpv4DownstreamMap(Tether4Entry *entry,
                          struct ipv4_input_metadata *in_metadata,
                          struct ipv4_output_metadata *out_metadata) {
  getDownstreamTether4Entry(entry, in_metadata, out_metadata);

  int32_t put_result =
      nep_tables_downstream4_map_add(&entry->key, &entry->value);
  ASSERT_EQ(put_result, 0);
}

TEST_F(OffloadTest, TestTcpV4Upstreasm) {
  uint8_t test_pkt[1500];
  Tether4Entry entry;
  SetupIpv4UpstreamMap(&entry, &v4_input_1, &v4_output_1);
  SetConfiguration(v4_output_1.src_mac_addr, v4_input_1.dst_mac_addr);
  hexdump("input key:", (uint8_t *) &entry.key, sizeof(Tether4Key));

  uint32_t len = sizeof(v4_input_pkt_1);
  struct network_ext_txd nw_txd;
  memcpy(test_pkt, v4_input_pkt_1, sizeof(v4_input_pkt_1));

  void* test_pkt_ptr = test_pkt;
  int32_t net_act = do_process_pkt((void **)&test_pkt_ptr, &len, true /* is_ethernet */,
  				false /* downstream*/, (int)NOA_PORT_WLAN_SW, &nw_txd.info);

  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  ASSERT_EQ(nw_txd.info.oif, upstreamIif);

  ASSERT_EQ(0, std::memcmp(test_pkt_ptr, &v4_output_pkt_1, sizeof(v4_output_pkt_1)));

  uint64_t l3_bytes = sizeof(v4_output_pkt_1);
  VerifyTetherStats(0, 0, 0, 1, l3_bytes, 0);
}

TEST_F(OffloadTest, TestTcpV4Downstreasm) {
  Tether4Entry entry;
  SetupIpv4DownstreamMap(&entry, &v4_input_2, &v4_output_2);
  SetConfiguration(v4_input_2.dst_mac_addr, v4_output_2.src_mac_addr);
  hexdump("input key:", (uint8_t *) &entry.key, sizeof(Tether4Key));

  TetherFlowIdKey flowId_k;
  getTetherFlowIdKey(&flowId_k, &v4_output_2);
  hexdump("input flowId key:", (uint8_t *) &flowId_k, sizeof(TetherFlowIdKey));
  nep_tables_flowid_map_add(&flowId_k, &flow_id_v_);

  // This is the expected packet *with* L2 (Eth + LLC) headers prepended
  uint8_t v4_output_pkt_2_with_llc[] = {
      // L2 Eth (14 bytes)
      0x4a, 0x30, 0x52, 0x1c, 0xb2, 0x62, 0xde, 0x87, 0xa0, 0x15,
      0x4e, 0xff, 0x00, 0x3c, // h_proto = htons(52 (L3) + 8 (LLC)) = htons(60) = 0x003c
      // LLC/SNAP (8 bytes)
      0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, // Original EtherType (IPv4)
      // L3/L4 (52 bytes) - (matches v4_input_pkt_2, but with NATed IPs/ports and new checksums)
      0x45, 0x00, 0x00, 0x34, 0xde, 0xb8,
      0x40, 0x00, 0x2b, 0x06, 0x5d, 0x69, 0x83, 0x99, 0xce, 0x64,
      0xc0, 0xa8, 0x00, 0xfc, 0x01, 0xbb, 0xa7, 0x82, 0x1a, 0x6f,
      0x14, 0x30, 0x79, 0xe8, 0x65, 0x2c, 0x80, 0x10, 0x00, 0x46,
      0xcb, 0xfa, 0x00, 0x00, 0x01, 0x01, 0x08, 0x0a, 0xa4, 0x76,
      0x7a, 0xc7, 0xb5, 0xf8, 0x0a, 0xb2
  };

  // v4_input_pkt_2 is L3-only.
  uint32_t l3_len = sizeof(v4_input_pkt_2);
  ASSERT_EQ(l3_len, 52U); // 20 (IP) + 32 (TCP)
  struct network_ext_txd nw_txd;

  // Create a buffer with headroom using std::vector
  std::vector<uint8_t> pkt_with_headroom(MODEM_RESERVE_HEADROOM_SIZE + l3_len);
  // Copy L3 data after headroom
  memcpy(pkt_with_headroom.data() + MODEM_RESERVE_HEADROOM_SIZE, v4_input_pkt_2, l3_len);

  // Point ptr to L3 data
  void* test_pkt_ptr = pkt_with_headroom.data() + MODEM_RESERVE_HEADROOM_SIZE;
  uint32_t len = l3_len;
  int32_t net_act = do_process_pkt((void **)&test_pkt_ptr, &len, false /* is_ethernet */,
  				true /* downstream*/, (int)NOA_PORT_MODEM_SW, &nw_txd.info);

  ASSERT_EQ(net_act, NET_ENGINE_ACT_FORWARD);
  ASSERT_EQ(nw_txd.info.oif, downstreamIif);

  // Check that len was updated.
  ASSERT_EQ(len, sizeof(v4_output_pkt_2_with_llc));
  ASSERT_EQ(len, l3_len + MODEM_RESERVE_HEADROOM_SIZE);

  // Check that pointer was moved back to start of headroom
  ASSERT_EQ(test_pkt_ptr, pkt_with_headroom.data());

  // Compare the entire resulting buffer with the new expected packet
  ASSERT_EQ(0, std::memcmp(test_pkt_ptr, &v4_output_pkt_2_with_llc, sizeof(v4_output_pkt_2_with_llc)));

  uint64_t l3_bytes = l3_len;
  VerifyTetherStats(1, l3_bytes, 0, 0, 0, 0);
}
}  // namespace noa::apps::nep::netengine
