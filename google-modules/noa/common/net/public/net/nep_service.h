// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file for NEP service.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifndef NEP_SERVICE_H
#define NEP_SERVICE_H

#ifdef linux
#include <linux/types.h>
#else
#include "pw_log/log.h"
#endif

/// @brief Function pointer type for sending an event to the APC.
///
/// @param id The event ID.
/// @param msg A pointer to the message data.
/// @param msg_len The length of the message data.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*SendEventToApcFromNep)(uint32_t id, void *msg, uint32_t msg_len);

/// @brief Structure to facilitate sending event callback to APC.
struct NepRpcService{
    SendEventToApcFromNep send_event_to_apc_from_nep;
};

// Functions Prototype

/// @brief Get NepRpcService instance.
///
/// @return A reference to the NepRpcService instance.
struct NepRpcService* GetNepService(void);

/// @brief Initialize sending event callback function.
///
/// @param send_event_to_apc_from_nep callback function
void NepRpcServiceCallbackInit(SendEventToApcFromNep send_event_to_apc_from_nep);

#endif /* NEP_SERVICE_H */
