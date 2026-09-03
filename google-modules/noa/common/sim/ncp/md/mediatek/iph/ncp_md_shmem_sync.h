/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC.
 *
 * NCP Modem Shared Memory Synchronization Module (Command Router)
 *
 * This module is a singleton that serves as a command router for all
 * synchronous AP-NCP communication. It decodes doorbell interrupts in a
 * lightweight ISR, defers the actual processing to a work queue, finds the
 * appropriate registered handler for a specific command, and manages the
 * response handshake.
 */

#ifndef __NCP_MD_SHMEM_SYNC_H__
#define __NCP_MD_SHMEM_SYNC_H__

#include <cstdint>
#include <map>

#include "linux_port/interrupt.h"
#include "ncp_modem_data.h"  // For struct noa_md_fw
#include "noa_md_dpa_doorbell.h"
#include "noa_md_shmem_layout.h"
#include "pw_function/function.h"
#include "pw_status/status.h"
#include "pw_sync/mutex.h"
#include "pw_system/work_queue.h"

// Forward declaration from ncp_md_irq.h
struct ncp_md_irq_dpa_isr_data;

namespace noa::driver::modem::ncp {

// Differentiates between generic command channels in the helper function.
enum class GenericChannelType {
  kData,
  kDebug,
};

class NcpShmemSync {
 public:
  // Defines the callback function signatures for different command categories.
  // The context (md_fw) is passed to allow handlers to access firmware state.
  using SwitchCommandHandler = pw::Function<enum noa_md_switch_status(
    struct noa_md_fw*, enum noa_md_switch_command,
    struct noa_md_switch_payload*)>;

  using DataCommandHandler = pw::Function<enum noa_md_shmem_cmd_status(
    struct noa_md_fw*, const struct noa_md_shmem_data_payload*,
    struct noa_md_shmem_data_payload*)>;

  using DebugCommandHandler = pw::Function<enum noa_md_shmem_cmd_status(
    struct noa_md_fw*, const struct noa_md_shmem_debug_payload*,
    struct noa_md_shmem_debug_payload*)>;

  // Defines the callback for high-priority, asynchronous data path interrupts.
  // This is called directly from the ISR context for maximum performance.
  using DataPathInterruptHandler = pw::Function<irqreturn_t(int, void*)>;

  // A structure to pass all necessary dependencies during initialization.
  struct Options {
    struct noa_md_fw* md_fw;
  };

  // Provides access to the singleton instance of this class.
  static NcpShmemSync& Instance();

  // Ensure the class is non-copyable and non-movable to enforce the
  // singleton pattern.
  NcpShmemSync(const NcpShmemSync&) = delete;
  NcpShmemSync& operator=(const NcpShmemSync&) = delete;

  /**
   * @brief Initializes the synchronization module.
   *
   * @param options A structure containing all necessary dependencies,
   * primarily the firmware context.
   * @return pw::OkStatus() on success, or an error status on failure.
   */
  pw::Status Init(const Options& options);

  /**
   * @brief Deinitializes the module and releases its resources.
   */
  void Exit();

  /**
   * @brief Registers a single handler for all switch-related commands.
   *
   * @param handler The function that will be called for any SWITCH_CTRL
   * doorbell.
   */
  void RegisterSwitchCommandHandler(SwitchCommandHandler handler);

  /**
   * @brief Registers a specific handler for a given data sub-command.
   *
   * @param sub_cmd The data sub-command (e.g., IFINDEX_TABLE_UPDATE) to
   * handle.
   * @param handler The function to be called when this sub-command is
   * received.
   */
  void RegisterDataSubCommandHandler(
    enum noa_md_shmem_data_transfer_cmd sub_cmd,
    DataCommandHandler handler);

  /**
   * @brief Registers a specific handler for a given debug sub-command.
   *
   * @param sub_cmd The debug sub-command (e.g., ENABLE) to handle.
   * @param handler The function to be called when this sub-command is
   * received.
   */
  void RegisterDebugSubCommandHandler(enum noa_md_shmem_debug_cmd sub_cmd,
    DebugCommandHandler handler);

  /**
   * @brief Registers callback for high-priority, asynchronous data path
   * interrupts.
   *
   * @param handler The function that will be called for any data path
   * doorbell.
   */
  void RegisterDataPathInterruptHandler(DataPathInterruptHandler handler);

 private:
  NcpShmemSync() = default;
  ~NcpShmemSync() = default;

  /**
   * @brief The master ISR for all synchronous commands from the AP.
   *
   * This function is designed to be extremely lightweight and non-blocking.
   * Its sole responsibility is to capture the doorbell ID and defer the actual
   * processing to a work queue.
   * @param context The context provided by the IRQ driver, expected to be a
   * pointer to `ncp_md_irq_dpa_isr_data`.
   * @return IRQ_HANDLED if the interrupt is claimed.
   */
  static int DoorbellIsrHandler(void* context);

  /**
   * @brief Processes a command that was deferred from the ISR.
   *
   * This function executes in a work queue context, which is safe for sleeping
   * and locking. It acts as the main router, dispatching the command to the
   * appropriate handler based on the doorbell ID.
   * @param doorbell_id The doorbell ID captured by the ISR.
   */
  void ProcessShmemCommand(int doorbell_id);

  /**
   * @brief A private helper to process generic (Data/Debug) commands.
   *
   * This function encapsulates the common logic for handling generic commands,
   * including finding the correct handler from a map, safely invoking it, and
   * managing the response payload with proper locking.
   *
   * @param channel_type  Specifies whether this is a DATA or DEBUG command.
   * @param req_payload   Pointer to the AP->NCP request payload.
   * @param resp_payload  Pointer to the NCP->AP response payload.
   * @param resp_lock     The mutex dedicated to protecting the response
   * payload.
   * @param ack_doorbell  The doorbell ID to ring for acknowledging the
   * command.
   */
  void ProcessGenericCommand(
    GenericChannelType channel_type,
    struct noa_md_shmem_cmd_payload* req_payload,
    struct noa_md_shmem_cmd_payload* resp_payload,
    pw::sync::Mutex& resp_lock,
    enum noa_md_ncp_to_apc_doorbell ack_doorbell);

  // Pointer to the main firmware structure to provide context to handlers.
  struct noa_md_fw* md_fw_ = nullptr;

  // Pointers to the various payload sections within the shared memory layout.
  struct noa_md_switch_payload* switch_payload_ = nullptr;
  struct noa_md_shmem_cmd_payload* ap2ncp_data_payload_ = nullptr;
  struct noa_md_shmem_cmd_payload* ncp2ap_data_payload_ = nullptr;
  struct noa_md_shmem_cmd_payload* ap2ncp_debug_payload_ = nullptr;
  struct noa_md_shmem_cmd_payload* ncp2ap_debug_payload_ = nullptr;

  // This lock protects the handler maps from concurrent access between the
  // work queue (processing commands) and other tasks (registering handlers).
  pw::sync::Mutex registration_lock_;

  // These locks protect their corresponding NCP->AP response payloads from
  // concurrent writes within the NCP.
  pw::sync::Mutex ncp2ap_data_lock_;
  pw::sync::Mutex ncp2ap_debug_lock_;

  // A single, monolithic handler for all switch-related commands.
  SwitchCommandHandler switch_cmd_handler_;

  // Maps that associate a specific sub-command with its registered handler.
  std::map<enum noa_md_shmem_data_transfer_cmd, DataCommandHandler>
      data_cmd_handlers_;
  std::map<enum noa_md_shmem_debug_cmd, DebugCommandHandler>
      debug_cmd_handlers_;
  DataPathInterruptHandler data_path_interrupt_handler_;
};

}  // namespace noa::driver::modem::ncp

#endif /* __NCP_MD_SHMEM_SYNC_H__ */
