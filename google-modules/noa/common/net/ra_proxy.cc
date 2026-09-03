// SPDX-License-Identifier: GPL-2.0-only
/*
 * Router Advertisement Proxy implementation
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/inet.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/timer.h>
#include <net/ipv6.h>
#include "checksum.h"
#include "ra_packet.h"
#include "ra_proxy.h"
#include "ra_timers.h"
#else
#include "linux_port/container_of.h"
#include "linux_port/memory-alloc.h"
#include "linux_port/jiffies.h"
#include "linux_port/timer.h"
#include "linux_port/types.h"
#include "net/icmpv6.h"
#include "net/ipv6.h"
#include "net/map_def.h"
#include "net/nep_helpers.h"
#include "net/nep_service.h"
#include "net/ra_packet.h"
#include "net/ra_proxy.h"
#include "net/ra_timers.h"
#include "pw_log/log.h"
#include "pw_chrono/system_clock.h"
#include "checksum.h"
#endif

// RA Proxy Processing:
//
// Receive a new Router Advertisement (RA):
//     - Create RA timers to monitor each lifetime value.
//     - Schedule a monitor timer with the minimal non-zero lifetime value.
//     - Send a modified RA to the kernel with extended lifetime values and schedule a refresh
//       timer.
//
// Receive an existing RA:
//     - Update the corresponding timers for each lifetime value.
//     - Reschedule the monitor timer.
//
// Monitor timer timeout or refresh timer timeout:
//     - Send an RA with the lifetime field for the timed-out lifetime set to zero and all other
//       lifetime fields set to their maximal values.
//     - If an RA's minimal lifetime value is 0, remove it from the RA timers monitor.
//
// Send packet to the kernel:
//     - Move the MTU option to the end of the packet and send the packet to the kernel.

static int is_initialized = 0;
static ras_timers_t ras_timers;

extern struct in6_addr all_hosts_addr;

static uint32_t get_min_lifetime(ra_packet_t *ra_packet) {
	int i;
	uint32_t min_lifetime = 0xffffffff;

	for (i = 0; i < ra_packet->num_sections; ++i) {
		packet_section_t *section = &ra_packet->packet_sections[i];
		if (section->type != SECTION_TYPE_LIFETIME) {
			continue;
		}
		if (section->lifetime > 0 && section->lifetime < min_lifetime) {
			min_lifetime = section->lifetime;
		}
	}

	return min_lifetime;
}

static int ra_proxy_send_to_kernel(const int ifindex, const uint8_t *packet_data, int packet_len)
{
#ifdef linux
	struct sk_buff *skb;
	struct net_device *dev;

	// Get the netdevice
	dev = dev_get_by_index(&init_net, ifindex);
	if (!dev) {
		return -1;
	}

	skb = alloc_skb(packet_len, GFP_KERNEL);
	if (!skb) {
		dev_put(dev);
		return -1;
	}

	// Copy the packet data into the sk_buff
	skb_put_data(skb, packet_data, packet_len);

	// Set up sk_buff metadata
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->pkt_type = PACKET_HOST;
	skb->ip_summed = CHECKSUM_NONE;

	// Push the skb to the network stack
	netif_rx(skb);
	dev_put(dev);
#else
	PW_LOG_DEBUG("ra_proxy: Send RA to %" PRId32 ", data = %p, len = %" PRId32 "",
		     ifindex, packet_data, packet_len);

	if (packet_len > MAX_RA_PACKET_SIZE) {
		return -1;
	}

	NepRpcService* nep_rpc_service = GetNepService();

	if (nep_rpc_service) {
		rpc_send_ra_t rpc_send_ra;
		rpc_send_ra.ifindex = ifindex;
		rpc_send_ra.packet_len = packet_len;
		memcpy(rpc_send_ra.packet_data, packet_data, packet_len);
		nep_rpc_service->send_event_to_apc_from_nep(
				CMD_CALLBACK_SEND_RA_TO_KERNEL,
				(void *)&rpc_send_ra, sizeof(rpc_send_ra_t));
	}
#endif

	return 0;
}

static int ra_proxy_send_unsolicited_ra(const int ifindex, const uint8_t *packet_data,
					     int packet_len) {
	uint8_t *unsolicited_packet;
	struct ipv6hdr *ip6h;
	struct icmp6hdr *icmp6h;
	int i;

	unsolicited_packet = (uint8_t *)kzalloc(packet_len, GFP_KERNEL);
	if (unsolicited_packet == NULL) {
		return -1;
	}
	memcpy(unsolicited_packet, packet_data, packet_len);

	ip6h = (struct ipv6hdr *) (unsolicited_packet + IPV6_HEADER_OFFSET);
	if (memcmp(&ip6h->daddr, &all_hosts_addr, sizeof(struct in6_addr)) != 0) {
		icmp6h = (struct icmp6hdr *) (unsolicited_packet + ICMP6_HEADER_OFFSET);
		for (i = 0; i < 4; ++i) {
			const __u32 old_value = ip6h->daddr.s6_addr32[i];
			const __u32 new_value = all_hosts_addr.s6_addr32[i];
			// Update the ICMPv6 checksum
			icmp6h->icmp6_cksum = nep_csum32_update(
					icmp6h->icmp6_cksum, old_value, new_value);
		}
		memcpy(&ip6h->daddr, &all_hosts_addr, sizeof(struct in6_addr));
		// Note: Multicast packet may have unicast destination MAC in
		// WiFi environment, so we don't need to change the destination
		// MAC to 33:33:00:00:00:01
	}

	return ra_proxy_send_to_kernel(ifindex, unsolicited_packet, packet_len);
}

static inline uint32_t get_boottime_ms(void) {
#ifdef linux
	return ktime_to_ms(ktime_get_boottime());
#else
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			pw::chrono::SystemClock::now().time_since_epoch()).count());
#endif
}

static void ra_proxy_lifetime_expired(const ra_timers_t *ra_timers) {
	int i;
	const ra_packet_t *ra_packet = &ra_timers->ra_packet;
	struct icmp6hdr *icmp6h;
	uint8_t *timeout_packet;
	uint64_t expire_time_ms;
	uint64_t current_time_ms = get_boottime_ms();
	bool is_remove_ra_timers = true;

	timeout_packet = (uint8_t *)kzalloc(ra_packet->packet_length, GFP_KERNEL);
	if (timeout_packet == NULL) {
		return;
	}
	memcpy(timeout_packet, ra_packet->packet_buffer, ra_packet->packet_length);
	icmp6h = (struct icmp6hdr *) (timeout_packet + ICMP6_HEADER_OFFSET);

	for (i = 0; i < ra_packet->num_sections; ++i) {
		const packet_section_t *section = &ra_packet->packet_sections[i];
		if (section->type != SECTION_TYPE_LIFETIME) {
			continue;
		}
		if (i == ra_packet->router_lifetime_idx) {
			expire_time_ms = ra_timers->expire_times_ms[RA_TIMER_TYPE_ROUTER_LIFETIME];
		} else if (i == ra_packet->pio_valid_lifetime_idx) {
			expire_time_ms = ra_timers->expire_times_ms[RA_TIMER_TYPE_PIO_VALID_LIFETIME];
		} else if (i == ra_packet->pio_preferred_lifetime_idx) {
			expire_time_ms = ra_timers->expire_times_ms[RA_TIMER_TYPE_PIO_PREFERRED_LIFETIME];
		} else if (i == ra_packet->rio_route_lifetime_idx) {
			expire_time_ms = ra_timers->expire_times_ms[RA_TIMER_TYPE_RIO_ROUTE_LIFETIME];
		} else if (i == ra_packet->rdnss_lifetime_idx) {
			expire_time_ms = ra_timers->expire_times_ms[RA_TIMER_TYPE_RDNSS_LIFETIME];
		} else {
			continue;
		}
		if (i == ra_packet->router_lifetime_idx) {
			const __u16 old_value = *((const __u16 *)(ra_packet->packet_buffer + section->start));
			__u16 new_value = (current_time_ms >= expire_time_ms)
					? 0
					: htons((expire_time_ms - current_time_ms) / 1000);
			*((__u16 *)(timeout_packet + section->start)) = new_value;
			// Update the ICMPv6 checksum
			icmp6h->icmp6_cksum = nep_csum16_update(
					icmp6h->icmp6_cksum, old_value, new_value);
			if (new_value != 0) {
				is_remove_ra_timers = false;
			}
		} else {
			const __u32 old_value = *((const __u32 *)(ra_packet->packet_buffer + section->start));
			__u32 new_value = (current_time_ms >= expire_time_ms)
					? 0
					: htonl((expire_time_ms - current_time_ms) / 1000);
			*((__u32 *)(timeout_packet + section->start)) = new_value;
			// Update the ICMPv6 checksum
			icmp6h->icmp6_cksum = nep_csum32_update(
					icmp6h->icmp6_cksum, old_value, new_value);
			if (new_value != 0) {
				is_remove_ra_timers = false;
			}
		}
	}

	// Move MTU option to the end
	if (ra_packet->mtu_option_idx >= 0) {
		const packet_section_t *section =
				&ra_packet->packet_sections[ra_packet->mtu_option_idx];
		int start = section->start;
		int length = section->length;
		uint8_t *buffer = timeout_packet;
		// Move options follow mtu option to the front
		memmove(buffer + start, buffer + start + length,
			ra_packet->packet_length - (start + length));
		// Copy the MTU option from the original packet buffer
		memcpy(buffer + ra_packet->packet_length - length,
		       ra_packet->packet_buffer + start, length);
	}

	// TODO(b/409495432) - Get the real ifindex of the incoming RA.
	ra_proxy_send_unsolicited_ra(ras_timers.sta_ifindex, timeout_packet, ra_packet->packet_length);
	kfree(timeout_packet);
	if (is_remove_ra_timers) {
		ra_timers_remove(ra_packet);
	}
}

static void refresh_timer_expired(const ra_timers_t *ra_timers) {
	const ra_packet_t *ra_packet = &ra_timers->ra_packet;

	ra_proxy_send_unsolicited_ra(ras_timers.sta_ifindex, ra_packet->proxied_packet, ra_packet->packet_length);
}

void ra_proxy_init(void) {
	ra_timers_init(&ras_timers, ra_proxy_lifetime_expired, refresh_timer_expired);
	is_initialized = 1;
}

void ra_proxy_deinit(void) {
	is_initialized = 0;
	ra_timers_deinit();
}

// return 0: pass
// return -1: drop
int ra_proxy_accept_packet(const uint8_t* packet, uint32_t packet_len, int ifindex) {
	static ra_packet_t ra_packet;
	int result;
	uint32_t min_lifetime;
	ra_timers_t *ra_timers;

	if (!is_initialized) {
		return 0;
	}

	if (ras_timers.sta_ifindex != ifindex) {
		return 0;
	}

	result = parse_ra_packet(packet, packet_len, &ra_packet);
	// Non RA packet
	if (result < 0) {
		return 0;
	}

	// RA packet with zero router lifetime
	if (ra_packet.router_lifetime == 0) {
		return 0;
	}

	// RA packets with a sufficiently large minimum lifetime are not supported.
	min_lifetime = get_min_lifetime(&ra_packet);
	if (min_lifetime > RA_PROXY_MAX_SUPPORT_LIFETIME_SECONDS) {
		return 0;
	}

	result = ra_timers_add_or_update(&ra_packet);
	if (result != RA_TIMERS_RESULT_SUCCESS
	    && result != RA_TIMERS_RESULT_NEW_ENTRY
	    && result != RA_TIMERS_RESULT_REPLACED_BY_SOLICITED) {
		// This RA packet is not proxied, go through the normal path.
		return 0;
	}

	if (result == RA_TIMERS_RESULT_NEW_ENTRY
	    || result == RA_TIMERS_RESULT_REPLACED_BY_SOLICITED) {
		ra_timers = ra_timers_get(&ra_packet);
		if (ra_timers == NULL) {
			// It shouldn't happen.
			ra_timers_remove(&ra_packet);
			return 0;
		}
		if (generate_proxied_packet(&ra_timers->ra_packet) != 0) {
			ra_timers_remove(&ra_packet);
			return 0;
		}
		if ((ra_timers->ra_packet.flags & RA_PACKET_FLAG_SOLICITED) != 0) {
			ra_proxy_send_to_kernel(ras_timers.sta_ifindex,
						ra_timers->ra_packet.proxied_packet,
						ra_timers->ra_packet.packet_length);
		} else {
			ra_proxy_send_unsolicited_ra(ras_timers.sta_ifindex,
						     ra_timers->ra_packet.proxied_packet,
						     ra_timers->ra_packet.packet_length);
		}
	} else if ((ra_packet.flags & RA_PACKET_FLAG_SOLICITED) != 0) {
		// Send Proxied RA to kernel for all solicited RAs.
		ra_timers = ra_timers_get(&ra_packet);
		if (ra_timers == NULL) {
			// It shouldn't happen.
			ra_timers_remove(&ra_packet);
			return 0;
		}
		ra_proxy_send_to_kernel(ras_timers.sta_ifindex,
					ra_timers->ra_packet.proxied_packet,
					ra_timers->ra_packet.packet_length);
	}

	return -1;
}

void ra_proxy_sta_connect(int ifindex)
{
	ras_timers.sta_ifindex = ifindex;
}

void ra_proxy_sta_disconnect(int ifindex) {
	if (ifindex == ras_timers.sta_ifindex) {
		ras_timers.sta_ifindex = 0;
		ra_timers_clear();
	}
}

