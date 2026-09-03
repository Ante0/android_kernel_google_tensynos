#ifndef __HARNESS_MODEM_DEVICE_H__
#define __HARNESS_MODEM_DEVICE_H__

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>

#include "noa_test_harness.h"

/**
 * noa_harness_modem_isr_task() - The work function for MODEM ISR
 * @work: The work_struct context
 *
 * This function is scheduled to run in a workqueue to simulate the bottom-half
 * of an interrupt service routine. It retrieves the virtual device context and
 * reads data from the harness ring buffer.
 */
void noa_harness_modem_isr_task(struct work_struct *work);

/**
 * noa_harness_modem_netdev_setup() - Setup function for the harness modem net device
 * @dev: The net_device to set up
 *
 * This function initializes the provided net_device as a raw IP,
 * point-to-point interface. It assigns the custom net_device_ops and
 * prepares the device for registration with the networking stack.
 */
void noa_harness_modem_netdev_setup(struct net_device *dev);

/**
 * noa_harness_modem_ncp_format_tx_data() - Formats TX data for NCP
 * @data: Pointer to the data descriptor to be formatted
 * @pkt:  Pointer to the packet information
 *
 * This function populates a NOA descriptor for a packet being transmitted from
 * the NCP side. It sets up forwarding information and vendor-specific metadata.
 *
 * Return: 0 on success.
 */
int noa_harness_modem_ncp_format_tx_data(void *data, const struct noa_harness_pkt *pkt);

/**
 * noa_harness_modem_ncp_parse_rx_pkt() - Parses RX packet on NCP
 * @pkt:  Pointer to the packet structure to store parsed information
 * @data: Pointer to the received data descriptor
 *
 * This function parses a received NOA descriptor on the NCP side to extract
 * packet metadata, such as the TKID.
 *
 * Return: 0 on success.
 */
int noa_harness_modem_ncp_parse_rx_pkt(struct noa_harness_pkt *pkt, const void *data);

/**
 * noa_harness_modem_host_format_tx_data() - Formats TX data for the host
 * @data: Pointer to the data descriptor to be formatted
 * @pkt:  Pointer to the packet information
 *
 * This function populates a NOA descriptor for a packet being transmitted from
 * the host side.
 *
 * Return: 0 on success.
 */
int noa_harness_modem_host_format_tx_data(void *data, const struct noa_harness_pkt *pkt);

/**
 * noa_harness_modem_host_parse_rx_pkt() - Parses RX packet on the host
 * @pkt:  Pointer to the packet structure to store parsed information
 * @data: Pointer to the received data descriptor
 *
 * This function parses a received vendor descriptor on the host side. It can
 * distinguish between message and payload descriptors, skipping messages and
 * extracting metadata from payloads.
 *
 * Return: 0 on success, -EAGAIN if the descriptor is a message to be skipped,
 * or -EINVAL for invalid descriptor types.
 */
int noa_harness_modem_host_parse_rx_pkt(struct noa_harness_pkt *pkt, const void *data);

#endif /* __HARNESS_MODEM_DEVICE_H__ */
