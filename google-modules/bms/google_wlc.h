/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Google Wireless Charging Driver
 *
 * Copyright 2023 Google LLC
 *
 */

#ifndef __GOOGLE_WLC_H_
#define __GOOGLE_WLC_H_

#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#if IS_ENABLED(CONFIG_GPIOLIB)
#include <linux/gpio/driver.h>
#endif
#include <linux/types.h>
#include <linux/mutex.h>
#include "gbms_power_supply.h"
#include "google_bms.h"
#include "max77779.h"

#define WLC_USER_VOTER				"WLC_USER_VOTER"
#define WLC_AUTH_VOTER				"WLC_AUTH_VOTER"
#define MPP25_VOTER				"MPP25_VOTER"
#define EOC_VOTER				"EOC_VOTER"
#define NEGO_VOTER				"NEGO_VOTER"
#define MODE_VOTER				"WLC_MODE_VOTER"
#define OCP_VOTER				"OCP_VOTER"
#define OUTPUT_DISABLE_VOTER			"OUTPUT_DISABLE_VOTER"
#define EOC_CLOAK_VOTER				"EOC_CLOAK_VOTER"
#define DC_ICL_VOTABLE_HANDLE			"DC_ICL"
#define CHG_ENABLE_VOTER			"CHG_ENABLE_VOTER"
#define CHG_SWITCH_VOTER			"CHG_SWITCH_VOTER"
#define USB_CONN_VOTER				"USB_CONN_VOTER"
#define WLC_MDIS_VOTER				"WLC_MDIS_VOTER"
#define WLCFW_VOTER				"WLC_FWUPDATE"
#define EDS_VOTER				"EDS_VOTER"
#define DPLOSS_VOTER				"DPLOSS_VOTER"
#define DD_VOTER				"DD_VOTER"
#define USECASE_INPUT_SUSPEND_VOTER		"USECASE_INPUT_SUSPEND"
#define WLC_RETAIL_VOTER			"WLC_RETAIL_VOTER"

#define GOOGLE_WLC_NOTIFIER_DELAY_MS			100
#define GOOGLE_WLC_PRESENT_CHECK_INTERVAL_MS		1000
#define GOOGLE_WLC_DCIN_RETRY_DELAY_MS			2000
#define GOOGLE_WLC_DEFAULT_STARTUP_UA			100000
#define GOOGLE_WLC_BOOTUP_UA				500000
#define GOOGLE_WLC_CHG_SUSPEND_MW			0
#define GOOGLE_WLC_STARTUP_MW				0
#define GOOGLE_WLC_MPP_HEADROOM_UA			25000
#define GOOGLE_WLC_MPP_RESTRICTED_ICL			400000
#define GOOGLE_WLC_MPP_MAX_POWER			15000
#define GOOGLE_WLC_MPP25_MAX_POWER			25000
#define GOOGLE_WLC_MPP_NPM_MAX_POWER			15000
#define GOOGLE_WLC_EPP_MAX_POWER			10000
#define GOOGLE_WLC_BPP_MAX_POWER			5000
#define GOOGLE_WLC_PREAUTH_RAMP_TARGET			7500
#define GOOGLE_WLC_MPP_RAMP_MAX_RETRY_TIME		10000 /* 10 seconds */
#define GOOGLE_WLC_RAMP_RETRY_INTERVAL			200 /* ms */
#define GOOGLE_WLC_RAMP_DONE_CHECK_NUM			0
#define GOOGLE_WLC_RAMP_SKIP_CHECK_NUM			5
#define GOOGLE_WLC_ICL_RAMP_TIMEOUT_MS			5000
#define GOOGLE_WLC_RAMPDOWN_DECREASE_MULT		2
#define GOOGLE_WLC_DCIN_CHECK_INTERVAL_MS		(1 * 1000)
#define MPP_RAMP_DELAY_MS				(1 * 1000)
#define MPP_RESTRICTED_RETRY_MS				500
#define GOOGLE_WLC_MAX_FOD_NUM				20
#define GOOGLE_WLC_CSP_ENABLE_DELAY_MS			750
#define GOOGLE_WLC_NUM_GPIOS				16
#define GOOGLE_WLC_OUTPUT_ENABLE			1
#define GOOGLE_WLC_SWC_ON_GPIO				2
#define GOOGLE_WLC_ONLINE_SPOOF				5
#define GOOGLE_WLC_DISCONNECT_DEBOUNCE_MS		(2 * 1000)
#define MPP_TX_TIMEOUT_MS				(20 * 1000)
#define I2C_LOG_NUM					128
#define GOOGLE_WLC_FOD_NUM_MAX				16
#define GOOGLE_WLC_RX_ILIM_MAX_UA			1600000
#define TX_ID_STR_LEN					9
#define UEVENT_ENVP_LEN					20
#define NOTIFIER_REGISTER_RETRY_MS			1000
#define DEFAULT_DC_ICL_STEP				25000
#define WLC_SOC_STATS_LEN				101
#define WLC_CHARGE_STATS_TIMEOUT_MS			(10 * 1000)
#define GOOGLE_WLC_MPLA_NUM_MAX				12
#define GOOGLE_WLC_RF_CURR_NUM_MAX			7
#define MDIS_LEVEL_MAX					26
#define MPP25_MIN_POTENTIAL_POWER			20000
#define GOOGLE_WLC_OPFREQ_THRES				300
#define GOOGLE_WLC_CLOAK_DEBOUNCE_MS			1000
#define MPP25_PM_SWITCH_TIMEOUT_MS			(15 * 1000)
#define WLC_DC_DISABLE_SWC_TIMEOUT_MS			(20 * 1000)
#define MPP25_CALIBRATION_TIMEOUT_MS			(5 * 60 * 1000)
#define MPP_DPLOSS_NUM_STEPS				3
#define GOOGLE_WLC_VOUT_MIN				10500
#define GOOGLE_WLC_VOUT_MAX				19000
#define WPC_RCS_TX_MAX_POWER				1
#define WPC_RCS_TX_SYS_PROTECTION			2
#define WPC_RCS_TX_MAX_VOLTAGE				3
#define WLC_DC_LOAD_DECREASE_STEP			20
#define WLC_DC_MAX_SOC					80
#define THERMAL_EDS_SIZE				10
#define GOOGLE_WLC_FOD_CLOAK_TIMEOUT			500
#define WLC_DC_DPLOSS_PARAM_INIT_DELAY			(4 * 1000)
#define WLC_FW_RECHECK_TIMEOUT_MS			(10 * 1000)
#define WLC_FW_CHECK_TIMEOUT_MS				(1 * 1000)
#define MPP25_CAL_ENTER_TIMEOUT				(30 * 1000)
#define MPP25_CAL_RENEGO_TIMEOUT			(10 * 1000)
#define WLC_FWUPDATE_RETRIES_MAX			10
#define WLC_FWUPDATE_SOC_THRESHOLD			10
#define WLC_DISABLE_TIMEOUT				(5 * 1000)
#define GOOGLE_WLC_DC_MAX_VOLT_DEFAULT_MV		19000
#define WLC_DC_MAX_VOUT_DELTA_DEFAULT			80
#define WLC_DC_MAX_POUT_DELTA_DEFAULT			2000
#define AUTH_EDS_INTERVAL_MS				(100 * 1000)
#define FW_EDS_INTERVAL_MS				(60 * 1000)
#define EDS_ICL_LEVEL_MAX				2
#define WLC_PLA_ACK_TIMEOUT_MS				20000
#define DREAM_DEBOUNCE_TIME_S				400
#define GOOGLE_WLC_IOP_FAIL_TIMEOUT_MS			(10 * 1000)
#define GOOGLE_WLC_IOP_FAIL_COUNT_MAX			5
#define WLC_INIT_CHECK_INTERVAL				1000
#define MPP25_HPM_MV					18000
#define MPP25_DPLOSS_STABLE_CHECK_RETRIES		100
#define MPP25_VOUT_STABLE_CHECK_RETRIES			15
#define MPP25_VOUT_STABLE_CHECK_THRESHOLD_MV		100
#define MPP25_DPLOSS_STABLE_CHECK_INTERVAL		1000
#define MPP25_DPLOSS_STABLE_CHECK_THRESHOLD		500
#define MPP25_DPLOSS_STABLE_CHECK_GOAL			2
#define MPP25_DPLOSS_FALLBACK_MIN_STEP1			8000
#define MPP25_DPLOSS_FALLBACK_MAX_STEP1			11000
#define MPP25_DPLOSS_FALLBACK_MIN_STEP2			11000
#define MPP25_DPLOSS_FALLBACK_MAX_STEP2			13000
#define MPP25_DPLOSS_FALLBACK_MIN_STEP3			13000
#define MPP25_DPLOSS_FALLBACK_MAX_STEP3			15500
#define MPP25_DPLOSS_FALLBACK_MIN_STEP4			18000
#define MPP25_DPLOSS_FALLBACK_MAX_STEP4			25000
#define WLC_CHARGER_STARTUP_RETRIES			10
#define WLC_IOUT_VALID_LEVEL_MA				50
#define WLC_CHARGER_STARTUP_CHECK_INTERVAL		1000
#define MPP_NPM_MIN_VOLTAGE				12000
#define MPP_NPM_MAX_VOLTAGE				14000
#define MPP_NPM_MAX_POWER_12V_THRESHOLD			12800 /* 15W/14V*12V, don't overshoot 15W */
#define MPP25_MAX_OUTPUT_VOLTAGE_MV			19000
#define GOOGLE_WLC_IOP_VOUT_MV				9000
#define MPP25_CHECK_DELAY_MS				3000
#define CHARGER_SWITCH_CHECK_TIMEOUT_S			300 /* 5 minutes */
#define GOOGLE_WLC_CHG_STABLE_TIME_MS			(30 * 1000)
#define GOOGLE_WLC_DC_CHG_STABLE_TIME_MS		(60 * 1000)
#define WLC_DC_XCE_THRESH_DEFAULT			1
#define WLC_DC_DPLOSS_CALI_POWER_MIN			15000
#define FSK_MISSING_COUNT_MAX				3
#define GOOGLE_WLC_FSK_MISSING_INT_CHECK_S		2
#define GOOGLE_WLC_CHG_SESSION_CHECK_S			10
#define LIMIT_REASON_RETRY_TIME_MS			(5 * 1000)
#define LIMIT_REASON_RETRY_COUNT			3
#define GOOGLE_WLC_SWC_STATE_CHECK_COUNT		20
#define RCS_LOAD_BACKOFF_IRQ_COUNT			50
#define WLC_DISCONNECT_TIMEOUT				(5 * 1000)
#define GOOGLE_WLC_MPP_NEGO_TIMEOUT_MS			(5 * 60 * 1000)
#define GOOGLE_WLC_XID_DELAY_MS				100
#define GOOGLE_WLC_XID_RETRY_COUNT			10

#define FWUPDATE_DATA_MINSIZE				8
#define FWUPDATE_DATA_MAXSIZE				12
#define FWUPDATE_TYPE_OFFSET				0
#define FWUPDATE_CATEGORY_OFFSET			1
#define FWUPDATE_CUR_MAJOR_OFFSET			2
#define FWUPDATE_CUR_MINOR_OFFSET			3
#define FWUPDATE_MAJOR_OFFSET				4
#define FWUPDATE_MINOR_OFFSET				5
#define FWUPDATE_STATUS_OFFSET				6
#define FWUPDATE_ATTEMPTS_OFFSET			7
#define FWUPDATE_DATA0_OFFSET				8
#define FWUPDATE_DATA1_OFFSET				9
#define FWUPDATE_DATA2_OFFSET				10
#define FWUPDATE_DATA3_OFFSET				11

#define TXID_TYPE_MASK					0xFF000000
#define TXID_TYPE_SHIFT					24
#define TXID_DD_TYPE					0xE0
#define TXID_DD_TYPE2					0xA0
#define IOP_MFG_NUM_MAX					10
#define INCOMPAT_COUNT					5
#define IOP_SOC_MAX					90

#define FSK_MISSING_HISTORY_SIZE			20
#define GOOGLE_WLC_BPP_MISCOMM_POWER			4500
#define BPP_MISCOMM_DISCONNECT_COUNT			3

#define WLC_RETAIL_LOWSOC_THRESHOLD			20
#define WLC_CLOAK_ERROR_COUNT_MAX			5

#define PRESENT_RATELIMIT_INTERVAL			(30 * 60 * HZ)
#define PRESENT_RATELIMIT_BURST				20

#define GOOGLE_WLC_BPP_RAMP_DONE_CHECK_NUM		5
#define GOOGLE_WLC_IOP_BPP_RAMP_DONE_CHECK_NUM		20
#define GOOGLE_WLC_IOP_BPP_RAMP_DONE_CHECK_MS		(1 * 1000)
#define GOOGLE_WLC_RAMP_DONE_ICL_MIN_UA			650000

#define GOOGLE_WLC_READING_MAX_IOUT_MA			10000
#define GOOGLE_WLC_READING_MAX_VOLT_MV			25000
#define GOOGLE_WLC_READING_MAX_FREQ_KHZ			1000
#define GOOGLE_WLC_READING_MAX_TEMP_C			1000

struct wlc_chg_config {
	u32 target_power_mode; /* target power mode */
	u32 charger_type; /* CP or Buck charger */
	u32 mod_mode;
	u32 mod_change_timing;
	u32 init_cur_ua;
	u32 icl_rounding;
	u32 init_volt_uv;
	bool auto_vout;
	u32 icl_step_size_ua;
	u32 max_volt_uv;
	bool mpp25_only; /* only supports mpp25, not mpp15 */
	const char *mpp_mdis_name;
	u32 mpp_mdis_pwr[MDIS_LEVEL_MAX];
	int mpp_mdis_num;
	bool exit_by_ept;

};

struct google_wlc_platform_data {
	struct gpio_desc				*irq_gpio;
	int						irq_int;
	int						irq_hybrid;
	struct gpio_desc				*ap5v_gpio;
	struct gpio_desc				*vdden_gpio;
	struct gpio_desc				*mode_gpio;
	struct gpio_desc				*inhibit_gpio;
	struct gpio_desc				*wcin_inlim_en_gpio;
	struct gpio_desc				*det_gpio;
	struct gpio_desc				*qi_version_gpio;
	struct power_supply				*batt_psy;
	struct regulator				*ext_bst_supply;
	u8						*bpp_fods;
	u8						*epp_fods;
	struct wlc_chg_config				*chg_configs;
	int						bpp_fod_num;
	int						epp_fod_num;
	u8						fod_qf;
	u8						fod_rf;
	u32						soft_ocp_icl;
	bool						support_txid;
	bool						support_epp;
	u8						project_id;
	u8						*mpp_mplas;
	int						mpp_mpla_num;
	u8						*rf_currs;
	int						rf_curr_num;
	u32						bpp_mdis_pwr[MDIS_LEVEL_MAX];
	u32						epp_mdis_pwr[MDIS_LEVEL_MAX];
	u32						mpp_mdis_pwr[MDIS_LEVEL_MAX];
	u32						wlc_dc_mdis_fcc[MDIS_LEVEL_MAX];
	int						bpp_mdis_num;
	int						epp_mdis_num;
	int						mpp_mdis_num;
	int						wlc_dc_mdis_num;
	bool						has_mpp25_wlc_dc;
	bool						has_mpp15_wlc_dc;
	u32						support_mpp25;
	bool						eoc_cloak_en;
	u32						dploss_steps[MPP_DPLOSS_NUM_STEPS];
	u32						dploss_volts[MPP_DPLOSS_NUM_STEPS];
	int						dploss_points_num;
	const char					*bl_name;
	const char					*fw_name;
	u32						wlc_dc_max_voltage;
	u8						mod_depth_max;
	int						mod_soc;
	u32						mpp_eds_icl[EDS_ICL_LEVEL_MAX];
	u32						mpp_eds_soc[EDS_ICL_LEVEL_MAX];
	int						mpp_eds_level_num;
	int						fwupdate_option;
	u32						power_mitigate_threshold;
	int						qispec;
	u32						iop_vout_mv[IOP_MFG_NUM_MAX];
	u16						iop_vout_mfg[IOP_MFG_NUM_MAX];
	int						iop_vout_mfg_num;
	u32						cloak_ping_delay_ms;
	int						chg_configs_num;
	u32						iop_bpp_vout_tolerance;
	bool						no_charger_switch_cloak;
	u32						wlc_dc_xce_thresh;
	u32						wlc_dc_max_vout_delta;
	u32						wlc_dc_max_pout_delta;
	u32						wlc_epp_max_power_mw;
};

enum google_wlc_status {
	GOOGLE_WLC_STATUS_NOT_DETECTED = 0,
	GOOGLE_WLC_STATUS_DETECTED,
	GOOGLE_WLC_STATUS_CHARGING,
	GOOGLE_WLC_STATUS_DC_CHARGING,
	GOOGLE_WLC_STATUS_CLOAK,
	GOOGLE_WLC_STATUS_CLOAK_ENTERING,
	GOOGLE_WLC_STATUS_CLOAK_EXITING,
};

enum google_wlc_chg_stable_type {
	CHG_STABLE_TYPE_NONE = 0,
	CHG_STABLE_TYPE_CHARGING,
	CHG_STABLE_TYPE_DC_CHARGING,
};

static const char * const google_wlc_status_str[] = {
	"Not-Detected", "Detected", "Charging", "DC-Charging",
	"Cloak", "Entering Cloak", "Exiting Cloak"
};

static const char * const google_wlc_fwupdate_state_str[] = {
	"Unknown", "Not start", "In progress", "Success", "Fail"
};

struct icl_loop_status {
	int						load_step;
	int						current_now;
	int						vrect;
	int						vrect_target;
	int						icl_target;
	int						icl_current_vote;
	int						icl_next;
	int						vout;
};

enum sys_op_mode {
	RX_MODE_AC_MISSING = 0,
	RX_MODE_WPC_BPP,
	RX_MODE_WPC_EPP_NEGO,
	RX_MODE_WPC_EPP,
	RX_MODE_WPC_MPP_NEGO,
	RX_MODE_WPC_MPP,
	RX_MODE_WPC_MPP_RESTRICTED,
	RX_MODE_WPC_MPP_CPM,
	RX_MODE_WPC_MPP_NPM,
	RX_MODE_WPC_MPP_LPM,
	RX_MODE_WPC_MPP_HPM,
	RX_MODE_MPP_CLOAK,
	RX_MODE_PDET,
	RX_MODE_UNKNOWN,
};

enum cloak_error_reason {
	CLOAK_EXIT_NORMAL = 0,
	CLOAK_EXIT_FSK_FAIL,
	CLOAK_DET_PING_TIMEOUT,
	CLOAK_PING_TIMEOUT,
	CLOAK_EXIT_PTX_XID_CHECK_FAIL,
	CLOAK_PTX_POWER_DOWN_TIMEOUT,
	CLOAK_PTX_POWER_DOWN_FAILURE,
	CLOAK_PTX_FSK_MISSING,
	CLOAK_PING_FREQ_INCORRECT,
};

enum adapter_type {
	AD_TYPE_WLC = 0,
	AD_TYPE_WPC_BPP = 1,
	AD_TYPE_WPC_EPP = 2,
	AD_TYPE_WPC_MPP = 4,
	AD_TYPE_WPC_MPP25 = 5,
};

static const char * const sys_op_mode_str[] = {
	"AC Missing", "BPP", "EPP Nego", "EPP", "MPP Nego", "MPP", "MPP Restricted",
	"MPP_CPM", "MPP_NPM", "MPP_LPM", "MPP_HPM",
	"Cloak", "PDET", "Unknown"
};

enum icl_loop_state {
	ICL_LOOP_INACTIVE = 0,
	ICL_LOOP_ONGOING,
	ICL_LOOP_DONE,
};

enum icl_loop_mode {
	ICL_LOOP_INTERRUPT = 0,
	ICL_LOOP_TIMER,
};

enum ept_reason {
	EPT_UNKNOWN			= 0,
	EPT_END_OF_CHARGE		= 0x01,
	EPT_INTERNAL_FAULT		= 0x02,
	EPT_OVER_TEMP			= 0x03,
	EPT_OVER_VOLTAGE		= 0x04,
	EPT_OVER_CURRENT		= 0x05,
	EPT_BATTERY_FAILURE		= 0x06,
	EPT_NO_RESPONSE			= 0x08,
	EPT_ABORT_NEGOTIATION		= 0x0A,
	EPT_RESTART_POWER_TRANSFER	= 0x0B,
	EPT_REPING			= 0x0C,
	EPT_NFC_RX			= 0x0D,
	EPT_NFC_TX			= 0x0E,
	EPT_RECALIBRATION		= 0x0F,
	EPT_POWER_MODE_CHANGE		= 0x10,
};

enum wlc_disable_state {
	WLC_NOT_DISABLED,
	WLC_CLOAK_ONLY, /* No inhibit fallback */
	WLC_SOFT_DISABLE, /* Maintain "present" state but fallback to inhibited */
	WLC_HARD_DISABLE,
};

/* From Qi2.2 spec CLOAK packet */
enum cloak_reason {
	CLOAK_GENERIC,
	CLOAK_FORCED,
	CLOAK_THERMAL,
	CLOAK_INSUFFICIENT_POWER,
	CLOAK_COEX,
	CLOAK_EOC,
	CLOAK_TX_INITIATED,
	CLOAK_FOD,
};

enum eds_state {
	EDS_AVAILABLE,
	EDS_SEND,
	EDS_SENT,
	EDS_RECEIVED,
	EDS_RESET,
};

enum align_status {
	ALIGN_CHECKING,
	ALIGN_MOVE,
	ALIGN_CENTERED,
	ALIGN_ERROR,
};

static char *align_status_str[] = {
	"...", "M2C", "OK", "-1"
};

enum wlc_feature {
	WLCF_DISABLE_ALL_FEATURE     = 0x00,
	WLCF_DREAM_ALIGN             = 0x01,
	WLCF_DREAM_DEFEND            = 0x02,
	WLCF_FAST_CHARGE             = 0x04,
	WLCF_CHARGE_15W              = 0x08,
	WLCF_QI_PASSED_FEATURE       = 0x10,
	WLCF_RETAIL_CHARGE           = 0x20,
	WLCF_DREAM_DEFER             = 0x40,
};

enum uevent_source {
	UEVENT_WLC_INIT = 0,
	UEVENT_WLC_OFF,
	UEVENT_WLC_ON,
	UEVENT_FAN,
	UEVENT_FWUPDATE,
	UEVENT_TXSRC,
};

static char *uevent_source_str[] = {
	"N/A", "WLC_OFF", "WLC_ON", "FAN", "FWUPDATE", "TXSRC"
};

enum gpio_mode {
	GPIO_VOL_LOW = 0,
	GPIO_VOL_HIGH,
	GPIO_NO_PULL,
};

static char *mode_gpio_str[] = {
	"GPIO_VOL_LOW", "GPIO_VOL_HIGH", "GPIO_NO_PULL"
};

struct wlc_soc_data {
	ktime_t last_update;
	int elapsed_time;
	int pout_min;
	int pout_max;
	int of_freq;
	int alignment;
	int vrect;
	int iout;
	int die_temp;
	int sys_mode;
	long pout_sum;
};

struct google_wlc_stats {
	ktime_t start_time;
	struct wlc_soc_data soc_data[WLC_SOC_STATS_LEN];
	int adapter_type;
	int cur_soc;
	int volt_conf;
	int cur_conf;
	int of_freq;
	int last_soc;
	u32 adapter_capabilities[5];
	u32 receiver_state[2];
};

enum mpp25_state {
	MPP25_OFF = 0,
	MPP25_CHECKING, /* online=2 received */
	MPP25_PREPARING, /* ramping down */
	MPP25_READY, /* online=2 returned */
	MPP25_DPLOSS_CALIBRATION,
	MPP25_ENTER_HPM,
	MPP25_DPLOSS_CAL4,
	MPP25_ACTIVE,
};
static char *mpp25_state_str[] = {
	"OFF", "Not Ready", "Preparing", "Ready", "DPLoss",
	"Enter HPM", "DPloss Cal4", "Active"
};

enum dploss_cal_event {
	DPLOSS_CAL_NONE,
	DPLOSS_CAL_START,
	DPLOSS_CAL_ENTER,
	DPLOSS_CAL_RESPONSE,
	DPLOSS_CAL_CAPTURE,
	DPLOSS_CAL_COMMIT,
	DPLOSS_CAL_EXIT,
	DPLOSS_CAL_ABORT,
	DPLOSS_CAL_EXTEND,
};

static char *dploss_cal_event_str[] = {
	"None", "Start", "Enter", "Response", "Capture", "Commit", "Exit", "Abort", "Extend"
};

enum mpp_powermode {
	MPP_POWERMODE_NONE = 0,
	MPP_POWERMODE_CONTINUOUS,
	MPP_POWERMODE_LIGHT,
	MPP_POWERMODE_NOMINAL,
	MPP_POWERMODE_HIGH,
	MPP_POWERMODE_DEFAULT,
};
static char *mpp_powermode_str[] = {
	"None", "CPM", "LPM", "NPM", "HPM", "Default"
};

enum charger_type {
	CHARGER_TYPE_BUCK,
	CHARGER_TYPE_SWC,
};

enum mated_q_result {
	MATED_Q_INVALID = 0,
	MATED_Q_NO_FOD,
	MATED_Q_FOD,
	MATED_Q_INCONCLUSIVE,
};

/* From Qi2.0~Qi2.2 ECAP packet */
enum power_limit_reason {
	POWER_LIMIT_NO_LIMIT = 0,
	POWER_LIMIT_POSSIBLE_FO = 2,
	POWER_LIMIT_BROWN_OUT_PROTECTION = 3,
	POWER_LIMIT_OT = 4,
	POWER_LIMIT_OC = 6,
	POWER_LIMIT_MAX_AVAIL_POWER = 7,
	POWER_LIMIT_POWER_MODES = 8,
	POWER_LIMIT_CAL_NOT_MET = 10,
	POWER_LIMIT_CAL_LIMIT = 11,
	POWER_LIMIT_COOLING_CONTROL = 12,
};

/* From Qi2.2 CAL_START_RESPONSE packet */
enum dploss_cal_reject_reason {
	CAL_RESP_NO_ERR,
	CAL_RESP_PLA_SEL,
	CAL_RESP_FOD_REFRESH_SEQ,
	CAL_RESP_FO_DETECTED,
	CAL_RESP_NOT_IN_PWR_TRF,
	CAL_RESP_BUSY,
	CAL_RESP_CAL_MODE_NS,
};

enum ask_mod_mode {
	ASK_MOD_MODE_NEG_CAP_MOD,
	ASK_MOD_MODE_ADAPTIVE_LOAD_MOD,
	ASK_MOD_MODE_POS_CAP_MOD,
	ASK_MOD_MODE_LOAD_MOD,
};

static char *ask_mod_str[] = {
	"Neg Cap", "Load (Adaptive)", "Pos Cap", "Load (No adaptive)"
};

enum ask_mod_timing {
	ASK_MOD_TIMING_EARLY, /* change ask mod before starting charger */
	ASK_MOD_TIMING_LATE, /* change ask mod after starting charger */
	ASK_MOD_TIMING_GPIO, /* control ask mod by virtual gpio */
	ASK_MOD_TIMING_POS_CAP_GPIO, /* start charger with pos cap mod, then control by gpio */
};

enum fw_update_step {
	FW_UPDATE_STEP,
	CRC_VERIFY_STEP,
	READ_REQ_FWVER,
};

enum swc_enable_state {
	SWC_NOT_ENABLED,
	SWC_ENABLED,
	SWC_DISABLED,
};

enum fw_update_option {
	FWUPDATE_CRC_NOTSUPPORT = -1,
	FWUPDATE_DISABLE = 0,
	FWUPDATE_DIFF = 1,
	FWUPDATE_NEW = 2,
	FWUPDATE_FORCE = 3,
	FWUPDATE_FORCE_MANUAL = 4,
};

enum fw_update_status {
	FWUPDATE_STATUS_UNKNOWN = 0,
	FWUPDATE_STATUS_SUCCESS = 1,
	FWUPDATE_ERROR_CRC = 100,
	FWUPDATE_ERROR_I2C = 101,
	FWUPDATE_ERROR_BACKPOWER = 102,
	FWUPDATE_ERROR_BOOTLOADER = 103,
	FWUPDATE_ERROR_FLASH = 104,
};

struct wlc_fw_ver {
	u8 project_id;
	u8 major;
	u8 minor;
	u64 crc;
};

struct wlc_fw_data {
	struct wlc_fw_ver ver;     /* target version */
	struct wlc_fw_ver req_ver; /* only used for READ_REQ_FWVER */
	struct wlc_fw_ver cur_ver;
	int update_option;
	bool update_check_pending;
	bool update_done;
	bool needs_update;
	bool update_support;
	bool erase_fw;
	uint32_t ver_tag;
	int attempts;
	int status;
	int data0;
	int data1;
	int data2;
	int data3;
};

enum eds_type {
	EDS_AUTH = 1,
	EDS_THERMAL,
	EDS_FW_UPDATE,
};

struct powermode_data {
	bool supported;
	int min_volt;
	int max_volt;
	int pot_pwr;
};

struct mode_cap_data {
	struct powermode_data cpm;
	struct powermode_data lpm;
	struct powermode_data npm;
	struct powermode_data hpm;
};

enum fod_cloak_status {
	FOD_CLOAK_NONE = 0,
	FOD_CLOAK_STARTED,
	FOD_CLOAK_ENTERED,
	FOD_CLOAK_RESEND,
	FOD_CLOAK_DONE,
};

enum fw_msg_type {
	FWU_MSG_WLC_TYPE_UNKNOWN = 0,
	FWU_MSG_WLC_TYPE_RX,
	FWU_MSG_WLC_TYPE_TX_START,
	FWU_MSG_WLC_TYPE_TX_STOP,
	FWU_MSG_WLC_TYPE_TX_OFFLINE,
	FWU_MSG_WLC_TYPE_TX_EXECUTE,
	FWU_MSG_WLC_TYPE_CRC_CHECK,
};

enum fw_update_state {
	FWU_STATE_UNKNOWN = 0,
	FWU_STATE_NOT_STARTED,
	FWU_STATE_IN_PROGRESS,
	FWU_STATE_SUCCESS,
	FWU_STATE_FAIL,
};

enum skip_nego_mode {
	SKIP_ALL_BUT_OT = 1,
	SKIP_FO_ONLY,
	FORCE_25W_NEGO,
};

enum icl_target_round_method {
	ICL_TARGET_ROUND_UP_WITH_COMP,
	ICL_TARGET_ROUND_UP,
	ICL_TARGET_ROUND_DOWN,
	ICL_TARGET_ROUND_NEAREST,
};

enum charger_switch_rc {
	CHARGER_SWITCH_OK,
	CHARGER_SWITCH_RESCHEDULE,
	CHARGER_SWITCH_WAIT_AUTH,
	CHARGER_SWITCH_MPP25_NOTSUPP,
};

enum compatibility_type {
	COMPAT_GPP = 1,
	COMPAT_HPP = 2,
	COMPAT_BPP = 3,
	COMPAT_EPP = 4,
	COMPAT_MPP_RESTRICTED = 5,
	COMPAT_MPP = 6,
	COMPAT_MPP25 = 7,
	COMPAT_FORCED_BPP = 127,
	COMPAT_NOT_SUPPORTED = -1,
	COMPAT_LOWPOWER = -2,
};

/* Used for controlling charging by Switch Cap charger */
struct wlc_dc_data {
	bool dploss_param_init_ok;
	bool dploss_param_ok;
	enum swc_enable_state swc_en_state;
	bool swc_on;
	int max_voltage_limit;
};

/* Used for controlling charging by Switch Cap charger */
struct mpp25_data {
	int state;
	bool qi_ver_ok;
	bool mated_q_ok;
	bool nego_pwr_ok;
	bool pot_pwr_ok;
	bool power_limit_reason_ok;
	bool cal_enter_ok;
	bool cal_active;
	bool dploss_cal_ok;
	bool pwrmode_ok;
	bool cal4_ok;
	bool back_npm;
	enum dploss_cal_event last_dploss_event;
	bool dploss_event_success;
	int dploss_step;
	enum fod_cloak_status fod_cloak;
	struct mode_cap_data mode_capabilities;
	int dploss_comp;
	bool setup_done;
};

struct psy_info {
	int online;
	int max_power;
	int voltage_max;
	int current_max;
};

struct wlc_adapter_capabilities_1_fields {
	u16 ptmc;
	u16 qi_id;
};

struct wlc_adapter_capabilities_2_fields {
	u16 nego_power;
	u8 limit_reason;
	u8 mdis_level;
};

struct wlc_adapter_capabilities_4_fields {
	u16 mpp25_state;
	int8_t compatibility;
	uint8_t flag_fsk_fallback_15w : 1;
	uint8_t flag_iop_fallback_bpp : 1;
	uint8_t reserved : 6;
};

struct wlc_receiver_state_0_fields {
	u16 eds_total_count;
	u16 eds_error_count;
};

struct wlc_receiver_state_1_fields {
	u16 disconnect_total_count;
	u16 irq_error_count;
};

struct google_wlc_data {
	struct device				*dev;
	struct i2c_client			*client;
	u16					chip_id;
	struct google_wlc_platform_data		*pdata;
	struct google_wlc_chip			*chip;
	struct power_supply			*wlc_psy;
	struct power_supply			*chgr_psy; /* buck charger */
	struct power_supply			*wlc_dc_psy; /* switch cap charger */
	struct notifier_block			nb;
	#if IS_ENABLED(CONFIG_GPIOLIB)
	struct gpio_chip			gpio;
	#endif
	/* gvotables */
	struct gvotable_election		*dc_icl_votable;
	struct gvotable_election		*wlc_disable_votable;
	struct gvotable_election		*chg_mode_votable;
	struct gvotable_election		*icl_ramp_target_votable;
	struct gvotable_election		*wlc_dc_power_votable;
	struct gvotable_election		*hda_tz_votable;
	struct gvotable_election		*cp_fcc_votable;
	struct gvotable_election		*dc_avail_votable;
	struct gvotable_election		*msc_last_votable;
	struct gvotable_election		*csi_status_votable;
	struct gvotable_election		*pri_chg_min_pwr_votable;
	/* mutexes */
	struct mutex				io_lock;
	struct mutex				status_lock;
	struct mutex				csp_status_lock;
	struct mutex				eds_lock;
	struct mutex				fwupdate_lock;
	struct mutex				cmd_lock;
	struct mutex				stats_lock;
	struct mutex				completion_lock;
	struct mutex				ext_boost_lock;
	struct mutex				vdden_lock;
	/* delayed works */
	struct delayed_work			psy_notifier_work;
	struct delayed_work			icl_ramp_work;
	struct delayed_work			icl_ramp_timeout_work;
	struct delayed_work			mpp_nego_timeout_work;
	struct delayed_work			present_check_work;
	struct delayed_work			soc_work;
	struct delayed_work			csp_enable_work;
	struct delayed_work			disconnect_work;
	struct delayed_work			disable_work;
	struct delayed_work			icl_ramp_target_work;
	struct delayed_work			dc_power_limit_work;
	struct delayed_work			tx_work;
	struct delayed_work			register_usecase_work;
	struct delayed_work			charge_stats_hda_work;
	struct delayed_work			charging_init_work;
	struct delayed_work			mpp25_init_work;
	struct delayed_work			mpp25_timeout_work;
	struct delayed_work			wlc_fw_update_work;
	struct delayed_work			eds_work;
	struct delayed_work			pla_ack_timeout_work;
	struct delayed_work			check_iop_timeout_work;
	struct delayed_work			set_iop_vout_work;
	struct delayed_work			charger_switch_ready_work;
	struct delayed_work			chg_stable_work;
	struct delayed_work			limit_reason_work;
	struct delayed_work			cloak_err_work;
	struct delayed_work			vdd_err_work;
	struct delayed_work			xid_work;
	/* buck charger icl ramp related */
	int					icl_now;
	int					icl_ramp_target_mw;
	int					icl_ramp_disable;
	int					ramp_done_check_count;
	int					ramp_skip_check_count;
	enum icl_loop_state			icl_loop_state;
	enum icl_loop_mode			icl_loop_mode;
	int					icl_accuracy_comp;
	bool					force_icl_decrease;
	/* wake sources */
	struct wakeup_source			*icl_ramp_ws;
	struct wakeup_source			*icl_ramp_timeout_ws;
	struct wakeup_source			*notifier_ws;
	struct wakeup_source			*presence_ws;
	struct wakeup_source			*disconnect_ws;
	struct wakeup_source			*fwupdate_ws;
	struct wakeup_source			*mpp25_ws;
	struct wakeup_source			*init_charging_ws;
	struct wakeup_source			*wlc_disable_ws;
	struct wakeup_source			*icl_target_ws;
	struct wakeup_source			*eds_tx_ws;
	struct wakeup_source			*limit_reason_ws;
	struct wakeup_source			*xid_ws;
	struct wakeup_source			*chg_stable_ws;
	struct logbuffer			*log;
	struct logbuffer			*fw_log;
	struct dentry				*debug_entry;
	/* status or charge session data */
	enum google_wlc_status			status;
	enum sys_op_mode			mode;
	int					gpio_mode;
	int					disconnect_count;
	int					last_opfreq;
	struct google_wlc_stats			chg_data;
	int					online;
	int					last_capacity;
	bool					send_csp;
	int					disable_state;
	bool					output_enable;
	int					iout_multiplier;
	bool					usb_connected;
	u32					tx_id;
	u32					last_tx_id;
	u8					tx_id_str[TX_ID_STR_LEN];
	int					wlc_charge_enabled;
	struct wlc_dc_data			dc_data;
	struct wlc_chg_config			*output_chg_config;
	int					chg_config_index;
	struct mpp25_data			mpp25;
	bool					mpp25_disabled; /* disable for rest of session */
	bool					mpp15_wlc_dc_ready;
	int					cloak_enter_reason;
	int					usecase;
	bool					usecase_ready;
	struct completion			cloak_completion;
	struct completion			cal_enter_done;
	struct completion			cal_renego_done;
	struct completion			icl_ramp_done;
	struct completion			pwrmode_switch;
	struct completion			charger_disconnect;
	bool					wait_for_cloak;
	bool					wait_for_exit_cloak;
	int					wait_for_mode;
	bool					pwrmode_switch_initial_done;
	bool					pwrmode_switch_renego_done;
	int					inlim_setting;
	bool					inlim_available;
	bool					wait_for_cal_enter;
	bool					wait_for_cal_renego;
	bool					wait_for_icl_ramp;
	u32					mitigate_threshold;
	u32					trigger_dd;
	ktime_t					online_at;
	bool					vout_ready;
	struct psy_info				last_psy_info;
	bool					ext_boost_enabled;
	bool					iop_bpp;
	int					compatibility;
	int					ask_mod_mode;
	bool					charger_started;
	bool					mpp_initialized;
	bool					charging_initialized;
	bool					mpp_restricted_set;
	u8					limit_reason;
	u8					limit_reason_retry_count;
	int					mdis_pwr_limit;
	bool					wlc_dc_to_ramp;
	u16					qi_id;
	bool					charger_switch_check_finished;
	int					rcs_backoff_count;
	/* eds or auth related */
	u8					*tx_buf;
	u8					*rx_buf;
	u8					rx_thermal_buf[THERMAL_EDS_SIZE];
	bool					tx_done;
	bool					tx_busy;
	bool					rx_done;
	u16					tx_len;
	u16					rx_len;
	u16					rx_thermal_len;
	int					eds_state;
	int					eds_stream;
	int					nego_power;
	u64					feature;
	u16					eds_error_count;
	u16					eds_total_count;
	bool					eds_event;
	bool					wpc_auth_type;
	bool					run_fwupdate;
	int					last_charging_uevent;
	ktime_t					wlc_offline_last;
	u32					wlc_offline_count;
	ktime_t					fsk_missing_last;
	u32					fsk_missing_count;
	u32					session_txid;
	ktime_t					fsk_missing_history[FSK_MISSING_HISTORY_SIZE];
	int					history_index;
	bool					shutdown_notify;
	/* debug or fwupdate related values */
	bool					force_bpp;
	bool					manual_force_bpp;
	int					online_disable;
	bool					online_disable_override;
	bool					fwupdate_mode;
	u32					addr_fw;
	u16					addr;
	u16					count;
	bool					online_spoof;
	int					de_fod_n;
	u8					de_fod[GOOGLE_WLC_FOD_NUM_MAX];
	int					auth_disable;
	int					enable_eoc_cloak;
	int					enable_i2c_debug;
	char					*i2c_debug_buf;
	int					fast_mpp_ramp;
	int					de_mpla_n;
	u8					de_mpla[GOOGLE_WLC_MPLA_NUM_MAX];
	int					de_rf_curr_n;
	u8					de_rf_curr[GOOGLE_WLC_RF_CURR_NUM_MAX];
	int					mdis_level;
	int					pri_chg_mdis_lvl;
	int					skip_nego;
	int					pkt_ready;
	int					wlc_dc_skip_qi_ver;
	int					wlc_dc_skip_mated_q;
	int					wlc_dc_skip_pot_pwr;
	int					wlc_dc_skip_pwr_limit_check;
	u8					wlc_dc_debug_powermode;
	int					wlc_dc_skip_powermode;
	int					wlc_dc_skip_dploss;
	int					wlc_dc_skip_dploss_param;
	u8					wlc_dc_debug_gain_linear;
	int					mpp25_dploss_cal4;
	int					mpp25_dploss_cal4_volt;
	int					fsk_log;
	u16					disconnect_total_count;
	u16					irq_error_count;
	u16					irq_load_decrease_count;
	u32					qi22_write_mpla2;
	u16					mpla2_alpha_fm_itx;
	bool					stdby_mode;
	u32					mpla2_alpha_fm_vrect;
	u16					mpla2_alpha_fm_irect;
	u32					wlc_dc_max_vout_delta;
	u32					wlc_dc_max_pout_delta;
	u32					icl_target_rounding_override;
	u32					dploss_thresh;
	u32					no_charger_switch_cloak;
	u32					wlc_dc_xce_thresh;
	bool					debug_force_crc_check_only;
	u32					iop_fail_count_max;
	u32					cloak_err_count;
	u32					cloak_err_count_max;
	/* end */
	bool					boot_on_wlc;
	struct wlc_fw_data			fw_data;
	u8					debug_cal_power;
	int					ad_type;
	int					vrect_count;
	ktime_t					last_vrect;
	ktime_t					this_vrect;
	u8					fwupdate_state;
	bool					mod_enable;
	bool					wait_for_dd;
	bool					lower_qi_version;
	u32					cloak_ping_delay_ms;
	bool					bpp_ramp_done_check;
	u32					bpp_ramp_done_target;
	u32					mpp_mdis_chg_idx;
	const char				*mdis_name;
	int					mdis_limit;
	bool					mode_ready;
	u8					product_id;
	bool					txsrc;
	u16					ptmc;
	u8					xid_retry_count;
	bool					retail_power_limit;
	int					chg_stable_type;
};

struct google_wlc_bits {
	u8				prop_received;
	u8				sadt_received;
	u8				sadt_sent;
	u8				stat_vout; /* VOUT state change */
	u8				stat_vrect; /* VRECT state change*/
	u8				operation_mode;	/* Mode change */
	u8				over_voltage;
	u8				over_current;
	u8				over_temperature;
	u8				sadt_error;
	u8				power_adjust;
	u8				load_decrease_alert;
	u8				load_increase_alert;
	u8				fsk_timeout;
	u8				fsk_received;
	u8				dploss_cal_success;
	u8				dploss_cal_error;
	u8				rcs;
	u8				dploss_param_match;
	u8				dploss_param_error;
	u8				dploss_cal_retry;
	u8				fsk_missing;
	u8				dynamic_mod;
	u8				cloak_error;
	u8				vdd_err;
	u8				sys_mode;
};

struct google_wlc_packet {
	u8				header;
	u8				cmd;
	u8				data[128];
};

struct google_wlc_chip {

	int reg_chipid;
	int reg_vout;
	int reg_vrect;
	int reg_iout;
	int reg_status;
	int reg_fw_major;
	int reg_fw_minor;
	int reg_die_temp;
	int reg_op_freq;
	int reg_fod_start;
	int reg_eds_recv_size;
	int reg_eds_recv_buf;
	int reg_eds_send_size;
	int reg_eds_send_buf;
	int reg_adt_err;
	int reg_eds_status;
	int reg_eds_stream;
	u8 bit_eds_status_busy;
	u8 val_eds_status_busy;
	u8 val_eds_stream_fwupdate;
	int num_fods;
	int reg_fod_qf;
	int reg_fod_rf;
	int reg_ptmc_id;
	int reg_txid_buf;
	int reg_qi_version;
	int reg_tx_rcs;
	int reg_mpla_start;
	int num_mplas;
	int reg_rf_curr_start;
	int num_rf_currs;
	int reg_limit_rsn;
	int reg_alpha_fm_itx;
	int reg_alpha_fm_vrect;
	int reg_alpha_fm_irect;
	size_t rx_buf_size;
	size_t tx_buf_size;
	struct regmap *regmap;
	struct regmap *fw_regmap;
	int val_default_vout;
	int reg_qi_id;

	int (*reg_read_n)(struct google_wlc_data *chgr, unsigned int reg, void *buf, size_t n);
	int (*reg_read_8)(struct google_wlc_data *chgr, u16 reg, u8 *val);
	int (*reg_read_16)(struct google_wlc_data *chgr, u16 reg, u16 *val);
	int (*reg_read_adc)(struct google_wlc_data *chgr, u16 reg, u16 *val);
	int (*reg_write_n)(struct google_wlc_data *charger, u16 reg, const void *buf, size_t n);
	int (*reg_write_8)(struct google_wlc_data *charger, u16 reg, u8 val);
	int (*reg_write_16)(struct google_wlc_data *charger, u16 reg, u16 val);

	bool (*chip_check_id)(struct google_wlc_data *chgr);
	int (*chip_get_vout)(struct google_wlc_data *chgr, u32 *mv);
	int (*chip_get_vrect)(struct google_wlc_data *chgr, u32 *mv);
	int (*chip_get_iout)(struct google_wlc_data *chgr, u32 *ma);
	int (*chip_get_temp)(struct google_wlc_data *chgr, u32 *millic);
	int (*chip_get_opfreq)(struct google_wlc_data *chgr, u32 *khz);
	int (*chip_get_status)(struct google_wlc_data *chgr, u16 *status);
	int (*chip_get_status_fields)(struct google_wlc_data *chgr,
				      struct google_wlc_bits *status_fields);
	int (*chip_recv_eds)(struct google_wlc_data *chgr, size_t *len, u8 *stream);
	int (*chip_send_eds)(struct google_wlc_data *chgr, u8 data[], size_t len, u8 type);
	int (*chip_get_adt_err)(struct google_wlc_data *chgr, u8 *err);
	int (*chip_check_eds_status)(struct google_wlc_data *chgr);
	int (*chip_get_load_step)(struct google_wlc_data *chgr, s32 *ua);
	int (*chip_get_ptmc_id)(struct google_wlc_data *chgr, u16 *id);
	int (*chip_get_tx_qi_ver)(struct google_wlc_data *chgr, u8 *ver);
	int (*chip_get_tx_rcs)(struct google_wlc_data *chgr, u8 *rcs);
	int (*chip_get_tx_kest)(struct google_wlc_data *chgr, u32 *kest);
	int (*chip_get_project_id)(struct google_wlc_data *chgr, u8 *pid);
	int (*chip_get_limit_rsn)(struct google_wlc_data *chgr, u8 *reason);
	const char *(*chip_get_txid_str)(struct google_wlc_data *chgr);
	int (*chip_set_qi_id)(struct google_wlc_data *chgr, u16 qi_id);

	/* Functions that should be implemented individually by chip */
	int (*chip_get_vout_set)(struct google_wlc_data *chgr, u32 *mv);
	int (*chip_set_vout)(struct google_wlc_data *chgr, u32 mv);
	int (*chip_get_vrect_target)(struct google_wlc_data *chgr, u32 *mv);
	int (*chip_get_sys_mode)(struct google_wlc_data *chgr, u8 *mode);
	int (*chip_add_info_string)(struct google_wlc_data *chgr, char *buf);
	int (*chip_get_interrupts)(struct google_wlc_data *chgr, u32 *int_val,
				   struct google_wlc_bits *int_fields);
	int (*chip_clear_interrupts)(struct google_wlc_data *chgr, u32 int_val);
	int (*chip_enable_interrupts)(struct google_wlc_data *chgr);
	int (*chip_set_cloak_mode)(struct google_wlc_data *chgr, bool enable, u8 reason);
	int (*chip_send_csp)(struct google_wlc_data *chgr);
	int (*chip_send_ept)(struct google_wlc_data *chgr, enum ept_reason reason);
	int (*chip_write_fod)(struct google_wlc_data *chgr, int sys_mode);
	int (*chip_get_cloak_reason)(struct google_wlc_data *chgr, u8 *reason);
	int (*chip_get_cloak_exit_reason)(struct google_wlc_data *chgr, u8 *reason);
	int (*chip_send_sadt)(struct google_wlc_data *chgr, u8 stream);
	int (*chip_eds_reset)(struct google_wlc_data *chgr);
	int (*chip_get_negotiated_power)(struct google_wlc_data *chgr, u32 *mw);
	int (*chip_get_potential_power)(struct google_wlc_data *chgr, u32 *mw);
	int (*chip_enable_load_increase)(struct google_wlc_data *chgr, bool enable);
	int (*chip_fw_reg_read_n)(struct google_wlc_data *chgr, unsigned int reg,
				  char *buf, size_t n);
	int (*chip_fw_reg_write_n)(struct google_wlc_data *chgr, u32 reg,
				   const char *buf, size_t n);
	int (*chip_get_mpp_xid)(struct google_wlc_data *chgr, u32 *device_id, u32 *mfg_rsvd_id,
				u32 *unique_id, u8 *product_id);
	int (*chip_write_mpla)(struct google_wlc_data *chgr);
	int (*chip_write_rf_curr)(struct google_wlc_data *chgr);
	int (*chip_send_packet)(struct google_wlc_data *chgr, struct google_wlc_packet packet);
	int (*chip_get_packet)(struct google_wlc_data *chgr, struct google_wlc_packet *packet,
			       size_t *len);
	int (*chip_get_mated_q)(struct google_wlc_data *chgr, u8 *res);
	int (*chip_set_mod_mode)(struct google_wlc_data *chgr, enum ask_mod_mode mode);
	int (*chip_enable_auto_vout)(struct google_wlc_data *chgr, bool enable);
	int (*chip_set_mpp_powermode)(struct google_wlc_data *chgr, enum mpp_powermode powermode,
				      bool preserve_session);
	int (*chip_write_poweron_params)(struct google_wlc_data *chgr);
	int (*chip_do_dploss_event)(struct google_wlc_data *chgr, enum dploss_cal_event event);
	int (*chip_fwupdate)(struct google_wlc_data *chgr, int step);
	int (*chip_get_vinv)(struct google_wlc_data *chgr, u32 *mv);
	int (*chip_get_mode_capabilities)(struct google_wlc_data *chgr, struct mode_cap_data *cap);
	int (*chip_set_dynamic_mod)(struct google_wlc_data *chgr);
	int (*chip_get_xce)(struct google_wlc_data *chgr, s8 *xce);
};

int google_wlc_chip_init(struct google_wlc_data *charger);

size_t google_wlc_hex_str(const u8 *data, size_t len, char *buf, size_t max_buf, bool msbfirst);

bool google_wlc_is_present(const struct google_wlc_data *charger);

bool mode_is_mpp(int mode);

#define MA_TO_UA(ma)((ma) * 1000)
#define UA_TO_MA(ua) ((ua) / 1000)
#define MV_TO_UV(mv) ((mv) * 1000)
#define UV_TO_MV(uv) ((uv) / 1000)
#define KHZ_TO_HZ(khz) ((khz) * 1000)
#define HZ_TO_KHZ(khz) ((khz) / 1000)
#define C_TO_MILLIC(c) ((c) * 1000)
#define MILLIC_TO_C(mc) ((mc) / 1000)
#define MILLIC_TO_DECIC(mc) ((mc) / 100)


#define logbuffer_prlog(p, fmt, ...)     \
	gbms_logbuffer_prlog(p, LOGLEVEL_INFO, 0, LOGLEVEL_DEBUG, fmt, ##__VA_ARGS__)

#define logbuffer_devlog(p, dev, fmt, ...)     \
	gbms_logbuffer_devlog(p, dev, LOGLEVEL_INFO, 0, LOGLEVEL_DEBUG, fmt, ##__VA_ARGS__)

#define DEBUG_I2C_LOG(chgr, ...)  do {	      \
	if ((chgr)->enable_i2c_debug)	      \
		logbuffer_devlog((chgr)->fw_log, (chgr)->dev, __VA_ARGS__);	  \
} while (0)
#endif  // _ GOOGLE_WLC_H_
