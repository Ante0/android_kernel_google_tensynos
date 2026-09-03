// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles VPN offload related information.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */

#include "noa_vpn_egress_handler.h"
#include "noa_vpn_ingress_handler.h"
#include "noa_vpn_manager.h"

#include <linux/atomic.h>
#include <linux/slab.h> // For kmalloc/kfree
#include <linux/version.h>
#include <linux/workqueue.h> // For workqueues
#include <net/xfrm.h>

#include <common/map_def.h>
#include <net/nep_cmd_rpc_service/noa_nep_cmd_dispatch.h>
#include <sim/nep/noa_ipsec.h>

#include "vpn/packed_xfrm.h"

#define XFRM_IF_ID_UNSET 0

// Align with IPsec Engine 1.1 Arch Spec Section 3.
#define NOA_VPN_SA_TABLE_SIZE 32
#define NOA_VPN_SP_TABLE_SIZE 32

// TODO(b/437261470): Replace 0 with noa_service_nep_cmd_service_CmdResult_SUCCESS
#define NEP_RPC_CMD_RESULT_SUCCESS 0

struct vpn_manager {
	struct list_head state_list;
	struct list_head policy_list;
	int state_list_nodes;
	int policy_list_nodes;
};

struct sa_entry {
	struct xfrm_state *state;
	u32 seq;
	struct list_head list;
};

struct sp_entry {
	struct xfrm_policy *policy;
	struct list_head list;
};

struct noa_rpc_work_data {
	struct work_struct work;
	int cmd;
	size_t msg_len;
	char msg[];
};

static struct vpn_manager g_vpn_manager;

// A sequence number generator for each SA, used to link the kernel side SA and HW side SA.
static atomic_t g_sa_seq_generator = ATOMIC_INIT(0);

// Use spinlock because the global xfrm_state list and xfrm_policy list might be accessed via
// noa_vpn_manager_handle_ipsec_offload_{tx,rx}_skb() during interrupt context where
// mutex can't be used.
static DEFINE_SPINLOCK(noa_vpn_lock);

static struct sa_entry *to_sa_entry(struct xfrm_state *state)
{
	return (struct sa_entry *)state->xso.offload_handle;
}

static struct sp_entry *to_sp_entry(struct xfrm_policy *policy)
{
	return (struct sp_entry *)policy->xdo.offload_handle;
}

/**
 * @brief Adds the sa_entry to the global xfrm_state list.
 *
 * @return true if the `sa_entry` was successfully added to the list.
 * @return false if the list is full.
 */
static bool sa_list_add_locked(struct sa_entry *sa_entry)
{
	if (g_vpn_manager.state_list_nodes >= NOA_VPN_SA_TABLE_SIZE)
		return false;

	list_add_tail(&sa_entry->list, &g_vpn_manager.state_list);
	g_vpn_manager.state_list_nodes++;
	return true;
}

/**
 * @brief Deletes the sa_entry from the global xfrm_state list.
 *
 * @return true if the `sa_entry` is successfully removed from the list.
 * @return false if the `sa_entry` is not found in the list.
 */
static bool sa_list_delete_locked(struct sa_entry *sa_entry)
{
	if (!list_empty(&sa_entry->list)) {
		list_del_init(&sa_entry->list);
		g_vpn_manager.state_list_nodes--;
		return true;
	}
	return false;
}

/**
 * @brief Adds the sp_entry to the global xfrm_policy list.
 *
 * @return true if the `sp_entry` was successfully added to the list.
 * @return false if the list is full.
 */
static bool sp_list_add_locked(struct sp_entry *sp_entry)
{
	if (g_vpn_manager.policy_list_nodes >= NOA_VPN_SP_TABLE_SIZE)
		return false;

	list_add_tail(&sp_entry->list, &g_vpn_manager.policy_list);
	g_vpn_manager.policy_list_nodes++;
	return true;
}

/**
 * @brief Deletes the sp_entry from the global xfrm_policy list.
 *
 * @return true if the `sp_entry` is successfully removed from the list.
 * @return false if the `sp_entry` is not found in the list.
 */
static bool sp_list_delete_locked(struct sp_entry *sp_entry)
{
	if (!list_empty(&sp_entry->list)) {
		list_del_init(&sp_entry->list);
		g_vpn_manager.policy_list_nodes--;
		return true;
	}
	return false;
}

static inline bool sp_list_delete(struct sp_entry *sp_entry)
{
	bool ret;

	spin_lock(&noa_vpn_lock);
	ret = sp_list_delete_locked(sp_entry);
	spin_unlock(&noa_vpn_lock);
	return ret;
}

static void noa_rpc_work_handler(struct work_struct *work) {
	struct noa_rpc_work_data *work_item = container_of(work, struct noa_rpc_work_data, work);

	if (noa_nep_cmd_request_send(work_item->cmd, work_item->msg, work_item->msg_len, NULL,
				     NULL) != 0)
		pr_err("Async RPC failed for cmd %d\n", work_item->cmd);

	kfree(work_item);
}

/**
 * @brief Asynchronously sends a command to NOA via a workqueue.
 *
 * This function is designed to be called from an atomic context (e.g., inside a
 * spinlock) where sleeping is not allowed. It allocates a work item, copies the
 * command and its payload, and schedules it on the system workqueue. The actual
 * RPC call is performed later by the work handler in a process context.
 *
 */
static int send_cmd_to_noa_by_workqueue(int cmd, void *msg, size_t msg_len)
{
	struct noa_rpc_work_data *work_item;
	size_t work_item_size = sizeof(*work_item) + msg_len;

	work_item = kmalloc(work_item_size, GFP_ATOMIC);
	if (!work_item) {
		pr_err("Failed to allocate memory for noa_rpc_work_data\n");
		return -ENOMEM;
	}

	INIT_WORK(&work_item->work, noa_rpc_work_handler);
	work_item->cmd = cmd;
	work_item->msg_len = msg_len;
	memcpy(work_item->msg, msg, msg_len);

	schedule_work(&work_item->work);
	return 0;
}

static int send_cmd_to_noa(int cmd, struct sa_entry *sa_entry, struct netlink_ext_ack *extack)
{
	int response_code = 0;

	// TODO(b/418683239): Support AddSp and DelSp.
	switch (cmd) {
		case CMD_VPN_ADD_SA: {
			packed_xfrm_state p_xfrm_state = {0};
			int err = noa_vpn_add_state(sa_entry->state,
						    sa_entry->seq);
			if (err)
				return err;

			if (!copy_from_xfrm_state(&p_xfrm_state, sa_entry->state))
				return -EOPNOTSUPP;

			p_xfrm_state.seq = sa_entry->seq;
			if (noa_nep_cmd_request_send(cmd, &p_xfrm_state, sizeof(p_xfrm_state), NULL,
						     &response_code) != 0)
				return -EOPNOTSUPP;

			if (response_code != NEP_RPC_CMD_RESULT_SUCCESS)
				return -EOPNOTSUPP;

			return 0;
		}
		case CMD_VPN_DEL_SA: {
			noa_vpn_del_state(sa_entry->state);
			send_cmd_to_noa_by_workqueue(cmd, &sa_entry->seq, sizeof(sa_entry->seq));
			return 0;
		}
		case CMD_VPN_FREE_SA: {
			noa_vpn_free_state(sa_entry->state);
			send_cmd_to_noa_by_workqueue(cmd, &sa_entry->seq, sizeof(sa_entry->seq));
			return 0;
		}
		default:
			return -EOPNOTSUPP;
	}
}

static bool xfrm_id_match(const struct xfrm_state *sa1, const struct xfrm_state *sa2)
{
	return sa1->props.family == sa2->props.family &&
		   !memcmp(&sa1->id, &sa2->id, sizeof(struct xfrm_id));
}

// Since SPI is the only information for SA lookup when a packet is XLAT'ed, this check is
// used to ensure that every SPI of TX SAs configured to NOA side and every SPI of RX SAs
// configured to NOA side are different.
static bool spi_collides_with_other_tunnels_locked(const struct xfrm_state *state)
{
	struct sa_entry *sa_entry;

	list_for_each_entry(sa_entry, &g_vpn_manager.state_list, list) {
		if (sa_entry->state->id.spi == state->id.spi) {
			// 1. If they have the same xfrm_id, they belong to the same tunnel.
			// 2. If they have different xfrm_id but the same if_id, they belong
			//    to the same tunnel. This usually happens during migration.
			if (!xfrm_id_match(sa_entry->state, state) &&
			    sa_entry->state->if_id != state->if_id &&
			    sa_entry->state->xso.dir == state->xso.dir)
				return true;
		}
	}
	return false;
}

static struct xfrm_state *
lookup_sa_by_spi_and_ports_locked(__be32 spi, __be16 sport, __be16 dport)
{
	struct sa_entry *sa_entry;

	list_for_each_entry(sa_entry, &g_vpn_manager.state_list, list) {
		if (sa_entry->state->id.spi == spi && sa_entry->state->encap &&
		    sa_entry->state->encap->encap_sport == sport &&
		    sa_entry->state->encap->encap_dport == dport) {
			return sa_entry->state;
		}
	}

	return NULL;
}

static inline int add_dev_to_ingress_hook_locked(struct xfrm_state *state)
{
	if (state->xso.dir == XFRM_DEV_OFFLOAD_IN)
		return noa_vpn_ingress_hook_add(xs_net(state), state->xso.dev);
	return 0;
}

static inline void delete_dev_from_ingress_hook_locked(struct xfrm_state *state)
{
	if (state->xso.dir == XFRM_DEV_OFFLOAD_IN)
		noa_vpn_ingress_hook_delete(xs_net(state), state->xso.dev);
}

static inline int add_dev_to_egress_hook_locked(struct xfrm_state *state)
{
	if (state->xso.dir == XFRM_DEV_OFFLOAD_OUT)
		return noa_vpn_egress_hook_add(xs_net(state), state->xso.dev);
	return 0;
}

static inline void delete_dev_from_egress_hook_locked(struct xfrm_state *state)
{
	if (state->xso.dir == XFRM_DEV_OFFLOAD_OUT)
		noa_vpn_egress_hook_delete(xs_net(state), state->xso.dev);
}

static int noa_ipsec_add_state(struct xfrm_state *state, struct netlink_ext_ack *extack)
{
	struct sa_entry *sa_entry = NULL;
	int err = 0;

	spin_lock(&noa_vpn_lock);
	if (spi_collides_with_other_tunnels_locked(state)) {
		spin_unlock(&noa_vpn_lock);

		// Noted that fallback to the non-offload path is supported by crypto offload, not
		// supported by packet offload.
		return -EOPNOTSUPP;
	}

	err = add_dev_to_ingress_hook_locked(state);
	if (err < 0 && err != ENOSPC) {
		spin_unlock(&noa_vpn_lock);
		return err;
	}

	err = add_dev_to_egress_hook_locked(state);
	if (err < 0 && err != ENOSPC) {
		spin_unlock(&noa_vpn_lock);
		return err;
	}

	sa_entry = kzalloc(sizeof(*sa_entry), GFP_KERNEL);
	if (!sa_entry || !sa_list_add_locked(sa_entry)) {
		delete_dev_from_ingress_hook_locked(state);
		delete_dev_from_egress_hook_locked(state);
		spin_unlock(&noa_vpn_lock);
		kfree(sa_entry);

		return -ENOMEM;
	}

	sa_entry->state = state;
	sa_entry->seq = atomic_inc_return(&g_sa_seq_generator);
	state->xso.offload_handle = (unsigned long)sa_entry;
	spin_unlock(&noa_vpn_lock);

	err = send_cmd_to_noa(CMD_VPN_ADD_SA, sa_entry, extack);
	if (err) {
		spin_lock(&noa_vpn_lock);
		delete_dev_from_ingress_hook_locked(state);
		delete_dev_from_egress_hook_locked(state);
		sa_list_delete_locked(sa_entry);
		state->xso.offload_handle = 0;
		spin_unlock(&noa_vpn_lock);
		kfree(sa_entry);

		return err;
	}
	return 0;
}

static void noa_ipsec_del_state(struct xfrm_state *state)
{
	struct sa_entry *sa_entry = to_sa_entry(state);

	if (sa_entry) {
		spin_lock(&noa_vpn_lock);
		delete_dev_from_ingress_hook_locked(state);
		delete_dev_from_egress_hook_locked(state);
		sa_list_delete_locked(sa_entry);
		spin_unlock(&noa_vpn_lock);

		send_cmd_to_noa(CMD_VPN_DEL_SA, sa_entry, NULL);
	}
}

static void noa_ipsec_free_state(struct xfrm_state *state)
{
	struct sa_entry *sa_entry = to_sa_entry(state);

	if (sa_entry) {
		spin_lock(&noa_vpn_lock);
		sa_list_delete_locked(sa_entry);
		spin_unlock(&noa_vpn_lock);

		send_cmd_to_noa(CMD_VPN_FREE_SA, sa_entry, NULL);
		kfree(sa_entry);
	}
}

static bool noa_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *state)
{
	struct xfrm_offload *xo;
	struct sec_path *sp;

	// Current implementation can't handle paged skb. Let it take the fallback path.
	if (skb_shinfo(skb)->nr_frags != 0 && state->xso.type == XFRM_DEV_OFFLOAD_CRYPTO) {
		return false;
	}

	// The crypto offload path cannot properly handle GSO packets where
	// the skb->inner_protocol is unset. Let the GSO packet fall back to
	// software encryption path. The XFRM stack will segment the packet
	// and then process each segment.
	if (skb_is_gso(skb) && state->xso.type == XFRM_DEV_OFFLOAD_CRYPTO) {
		return false;
	}

	if (state->if_id && state->xso.type == XFRM_DEV_OFFLOAD_PACKET) {
		// Packet offload add the SA when xfrm interface is set.
		sp = secpath_set(skb);
		if (!sp)
			return false;

		sp->olen++;
		sp->xvec[sp->len++] = state;

		xo = xfrm_offload(skb);
		xo->status = 0;
		// The XFRM_XMIT flag is set to enable packet to be returned
		// immediately from the validate_xmit_xfrm(), thus alignning
		// with the existing kernel code path for packet offload mode.
		xo->flags |= XFRM_XMIT;
		xfrm_state_hold(state);
	}
	return true;
}

static void noa_ipsec_update_curlft(struct xfrm_state *state)
{
	// To be implemented
}

static int noa_ipsec_add_policy(struct xfrm_policy *policy, struct netlink_ext_ack *extack)
{
	struct sp_entry *sp_entry;
	int err = 0;

	spin_lock(&noa_vpn_lock);
	sp_entry = kzalloc(sizeof(*sp_entry), GFP_KERNEL);
	if (!sp_entry || !sp_list_add_locked(sp_entry)) {
		spin_unlock(&noa_vpn_lock);
		kfree(sp_entry);
		return -ENOMEM;
	}

	sp_entry->policy = policy;
	policy->xdo.offload_handle = (unsigned long)sp_entry;
	spin_unlock(&noa_vpn_lock);

	err = noa_vpn_add_policy(policy);
	if (err) {
		sp_list_delete(sp_entry);
		policy->xdo.offload_handle = 0;
		kfree(sp_entry);
		return err;
	}
	return 0;
}

static void noa_ipsec_del_policy(struct xfrm_policy *policy)
{
	struct sp_entry *sp_entry = to_sp_entry(policy);

	if (sp_entry)
		sp_list_delete(sp_entry);

	noa_vpn_del_policy(policy);
}

static void noa_ipsec_free_policy(struct xfrm_policy *policy)
{
	struct sp_entry *sp_entry = to_sp_entry(policy);

	if (sp_entry) {
		sp_list_delete(sp_entry);
		kfree(sp_entry);
	}
	noa_vpn_free_policy(policy);
}

static const struct xfrmdev_ops noa_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add = noa_ipsec_add_state,
	.xdo_dev_state_delete = noa_ipsec_del_state,
	.xdo_dev_state_free = noa_ipsec_free_state,
	.xdo_dev_offload_ok = noa_ipsec_offload_ok,

	// Not implemented because Android uses Replay Window instead of ESN for Replay Protection.
	.xdo_dev_state_advance_esn = NULL,

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
	.xdo_dev_state_update_curlft = noa_ipsec_update_curlft,
#else
	.xdo_dev_state_update_stats = noa_ipsec_update_curlft,
#endif
	.xdo_dev_policy_add = noa_ipsec_add_policy,
	.xdo_dev_policy_delete = noa_ipsec_del_policy,
	.xdo_dev_policy_free = noa_ipsec_free_policy,
};

__maybe_unused
static u32 find_xfrm_if_id_for_xlat_tx_skb(const struct sk_buff *skb)
{
	struct ipv6hdr *ip6_hdr;
	struct udphdr *uh;
	struct ip_esp_hdr *esp_hdr;
	struct xfrm_state *sa;

	if (skb->protocol != htons(ETH_P_IPV6))
		return XFRM_IF_ID_UNSET;

	ip6_hdr = ipv6_hdr(skb);

	// TODO: Go through all nexthdr field until UDP is found, because the first nexthdr
	// field may not be UDP.
	if (ip6_hdr->nexthdr != NEXTHDR_UDP)
		return XFRM_IF_ID_UNSET;

	uh = (struct udphdr *)(ip6_hdr + 1);
	if (ntohs(uh->len) < sizeof(struct udphdr) + sizeof(struct ip_esp_hdr))
		return XFRM_IF_ID_UNSET;

	esp_hdr = (struct ip_esp_hdr *)(uh + 1);

	spin_lock(&noa_vpn_lock);
	sa = lookup_sa_by_spi_and_ports_locked(esp_hdr->spi, uh->source, uh->dest);
	spin_unlock(&noa_vpn_lock);

	return sa ? sa->if_id : XFRM_IF_ID_UNSET;
}

static inline int clone_security(struct xfrm_state *x, struct xfrm_sec_ctx *security)
{
	struct xfrm_user_sec_ctx *uctx;
	int size = sizeof(*uctx) + security->ctx_len;
	int err;

	uctx = kmalloc(size, GFP_KERNEL);
	if (!uctx)
		return -ENOMEM;

	uctx->exttype = XFRMA_SEC_CTX;
	uctx->len = size;
	uctx->ctx_doi = security->ctx_doi;
	uctx->ctx_alg = security->ctx_alg;
	uctx->ctx_len = security->ctx_len;
	memcpy(uctx + 1, security->ctx_str, security->ctx_len);
	err = security_xfrm_state_alloc(x, uctx);
	kfree(uctx);
	if (err)
		return err;

	return 0;
}

static struct xfrm_state *xfrm_state_clone(struct xfrm_state *orig,
					   struct xfrm_encap_tmpl *encap)
{
	struct net *net = xs_net(orig);
	struct xfrm_state *x = xfrm_state_alloc(net);
	if (!x)
		goto out;

	memcpy(&x->id, &orig->id, sizeof(x->id));
	memcpy(&x->sel, &orig->sel, sizeof(x->sel));
	memcpy(&x->lft, &orig->lft, sizeof(x->lft));
	x->props.mode = orig->props.mode;
	x->props.replay_window = orig->props.replay_window;
	x->props.reqid = orig->props.reqid;
	x->props.family = orig->props.family;
	x->props.saddr = orig->props.saddr;

	if (orig->aalg) {
		x->aalg = xfrm_algo_auth_clone(orig->aalg);
		if (!x->aalg)
			goto error;
	}
	x->props.aalgo = orig->props.aalgo;

	if (orig->aead) {
		x->aead = xfrm_algo_aead_clone(orig->aead);
		x->geniv = orig->geniv;
		if (!x->aead)
			goto error;
	}
	if (orig->ealg) {
		x->ealg = xfrm_algo_clone(orig->ealg);
		if (!x->ealg)
			goto error;
	}
	x->props.ealgo = orig->props.ealgo;

	if (orig->calg) {
		x->calg = xfrm_algo_clone(orig->calg);
		if (!x->calg)
			goto error;
	}
	x->props.calgo = orig->props.calgo;

	if (encap || orig->encap) {
		if (encap)
			x->encap = kmemdup(encap, sizeof(*x->encap),
					GFP_KERNEL);
		else
			x->encap = kmemdup(orig->encap, sizeof(*x->encap),
					GFP_KERNEL);

		if (!x->encap)
			goto error;
	}

	if (orig->security)
		if (clone_security(x, orig->security))
			goto error;

	if (orig->coaddr) {
		x->coaddr = kmemdup(orig->coaddr, sizeof(*x->coaddr),
				    GFP_KERNEL);
		if (!x->coaddr)
			goto error;
	}

	if (orig->replay_esn) {
		if (xfrm_replay_clone(x, orig))
			goto error;
	}

	memcpy(&x->mark, &orig->mark, sizeof(x->mark));
	memcpy(&x->props.smark, &orig->props.smark, sizeof(x->props.smark));

	x->props.flags = orig->props.flags;
	x->props.extra_flags = orig->props.extra_flags;

	x->if_id = orig->if_id;
	x->tfcpad = orig->tfcpad;
	x->replay_maxdiff = orig->replay_maxdiff;
	x->replay_maxage = orig->replay_maxage;
	memcpy(&x->curlft, &orig->curlft, sizeof(x->curlft));
	x->km.state = orig->km.state;
	x->km.seq = orig->km.seq;
	x->replay = orig->replay;
	x->preplay = orig->preplay;
	x->mapping_maxage = orig->mapping_maxage;
	x->lastused = orig->lastused;
	x->new_mapping = 0;
	x->new_mapping_sport = 0;

	return x;

 error:
	xfrm_state_put(x);
out:
	return NULL;
}

static u32 get_xfrm_if_id_tx_skb(const struct sk_buff *skb)
{
	struct sec_path *sp;
	struct xfrm_state *sa;

	sp = skb_sec_path(skb);
	if (sp) {
		sa = sp->xvec[sp->len - 1];
		return likely(sa) ? sa->if_id : XFRM_IF_ID_UNSET;
	}

#if 0 // VPN CLAT
	// The sec_path doesn't exist if the packet is XLAT'ed. Scan the packet headers to
	// find if_id.
	return find_xfrm_if_id_for_xlat_tx_skb(skb);
#else
	return XFRM_IF_ID_UNSET;
#endif
}

struct xfrm_state *lookup_sa_by_ipsec_handle(u32 ipsec_handle)
{
	struct sa_entry *sa_entry;

	spin_lock(&noa_vpn_lock);
	list_for_each_entry(sa_entry, &g_vpn_manager.state_list, list) {
		if (sa_entry->seq == ipsec_handle) {
			spin_unlock(&noa_vpn_lock);
			return sa_entry->state;
		}
	}
	spin_unlock(&noa_vpn_lock);
	return NULL;
}

static int ipsec_metadata_to_xo_status(int status)
{
	switch (status) {
	case IPSEC_METADATA_STATUS_SUCCESS:
		return CRYPTO_SUCCESS;
	case IPSEC_METADATA_STATUS_AUTH_FAILED:
		return CRYPTO_TUNNEL_ESP_AUTH_FAILED;
	default:
		return CRYPTO_GENERIC_ERROR;
	}
}

void noa_vpn_manager_handle_ipsec_offload_rx_skb(struct sk_buff *skb,
						 const struct noa_rx_ipsec_metadata *metadata)
{
	struct xfrm_state *sa;
	struct sec_path *sp;
	struct xfrm_offload *xo;

	if (metadata->status == IPSEC_METADATA_STATUS_SKIP)
		return;

	sa = lookup_sa_by_ipsec_handle(metadata->ipsec_handle);
	if (!sa)
		return;

	xfrm_state_hold(sa);

	sp = secpath_set(skb);
	sp->xvec[sp->len++] = sa;
	sp->olen++;

	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;
	xo->status = ipsec_metadata_to_xo_status(metadata->status);

	skb_shinfo(skb)->android_oem_data1[0] = metadata->ipsec_handle;

	// To optimize the performance, the UDP checksum is not recalculated after the decryption.
	// This tells the network stack to skip UDP checksum validation.
	if (sa->xso.type == XFRM_DEV_OFFLOAD_CRYPTO && sa->encap
		&& sa->encap->encap_type == UDP_ENCAP_ESPINUDP)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}
EXPORT_SYMBOL_GPL(noa_vpn_manager_handle_ipsec_offload_rx_skb);

void noa_vpn_manager_handle_ipsec_offload_tx_skb(const struct sk_buff *skb,
						 struct noa_tx_ipsec_metadata *metadata)
{
	metadata->xfrm_interface_id = get_xfrm_if_id_tx_skb(skb);
}
EXPORT_SYMBOL_GPL(noa_vpn_manager_handle_ipsec_offload_tx_skb);

bool is_ipsec_offload_tx_packet(const struct sk_buff *skb)
{
	return get_xfrm_if_id_tx_skb(skb) != 0;
}
EXPORT_SYMBOL_GPL(is_ipsec_offload_tx_packet);

void noa_vpn_manager_setup_xfrmdev_ops(struct net_device *net)
{
	if (!net->xfrmdev_ops)
		net->xfrmdev_ops = &noa_ipsec_xfrmdev_ops;

	net->features |= NETIF_F_HW_ESP;
	net->hw_enc_features |= NETIF_F_HW_ESP;
}
EXPORT_SYMBOL_GPL(noa_vpn_manager_setup_xfrmdev_ops);

void noa_vpn_manager_init(void)
{
	INIT_LIST_HEAD(&g_vpn_manager.state_list);
	INIT_LIST_HEAD(&g_vpn_manager.policy_list);
	noa_vpn_ingress_handler_init();
	noa_vpn_egress_handler_init();
}

__maybe_unused
void do_switch_migrate(void) {
	struct sa_entry *sa_entry;

	printk(KERN_NOTICE "do_switch_migrate update state");
	list_for_each_entry(sa_entry, &g_vpn_manager.state_list, list) {
		struct xfrm_state* clone_state = NULL;

		spin_lock(&noa_vpn_lock);
		clone_state = xfrm_state_clone(sa_entry->state, NULL);
		spin_unlock(&noa_vpn_lock);
		if (clone_state) {
			struct xfrm_migrate m;
			struct net *net = xs_net(clone_state);

			m.new_family = clone_state->props.family;
			m.old_family = clone_state->props.family;
			m.old_saddr = clone_state->props.saddr;
			m.new_saddr = clone_state->props.saddr;
			m.old_daddr = clone_state->id.daddr;
			m.new_daddr = clone_state->id.daddr;
			m.reqid = clone_state->props.reqid;
			m.mode = clone_state->props.mode;
			m.proto = clone_state->id.proto;

			xfrm_state_migrate(clone_state, &m /* m */,  clone_state->encap /* encap */,
				net /* net*/, NULL /* xuo */, NULL /* extack */);

			xfrm_state_delete(sa_entry->state);
		}
	}
}
