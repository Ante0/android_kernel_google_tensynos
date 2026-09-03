#include "wlan_dp.h"

#include "sys_if/mailbox/sys_if_mailbox.h"
#include "sys_if/memory/sys_if_memory.h"
#include "wlan_log/wlan_log.h"
#include "wlan_cast.h"
#include "noa_desc.h"
#include "wlan_service_rpc_protocol.h"
#include "wlan_debug_controller/wlan_packet_sniffer/wlan_debug_packet_sniffer.h"
#include "common/compiler.h"

#define UPDATE_POLLING_MASK(current_mask, original_mask, handled_mask)                             \
	(((current_mask) & (~(original_mask))) | (handled_mask))
#define UNMASK_POLLING_BIT(mask, id) ((mask) & (~(1UL << id)))
#ifndef WLAN_PKT_PAD
#define WLAN_PKT_PAD (60U)
#endif

#define WLAN_DP_STATS_MUL_INC(dp, name, cnt) (dp->dp_stats.name += cnt)
#define WLAN_DP_STATS_INC(dp, name) WLAN_DP_STATS_MUL_INC(dp, name, 1)
#define WLAN_DP_STATS_RX_INC(dp) WLAN_DP_STATS_INC(dp, rx)
#define WLAN_DP_STATS_RX_FORWARD_INC(dp) WLAN_DP_STATS_INC(dp, rx_forward)
#define WLAN_DP_STATS_RX_ERR_INC(dp) WLAN_DP_STATS_INC(dp, rx_err)
#define WLAN_DP_STATS_TX_INC(dp) WLAN_DP_STATS_INC(dp, tx)
#define WLAN_DP_STATS_TX_FORWARD_INC(dp) WLAN_DP_STATS_INC(dp, tx_forward)
#define WLAN_DP_STATS_TX_ERR_INC(dp) WLAN_DP_STATS_INC(dp, tx_err)
#define WLAN_DP_STATS_TXCPL_INC(dp) WLAN_DP_STATS_INC(dp, tx_cpl)
#define WLAN_DP_STATS_TXCPL_ERR_INC(dp) WLAN_DP_STATS_INC(dp, tx_cpl_err)
#define WLAN_DP_STATS_RX_REPLENISH_INC(dp) WLAN_DP_STATS_INC(dp, rx_replenish)
#define WLAN_DP_STATS_RX_REPLENISH_ERR_INC(dp) WLAN_DP_STATS_INC(dp, rx_replenish_err)
#define WLAN_DP_STATS_RXBM_SYNC_INC(dp) WLAN_DP_STATS_INC(dp, rxbm_sync)
#define WLAN_DP_STATS_TXBM_SYNC_INC(dp) WLAN_DP_STATS_INC(dp, txbm_sync)
#define WLAN_DP_STATS_TXBM_SYNC_MUL_INC(dp, cnt) WLAN_DP_STATS_MUL_INC(dp, txbm_sync, cnt)
#define WLAN_DP_STATS_NEP_FW_INPUT_INC(dp) WLAN_DP_STATS_INC(dp, nep_fw_input)
#define WLAN_DP_STATS_DEV_RX_INC(dp) WLAN_DP_STATS_INC(dp, dev_rx)
#define WLAN_DP_STATS_NEP_FW_OUTPUT_INC(dp, id) WLAN_DP_STATS_INC(dp, nep_fw_output[id])
#define WLAN_DP_STATS_DEV_TX_INC(dp, id) WLAN_DP_STATS_INC(dp, dev_tx[id])
#define WLAN_DP_STATS_DEV_TXCPL_INC(dp) WLAN_DP_STATS_INC(dp, dev_txcpl)
#define WLAN_DP_STATS_DEV_RX_REPLENISH_INC(dp) WLAN_DP_STATS_INC(dp, dev_rx_replenish)

#if IS_ENABLED(CONFIG_NOA_WLAN_TX_PKTID_AUDIT)
static_assert(CONFIG_NOA_WLAN_MAX_TX_PKTID != 0);
static SEC_EXRAM_DATA uint8_t tx_pktid_audit[CONFIG_NOA_WLAN_MAX_TX_PKTID] = { 0 };
#endif // IS_ENABLED(CONFIG_NOA_WLAN_TX_PKTID_AUDIT)

static uint32_t FindGcd(uint32_t num1, uint32_t num2)
{
	uint32_t temp;
	while (num2) {
		num1 %= num2;
		temp = num1;
		num1 = num2;
		num2 = temp;
	}
	return num1;
}

static uint32_t WlanWorkerGetBudget(WlanWorker *const worker)
{
	uint32_t budget = WLAN_WORKER_UNDEFINED_PRIORITY_BUDGET;

	switch (worker->priority) {
	case kWlanWorkerPriorityHigh:
		budget = WLAN_WORKER_HIGH_PRIORITY_BUDGET;
		break;
	case kWlanWorkerPriorityMid:
		budget = WLAN_WORKER_MID_PRIORITY_BUDGET;
		break;
	case kWlanWorkerPriorityLow:
		budget = WLAN_WORKER_LOW_PRIORITY_BUDGET;
		break;
	default:
		WLAN_LOG_WARN(Dp, "%s(): Undefined priority worker.", __func__);
	}

	return budget;
};

static void WlanWorkerTaskWrapper(unsigned long context) // NO_CPP_INT_CHECK
{
	WlanWorker *worker = WLAN_REINTERPRET_CAST(WlanWorker *, context);
	uint32_t budget = WlanWorkerGetBudget(worker);

	if (worker->work && budget) {
		worker->work(worker, budget);
	}
};

static void WlanWorkerInit(WlanWorker *const worker, WlanWorkerPriority priority, WorkFunction work)
{
	worker->priority = priority;
	worker->work = work;
	tasklet_init(&worker->task, WlanWorkerTaskWrapper,
		     WLAN_REINTERPRET_CAST(uintptr_t, worker));
}

static void WlanWorkerDeinit(WlanWorker *const worker)
{
	tasklet_kill(&worker->task);
}

static void WlanDpSubTaskStart(WlanDp *wlan_dp)
{
	wlan_dp->delayed_works.running_work_num++;
	if (wlan_dp->delayed_works.running == false) {
		wlan_dp->delayed_works.running = true;
		SysIfScheduleDelayedWork(&wlan_dp->delayed_works.shared_worker,
					 wlan_dp->delayed_works.interval_msec);
	}
}

static void WlanDpSubTaskStop(WlanDp *wlan_dp)
{
	wlan_dp->delayed_works.running_work_num--;
	if (wlan_dp->delayed_works.running_work_num == 0) {
		wlan_dp->delayed_works.running = false;
		SysIfCancelDelayedWork(&wlan_dp->delayed_works.shared_worker);
	}
}

static void WlanDpAcquireWakeLock(WlanDp *wlan_dp)
{
	atomic_add(1, &wlan_dp->idle_detector.wake_lock);
	wlan_dp->idle_detector.idle_count = 0;
}

static void WlanDpReleaseWakeLock(WlanDp *wlan_dp)
{
	atomic_sub(1, &wlan_dp->idle_detector.wake_lock);
}

static void WlanDpIdleDetectorTask(void *ctx)
{
	WlanDp *dp = WLAN_REINTERPRET_CAST(WlanDp *, ctx);
	WlanDpIdleDetector *idle_detector = &dp->idle_detector;

	// Check for TX idle condition.
	if (atomic_read(&idle_detector->wake_lock) == 0) {
		idle_detector->idle_count++;
		// If the TX idle count exceeds the threshold, trigger the
		// TX idle detected callback and reset the counter.
		if (idle_detector->idle_count > WLAN_IDLE_COUNT_THRESHOLD) {
			if (idle_detector->idle_detected_callback) {
				idle_detector->idle_detected_callback(
					idle_detector->idle_detected_ctx);
			}
			idle_detector->idle_count = 0;
		}
	} else {
		// Reset the idle counter if not idle.
		idle_detector->idle_count = 0;
	}
}

void WlanDpIdleDetectionStart(WlanDp *wlan_dp)
{
	wlan_dp->idle_detector.param.running = true;
	wlan_dp->idle_detector.param.remaining_time = wlan_dp->idle_detector.param.interval_msec;
	WlanDpSubTaskStart(wlan_dp);
}

void WlanDpIdleDetectionStop(WlanDp *wlan_dp)
{
	wlan_dp->idle_detector.param.running = false;
	wlan_dp->idle_detector.param.remaining_time = 0;
	WlanDpSubTaskStop(wlan_dp);
}

static void WlanDpThroughputMonitorCallback(void *ctx)
{
	WlanDp *dp = WLAN_REINTERPRET_CAST(WlanDp *, ctx);
	if (dp->mode != kWlanDpRaProxyForcedFwdToNetEngineMode) {
		if (dp->ra_proxy_flags.ra_proxy_enabled &&
		    dp->ra_proxy_flags.ra_proxy_high_tput_flag) {
			dp->ra_proxy_flags.last_mode = dp->mode;
			WlanDpSetMode(dp, kWlanDpRaProxyForcedFwdToNetEngineMode);
			WLAN_LOG_DEBUG(Dp, "Set mode to kWlanDpRaProxyForcedFwdToNetEngineMode");
		}
	} else {
		// turn off ra proxy mode if the throuput is lower than the threshold
		if (!dp->ra_proxy_flags.ra_proxy_enabled ||
		    !dp->ra_proxy_flags.ra_proxy_high_tput_flag) {
			WlanDpSetMode(dp, dp->ra_proxy_flags.last_mode);
			WLAN_LOG_DEBUG(Dp, "Set mode to kWlanDpNormalMode");
		}
	}
}

static void WlanDpThroughputMonitorTask(void *ctx)
{
	WlanDp *dp = WLAN_REINTERPRET_CAST(WlanDp *, ctx);
	WlanDpThroughputMonitor *throughput_monitor = &dp->throughput_monitor;
	uint32_t scaled_current_rx_bps;

	// calculate throughput
	scaled_current_rx_bps =
		throughput_monitor->rx_byte * 1000 / dp->delayed_works.interval_msec;
	throughput_monitor->rx_bps =
		(throughput_monitor->rx_bps * 3 + scaled_current_rx_bps * 5) >> 3;
	WLAN_LOG_DEBUG(Dp, "%s(): cur_rx_byte: %u, scaled_rx_bps: %u, rx_bps: %u", __func__,
		       throughput_monitor->rx_byte, scaled_current_rx_bps,
		       throughput_monitor->rx_bps);

	if (throughput_monitor->rx_bps > WLAN_PPF_HIGH_TPUT_THRESHOLD) {
		dp->ra_proxy_flags.ra_proxy_high_tput_flag = true;
	} else {
		dp->ra_proxy_flags.ra_proxy_high_tput_flag = false;
	}

	if (throughput_monitor->throughput_monitor_callback) {
		throughput_monitor->throughput_monitor_callback(
			throughput_monitor->throughput_monitor_ctx);
	}

	// clear the dp_stats
	throughput_monitor->rx_byte = 0;
}

void WlanDpThroughputMonitorStart(WlanDp *wlan_dp)
{
	wlan_dp->throughput_monitor.param.running = true;
	wlan_dp->throughput_monitor.param.remaining_time =
		wlan_dp->throughput_monitor.param.interval_msec;
	WlanDpSubTaskStart(wlan_dp);
}

void WlanDpThroughputMonitorStop(WlanDp *wlan_dp)
{
	wlan_dp->throughput_monitor.param.running = false;
	wlan_dp->throughput_monitor.param.remaining_time = 0;
	WlanDpSubTaskStop(wlan_dp);
}

static void WlanDpDelayedWorkTask(void *ctx)
{
	int i = 0;
	WlanDp *dp = WLAN_REINTERPRET_CAST(WlanDp *, ctx);
	WlanDpDelayedWorkParam **work_params = dp->delayed_works.work_params;
	WlanDpDelayedWorkParam *cur_param = NULL;

	for (i = 0; i < dp->delayed_works.work_num; i++) {
		cur_param = work_params[i];
		cur_param->remaining_time -= dp->delayed_works.interval_msec;
		if (cur_param->remaining_time == 0 && cur_param->running &&
		    cur_param->delayed_work_task) {
			cur_param->delayed_work_task(dp);
			cur_param->remaining_time = cur_param->interval_msec;
		}
	}

	if (dp->delayed_works.running_work_num > 0) {
		SysIfScheduleDelayedWork(&dp->delayed_works.shared_worker,
					 dp->delayed_works.interval_msec);
	}
}

static bool WlanDpAreAllOutputRingsEmpty(WlanDp *wlan_dp)
{
	uint32_t ring_id;
	WlanRing *ring;
	uint32_t rxcmpl_ring_num =
		WlanRingManagerGetRingNum(wlan_dp->ring_manager, kNepRxCmplRingGroup);

	for (ring_id = 0; ring_id < rxcmpl_ring_num; ring_id++) {
		if (wlan_dp->mode == kWlanDpDirectApcMode) {
			if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcDirectTxPostRingGroup,
						   ring_id, &ring) != 0) {
				continue;
			}
		} else {
			if (WlanRingManagerGetRing(wlan_dp->ring_manager, kNepRxCmplRingGroup,
						   ring_id, &ring) != 0) {
				continue;
			}
		}
		if (WlanRingGetReadCount(ring) != 0) {
			return false;
		}
	}
	return true;
}

void WlanDpDelayedWorkStop(WlanDp *wlan_dp)
{
	int i = 0;
	for (i = 0; i < wlan_dp->delayed_works.work_num; i++) {
		wlan_dp->delayed_works.work_params[i]->running = false;
		wlan_dp->delayed_works.work_params[i]->remaining_time = 0;
	}

	wlan_dp->delayed_works.running = false;
}

int32_t WlanDpDrainOutputRingAsync(WlanDp *wlan_dp, uint32_t timeout_ms)
{
	uintptr_t lock_flags = 0;
	WlanWorker *worker = &wlan_dp->ring_svc_dp_worker;
	int i;

	if (WlanDpAreAllOutputRingsEmpty(wlan_dp)) {
		return 0;
	}

	spin_lock_irqsave(&worker->mask_lock, lock_flags);
	for (i = 0; i < wlan_dp->num_ring_svc_irq; i++) {
		worker->intr_ctx_polling_mask |= (1U << i);
	}
	spin_unlock_irqrestore(&worker->mask_lock, lock_flags);

	wlan_dp->is_draining = true;
	// Ensure worker runs even if no interrupt came in
	tasklet_schedule(&worker->task);

	// TODO: Use wait_for_completion_timeout when supported.
	wait_for_completion(&wlan_dp->drain_completion);

	return 0;
}

void WlanDpTriggerDataPathPoll(WlanDp *wlan_dp)
{
	WlanWorker *worker = &wlan_dp->wlan_dev_dp_worker;
	uintptr_t lock_flags = 0;
	int i;

	spin_lock_irqsave(&worker->mask_lock, lock_flags);
	for (i = 0; i < wlan_dp->num_wlan_dev_irq; i++) {
		struct WlanIntrContext *intr_ctx = &wlan_dp->wlan_dev_intr_ctx_group[i];

		if (intr_ctx->rx_data_ring_polling_mask || intr_ctx->tx_cpl_ring_polling_mask) {
			worker->intr_ctx_polling_mask |= (1U << intr_ctx->intr_id);
		}
	}
	spin_unlock_irqrestore(&worker->mask_lock, lock_flags);

	if (worker->intr_ctx_polling_mask) {
		WLAN_LOG_INFO(Dp, "Proactively scheduling DP worker for RX/TXCPL");
		tasklet_schedule(&worker->task);
	}
}

static void WlanDpAddDelayedWork(WlanDp *const wlan_dp, WlanDpDelayedWorkParam *task_param)
{
	uint8_t idx = wlan_dp->delayed_works.work_num;

	if (idx == 0) {
		wlan_dp->delayed_works.interval_msec = task_param->interval_msec;
	} else {
		wlan_dp->delayed_works.interval_msec =
			FindGcd(wlan_dp->delayed_works.interval_msec, task_param->interval_msec);
	}

	wlan_dp->delayed_works.work_params[idx] = task_param;
	wlan_dp->delayed_works.work_num++;
}

int32_t WlanDpInit(WlanDp *const wlan_dp, const WlanDpInitParams *const params)
{
	uint16_t i;

	if (params->num_ring_svc_irq > MAX_NUM_RING_SVC_IRQ ||
	    params->num_wlan_dev_irq > MAX_NUM_WLAN_DEV_IRQ) {
		return -EINVAL;
	}

	memset(wlan_dp, 0, sizeof(WlanDp));
	wlan_dp->mode = params->mode;
	wlan_dp->state = kWlanDpStateStop;
	wlan_dp->ring_manager = params->ring_manager;
	wlan_dp->wdev_if = params->wdev_if;
	wlan_dp->nep_tx_buffer_pool = params->nep_tx_buffer_pool;
	wlan_dp->ext_svc = params->ext_svc;
	wlan_dp->sta_table = params->sta_table;
	wlan_dp->flow_id_table = params->flow_id_table;
	wlan_dp->tx_prepare_callback = params->tx_prepare_callback;
	wlan_dp->tx_prepare_ctx = params->tx_prepare_ctx;
	atomic_set(&wlan_dp->idle_detector.wake_lock, 0);
	wlan_dp->idle_detector.param.interval_msec = WLAN_IDLE_DETECTION_INTERVAL_IN_MSEC;
	wlan_dp->idle_detector.param.delayed_work_task = WlanDpIdleDetectorTask;
	wlan_dp->idle_detector.idle_detected_callback = params->idle_detected_callback;
	wlan_dp->idle_detector.idle_detected_ctx = params->idle_detected_ctx;
	wlan_dp->throughput_monitor.param.interval_msec = WLAN_THROUGHPUT_MONITOR_INTERVAL_IN_MSEC;
	wlan_dp->throughput_monitor.param.delayed_work_task = WlanDpThroughputMonitorTask;
	wlan_dp->throughput_monitor.throughput_monitor_callback = WlanDpThroughputMonitorCallback;
	wlan_dp->throughput_monitor.throughput_monitor_ctx = wlan_dp;
	wlan_dp->ra_proxy_flags.ra_proxy_enabled = false;
	wlan_dp->ra_proxy_flags.ra_proxy_high_tput_flag = false;
	wlan_dp->ra_proxy_flags.last_mode = kWlanDpNormalMode;
	wlan_dp->delayed_works.interval_msec = WLAN_DELAYED_WORK_INTERVAL_IN_MSEC;
	WlanDpAddDelayedWork(wlan_dp, &wlan_dp->idle_detector.param);
	WlanDpAddDelayedWork(wlan_dp, &wlan_dp->throughput_monitor.param);
	SysIfInitDelayedWorkWithResource(&wlan_dp->delayed_works.shared_worker, WlanDpDelayedWorkTask, wlan_dp);

	init_completion(&wlan_dp->drain_completion);

	// Initialize Ring service interrupts.
	for (i = 0; i < params->num_ring_svc_irq; i++) {
		const struct WlanDpInitIrqInfo *irq_info = &(params->ring_svc_irq_info[i]);
		struct WlanIntrContext *intr_ctx = &(wlan_dp->ring_svc_intr_ctx_group[i]);

		intr_ctx->intr_id = i;
		intr_ctx->type = kWlanIntrContextTypeRingSvc;
		intr_ctx->irq_num = irq_info->irq_num;
		intr_ctx->rx_data_ring_polling_mask = irq_info->rx_data_ring_polling_mask;
		intr_ctx->tx_cpl_ring_polling_mask = irq_info->tx_cpl_ring_polling_mask;

		// The ring service currently uses a mailbox to notify the
		// Wi-Fi firmware, but we may switch to a standard
		// interrupt-based notification mechanism.
		if (SysIfRegisterMailbox(kNcpWifiMailboxTypeNep, 0, WlanDpRingSvcRxIsr, intr_ctx)) {
			WLAN_LOG_ERROR(Dp, "%s(): register mailbox failed.", __func__);
			WlanDpDeinit(wlan_dp);
			return -EINVAL;
		}

		if (SysIfRegisterMailbox(kNcpWifiMailboxTypeAp, kDoorbellDirectSubEvent,
					 WlanDpRingSvcRxIsr, intr_ctx)) {
			WLAN_LOG_ERROR(Cfg, "%s(): register mailbox failed.", __func__);
		}

		wlan_dp->num_ring_svc_irq++;
	}

	WlanWorkerInit(&wlan_dp->wlan_dev_dp_worker, kWlanWorkerPriorityHigh, WlanDpWdevRxTask);
	WlanWorkerInit(&wlan_dp->ring_svc_dp_worker, kWlanWorkerPriorityHigh, WlanDpRingSvcRxTask);
	WlanWorkerInit(&wlan_dp->buffer_replenish_worker, kWlanWorkerPriorityHigh,
		       WlanDpBufferRefillTask);

	return 0;
};

void WlanDpDeinit(WlanDp *const wlan_dp)
{
	uint16_t i;

	for (i = 0; i < wlan_dp->num_wlan_dev_irq; i++) {
		WlanIntrContext *intr_ctx = &(wlan_dp->wlan_dev_intr_ctx_group[i]);
		SysIfFreeIrq(intr_ctx->irq_num, intr_ctx);
	}

	SysIfUnregisterMailbox(kNcpWifiMailboxTypeNep, 0);

	WlanWorkerDeinit(&wlan_dp->wlan_dev_dp_worker);
	WlanWorkerDeinit(&wlan_dp->ring_svc_dp_worker);
	WlanWorkerDeinit(&wlan_dp->buffer_replenish_worker);

	SysIfDeinitDelayedWork(&wlan_dp->delayed_works.shared_worker);

	deinit_completion(&wlan_dp->drain_completion);

	memset(wlan_dp, 0, sizeof(WlanDp));
};

void WlanDpStart(WlanDp *const wlan_dp)
{
	wlan_dp->state = kWlanDpStateStart;
}

void WlanDpStop(WlanDp *const wlan_dp)
{
	wlan_dp->state = kWlanDpStateStop;
	tasklet_kill(&wlan_dp->wlan_dev_dp_worker.task);
	tasklet_kill(&wlan_dp->ring_svc_dp_worker.task);
	tasklet_kill(&wlan_dp->buffer_replenish_worker.task);
}

int32_t WlanDpWdevRequestIrqs(WlanDp *const wlan_dp, int32_t irq_nums,
			      const WlanDpInitIrqInfo *irq_info)
{
	int i = 0;

	for (i = 0; i < irq_nums; i++) {
		WlanIntrContext *intr_ctx = &(wlan_dp->wlan_dev_intr_ctx_group[i]);

		intr_ctx->intr_id = i;
		intr_ctx->type = kWlanIntrContextTypeWlanDev;
		intr_ctx->irq_num = irq_info[i].irq_num;
		intr_ctx->rx_data_ring_polling_mask = irq_info[i].rx_data_ring_polling_mask;
		intr_ctx->tx_cpl_ring_polling_mask = irq_info[i].tx_cpl_ring_polling_mask;

		if (SysIfRequestIrq(intr_ctx->irq_num, WlanDpWdevRxIsr, 0, intr_ctx)) {
			WLAN_LOG_ERROR(Dp, "%s(): request IRQ(%" PRIi16 ") failed.", __func__,
				       intr_ctx->irq_num);
			WlanDpDeinit(wlan_dp);
			return -EINVAL;
		}

		SysIfEnableIrq(intr_ctx->irq_num);

		wlan_dp->num_wlan_dev_irq++;
	}

	return 0;
}

static int32_t WlanDpWdevRxIsrClassify(WlanDp *wlan_dp)
{
	if (!wlan_dp) {
		WLAN_LOG_ERROR(Dp, "%s(): Invalid wlan_dp.", __func__);
		return -EINVAL;
	}

	// 1. FW_TRAP Check

	if (WdevIfFwTrapCheck(wlan_dp->wdev_if, wlan_dp->fw_trap_addr)) {
		WLAN_LOG_ERROR(Dp, "%s(): FW trap data is not zero.", __func__);
		return -EFAULT;
	}

	// 2. Completion Time Out Check
	if (WdevIfPcieCheckCmplTimeOut(wlan_dp->wdev_if)) {
		WLAN_LOG_ERROR(Dp, "%s(): Completion time out detected.", __func__);
		return -ETIMEDOUT;
	}

	// 3. PCIe link state check
	if (SysIfCheckPcieLinkState() != 1) {
		WLAN_LOG_ERROR(Dp, "%s(): PCIe link state check failed.", __func__);
		return -EIO;
	}

	return 0;
}

NoaIrqReturn WlanDpWdevRxIsr(int32_t irq, void *context)
{
	WlanIntrContext *intr_ctx = WLAN_STATIC_CAST(WlanIntrContext *, context);
	// Calculate the base address of the interrupt context group.
	WlanIntrContext *intr_ctx_group = WLAN_REINTERPRET_CAST(
		WlanIntrContext *, WLAN_REINTERPRET_CAST(uintptr_t, intr_ctx) -
					   (intr_ctx->intr_id * sizeof(WlanIntrContext)));
	WlanDp *wlan_dp = container_of(WLAN_STATIC_CAST(void *, intr_ctx_group), WlanDp,
				       wlan_dev_intr_ctx_group);
	WlanWorker *worker = &wlan_dp->wlan_dev_dp_worker;
	uint8_t ring_idx;

	SysIfDisableIrqNoSync(irq);
	SysIfClearIrq(irq);

	WdevIfAcknowledgeInterrupt(wlan_dp->wdev_if, irq);

	if (wlan_dp->state != kWlanDpStateStart) {
		WLAN_LOG_WARN(Cfg, "%s(): WlanDpState is not starting.", __func__);
		SysIfNotifyMailbox(kNcpWifiMailboxTypeNepToAp, kDoorbellRxEvent);
		SysIfEnableIrq(irq);
		return kIrqHandled;
	}

	// Validate the IRQ number against the expected value.
	if (irq != intr_ctx->irq_num) {
		WLAN_LOG_ERROR(Dp, "%s(): irq(%" PRIi32 ") != intr_ctx->irq_num(%" PRIi16 ")",
			       __func__, irq, intr_ctx->irq_num);
		return kIrqHandled;
	}

	intr_ctx->intr_stats.num_total_intr++;

	if (WlanDpWdevRxIsrClassify(wlan_dp) != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): WlanDpWdevRxIsrClassify failed.", __func__);
		// TODO(b/406447038): If there is an error detected, we should report to AP
		// immediately and skip the rest of data/control ring process.
	}

	// Update the interrupt statistics for TX completion rings.
	if (intr_ctx->tx_cpl_ring_polling_mask) {
		for (ring_idx = 0; ring_idx < MAX_NUM_RX_DATA_RINGS; ring_idx++) {
			if (intr_ctx->tx_cpl_ring_polling_mask & (1U << ring_idx)) {
				intr_ctx->intr_stats.num_tx_cpl_ring[ring_idx]++;
			}
		}
	}

	// Update the interrupt statistics for RX data rings.
	if (intr_ctx->rx_data_ring_polling_mask) {
		for (ring_idx = 0; ring_idx < MAX_NUM_RX_DATA_RINGS; ring_idx++) {
			if (intr_ctx->rx_data_ring_polling_mask & (1U << ring_idx)) {
				intr_ctx->intr_stats.num_rx_data_ring[ring_idx]++;
			}
		}
	}

	// Schedule the worker task if there is work to do.
	if (intr_ctx->tx_cpl_ring_polling_mask || intr_ctx->rx_data_ring_polling_mask) {
		uintptr_t lock_flags = 0;
		spin_lock_irqsave(&worker->mask_lock, lock_flags);
		worker->intr_ctx_polling_mask |= (1U << intr_ctx->intr_id);
		spin_unlock_irqrestore(&worker->mask_lock, lock_flags);
		tasklet_schedule(&worker->task);
	}

	SysIfNotifyMailbox(kNcpWifiMailboxTypeNepToAp, kDoorbellRxEvent);

	return kIrqHandled;
}

// TODO: Enhance coherence control to batch update.
static void ComposeWdevCmplCoherenceInfo(WlanDp *wlan_dp, WlanRing *cmpl_ring,
					 WdevCmplDescCoherenceValidationMethod val_method,
					 WdevCmplDescCoherenceInfo *coherence_info)
{
	memset(coherence_info, 0, sizeof(WdevPostDescCoherenceInfo));

	if (!wlan_dp || !cmpl_ring) {
		return;
	}

	if (val_method == kWdevCmplDescCoherenceValidationMethodBrcmSnCsum) {
		coherence_info->brcm_sn_cks.current_sn = cmpl_ring->sn;
		coherence_info->brcm_sn_cks.ring_desc_size = cmpl_ring->desc_sz;
	}
}

static void SyncWdevCmplCoherenceInfo(WlanDp *wlan_dp, WlanRing *cmpl_ring,
				      WdevCmplDescCoherenceValidationMethod val_method,
				      WdevCmplDescCoherenceInfo *coherence_info)
{
	if (!wlan_dp || !cmpl_ring) {
		return;
	}

	if (val_method == kWdevCmplDescCoherenceValidationMethodBrcmSnCsum) {
		cmpl_ring->sn = coherence_info->brcm_sn_cks.next_sn;
	}
}

static bool WlanDpHandleWdevRxCmpl(WlanDp *wlan_dp, WlanRing *src_ring, uint32_t *budget)
{
	uint32_t src_ring_read_count;
	uint32_t prim_rx_dst_ring_write_count;
	uint32_t flbk_rx_dst_ring_write_count;
	NoaDesc *noa_desc;
	NoaNetworkRxD *network_ext_rxd;
	void *wdev_desc;
	WlanRing *prim_rx_dst_ring;
	WlanRing *flbk_rx_dst_ring;
	const BmTkidItem *tkid_item;
	WdevCmplDescCoherenceInfo coherence_info;
	WdevCmplDescCoherenceValidationMethod val_method =
		WdevIfGetCmplValidateDescriptorMethod(wlan_dp->wdev_if);
	WdevRxCmplDescriptorInfo desc_info;
	uint8_t *head_room;
	uint32_t iif = 0;
	const uint8_t feedthrough_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);
	const uint8_t netengine_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
	const uint8_t wdev_rx_ring = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);

	if (!wlan_dp || !src_ring || !budget) {
		WLAN_LOG_ERROR(Rx, "%s(): invalid inputs.", __func__);
		return false;
	}

	if (wlan_dp->mode == kWlanDpDirectApcMode) {
		if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcDirectRxCmplRingGroup, 0,
					   &prim_rx_dst_ring) != 0) {
			WLAN_LOG_ERROR(Rx, "%s(): WlanRingManagerGetRing failed.", __func__);
			return false;
		}
	} else {
		if (WlanRingManagerGetRing(wlan_dp->ring_manager, kNepTxPostRingGroup, 0,
					   &prim_rx_dst_ring) != 0) {
			WLAN_LOG_ERROR(Rx, "%s(): WlanRingManagerGetRing failed.", __func__);
			return false;
		}
	}

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcFallbackRxCmplRingGroup, 0,
				   &flbk_rx_dst_ring) != 0) {
		WLAN_LOG_ERROR(Rx, "%s(): WlanRingManagerGetRing failed.", __func__);
		return false;
	}

	src_ring_read_count = WlanRingGetReadCount(src_ring);
	prim_rx_dst_ring_write_count = WlanRingGetWriteCount(prim_rx_dst_ring);
	flbk_rx_dst_ring_write_count = WlanRingGetWriteCount(flbk_rx_dst_ring);

	if (src_ring_read_count == 0 || *budget == 0) {
		goto DONE;
	}

	for (; *budget && src_ring_read_count && prim_rx_dst_ring_write_count;
	     *budget -= 1, src_ring_read_count -= 1, prim_rx_dst_ring_write_count -= 1) {
		wdev_desc = WLAN_REINTERPRET_CAST(void *, WlanRingGetReadBase(src_ring));
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc), src_ring->desc_sz);
		ComposeWdevCmplCoherenceInfo(wlan_dp, src_ring, val_method, &coherence_info);
		if (WdevIfHandleRxCplDesc(wlan_dp->wdev_if, wdev_desc, &coherence_info,
					  &desc_info) != 0) {
			WLAN_LOG_ERROR(Rx, "%s(): WdevIfHandleRxCplDesc failed.", __func__);
			WLAN_DP_STATS_RX_ERR_INC(wlan_dp);
			goto NEXT_DESC;
		}
		if (wlan_dp->wdev_if->chip_id == kWlanDeviceChipIdWcn7760) {
			iif = 0; //TODO: Check for flow id mapping in Wcn
		} else {
			iif = FindOifFromBssIdx(wlan_dp->flow_id_table, desc_info.bss_idx);
		}
		if (iif < 0) {
			WLAN_LOG_ERROR(Rx, "%s(): invalid bss_idx: %" PRIu32, __func__,
				       desc_info.bss_idx);
			WLAN_DP_STATS_RX_ERR_INC(wlan_dp);
			goto NEXT_DESC;
		}
		SyncWdevCmplCoherenceInfo(wlan_dp, src_ring, val_method, &coherence_info);

		if (wlan_dp->simulate_rx_drop) {
			/* Dynamic test mode: intentionally drop received packets for simulation/testing */
			BmRelease(kVendorRxBufferManager, desc_info.pktid);
			WLAN_DP_STATS_RX_ERR_INC(wlan_dp);
			goto NEXT_DESC;
		}

		if (BmFind(kVendorRxBufferManager, desc_info.pktid, &tkid_item) != 0) {
			WLAN_LOG_WARN(Rx, "%s(): failed to find buffer for received tkid %u.",
				      __func__, desc_info.pktid);
			if (flbk_rx_dst_ring_write_count) {
				void *dst_desc = WLAN_REINTERPRET_CAST(
					void *, WlanRingGetWriteBase(flbk_rx_dst_ring));
				memcpy(dst_desc, wdev_desc, src_ring->desc_sz);
				SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, dst_desc),
						 flbk_rx_dst_ring->desc_sz);
				WlanRingUpdateSwWrite(flbk_rx_dst_ring);
				flbk_rx_dst_ring_write_count--;
				WLAN_LOG_INFO(
					Rx,
					"%s(): processing Rx completion for tkid %u via fallback path.",
					__func__, desc_info.pktid);
			} else {
				break;
			}
			goto NEXT_DESC;
		}

		WLAN_PACKET_SNIFF(WLAN_STATIC_CAST(uint64_t, tkid_item->va + desc_info.head_offset +
								     WLAN_PKT_PAD),
				  desc_info.data_len - desc_info.head_offset, NULL, NULL, NoaHwRx);
		wlan_dp->throughput_monitor.rx_byte += desc_info.data_len;

		head_room = (WLAN_REINTERPRET_CAST(uint8_t *, tkid_item->va));
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, head_room), src_ring->desc_sz);
		memcpy(head_room, wdev_desc, src_ring->desc_sz);
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, head_room), src_ring->desc_sz);

		noa_desc = WLAN_REINTERPRET_CAST(NoaDesc *, WlanRingGetWriteBase(prim_rx_dst_ring));
		memset(noa_desc, 0, sizeof(NoaDesc));
		noa_desc->ver = 0;
		noa_desc->ddone = 0;
		noa_desc->src = wdev_rx_ring;

		switch (wlan_dp->mode) {
		case kWlanDpNormalMode:
			network_ext_rxd =
				WLAN_REINTERPRET_CAST(NoaNetworkRxD *, &noa_desc->ext_data[0]);
			network_ext_rxd->iif = iif;
			if (!wlan_dp->sta_table->hotspot_en &&
			    (desc_info.flags & kWdevPacketFlagStationMode ||
			     desc_info.flags & kWdevPacketFlag802dot11)) {
				noa_desc->dst = feedthrough_path;
				noa_desc->cp = NOAD_NO_COPY;
				noa_desc->fk = NOAD_FEEDBACK_DISABLE;
				noa_desc->reason = FWD_REASON_FEEDTHROUGH;
			} else {
				noa_desc->dst = netengine_path;
				// Request to copy to netengine SRAM.
				noa_desc->cp = NOAD_COPY_DATA;
				// Request to send feedback event.
				noa_desc->fk = NOAD_FEEDBACK_ENABLE;
				noa_desc->reason = FWD_REASON_NETENGINE;
			}
			break;
		case kWlanDpDirectApcMode:
			noa_desc->dst = feedthrough_path;
			noa_desc->cp = NOAD_NO_COPY;
			noa_desc->fk = NOAD_FEEDBACK_DISABLE;
			noa_desc->reason = FWD_REASON_FEEDTHROUGH;
			break;
		case kWlanDpForceFeedthroughMode:
			noa_desc->dst = feedthrough_path;
			noa_desc->cp = NOAD_NO_COPY;
			noa_desc->fk = NOAD_FEEDBACK_DISABLE;
			noa_desc->reason = FWD_REASON_FEEDTHROUGH;
			break;
		case kWlanDpForceTetheringMode:
			noa_desc->dst = netengine_path;
			noa_desc->cp = NOAD_COPY_DATA;
			noa_desc->fk = NOAD_FEEDBACK_ENABLE;
			noa_desc->reason = FWD_REASON_NETENGINE;
			break;
		case kWlanDpVpnForceFwdToNetEngineMode:
			// Route all packet to NetEngine because some of them are VPN packets
			// that need to be further processed in NetEngine.
			// TODO(b/355097058): Support dynamic switch for fast path, and remove
			// the preprocessor.
			noa_desc->dst = netengine_path;
			// For VPN use case, set NO_COPY for NetEngine to decrypt the packet.
			// This is workaround and will effect the tethering path, need to fix
			// it.
			noa_desc->cp = NOAD_NO_COPY;
			noa_desc->fk = NOAD_FEEDBACK_DISABLE;
			noa_desc->reason = FWD_REASON_NETENGINE;
			break;
		case kWlanDpRaProxyForcedFwdToNetEngineMode:
			noa_desc->dst = netengine_path;
			noa_desc->cp = NOAD_COPY_DATA;
			noa_desc->fk = NOAD_FEEDBACK_ENABLE;
			noa_desc->reason = FWD_REASON_NETENGINE;
			break;
		default:
			WLAN_LOG_ERROR(Rx,
				       "%s(): unknown mode: %" PRIu32
				       ". Force feedthrough to the APC.",
				       __func__, wlan_dp->mode);
			noa_desc->dst = feedthrough_path;
			noa_desc->cp = NOAD_NO_COPY;
			noa_desc->fk = NOAD_FEEDBACK_DISABLE;
			noa_desc->reason = FWD_REASON_FEEDTHROUGH;
			break;
		}
		noa_desc->dp_low = WLAN_STATIC_CAST(uint32_t, tkid_item->pa + WLAN_PKT_PAD);
		noa_desc->dp_high = WLAN_STATIC_CAST(
			uint32_t, (tkid_item->pa + WLAN_PKT_PAD) >> 32 & 0xFFFFFFFF);
		noa_desc->dv = tkid_item->va + WLAN_PKT_PAD;
		noa_desc->tkid = desc_info.pktid;
		noa_desc->mode = NOAD_MODE_DATA;
		noa_desc->desc_type = NOA_DESC_BASIC;
		// Define the 802.3 header offset.
		noa_desc->head_offset = desc_info.head_offset;
		// Extend data len to include header offset.
		noa_desc->dl = desc_info.data_len;
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, noa_desc), NOA_DESC_WLAN_RX_BYTE);
		WLAN_PACKET_SNIFF(noa_desc->dv + noa_desc->head_offset,
				  noa_desc->dl - noa_desc->head_offset, NULL, NULL, NepTx);
		// Update buffer state.
		if (noa_desc->dst == feedthrough_path) {
			BmRelease(kVendorRxBufferManager, desc_info.pktid);
		} else {
			BmPlaceOnHold(kVendorRxBufferManager, desc_info.pktid);
		}
		WlanRingUpdateSwWrite(prim_rx_dst_ring);
		if (noa_desc->dst == feedthrough_path) {
			WLAN_DP_STATS_RX_INC(wlan_dp);
		} else {
			WLAN_DP_STATS_RX_FORWARD_INC(wlan_dp);
		}
		WLAN_DP_STATS_NEP_FW_INPUT_INC(wlan_dp);
	NEXT_DESC:
		WlanRingUpdateSwRead(src_ring);
		WLAN_DP_STATS_DEV_RX_INC(wlan_dp);
	}

	WlanRingUpdateHwRead(src_ring);
	WlanRingUpdateHwWrite(prim_rx_dst_ring);
	WlanRingUpdateHwWrite(flbk_rx_dst_ring);

	if (wlan_dp->mode == kWlanDpDirectApcMode) {
		SysIfNotifyMailbox(kNcpWifiMailboxTypeNepToAp, kDoorbellRxEvent);
	} else {
		SysIfNotifyMailbox(kNcpWifiMailboxTypeNep, 0);
	}

DONE:
	return WlanRingGetReadCount(src_ring) > 0;
}

#define NOA_TX_PKTID_REPLN_BATCH_SIZE 4096U
static bool WlanDpHandleWdevTxCmpl(WlanDp *wlan_dp, WlanRing *src_ring, uint32_t *budget)
{
	uint32_t src_ring_read_count;
	uint32_t dst_ring_write_count;
	void *wdev_desc;
	void *dst_desc;
	WdevCmplDescCoherenceInfo coherence_info;
	WdevCmplDescCoherenceValidationMethod val_method =
		WdevIfGetCmplValidateDescriptorMethod(wlan_dp->wdev_if);
	WdevTxCmplDescriptorInfo desc_info;
	WlanRing *dst_ring;
	SEC_EXRAM_DATA static NoaBufferPoolDesc
		noa_tx_buffer_repln_buf[NOA_TX_PKTID_REPLN_BATCH_SIZE];
	uint32_t noa_tx_buffer_repln_num = 0;
	NoaBufferPoolDesc *noa_buffer_desc;

	if (!wlan_dp || !src_ring || !budget) {
		WLAN_LOG_ERROR(Rx, "%s(): invalid inputs.", __func__);
		return false;
	}

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcTxCmplRingGroup, 0, &dst_ring) != 0) {
		WLAN_LOG_ERROR(Rx, "%s(): WlanRingManagerGetRing failed.", __func__);
		return false;
	}

	src_ring_read_count = WlanRingGetReadCount(src_ring);
	dst_ring_write_count = WlanRingGetWriteCount(dst_ring);

	if (src_ring_read_count == 0 || *budget == 0 || dst_ring_write_count == 0) {
		goto DONE;
	}

	for (; *budget && src_ring_read_count && dst_ring_write_count;
	     *budget -= 1, src_ring_read_count -= 1, dst_ring_write_count -= 1) {
		WLAN_DP_STATS_DEV_TXCPL_INC(wlan_dp);
		wdev_desc = WLAN_REINTERPRET_CAST(void *, WlanRingGetReadBase(src_ring));
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc), src_ring->desc_sz);
		ComposeWdevCmplCoherenceInfo(wlan_dp, src_ring, val_method, &coherence_info);
		if (WdevIfHandleTxCplDesc(wlan_dp->wdev_if, wdev_desc, &coherence_info,
					  &desc_info) != 0) {
			WLAN_LOG_ERROR(Rx, "%s(): WdevIfHandleTxCplDesc failed.", __func__);
			WLAN_DP_STATS_TXCPL_ERR_INC(wlan_dp);
			goto NEXT_DESC;
		}
		SyncWdevCmplCoherenceInfo(wlan_dp, src_ring, val_method, &coherence_info);
		// WLAN_LOG_DEBUG(Rx, "%s(): TXCPL.pktid: %" PRIu32, __func__, desc_info.pktid);

#if IS_ENABLED(CONFIG_NOA_WLAN_TX_PKTID_AUDIT)
		if (desc_info.pktid < CONFIG_NOA_WLAN_MAX_TX_PKTID) {
			if (!tx_pktid_audit[desc_info.pktid]) {
				WLAN_LOG_WARN(Rx,
					      "%s(): Attempted to free PKTID %u, which "
					      "was not in use; possible duplicate free.",
					      __func__, desc_info.pktid);
			}
			tx_pktid_audit[desc_info.pktid] = false;
		} else {
			WLAN_LOG_ERROR(
				Tx,
				"%s: Invalid PKTID %u; value exceeds or equals the max limit of %u",
				__func__, desc_info.pktid, CONFIG_NOA_WLAN_MAX_TX_PKTID);
		}
#endif // IS_ENABLED(CONFIG_NOA_WLAN_TX_PKTID_AUDIT)

		if (desc_info.pktid >= WLAN_NEP_PKTID_MASK) {
			const BmTkidItem *tkid_item;
			if (BmReactivate(kNoaTxBufferManager,
					 (desc_info.pktid - WLAN_NEP_PKTID_MASK)) != 0) {
				WLAN_LOG_ERROR(
					Rx,
					"%s(): Failed to reactivate NEP TX buffer for pktid %" PRIu16,
					__func__, desc_info.pktid);
				WLAN_DP_STATS_TXCPL_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}
			if (BmFind(kNoaTxBufferManager, (desc_info.pktid - WLAN_NEP_PKTID_MASK),
				   &tkid_item) != 0) {
				WLAN_LOG_ERROR(
					Rx, "%s(): Failed to find NEP TX buffer for pktid %" PRIu16,
					__func__, desc_info.pktid);
				WLAN_DP_STATS_TXCPL_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}
			noa_buffer_desc = &noa_tx_buffer_repln_buf[noa_tx_buffer_repln_num];
			noa_buffer_desc->tkid = desc_info.pktid;
			noa_buffer_desc->dp_high = (tkid_item->pa >> 32U) & 0xFFFFFFFF;
			noa_buffer_desc->dp_low = tkid_item->pa & 0xFFFFFFFF;
			noa_buffer_desc->dv = tkid_item->va;
			noa_tx_buffer_repln_num++;
			if (noa_tx_buffer_repln_num >= NOA_TX_PKTID_REPLN_BATCH_SIZE) {
				break;
			}
		} else {
			dst_desc = WLAN_REINTERPRET_CAST(void *, WlanRingGetWriteBase(dst_ring));
			memcpy(dst_desc, wdev_desc, dst_ring->desc_sz);
			SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, dst_desc),
					 dst_ring->desc_sz);
			WlanRingUpdateSwWrite(dst_ring);
			WLAN_DP_STATS_TXCPL_INC(wlan_dp);
		}
	NEXT_DESC:
		WlanRingUpdateSwRead(src_ring);
	}

	if (noa_tx_buffer_repln_num) {
		if (WlanNepBufferPoolBatchReplenish(wlan_dp->nep_tx_buffer_pool,
						    noa_tx_buffer_repln_num,
						    noa_tx_buffer_repln_buf) != 0) {
			WLAN_LOG_WARN(Rx, "%s(): Failed to process NEP TX buffer refill", __func__);
		} else {
			WLAN_DP_STATS_TXBM_SYNC_MUL_INC(wlan_dp, noa_tx_buffer_repln_num);
		}
	}

	WlanRingUpdateHwRead(src_ring);
	WlanRingUpdateHwWrite(dst_ring);
	SysIfNotifyMailbox(kNcpWifiMailboxTypeNepToAp, kDoorbellRxEvent);

DONE:
	return WlanRingGetReadCount(src_ring) > 0;
}

void WlanDpWdevRxTask(struct WlanWorker *worker, uint32_t budget)
{
	WlanDp *wlan_dp = container_of(worker, WlanDp, wlan_dev_dp_worker);
	uint8_t i;
	uintptr_t lock_flags = 0;
	uint32_t tmp_intr_ctx_polling_mask;
	uint32_t ori_intr_ctx_polling_mask;
	WlanIntrContext *intr_ctx;
	uint32_t ring_id;
	WlanRing *ring;
	bool more = false;

	spin_lock_irqsave(&worker->mask_lock, lock_flags);
	ori_intr_ctx_polling_mask = worker->intr_ctx_polling_mask;
	spin_unlock_irqrestore(&worker->mask_lock, lock_flags);
	tmp_intr_ctx_polling_mask = ori_intr_ctx_polling_mask;

	for (i = 0; i < wlan_dp->num_wlan_dev_irq; i++) {
		if (budget && ori_intr_ctx_polling_mask & (1U << i)) {
			intr_ctx = &wlan_dp->wlan_dev_intr_ctx_group[i];

			if (intr_ctx->rx_data_ring_polling_mask) {
				uint32_t rxcmpl_ring_num = WlanRingManagerGetRingNum(
					wlan_dp->ring_manager, kWdevRxCmplRingGroup);
				for (ring_id = 0; ring_id < rxcmpl_ring_num; ring_id++) {
					if ((intr_ctx->rx_data_ring_polling_mask &
					     (1UL << ring_id)) == 0) {
						continue;
					}

					if (WlanRingManagerGetRing(wlan_dp->ring_manager,
								   kWdevRxCmplRingGroup, ring_id,
								   &ring) != 0) {
						WLAN_LOG_ERROR(Dp,
							       "%s(): unable to get ring: %" PRIu32
							       " type: %u" PRIu32,
							       __func__, ring_id,
							       kWdevRxCmplRingGroup);
						continue;
					}

					more |= WlanDpHandleWdevRxCmpl(wlan_dp, ring, &budget);
				}
			}

			if (intr_ctx->tx_cpl_ring_polling_mask) {
				uint32_t txcmpl_ring_num = WlanRingManagerGetRingNum(
					wlan_dp->ring_manager, kWdevTxCmplRingGroup);
				for (ring_id = 0; ring_id < txcmpl_ring_num; ring_id++) {
					if ((intr_ctx->tx_cpl_ring_polling_mask &
					     (1UL << ring_id)) == 0) {
						continue;
					}

					if (WlanRingManagerGetRing(wlan_dp->ring_manager,
								   kWdevTxCmplRingGroup, ring_id,
								   &ring) != 0) {
						WLAN_LOG_ERROR(Dp,
							       "%s(): unable to get ring: %" PRIu32
							       " type: %u" PRIu32,
							       __func__, ring_id,
							       kWdevTxCmplRingGroup);
						continue;
					}

					more |= WlanDpHandleWdevTxCmpl(wlan_dp, ring, &budget);
				}
			}

			if (!more) {
				tmp_intr_ctx_polling_mask =
					UNMASK_POLLING_BIT(tmp_intr_ctx_polling_mask, i);
			}
		}
	}

	spin_lock_irqsave(&worker->mask_lock, lock_flags);
	worker->intr_ctx_polling_mask =
		UPDATE_POLLING_MASK(worker->intr_ctx_polling_mask, ori_intr_ctx_polling_mask,
				    tmp_intr_ctx_polling_mask);
	spin_unlock_irqrestore(&worker->mask_lock, lock_flags);

	if (ori_intr_ctx_polling_mask != tmp_intr_ctx_polling_mask) {
		for (i = 0; i < wlan_dp->num_wlan_dev_irq; i++) {
			if ((ori_intr_ctx_polling_mask & (1U << i)) !=
			    (tmp_intr_ctx_polling_mask & (1U << i))) {
				intr_ctx = &(wlan_dp->wlan_dev_intr_ctx_group[i]);
				SysIfEnableIrq(intr_ctx->irq_num);
			}
		}
	}

	if (worker->intr_ctx_polling_mask) {
		tasklet_schedule(&worker->task);
	}
};

MailboxReturn WlanDpRingSvcRxIsr(int32_t irq, void *context)
{
	WlanIntrContext *intr_ctx = WLAN_STATIC_CAST(WlanIntrContext *, context);
	// Calculate the base address of the interrupt context group.
	WlanIntrContext *intr_ctx_group = WLAN_REINTERPRET_CAST(
		WlanIntrContext *, WLAN_REINTERPRET_CAST(uintptr_t, intr_ctx) -
					   (intr_ctx->intr_id * sizeof(WlanIntrContext)));
	WlanDp *wlan_dp = container_of(WLAN_STATIC_CAST(void *, intr_ctx_group), WlanDp,
				       ring_svc_intr_ctx_group);
	WlanWorker *worker = &wlan_dp->ring_svc_dp_worker;
	uint8_t ring_idx;

	intr_ctx->intr_stats.num_total_intr++;

	if (wlan_dp->state != kWlanDpStateStart) {
		WLAN_LOG_WARN(Cfg, "%s(): WlanDpState is not starting.", __func__);
		return kMailboxIrqHandled;
	}
	// Update the interrupt statistics for TX completion rings.
	if (intr_ctx->tx_cpl_ring_polling_mask) {
		for (ring_idx = 0; ring_idx < MAX_NUM_TX_CPL_RINGS; ring_idx++) {
			if (intr_ctx->tx_cpl_ring_polling_mask & (1U << ring_idx)) {
				intr_ctx->intr_stats.num_tx_cpl_ring[ring_idx]++;
			}
		}
	}

	// Update the interrupt statistics for RX data rings.
	if (intr_ctx->rx_data_ring_polling_mask) {
		for (ring_idx = 0; ring_idx < MAX_NUM_RX_DATA_RINGS; ring_idx++) {
			if (intr_ctx->rx_data_ring_polling_mask & (1U << ring_idx)) {
				intr_ctx->intr_stats.num_rx_data_ring[ring_idx]++;
			}
		}
	}

	// Schedule the worker task if there is work to do.
	if (intr_ctx->tx_cpl_ring_polling_mask || intr_ctx->rx_data_ring_polling_mask) {
		uintptr_t lock_flags = 0;
		spin_lock_irqsave(&worker->mask_lock, lock_flags);
		worker->intr_ctx_polling_mask |= (1U << intr_ctx->intr_id);
		spin_unlock_irqrestore(&worker->mask_lock, lock_flags);
		tasklet_schedule(&worker->task);
	}

	return kMailboxIrqHandled;
}

static void ComposeWdevRxPostCoherenceInfo(WlanDp *wlan_dp, WlanRing *refill_ring,
					   WdevPostDescCoherenceValidationMethod val_method,
					   WdevPostDescCoherenceInfo *coherence_info)
{
	memset(coherence_info, 0, sizeof(WdevPostDescCoherenceInfo));

	if (!wlan_dp || !refill_ring) {
		return;
	}

	if (val_method == kWdevPostDescCoherenceValidationMethodBrcmSn) {
		coherence_info->brcm_sn_cks.current_sn = refill_ring->sn;
		coherence_info->brcm_sn_cks.ring_desc_size = refill_ring->desc_sz;
	}
}

static void SyncWdevRxPostCoherenceInfo(WlanDp *wlan_dp, WlanRing *refill_ring,
					WdevPostDescCoherenceValidationMethod val_method,
					WdevPostDescCoherenceInfo *coherence_info)
{
	if (!wlan_dp || !refill_ring) {
		return;
	}

	if (val_method == kWdevPostDescCoherenceValidationMethodBrcmSn) {
		refill_ring->sn = coherence_info->brcm_sn_cks.next_sn;
	}
}

static bool WlanDpHandleRingSvcRxCmpl(WlanDp *wlan_dp, WlanRing *src_ring, uint32_t *budget)
{
	uint32_t read_count;
	NoaDesc *noa_desc;
	uint8_t *wdev_desc;
	WlanRing *dst_refill_ring;

	if (!wlan_dp || !src_ring || !budget) {
		WLAN_LOG_ERROR(Dp, "%s(): invalid inputs.", __func__);
		return false;
	}

	read_count = WlanRingGetReadCount(src_ring);

	if (read_count == 0 || *budget == 0) {
		goto DONE;
	}

	WlanDpAcquireWakeLock(wlan_dp);

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kWdevRxPostRingGroup, 0,
				   &dst_refill_ring) != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): WlanRingManagerGetRing failed.", __func__);
		goto DONE;
	}

	for (; read_count && *budget; read_count -= 1, *budget -= 1) {
		noa_desc = WLAN_REINTERPRET_CAST(NoaDesc *, WlanRingGetReadBase(src_ring));
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, noa_desc), src_ring->desc_sz);
		if (noa_desc->mode == NOAD_MODE_DATA) {
			WlanRing *dst_ring;
			NoaWlanExtendTxD ntw_ext_txd;
			NoaWlanExtendTxD *wlan_ext_txd;
			uint32_t ring_id;

			WLAN_PACKET_SNIFF(noa_desc->dv + noa_desc->head_offset,
					  noa_desc->dl - noa_desc->head_offset, NULL, NULL, NepRx);

			if (wlan_dp->tx_prepare_callback) {
				wlan_dp->tx_prepare_callback(wlan_dp->tx_prepare_ctx);
			}

			if (noa_desc->desc_type == NOA_DESC_NETENG_PKT_FLOW) {
				const StaInfo *sta_info;
				NoaNetworkTxD *ntw_txd =
					WLAN_REINTERPRET_CAST(NoaNetworkTxD *, noa_desc->ext_data);

				if (StaTableGetStaInfo(wlan_dp->sta_table,
						       WLAN_REINTERPRET_CAST(const uint8_t *,
									     ntw_txd->dest),
						       ntw_txd->info.oif, &sta_info) != 0) {
					WLAN_LOG_ERROR(
						Tx,
						"%s(): The destination station "
						"(%02X:%02X:%02X:%02X:%02X:%02X) on OIF %" PRIu32
						" is unknown.",
						__func__, ntw_txd->dest[0], ntw_txd->dest[1],
						ntw_txd->dest[2], ntw_txd->dest[3],
						ntw_txd->dest[4], ntw_txd->dest[5],
						ntw_txd->info.oif);
					WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
					goto NEXT_DESC;
				}

				if (WdevIfPrepareNoaWlanExtendTxD(wlan_dp->wdev_if, noa_desc,
								  sta_info, &ntw_ext_txd) != 0) {
					WLAN_LOG_ERROR(
						Tx, "%s(): WdevIfPrepareNoaWlanExtendTxD failed.",
						__func__);
					WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
					goto NEXT_DESC;
				}
				wlan_ext_txd = &ntw_ext_txd;
			} else if (noa_desc->desc_type == NOA_DESC_WLAN_TX_BRCM ||
				   noa_desc->desc_type == NOA_DESC_WLAN_TX_QCA) {
				wlan_ext_txd = WLAN_REINTERPRET_CAST(NoaWlanExtendTxD *,
								     noa_desc->ext_data);
				SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, wlan_ext_txd),
						   sizeof(NoaWlanExtendTxD));
			} else {
				WLAN_LOG_ERROR(Tx, "%s(): unknown reason code: %" PRIu32, __func__,
					       noa_desc->reason);
				WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}

			if (WdevIfGetTxFlowRingId(wlan_dp->wdev_if, wlan_ext_txd, &ring_id) != 0) {
				WLAN_LOG_ERROR(Tx, "%s(): WdevIfGetTxFlowRingId failed.", __func__);
				WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}

			if (WlanRingManagerGetRing(wlan_dp->ring_manager, kWdevTxPostRingGroup,
						   ring_id, &dst_ring) != 0) {
				WLAN_LOG_ERROR(Tx, "%s(): WlanRingManagerGetRing failed.",
					       __func__);
				WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}

			if (!WlanRingIsActive(dst_ring)) {
				WLAN_LOG_ERROR(Tx,
					       "%s(): dst_ring: %p, ring: %u "
					       "(hw_idx: %u, flag: %u) is deactive.",
					       __func__, dst_ring, ring_id, dst_ring->hw_idx,
					       dst_ring->flags);
				WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}

			if (WlanRingGetWriteCount(dst_ring) == 0) {
				WLAN_LOG_ERROR(Tx, "%s(): dst ring (%u) is full. flags: %u",
					       __func__, ring_id, dst_ring->flags);
				break;
			}

			wdev_desc = WlanRingGetWriteBase(dst_ring);
			if (WdevIfPrepareTxPostDesc(wlan_dp->wdev_if, noa_desc, wlan_ext_txd,
						    dst_ring->desc_sz, NULL, wdev_desc) != 0) {
				WLAN_LOG_ERROR(Tx, "%s(): WdevIfPrepareTxPostDesc failed.",
					       __func__);
				WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}
			SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc),
					 dst_ring->desc_sz);
#if IS_ENABLED(CONFIG_NOA_WLAN_TX_PKTID_AUDIT)
			if (noa_desc->tkid < CONFIG_NOA_WLAN_MAX_TX_PKTID) {
				if (tx_pktid_audit[noa_desc->tkid]) {
					WLAN_LOG_ERROR(Tx,
						       "%s: PKTID %u is already in use; "
						       "possible duplicate transmission.",
						       __func__, noa_desc->tkid);
				}
				tx_pktid_audit[noa_desc->tkid] = true;
			} else {
				WLAN_LOG_ERROR(Tx,
					       "%s: Invalid PKTID %u; value exceeds "
					       "or equals the max limit of %u",
					       __func__, noa_desc->tkid,
					       CONFIG_NOA_WLAN_MAX_TX_PKTID);
			}
#endif // IS_ENABLED(CONFIG_NOA_WLAN_TX_PKTID_AUDIT)
			WlanRingUpdateSwWrite(dst_ring);
			WlanRingUpdateHwWrite(dst_ring);
			WdevIfRingTxPostDoorbell(wlan_dp->wdev_if,
						 WLAN_REINTERPRET_CAST(void *, dst_ring));
			if (noa_desc->desc_type == NOA_DESC_NETENG_PKT_FLOW) {
				BmPlaceOnHold(kNoaTxBufferManager,
					      noa_desc->tkid - WLAN_NEP_PKTID_MASK);
				WLAN_DP_STATS_TX_FORWARD_INC(wlan_dp);
			} else {
				WLAN_DP_STATS_TX_INC(wlan_dp);
			}
			WLAN_DP_STATS_DEV_TX_INC(wlan_dp, ring_id);
		} else if (noa_desc->mode == NOAD_MODE_FEEDBACK) {
			// TODO(b/435038746): Create the dedicated feedback ring for the NEP-to-NCP.
			uint32_t available_write_count = 0;
			WdevPostDescCoherenceInfo coherence_info;
			WdevPostDescCoherenceValidationMethod val_method =
				WdevIfGetPostValidateDescriptorMethod(wlan_dp->wdev_if);
			BmReactivate(kVendorRxBufferManager, noa_desc->tkid);
			available_write_count = WlanRingGetWriteCount(dst_refill_ring);
			if (available_write_count == 0) {
				WLAN_LOG_ERROR(Dp, "%s(): internal error.", __func__);
				WLAN_DP_STATS_RX_REPLENISH_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}
			ComposeWdevRxPostCoherenceInfo(wlan_dp, dst_refill_ring, val_method,
						       &coherence_info);
			wdev_desc = WlanRingGetWriteBase(dst_refill_ring);
			SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc),
					   dst_refill_ring->desc_sz);
			if (WdevIfPrepareRxPostDesc(wlan_dp->wdev_if, noa_desc->tkid,
						    dst_refill_ring->desc_sz, &coherence_info,
						    wdev_desc) != 0) {
				WLAN_LOG_ERROR(Dp, "%s(): WdevIfPrepareRxPostDesc failed.",
					       __func__);
				WLAN_DP_STATS_RX_REPLENISH_ERR_INC(wlan_dp);
				goto NEXT_DESC;
			}
			SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc),
					 dst_refill_ring->desc_sz);
			SyncWdevRxPostCoherenceInfo(wlan_dp, dst_refill_ring, val_method,
						    &coherence_info);
			WlanRingUpdateSwWrite(dst_refill_ring);
			WlanRingUpdateHwWrite(dst_refill_ring);
			WLAN_DP_STATS_RXBM_SYNC_INC(wlan_dp);
			WLAN_DP_STATS_DEV_RX_REPLENISH_INC(wlan_dp);
		} else {
			WLAN_LOG_ERROR(Dp, "%s(): unsupported mode: %" PRIu32, __func__,
				       noa_desc->mode);
			WLAN_DP_STATS_TX_ERR_INC(wlan_dp);
		}

	NEXT_DESC:
		WlanRingUpdateSwRead(src_ring);
		WLAN_DP_STATS_NEP_FW_OUTPUT_INC(wlan_dp, 0);
	}

	WlanRingUpdateHwRead(src_ring);
	WlanDpReleaseWakeLock(wlan_dp);

DONE:
	return WlanRingGetReadCount(src_ring) > 0;
};

void WlanDpRingSvcRxTask(WlanWorker *worker, uint32_t budget)
{
	WlanDp *wlan_dp = container_of(worker, WlanDp, ring_svc_dp_worker);
	uint8_t i;
	uintptr_t lock_flags = 0;
	uint32_t tmp_intr_ctx_polling_mask;
	uint32_t ori_intr_ctx_polling_mask;
	uint32_t ring_id;
	WlanRing *ring;
	WlanIntrContext *intr_ctx;
	uint32_t rxcmpl_ring_num =
		WlanRingManagerGetRingNum(wlan_dp->ring_manager, kWdevRxCmplRingGroup);

	spin_lock_irqsave(&worker->mask_lock, lock_flags);
	ori_intr_ctx_polling_mask = worker->intr_ctx_polling_mask;
	spin_unlock_irqrestore(&worker->mask_lock, lock_flags);
	tmp_intr_ctx_polling_mask = ori_intr_ctx_polling_mask;

	for (i = 0; i < wlan_dp->num_ring_svc_irq; i++) {
		if (budget && ori_intr_ctx_polling_mask & (1UL << i)) {
			bool more = false;

			intr_ctx = &wlan_dp->ring_svc_intr_ctx_group[i];

			if (intr_ctx->rx_data_ring_polling_mask) {
				for (ring_id = 0; ring_id < rxcmpl_ring_num; ring_id++) {
					if ((intr_ctx->rx_data_ring_polling_mask &
					     (1UL << ring_id)) == 0) {
						continue;
					}

					if (wlan_dp->mode == kWlanDpDirectApcMode) {
						if (WlanRingManagerGetRing(wlan_dp->ring_manager,
									   kApcDirectTxPostRingGroup,
									   ring_id, &ring) != 0) {
							WLAN_LOG_ERROR(
								Dp,
								"%s(): unable to get ring: %" PRIu32
								" type: %u" PRIu32,
								__func__, ring_id,
								kApcDirectTxPostRingGroup);
							continue;
						}
					} else {
						if (WlanRingManagerGetRing(wlan_dp->ring_manager,
									   kNepRxCmplRingGroup,
									   ring_id, &ring) != 0) {
							WLAN_LOG_ERROR(
								Dp,
								"%s(): unable to get ring: %" PRIu32
								" type: %u" PRIu32,
								__func__, ring_id,
								kNepRxCmplRingGroup);
							continue;
						}
					}

					more |= WlanDpHandleRingSvcRxCmpl(wlan_dp, ring, &budget);
				}
			}

			if (!more) {
				tmp_intr_ctx_polling_mask =
					UNMASK_POLLING_BIT(tmp_intr_ctx_polling_mask, i);
			}
		}
	}

	if (wlan_dp->is_draining) {
		if (WlanDpAreAllOutputRingsEmpty(wlan_dp)) {
			wlan_dp->is_draining = false;
			complete(&wlan_dp->drain_completion);
		}
	}

	spin_lock_irqsave(&worker->mask_lock, lock_flags);
	worker->intr_ctx_polling_mask =
		UPDATE_POLLING_MASK(worker->intr_ctx_polling_mask, ori_intr_ctx_polling_mask,
				    tmp_intr_ctx_polling_mask);
	spin_unlock_irqrestore(&worker->mask_lock, lock_flags);

	if (worker->intr_ctx_polling_mask) {
		tasklet_schedule(&worker->task);
	}
};

bool WlanDpWdevRxBufferRefillTask(WlanDp *wlan_dp, uint32_t *budget)
{
	WlanRing *src_refill_ring;
	WlanRing *dst_refill_ring;
	uint8_t *wdev_desc;
	uint32_t available_write_count = 0;
	uint32_t available_read_count = 0;
	WdevPostDescCoherenceInfo coherence_info;
	WdevPostDescCoherenceValidationMethod val_method =
		WdevIfGetPostValidateDescriptorMethod(wlan_dp->wdev_if);
	NoaWlanBufferReplnDesc *bm_desc;
	uint32_t available_budget = *budget;

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcVendorRxBufReplnRingGroup, 0,
				   &src_refill_ring) != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): WlanRingManagerGetRing failed.", __func__);
		return false;
	}

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kWdevRxPostRingGroup, 0,
				   &dst_refill_ring) != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): WlanRingManagerGetRing failed.", __func__);
		return false;
	}

	available_write_count = WlanRingGetWriteCount(dst_refill_ring);
	available_read_count = WlanRingGetReadCount(src_refill_ring);

	while (available_budget && available_write_count && available_read_count) {
		bm_desc = (struct NoaWlanBufferReplnDesc *)WlanRingGetReadBase(src_refill_ring);
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, bm_desc),
				   src_refill_ring->desc_sz);
		// The packet data starts after a predefined headroom.
		// Adjust the buffer information to point to the actual start of the
		// WLAN packet payload and reflect its true size, excluding the headroom.
		if (BmAcquire(kVendorRxBufferManager, bm_desc->tkid, bm_desc->len, bm_desc->pa,
			      bm_desc->dpa_va) != 0) {
			WLAN_LOG_ERROR(Bm,
				       "%s(): BmAcquire failed. desc: %p, tkid: %u, va: %p, "
				       "desc_sz: %u/%u",
				       __func__, bm_desc, bm_desc->tkid, (void *)bm_desc->dpa_va,
				       src_refill_ring->desc_sz, sizeof(NoaWlanBufferReplnDesc));
			WLAN_DP_STATS_RX_REPLENISH_ERR_INC(wlan_dp);
			goto NEXT_BUF;
		}
		if (!bm_desc->to_dev) {
			goto NEXT_BUF;
		}
		ComposeWdevRxPostCoherenceInfo(wlan_dp, dst_refill_ring, val_method,
					       &coherence_info);
		wdev_desc = WlanRingGetWriteBase(dst_refill_ring);
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc),
				   dst_refill_ring->desc_sz);
		if (WdevIfPrepareRxPostDesc(wlan_dp->wdev_if, bm_desc->tkid,
					    dst_refill_ring->desc_sz, &coherence_info,
					    wdev_desc) != 0) {
			WLAN_LOG_ERROR(Dp, "%s(): WdevIfPrepareRxPostDesc failed.", __func__);
			WLAN_DP_STATS_RX_REPLENISH_ERR_INC(wlan_dp);
			goto NEXT_BUF;
		}
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc),
				 dst_refill_ring->desc_sz);
		SyncWdevRxPostCoherenceInfo(wlan_dp, dst_refill_ring, val_method, &coherence_info);
		WlanRingUpdateSwWrite(dst_refill_ring);
		available_write_count--;
		WLAN_DP_STATS_RX_REPLENISH_INC(wlan_dp);
		WLAN_DP_STATS_DEV_RX_REPLENISH_INC(wlan_dp);
	NEXT_BUF:
		WlanRingUpdateSwRead(src_refill_ring);
		available_read_count--;
		available_budget--;
	}

	WlanRingUpdateHwWrite(dst_refill_ring);
	WlanRingUpdateHwRead(src_refill_ring);
	*budget = available_budget;
	// TODO(b/421035870): To improve code clarity, 'Ring TX doorbell'
	// will be refactored to a more descriptive name.
	WdevIfRingTxPostDoorbell(wlan_dp->wdev_if, WLAN_REINTERPRET_CAST(void *, dst_refill_ring));

	if (WlanRingGetReadCount(src_refill_ring) != 0) {
		return true;
	}

	return false;
}

bool WlanDpNoaTxBufferRefillTask(WlanDp *wlan_dp, uint32_t *budget)
{
	WlanRing *src_refill_ring;
	uint32_t available_read_count = 0;
	NoaWlanBufferReplnDesc *bm_desc;
	uint32_t available_budget = *budget;
	NoaBufferPoolDesc noa_buffer_desc;

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcNoaTxBufReplnRingGroup, 0,
				   &src_refill_ring) != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): WlanRingManagerGetRing failed.", __func__);
		return false;
	}

	available_read_count = WlanRingGetReadCount(src_refill_ring);

	while (available_budget && available_read_count) {
		bm_desc = (struct NoaWlanBufferReplnDesc *)WlanRingGetReadBase(src_refill_ring);
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, bm_desc),
				   src_refill_ring->desc_sz);
		if (BmAcquire(kNoaTxBufferManager, bm_desc->tkid, bm_desc->len, bm_desc->pa,
			      bm_desc->dpa_va) != 0) {
			WLAN_LOG_ERROR(Bm,
				       "%s(): BmAcquire failed. desc: %p, tkid: %u, va: %p, "
				       "desc_sz: %u/%u",
				       __func__, bm_desc, bm_desc->tkid, (void *)bm_desc->dpa_va,
				       src_refill_ring->desc_sz, sizeof(NoaWlanBufferReplnDesc));
			goto NEXT_BUF;
		}
		if (!bm_desc->to_dev) {
			goto NEXT_BUF;
		}
		noa_buffer_desc.tkid = bm_desc->tkid + wlan_dp->nep_tx_buffer_pool->pktid_offset;
		noa_buffer_desc.dp_high = (bm_desc->pa >> 32U) & 0xFFFFFFFF;
		noa_buffer_desc.dp_low = bm_desc->pa & 0xFFFFFFFF;
		noa_buffer_desc.dv = bm_desc->dpa_va;
		if (WlanNepBufferPoolReplenish(wlan_dp->nep_tx_buffer_pool, &noa_buffer_desc) !=
		    0) {
			break;
		}
		WLAN_DP_STATS_TXBM_SYNC_INC(wlan_dp);
	NEXT_BUF:
		WlanRingUpdateSwRead(src_refill_ring);
		available_read_count--;
		available_budget--;
	}

	WlanRingUpdateHwRead(src_refill_ring);
	*budget = available_budget;

	if (WlanRingGetReadCount(src_refill_ring) != 0) {
		return true;
	}

	return false;
}

bool WlanDpApcFeedbackEventHandleTask(WlanDp *wlan_dp, uint32_t *budget)
{
	WlanRing *src_refill_ring;
	uint32_t available_read_count = 0;
	NoaDesc *noa_desc;
	uint32_t available_budget = *budget;

	if (WlanRingManagerGetRing(wlan_dp->ring_manager, kApcFeedbackRingGroup, 0,
				   &src_refill_ring) != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): WlanRingManagerGetRing failed.", __func__);
		return false;
	}

	available_read_count = WlanRingGetReadCount(src_refill_ring);

	while (available_budget && available_read_count) {
		noa_desc = WLAN_REINTERPRET_CAST(NoaDesc *, WlanRingGetReadBase(src_refill_ring));
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, noa_desc),
				   src_refill_ring->desc_sz);
		if (noa_desc->reason == FWD_REASON_FALLBACK || noa_desc->reason == FWD_REASON_VPN) {
			BmRelease(kVendorRxBufferManager, noa_desc->tkid);
		} else {
			WLAN_LOG_ERROR(Bm, "%s(): noa_desc.reason: %u", __func__, noa_desc->reason);
			goto NEXT_DESC;
		}
	NEXT_DESC:
		WlanRingUpdateSwRead(src_refill_ring);
		available_read_count--;
		available_budget--;
	}

	WlanRingUpdateHwRead(src_refill_ring);
	*budget = available_budget;

	if (WlanRingGetReadCount(src_refill_ring) != 0) {
		return true;
	}

	return false;
}

void WlanDpBufferRefillTask(WlanWorker *worker, uint32_t budget)
{
	WlanDp *wlan_dp = container_of(worker, WlanDp, buffer_replenish_worker);
	bool more = false;

	if (wlan_dp->state != kWlanDpStateStart) {
		tasklet_schedule(&wlan_dp->buffer_replenish_worker.task);
		return;
	}

	more |= budget ? WlanDpApcFeedbackEventHandleTask(wlan_dp, &budget) : true;
	more |= budget ? WlanDpWdevRxBufferRefillTask(wlan_dp, &budget) : true;
	more |= budget ? WlanDpNoaTxBufferRefillTask(wlan_dp, &budget) : true;

	if (more) {
		tasklet_schedule(&wlan_dp->buffer_replenish_worker.task);
	}
}

void TriggerWlanDpBufferRefill(WlanDp *wlan_dp)
{
	if (wlan_dp) {
		tasklet_schedule(&wlan_dp->buffer_replenish_worker.task);
	}
}

void WlanDpSetMode(WlanDp *wlan_dp, WlanDpMode mode)
{
	if (!wlan_dp) {
		return;
	}

	wlan_dp->mode = mode;
}

void WlanDpSetFwTrapAddr(WlanDp *wlan_dp, uint64_t trap_addr)
{
	if (!wlan_dp) {
		return;
	}

	wlan_dp->fw_trap_addr = trap_addr;
}

void WlanDpIrqDisable(WlanDp *wlan_dp)
{
	int i;
	for (i = 0; i < wlan_dp->num_wlan_dev_irq; i++) {
		struct WlanIntrContext *intr_ctx = &(wlan_dp->wlan_dev_intr_ctx_group[i]);
		SysIfDisableIrq(intr_ctx->irq_num);
	}
}

void WlanDpIrqEnable(WlanDp *wlan_dp)
{
	int i;
	for (i = 0; i < wlan_dp->num_wlan_dev_irq; i++) {
		struct WlanIntrContext *intr_ctx = &(wlan_dp->wlan_dev_intr_ctx_group[i]);
		SysIfEnableIrq(intr_ctx->irq_num);
	}
}

void WlanDpSetSimulateRxDrop(WlanDp *wlan_dp, bool drop)
{
	if (wlan_dp) {
		wlan_dp->simulate_rx_drop = drop;
	}
}
