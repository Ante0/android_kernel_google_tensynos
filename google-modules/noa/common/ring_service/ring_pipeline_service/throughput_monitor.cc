// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of ring ISR handler component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Stanley Jhu <stanleyjhu@google.com>
 */
#define PW_LOG_MODULE_NAME "rs-throughput"

#include "ring_pipeline_service/throughput_monitor.h"

#include <atomic>
#include <chrono>

#include "osal/osal.h"
#include "pw_log/log.h"
#include "pw_thread/sleep.h"
#include "ring_pipeline_service/ring_data_route.h"

namespace noa::service::ring_service {

static constexpr uint32_t kMonitorPeriodMs = 1000;
// EMA (Exponential Moving Average) weight for the current value (1/8).
static constexpr int kEmaWeightFactor = 8;
// Timeout for the monitor task semaphore, slightly longer than the period.
static constexpr uint32_t kSemaphoreTimeoutMs = kMonitorPeriodMs + 100;

ThroughputMonitor& ThroughputMonitor::Instance() {
  static ThroughputMonitor instance;
  return instance;
}

void ThroughputMonitor::PeriodicTimerHandler(pw::chrono::SystemClock::time_point) {
  timer_semaphore_.release();
  timer_.InvokeAfter(std::chrono::milliseconds(kMonitorPeriodMs));
}

static void UpdatePathStats(ThroughputStats& path_stats,
                            uint32_t current_packets) {
  uint32_t packets_in_period = current_packets - path_stats.last_packets;
  path_stats.last_packets = current_packets;

  // This calculates an exponential moving average (EMA) for the packets per
  // second (pps) using integer arithmetic. It's a computationally cheap way
  // to get a smoothed average that gives more weight to recent measurements.
  //
  // The formula is equivalent to:
  //   new_avg = (old_avg * (7/8)) + (current_value * (1/8))
  //
  // This gives 87.5% weight to the historical average and 12.5% to the new
  // measurement, smoothing out temporary spikes in throughput.
  path_stats.avg_pps = (path_stats.avg_pps * (kEmaWeightFactor - 1) +
                        packets_in_period) / kEmaWeightFactor;
  if (packets_in_period > path_stats.max_pps) {
    path_stats.max_pps = packets_in_period;
  }
}

void ThroughputMonitor::MonitorTask(void* /*param*/) {
  ThroughputMonitor& monitor = Instance();
  RingDataRouteStats* stats = RingRouteGetStats();

  for (;;) {
    (void)monitor.timer_semaphore_.try_acquire_for(
        std::chrono::milliseconds(kSemaphoreTimeoutMs));

    if (monitor.stop_requested_.load(std::memory_order_relaxed)) {
      break;
    }

    UpdatePathStats(monitor.feedthrough_wlan_host_stats_,
                    stats->feedthrough_to_wlan_host);
    UpdatePathStats(monitor.feedthrough_modem_host_stats_,
                    stats->feedthrough_to_modem_host);
    UpdatePathStats(monitor.fallback_wlan_stats_,
                    stats->fallback_to_wlan_host);
    UpdatePathStats(monitor.fallback_modem_stats_,
                    stats->fallback_to_modem_host);
    UpdatePathStats(monitor.feedthrough_wlan_device_stats_,
                    stats->feedthrough_to_wlan_device);
    UpdatePathStats(monitor.feedthrough_modem_device_stats_,
                    stats->feedthrough_to_modem_device);
    UpdatePathStats(monitor.forward_to_wlan_stats_, stats->forward_to_wlan);
    UpdatePathStats(monitor.forward_to_modem_stats_, stats->forward_to_modem);

    if (monitor.enable_log_) {
      PW_LOG_DEBUG("RS Tput (Avg pps):");
      PW_LOG_DEBUG("  FT WLAN H:%-5" PRIu32 " FT WLAN D:%-5" PRIu32,
                  monitor.feedthrough_wlan_host_stats_.avg_pps,
                  monitor.feedthrough_wlan_device_stats_.avg_pps);
      PW_LOG_DEBUG("  FT MD   H:%-5" PRIu32 " FT MD   D:%-5" PRIu32,
                  monitor.feedthrough_modem_host_stats_.avg_pps,
                  monitor.feedthrough_modem_device_stats_.avg_pps);
      PW_LOG_DEBUG("  FB WLAN  :%-5" PRIu32 " FB MD    :%-5" PRIu32,
                  monitor.fallback_wlan_stats_.avg_pps,
                  monitor.fallback_modem_stats_.avg_pps);
      PW_LOG_DEBUG("  FW WLAN  :%-5" PRIu32 " FW MD    :%-5" PRIu32,
                  monitor.forward_to_wlan_stats_.avg_pps,
                  monitor.forward_to_modem_stats_.avg_pps);
    }
  }

  monitor.timer_.Cancel();
  monitor.monitor_task_handle_ = nullptr;
  driver::osal::OsDeleteCurrentTask();
}

void ThroughputMonitor::Start() {
  if (monitor_task_handle_) {
    PW_LOG_WARN("Throughput monitor is already running.");
    return;
  }

  enable_log_ = false;
  stop_requested_.store(false, std::memory_order_relaxed);
  if (!driver::osal::OsCreateTaskLowestPriority(
          MonitorTask, "RsThroughputMon", this, &monitor_task_handle_)) {
    PW_LOG_CRITICAL("Failed to create throughput monitor task.");
    monitor_task_handle_ = nullptr;
    return;
  }

  timer_.InvokeAfter(std::chrono::milliseconds(kMonitorPeriodMs));
}

void ThroughputMonitor::Stop() {
  if (!monitor_task_handle_) {
    PW_LOG_WARN("Throughput monitor is not running.");
    return;
  }
  stop_requested_.store(true, std::memory_order_relaxed);
  while (monitor_task_handle_ != nullptr) {
    pw::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void ThroughputMonitor::DumpStatsForShell() {
  if (!monitor_task_handle_) {
    PW_LOG_WARN("Throughput monitor is not running.");
    return;
  }

  PW_LOG_INFO("Ring Service Throughput:");
  const char* format = "    %-10s: Avg %4" PRIu32 " pps, Max %4" PRIu32 " pps";

  PW_LOG_INFO("  Feedthrough (WLAN)");
  PW_LOG_INFO(format, "To Host", feedthrough_wlan_host_stats_.avg_pps,
              feedthrough_wlan_host_stats_.max_pps);
  PW_LOG_INFO(format, "To Device", feedthrough_wlan_device_stats_.avg_pps,
              feedthrough_wlan_device_stats_.max_pps);

  PW_LOG_INFO("  Feedthrough (Modem)");
  PW_LOG_INFO(format, "To Host", feedthrough_modem_host_stats_.avg_pps,
              feedthrough_modem_host_stats_.max_pps);
  PW_LOG_INFO(format, "To Device", feedthrough_modem_device_stats_.avg_pps,
              feedthrough_modem_device_stats_.max_pps);

  PW_LOG_INFO("  Fallback");
  PW_LOG_INFO(format, "WLAN", fallback_wlan_stats_.avg_pps,
              fallback_wlan_stats_.max_pps);
  PW_LOG_INFO(format, "Modem", fallback_modem_stats_.avg_pps,
              fallback_modem_stats_.max_pps);

  PW_LOG_INFO("  Forward");
  PW_LOG_INFO(format, "To WLAN", forward_to_wlan_stats_.avg_pps,
              forward_to_wlan_stats_.max_pps);
  PW_LOG_INFO(format, "To Modem", forward_to_modem_stats_.avg_pps,
              forward_to_modem_stats_.max_pps);
}

void ThroughputMonitor::EnableLog(bool enable) {
  enable_log_ = enable;
}

}  // namespace noa::service::ring_service