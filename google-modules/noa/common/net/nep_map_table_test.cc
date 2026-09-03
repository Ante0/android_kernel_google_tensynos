#include <errno.h>
#include <string.h>

#include "gtest/gtest.h"
#include "net/nep_map_table.h"

namespace noa::apps::nep::netengine {

#define NEP_MAP_TABLE_TEST_MAX_ENTRY 16
#define NEP_MAP_TABLE_TEST_KEY_SIZE sizeof(uint64_t)
#define NEP_MAP_TABLE_TEST_VALUE_SIZE sizeof(uint64_t)
#define NEP_MAP_TABLE_TEST_BUCKET_NUM 16

class NepMapTableTest : public ::testing::Test {
 protected:
  uint8_t buffer_[NEP_TABLE_TOTAL_MEM_SIZE(
      NEP_MAP_TABLE_TEST_MAX_ENTRY, NEP_MAP_TABLE_TEST_BUCKET_NUM,
      ADJUST_SIZE(NEP_MAP_TABLE_TEST_KEY_SIZE),
      ADJUST_SIZE(NEP_MAP_TABLE_TEST_VALUE_SIZE))] __aligned(4);
};

TEST_F(NepMapTableTest, AddAndRemoveEntry) {
  struct nep_map_table table;
  uint64_t key, value;
  uint64_t* get_value;
  int32_t ret_int;
  bool ret_bool;

  ret_bool = nep_map_table_init(NEP_MAP_TABLE_TEST_MAX_ENTRY,
                                NEP_MAP_TABLE_TEST_KEY_SIZE,
                                NEP_MAP_TABLE_TEST_VALUE_SIZE, &table, buffer_);
  ASSERT_TRUE(ret_bool);

  // Validate the entry is added.
  key = 1234;
  value = 5678;
  ret_int = nep_map_table_add(&table, (const void*)&key, (const void*)&value);
  ASSERT_EQ(ret_int, NEP_MAP_TABLE_ERR_NONE);
  get_value = (uint64_t*)nep_map_table_get_ptr(&table, (const void*)&key);
  ASSERT_EQ(*get_value, value);

  // Validate the entry is remove.
  ret_int = nep_map_table_remove(&table, (const void*)&key);
  ASSERT_EQ(ret_int, NEP_MAP_TABLE_ERR_NONE);
  get_value = (uint64_t*)nep_map_table_get_ptr(&table, (const void*)&key);
  ASSERT_EQ(get_value, nullptr);

  nep_map_table_destroy(&table);
}

TEST_F(NepMapTableTest, GetNextEntry) {
  struct nep_map_table table;
  uint64_t key_base = 1234;
  uint64_t value_base = 5678;
  uint64_t key_offset = 13;
  uint64_t value_offset = 29;
  uint64_t key, value;
  uint32_t check_bits = 0;
  int32_t ret_int;
  bool ret_bool;

  ret_bool = nep_map_table_init(NEP_MAP_TABLE_TEST_MAX_ENTRY,
                                NEP_MAP_TABLE_TEST_KEY_SIZE,
                                NEP_MAP_TABLE_TEST_VALUE_SIZE, &table, buffer_);
  ASSERT_TRUE(ret_bool);

  // Add 10 entries.
  for (uint32_t i = 0; i < 10; ++i) {
    key = key_base * i + key_offset;
    value = value_base * i + value_offset;
    ret_int = nep_map_table_add(&table, (const void*)&key, (const void*)&value);
    ASSERT_EQ(ret_int, NEP_MAP_TABLE_ERR_NONE);
  }

  // Remove the 7th entry.
  key = key_base * 6 + key_offset;
  ret_int = nep_map_table_remove(&table, (const void*)&key);
  ASSERT_EQ(ret_int, NEP_MAP_TABLE_ERR_NONE);

  // Verify that we can retrieve all entries except the 7th.
  key = 0;
  for (uint32_t i = 0; i < 9; ++i) {
    ret_int = nep_map_table_get_next(&table, (void*)&key, (void*)&value);
    ASSERT_EQ(ret_int, NEP_MAP_TABLE_ERR_NONE);
    ASSERT_EQ((key - key_offset) / key_base,
              (value - value_offset) / value_base);
    ASSERT_LT((key - key_offset) / key_base, (uint64_t)10);
    check_bits |= 0x1 << ((key - key_offset) / key_base);
  }
  ASSERT_EQ(check_bits, (uint32_t)0b1110111111);

  // No more entry after the last entry.
  ret_int = nep_map_table_get_next(&table, (void*)&key, (void*)&value);
  ASSERT_EQ(ret_int, NEP_MAP_TABLE_ERR_NO_ENTRY);

  nep_map_table_destroy(&table);
}

}  // namespace noa::apps::nep::netengine
