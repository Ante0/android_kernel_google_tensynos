#ifndef CORE_DP_WLAN_DP_H
#define CORE_DP_WLAN_DP_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "sys_if/interrupt/sys_if_interrupt.h"
#include "sys_if/mailbox/sys_if_mailbox.h"
#include "sys_if/workqueue/sys_if_workqueue.h"
#include "modules/wlan_ring_manager/wlan_ring_manager.h"
#include "modules/sta_table/sta_table.h"
#include "modules/flow_id_table/flow_id_table.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"
#include "modules/wlan_nep_buffer_pool/wlan_nep_buffer_pool.h"
#include "wdev_if/wdev_if.h"
#include "ext_svc/ext_svc.h"

struct WlanWorker;

/// @brief Maximum number of delayed functions
#define MAX_NUM_DELAYED_WORKS 3
/// @brief Maximum number of TX completion rings supported.
#define MAX_NUM_TX_CPL_RINGS 4
/// @brief Maximum number of RX data rings supported.
#define MAX_NUM_RX_DATA_RINGS 32
/// @brief Maximum number of WLAN device IRQs supported.
#define MAX_NUM_WLAN_DEV_IRQ 32
/// @brief Maximum number of ring service IRQs supported.
#define MAX_NUM_RING_SVC_IRQ 1
/// @brief Budget for high priority WLAN worker.
/// @note The current budget of 256 is a preliminary value for testing
/// and will be subject to fine-tuning upon completion of the data
/// path.
#define WLAN_WORKER_HIGH_PRIORITY_BUDGET 0x100
/// @brief Budget for medium priority WLAN worker.
/// @note The current budget of 256 is a preliminary value for testing
/// and will be subject to fine-tuning upon completion of the data
/// path.
#define WLAN_WORKER_MID_PRIORITY_BUDGET 0x100
/// @brief Budget for low priority WLAN worker.
/// @note The current budget of 256 is a preliminary value for testing
/// and will be subject to fine-tuning upon completion of the data
/// path.
#define WLAN_WORKER_LOW_PRIORITY_BUDGET 0x100
/// @brief Budget for undefined priority WLAN worker.
/// @note The current budget of 1 is a preliminary value for testing
/// and will be subject to fine-tuning upon completion of the data
/// path.
#define WLAN_WORKER_UNDEFINED_PRIORITY_BUDGET 0x1
/// @brief Interval for WLAN idle detection in milliseconds.
#define WLAN_IDLE_DETECTION_INTERVAL_IN_MSEC 500
/// @brief Interval for WLAN throughput monitor in milliseconds.
#define WLAN_THROUGHPUT_MONITOR_INTERVAL_IN_MSEC 500
/// @brief Interval for default WLAN delayed works in milliseconds.
#define WLAN_DELAYED_WORK_INTERVAL_IN_MSEC 50
/// @brief Threshold for idle count before declaring dp idle.
#define WLAN_IDLE_COUNT_THRESHOLD 5
/// @brief Threshold for identifying high throughput
#define WLAN_PPF_HIGH_TPUT_THRESHOLD 10000000
/// @brief Scaling factor for WLAN PPS values.
#define WLAN_PPS_SCALING_FACTOR 10

/// @brief  Function pointer type for WLAN worker work functions.
///
/// @param[in] worker A pointer to the WlanWorker structure.
/// @param[in] budget Budget for the work function.
typedef void (*WorkFunction)(struct WlanWorker *worker, uint32_t budget);

/// @brief  WLAN worker priority levels.
typedef enum WlanWorkerPriority {
	kWlanWorkerPriorityStart = 0,
	kWlanWorkerPriorityHigh = kWlanWorkerPriorityStart,
	kWlanWorkerPriorityMid,
	kWlanWorkerPriorityLow,
	kWlanWorkerPriorityEnd,
	kWlanWorkerPriorityNum = kWlanWorkerPriorityEnd,
} WlanWorkerPriority;

/// @brief WLAN interrupt context types.
typedef enum WlanIntrContextType {
	kWlanIntrContextTypeRingSvc = 0,
	kWlanIntrContextTypeWlanDev,
	kWlanIntrContextTypeMax,
} WlanIntrContextType;

/// @brief WLAN DP Modes
typedef enum WlanDpMode {
	kWlanDpNormalMode = 0,
	kWlanDpDirectApcMode,
	kWlanDpForceFeedthroughMode,
	kWlanDpForceTetheringMode,
	kWlanDpVpnForceFwdToNetEngineMode,
	kWlanDpRaProxyForcedFwdToNetEngineMode,
} WlanDpMode;

/// @brief WLAN DP State
typedef enum WlanDpState {
	kWlanDpStateStop = 0,
	kWlanDpStateStart,
	kWlanDpStateIdle,
} WlanDpState;

/// @brief Structure representing a WLAN worker.
typedef struct WlanWorker {
	/// @brief The tasklet structure for scheduling the bottom-half
	/// work.
	struct tasklet_struct task;
	/// @brief A function pointer to the actual work function that will
	/// be executed in the tasklet context.
	WorkFunction work;
	/// @brief A bitmask indicating which interrupt context should be
	/// processed by the worker.
	uint32_t intr_ctx_polling_mask;
	/// @brief A spinlock to protect the polling masks from concurrent
	/// access.
	spinlock_t mask_lock;
	// Priority of the worker.
	WlanWorkerPriority priority;
} WlanWorker;

/// @brief Structure storing WLAN interrupt statistics.
typedef struct WlanIntrStats {
	/// @brief An array to store the number of TX completion interrupts
	/// per ring.
	uint32_t num_tx_cpl_ring[MAX_NUM_TX_CPL_RINGS];
	/// @brief An array to store the number of TX data interrupts per
	/// ring.
	uint32_t num_rx_data_ring[MAX_NUM_RX_DATA_RINGS];
	/// @brief A counter to track the total number of interrupts
	/// received. The number will be set to 0 after synchronizing to the
	/// NoaInterruptStateInfo.
	uint32_t num_total_intr;
} WlanIntrStats;

/// @brief Structure representing a WLAN interrupt context.
typedef struct WlanIntrContext {
	/// @brief Interrupt context ID.
	uint16_t intr_id;
	/// @brief Type of interrupt context.
	WlanIntrContextType type;
	/// @brief The IRQ number associated with the WLAN device.
	uint32_t irq_num;
	/// @brief A bitmask to control polling for TX completion rings.
	/// Each bit corresponds to a ring, with '1' enabling polling and
	/// '0' disabling it.
	uint32_t tx_cpl_ring_polling_mask;
	/// @brief A bitmask to control polling for RX data rings. Each bit
	/// corresponds to a ring, with '1' enabling polling and '0'
	/// disabling it.
	uint32_t rx_data_ring_polling_mask;
	/// @brief Interrupt statistics.
	WlanIntrStats intr_stats;
} WlanIntrContext;

#define MAX_OUTPUT_NUM 80
/// @brief Structure storing WLAN data processing statistics.
typedef struct WlanDpStats {
	/// @brief RX feedthrough packet count.
	uint32_t rx;
	/// @brief RX forwarding packet count.
	uint32_t rx_forward;
	/// @brief RX packet error count.
	uint32_t rx_err;
	/// @brief TX feedthrough packet count.
	uint32_t tx;
	/// @brief TX forwarding packet count.
	uint32_t tx_forward;
	/// @brief TX packet error count.
	uint32_t tx_err;
	/// @brief TX completion count.
	uint32_t tx_cpl;
	/// @brief TX completion error count.
	uint32_t tx_cpl_err;
	/// @brief RX replenish count.
	uint32_t rx_replenish;
	/// @brief RX replenish error count.
	uint32_t rx_replenish_err;
	/// @brief RX buffer sync event count.
	uint32_t rxbm_sync;
	/// @brief TX buffer sync event count
	uint32_t txbm_sync;
	/// @brief NEP firmware input ring packet count.
	uint32_t nep_fw_input;
	/// @brief NEP firmware output ring packet count
	uint32_t nep_fw_output[MAX_OUTPUT_NUM];
	/// @brief Device RX ring packet count.
	uint32_t dev_rx;
	/// @brief Device TX ring packet count.
	uint32_t dev_tx[MAX_OUTPUT_NUM];
	/// @brief Device TXCPL ring packet count.
	uint32_t dev_txcpl;
	/// @brief Device RX replenish ring packet count.
	uint32_t dev_rx_replenish;
} WlanDpStats;

typedef struct WlanDpDelayedWorkParam {
	/// @brief Task running status
	bool running;
	/// @brief Task reschedule interval
	uint32_t interval_msec;
	/// @brief Task remaining time to wake up
	uint32_t remaining_time;
	/// @brief Delayed work task function
	void (*delayed_work_task)(void *ctx);
} WlanDpDelayedWorkParam;

/// @brief Structure representing the WLAN Data Path Idle Detector.
typedef struct WlanDpIdleDetector {
	/// @brief TX reference-counted wake lock.
	atomic_t wake_lock;
	/// @brief Counter for consecutive idle intervals.
	uint32_t idle_count;
	/// @brief Delayed work structure for idle detection.
	SysIfDelayedWorkStruct idle_detect_worker;
	/// @brief Callback function for idle detection.
	void (*idle_detected_callback)(void *);
	/// @brief Context pointer for idle detection callback.
	void *idle_detected_ctx;
	/// @brief Delayed work parameter for idle detection
	WlanDpDelayedWorkParam param;
} WlanDpIdleDetector;

/// @brief Structure representing the WLAN throughput monitor
typedef struct WlanDpThroughputMonitor {
	/// @brief RX packet byte count
	uint32_t rx_byte;
	/// @brief RX bytes per second.
	uint32_t rx_bps;
	/// @brief Callback function for throughput monitor
	void (*throughput_monitor_callback)(void *);
	/// @brief Context pointer for throughout monitor
	void *throughput_monitor_ctx;
	/// @brief Delayed work parameter for throughput monitor
	WlanDpDelayedWorkParam param;
} WlanDpThroughputMonitor;

typedef struct WlanDpDelayedWorks {
	/// @brief Number of works registered
	uint8_t work_num;
	/// @brief Number of running sub-tasks
	uint8_t running_work_num;
	/// @brief Status records any sub-task is running
	bool running;
	/// @brief Task reschedule interval
	uint32_t interval_msec;
	/// @brief Delayed work parameters
	WlanDpDelayedWorkParam *work_params[MAX_NUM_DELAYED_WORKS];
	/// @brief Shared common delayed worker for all tasks
	SysIfDelayedWorkStruct shared_worker;
} WlanDpDelayedWorks;

typedef struct WlanDpRaProxyFlags {
	bool ra_proxy_enabled;
	bool ra_proxy_high_tput_flag;
	WlanDpMode last_mode;
} WlanDpRaProxyFlags;

/// @brief Structure representing the WLAN data path (DP).
typedef struct WlanDp {
	/// @brief WLAN DP mode.
	WlanDpMode mode;
	/// @brief WLAN DP state.
	WlanDpState state;
	/// @brief Number of WLAN device IRQs.
	uint16_t num_wlan_dev_irq;
	/// @brief Number of ring service IRQs.
	uint16_t num_ring_svc_irq;
	/// @brief Ring service worker.
	WlanWorker ring_svc_dp_worker;
	/// @brief WLAN device worker.
	WlanWorker wlan_dev_dp_worker;
	/// @brief Buffer replenish worker.
	WlanWorker buffer_replenish_worker;
	/// @brief WLAN device interrupt contexts.
	WlanIntrContext wlan_dev_intr_ctx_group[MAX_NUM_WLAN_DEV_IRQ];
	/// @brief Ring service interrupt contexts.
	WlanIntrContext ring_svc_intr_ctx_group[MAX_NUM_RING_SVC_IRQ];
	/// @brief WLAN data processing statistic.
	WlanDpStats dp_stats;
	/// @brief WLAN idle detector
	WlanDpIdleDetector idle_detector;
	/// @brief WLAN throughput monitor
	WlanDpThroughputMonitor throughput_monitor;
	/// @brief Shared delayed work structure
	WlanDpDelayedWorks delayed_works;
	/// @brief RA proxy flags
	WlanDpRaProxyFlags ra_proxy_flags;
	/// @brief Callback function for TX preparation.
	void (*tx_prepare_callback)(void *);
	/// @brief Context pointer for TX preparation callback.
	void *tx_prepare_ctx;
	/// @brief Pointer to the WLAN ring manager.
	WlanRingManager *ring_manager;
	/// @brief Pointer to the WDEV interface.
	WdevIf *wdev_if;
	/// @brief Pointer to the station table.
	StaTable *sta_table;
	/// @brief WLAN Flow id table
	FlowIdTable *flow_id_table;
	/// @brief Pointer to the Buffer Manager.
	BufferManagerType *bm;
	/// @brief Pointer to the WLAN NEP transmit buffer pool.
	WlanNepBufferPool *nep_tx_buffer_pool;
	/// @brief Pointer to the external services.
	ExternalServices *ext_svc;
	/// @brief Address for the firmware trap data buffer.
	uint64_t fw_trap_addr;
	/// @brief Flag to indicate if the DP is in draining mode.
	bool is_draining;
	/// @brief Simulate RX packet drop for test/verification.
	bool simulate_rx_drop;
	/// @brief Semaphore to signal drain completion.
	struct completion drain_completion;
} WlanDp;

/// @brief Structure containing initialization information for a WLAN
/// DP IRQ.
typedef struct WlanDpInitIrqInfo {
	/// @brief IRQ number.
	uint32_t irq_num;
	/// @brief Bitmask to control polling for TX completion rings.
	uint32_t tx_cpl_ring_polling_mask;
	/// @brief Bitmask to control polling for RX data rings.
	uint32_t rx_data_ring_polling_mask;
} WlanDpInitIrqInfo;

/// @brief Structure containing initialization parameters for a WLAN
/// DP.
typedef struct WlanDpInitParams {
	/// @brief WLAN DP mode.
	WlanDpMode mode;
	/// @brief Number of WLAN device IRQs.
	uint16_t num_wlan_dev_irq;
	/// @brief Number of ring service IRQs.
	uint16_t num_ring_svc_irq;
	/// @brief WLAN device IRQ information.
	WlanDpInitIrqInfo wlan_dev_irq_info[MAX_NUM_WLAN_DEV_IRQ];
	/// @brief Ring service IRQ information.
	WlanDpInitIrqInfo ring_svc_irq_info[MAX_NUM_RING_SVC_IRQ];
	/// @brief Callback function for idle detection.
	void (*idle_detected_callback)(void *);
	/// @brief Context pointer for idle detection callback.
	void *idle_detected_ctx;
	/// @brief Callback function for TX preparation.
	void (*tx_prepare_callback)(void *);
	/// @brief Context pointer for TX preparation callback.
	void *tx_prepare_ctx;
	/// @brief Pointer to the WLAN ring manager.
	WlanRingManager *ring_manager;
	/// @brief Pointer to the WDEV interface.
	WdevIf *wdev_if;
	/// @brief Pointer to the station table.
	StaTable *sta_table;
	/// @brief Pointer to the WLAN Flow ID table.
	FlowIdTable *flow_id_table;
	/// @brief Pointer to the Buffer Manager.
	BufferManagerType *bm;
	/// @brief Pointer to the WLAN NEP transmit buffer pool.
	WlanNepBufferPool *nep_tx_buffer_pool;
	/// @brief Pointer to the external services.
	ExternalServices *ext_svc;
} WlanDpInitParams;

/// @brief Initializes the WLAN data path.
///
/// @param[in] wlan_dp A pointer to the WlanDp structure.
/// @param[in] params A pointer to the WlanDpInitParams structure.
/// @return 0 on success, a negative error code otherwise.
extern int32_t WlanDpInit(WlanDp *const wlan_dp, const WlanDpInitParams *const params);

/// @brief  Deinitializes the WLAN data path.
extern void WlanDpDeinit(WlanDp *const wlan_dp);

/// @brief  Starts the WLAN data path.
extern void WlanDpStart(WlanDp *const wlan_dp);

/// @brief  Stops the WLAN data path.
extern void WlanDpStop(WlanDp *const wlan_dp);

/// @brief Request IRQ for WiFi device
///
/// @param[in] wlan_dp A pointer to the WlanDp structure.
/// @param[in] irq_nums The IRQ requesting number
/// @param[in] irq_info The necessary IRQ initialization information
/// @return 0 on success, a negative error code otherwise.
extern int32_t WlanDpWdevRequestIrqs(WlanDp *const wlan_dp, int32_t irq_nums,
				     const WlanDpInitIrqInfo *irq_info);

/// @brief  ISR for WLAN RX data path.
///
/// @param[in] irq The IRQ number that triggered the interrupt.
/// @param[in] context A pointer to the WlanIntrContext structure
/// associated with the interrupt.
/// @return An NoaIrqReturn value indicating how the interrupt was
/// handled.
extern NoaIrqReturn WlanDpWdevRxIsr(int32_t irq, void *context);

/// @brief WLAN RX worker function.
///
/// @param[in] worker A pointer to the `WlanWorker` structure.
/// @param[in] budget The budget for the worker.
extern void WlanDpWdevRxTask(WlanWorker *worker, uint32_t budget);

/// @brief ISR for ring service RX.
///
/// @param[in] irq The IRQ number that triggered the interrupt.
/// @param[in] context A pointer to the interrupt context.
/// @return A `NoaIrqReturn` value indicating the result of the ISR.
extern MailboxReturn WlanDpRingSvcRxIsr(int32_t irq, void *context);

/// @brief Task function for processing received data in the ring service.
///
/// @param[in] worker A pointer to the worker thread executing the task.
/// @param[in] budget The maximum number of packets to process in this
/// iteration.
extern void WlanDpRingSvcRxTask(WlanWorker *worker, uint32_t budget);

/// @brief  Refills the WLAN DP WDEV RX queue.
///
/// @param[in] wlan_dp Pointer to the WLAN data path structure.
/// @param[in] budget  Maximum number of packets to process.
extern bool WlanDpWdevRxBufferRefillTask(WlanDp *wlan_dp, uint32_t *budget);

/// @brief  Refills the WLAN DP NEP TX queue.
///
/// @param[in] wlan_dp Pointer to the WLAN data path structure.
/// @param[in] budget  Maximum number of packets to process.
extern bool WlanDpNoaTxBufferRefillTask(WlanDp *wlan_dp, uint32_t *budget);

extern bool WlanDpApcFeedbackEventHandleTask(WlanDp *wlan_dp, uint32_t *budget);

extern void WlanDpBufferRefillTask(WlanWorker *worker, uint32_t budget);

/// @brief Triggers the WLAN DP WDEV RX refill task.
///
/// @param[in] wlan_dp Pointer to the WLAN data path structure.
extern void TriggerWlanDpBufferRefill(WlanDp *wlan_dp);

/// @brief Set the WLAN DP mode.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
/// @param[in] mode The desired WLAN DP mode.
extern void WlanDpSetMode(WlanDp *wlan_dp, WlanDpMode mode);

/// @brief Set the FW_TRAP buffer address.
///
/// Since the FW_TRAP buffer address is written during the FW start,
/// we need to set the address after the WLAN DP is initialized.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
/// @param[in] fw_trap_addr The address of the FW_TRAP buffer.
extern void WlanDpSetFwTrapAddr(WlanDp *wlan_dp, uint64_t fw_trap_addr);

/// @brief Disable all IRQ.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpIrqDisable(WlanDp *wlan_dp);

/// @brief Enable all IRQ.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpIrqEnable(WlanDp *wlan_dp);

/// @brief Starts WLAN data path idle detection.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpIdleDetectionStart(WlanDp *wlan_dp);

/// @brief Stop WLAN data path idle detection.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpIdleDetectionStop(WlanDp *wlan_dp);

/// @brief Starts WLAN throughput monitor.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpThroughputMonitorStart(WlanDp *wlan_dp);

/// @brief Stops WLAN throughput monitor.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpThroughputMonitorStop(WlanDp *wlan_dp);

/// @brief Stops All delayed work registered.
///
/// @param [in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpDelayedWorkStop(WlanDp *wlan_dp);

/// @brief Drains the output ring asynchronously.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
/// @param[in] timeout_ms Timeout in milliseconds.
/// @return 0 on success, -ETIMEDOUT on timeout.
extern int32_t WlanDpDrainOutputRingAsync(WlanDp *wlan_dp, uint32_t timeout_ms);

/// @brief Triggers the WLAN data path polling.
///
/// This function sets the polling masks for all relevant interrupts (RX Data,
/// TX Completion) and schedules the WLAN device worker. This is used to
/// proactively process any residual data on the rings after a mode switch
/// or initialization.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
extern void WlanDpTriggerDataPathPoll(WlanDp *wlan_dp);

/// @brief Dynamically enables or disables RX packet drop simulation.
///
/// @param[in] wlan_dp Pointer to the WLAN DP instance.
/// @param[in] drop True to drop received packets, false to process normally.
extern void WlanDpSetSimulateRxDrop(WlanDp *wlan_dp, bool drop);

#endif /* CORE_DP_WLAN_DP_H */
