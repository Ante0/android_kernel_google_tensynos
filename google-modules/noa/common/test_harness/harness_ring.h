#ifndef __HARNESS_RING_H__
#define __HARNESS_RING_H__

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include "noa_test_harness.h"

/**
 * noa_harness_ring_read() - Reads data from the harness ring.
 * @vdev: Pointer to the virtual device structure.
 *
 * Return: 0 on success, -EAGAIN if the ring is empty, or another negative error code on failure.
 */
int noa_harness_ring_read(struct noa_harness_vdev *vdev);

/**
 * noa_harness_ring_write() - Writes data to the harness ring.
 * @vdev: Pointer to the virtual device structure.
 * @skb: Pointer to the socket buffer to write.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_ring_write(struct noa_harness_vdev *vdev, struct sk_buff *skb);

/**
 * noa_harness_register_ring() - Registers and initializes a harness ring.
 * @tm: Pointer to the main test harness structure.
 * @vdev: Pointer to the virtual device associated with this ring.
 * @h_ring: Pointer to the harness ring structure to be initialized.
 * @type: The type of the ring (e.g., producer or consumer).
 * @direction: The data flow direction of the ring.
 * @ring_id: The unique identifier for the ring.
 * @name: A descriptive name for the ring.
 * @desc_len: The length of each descriptor in the ring.
 * @max_items: The maximum number of items the ring can hold.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_register_ring(struct noa_harness *tm, struct noa_harness_vdev *vdev,
			      struct harness_ring *h_ring, u8 type, u8 direction, u8 ring_id,
			      const char *name, u32 desc_len, u32 max_items);

/**
 * noa_harness_unregister_ring() - Unregisters and cleans up a harness ring.
 * @tm: Pointer to the main test harness structure.
 * @h_ring: Pointer to the harness ring to be unregistered.
 */
void noa_harness_unregister_ring(struct noa_harness *tm, struct harness_ring *h_ring);

#endif /* __HARNESS_RING_H__ */
