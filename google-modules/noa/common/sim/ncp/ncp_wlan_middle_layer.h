// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WLAN Middle Layer
 *
 * Copyright (c) 2024 Google LLC.
 *
 * This file provides the interface for the NCP WLAN Middle Layer, which
 * facilitates communication and coordination between the NCP and the NCP
 * WLAN firmware.
 *
 * Author: Wade Shih <wadeshih@google.com>
 */

#ifndef __NCP_WLAN_MIDDLE_LAYER_
#define __NCP_WLAN_MIDDLE_LAYER_

/**
 * @brief Initializes the NCP WLAN Middle Layer platform.
 *
 * This function performs necessary initialization for the NCP WLAN
 * Middle Layer, including resource allocation and setup.
 *
 * @return 0 on success, a negative error code otherwise.
 */
extern int ncp_wlan_ml_platform_init(void);

/**
 * @brief Deinitializes the NCP WLAN Middle Layer platform.
 *
 * This function releases resources and performs cleanup for the NCP WLAN
 * Middle Layer.
 */
extern void ncp_wlan_ml_platform_deinit(void);

/**
 * @brief  Handle a shell command from the NCP simulator.
 *
 * @param[in] cmd The shell command string.
 * @param[in] argv_len The number of arguments in the argv array.
 * @param[in] argv The argument vector for the shell command.
 */
extern void ncp_wlan_ml_shell_cmd_handler(const char *cmd, int argv_len, const char *argv);

/**
 * @brief Get the WLAN client device.
 *
 * This function returns a pointer to the WLAN client device structure.
 *
 * @return Pointer to the WLAN client device structure.
 */
extern struct device *ncp_wlan_get_wlan_client_device(void);

#endif /* __NCP_WLAN_MIDDLE_LAYER_ */
