/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * On-device Power Monitor driver.
 *
 * Copyright 2023-2024 Google LLC
 */

#ifndef __GOOGLE_ODPM_H__
#define __GOOGLE_ODPM_H__

#include <linux/platform_device.h>
#include <linux/types.h>

#include <soc/google/goog_mba_cpm_iface.h>

#define MBA_CLIENT_TX_TOUT 3000
#define MBA_REQUEST_TIMEOUT 3000

#define ODPM_CHANNEL_NUM 32
#define ODPM_CHANNEL_SIZE 8
#define ODPM_CHANNEL_M_MIN 0
#define ODPM_CHANNEL_M_MAX 15
#define ODPM_CHANNEL_S_MIN 41
#define ODPM_CHANNEL_S_MAX 56
#define ODPM_CHANNEL_OFFSET (ODPM_CHANNEL_S_MIN - ODPM_CHANNEL_M_MAX - 1)
#define UHZ_PER_HZ 1000000
#define ODPM_SAMPLING_FREQ_CHAR_LEN_MAX 20

#define ODPM_RAIL_NAME_STR_LEN_MAX 49

#define ODPM_VNODE_NUM 11

// Shared Memory Table (unit:byte)
//     [0:7]   (8 bytes)  VERSION
//   [8:263] (256 bytes)  CHANNEL
// [264:279]  (16 bytes)  SAMPLE_COUNT
#define SHARED_VERSION_OFFSET 0
#define SHARED_VERSION_SIZE 8
#define SHARED_CHANNEL_OFFSET (SHARED_VERSION_OFFSET + SHARED_VERSION_SIZE)
#define SHARED_CHANNEL_SIZE 256
#define SHARED_SAMPLE_M_INT_OFFSET (SHARED_CHANNEL_OFFSET + SHARED_CHANNEL_SIZE)
#define SHARED_SAMPLE_SIZE 4
#define SHARED_SAMPLE_M_EXT_OFFSET (SHARED_SAMPLE_M_INT_OFFSET + SHARED_SAMPLE_SIZE)
#define SHARED_SAMPLE_S_INT_OFFSET (SHARED_SAMPLE_M_EXT_OFFSET + SHARED_SAMPLE_SIZE)
#define SHARED_SAMPLE_S_EXT_OFFSET (SHARED_SAMPLE_S_INT_OFFSET + SHARED_SAMPLE_SIZE)

#define CHANNEL_ENABLED 1
#define MEASUREMENT_ENABLED 1
#define GPADC_DATA_CLR_VAL 1 /* Clear conversion data only */

//TODO(b/414533162): Make polling interval configurable via sysfs
#define ACC_READ_POLLING_MIN_US 10000
#define ACC_READ_POLLING_MAX_US 11000
#define ACC_READ_TIMEOUT_MS 100
#define AVG_READ_INTERVAL_MS 1
#define TIME_ALLOWED_TO_UPDATE_MS 0 // disable the rate limit
#define DATA_SCALAR 1000000
#define DATA_RIGHT_SHIFTER 16
#define DATA_VOLTAGE_FACTOR 6000000
#define DATA_CURRENT_FACTOR 20000000
#define DATA_CURRENT_LDO_FACTOR 2500000
#define DATA_POWER_FACTOR 30000000
#define DATA_POWER_LDO_FACTOR 3750000

#define MAX_CPM_READING_LENGTH 7

static char s_last_cpm_reading[MAX_CPM_READING_LENGTH] = {'\0'};

enum odpm_rail_type {
	ODPM_RAIL_TYPE_LDO,
	ODPM_RAIL_TYPE_BUCK,
	ODPM_RAIL_TYPE_SHUNT,
};

enum odpm_sampling_rate_type {
	ODPM_SAMPLING_RATE_INTERNAL,
	ODPM_SAMPLING_RATE_EXTERNAL,
	ODPM_SAMPLING_RATE_ALL,
};

enum odpm_pmic_type {
	PMIC_MAIN,
	PMIC_SUB,
	PMIC_INVALID,
};

struct odpm_rail_data {
	/* Config */
	const char *name;
	const char *regulator_name;
	const char *schematic_name;
	const char *subsystem_name;
	enum odpm_pmic_type pmic_type;
	enum odpm_rail_type rail_type;
	/* The shunt resistor register is utilized for external rail only. */
	u32 shunt_res_reg;
	u32 power_coefficient;

	/* Data */
	u64 acc_power;
	u64 acc_voltage;
	u64 acc_current;
	ktime_t measurement_stop_ms;
	ktime_t measurement_start_ms;
};

struct odpm_chip {
	/* Config */
	const char *name;
	int hw_rev;
	u32 max_refresh_time_ms;

	int num_rails;
	struct odpm_rail_data *rails;

	u32 *sampling_rate_int_uhz;
	int sampling_rate_int_count;
	u32 *sampling_rate_ext_uhz;
	int sampling_rate_ext_count;

	u32 sample_count_m_int;
	u32 sample_count_m_ext;
	u32 sample_count_s_int;
	u32 sample_count_s_ext;

	/* Data */
	u64 acc_timestamp_ms;
	int int_sampling_rate_index;
	int ext_sampling_rate_index;

	bool rx_ext_config_confirmation;
	bool acc_data_ready;
	bool acc_transfer_started;
};

struct odpm_channel_data {
	/* The index of PMIC telemetry channel */
	int telem_channel_idx;
	enum odpm_pmic_type pmic_type;
	bool external;
	/* The index of rail that this channel is monitoring */
	int rail_idx;
	bool enabled;

	u64 data_read;
	u64 debug_stats;
};

struct mba_send_node {
	u32 type;
	u32 value0;
	u32 value1;
	struct list_head list;
};

struct odpm_virt_node {
	const char *node_name;
	u32 *telem_channel_idx;
	int telem_channel_idx_count;
	u64 total_acc_power;
};

/**
 * dynamic struct google_odpm
 */
struct google_odpm {
	struct device *dev;
	struct odpm_chip chip;
	struct mutex lock; /* Global HW lock */
	bool config_cpl; /* A flag to indicate the configuration is complete */

	struct odpm_channel_data channels[ODPM_CHANNEL_NUM];
	void __iomem *shared_memory_base;
	u32 shared_memory_address;
	u32 shared_memory_size;
	u32 protocol_version;

	struct cpm_iface_client *client;
	struct workqueue_struct *wq;
	struct work_struct mba_send_work;
	spinlock_t mba_send_list_lock; /* For mba send list */
	struct list_head mba_send_list;

	u32 remote_ch;
	bool ready;

	/*
	 * The update rate limit for accumulated power in milliseconds.
	 * A value of 0 disables the feature.
	 */
	u32 rate_limit_ms;

	bool   support_virt_nodes;
	struct odpm_virt_node virt_nodes[ODPM_VNODE_NUM];
};

struct odpm_rail_energy {
	/* Config */
	const char *regulator_name;
	const char *schematic_name;
	const char *subsystem_name;
	enum odpm_pmic_type pmic_type;
	enum odpm_rail_type rail_type;

	/* Data */
	u64 acc_energy;
	u64 timestamp_ms;
};

enum odpm_mba_msg_type {
	ODPM_MBA_MSG_TYPE_ADDR,
	ODPM_MBA_MSG_TYPE_MEAS_MODE,
	ODPM_MBA_MSG_TYPE_SAMPLING_RATE_IDX,
	ODPM_MBA_MSG_TYPE_EXT_SAMPLING_RATE_IDX,
	ODPM_MBA_MSG_TYPE_CHANNEL_ENABLED,
	ODPM_MBA_MSG_TYPE_CHANNEL_RAILS,
	ODPM_MBA_MSG_TYPE_AVG_COEFF,
	ODPM_MBA_MSG_TYPE_DATA,
	ODPM_MBA_MSG_TYPE_DEBUG,
	ODPM_MBA_MSG_TYPE_SAMPLING_MODE,
	ODPM_MBA_MSG_TYPE_ACC_MODE,
	ODPM_MBA_MSG_TYPE_AVG_MODE,
	ODPM_MBA_MSG_TYPE_MEAS_SWITCH,
	ODPM_MBA_MSG_TYPE_RESET_TELEM,
	ODPM_MBA_MSG_TYPE_SHUNT_RES,
	ODPM_MBA_MSG_TYPE_PMIC_TRANS,
};

enum odpm_sampling_mode_type {
	ODPM_SAMPLING_ONESHOT = 0,
	ODPM_SAMPLING_CONTINUOUS,
};

enum odpm_data_mode_type {
	ODPM_INST_MODE,
	ODPM_ACC_MODE,
	ODPM_AVG_MODE,
	ODPM_DEBUG_MODE,
};

enum odpm_data_type {
	ODPM_VOLTAGE,
	ODPM_CURRENT,
	ODPM_POWER,
};

enum odpm_acc_modes {
	ODPM_ACC_POW = 0,
	ODPM_ACC_CURR,
	ODPM_ACC_MAX,
};

enum odpm_avg_modes {
	ODPM_AVG_POW = 0,
	ODPM_AVG_CURR,
	ODPM_AVG_MAX,
};

enum odpm_trans_type {
	ODPM_TRANS_AVG = 1,
	ODPM_TRANS_ACC,
	ODPM_TRANS_BOTH,
};

struct iio_dev *google_get_odpm_iio_dev(void);
int inst_read(struct device *dev, int data_type);

/**
 * google_odpm_get_rail_energy() - Get the accumulated energy data for all rails.
 *
 * This function retrieves the latest accumulated energy measurements for each
 * configured ODPM rail. It might take less than 100 ms to get the data from the PMIC.
 *
 * Return: A pointer to a constant array of struct odpm_rail_energy containing
 *         the energy data, or NULL on error.
 */
const struct odpm_rail_energy *google_odpm_get_rail_energy(void);

#endif // __GOOGLE_ODPM_H__
