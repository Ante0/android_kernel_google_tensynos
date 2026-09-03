/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/if.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/pkt_cls.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#else
#define CSUM_MANGLED_0 ((__u16)0xffff)
#endif

#include "bpf.h"
#include "checksum.h"

uint32_t nep_csum16_calculate(void* data, int32_t len) {
  u16* ptr = (u16*)data;
  u32 sum = 0;

  while (len > 1) {
    sum += *ptr++;
    len -= 2;

    // Handle carry-over from the addition
    if (sum & 0x10000) {
      sum = (sum & 0xffff) + (sum >> 16);
    }
  }

  // Handle an odd-sized final byte
  if (len > 0) {
    sum += *(u8*)ptr;
  }

  // Fold the 32-bit sum into 16-bits
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  return sum;
}

static int32_t nep_skb_copy_bytes(struct nep_sk_buff *skb, u32 offset, void* to,
                               int32_t len) {
  void* ptr = (void*)((uintptr_t)skb->data + offset);
  memcpy(to, ptr, len);

  return 0;
}

int32_t nep_skb_store_bytes(struct nep_sk_buff *skb, u32 offset,
                            const void* from, int32_t len) {
  void* ptr = (void*)((uintptr_t)skb->data + offset);
  memcpy(ptr, from, len);

  return 0;
}

bool nep_l3_csum_replace(struct nep_sk_buff *skb, int32_t offset, u64 from,
                         u64 to, u8 field_sz) {
  __u16 sum, *ptr;

  nep_skb_copy_bytes(skb, offset, &sum, sizeof(sum));
  ptr = &sum;
  switch (field_sz) {
    case 2:
      sum = nep_csum16_update(sum, (__u16)from, (__u16)to);
      break;
    case 4:
      sum = nep_csum32_update(sum, (__u32)from, (__u32)to);
      break;
    default:
      return false;
  }

  nep_skb_store_bytes(skb, offset, ptr, sizeof(sum));

  return true;
}

bool nep_l4_csum_replace(struct nep_sk_buff *skb, int32_t offset, u64 from,
                         u64 to, u64 flags) {
  bool is_mmzero = flags & BPF_F_MARK_MANGLED_0;
  __u16 sum, *ptr;

  nep_skb_copy_bytes(skb, offset, &sum, sizeof(sum));
  ptr = &sum;
  if (is_mmzero && !*ptr) {
    return true;
  }

  switch (flags & BPF_F_HDR_FIELD_MASK) {
    case 2:
    case 4:
      sum = nep_csum32_update(sum, (__u32)from, (__u32)to);
      break;
    default:
      return false;
  }

  if (is_mmzero && !*ptr) {
    *ptr = CSUM_MANGLED_0;
  }
  nep_skb_store_bytes(skb, offset, ptr, sizeof(sum));

  return true;
}

#ifdef linux
EXPORT_SYMBOL_GPL(nep_csum16_calculate);
EXPORT_SYMBOL_GPL(nep_skb_store_bytes);
EXPORT_SYMBOL_GPL(nep_l3_csum_replace);
EXPORT_SYMBOL_GPL(nep_l4_csum_replace);
#endif
