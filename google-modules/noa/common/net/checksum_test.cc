#include <errno.h>
#include <string.h>

#include "checksum.h"
#include "gtest/gtest.h"

namespace noa::apps::nep::netengine {

class ChecksumTest : public ::testing::Test {
 public:
  void SetUp() override {}

  void TearDown() override {}
};

TEST_F(ChecksumTest, Checksum_32bit_add) {
  struct Bit32AddTestVector {
    uint32_t input1;
    uint32_t input2;
    uint32_t result;
  };

  const std::array<Bit32AddTestVector, 5> test_vectors_ = {
      // Test Case 1: Basic addition, no overflow.
      Bit32AddTestVector{ .input1 = 0x12345678u, .input2 = 0x87654321u, .result = 0x99999999u },
      // Test Case 2: Addition with overflow.
      Bit32AddTestVector{ .input1 = 0xFFFFFFFFu, .input2 = 0x00000001u, .result = 0x00000001u },
      // Test Case 3: Addition resulting in all ones.
      Bit32AddTestVector{ .input1 = 0xFFFFFFFEu, .input2 = 0x00000001u, .result = 0xFFFFFFFFu },
      // Test Case 4: Addition of zero (idempotency).
      Bit32AddTestVector{ .input1 = 0x12345678u, .input2 = 0x00000000u, .result = 0x12345678u },
      // Test Case 5: Test with larger values to ensure carry propagation.
      Bit32AddTestVector{ .input1 = 0xAABBCCDDu, .input2 = 0x11223344u, .result = 0xBBDE0021u }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum32_add(test_vector.input1, test_vector.input2), test_vector.result);
  }
}

TEST_F(ChecksumTest, Checksum_32bit_sub) {
  struct Bit32SubTestVector {
    uint32_t input1;
    uint32_t input2;
    uint32_t result;
  };

  const std::array<Bit32SubTestVector, 5> test_vectors_ = {
      // Test Case 1: Basic subtraction, no underflow.
      Bit32SubTestVector{ .input1 = 0x99999999u, .input2 = 0x12345678u, .result = 0x87654321u },
      // Test Case 2: Subtraction with underflow.
      Bit32SubTestVector{ .input1 = 0x00000000u, .input2 = 0x00000001u, .result = 0xFFFFFFFEu },
      // Test Case 3: Subtraction resulting in all ones.
      Bit32SubTestVector{ .input1 = 0x00000001u, .input2 = 0x00000001u, .result = 0xFFFFFFFFu },
      // Test Case 4: Subtraction of zero (idempotency).
      Bit32SubTestVector{ .input1 = 0x12345678u, .input2 = 0x00000000u, .result = 0x12345678u },
      // Test Case 5: Test with larger values to ensure borrow propagation.
      Bit32SubTestVector{ .input1 = 0xBBDE0021u, .input2 = 0x11223344u, .result = 0xAABBCCDDu }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum32_sub(test_vector.input1, test_vector.input2), test_vector.result);
  }
}

TEST_F(ChecksumTest, Checksum_16bit_add) {
  struct Bit16AddTestVector {
    uint16_t input1;
    uint16_t input2;
    uint16_t result;
  };

  const std::array<Bit16AddTestVector, 5> test_vectors_ = {
      // Test Case 1: Basic addition, no overflow.
      Bit16AddTestVector{ .input1 = 0x1234u, .input2 = 0x4321u, .result = 0x5555u },
      // Test Case 2: Addition with overflow (wraparound).
      Bit16AddTestVector{ .input1 = 0xFFFFu, .input2 = 0x0001u, .result = 0x0001u },
      // Test Case 3: Addition resulting in all ones.
      Bit16AddTestVector{ .input1 = 0xFFFEu, .input2 = 0x0001u, .result = 0xFFFFu },
      // Test Case 4: Addition of zero (idempotency).
      Bit16AddTestVector{ .input1 = 0xABCDu, .input2 = 0x0000u, .result = 0xABCDu },
      // Test Case 5: Carry propagation within 16 bits.
      Bit16AddTestVector{ .input1 = 0x89ABu, .input2 = 0x0123u, .result = 0x8ACEu }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum16_add(test_vector.input1, test_vector.input2), test_vector.result);
  }
}

TEST_F(ChecksumTest, Checksum_16bit_sub) {
  struct Bit16SubTestVector {
    uint16_t input1;
    uint16_t input2;
    uint16_t result;
  };

  const std::array<Bit16SubTestVector, 5> test_vectors_ = {
      Bit16SubTestVector{ .input1 = 0x5555u, .input2 = 0x1234u, .result = 0x4321u },
      Bit16SubTestVector{ .input1 = 0x0000u, .input2 = 0x0001u, .result = 0xFFFEu },
      Bit16SubTestVector{ .input1 = 0xAAAAu, .input2 = 0xAAAAu, .result = 0xFFFFu },
      Bit16SubTestVector{ .input1 = 0x1234u, .input2 = 0x0000u, .result = 0x1234u },
      Bit16SubTestVector{ .input1 = 0x8ACEu, .input2 = 0x0123u, .result = 0x89ABu }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum16_sub(test_vector.input1, test_vector.input2), test_vector.result);
  }
}

TEST_F(ChecksumTest, Checksum_32bit_fold) {
  struct Bit32FoldTestVector {
    uint32_t input;
    uint16_t result;
  };

  const std::array<Bit32FoldTestVector, 5> test_vectors_ = {
       // 0x1234 + 0x5678 = 0x68AC, ~0x68AC = 0x9753.
      Bit32FoldTestVector{ .input = 0x12345678u, .result = 0x9753u },
      // 0xABCD + 0xFFFF = 0x1ABCC, 0xABCC + 0x0001 = 0xABCD, ~0xABCD = 0x5432.
      Bit32FoldTestVector{ .input = 0xABCDFFFFu, .result = 0x5432u },
      // 0xFFFF + 0xFFFF = 0x1FFFE, 0xFFFE + 0x0001 = 0xFFFF, ~0xFFFF = 0x0000.
      Bit32FoldTestVector{ .input = 0xFFFFFFFFu, .result = 0x0000u },
      // 0xFFFF + 0xFFFE = 0x1FFFD, 0xFFFD + 0x0001 = 0xFFFE, ~0xFFFE = 0x0001.
      Bit32FoldTestVector{ .input = 0xFFFFFFFEu, .result = 0x0001u },
      // 0x0000 + 0x0000 = 0x0000, ~0x0000 = 0xFFFF.
      Bit32FoldTestVector{ .input = 0x00000000u, .result = 0xFFFFu }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum32_fold(test_vector.input), test_vector.result);
  }
}

TEST_F(ChecksumTest, Checksum_16bit_update) {
  struct Bit16UpdateTestVector {
    uint16_t csum;
    uint16_t old_value;
    uint16_t new_value;
    uint16_t result;
  };

  const std::array<Bit16UpdateTestVector, 5> test_vectors_ = {
      // Test Case 1: Basic update, no carry or borrow.
      // (~0xA123 + ~0xFABC + 0x2345) = 0x8764, ~0x8764 = 0x789B.
      Bit16UpdateTestVector{ .csum = 0xA123u, .old_value = 0xFABCu, \
      .new_value = 0x2345u, .result = 0x789Bu },
      // Test Case 2: Update with carry during addition.
      // (~0xFFFF + ~0x0001 + 0x0002) = 0x10000, 0x0000+0x1 = 0x0001, ~0x0001 = 0xFFFE.
      Bit16UpdateTestVector{ .csum = 0xFFFFu, .old_value = 0x0001u, \
      .new_value = 0x0002u, .result = 0xFFFEu },
      // Test Case 3: update with zero checksum, non-zero values.
      // (~0x0000 + ~0x0002 + 0x0001) = 0x1FFFD -> 0xFFFE, ~0xFFFE = 0x0001.
      Bit16UpdateTestVector{ .csum = 0x0000u, .old_value = 0x0002u, \
      .new_value = 0x0001u, .result = 0x0001u },
      // Test Case 4: Basic update with carry.
      // (~0xAABB + ~0xCCDD + 0xEEFF) = 0x17765 -> 0x7766, ~0x7766 = 0x8899.
      Bit16UpdateTestVector{ .csum = 0xAABBu, .old_value = 0xCCDDu, \
      .new_value = 0xEEFFu, .result = 0x8899u },
      // Test Case 5: No change (old_value == new_value).
      Bit16UpdateTestVector{ .csum = 0x5555u, .old_value = 0x1234u, \
      .new_value = 0x1234u, .result = 0x5555u }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum16_update(test_vector.csum, test_vector.old_value, \
    test_vector.new_value), test_vector.result);
  }
}

TEST_F(ChecksumTest, Checksum_32bit_update) {
  struct Bit32UpdateTestVector {
    uint16_t csum;
    uint32_t old_value;
    uint32_t new_value;
    uint16_t result;
  };

  const std::array<Bit32UpdateTestVector, 5> test_vectors_ = {
      // Test Case 1: Basic Update.
      //~(~0x1234 - (0x5678+0x9ABC) + (0xDEF0+0x1234)) = 0x1244.
      Bit32UpdateTestVector{ .csum = 0x1234u, .old_value = 0x56789ABCu, \
      .new_value = 0xDEF01234u, .result = 0x1244u },
      // Test Case 2: Basic update with multiple carries.
      //~(~0x0001 - (0x0000+0x0000) + (0xFFFF+0xFFFF) = 0x3FFFB -> 0xFFFE, ~0xFFFE=0x1.
      Bit32UpdateTestVector{ .csum = 0x0001u, .old_value = 0x00000000u, \
      .new_value = 0xFFFFFFFFu, .result = 0x0001u },
      // Test Case 3: No Change.
      Bit32UpdateTestVector{ .csum = 0x5555u, .old_value = 0x12345678u, \
      .new_value = 0x12345678u, .result = 0x5555u },
      // Test Case 4: Zero Checksum, non-zero values.
      //~(~0x0000 - (0xAABB+0xCCDD) + (0x1122+0x3344) = 0x3333.
      Bit32UpdateTestVector{ .csum = 0x0000u, .old_value = 0xAABBCCDDu, \
      .new_value = 0x11223344u, .result = 0x3333u },
      // Test Case 5: Large checksum.
      //~(~0xFFFF - (0x7FFF+0xFFFF) + (0x8000+0x0000) = 0xFFFE.
      Bit32UpdateTestVector{ .csum = 0xFFFFu, .old_value = 0x7FFFFFFFu, \
      .new_value = 0x80000000u, .result = 0xFFFEu }};

  for (const auto& test_vector : test_vectors_) {
    ASSERT_EQ(nep_csum32_update(test_vector.csum, test_vector.old_value, \
    test_vector.new_value), test_vector.result);
  }
}

}  // namespace noa::apps::nep::netengine