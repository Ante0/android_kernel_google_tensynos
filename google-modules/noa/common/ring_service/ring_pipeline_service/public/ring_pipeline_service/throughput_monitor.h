// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of ring ISR handler component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Stanley Jhu <stanleyjhu@google.com>
 */

#pragma once

#include <cstdint>

#include "osal/osal.h"
#include "pw_chrono/system_timer.h"
#include "pw_sync/binary_semaphore.h"
#include "ring_pipeline_service/ring_data_route.h"

namespace noa::service::ring_service {

struct ThroughputStats {
  uint32_t last_packets = 0;
  uint32_t avg_pps = 0;
  uint32_t max_pps = 0;
};

class ThroughputMonitor {
 public:
  static ThroughputMonitor& Instance();

  void Start();
  void Stop();
  void DumpStatsForShell();
  void EnableLog(bool enable);

 private:
  ThroughputMonitor()
      : timer_([this](auto next) { this->PeriodicTimerHandler(next); }) {}
  ~ThroughputMonitor() = default;

  static void MonitorTask(void* param);
  void PeriodicTimerHandler(pw::chrono::SystemClock::time_point);

  pw::chrono::SystemTimer timer_;

  pw::sync::BinarySemaphore timer_semaphore_;
  driver::osal::OsTaskHandle monitor_task_handle_ = nullptr;
  std::atomic<bool> stop_requested_{false};
  volatile bool enable_log_ = false;

  // Statistics
  ThroughputStats feedthrough_wlan_host_stats_{};
  ThroughputStats feedthrough_modem_host_stats_{};
  ThroughputStats fallback_wlan_stats_{};
  ThroughputStats fallback_modem_stats_{};
  ThroughputStats feedthrough_wlan_device_stats_{};
  ThroughputStats feedthrough_modem_device_stats_{};
  ThroughputStats forward_to_wlan_stats_{};
  ThroughputStats forward_to_modem_stats_{};
};

}  // namespace noa::service::ring_service
