#ifndef __NOA_TEST_HARNESS_H__
#define __NOA_TEST_HARNESS_H__

#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/bitmap.h>

#include "common/ring.h"

struct google_dpa;
struct google_dpa_doorbell;
struct noa_harness;
struct noa_harness_vdev;

/**
 * enum noa_harness_device_mode - Defines the behavior of the test harness device.
 * @NOA_HARNESS_DEVICE_MODE_FEEDTHROUGH: Packets are passed through without modification.
 * @NOA_HARNESS_DEVICE_MODE_NETENGINE: Packets are forwarded to the NEP NetEngine for processing.
 */
enum noa_harness_device_mode {
	NOA_HARNESS_DEVICE_MODE_FEEDTHROUGH,
	NOA_HARNESS_DEVICE_MODE_NETENGINE,
};

#define NOA_HARNESS_MODEM_DESC_BYTE ((16U))

/**
 * enum noa_virtual_device_type - Defines the types of virtual devices in the test harness.
 * @NOA_VIRTUAL_WLAN_HOST: Represents the host-side of the WLAN virtual device.
 * @NOA_VIRTUAL_WLAN_NCP: Represents the network co-processor (NCP) side of the
 *			WLAN virtual device.
 * @NOA_VIRTUAL_DEVICE_MAX: The maximum number of virtual device types.
 */
enum noa_virtual_device_type {
	NOA_VIRTUAL_WLAN_HOST,
	NOA_VIRTUAL_WLAN_NCP,
	NOA_VIRTUAL_MODEM_HOST,
	NOA_VIRTUAL_MODEM_NCP,
	NOA_VIRTUAL_DEVICE_MAX,
};

/**
 * struct harness_ring - Represents a communication ring in the test harness.
 * @id:			The unique identifier for the ring.
 * @direction:		The data flow direction of the ring.
 * @ring:		The underlying NOA ring wrapper.
 * @ring_buffer:	The CPU virtual address of the ring buffer.
 * @ring_buffer_dma:	The DMA address of the ring buffer.
 * @lock:		A spinlock to protect access to the ring.
 */
struct harness_ring {
	u8 id;
	u8 direction;
	struct noa_ring_wrapper ring;
	void *ring_buffer;
	dma_addr_t ring_buffer_dma;
	spinlock_t lock;
};

/**
 * struct noa_harness_memory_mapper - Manages memory mappings for the test harness.
 * @dev:		Pointer to the device structure.
 * @dpa:		Pointer to the Google DPA structure.
 * @dpa_dev:		The device for the DPA side.
 * @addr_htable:	A hash table for storing address mappings.
 * @collection:		A list head for tracking all mapping nodes.
 * @node_lock:		A spinlock to protect the mapping collection.
 */
struct noa_harness_memory_mapper {
	struct device *dev;
	struct google_dpa *dpa;
	struct device *dpa_dev;
	struct rhashtable addr_htable;
	struct list_head collection;
	spinlock_t node_lock;
};

struct noa_desc;

/**
 * struct noa_harness_isr_task - Represents a task to be run in an interrupt
 *				 service routine.
 * @vdev:	Pointer to the virtual device associated with this task.
 * @list:	The list head for linking this task into a list of ISR tasks.
 */
struct noa_harness_isr_task {
	struct noa_harness_vdev *vdev;
	struct list_head list;
};

/**
 * struct noa_harness_pkt - Represents a packet in the test harness.
 * @should_drop: A flag indicating if this packet must be dropped.
 * @send_to_netengine: A flag indicating that the packet is sent for NetEngine processing.
 * @needs_nep_buffer_refill: A flag indicating if the NEP buffer pool needs to be refilled.
 * @head_offset: The offset from the start of the buffer to the actual packet
 *		 data.
 * @size:        The size of the packet.
 * @tkid:        A unique token identifier for the packet, used for tracking.
 * @dpa_address: The DMA address of the skb->head after being mapped by the DPA
 *		 memory mapper.
 * @address:     The CPU virtual address of the skb->head.
 */
struct noa_harness_pkt {
	bool should_drop;
	bool send_to_netengine;
	bool needs_nep_buffer_refill;
	u16 head_offset;
	u16 size;
	u16 tkid;
	u64 dpa_address;
	u64 address;
};

struct harness_nep_buffer_pool {
	u8 pool_id;
	u32 size;
	u32 tkid_offset;
	spinlock_t lock;
	struct noa_ring_wrapper ring;
	void *ring_buffer;
	dma_addr_t ring_buffer_dma;
	void *doorbell;
};

/**
 * struct noa_harness_vdev - Represents a virtual network device in the test harness.
 * @support_nep_buffer_pool: 	Support NEP buffer pool.
 * @mode:		The mode of this device.
 * @dev:		Pointer to the net_device structure.
 * @tm:			Pointer to the main test harness structure.
 * @tx_ring:		The transmission ring for this device.
 * @format_tx_data:	Callback to format the transmit data descriptor.
 * @rx_ring:		The reception ring for this device.
 * @parse_rx_pkt:	Callback to parse the descriptor of a received packet.
 * @doorbell:		Pointer to the doorbell used by this device.
 * @work:			The work struct for handling deferred tasks.
 * @isr_task:		The ISR task associated with this device.
 * @nep_buffer_pool:	The NEP buffer pool for this device.
 * @refill_work:	The work struct for refilling the NEP buffer pool.
 */
struct noa_harness_vdev {
	bool support_nep_buffer_pool;
	enum noa_harness_device_mode mode;
	struct net_device *dev;
	struct noa_harness *tm;
	struct harness_ring tx_ring;
	int (*format_tx_data)(void *data, const struct noa_harness_pkt *pkt);
	struct harness_ring rx_ring;
	int (*parse_rx_pkt)(struct noa_harness_pkt *pkt, const void *data);
	void *doorbell;
	struct work_struct work;
	struct work_struct refill_work;
	struct noa_harness_isr_task isr_task;
	struct harness_nep_buffer_pool nep_buffer_pool;
};

/**
 * enum noa_harness_doorbell_id - Defines the doorbell identifiers used in the
 *				  test harness.
 * @NOA_HARNESS_DOORBELL_WLAN:	The doorbell for the WLAN virtual device.
 * @NOA_HARNESS_DOORBELL_MODEM:	The doorbell for the modem virtual device.
 * @NOA_HARNESS_DOORBELL_MAX:	The maximum number of doorbell identifiers.
 */
enum noa_harness_doorbell_id {
	NOA_HARNESS_DOORBELL_WLAN,
	NOA_HARNESS_DOORBELL_MODEM,
	NOA_HARNESS_DOORBELL_MAX,
};

/**
 * struct noa_harness_doorbell - Represents a doorbell in the test harness.
 * @hw_doorbell:	Pointer to the underlying hardware doorbell.
 * @hw_doorbell_dev:	Pointer to the device associated with the hardware doorbell.
 * @isr_task_list:	A list of ISR tasks to be executed when the doorbell is
 *			rung.
 */
struct noa_harness_doorbell {
	struct google_dpa_doorbell *hw_doorbell;
	struct device *hw_doorbell_dev;
	struct list_head isr_task_list;
};

#define NOA_HARNESS_GENERIC_TKID_OFFSET ((0U))
#define MAX_NOA_HARNESS_GENERIC_TKID_SIZE ((4096U))
#define NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_OFFSET                                               \
	(NOA_HARNESS_GENERIC_TKID_OFFSET + MAX_NOA_HARNESS_GENERIC_TKID_SIZE)
#define MAX_NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_SIZE ((512U))
#define NOA_HARNESS_MODEM_NEP_BUFFER_POOL_TKID_OFFSET                                              \
	(NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_OFFSET +                                            \
	 MAX_NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_SIZE)
#define MAX_NOA_HARNESS_MODEM_NEP_BUFFER_POOL_TKID_SIZE ((512U))
#define MAX_NOA_HARNESS_SKB_ENTRIES                                                                \
	((MAX_NOA_HARNESS_GENERIC_TKID_SIZE + MAX_NOA_HARNESS_WLAN_NEP_BUFFER_POOL_TKID_SIZE +     \
	  MAX_NOA_HARNESS_MODEM_NEP_BUFFER_POOL_TKID_SIZE))

/**
 * struct noa_harness_skb_table_entry - An entry in the SKB table.
 * @is_map_to_dpa:	A flag indicating if the SKB is mapped to the DPA.
 * @map_dpa_len:	The length of the DPA mapping in bytes. This is only
 *			valid if is_map_to_dpa is true.
 * @skb_address:	The address of the SKB.
 */
struct noa_harness_skb_table_entry {
	bool is_map_to_dpa;
	size_t map_dpa_len;
	void *skb_address;
};

/**
 * struct noa_harness_buffer_manager - Manages SKB buffers for the test harness.
 * @dev:		Pointer to the device structure.
 * @skb_table:		An array of SKB table entries.
 * @skb_bitmap:		A bitmap for tracking allocated SKB IDs.
 * @lock:		A spinlock to protect access to the buffer manager.
 * @mapper:		The memory mapper for this buffer manager.
 */
struct noa_harness_buffer_manager {
	struct device *dev;
	struct noa_harness_skb_table_entry skb_table[MAX_NOA_HARNESS_SKB_ENTRIES];
	unsigned long skb_bitmap[BITS_TO_LONGS(MAX_NOA_HARNESS_SKB_ENTRIES)];
	spinlock_t lock;
	struct noa_harness_memory_mapper mapper;
};

/**
 * struct noa_harness - The main structure for the NOA test harness.
 * @dev:		Pointer to the device structure.
 * @dpa:		Pointer to the Google DPA structure.
 * @noa_harness_netdevs: An array of pointers to the virtual network devices.
 * @kobj:		The kernel object for sysfs integration.
 * @buf_mgr:		The buffer manager for this harness.
 * @wq:			The workqueue for deferred tasks.
 * @doorbells:		An array of doorbell structures.
 */
struct noa_harness {
	struct device *dev;
	struct google_dpa *dpa;
	struct net_device *noa_harness_netdevs[NOA_VIRTUAL_DEVICE_MAX];
	struct kobject kobj;
	struct noa_harness_buffer_manager buf_mgr;
	struct workqueue_struct *wq;
	struct noa_harness_doorbell doorbells[NOA_HARNESS_DOORBELL_MAX];
};

#endif /* __NOA_TEST_HARNESS_H__ */
