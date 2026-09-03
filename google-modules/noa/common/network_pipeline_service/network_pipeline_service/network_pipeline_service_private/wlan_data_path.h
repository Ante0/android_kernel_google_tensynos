/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The Wlan Data Path Implementation.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_SERVICE_WLAN_DATA_PATH_H
#define NOA_NETWORK_PIPELINE_SERVICE_WLAN_DATA_PATH_H

#ifdef linux
#include <linux/types.h>
#else /* linux */
#include <cstdint>
#endif /* linux */

int32_t NepWlanBufferPoolSetup(void);

/**
 * @brief Sets up the WLAN device-to-host data path.
 *
 * This function initializes and configures the data path for network traffic
 * flowing from the WLAN device to the host system. It involves setting up
 * various stages, such as caching, tethering, and routing, to manage and
 * process incoming data packets.
 *
 * @return 0 on success, or a negative error code if an error occurs during setup.
 */
int32_t NepWlanDeviceToHostPathSetup(void);

/**
 * @brief Sets up the WLAN host-to-device data path.
 *
 * This function initializes and configures the data path for network traffic
 * flowing from the host system to the WLAN device. It establishes the necessary
 * stages, including caching and routing, to manage outgoing data packets.
 *
 * @return 0 on success, or a negative error code if an error occurs during setup.
 */
int32_t NepWlanHostToDevicePathSetup(void);

#endif /* NOA_NETWORK_PIPELINE_SERVICE_WLAN_DATA_PATH_H */
