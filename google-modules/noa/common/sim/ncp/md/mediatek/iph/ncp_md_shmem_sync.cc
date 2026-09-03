/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC.
 *
 * NCP Modem Shared Memory Synchronization Module (Command Router)
 * Implementation
 */

#include "ncp_md_shmem_sync.h"

#include "linux_port/dma_mapping.h"
#include "pw_log/log.h"

#include "ncp_md_irq.h"
#include "ncp_modem_data.h"  // For struct noa_md_fw
#include "sys_common.h"

namespace noa::driver::modem::ncp {

NcpShmemSync& NcpShmemSync::Instance() {
  static NcpShmemSync instance;
  return instance;
}

pw::Status NcpShmemSync::Init(const Options& options) {
  if (!options.md_fw || !options.md_fw->shared_mem_info.addr) {
    PW_LOG_ERROR("Invalid md_fw or shmem address. Cannot init NcpShmemSync.");
    return pw::Status::InvalidArgument();
  }

  md_fw_ = options.md_fw;
  auto* shmem_base =
    reinterpret_cast<struct noa_md_shmem_layout*>(md_fw_->shared_mem_info.addr);

  // Initialize pointers to the different payload areas in shared memory.
  switch_payload_ = &shmem_base->switch_payload;
  ap2ncp_data_payload_ = &shmem_base->ap2ncp_data_payload;
  ncp2ap_data_payload_ = &shmem_base->ncp2ap_data_payload;
  ap2ncp_debug_payload_ = &shmem_base->ap2ncp_debug_payload;
  ncp2ap_debug_payload_ = &shmem_base->ncp2ap_debug_payload;

  int ret = ncp_md_irq_dpa_init(DoorbellIsrHandler, md_fw_);
  if (ret != 0) {
    PW_LOG_ERROR("Failed to register DPA IRQ handler, ret=%d", ret);
    return pw::Status::Internal();
  }
  return pw::OkStatus();
}

void NcpShmemSync::Exit() {
  // Unregister the DPA IRQ handler that was set up in Init().
  ncp_md_irq_dpa_exit();

  // Clear internal state to prevent stale pointers.
  md_fw_ = nullptr;
  switch_cmd_handler_ = nullptr;
  data_cmd_handlers_.clear();
  debug_cmd_handlers_.clear();
  data_path_interrupt_handler_ = nullptr;
}

void NcpShmemSync::RegisterSwitchCommandHandler(SwitchCommandHandler handler) {
  registration_lock_.lock();
  switch_cmd_handler_ = std::move(handler);
  registration_lock_.unlock();
}

void NcpShmemSync::RegisterDataSubCommandHandler(
  enum noa_md_shmem_data_transfer_cmd sub_cmd, DataCommandHandler handler) {
  registration_lock_.lock();
  data_cmd_handlers_[sub_cmd] = std::move(handler);
  registration_lock_.unlock();
}

void NcpShmemSync::RegisterDebugSubCommandHandler(
  enum noa_md_shmem_debug_cmd sub_cmd, DebugCommandHandler handler) {
  registration_lock_.lock();
  debug_cmd_handlers_[sub_cmd] = std::move(handler);
  registration_lock_.unlock();
}

void NcpShmemSync::RegisterDataPathInterruptHandler(
  DataPathInterruptHandler handler) {
  data_path_interrupt_handler_ = std::move(handler);
}

int NcpShmemSync::DoorbellIsrHandler(void* context) {
  auto* isr_data = static_cast<struct ncp_md_irq_dpa_isr_data*>(context);
  if (!isr_data) {
    PW_LOG_CRITICAL("NcpShmemSync ISR context is NULL");
    return IRQ_NONE;
  }

  auto& self = NcpShmemSync::Instance();
  const int doorbell_id = isr_data->id;

  if (doorbell_id >= kNoaModemRingTxDrb0 &&
      doorbell_id < kNoaModemApcToNcpRingMax) {
    if (self.data_path_interrupt_handler_) {
      // Directly invoke the registered high-performance ISR.
      return self.data_path_interrupt_handler_(doorbell_id, isr_data->data);
    }
    return IRQ_NONE;
  }

  pw::system::GetWorkQueue().PushWork(
    [doorbell_id]() {
      NcpShmemSync::Instance().ProcessShmemCommand(doorbell_id);
    }
  );

  return IRQ_HANDLED;
}

void NcpShmemSync::ProcessGenericCommand(
  GenericChannelType channel_type,
  struct noa_md_shmem_cmd_payload* req_payload,
  struct noa_md_shmem_cmd_payload* resp_payload,
  pw::sync::Mutex& resp_lock,
  enum noa_md_ncp_to_apc_doorbell ack_doorbell) {
  enum noa_md_shmem_cmd_status status = SHMEM_CMD_STATUS_FAILED;
  uint32_t sub_cmd_int;

  if (channel_type == GenericChannelType::kData) {
    auto sub_cmd = static_cast<enum noa_md_shmem_data_transfer_cmd>(
      req_payload->payload.data_payload.sub_cmd);
    sub_cmd_int = static_cast<uint32_t>(sub_cmd);

    registration_lock_.lock();
    auto it = data_cmd_handlers_.find(sub_cmd);
    if (it != data_cmd_handlers_.end() && it->second) {
      status = it->second(md_fw_, &req_payload->payload.data_payload,
                          &resp_payload->payload.data_payload);
    } else {
      PW_LOG_ERROR("No handler for data sub-command: %u", sub_cmd_int);
    }
    registration_lock_.unlock();

  } else {  // GenericChannelType::kDebug
    auto sub_cmd = static_cast<enum noa_md_shmem_debug_cmd>(
      req_payload->payload.debug_payload.sub_cmd);
    sub_cmd_int = static_cast<uint32_t>(sub_cmd);

    registration_lock_.lock();
    auto it = debug_cmd_handlers_.find(sub_cmd);
    if (it != debug_cmd_handlers_.end() && it->second) {
      status = it->second(md_fw_, &req_payload->payload.debug_payload,
                          &resp_payload->payload.debug_payload);
    } else {
      PW_LOG_ERROR("No handler for debug sub-command: %u", sub_cmd_int);
    }
    registration_lock_.unlock();

  }

  resp_lock.lock();
  resp_payload->status = status;
  FlushDCache(resp_payload, sizeof(*resp_payload));
  resp_lock.unlock();

  ncp_md_irq_dpa_notify_apc(ack_doorbell);
}

void NcpShmemSync::ProcessShmemCommand(int doorbell_id) {
  switch (doorbell_id) {
    case NOA_MD_APC2NCP_SWITCH_CTRL: {
      InvalidateDCache(switch_payload_, sizeof(*switch_payload_));

      enum noa_md_switch_status status = SWITCH_STATUS_FAILED;
      SwitchCommandHandler handler_copy;

      registration_lock_.lock();
      if (switch_cmd_handler_) {
        status = switch_cmd_handler_(
          md_fw_,
          static_cast<enum noa_md_switch_command>(switch_payload_->command),
          switch_payload_);
      } else {
        PW_LOG_ERROR("No handler registered for SWITCH_CTRL command!");
      }
      registration_lock_.unlock();

      switch_payload_->status = status;
      FlushDCache(switch_payload_, sizeof(*switch_payload_));

      ncp_md_irq_dpa_notify_apc(NOA_MD_NCP2APC_SWITCH_CTRL_EVENT);
      break;
    }

    case NOA_MD_APC2NCP_SHMEM_DATA_NOTIFY: {
      InvalidateDCache(ap2ncp_data_payload_, sizeof(*ap2ncp_data_payload_));
      ProcessGenericCommand(GenericChannelType::kData, ap2ncp_data_payload_,
        ncp2ap_data_payload_, ncp2ap_data_lock_,
        NOA_MD_NCP2APC_SHMEM_DATA_ACK);
      break;
    }

    case NOA_MD_APC2NCP_SHMEM_DEBUG_NOTIFY: {
      InvalidateDCache(ap2ncp_debug_payload_, sizeof(*ap2ncp_debug_payload_));
      ProcessGenericCommand(GenericChannelType::kDebug, ap2ncp_debug_payload_,
        ncp2ap_debug_payload_, ncp2ap_debug_lock_,
        NOA_MD_NCP2APC_SHMEM_DEBUG_ACK);
      break;
    }

    default:
      PW_LOG_WARN(
          "ProcessShmemCommand received unhandled doorbell_id: %d. "
          "This might be an async notification for another handler.",
          doorbell_id);
      break;
  }
}

}  // namespace noa::driver::modem::ncp
