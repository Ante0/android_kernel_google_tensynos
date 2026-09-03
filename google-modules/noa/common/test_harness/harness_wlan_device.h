#ifndef __HARNESS_WLAN_DEVICE_H__
#define __HARNESS_WLAN_DEVICE_H__

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>

#include "noa_test_harness.h"

/**
 * noa_harness_wlan_isr_task() - The work function for WLAN ISR
 * @work: The work_struct context
 *
 * This function is scheduled to run in a workqueue to simulate the bottom-half
 * of an interrupt service routine. It retrieves the virtual device context and
 * reads data from the harness ring buffer.
 */
void noa_harness_wlan_isr_task(struct work_struct *work);

/**
 * noa_harness_wlan_host_netdev_setup() - Setup function for the host harness net device
 * @dev: The net_device to set up
 *
 * This function initializes the provided net_device with standard Ethernet
 * settings, assigns the custom net_device_ops, and sets a random hardware
 * address. It prepares the device for registration with the networking stack.
 */
void noa_harness_wlan_host_netdev_setup(struct net_device *dev);

/**
 * noa_harness_wlan_ncp_netdev_setup() - Setup function for the NCP harness net device
 * @dev: The net_device to set up
 *
 * This function initializes the provided net_device with standard Ethernet
 * settings, assigns the custom net_device_ops, and sets a fixed hardware
 * address. It prepares the device for registration with the networking stack.
 */
void noa_harness_wlan_ncp_netdev_setup(struct net_device *dev);

/**
 * noa_harness_wlan_format_tx_data() - Formats the transmit data descriptor
 * @data: Pointer to the destination buffer where the descriptor will be written
 * @pkt:  Pointer to the source packet structure containing metadata
 *
 * This function populates a noa_desc structure with information required for
 * transmitting a packet, such as the forwarding reason and packet identifier.
 *
 * Return: 0 on success.
 */
int noa_harness_wlan_format_tx_data(void *data, const struct noa_harness_pkt *pkt);

/**
 * noa_harness_wlan_parse_rx_pkt() - Parses the descriptor of a received packet
 * @pkt:  Pointer to the destination packet structure to be filled
 * @data: Pointer to the source buffer containing the received descriptor
 *
 * This function extracts metadata, like the packet identifier, from a received
 * noa_desc descriptor and populates the corresponding fields in the
 * noa_harness_pkt structure.
 *
 * Return: 0 on success.
 */
int noa_harness_wlan_parse_rx_pkt(struct noa_harness_pkt *pkt, const void *data);

#endif /* __HARNESS_WLAN_DEVICE_H__ */
