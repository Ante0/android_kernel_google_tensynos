#include <errno.h>
#include <string.h>

#include "gtest/gtest.h"
#include "net/memory_pool.h"

namespace noa::apps::nep::netengine {

class MemoryPoolTest : public ::testing::Test {
 protected:
  uint8_t buffer_[512];
};

TEST_F(MemoryPoolTest, InitializationFailures) {
  struct memory_pool pool;
  int32_t ret;

  // Init success
  ret = memory_pool_init(&pool, sizeof(buffer_), MINIMUM_BLOCK_SIZE, buffer_);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_NONE);
  memory_pool_destroy(&pool);

  // Init fails if memory_pool is null
  ret = memory_pool_init(nullptr, sizeof(buffer_), 16, buffer_);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_INVALID_PARAMETER);

  // Init fails if buffer size is smaller than MINIMUM_BLOCK_SIZE
  ret = memory_pool_init(&pool, MINIMUM_BLOCK_SIZE - 1, 16, buffer_);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_INVALID_PARAMETER);

  // Init fails if block size is smaller than MINIMUM_BLOCK_SIZE
  ret =
      memory_pool_init(&pool, sizeof(buffer_), MINIMUM_BLOCK_SIZE - 1, buffer_);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_INVALID_PARAMETER);

  // Init fails if buffer size is not multiple of the block size
  ret = memory_pool_init(&pool, sizeof(buffer_), 13, buffer_);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_INVALID_PARAMETER);
}

TEST_F(MemoryPoolTest, AllocateAndFreeEntries) {
  struct memory_pool pool;
  uint8_t *mem1, *mem2;
  int32_t ret;

  ret = memory_pool_init(&pool, sizeof(buffer_), MINIMUM_BLOCK_SIZE, buffer_);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_NONE);

  // Validate the number of free block in the pool
  for (uint32_t i = 0; i < sizeof(buffer_) / MINIMUM_BLOCK_SIZE; ++i) {
    ret = memory_pool_allocate(&pool, (void**)&mem1);
    ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_NONE);
    ASSERT_LE(buffer_, mem1);
    ASSERT_LT(mem1, buffer_ + sizeof(buffer_));
  }

  // No free block in the pool
  ret = memory_pool_allocate(&pool, (void**)&mem2);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_NO_RESOURCE);
  // Free a block and it can be allocated again
  memory_pool_free(&pool, mem1);
  ret = memory_pool_allocate(&pool, (void**)&mem2);
  ASSERT_EQ(ret, NEP_MEMORY_POOL_ERR_NONE);
  ASSERT_EQ(mem1, mem2);

  memory_pool_destroy(&pool);
}

}  // namespace noa::apps::nep::netengine
