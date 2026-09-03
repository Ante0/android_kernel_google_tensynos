#include "vpn/packed_xfrm.h"

#ifdef __KERNEL__
#include <linux/build_bug.h>
#else
#include <assert.h>
#include <string.h>
#endif

#ifdef __KERNEL__
static bool copy_xfrm_algo_auth(packed_xfrm_algo_auth* to, const struct xfrm_algo_auth* from) {
  const unsigned int copy_len = xfrm_alg_auth_len(from);
  if (copy_len > sizeof(*to)) {
    printk(KERN_WARNING "xfrm_alg_auth_len is too long: alg_name=%s\n", from->alg_name);
    return false;
  }
  memcpy(to, from, copy_len);
  return true;
}

static bool copy_xfrm_algo(packed_xfrm_algo* to, const struct xfrm_algo* from) {
  const unsigned int copy_len = xfrm_alg_len(from);
  if (copy_len > sizeof(*to)) {
    printk(KERN_WARNING "xfrm_alg_len is too long: alg_name=%s\n", from->alg_name);
    return false;
  }
  memcpy(to, from, copy_len);
  return true;
}

static bool copy_xfrm_algo_aead(packed_xfrm_algo_aead* to, const struct xfrm_algo_aead* from) {
  const unsigned int copy_len = aead_len((struct xfrm_algo_aead*)from);
  if (copy_len > sizeof(*to)) {
    printk(KERN_WARNING "aead_len is too long: alg_name=%s\n", from->alg_name);
    return false;
  }
  memcpy(to, from, copy_len);
  return true;
}

static void copy_xfrm_mark(packed_xfrm_mark* to, const struct xfrm_mark* from) {
  to->v = from->v;
  to->m = from->m;
}

static void copy_xfrm_address_t(packed_xfrm_address_t* to, const xfrm_address_t* from) {
  static_assert(sizeof(*to) == sizeof(*from));
  memcpy(to, from, sizeof(*to));
}

static void copy_xfrm_id(packed_xfrm_id* to, const struct xfrm_id* from) {
  copy_xfrm_address_t(&to->daddr, &from->daddr);
  to->spi = from->spi;
  to->proto = from->proto;
}

static void copy_xfrm_dev_offload(packed_xfrm_dev_offload* to, const struct xfrm_dev_offload* from) {
  to->offload_handle = from->offload_handle;
  to->dir = from->dir;
  to->type = from->type;
  to->flags = from->flags;
}

static void copy_xfrm_mode(packed_xfrm_mode* to, const struct xfrm_mode* from) {
  to->encap = from->encap;
  to->family = from->family;
  to->flags = from->flags;
}

static void copy_xfrm_encap_tmpl(packed_xfrm_encap_tmpl* to, const struct xfrm_encap_tmpl* from) {
  to->encap_type = from->encap_type;
  to->encap_sport = from->encap_sport;
  to->encap_dport = from->encap_dport;
  copy_xfrm_address_t(&to->encap_oa, &from->encap_oa);
}

static void copy_xfrm_selector(packed_xfrm_selector* to, const struct xfrm_selector* from) {
  copy_xfrm_address_t(&to->daddr, &from->daddr);
  copy_xfrm_address_t(&to->saddr, &from->saddr);
  to->dport = from->dport;
  to->dport_mask = from->dport_mask;
  to->sport = from->sport;
  to->sport_mask = from->sport_mask;
  to->family = from->family;
  to->prefixlen_d = from->prefixlen_d;
  to->prefixlen_s = from->prefixlen_s;
  to->proto = from->proto;
  to->ifindex = from->ifindex;
  to->user = from->user;
}

bool copy_from_xfrm_state(packed_xfrm_state* packed_state,
                          const xfrm_state* state) {
  bool copy_failed = false;

  copy_xfrm_id(&packed_state->id, &state->id);
  copy_xfrm_selector(&packed_state->sel, &state->sel);
  copy_xfrm_mark(&packed_state->mark, &state->mark);

  packed_state->if_id = state->if_id;
  packed_state->tfcpad = state->tfcpad;
  packed_state->genid = state->genid;

  packed_state->props.reqid = state->props.reqid;
  packed_state->props.mode = state->props.mode;
  packed_state->props.replay_window = state->props.replay_window;
  packed_state->props.aalgo = state->props.aalgo;
  packed_state->props.ealgo = state->props.ealgo;
  packed_state->props.calgo = state->props.calgo;
  packed_state->props.flags = state->props.flags;
  packed_state->props.family = state->props.family;
  copy_xfrm_address_t(&packed_state->props.saddr, &state->props.saddr);
  packed_state->props.header_len = state->props.header_len;
  packed_state->props.trailer_len = state->props.trailer_len;
  packed_state->props.extra_flags = state->props.extra_flags;
  copy_xfrm_mark(&packed_state->props.smark, &state->props.smark);

  if (state->aalg) {
    packed_state->has_aalg = 1;
    copy_failed |= !copy_xfrm_algo_auth(&packed_state->aalg, state->aalg);
  }
  if (state->ealg) {
    packed_state->has_ealg = 1;
    copy_failed |= !copy_xfrm_algo(&packed_state->ealg, state->ealg);
  }
  if (state->calg) {
    packed_state->has_calg = 1;
    copy_failed |= !copy_xfrm_algo(&packed_state->calg, state->calg);
  }
  if (state->aead) {
    packed_state->has_aead = 1;
    copy_failed |= !copy_xfrm_algo_aead(&packed_state->aead, state->aead);
  }

  if (state->geniv) {
    snprintf(packed_state->geniv, sizeof(packed_state->geniv), "%s", state->geniv);
  }

  packed_state->new_mapping_sport = state->new_mapping_sport;
  packed_state->new_mapping = state->new_mapping;
  packed_state->mapping_maxage = state->mapping_maxage;

  if (state->encap) {
    packed_state->has_encap = 1;
    copy_xfrm_encap_tmpl(&packed_state->encap, state->encap);
  }
  if (state->coaddr) {
    packed_state->has_coaddr = 1;
    copy_xfrm_address_t(&packed_state->coaddr, state->coaddr);
  }

  copy_xfrm_dev_offload(&packed_state->xso, &state->xso);
  copy_xfrm_mode(&packed_state->inner_mode, &state->inner_mode);
  copy_xfrm_mode(&packed_state->inner_mode_iaf, &state->inner_mode_iaf);
  copy_xfrm_mode(&packed_state->outer_mode, &state->outer_mode);

  // TODO(b/418683239): Comment out this code after the dev branch switches to 6.12.
  //packed_state->dir = state->dir;

  if (copy_failed) {
    memset(packed_state, 0, sizeof(*packed_state));
    return false;
  }
  return true;
}

void copy_from_xfrm_policy(packed_xfrm_policy* packed_policy,
                           const xfrm_policy* policy) {
  printk(KERN_WARNING "copy_from_xfrm_policy is not implemented\n");
}

#else /* __KERNEL__ */
bool copy_from_xfrm_state(packed_xfrm_state* packed_state,
                          const xfrm_state* state) {
  static_assert(
      sizeof(xfrm_state) == sizeof(packed_xfrm_state),
      "In non-kernel simulations, the underlying type of xfrm_state should be "
      "packed_xfrm_state.");
  memcpy(packed_state, state, sizeof(*packed_state));
  return true;
}

void copy_from_xfrm_policy(packed_xfrm_policy* packed_policy,
                           const xfrm_policy* policy) {
  static_assert(
      sizeof(xfrm_policy) == sizeof(packed_xfrm_policy),
      "In non-kernel simulations, the underlying type of xfrm_policy should be "
      "packed_xfrm_policy.");
  memcpy(packed_policy, policy, sizeof(*packed_policy));
}
#endif
