// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles XFRM packet offload.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */

#include "noa_ipsec_crypto.h"

#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <linux/scatterlist.h>

#include <net/xfrm.h>
#include <net/esp.h>
#include <net/ip6_route.h>

#include "noa_ipsec_utils.h"

/**
 * ESP helpers
 *
 * The following esp-related functions are copied from esp4.c.
 */
static void *esp_alloc_tmp(struct crypto_aead *aead, int nfrags, int extralen)
{
	unsigned int len;

	len = extralen;

	len += crypto_aead_ivsize(aead);

	if (len) {
		len += crypto_aead_alignmask(aead) &
		       ~(crypto_tfm_ctx_alignment() - 1);
		len = ALIGN(len, crypto_tfm_ctx_alignment());
	}

	len += sizeof(struct aead_request) + crypto_aead_reqsize(aead);
	len = ALIGN(len, __alignof__(struct scatterlist));

	len += sizeof(struct scatterlist) * nfrags;

	return kmalloc(len, GFP_ATOMIC);
}

struct esp_output_extra {
	__be32 seqhi;
	u32 esphoff;
};
#if 0
static inline void *esp_tmp_extra(void *tmp)
{
	return PTR_ALIGN(tmp, __alignof__(struct esp_output_extra));
}
#endif
static inline u8 *esp_tmp_iv(struct crypto_aead *aead, void *tmp, int extralen)
{
	return crypto_aead_ivsize(aead) ?
	       PTR_ALIGN((u8 *)tmp + extralen,
			 crypto_aead_alignmask(aead) + 1) : tmp + extralen;
}

static inline struct aead_request *esp_tmp_req(struct crypto_aead *aead, u8 *iv)
{
	struct aead_request *req;

	req = (void *)PTR_ALIGN(iv + crypto_aead_ivsize(aead),
				crypto_tfm_ctx_alignment());
	aead_request_set_tfm(req, aead);
	return req;
}

static inline struct scatterlist *esp_req_sg(struct crypto_aead *aead,
					     struct aead_request *req)
{
	return (void *)ALIGN((unsigned long)(req + 1) +
			     crypto_aead_reqsize(aead),
			     __alignof__(struct scatterlist));
}

struct noa_ipsec_packet {
	struct iphdr *ip_hdr;
	struct ipv6hdr *ipv6_hdr;
	struct udphdr *udp_hdr;
	struct ip_esp_hdr *esp_hdr;
	unsigned char *inner_pkt;
};

static void fill_esp_header(struct noa_ipsec_packet *pkt, int spi, int seq)
{
	pkt->esp_hdr->spi = spi;
	pkt->esp_hdr->seq_no = htonl(seq);
}

static void fill_outer_ipv4_udp_header(struct noa_ipsec_packet *pkt, const struct xfrm_state *sa,
				       const unsigned char *inner_hdr, int encrypted_data_len)
{
	const bool is_ipv4 = is_pkt_ipv4(inner_hdr);
	const struct iphdr *inner_v4_hdr = (struct iphdr *)inner_hdr;
	const struct ipv6hdr *inner_v6_hdr = (struct ipv6hdr *)inner_hdr;

	pkt->udp_hdr->source = sa->encap->encap_sport;
	pkt->udp_hdr->dest = sa->encap->encap_dport;
	pkt->udp_hdr->len = htons(encrypted_data_len + UDP_HEADER_SIZE);
	pkt->udp_hdr->check = 0;

	pkt->ip_hdr->version = 4;
	pkt->ip_hdr->ihl = 5;
	// TODO: Follow xfrm4_tunnel_encap_add to set ip_hdr->tos.
	pkt->ip_hdr->tos = is_ipv4 ? inner_v4_hdr->tos : 0;
	pkt->ip_hdr->tot_len = htons(encrypted_data_len + UDP_HEADER_SIZE + IP_HEADER_SIZE);
	pkt->ip_hdr->id = 0;
	// TODO: Follow xfrm4_tunnel_encap_add to set ip_hdr->frag_off.
	pkt->ip_hdr->frag_off = htons(IP_DF);
	// TODO: Follow xfrm4_tunnel_encap_add to set ip_hdr->ttl.
	pkt->ip_hdr->ttl = is_ipv4 ? inner_v4_hdr->ttl : inner_v6_hdr->hop_limit;
	pkt->ip_hdr->protocol = IPPROTO_UDP;
	pkt->ip_hdr->saddr = sa->props.saddr.a4;
	pkt->ip_hdr->daddr = sa->id.daddr.a4;
	pkt->ip_hdr->check = ip_fast_csum((unsigned char *)pkt->ip_hdr, pkt->ip_hdr->ihl);
}

static void fill_outer_ipv6_header(struct noa_ipsec_packet *pkt, const struct xfrm_state *sa,
				   const unsigned char *inner_hdr, int encrypted_data_len)
{
	const bool is_ipv4 = is_pkt_ipv4(inner_hdr);
	const struct iphdr *inner_v4_hdr = (struct iphdr *)inner_hdr;
	const struct ipv6hdr *inner_v6_hdr = (struct ipv6hdr *)inner_hdr;

	pkt->ipv6_hdr->version = 6;
	// Leave ipv6_hdr->priority unset as what xfrm6_tunnel_encap_add() does.
	// pkt->ipv6_hdr->priority = 0;
	if (!is_ipv4) {
		memcpy(pkt->ipv6_hdr->flow_lbl, inner_v6_hdr->flow_lbl,
			sizeof(inner_v6_hdr->flow_lbl));
	}
	pkt->ipv6_hdr->payload_len = htons(encrypted_data_len);
	pkt->ipv6_hdr->nexthdr = NEXTHDR_ESP;
	// TODO: Follow xfrm6_tunnel_encap_add to set ipv6_hdr->hop_limit.
	pkt->ipv6_hdr->hop_limit = is_ipv4 ? inner_v4_hdr->ttl : inner_v6_hdr->hop_limit;
	pkt->ipv6_hdr->saddr = *(struct in6_addr *)&sa->props.saddr;
	pkt->ipv6_hdr->daddr = *(struct in6_addr *)&sa->id.daddr;
}

static void fill_outer_headers(struct noa_ipsec_packet *pkt, const struct xfrm_state *sa,
			       const unsigned char *inner_hdr, int encrypted_data_len)
{
	return (sa->props.family == AF_INET) ?
		fill_outer_ipv4_udp_header(pkt, sa, inner_hdr, encrypted_data_len) :
		fill_outer_ipv6_header(pkt, sa, inner_hdr, encrypted_data_len);
}

static void init_esp_info(struct esp_info *esp, struct crypto_aead *aead, int orig_pkt_len,
			  int oseq, bool is_inner_pkt_ipv4)
{
	const int alen = crypto_aead_authsize(aead);
	const int blksize = ALIGN(crypto_aead_blocksize(aead), 4);

	esp->inplace = true;
	// TODO: Set esp->proto based on the MAC header.
	esp->proto = is_inner_pkt_ipv4 ? IPPROTO_IPIP : IPPROTO_IPV6;
	// TODO: Set esp->tfclen based on sa->tfcpad.
	esp->tfclen = 0;
	esp->clen = ALIGN(orig_pkt_len + 2 + esp->tfclen, blksize);
	esp->plen = esp->clen - orig_pkt_len - esp->tfclen;
	esp->tailen = esp->tfclen + esp->plen + alen;
	// TODO: Set esp->nfrags by checking whether fragmentation is required.
	esp->nfrags = 1;
	// TODO: oseq is lower 32-bit. Consider setting seqno with both lower and higher 32-bit
	esp->seqno = cpu_to_be64(oseq);
}

/**
 * encrypt_data - Does encryption given the `data`, `esp`, and `aead`.
 *
 * Note that the format of `data` is:
 *     | ESP Header | IV | IP Packet | ESP Trailer | Zero-padding for ESP Auth |
 *
 * However, only IV, IP Packet, and ESP Trailer will be encrypted.
 * After the encryption, the format of `data` will be:
 *     | ESP Header |      ESP Encrypted Data      |        ESP Auth           |
 *
 * If the encryption is successful, the size of ESP Packet is returned; otherwise, a negative value
 * is returned.
 */
static int encrypt_data(unsigned char *data, int data_len, struct esp_info *esp,
			struct crypto_aead *aead)
{
	u8 *iv;
	void *tmp;
	int ivlen;
	int assoclen;
	int extralen;
	int err;
	//struct esp_output_extra *extra;
	struct scatterlist *sg;
	struct aead_request *req;

	// TODO: Set it based on x->props.flags & XFRM_STATE_ESN.
	extralen = 0;
	ivlen = crypto_aead_ivsize(aead);
	assoclen = sizeof(struct ip_esp_hdr);

	tmp = esp_alloc_tmp(aead, esp->nfrags, extralen);
	//extra = esp_tmp_extra(tmp);
	iv = esp_tmp_iv(aead, tmp, extralen);
	req = esp_tmp_req(aead, iv);
	sg = esp_req_sg(aead, req);

	sg_init_table(sg, esp->nfrags);
	sg_set_buf(sg, data, data_len);
	// TODO: Check if it's needed.
	sg_mark_end(&sg[1]);

	aead_request_set_crypt(req, sg, sg, ivlen + esp->clen, iv);
	aead_request_set_ad(req, assoclen);

	memset(iv, 0, ivlen);
	memcpy(iv + ivlen - min(ivlen, 8), (u8 *)&esp->seqno + 8 - min(ivlen, 8), min(ivlen, 8));

	err = crypto_aead_encrypt(req);
	if (err < 0)
		return err;

	return sg->length;
}

static void setup_noa_ipsec_packet_for_packet_offload(struct noa_ipsec_packet *ipsec_pkt,
						      unsigned char *pkt, int family)
{
	if (family == AF_INET) {
		ipsec_pkt->ip_hdr = (struct iphdr *)pkt;
		ipsec_pkt->udp_hdr = (struct udphdr *)(pkt + IP_HEADER_SIZE);
		ipsec_pkt->esp_hdr =
			(struct ip_esp_hdr *)(pkt + IP_HEADER_SIZE + UDP_HEADER_SIZE);
	} else {
		ipsec_pkt->ipv6_hdr = (struct ipv6hdr *)pkt;
		ipsec_pkt->esp_hdr = (struct ip_esp_hdr *)(pkt + IP6_HEADER_SIZE);
	}
}

/**
 * This function is implemented based on esp_output().
 * Current implement supports following:
 * - IPv4 packet -> IPv4 + UDP-encap packet
 * - IPv6 packet -> IPv4 + UDP-encap packet
 *
 * @sa: The Security Association for the encryption
 * @pkt: The packet to be encrypted. The packet will remain unchanged, and the encrypted data
 *       will be stored in `buf`
 * @pkt_len: The length of `pkt`
 * @out_pkt: The buffer to store the encrypted packet (Must be IPv4 with UDP-encapsulation)
 * @oseq: The sequence number for the ESP seq of the output packet.
 * @return: If the encryption is successful, the size of encrypted packet is returned; otherwise,
 *          a negative value is returned
 *
 * The format of the encrypted packet will be:
 *       | IP Header | UDP Header | ESP Header | ESP Encrypted Data | ESP Auth |
 *
 *       The data format of each ESP field:
 *         - ESP Header: | SPI | seq |
 *           - SPI: 4 bytes
 *           - seq: 4 bytes
 *         - ESP Encrypted Data: | IV | IP Packet | ESP Trailer |
 *           - IV: `ivlen` bytes
 *           - ESP Trailer: | Padding | Padding Length | Next Header |
 *             - Padding: `padlen` bytes
 *             - Padding Length: 1 byte
 *             - Next Header: 1 byte
 *         - ESP Auth: ICV
 *           - ICV: `alen` bytes
 */
int encrypt_packet_for_packet_offload(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len,
		   unsigned char *out_pkt, int oseq)
{
	struct crypto_aead *aead = sa->data;
	const int iv_len = crypto_aead_ivsize(aead);
	struct noa_ipsec_packet output_pkt;
	unsigned char inner_hdr[IP6_HEADER_SIZE];
	unsigned char *trailer;
	struct esp_info esp;
	int trailer_len;
	int encrypted_data_len;

	if (!is_pkt_ipv4(pkt) && !is_pkt_ipv6(pkt))
		return -EPROTONOSUPPORT;

	// Copy the IP header as it will be used to fill the outer IP header after the encryption.
	memcpy(inner_hdr, pkt, is_pkt_ipv4(pkt) ? IP_HEADER_SIZE : IP6_HEADER_SIZE);

	setup_noa_ipsec_packet_for_packet_offload(&output_pkt, out_pkt, sa->props.family);
	fill_esp_header(&output_pkt, sa->id.spi, oseq);

	// Step 1. Prepare the data to be encrypted
	//   a. ESP IV
	//   b. original packet
	//   c. trailer data

	// Step 1.a
	// Do nothing. Just need to preserve the space of length `iv_len` in step 1.b.

	// Step 1.b
	memcpy(output_pkt.esp_hdr->enc_data + iv_len, pkt, pkt_len);

	// Step 1.c
	// Reference to esp_output_head() to append trailer.
	//   1. esp_output_fill_trailer(tail, esp->tfclen, esp->plen, esp->proto);
	trailer = output_pkt.esp_hdr->enc_data + iv_len + pkt_len;

	init_esp_info(&esp, aead, pkt_len, oseq, is_pkt_ipv4(inner_hdr));
	trailer_len = esp.tailen;
	// TODO: Implement a dedicated version of esp_output_fill_trailer for noa.
	esp_output_fill_trailer(trailer, esp.tfclen, esp.plen, esp.proto);

	// Step 2. Encrypt the data that is set up in step 1.
	// Reference to esp_output_tail() to encrypt data.
	encrypted_data_len = encrypt_data(output_pkt.esp_hdr->enc_data - ESP_HEADER_SIZE,
		iv_len + pkt_len + trailer_len + ESP_HEADER_SIZE, &esp, aead);

	if (encrypted_data_len < 0)
		return encrypted_data_len;

	// Step 3. Complete UDP encapsulation and outer IP header.
	// TODO: Move the encapsulation out of this crypto file.
	// Complete outer IP/IPv6 headers, and UDP encapsulation if needed.
	fill_outer_headers(&output_pkt, sa, inner_hdr, encrypted_data_len);

	return (sa->props.family == AF_INET) ? ntohs(output_pkt.ip_hdr->tot_len) :
		(ntohs(output_pkt.ipv6_hdr->payload_len) + IP6_HEADER_SIZE);
}

static void setup_noa_ipsec_packet(struct noa_ipsec_packet *ipsec_pkt, unsigned char *pkt,
				   int iv_len)
{
	int offset = 0;
	struct iphdr *ip_hdr = NULL;
	struct ipv6hdr *ipv6_hdr = NULL;
	struct udphdr *udp_hdr = NULL;
	struct ip_esp_hdr *esp_hdr = NULL;

	if (is_pkt_ipv4(pkt)) {
		ip_hdr = (struct iphdr *)pkt;
		offset += IP_HEADER_SIZE;
		if (ip_hdr->protocol == IPPROTO_UDP) {
			udp_hdr = (struct udphdr *)(pkt + offset);
			offset += UDP_HEADER_SIZE;
		}
		esp_hdr = (struct ip_esp_hdr *)(pkt + offset);
	} else {
		ipv6_hdr = (struct ipv6hdr *)pkt;
		offset += IP6_HEADER_SIZE;
		if (ipv6_hdr->nexthdr == NEXTHDR_UDP) {
			udp_hdr = (struct udphdr *)(pkt + offset);
			offset += UDP_HEADER_SIZE;
		}
		esp_hdr = (struct ip_esp_hdr *)(pkt + offset);
	}

	ipsec_pkt->ip_hdr = ip_hdr;
	ipsec_pkt->ipv6_hdr = ipv6_hdr;
	ipsec_pkt->udp_hdr = udp_hdr;
	ipsec_pkt->esp_hdr = esp_hdr;
	ipsec_pkt->inner_pkt = esp_hdr->enc_data + iv_len;
}

__maybe_unused
static void calculate_udp4_checksum(unsigned char *pkt)
{
	const struct iphdr *ip_hdr = (struct iphdr *)pkt;
	struct udphdr *udp_hdr = get_udp_header(pkt);

	if (ip_hdr->version != 4 || !udp_hdr)
		return;

	udp_hdr->check = 0;
	udp_hdr->check = csum_tcpudp_magic(ip_hdr->saddr, ip_hdr->daddr, ntohs(udp_hdr->len),
			IPPROTO_UDP, csum_partial(udp_hdr, ntohs(udp_hdr->len), 0));
}

static void calculate_udp6_checksum(unsigned char *pkt)
{
	struct ipv6hdr *ipv6_hdr = (struct ipv6hdr *)pkt;
	struct udphdr *udp_hdr = get_udp_header(pkt);

	if (ipv6_hdr->version != 6 || !udp_hdr)
		return;

	udp_hdr->check = 0;
	udp_hdr->check = csum_ipv6_magic(&ipv6_hdr->saddr, &ipv6_hdr->daddr, ntohs(udp_hdr->len),
			IPPROTO_UDP, csum_partial(udp_hdr, ntohs(udp_hdr->len), 0));
}

__maybe_unused
static void calculate_udp_checksum(unsigned char *pkt)
{
	return is_pkt_ipv4(pkt) ? calculate_udp4_checksum(pkt) : calculate_udp6_checksum(pkt);
}

/**
 * This function is implemented based on encrypt_packet_for_packet_offload().
 * Current implement supports following:
 * - IPv4 + UDP-encap + ESP unencrypted packet -> IPv4 + UDP-encap + ESP encrypted packet
 * - IPv6 + ESP unencrypted packet -> IPv6 + ESP encrypted packet
 * - IPv6 + UDP-encap + ESP unencrypted packet -> IPv6 + UDP-encap + ESP encrypted packet
 *
 * @sa: The Security Association for the encryption
 * @pkt: The packet to be encrypted.
 * @pkt_len: The length of `pkt`
 * @oseq: The sequence number for the ESP seq of the output packet.
 * @return: If the encryption is successful, the size of encrypted packet is returned; otherwise,
 *          a negative value is returned
 */
int encrypt_packet_for_crypto_offload(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len)
{
	struct crypto_aead *aead = sa->data;
	const int iv_len = crypto_aead_ivsize(aead);
	struct noa_ipsec_packet output_pkt;
	const int minimal_pkt_len = is_pkt_ipv4(pkt) ?
		(IP_HEADER_SIZE + UDP_HEADER_SIZE + ESP_HEADER_SIZE + iv_len) :
		(IP6_HEADER_SIZE + ESP_HEADER_SIZE + iv_len);
	int inner_pkt_len;
	struct esp_info esp;
	int trailer_len;
	int encrypted_data_len;
	int oseq;

	if (!is_pkt_ipv4(pkt) && !is_pkt_ipv6(pkt))
		return -EPROTONOSUPPORT;

	if (pkt_len < minimal_pkt_len)
		return -EPROTONOSUPPORT;

	setup_noa_ipsec_packet(&output_pkt, pkt, iv_len);

	inner_pkt_len = is_pkt_ipv4(output_pkt.inner_pkt) ?
		ntohs(((struct iphdr *) output_pkt.inner_pkt)->tot_len) :
		ntohs(((struct ipv6hdr *) output_pkt.inner_pkt)->payload_len) + IP6_HEADER_SIZE;

	oseq = ntohl(output_pkt.esp_hdr->seq_no);
	init_esp_info(&esp, aead, inner_pkt_len, oseq, is_pkt_ipv4(output_pkt.inner_pkt));
	trailer_len = esp.tailen;

	// Reference to esp_output_tail() to encrypt data.
	encrypted_data_len = encrypt_data(output_pkt.esp_hdr->enc_data - ESP_HEADER_SIZE,
		iv_len + inner_pkt_len + trailer_len + ESP_HEADER_SIZE, &esp, aead);

	if (encrypted_data_len < 0)
		return encrypted_data_len;

	if (is_pkt_ipv6(pkt))
		calculate_udp6_checksum(pkt);

	// The linux kernel has already calculated the packet size after the encryption, so the
	// packet size should remain the same.
	return pkt_len;
}

/**
 * decrypt_data - Does decryption given the `data` and `aead`.
 *
 * The format of `data` is:
 *     | ESP Header |      ESP Encrypted Data      |        ESP Auth           |
 *
 * After the decryption, the format of `data` will be:
 *     | ESP Header | IV | IP Packet | ESP Trailer | Zero-padding for ESP Auth |
 *
 * If the decryption is successful, the size of IP Packet is returned; otherwise, a negative value
 * is returned.
 */
static int decrypt_data(unsigned char *data, int data_len, struct crypto_aead *aead)
{
	struct aead_request *req;
	int ivlen;
	int alen;
	int hlen;
	int elen;
	// TODO: Set nfrags correctly
	int nfrags = 1;
	int assoclen = sizeof(struct ip_esp_hdr);
	int seqhilen = 0;
	void *tmp;
	u8 *iv;
	struct scatterlist *sg;
	int err;
	int trimlen;
	u8 padlen;
	int padlen_position;

	ivlen = crypto_aead_ivsize(aead);
	alen = crypto_aead_authsize(aead);
	elen = data_len - sizeof(struct ip_esp_hdr) - ivlen;
	hlen = sizeof(struct ip_esp_hdr) + ivlen;

	tmp = esp_alloc_tmp(aead, nfrags, seqhilen);
	iv = esp_tmp_iv(aead, tmp, seqhilen);
	req = esp_tmp_req(aead, iv);
	sg = esp_req_sg(aead, req);

	sg_init_table(sg, nfrags);
	sg_set_buf(sg, data, data_len);
	aead_request_set_crypt(req, sg, sg, elen + ivlen, iv);
	aead_request_set_ad(req, assoclen);

	err = crypto_aead_decrypt(req);
	if (err < 0)
		return err;

	// Calculate the size of data (ESP Trailer and ESP Auth) appended after IP Packet.
	// 2 bytes for Padding Length and Next Header
	padlen_position = data_len - alen - 2;

	padlen = *(data + padlen_position);
	trimlen = padlen + alen + 2;

	// TODO: Check if anything is missing but is done in esp_input_done2().

	return data_len - hlen - trimlen;
}

/**
 * This function is implemented based on esp_input().
 * Current implement supports following:
 *   Crypto offload mode:
 *   - IPv4 + UDP-encap + ESP encrypted packet -> IPv4 + UDP-encap + ESP decrypted packet
 *   - IPv6 + ESP encrypted packet -> IPv6 + ESP decrypted packet
 *   - IPv6 + UDP-encap + ESP encrypted packet -> IPv6 + UDP-encap + ESP decrypted packet
 *   Packet offload mode:
 *   - IPv4 + UDP-encap + ESP encrypted packet -> Decrypted inner IPv4 packet
 *   - IPv4 + UDP-encap + ESP encrypted packet -> Decrypted inner IPv6 packet
 *
 * @sa: The Security Association for the decryption
 * @pkt: The IPv4 + UDP-encap'ed packet to be decrypted
 * @pkt_len: The length of the packet
 * @out_pkt: The buffer to store the decrypted packet
 * @return: If the decryption is successful, the size of decrypted packet is returned; otherwise,
 *          a negative value is returned.
 *
 * The data format of `pkt` is:
 *     | IP Header | UDP Header | ESP Header | ESP Encrypted Data | ESP Auth |
 *
 * The data format of each ESP field:
 *   - ESP Header: | SPI | seq |
 *     - SPI: 4 bytes
 *     - seq: 4 bytes
 *   - ESP Encrypted Data: | IV | IP Packet | ESP Trailer |
 *     - IV: `ivlen` bytes
 *     - ESP Trailer: | Padding | Padding Length | Next Header |
 *       - Padding: `padlen` bytes
 *       - Padding Length: 1 byte
 *       - Next Header: 1 byte
 *   - ESP Auth: ICV
 *     - ICV: `alen` bytes
 *
 * Step 1: Determine the position of ESP header.
 * Step 2: Decrypt ESP packet
 * Step 3: Get the inner IP Packet (packet offload mode only)
 */
int decrypt_packet(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len,
		   unsigned char *out_pkt)
{
	struct noa_ipsec_packet output_pkt;
	struct crypto_aead *aead = sa->data;
	const int ivlen = crypto_aead_ivsize(aead);
	int out_pkt_len;
	int esp_pkt_len;
	const bool is_packet_offload = (sa->xso.type == XFRM_DEV_OFFLOAD_PACKET);

	// Step 1: Determine the position of ESP header.
	// Noted that packet validation is not implemented. Any preprocessing such as IP checksum
	// validation and packet reassembly should be performed before step 1.
	setup_noa_ipsec_packet(&output_pkt, pkt, ivlen);

	esp_pkt_len = pkt_len - (output_pkt.ip_hdr ? IP_HEADER_SIZE : IP6_HEADER_SIZE);
	if (output_pkt.udp_hdr)
		esp_pkt_len -= UDP_HEADER_SIZE;

	// Step 2: Decrypt ESP packet
	out_pkt_len = decrypt_data((unsigned char *)output_pkt.esp_hdr, esp_pkt_len, aead);
	if (out_pkt_len < 0)
		return out_pkt_len;

	// For crypto offload mode, return the packet without decapsulating outer headers.
	if (!is_packet_offload) {
		// To optimize the performance, the UDP checksum is not recalculated after the
		// decryption. To avoid the packet dropped by the kernel, skb->ip_summed will
		// be set to CHECKSUM_UNNECESSARY on the driver side.
		return pkt_len;
	}

	// Step 3: Get the inner Packet.
	memcpy(out_pkt, output_pkt.esp_hdr->enc_data + ivlen, out_pkt_len);

	return out_pkt_len;
}
