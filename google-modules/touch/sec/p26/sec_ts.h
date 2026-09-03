/* drivers/input/touchscreen/sec_ts.h
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 * http://www.samsungsemi.com/
 *
 * Core file for Samsung TSC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SEC_TS_H__
#define __SEC_TS_H__

#include <linux/completion.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#if defined(I3C_INTERFACE)
#include <linux/i3c/device.h>
#include <linux/i3c/master.h>
#endif
#if defined(I2C_INTERFACE)
#include <linux/i2c.h>
#endif
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include "sec_cmd.h"
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#ifdef CONFIG_SEC_SYSFS
#include <linux/sec_sysfs.h>
#endif

#ifdef CONFIG_INPUT_BOOSTER
#include <linux/input/input_booster.h>
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_TBN)
#include <touch_bus_negotiator.h>
#endif

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define SEC_TS_NAME		"sec_ts"
#define SEC_TS_DEVICE_NAME	"SEC_TS"

#undef SEC_TS_DEBUG_IO
#undef USE_OPEN_CLOSE
#undef USE_RESET_DURING_POWER_ON
#undef USE_RESET_EXIT_LPM
#undef USE_POR_AFTER_I2C_RETRY
#undef USE_POR_AFTER_SPI_RETRY
#undef USER_OPEN_DWORK
#undef PAT_CONTROL
#undef USE_CHARGER_WORK
#undef USE_STIM_PAD
#undef USE_SPEC_CHECK

#if defined(USE_RESET_DURING_POWER_ON) || defined(USE_POR_AFTER_I2C_RETRY) || \
    defined(USE_RESET_EXIT_LPM) || defined(USE_POR_AFTER_SPI_RETRY)
#define USE_POWER_RESET_WORK
#endif

// #define I3C_INTERFACE
// #define I2C_INTERFACE

#if defined(I3C_INTERFACE)
/* i3c */
#define I3C_PRIV_XFER_READ true
#define I3C_PRIV_XFER_WRITE false
#define I3C_MIF_PROTOCOL
#if defined(I3C_MIF_PROTOCOL)
#define SEC_TS_I3C_HEADER_SIZE	3
#define SEC_TS_I3C_MIF_RX_IDX 4
#define SEC_TS_I3C_MIF_TX_IDX 5
#define SEC_TS_I3C_MIF_RX_BUF 6
#define SEC_TS_I3C_MIF_TX_BUF 7
#else
#define SEC_TS_I3C_HEADER_SIZE	8
#endif
#elif defined(I2C_INTERFACE)
/* i2c */
#else
/* spi */
#define SPI_CLOCK_FREQ			10000000
#define SPI_DELAY_CS			10
#define SEC_TS_SPI_HEADER_SIZE		8
#endif

#define MAX_TX_NUM 64
#define MAX_RX_NUM 64

#define MAX_EVENT_COUNT 64

#define TOUCH_RESET_DWORK_TIME		10

#define MASK_1_BITS			0x0001
#define MASK_2_BITS			0x0003
#define MASK_3_BITS			0x0007
#define MASK_4_BITS			0x000F
#define MASK_5_BITS			0x001F
#define MASK_6_BITS			0x003F
#define MASK_7_BITS			0x007F
#define MASK_8_BITS			0x00FF

/* support feature */

#define TYPE_STATUS_EVENT_CMD_DRIVEN	0
#define TYPE_STATUS_EVENT_ERR		1
#define TYPE_STATUS_EVENT_INFO		2
#define TYPE_STATUS_EVENT_USER_INPUT	3
#define TYPE_STATUS_EVENT_HEATMAP_INFO	4
#define TYPE_STATUS_EVENT_CUSTOMLIB_INFO	6
#define TYPE_STATUS_EVENT_VENDOR_INFO	7
#define TYPE_STATUS_CODE_SAR	0x28

#define BIT_STATUS_EVENT_CMD_DRIVEN(a)	(a << TYPE_STATUS_EVENT_CMD_DRIVEN)
#define BIT_STATUS_EVENT_ERR(a)		(a << TYPE_STATUS_EVENT_ERR)
#define BIT_STATUS_EVENT_INFO(a)	(a << TYPE_STATUS_EVENT_INFO)
#define BIT_STATUS_EVENT_USER_INPUT(a)	(a << TYPE_STATUS_EVENT_USER_INPUT)
#define BIT_STATUS_EVENT_VENDOR_INFO(a)	(a << TYPE_STATUS_EVENT_VENDOR_INFO)

#define DO_FW_CHECKSUM			(1 << 0)
#define DO_PARA_CHECKSUM		(1 << 1)
#define MAX_SUPPORT_TOUCH_COUNT		10
#define MAX_SUPPORT_HOVER_COUNT		0

#define SEC_TS_EVENTID_HOVER		10

#define SEC_TS_DEFAULT_FW_NAME		"sec_hero.fw"
#define SEC_TS_DEFAULT_BL_NAME		"tsp_sec/s6smc41_blupdate_img_REL.bin"
#define SEC_TS_DEFAULT_PARA_NAME	"tsp_sec/s6smc41_para_REL_DGA0_V0106_150114_193317.bin"
#define SEC_TS_DEFAULT_FFU_FW		"ffu_tsp.bin"
#define SEC_TS_MAX_FW_PATH		64
#define SEC_TS_MAX_FW_NAME		64
#define SEC_TS_FW_BLK_SIZE_MAX		(256)
#define SEC_TS_FW_BLK_SIZE_DEFAULT	(256)
#define SEC_TS_FW_WRITE_DELAY		(1)
#define SEC_TS_FW_HEADER_SIGN		0x53494654
#define SEC_TS_FW_CHUNK_SIGN		0x53434654

#undef SEC_TS_FW_UPDATE_ON_PROBE
#define SEC_TS_FW_UPDATE_DELAY_MS_AFTER_PROBE	1000

#define SEC_TS_SELFTEST_REPORT_SIZE	120
#define SEC_TS_PRESSURE_MAX		0x3f

#define IO_PREALLOC_READ_BUF_SZ		2560
#define IO_PREALLOC_WRITE_BUF_SZ	2560

#define HOST_DMA_ALIGN(len) ((len) >= 256 ? ALIGN(len, 16) : ALIGN(len, 4))

#define VENDOR_REGISTER_DATA_SIZE 	128

#define AMBIENT_CAL			1
#define OFFSET_CAL			2

#define SEC_TS_SKIPTSP_DUTY		100


/* SEC_TS READ REGISTER ADDRESS */
/* 0x10 */
#define SEC_TS_CMD_READ_ID			0x10    // Read ID
#define SEC_TS_CMD_SW_RESET			0x12    // SW Reset
#define SEC_TS_CMD_ENTER_BOOT_MODE		0x13    // Enter Boot Mode
#define SEC_TS_CMD_GET_FW_VERSION		0x14    // Get Fw Version
#define SEC_TS_CMD_READ_PANEL_INFO		0x15	// Get Panel Info
#define SEC_TS_CMD_READ_CHIP_ID			0x17	// Read Unique Chip Id
#define SEC_TS_CMD_HEATMAP_ENABLE		0x1C	// Enable Heatmap
#define SEC_TS_CMD_GET_HEATMAP_VERSION		0x1D	// Read HEATMAP Version
#define SEC_TS_CMD_SPI_CHECKSUM_ENABLE		0x1F	// Set SPI Checksum Enable

/* 0x20 */
#define SEC_TS_CMD_SYSTEM_MODE			0x20    // System Mode
#define SEC_TS_CMD_POWER_MODE			0x21    // Power Mode
#define SEC_TS_CMD_TOUCH_FUNCTION		0x22	// Touch Function
#define SET_TS_CMD_SET_CHARGER_TYPE		0x23	// Charger type
#define SET_TS_CMD_SET_COVER_TYPE		0x24	// Cover type
#define SET_TS_CMD_SET_REPORT_RATE		0x25	// Report Rate

/* 0x30 */
#define SEC_TS_CMD_READ_WET_MODE_STATUS		0x3B

/* 0x50 */
#define SEC_TS_CMD_SET_GESTURE_ENABLE		0x50
#define SEC_TS_CMD_SET_GESTURE_STTW_PARAM	0x51
#define SEC_TS_CMD_SET_GESTURE_INVALID_ENABLE	0x52

/* 0x60 */
#define SEC_TS_CMD_READ_ONE_EVENT		0x60    // Read One Event
#define SEC_TS_CMD_READ_ALL_EVENT		0x61    // Read All Event
#define SEC_TS_CMD_CLEAR_EVENT_STACK		0x62    // Clear All Event Stack
#define SEC_TS_CMD_READ_EVENT_NUM		0x63    // Get Event Count
#define SEC_TS_CMD_RAWDATA_TYPE			0x68    // Rawdata Type
#define SEC_TS_CMD_RAWDATA_SIZE			0x69    // Rawdata Size
#define SEC_TS_CMD_GET_REPORT			0x6A    // Get Report
#define SEC_TS_CMD_GET_HEATMAP			0x6B    // Get Heatmap
#define SEC_TS_CMD_GET_DISP_NOISE_LEVEL		0x6C    // Get Display Noise Level
#define SEC_TS_CMD_GET_EXT_NOISE_LEVEL		0x6D    // Get External Noise Level
#define SEC_TS_CMD_SET_HOPPING_FREQ_FIX		0x6E	// Set Hopping Freq Fix
#define SEC_TS_CMD_SET_HOPPING_FREQ		0x6F	// Set Hopping Freq Index

/* 0x80 */
#define SEC_TS_CMD_WRITE_USER_STATUS_EVENT	0x81	// Write User Status Event
#define SEC_TS_CMD_SET_GET_TINTX_PIN_STATE	0x82	// Set/Get TINTX Pin State
#define SEC_TS_CMD_P2P_TEST			0x83	// P2P Test
#define SEC_TS_CMD_SELF_TEST			0x84	// Self Test
#define SEC_TS_CMD_PANEL_CAL			0x85	// Panel Cal
#define SEC_TS_CMD_SET_WET_MODE			0x8B

/* 0xB0 */
#define SEC_TS_CMD_SET_COORD_FILTER		0xB0
#define SEC_TS_CMD_SET_NOISE_MODE		0xBB
#define SEC_TS_CMD_SET_GRIP_DETECT		0xBC
#define SEC_TS_CMD_SET_HIGH_SENSITIVITY		0xBD
#define SEC_TS_CMD_SET_PALM_DETECT		0xBE
#define SEC_TS_CMD_SET_CONT_REPORT		0xBF

/* 0xC0 */
/* RTDP */
#define SEC_TS_CMD_START_RTDP			0xC0
#define SEC_TS_CMD_READ_RTDP_INFO		0xC1
#define SEC_TS_CMD_SET_RTDP_OFFSET		0xC2
#define SEC_TS_CMD_READ_RTDP_DATA		0xC3
#define SEC_TS_CMD_SET_RTDP_FADE		0xC4

/* 0xD0 */
/* Memory CMD*/
#define SEC_TS_CMD_MEMORY_SET_MEM_ADDR		0xD0	// Set Mem Addr
#define SEC_TS_CMD_MEMORY_SET_DATA_NUM		0xD1	// Set Data Num
#define SEC_TS_CMD_MEMORY_READ_WRITE_DATA	0xD2	// Read/Write Memory
#define SEC_TS_CMD_MEMORY_WRITE_BURST		0xD3	// Write Memory Burst [BOOT]
/* Flash CMD*/
#define SEC_TS_CMD_FLASH_GET_CHIP_ID		0xD5	// Read Flash Chip ID
#define SEC_TS_CMD_FLASH_GET_STATUS		0xD6	// Read Flash Status
#define SEC_TS_CMD_FLASH_SET_MEM_ADDR		0xD7	// Set Flash Addr
#define SEC_TS_CMD_FLASH_SET_DATA_NUM		0xD8	// Set Data Num
#define SEC_TS_CMD_FLASH_READ_WRITE_DATA	0xD9	// Read/Write Flash
#define SEC_TS_CMD_FLASH_ERASE			0xDA	// Erase Flash
#define SEC_TS_CMD_FLASH_SET_PROTECTION		0xDB	// Set Flash Protection

/* 0xF0 */
#define SEC_TS_CMD_SET_VENDOR_EVENT_LEVEL	0xF2
#define SEC_TS_CMD_SET_FREQ			0xF3
#define SEC_TS_CMD_GET_DDI_SYNC_FREQ		0xF5


/**
 * old ver command
**/
#define SEC_TS_CMD_DEADZONE_RANGE	0xAA
#define SEC_TS_CMD_EDGE_DEADZONE	0xE5


/* flash */
#define SEC_TS_FLASH_SIZE_64		64
#define SEC_TS_FLASH_SIZE_128		128
#define SEC_TS_FLASH_SIZE_256		256

#define SEC_TS_FLASH_SIZE_CMD		1
#define SEC_TS_FLASH_SIZE_ADDR		2
#define SEC_TS_FLASH_SIZE_CHECKSUM	1

#define SEC_TS_STATUS_BOOT_MODE		0xB0
#define SEC_TS_STATUS_APP_MODE		0xA0

#define SEC_TS_FIRMWARE_PAGE_SIZE_256	256
#define SEC_TS_FIRMWARE_PAGE_SIZE_128	128

/* SEC status event id */
#define SEC_TS_COORDINATE_EVENT		0
#define SEC_TS_STATUS_EVENT		1
#define SEC_TS_GESTURE_EVENT		2
#define SEC_TS_EMPTY_EVENT		3

#define SEC_TS_EVENT_BUFF_SIZE		16

#define SEC_TS_GESTURE_CODE_STTW		0
#define SEC_TS_GESTURE_CODE_STTW_INVALID_OUT_OF_RANGE		1
#define SEC_TS_GESTURE_CODE_STTW_INVALID_SHIFT		2
#define SEC_TS_GESTURE_CODE_STTW_INVALID_PALM		3
#define SEC_TS_GESTURE_CODE_STTW_INVALID_MULTI_TOUCH		4
#define SEC_TS_GESTURE_CODE_STTW_INVALID_LONG_PRESS		5
#define SEC_TS_GESTURE_CODE_STTW_INVALID_SHORT_PRESS		6

#define SEC_TS_COORDINATE_ACTION_NONE		0
#define SEC_TS_COORDINATE_ACTION_PRESS		1
#define SEC_TS_COORDINATE_ACTION_MOVE		2
#define SEC_TS_COORDINATE_ACTION_RELEASE	3

#define SEC_TS_TOUCHTYPE_NORMAL		0
#define SEC_TS_TOUCHTYPE_HOVER		1
#define SEC_TS_TOUCHTYPE_FLIPCOVER	2
#define SEC_TS_TOUCHTYPE_GLOVE		3
#define SEC_TS_TOUCHTYPE_STYLUS		4
#define SEC_TS_TOUCHTYPE_PALM		5
#define SEC_TS_TOUCHTYPE_WET		6
#define SEC_TS_TOUCHTYPE_PROXIMITY	7
#define SEC_TS_TOUCHTYPE_JIG		8
#define SEC_TS_TOUCHTYPE_GRIP		10

/* SEC_TS_INFO : Info acknowledge event */
#define SEC_TS_ACK_BOOT_COMPLETE	0x00
#define SEC_TS_ACK_WET_MODE	0x1

/* SEC_TS_VENDOR_INFO : Vendor acknowledge event */
#define SEC_TS_VENDOR_ACK_OFFSET_CAL_DONE	0x40
#define SEC_TS_VENDOR_ACK_SELF_TEST_DONE	0x41
#define SEC_TS_VENDOR_ACK_P2P_TEST_DONE		0x42

/* SEC_TS_STATUS_EVENT_USER_INPUT */
#define SEC_TS_EVENT_FORCE_KEY	0x1

/* SEC_TS_ERROR : Error event */
#define SEC_TS_ERR_EVNET_CORE_ERR	0x0
#define SEC_TS_ERR_EVENT_QUEUE_FULL	0x1
#define SEC_TS_ERR_EVENT_ESD		0x2
#define SEC_TS_ERR_EVENT_SYNC		0x3

/* SET FUNCTIONS */
#define SEC_TS_BIT_SETFUNC_WIRELESS_CHARGER	(1 << 13)
#define SEC_TS_BIT_SETFUNC_STYLUS		(1 << 11)
#define SEC_TS_BIT_SETFUNC_WET			(1 << 12)
#define SEC_TS_BIT_SETFUNC_CHARGER		(1 << 10)
#define SEC_TS_BIT_SETFUNC_GLOVE		(1 << 9)
#define SEC_TS_BIT_SETFUNC_COVER		(1 << 8)
#define SEC_TS_BIT_SETFUNC_PALM			(1 << 7)
#define SEC_TS_BIT_SETFUNC_GAME			(1 << 6)
#define SEC_TS_BIT_SETFUNC_HOVER		(1 << 3)
#define SEC_TS_BIT_SETFUNC_TOUCH_SENSE_ON	(1 << 2)
#define SEC_TS_BIT_SETFUNC_TOUCH_ENGINE_ON	(1 << 1)
#define SEC_TS_BIT_SETFUNC_STATE_MANAGEMENT_ON	(1 << 0)

#define SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC	(SEC_TS_BIT_SETFUNC_STATE_MANAGEMENT_ON |\
						SEC_TS_BIT_SETFUNC_TOUCH_ENGINE_ON |\
						SEC_TS_BIT_SETFUNC_TOUCH_SENSE_ON |\
						SEC_TS_BIT_SETFUNC_PALM |\
						SEC_TS_BIT_SETFUNC_WET)


#define SEC_TS_BIT_CHARGER_MODE_NO			(0x1 << 0)
#define SEC_TS_BIT_CHARGER_MODE_WIRE_CHARGER		(0x1 << 1)
#define SEC_TS_BIT_CHARGER_MODE_WIRELESS_CHARGER	(0x1 << 2)
#define SEC_TS_BIT_CHARGER_MODE_WIRELESS_BATTERY_PACK	(0x1 << 3)

#define STATE_MANAGE_ON			1
#define STATE_MANAGE_OFF		0

#define SEC_TS_CMD_EDGE_HANDLER		0xAA
#define SEC_TS_CMD_EDGE_AREA		0xAB
#define SEC_TS_CMD_DEAD_ZONE		0xAC
#define SEC_TS_CMD_LANDSCAPE_MODE	0xAD

enum spec_check_type {
	SPEC_NO_CHECK			= 0,
	SPEC_CHECK			= 1,
	SPEC_PASS			= 2,
	SPEC_FAIL			= 3,
};

enum region_type {
	REGION_NORMAL			= 0,
	REGION_EDGE			= 1,
	REGION_CORNER			= 2,
	REGION_NOTCH			= 3,
	REGION_TYPE_COUNT		= 4,
	/* REGION type should be continuous number start from 0,
	 * since REGION_TYPE_COUNT is used for type count
	 */
};

enum grip_write_mode {
	G_NONE				= 0,
	G_SET_EDGE_HANDLER		= 1,
	G_SET_EDGE_ZONE			= 2,
	G_SET_NORMAL_MODE		= 4,
	G_SET_LANDSCAPE_MODE	= 8,
	G_CLR_LANDSCAPE_MODE	= 16,
};
enum grip_set_data {
	ONLY_EDGE_HANDLER		= 0,
	GRIP_ALL_DATA			= 1,
};

enum TOUCH_POWER_MODE {
	SEC_TS_STATE_POWER_OFF = 0,
	SEC_TS_STATE_SUSPEND,
	SEC_TS_STATE_LPM,
	SEC_TS_STATE_POWER_ON
};

enum TOUCH_SYSTEM_MODE {
	TOUCH_SYSTEM_MODE_BOOT		= 0,
	TOUCH_SYSTEM_MODE_CALIBRATION	= 1,
	TOUCH_SYSTEM_MODE_TOUCH		= 2,
	TOUCH_SYSTEM_MODE_SELFTEST	= 3,
	TOUCH_SYSTEM_MODE_FLASH		= 4,
	TOUCH_SYSTEM_MODE_LOWPOWER	= 5,
	TOUCH_SYSTEM_MODE_SLEEP		= 6
};

enum TOUCH_MODE_STATE {
	TOUCH_MODE_STATE_IDLE		= 0,
	TOUCH_MODE_STATE_WET		= 1,
	TOUCH_MODE_STATE_TOUCH		= 2,
	TOUCH_MODE_STATE_NOISY		= 3,
	TOUCH_MODE_STATE_CAL		= 4,
	TOUCH_MODE_STATE_COVER		= 5,
	TOUCH_MODE_STATE_HOVER		= 6,
	TOUCH_MODE_STATE_WAKEUP		= 14
};

enum SLEEP_MODE_STATE {
	SLEEP_MODE_STATE_SLEEP		= 0,
	SLEEP_MODE_STATE_STOP		= 1,
};

enum {
	TEST_OPEN			= (0x1 << 0),
	TEST_NODE_VARIANCE		= (0x1 << 1),
	TEST_SHORT			= (0x1 << 2),
	TEST_SELF_NODE			= (0x1 << 5),
	TEST_NOT_SAVE			= (0x1 << 7),
	TEST_HIGH_FREQ			= (0x1 << 8),
};

enum switch_system_mode {
	TO_TOUCH_MODE			= 0,
	TO_LOWPOWER_MODE		= 1,
	TO_SELFTEST_MODE		= 2,
	TO_FLASH_MODE			= 3,
	TO_SLEEP_MODE			= 4,
	TO_STOP_MODE			= 5,
};

enum noise_mode_param {
	NOISE_MODE_DEFAULT	= 0x00,
	NOISE_MODE_OFF		= 0x10,
	NOISE_MODE_FORCE_ON	= 0x11,
};

#define HOPPING_FREQ_FIX_OFF		0x0
#define HOPPING_FREQ_FIX_ON		0x1

enum hopping_freq_param {
	HOPPING_FREQ_DEFAULT	= 0x00,
	HOPPING_FREQ_1		= 0x00,
	HOPPING_FREQ_2		= 0x01,
	HOPPING_FREQ_3		= 0x02,
};

enum {
	TYPE_RAW_DATA			= 0,	/* Total - Offset : delta data
						 **/
	TYPE_SIGNAL_DATA		= 1,	/* Signal - Filtering &
						 * Normalization
						 **/
	TYPE_AMBIENT_BASELINE		= 2,	/* Cap Baseline
						 **/
	TYPE_AMBIENT_DATA		= 3,	/* Cap Ambient
						 **/
	TYPE_REMV_BASELINE_DATA		= 4,
	TYPE_DECODED_DATA		= 5,	/* Raw */
	TYPE_REMV_AMB_DATA		= 6,	/* TYPE_RAW_DATA -
						 * TYPE_AMBIENT_DATA
						 **/
	TYPE_NORM2_DATA			= 15,	/* After fs norm. data
						 **/
	TYPE_OFFSET_DATA_SEC		= 19,	/* Cap Offset in SEC
						 * Manufacturing Line
						 **/
	TYPE_OFFSET_DATA_SDC		= 29,	/* Cap Offset in SDC
						 * Manufacturing Line
						 **/
	TYPE_NOI_P2P_MIN		= 30,	/* Peak-to-peak noise Min
						 **/
	TYPE_NOI_P2P_MAX		= 31,	/* Peak-to-peak noise Max
						 **/
	TYPE_INVALID_DATA		= 0xFF,	/* Invalid data type for
						 * release factory mode
						 **/
};

enum RESET_MODE {
	RESET_MODE_NA		= 0x00,
	RESET_MODE_SW		= 0x01,
	RESET_MODE_HW		= 0x02,
	RESET_MODE_AUTO		= (RESET_MODE_SW | RESET_MODE_HW),
};

enum SET_TOUCH_MODE {
	SET_TOUCH_MODE_NPA = 1,
	SET_TOUCH_MODE_NPI,
	SET_TOUCH_MODE_LPA,
	SET_TOUCH_MODE_LPI,
	SET_TOUCH_MODE_SENSE_ON,
	SET_TOUCH_MODE_SENSE_OFF,
	SET_TOUCH_MODE_RESET,
	SET_TOUCH_MODE_SENSING_TOGGLE,
};

enum {
	SEC_TS_ERR_NA = 0,
	SEC_TS_ERR_INIT,
	SEC_TS_ERR_ALLOC_FRAME,
	SEC_TS_ERR_ALLOC_FRAME_SS,
	SEC_TS_ERR_ALLOC_GAINTABLE,
	SEC_TS_ERR_REG_INPUT_DEV,
	SEC_TS_ERR_REG_INPUT_PAD_DEV
};

#define CMD_RESULT_WORD_LEN		10

#define SEC_TS_IO_RESET_CNT		3
#define SEC_TS_IO_RETRY_CNT		3
#define SEC_TS_BUF_CHECK_RETRY_CNT		10

#define SEC_TS_WAIT_FOR_READY_INTERVAL_MS	20
#define SEC_TS_BOOT_COMPLETE_TIME_MS		100
#define SEC_TS_VENDOR_SELF_TEST_TIME_MS		200
#define SEC_TS_VENDOR_P2P_TEST_TIME_MS		600
#define SEC_TS_VENDOR_ACK_OFFSET_CAL_MS		4000

#define SEC_TS_AOD_GESTURE_PRESS		(1 << 7)
#define SEC_TS_AOD_GESTURE_LONGPRESS		(1 << 6)
#define SEC_TS_AOD_GESTURE_DOUBLETAB		(1 << 5)


enum sec_ts_cover_id {
	SEC_TS_FLIP_WALLET = 0,
	SEC_TS_VIEW_COVER,
	SEC_TS_COVER_NOTHING1,
	SEC_TS_VIEW_WIRELESS,
	SEC_TS_COVER_NOTHING2,
	SEC_TS_CHARGER_COVER,
	SEC_TS_VIEW_WALLET,
	SEC_TS_LED_COVER,
	SEC_TS_CLEAR_FLIP_COVER,
	SEC_TS_QWERTY_KEYBOARD_EUR,
	SEC_TS_QWERTY_KEYBOARD_KOR,
	SEC_TS_MONTBLANC_COVER = 100,
};

enum sec_fw_update_status {
	SEC_NOT_UPDATE = 0,
	SEC_NEED_FW_UPDATE,
	SEC_NEED_CALIBRATION_ONLY,
	SEC_NEED_FW_UPDATE_N_CALIBRATION,
};

enum tsp_hw_parameter {
	TSP_ITO_CHECK		= 1,
	TSP_RAW_CHECK		= 2,
	TSP_MULTI_COUNT		= 3,
	TSP_WET_MODE		= 4,
	TSP_COMM_ERR_COUNT	= 5,
	TSP_MODULE_ID		= 6,
};

enum {
	GRIP_PRESCREEN_OFF	= 0,
	GRIP_PRESCREEN_MODE_1	= 1,
	GRIP_PRESCREEN_MODE_2	= 2,
	GRIP_PRESCREEN_MODE_3	= 3
};

enum {
	GRIP_PRESCREEN_TIMEOUT_MIN	= 0,
	GRIP_PRESCREEN_TIMEOUT_MAX	= 480
};

enum {
	ENCODED_ENABLE_OFF	= 0,
	ENCODED_ENABLE_ON	= 1
};

#define TEST_MODE_MIN_MAX		false
#define TEST_MODE_ALL_NODE		true
#define TEST_MODE_READ_FRAME		0
#define TEST_MODE_READ_CHANNEL		1
#define TEST_MODE_READ_ALL		2

/* factory test mode */
struct sec_ts_test_mode {
	u8 type;
	short min[REGION_TYPE_COUNT];
	short max[REGION_TYPE_COUNT];
	u8 self_report;
	enum spec_check_type spec_check;
};

struct sec_ts_fw_file {
	u8 *data;
	u32 pos;
	size_t size;
};

/*
 * write 0xE4 [ 11 | 10 | 01 | 00 ]
 * MSB <-------------------> LSB
 * read 0xE4
 * mapping sequence : LSB -> MSB
 * struct sec_ts_test_result {
 * * assy : front + OCTA assay
 * * module : only OCTA
 *	 union {
 *		 struct {
 *			 u8 assy_count:2;		-> 00
 *			 u8 assy_result:2;		-> 01
 *			 u8 module_count:2;	-> 10
 *			 u8 module_result:2;	-> 11
 *		 } __attribute__ ((packed));
 *		 unsigned char data[1];
 *	 };
 *};
 */
struct sec_ts_test_result {
	union {
		struct {
			u8 assy_count:2;
			u8 assy_result:2;
			u8 module_count:2;
			u8 module_result:2;
		} __packed;
		unsigned char data[1];
	};
};

/* 16 byte */
struct sec_ts_gesture_status {
	u8 eid:2;
	u8 stype:4;
	u8 sf:2;
	u8 gesture_id;
	u8 gesture_data_1;
	u8 gesture_data_2;
	u8 gesture_data_3;
	u8 gesture_data_4;
	u8 gesture_data_5;
	u8 left_event_5_0:6;
	u8 reserved_5:2;
	u8 gesture_data_6;
	u8 gesture_data_7;
	u8 gesture_data_8;
	u8 gesture_data_9;
	u8 gesture_data_10;
	u8 reserved_11;
	u8 reserved_12;
	u8 reserved_13;
} __packed;


/* status id for sec_ts event */
#define SEC_TS_EVENT_STATUS_ID_OSC_CAL		0x30
#define SEC_TS_EVENT_STATUS_ID_HOPPING		0x33
#define SEC_TS_EVENT_STATUS_ID_STATE		0x61
#define SEC_TS_EVENT_STATUS_ID_NOISE		0x64
#define SEC_TS_EVENT_STATUS_ID_GRIP		0x69
#define SEC_TS_EVENT_STATUS_ID_FOD		0x6B
#define SEC_TS_EVENT_STATUS_ID_PALM		0x70

/* 16 byte */
struct sec_ts_event_status {
	union {
		struct {
			u8 eid : 2;
			u8 stype : 4;
			u8 sf : 2;
			u8 status_id;
			u8 status_data_1;
			u8 status_data_2;
			u8 status_data_3;
			u8 status_data_4;
			u8 status_data_5;
			u8 left_event_5_0 : 5;
			u8 reserved_2 : 3;
			u8 noise_level;
			u8 max_strength;
			u8 hover_id_num : 4;
			u8 reserved10 : 4;
			u8 reserved11;
			u8 reserved12;
			u8 reserved13;
			u8 reserved14;
			u8 reserved15;
		} __packed;
		u8 data[16];
	};
} __packed;

/* 16 byte */
struct sec_ts_event_coordinate {
	u8 eid : 2;
	u8 tid : 4;
	u8 tchsta : 2;
	u8 x_11_4;
	u8 y_11_4;
	u8 y_3_0 : 4;
	u8 x_3_0 : 4;
	u8 major;
	u8 minor;
	u8 z : 6;
	u8 ttype_3_2 : 2;
	u8 left_event : 5;
	u8 max_energy_flag : 1;
	u8 ttype_1_0 : 2;
	u8 noise_level;
	u8 max_strength;
	u8 hover_id_num : 4;
	u8 noise_status : 2;
	u8 reserved10 : 2;
	u8 x_15_12 : 4;
	u8 reserved11 : 4;
	u8 y_15_12 : 4;
	u8 reserved12 : 4;
	u8 orientation_7_0;
	u8 orientation_15_8;
	u8 reserved15; //reserved
} __packed;

/* 16 byte */
struct sec_ts_event_hopping {
	u8 eid : 2;
	u8 stype : 4;
	u8 sf : 2;
	u8 event_id;
	u8 id : 4;
	u8 cause : 4;
	u8 prev_id;
	u8 noise_lvl[2];
	u8 reserved6;
	u8 reserved7;
	u8 reserved8;
	u8 reserved9;
	u8 reserved10;
	u8 reserved11;
	u8 reserved12;
	u8 reserved13;
	u8 reserved14;
	u8 reserved15;
} __packed;

struct sec_ts_event_pen {
	u8 eid : 2;
	u8 penid : 3;
	u8 reserved0 : 1;
	u8 protocol_type : 2;
	u8 stylus_status : 6;
	u8 dlink_type : 2;
	u8 x_11_4;
	u8 y_11_4;
	u8 y_3_0 : 4;
	u8 x_3_0 : 4;
	u8 dlink_15_8;
	u8 dlink_7_0;
	u8 left_event : 5;
	u8 reserved7 : 3;
	u8 ringx_11_4;
	u8 ringy_11_4;
	u8 ringy_3_0 : 4;
	u8 ringx_3_0 : 4;
	u8 tiltx_11_4;
	u8 tilty_11_4;
	u8 tilty_3_0 : 4;
	u8 tiltx_3_0 : 4;
	u8 tip_ring_len;
	u8 reserved15;
} __attribute__((packed));

/* not fixed */
struct sec_ts_coordinate {
	u8 id;
	u8 ttype;
	u8 action;
	u16 x;
	u16 y;
	u8 z;
	u8 hover_flag;
	u8 glove_flag;
	u8 touch_height;
	u16 mcount;
	u16 major;
	u16 minor;
	bool palm;
	u8 left_event;
	bool grip;
	s16 orientation;
	/* for debug purpose. */
	ktime_t ktime_pressed;
	ktime_t ktime_released;
};

struct sec_ts_data {
	u32 crc_addr;
	u32 fw_addr;
	u32 para_addr;
	u32 flash_page_size;
	u8 boot_ver[6];

	struct device *dev;
#if defined(I3C_INTERFACE)
	struct i3c_device *client;
#elif defined(I2C_INTERFACE)
	struct i2c_client *client;
#else
	struct spi_device *client;
#endif
	struct input_dev *input_dev;
	struct sec_ts_plat_data *plat_data;
	struct sec_ts_coordinate coord[MAX_SUPPORT_TOUCH_COUNT +
					MAX_SUPPORT_HOVER_COUNT];

	ktime_t isr_timestamp; /* time that the event was first received from
				* the touch IC, acquired during hard interrupt,
				* in CLOCK_MONOTONIC
				**/
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	u64 raw_timestamp_sensing;
	u64 timestamp_sensing;
#endif

	s64 longest_duration; /* ms unit */

	u8 lowpower_mode;
	u8 lowpower_status;
#ifdef USE_OPEN_CLOSE
	volatile bool input_closed;
#endif

	struct completion boot_completed;

	unsigned int touch_count;	/* active touch slot(s). */
	int tx_count;
	int rx_count;
	int io_burstmax;
	volatile int power_status;
	u16 touch_functions;
	struct sec_ts_event_coordinate touchtype;
	u8 gesture_status[6];
	struct mutex device_mutex;
	struct mutex io_mutex;
	struct mutex eventlock;

	u8 print_format;
	u8 frame_type;

#ifdef USE_POWER_RESET_WORK
	struct delayed_work reset_work;
	volatile bool reset_is_on_going;
#endif

#ifdef SEC_TS_FW_UPDATE_ON_PROBE
	struct work_struct fw_update_work;
#else
	struct delayed_work fw_update_work;
	struct workqueue_struct *fw_update_wq;
#endif

	struct work_struct reset_handler_work;	/* Used when a reset is triggered. */
	struct workqueue_struct *event_wq;	/* Used for event handler,
						 * suspend, resume threads
						 **/
	struct sec_cmd_data sec;
	union {
		short *pFrame;
		short *pFrameMS;
	};

	/* only available if sec_ts_read_frame_and_channel() be called */
	short *pFrameSS;

	/* Used for flip the heatmap. */
	short *pFrametemp;
	short *pFrameMS_irq;
	short *pFrameSS_irq;

	u8 vendor_register_data[VENDOR_REGISTER_DATA_SIZE];

	bool probe_done;
	bool reinit_done;
	bool flip_enable;
	int cover_type;
	u8 cover_cmd;

	unsigned int scrub_id;
	unsigned int scrub_x;
	unsigned int scrub_y;

	u8 grip_edgehandler_direction;
	int grip_edgehandler_start_y;
	int grip_edgehandler_end_y;
	u16 grip_edge_range;
	u8 grip_deadzone_up_x;
	u8 grip_deadzone_dn_x;
	int grip_deadzone_y;
	u8 grip_landscape_mode;
	int grip_landscape_edge;
	u16 grip_landscape_deadzone;

#ifdef CONFIG_TOUCHSCREEN_DUMP_MODE
	struct delayed_work ghost_check;
#endif
	u8 tsp_dump_lock;

	int report_rate;
	int vsync;
	int wet_mode;

	unsigned char ito_test[4];		/* ito panel tx/rx channel */
	unsigned char check_multi;
	unsigned int multi_count;		/* multi touch count */
	unsigned int palm_count;
	unsigned int wet_count;			/* wet mode count */
	unsigned int dive_count;		/* dive mode count */
	unsigned int comm_err_count;		/* comm error count */
	unsigned int io_err_count;		/* io error count */
	/*
	 * accumulated count of pressed
	 * touch from resume to suspend.
	 */
	unsigned int checksum_result;		/* checksum result */
	unsigned char module_id[4];
	unsigned int all_finger_count;
	unsigned int all_force_count;
	unsigned int all_aod_tap_count;
	unsigned int all_spay_count;
	unsigned int max_z_value;
	unsigned int min_z_value;
	unsigned int sum_z_value;
	unsigned char pressure_cal_base;
	unsigned char pressure_cal_delta;

	union {
		u32 debug;
		struct {
		u32 debug_events : 1;
		u32 debug_status : 1;
		u32 debug_reserved : 30;
		};
	};

	bool is_fw_corrupted;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
    struct goog_touch_interface *gti;
#endif /* IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */

	atomic_t irq_enabled;

	int (*sec_ts_write)(struct sec_ts_data *ts, u8 reg, u8 *data, int len);
	int (*sec_ts_read)(struct sec_ts_data *ts, u8 reg, u8 *data, int len);
	int (*sec_ts_write_burst)(struct sec_ts_data *ts, u8 *data, int len);
	int (*sec_ts_read_bulk)(struct sec_ts_data *ts, u8 *data, int len);

	void (*sec_irq_enable)(struct sec_ts_data *ts, bool enable);
	void (*sec_disable_irq_nosync)(struct sec_ts_data *ts);

	/* alloc for io read buffer */
	u8 *io_read_buf;
	/* alloc for io write buffer */
	u8 *io_write_buf;
	/* alloc for customized heatmap buffer */
	u8 io_heatmap_buf[IO_PREALLOC_WRITE_BUF_SZ];

	u8 heatmap_coord_buff[MAX_SUPPORT_TOUCH_COUNT * SEC_TS_EVENT_BUFF_SIZE];
	u8 read_event_buff[MAX_EVENT_COUNT][SEC_TS_EVENT_BUFF_SIZE];

	/* Self test limit */
	int *cm1_max;
	int *cm1_min;
	int *cm1_gap;
	int cmr_p2p_h;
	int cmr_p2p_l;
	int cmr_p2p_h_l_max;
	int cmr_p2p_h_l_min;
};

struct sec_ts_plat_data {
	int max_x;
	int max_y;
	unsigned int irq_gpio;
	int irq_type;
	int irq;
	int io_burstmax;
	int bringup;
	u8 spi_checksum_enable;

	char firmware_name[SEC_TS_MAX_FW_NAME];
	char selftest_limit_name[SEC_TS_MAX_FW_NAME];
	const char *model_name;

	u32 panel_revision;
	u8 core_version_of_ic[4];
	u8 core_version_of_bin[4];
	u8 config_version_of_ic[4];
	u8 config_version_of_bin[4];
	u8 img_version_of_ic[4];
	u8 img_version_of_bin[4];

	struct pinctrl *pinctrl;

	int reset_gpio;

	bool support_mt_pressure;
#ifdef KEY_SIDE_GESTURE
	bool support_sidegesture;
#endif

	/* convert mm to pixel for major and minor */
	u8 mm2px;
};

void sec_ts_print_data(struct sec_ts_data *ts, u32 size, u8 *data);
int sec_ts_stop_device(struct sec_ts_data *ts);
int sec_ts_start_device(struct sec_ts_data *ts);
int sec_ts_hw_reset(struct sec_ts_data *ts, bool wait_for_done);
int sec_ts_sw_reset(struct sec_ts_data *ts, bool wait_for_done);
int sec_ts_system_reset(struct sec_ts_data *ts,
			enum RESET_MODE mode,
			bool wait_for_done,
			bool sense_on);
int sec_ts_set_lowpowermode(struct sec_ts_data *ts, u8 mode);
int sec_ts_firmware_update_on_probe(struct sec_ts_data *ts, bool force_update);
int sec_ts_firmware_update_on_hidden_menu(struct sec_ts_data *ts,
					    int update_type);
int sec_ts_glove_mode_enables(struct sec_ts_data *ts, int mode);
int sec_ts_set_cover_type(struct sec_ts_data *ts, bool enable);
int sec_ts_wait_for_ready(struct sec_ts_data *ts, unsigned int ack, unsigned int time);

int sec_ts_fn_init(struct sec_ts_data *ts);
int sec_ts_execute_force_calibration(struct sec_ts_data *ts);
int sec_ts_set_power_mode(struct sec_ts_data *ts, u8 mode);
int sec_ts_fix_tmode(struct sec_ts_data *ts, u8 mode, u8 state);
int sec_ts_release_tmode(struct sec_ts_data *ts);
int sec_ts_ddi_osc_on(struct sec_ts_data *ts);
int sec_ts_enter_recovery(struct sec_ts_data *ts, u8 on, u8 hw_reset, u8 irq_control);
void sec_ts_unlocked_release_all_finger(struct sec_ts_data *ts);
void sec_ts_locked_release_all_finger(struct sec_ts_data *ts);
void sec_ts_fn_remove(struct sec_ts_data *ts);
void sec_ts_delay(unsigned int ms);
int sec_ts_read_information(struct sec_ts_data *ts);
int sec_ts_run_rawdata_type(struct sec_ts_data *ts, struct sec_cmd_data *sec, u8 data_type);
void sec_ts_run_rawdata_all(struct sec_ts_data *ts);
int execute_selftest(struct sec_ts_data *ts, u32 option);
int execute_p2ptest(struct sec_ts_data *ts);
int sec_ts_memorywrite(struct sec_ts_data *ts, u32 mem_addr, u8 *mem_data, u32 mem_size);
int sec_ts_read_frame(struct sec_ts_data *ts, u8 w_type, short *min, short *max);
int sec_ts_get_checksum_status(struct sec_ts_data *ts);

#if (1)//!defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
int sec_ts_raw_device_init(struct sec_ts_data *ts);
#endif
void sec_ts_raw_device_exit(struct sec_ts_data *ts);

extern struct class *sec_class;

extern void set_grip_data_to_ic(struct sec_ts_data *ts, u8 flag);
extern void sec_ts_set_grip_type(struct sec_ts_data *ts, u8 set_type);

#if !defined(I3C_INTERFACE) && !defined(I2C_INTERFACE)
int sec_ts_spi_checksum_enable(struct sec_ts_data *ts, u8 enb);
#endif

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
void goog_gti_probe(struct sec_ts_data *ts);
void sec_ts_read_event(struct sec_ts_data *ts);
irqreturn_t goog_sec_ts_isr(int irq, void *handle);
irqreturn_t goog_sec_ts_irq_thread(int irq, void *ptr);
extern const struct dev_pm_ops sec_ts_dev_pm_ops;
#endif /* IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */

#endif
