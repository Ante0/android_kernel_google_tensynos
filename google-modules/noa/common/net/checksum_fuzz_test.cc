#include "checksum.h"

#include "gtest/gtest.h"
#include "pw_fuzzer/fuzztest.h"

namespace noa::apps::nep::netengine {

void TestCsum16Calculate(const std::vector<uint8_t>& data) {
  // Calculate the checksum using nep_csum16_calculate
  uint32_t checksum = nep_csum16_calculate(const_cast<uint8_t*>(data.data()),
                                       static_cast<int32_t>(data.size()));

  // Basic check: Check if the checksum is within the valid range
  ASSERT_LE(checksum, std::numeric_limits<uint16_t>::max());
}

FUZZ_TEST(ChecksumTest, TestCsum16Calculate)
    .WithDomains(fuzztest::Arbitrary<std::vector<uint8_t>>().WithMaxSize(1024));

}  // namespace noa::apps::nep::netengine
