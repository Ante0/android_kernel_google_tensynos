// NOLINTBEGIN
#pragma once

#ifdef linux
#else
#include "common/core.h"
#include "linux_port/types.h"
#endif

#include "bpf.h"

uint32_t nep_csum16_calculate(void* data, int32_t len);
int32_t nep_skb_store_bytes(struct nep_sk_buff *skb, u32 offset,
                            const void* from, int32_t len);
bool nep_l3_csum_replace(struct nep_sk_buff *skb, int32_t offset, u64 from,
                         u64 to, u8 field_sz);
bool nep_l4_csum_replace(struct nep_sk_buff *skb, int32_t offset, u64 from,
                         u64 to, u64 flags);

// 32-bit checksum addition
static __always_inline __u32 nep_csum32_add(__u32 csum, __u32 value) {
  // One's complement addition
  u32 ret = (u32)csum;
  ret += (u32)value;
  return (__u32)(ret + (ret < (u32)value));
}

// 32-bit checksum subtraction
static __always_inline __u32 nep_csum32_sub(__u32 csum, __u32 value) {
  // Add the bit complement of the original value
  return nep_csum32_add(csum, (u32)~value);
}

// 16-bit checksum addition
static __always_inline __u16 nep_csum16_add(__u16 csum, __u16 value) {
  // One's complement addition
  u16 ret = (u16)csum;
  ret += (u16)value;
  return (__u16)(ret + (ret < (u16)value));
}

// 16-bit checksum subtraction
static __always_inline __u16 nep_csum16_sub(__u16 csum, __u16 value) {
  // Add the bit complement of the original value
  return nep_csum16_add(csum, (u16)~value);
}

static __always_inline __u16 nep_csum32_fold(__u32 csum) {
  csum = (csum & 0xffff) + (csum >> 16);  // Add the lower and upper 16-bits
  csum = (csum & 0xffff) + (csum >> 16);  // Repeat in case a carry occurred

  return (__u16)~csum;
}

// rfc1624: HC' = ~(~HC + ~m + m')
// HC  - old checksum in header
// HC' - new checksum in header
// m   - old value of a 16-bit field
// m'  - new value of a 16-bit field
static __always_inline __u16 nep_csum16_update(__u16 csum, __u16 old_value,
                                           __u16 new_value) {
  return ~nep_csum16_add(nep_csum16_sub(~csum, old_value), new_value);
}

static __always_inline __u16 nep_csum32_update(__u16 csum, __u32 old_value,
                                           __u32 new_value) {
  __u32 csum32 = (__u32)csum;
  csum32 = nep_csum32_add(nep_csum32_sub(~csum32, (__u32)old_value), (__u32)new_value);

  return nep_csum32_fold(csum32);
}

// NOLINTEND
