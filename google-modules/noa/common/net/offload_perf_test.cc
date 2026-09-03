#define PW_LOG_MODULE_NAME "netengine-core-perf"
#include "net/offload.h"

#include <errno.h>
#include <string.h>

#include <cinttypes>

#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "net/nep_helpers.h"
#include "net/nep_tables.h"
#include "net/nep_tables.h"
#include "arch/memory.h"
#include "common/compiler.h"

#include "net/v4_testcase.h"

#include "pw_log/log.h"
#include "pw_perf_test/perf_test.h"

#define IPV4_ADDRESS(a, b, c, d) \
  {(in_addr_t)((a << 24) | (b << 16) | (c << 8) | d)}
#define IPV6_ADDRESS(a, b, c, d, e, f, g, h)                  \
  {                                                           \
    .__in6_union = {.__s6_addr16 = {a, b, c, d, e, f, g, h} } \
  }

namespace {
constexpr uint8_t kWlanRxPath = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
    kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);
constexpr uint8_t kWlanTxPath = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
    kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);
constexpr uint8_t kModemRxPath = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
    kNoaNetworkFlowDeviceToHost, kNoaModemRingRxData);
constexpr uint8_t kModemTxPath = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
    kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);

TetherStatsValue stats_v_ = {0, 0, 0, 0, 0, 0};

void SetConfiguration(uint8_t *upstream_if_mac, uint8_t *downstream_if_mac) {
    struct offload_info *offload_info = get_tethering_offload_info();
    offload_info->upstream_port_rx = kModemRxPath;
    offload_info->upstream_port_tx = kModemTxPath;
    offload_info->downstream_port_rx = kWlanRxPath;
    offload_info->downstream_port_tx = kWlanTxPath;
    memcpy(offload_info->downstream_if_Mac, downstream_if_mac, ETH_ALEN);
    memcpy(offload_info->upstream_if_Mac, upstream_if_mac, ETH_ALEN);
    offload_info->pmtu = htons(1500);
    offload_info->limit_bytes = 0xffffffffffffffff;
    offload_info->stats = stats_v_;
}

void SetupIpv4UpstreamMap(Tether4Entry *entry,
                          struct ipv4_input_metadata *in_metadata,
                          struct ipv4_output_metadata *out_metadata) {
  getUpstreamTether4Entry(entry, in_metadata, out_metadata);
  nep_tables_upstream4_map_add(&entry->key, &entry->value);
}

void Upstream4PerfTest(pw::perf_test::State& state) {
    // SetUp
    nep_tables_init();
    uint8_t test_pkt[1500];
    Tether4Entry entry;
    SetupIpv4UpstreamMap(&entry, &v4_input_1, &v4_output_1);
    SetConfiguration(v4_output_1.src_mac_addr, v4_input_1.dst_mac_addr);

    int32_t oif = 0;
    struct network_ext_txd nw_txd;

    while (state.KeepRunning()) {
        // get packet
        uint32_t len = sizeof(v4_input_pkt_1);
        InvalidateDCache(v4_input_pkt_1, len);

        memcpy(test_pkt, v4_input_pkt_1, len);
        FlushDCache(test_pkt, len);
        InvalidateDCache(test_pkt, len);

        void* test_pkt_ptr = test_pkt;

        do_process_pkt((void **)&test_pkt_ptr, &len, true /* is_ethernet */,
                false /* downstream*/, kWlanRxPath, &oif, &nw_txd.info);
        FlushDCache(test_pkt, len);
    }

    // TearDown
    nep_tables_destroy();
}

PW_PERF_TEST(Upstream4PerfTest, Upstream4PerfTest);

void SetupIpv4DownstreamMap(Tether4Entry *entry,
                          struct ipv4_input_metadata *in_metadata,
                          struct ipv4_output_metadata *out_metadata) {
  getDownstreamTether4Entry(entry, in_metadata, out_metadata);
  nep_tables_downstream4_map_add(&entry->key, &entry->value);
}

TetherFlowIdKey flow_id_k_ = {{0xD6, 0xE9, 0x19, 0xD7, 0x41, 0x34},
                                {0, 0},
                                0};

TetherFlowIdValue flow_id_v_ = 123;

static void getTetherFlowIdKey(TetherFlowIdKey *target,
                        struct ipv4_output_metadata *out_metadata) {
    memset(target, 0, sizeof(TetherFlowIdKey));
    memcpy(target->dstMac, out_metadata->dst_mac_addr, ETH_ALEN);

    const auto* packet_bytes = static_cast<const uint8_t*>(out_metadata->packet);
    const auto* iphdr = reinterpret_cast<const struct iphdr*>(packet_bytes + sizeof(struct ethhdr));

    target->priority = nep_tos2priority(iphdr->tos);
}

void Downstream4PerfTest(pw::perf_test::State& state) {
    // SetUp
    nep_tables_init();
    uint8_t test_pkt[1500];
    Tether4Entry entry;
    SetupIpv4DownstreamMap(&entry, &v4_input_2, &v4_output_2);
    SetConfiguration(v4_input_2.dst_mac_addr, v4_output_2.src_mac_addr);

    TetherFlowIdKey flowId_k;
    getTetherFlowIdKey(&flowId_k, &v4_output_2);
    nep_tables_flowid_map_add(&flowId_k, &flow_id_v_);

    int32_t oif = 0;
    struct network_ext_txd nw_txd;

    while (state.KeepRunning()) {
        // get packet
        uint32_t len = sizeof(v4_input_pkt_2) - 14;
        InvalidateDCache(v4_input_pkt_2, sizeof(v4_input_pkt_2));

        memcpy(test_pkt, v4_input_pkt_2, sizeof(v4_input_pkt_2));
        FlushDCache(test_pkt, sizeof(test_pkt));
        InvalidateDCache(test_pkt, sizeof(test_pkt));

        void* test_pkt_ptr = test_pkt + 14;

        do_process_pkt((void **)&test_pkt_ptr, &len, false /* is_ethernet */,
                true /* downstream*/, kModemRxPath, &oif, &nw_txd.info);

        FlushDCache(test_pkt_ptr, sizeof(test_pkt_ptr));
    }

    // TearDown
    nep_tables_destroy();
}

PW_PERF_TEST(Downstream4PerfTest, Downstream4PerfTest);

} // namespace