/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The Header of Modem Data Path.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_SERVICE_MODEM_PATH_H
#define NOA_NETWORK_PIPELINE_SERVICE_MODEM_PATH_H

#ifdef linux
#include <linux/types.h>
#else /* linux */
#include <cstdint>
#endif /* linux */

int32_t NepModemBufferPoolSetup(void);

/**
 * @brief Setup the Modem device-to-host data path.
 *
 * This function initializes and configures the data path for network traffic flowing
 * from the Modem device to the host system. It sets up stages like caching,
 * tethering, and routing to process incoming data packets.
 *
 * @return Returns 0 on success, or a negative error code if setup fails.
 */
int32_t NepModemDeviceToHostPathSetup(void);

/**
 * @brief Setup the Modem host-to-device data path.
 *
 * This function initializes and configures the data path for network traffic flowing
 * from the host system to the Modem device. It establishes the necessary stages,
 * including caching and routing, to manage outgoing data packets.
 *
 * @return Returns 0 on success, or a negative error code if setup fails.
 */
int32_t NepModemHostToDevicePathSetup(void);

#endif /* NOA_NETWORK_PIPELINE_SERVICE_MODEM_PATH_H */
