#ifndef NOA_WLAN_NEP_HELPER_H_
#define NOA_WLAN_NEP_HELPER_H_
#include <common/ring.h>

struct noa_wlan_client;

extern int noa_wlan_add_session_entry(struct noa_wlan_client *client, u8 *pkt,
				      struct noa_session_info *txinfo);
extern int noa_wlan_client_ring_write(struct noa_wlan_client *client, noa_ring_producer *ring,
				      void *data);
extern bool noa_wlan_client_ring_is_empty(noa_ring_consumer *ring);
extern int noa_wlan_client_ring_read_loop(struct noa_wlan_client *client, noa_ring_consumer *ring,
					  int (*read_func)(struct noa_wlan_client *, void **));
extern int noa_wlan_ring_begin_processing(struct noa_ring_wrapper *ring);
extern void noa_wlan_ring_complete_processing(struct noa_ring_wrapper *ring);
extern ssize_t noa_wlan_ring_read(noa_ring_consumer *consumer, void *data, size_t len);
extern int noa_wlan_replenish_ring_write(struct noa_wlan_client *client, noa_ring_producer *ring,
					 struct noa_bm_buf *bm_bufs, u32 bm_bufs_num, bool to_dev);
extern int noa_wlan_feedback_ring_write(struct noa_wlan_client *client, noa_ring_producer *ring,
					struct noa_desc *d);

#endif // WLAN_NEP_HELPER_H_
