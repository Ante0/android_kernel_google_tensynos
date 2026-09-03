// Disable linting for non-standard identifier names (Linux kernel style)
// NOLINTBEGIN(readability-identifier-naming)
#ifndef __VPN_PACKED_XFRM_H__
#define __VPN_PACKED_XFRM_H__

/// @file vpn/packed_xfrm.h
///
/// This file defines data structures used to exchange offloaded xfrm_state and
/// xfrm_policy data between the Linux kernel and NOA.
///
/// For each relevant xfrm struct in the kernel, a corresponding "packed"
/// struct is defined here with the prefix `packed_`. These packed structs
/// contain a subset of the original data, omitting unnecessary fields for
/// improved efficiency and reduced overhead during data transfer.
///
/// Pointer Handling: Each pointer field in the original struct is replaced
/// with two fields in the packed version:
///
/// * A field to store the actual data if the pointer was not null.
///
/// * A `u8` flag (`has_<original_field_name>`) indicating whether the original
/// pointer was null. `u8` is used to ensure compatibility between C/C++ and
/// maintain a consistent packed size.
///
/// @note Consider packing `has_` boolean flags into a set of bit masks for
/// space optimization.

#ifdef __KERNEL__
#include <linux/build_bug.h>
#include <linux/types.h>
#include <net/xfrm.h>
#else
#include <stdbool.h>
#include <stdint.h>

#include "linux_port/types.h"
#endif

#define XFRM_PORT_MAX_DEPTH 6
// This constant is used as the size of key. The maximum key length is 256 bits
// with AES/GCM+ESP. But the content of the key contains extra 32 bits salt
// value, so the size required for the key should be 32 bytes + 4 bytes.
#define XFRM_PORT_KEY_LEN_BITS 256  // Key length in bits
#define XFRM_PORT_SALT_LEN_BITS 32  // Salt value length in bits
#define XFRM_PORT_MAX_KEY_LEN \
  ((XFRM_PORT_KEY_LEN_BITS + XFRM_PORT_SALT_LEN_BITS) / 8)

#define XFRM_PORT_MAX_NAME_LEN 64
#define XFRM_PORT_MAX_GENIV_LEN 12

enum { kXfrmModeTunnel = 1 };

#ifdef __KERNEL__
static_assert(kXfrmModeTunnel == XFRM_MODE_TUNNEL);
#endif

enum OffloadDirection {
  kXfrmPortXfrmDevOffloadIn = 1,  // XFRM_DEV_OFFLOAD_IN
  kXfrmPortXfrmDevOffloadOut,     // XFRM_DEV_OFFLOAD_OUT
  kXfrmPortXfrmDevOffloadFwd,     // XFRM_DEV_OFFLOAD_FWD
};

#ifdef __KERNEL__
static_assert(kXfrmPortXfrmDevOffloadIn == XFRM_DEV_OFFLOAD_IN);
static_assert(kXfrmPortXfrmDevOffloadOut == XFRM_DEV_OFFLOAD_OUT);
static_assert(kXfrmPortXfrmDevOffloadFwd == XFRM_DEV_OFFLOAD_FWD);
#endif

enum OffloadType {
  kXfrmPortXfrmDevOffloadUnspecified,  // XFRM_DEV_OFFLOAD_UNSPECIFIED
  kXfrmPortXfrmDevOffloadCrypto,       // XFRM_DEV_OFFLOAD_CRYPTO
  kXfrmPortXfrmDevOffloadPacket,       // XFRM_DEV_OFFLOAD_PACKET
};

#ifdef __KERNEL__
static_assert(kXfrmPortXfrmDevOffloadUnspecified == XFRM_DEV_OFFLOAD_UNSPECIFIED);
static_assert(kXfrmPortXfrmDevOffloadCrypto == XFRM_DEV_OFFLOAD_CRYPTO);
static_assert(kXfrmPortXfrmDevOffloadPacket == XFRM_DEV_OFFLOAD_PACKET);
#endif

// TODO: b/342314596 - Remove unnecessary fields from packed xfrm structures

// Algorithm definition corresponding to kernel string
static const uint8_t kXfrmPortAesGcm[] = "rfc4106(gcm(aes))";

// Unused fields in structs are commented out.

typedef union __attribute__((__packed__)) {
  u32 a4;
  u32 a6[4];
  // struct in6_addr	in6;
} packed_xfrm_address_t;

typedef struct __attribute__((__packed__)) {
  u32 v; /* value */
  u32 m; /* mask */
} packed_xfrm_mark;

typedef struct __attribute__((__packed__)) {
  packed_xfrm_address_t daddr;
  u32 spi;
  u8 proto;
} packed_xfrm_id;

typedef struct __attribute__((__packed__)) {
  packed_xfrm_address_t daddr;
  packed_xfrm_address_t saddr;
  u16 dport;
  u16 dport_mask;
  u16 sport;
  u16 sport_mask;
  u16 family;
  u8 prefixlen_d;
  u8 prefixlen_s;
  u8 proto;
  s32 ifindex;
  u32 user;
} packed_xfrm_selector;

typedef struct __attribute__((__packed__)) {
  //struct net_device	*dev;
  //netdevice_tracker	dev_tracker;
  //struct net_device	*real_dev;
  u64 offload_handle;
  u8 dir : 2;
  u8 type : 2;
  u8 flags : 2;
} packed_xfrm_dev_offload;

typedef struct __attribute__((__packed__)) {
  packed_xfrm_id id;
  packed_xfrm_address_t saddr;
  u16 encap_family;
  u32 reqid;
  u8 mode;
  u8 share;
  u8 optional;
  u8 allalgs;
  u32 aalgos;
  u32 ealgos;
  u32 calgos;
} packed_xfrm_tmpl;

typedef struct __attribute__((__packed__)) {
  // possible_net_t xp_net;
  // struct hlist_node bydst;
  // struct hlist_node byidx;

  // struct hlist_head state_cache_list;

  /* This lock only affects elements except for entry. */
  // rwlock_t lock;
  // refcount_t refcnt;
  // u32 pos;
  // struct timer_list timer;

  // atomic_t genid;
  u32 priority;
  u32 index;
  u32 if_id;
  packed_xfrm_mark mark;
  packed_xfrm_selector selector;
  // struct xfrm_lifetime_cfg lft;
  // struct xfrm_lifetime_cur curlft;
  // struct xfrm_policy_walk_entry walk;
  // struct xfrm_policy_queue polq;
  // bool bydst_reinsert;
  u8 type;
  u8 action;
  u8 flags;
  u8 xfrm_nr;
  u16 family;
  // struct xfrm_sec_ctx* security;
  packed_xfrm_tmpl xfrm_vec[XFRM_PORT_MAX_DEPTH];
  // struct rcu_head rcu;

  packed_xfrm_dev_offload xdo;
} packed_xfrm_policy;

typedef struct __attribute__((__packed__)) {
  u8 encap;
  u8 family;
  u8 flags;
} packed_xfrm_mode;

typedef struct __attribute__((__packed__)) {
  u8 alg_name[XFRM_PORT_MAX_NAME_LEN];
  u32 alg_key_len;   /* in bits */
  u32 alg_trunc_len; /* in bits */
  u8 alg_key[XFRM_PORT_MAX_KEY_LEN];
} packed_xfrm_algo_auth;

typedef struct __attribute__((__packed__)) {
  u8 alg_name[XFRM_PORT_MAX_NAME_LEN];
  u32 alg_key_len; /* in bits */
  u8 alg_key[XFRM_PORT_MAX_KEY_LEN];
} packed_xfrm_algo;

typedef struct __attribute__((__packed__)) {
  u8 alg_name[XFRM_PORT_MAX_NAME_LEN];
  u32 alg_key_len; /* in bits */
  u32 alg_icv_len; /* in bits */
  u8 alg_key[XFRM_PORT_MAX_KEY_LEN];
} packed_xfrm_algo_aead;

typedef struct __attribute__((__packed__)) {
  u16 encap_type;
  u16 encap_sport;
  u16 encap_dport;
  packed_xfrm_address_t encap_oa;
} packed_xfrm_encap_tmpl;

typedef struct __attribute__((__packed__)) {
  // possible_net_t xs_net;
  // union {
  //   struct hlist_node gclist;
  //   struct hlist_node bydst;
  // };
  // union {
  //  struct hlist_node dev_gclist;
  //  struct hlist_node bysrc;
  // };
  // struct hlist_node byspi;
  // struct hlist_node byseq;
  // struct hlist_node state_cache;
  // struct hlist_node state_cache_input;

  // refcount_t refcnt;
  // spinlock_t lock;

  // u32 pcpu_num;
  // Identifies the IPsec Security Association (SA) this state represents
  // (destination address, SPI, protocol).
  packed_xfrm_id id;
  // Further selector information (source/destination addresses/ports, protocol)
  // to match packets to this SA.
  packed_xfrm_selector sel;
  // Used to match xfrm policies and states
  packed_xfrm_mark mark;
  // XFRM virtual interface identifier used to in the XFRM states
  u32 if_id;
  // Padding for alignment
  u32 tfcpad;
  // Generation ID to track SA updates
  u32 genid;

  /* Key manager bits */
  // struct xfrm_state_walk	km;

  /* Parameters of this state. */
  struct __attribute__((__packed__)) {
    // Unique ID for this SA proposal
    u32 reqid;
    // Transport or tunnel mode.
    u8 mode;
    // Size of the replay window for protection against replay attacks
    u8 replay_window;
    // Authentication, encryption, and compression algorithms used by this SA.
    u8 aalgo, ealgo, calgo;
    u8 flags;
    // IPv4 or IPv6
    u16 family;
    // Source address of the SA.
    packed_xfrm_address_t saddr;
    // Lengths of additional headers/trailers added by the SA.
    s32 header_len;
    s32 trailer_len;
    u32 extra_flags;
    // The output mark of the SA. It's used to set the mark (and influence the routing) of the
    // packets emitted by those states. On a system where socket marks determine routing, the
    // packets emitted by an IPsec tunnel can be routed based on a mark that is determined by
    // the tunnel, not by the marks of the unencrypted packets.
    packed_xfrm_mark smark;
  } props;

  // struct xfrm_lifetime_cfg lft;

  /* Data for transformer */
  u8 has_aalg;
  packed_xfrm_algo_auth aalg;
  u8 has_ealg;
  packed_xfrm_algo ealg;
  u8 has_calg;
  packed_xfrm_algo calg;
  u8 has_aead;
  packed_xfrm_algo_aead aead;
  u8 geniv[XFRM_PORT_MAX_GENIV_LEN];

  /* mapping change rate limiting */
  u16 new_mapping_sport;
  u32 new_mapping;    /* seconds */
  u32 mapping_maxage; /* seconds for input SA */
  u8 has_encap;

  /* Data for encapsulator */
  packed_xfrm_encap_tmpl encap;
  // struct sock __rcu* encap_sk;

  /* NAT keepalive */
  // u32 nat_keepalive_interval; /* seconds */
  // time64_t nat_keepalive_expiration;

  /* Data for care-of address */
  u8 has_coaddr;
  packed_xfrm_address_t coaddr;

  /* IPComp needs an IPIP tunnel for handling uncompressed packets */
  // struct xfrm_state* tunnel;

  /* If a tunnel, number of users + 1 */
  // atomic_t tunnel_users;

  /* State for replay detection */
  // struct xfrm_replay_state replay;
  // struct xfrm_replay_state_esn* replay_esn;

  /* Replay detection state at the time we sent the last notification */
  // struct xfrm_replay_state preplay;
  // struct xfrm_replay_state_esn* preplay_esn;

  /* replay detection mode */
  // enum xfrm_replay_mode    repl_mode;

  /* internal flag that only holds state for delayed aevent at the moment */
  // u32 xflags;

  /* Replay detection notification settings */
  // u32 replay_maxage;
  // u32 replay_maxdiff;

  /* Replay detection notification timer */
  // struct timer_list rtimer;

  /* Statistics */
  // struct xfrm_stats stats;

  // struct xfrm_lifetime_cur curlft;
  // struct hrtimer mtimer;
  // Offload information
  packed_xfrm_dev_offload xso;

  /* used to fix curlft->add_time when changing date */
  // long saved_tmo;

  /* Last used time */
  // time64_t lastused;

  // struct page_frag xfrag;

  /* Reference to data common to all the instances of this transformer. */
  // const struct xfrm_type* type;
  packed_xfrm_mode inner_mode;
  packed_xfrm_mode inner_mode_iaf;
  packed_xfrm_mode outer_mode;

  //struct xfrm_type_offload* type_offload;

  /* Security context */
  // struct xfrm_sec_ctx *security;

  /* Private data of this transformer, format is opaque,
   * interpreted by xfrm_type methods. */
  // void *data;
  u8 dir;

  /* NOA specific */
  u32 seq;
} packed_xfrm_state;

/// @typedef xfrm_state
/// @typedef xfrm_policy
///
/// @brief type aliases for xfrm_state and xfrm_policy.
///
/// When simulating or testing code outside of the Linux kernel environment,
/// we use "packed" versions of these structures to avoids the need to directly
/// port the complex `xfrm_state` and `xfrm_policy` structures from the kernel,
/// simplifying development and testing.
#ifdef __KERNEL__
typedef struct xfrm_state xfrm_state;
typedef struct xfrm_policy xfrm_policy;
#else
typedef packed_xfrm_state xfrm_state;
typedef packed_xfrm_policy xfrm_policy;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// TODO(b/418683239): Move copy_from_xfrm_state() into __KERNEL__ section
// since the function not needed in non-kernel environment.
/// @brief Copies the content of a xfrm_state to a packed_xfrm_state.
///
/// @param[out] packed_state The destination packed_xfrm_state structure to
/// which the data will be copied.
///
/// @param[in] state The source xfrm_state structure from which the data will
/// be copied.
///
/// @return True if the copy succeeded; false otherwise.
bool copy_from_xfrm_state(packed_xfrm_state* packed_state,
                          const xfrm_state* state);

// TODO(b/418683239): Move copy_from_xfrm_policy() into __KERNEL__ section.
// since the function is not needed in non-kernel environment.
/// @brief Copies the content of a xfrm_policy to a packed_xfrm_policy.
///
/// @param[out] packed_policy The destination packed_xfrm_policy structure to
/// which the data will be copied.
///
/// @param[in] policy The source xfrm_policy structure from which the data will
/// be copied.
void copy_from_xfrm_policy(packed_xfrm_policy* packed_policy,
                           const xfrm_policy* policy);

#ifdef __cplusplus
}  // extern "C"

namespace noa::module::vpn {
using ::kXfrmPortAesGcm;
using ::packed_xfrm_address_t;
using ::packed_xfrm_algo;
using ::packed_xfrm_algo_aead;
using ::packed_xfrm_algo_auth;
using ::packed_xfrm_dev_offload;
using ::packed_xfrm_encap_tmpl;
using ::packed_xfrm_id;
using ::packed_xfrm_mark;
using ::packed_xfrm_mode;
using ::packed_xfrm_policy;
using ::packed_xfrm_selector;
using ::packed_xfrm_state;
using ::packed_xfrm_tmpl;
}  // namespace noa::module::vpn
#endif
#endif  // __VPN_PACKED_XFRM_H__
// NOLINTEND(readability-identifier-naming)
