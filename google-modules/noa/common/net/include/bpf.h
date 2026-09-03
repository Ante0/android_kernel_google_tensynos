// NOLINTBEGIN
#pragma once

#include "common/core.h"

#define BPF_F_HDR_FIELD_MASK 0xfULL
#define BPF_F_PSEUDO_HDR (1ULL << 4)
#define BPF_F_MARK_MANGLED_0 (1ULL << 5)

/*
 * Simplified sk_buff for ebpf
 * Referred from Linux kernel: include/uapi/linux/bpf.h
 */
struct nep_sk_buff {
  __u8* data;
  __u8* data_end;
  __u32 len;
  __u32 protocol;
  __u32 ifindex;
};

// NOLINTEND