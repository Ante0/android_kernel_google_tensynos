#include <errno.h>
#include <string.h>

#include "gtest/gtest.h"
#include "net/nep_tables.h"

namespace noa::apps::nep::netengine {

class NepTableTest : public ::testing::Test {
 public:
  void SetUp() override { nep_tables_init(); }

  void TearDown() override { nep_tables_destroy(); }
};

#define IPV4_ADDRESS(a, b, c, d) \
  {(in_addr_t)((a << 24) | (b << 16) | (c << 8) | d)}
#define IPV6_ADDRESS(a, b, c, d, e, f, g, h)                  \
  {                                                           \
    .__in6_union = {.__s6_addr16 = {a, b, c, d, e, f, g, h} } \
  }

void AssertTether4ValueEquals(Tether4Value& left, Tether4Value& right) {
#ifdef FOR_P27
  ASSERT_EQ(left.oif, right.oif);
  ASSERT_EQ(0, std::memcmp(&left.macHeader, &right.macHeader, sizeof(ethhdr)));
  ASSERT_EQ(left.pmtu, right.pmtu);
  ASSERT_EQ(0, std::memcmp(&left.src46, &right.src46, sizeof(in6_addr)));
  ASSERT_EQ(0, std::memcmp(&left.dst46, &right.dst46, sizeof(in6_addr)));
  ASSERT_EQ(left.dstPort, right.dstPort);
#else
  ASSERT_EQ(0, std::memcmp(&left.dstMac, &right.dstMac, ETH_ALEN));
  ASSERT_EQ(0, std::memcmp(&left.mangle46, &right.mangle46, sizeof(in6_addr)));
  ASSERT_EQ(left.manglePort, right.manglePort);
#endif
  ASSERT_EQ(left.last_used, right.last_used);
}

TEST_F(NepTableTest, AddAndRemoveSingleUpstream4Entry) {
  Tether4Key test_key = {
#ifdef FOR_P27
      12345,                                 // iif
      {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB},  // dstMac
      4,                                     // proto (IPPROTO_UDP)
#else
      4,                                     // proto (IPPROTO_UDP)
      {0, 0},                                // zero pad
#endif
      IPV4_ADDRESS(192, 168, 1, 10),         // src4 (in_addr)
      IPV4_ADDRESS(10, 100, 5, 23),          // dst4 (in_addr)
      8080,                                  // srcPort
      53,                                    // dstPort
  };

  Tether4Value test_value = {
#ifdef FOR_P27
      67890,  // oif
      {{0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45},
       {0, 0, 0, 0, 0, 0},
       0x0800},                                       // macHeader
      1500,                                           // pmtu
      IPV6_ADDRESS(0x2001, 0xdb8, 0, 0, 0, 0, 2, 1),  // src46 (in6_addr)
      IPV6_ADDRESS(0x2001, 0xdb8, 0, 0, 0, 0, 2, 1),  // dst46 (in6_addr)
      1234,                                           // srcPort
      4321,                                           // dstPort
#else
      {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45},           // dstMac
      {0, 0},                                         // zero pad
      IPV6_ADDRESS(0x2001, 0xdb8, 0, 0, 0, 0, 2, 1),  // mangle46 (in6_addr)
      1234,                                           // manglePort
      {0, 0, 0, 0, 0, 0},                             // zero pad
#endif
      1234567890ULL                                   // last_used
  };

  // Put the entry
  int32_t put_result = nep_tables_upstream4_map_add(&test_key, &test_value);
  ASSERT_EQ(put_result, NEP_MAP_TABLE_ERR_NONE);

  // Get the entry
  Tether4Value retrieved_value;
  int v = nep_tables_upstream4_map_lookup(&test_key, &retrieved_value, 1234567890ULL);

  // Check value available
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NONE, v);

  // Compare values
  AssertTether4ValueEquals(test_value, retrieved_value);

  // Delete the entry
  int32_t delete_result = nep_tables_upstream4_map_remove(&test_key);
  ASSERT_EQ(delete_result, NEP_MAP_TABLE_ERR_NONE);

  // Check if the entry is removed
  v = nep_tables_upstream4_map_lookup(&test_key, &retrieved_value, 1234567890ULL);
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NO_ENTRY, v);
}

void AssertTether6ValueEquals(Tether6Value& left, Tether6Value& right) {
  ASSERT_EQ(left.oif, right.oif);
  ASSERT_EQ(0, std::memcmp(&left.macHeader, &right.macHeader, sizeof(ethhdr)));
  ASSERT_EQ(left.pmtu, right.pmtu);
}

TEST_F(NepTableTest, AddAndRemoveSingleUpstream6Entry) {
  TetherUpstream6Key test_key = {
      12345,                                 // iif
      {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB},  // dstMac
      {0, 0, 0, 0, 0, 0},                    // zero pad for 8 byte alignment
      0x20010db812340000                     // Top 64-bits of the src ip
  };

  Tether6Value test_value = {
      67890,  // oif
      {{0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45},
       {0, 0, 0, 0, 0, 0},
       0x0800},  // macHeader
      1500       // pmtu
  };

  // Put the entry
  int32_t put_result = nep_tables_upstream6_map_add(&test_key, &test_value);
  ASSERT_EQ(put_result, NEP_MAP_TABLE_ERR_NONE);

  // Get the entry
  Tether6Value retrieved_value;
  int v = nep_tables_upstream6_map_lookup(&test_key, &retrieved_value);

  // Check value available
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NONE, v);

  // Compare values
  AssertTether6ValueEquals(test_value, retrieved_value);

  // Delete the entry
  int32_t delete_result = nep_tables_upstream6_map_remove(&test_key);
  ASSERT_EQ(delete_result, NEP_MAP_TABLE_ERR_NONE);

  // Check if the entry is removed
  v = nep_tables_upstream6_map_lookup(&test_key, &retrieved_value);
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NO_ENTRY, v);
}

TEST_F(NepTableTest, AddAndRemoveSingleDownstream6Entry) {
  TetherDownstream6Key test_key = {
      12345,                                 // iif
      {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB},  // dstMac
      {0, 0},                                // zero pad
      IPV6_ADDRESS(0x2401, 0xe180, 4, 1, 2, 3, 0xc98e, 0x7c03) // neigh6
  };

  Tether6Value test_value = {
      67890,  // oif
      {{0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45},
       {0, 0, 0, 0, 0, 0},
       0x0800},  // macHeader
      1500       // pmtu
  };

  // Put the entry
  int32_t put_result = nep_tables_downstream6_map_add(&test_key, &test_value);
  ASSERT_EQ(put_result, NEP_MAP_TABLE_ERR_NONE);

  // Get the entry
  Tether6Value retrieved_value;
  int v = nep_tables_downstream6_map_lookup(&test_key, &retrieved_value);

  // Check value available
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NONE, v);

  // Compare values
  AssertTether6ValueEquals(test_value, retrieved_value);

  // Delete the entry
  int32_t delete_result = nep_tables_downstream6_map_remove(&test_key);
  ASSERT_EQ(delete_result, NEP_MAP_TABLE_ERR_NONE);

  // Check if the entry is removed
  v = nep_tables_downstream6_map_lookup(&test_key, &retrieved_value);
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NO_ENTRY, v);
}

TEST_F(NepTableTest, AddAndRemoveSingleFlowIdEntry) {
  TetherFlowIdKey test_key = {
      {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB},  // dstMac
      {0, 0},                                // zero pad for 8 byte alignment
      1                                      //Priority
  };

  TetherFlowIdValue test_value = 0x11223344;

  // Put the entry
  int32_t put_result = nep_tables_flowid_map_add(&test_key, &test_value);
  ASSERT_EQ(put_result, NEP_MAP_TABLE_ERR_NONE);

  // Get the entry
  TetherFlowIdValue retrieved_value;
  int v = nep_tables_flowid_map_lookup(&test_key, &retrieved_value);

  // Check value available
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NONE, v);

  // Compare values
  ASSERT_EQ(retrieved_value, test_value);

  // Remove the entry
  int32_t delete_result = nep_tables_flowid_map_remove(&test_key);
  ASSERT_EQ(delete_result, NEP_MAP_TABLE_ERR_NONE);

  // Check if the entry is removed
  v = nep_tables_flowid_map_lookup(&test_key, &retrieved_value);
  ASSERT_EQ(NEP_MAP_TABLE_ERR_NO_ENTRY, v);
}

TEST(NetengineTetherOffsetTest, CheckNetengineTetherEntryOffsets) {
  ASSERT_EQ(sizeof(NetengineTether4Entry), 64U);
  ASSERT_EQ(offsetof(NetengineTether4Entry, header), 0U);
  ASSERT_EQ(offsetof(NetengineTether4Entry, key), 4U);
  ASSERT_EQ(offsetof(NetengineTether4Entry, key.source_ip_address), 7U);
  ASSERT_EQ(offsetof(NetengineTether4Entry, value), 19U);
  // 17 (source ip offset) + 19 (value offset) = 36
  ASSERT_EQ(offsetof(NetengineTether4Entry, value.source_ip_address), 36U);
}

}  // namespace noa::apps::nep::netengine
