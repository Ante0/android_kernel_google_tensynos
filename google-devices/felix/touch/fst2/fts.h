/*
  *
  **************************************************************************
  **                        STMicroelectronics				  **
  **************************************************************************
  *                                                                        *
  * FTS Capacitive touch screen controller (FingerTipS)		           *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file fts.h
  * \brief Contains all the definitions and structs used generally by the driver
  */

#ifndef _LINUX_FTS_H_
#define _LINUX_FTS_H_

#include <linux/device.h>
#include "fts_lib/fts_io.h"

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
#include <goog_touch_interface.h>
#endif

#undef pr_fmt
#define pr_fmt(fmt) "gtd: fst2: " fmt
#undef dev_fmt
#define dev_fmt(fmt) "gtd: " fmt

#define FTS_TS_DRV_NAME		"fst2"
#define FTS_TS_DRV_VERSION	"6.0.3"
#define FTS_TS_DRV_VER		0x06000004

#define PINCTRL_STATE_ACTIVE    "pmx_ts_active"
#define PINCTRL_STATE_SUSPEND   "pmx_ts_suspend"
#define PINCTRL_STATE_RELEASE   "pmx_ts_release"

#define MAX_PROBE_RETRY 3
#define MAX_FIFO_EVENT	32 /* /< max number of events that the FIFO can
				 * collect  */
#define EVENT_DATA_SIZE (FIFO_EVENT_SIZE * MAX_FIFO_EVENT) /* /< event data
                                                            * buffer size */
/* **** PANEL SPECIFICATION **** */
#define X_AXIS_MAX	2207	/* /< Max X coordinate of the display */
#define X_AXIS_MIN	0	/* /< min X coordinate of the display */
#define Y_AXIS_MAX	1839	/* /< Max Y coordinate of the display */
#define Y_AXIS_MIN	0	/* /< min Y coordinate of the display */

#define PRESSURE_MIN	0	/* /< min value of pressure reported */
#define PRESSURE_MAX	127	/* /< Max value of pressure reported */

#define DISTANCE_MIN	0	/* /< min distance between the tool and the
				 * display */
#define DISTANCE_MAX	127	/* /< Max distance between the tool and the
				 * display */

#define TOUCH_ID_MAX	10	/* /< Max number of simoultaneous touches
				 * reported */
#define PEN_ID_MAX	4	/* /< Max number of simoultaneous pen
				 * touches reported */

#define ABS_MAJOR_MIN(scale) (PRESSURE_MIN * scale)	/* /< MIN value of
					 * Major axis reported */
#define ABS_MINOR_MIN(scale) (PRESSURE_MIN * scale)	/* /< MIN value of
					 * Minor axis reported */
#define ABS_MAJOR_MAX(scale) (PRESSURE_MAX * scale)	/* /< MAX value of
					 * Major axis reported */
#define ABS_MINOR_MAX(scale) (PRESSURE_MAX * scale)	/* /< MAX value of
					 * Minor axis reported */
/* **** END **** */

/* Touch Types */
#define TOUCH_TYPE_FINGER_HOVER		0x00	/* /< Finger hover */
#define TOUCH_TYPE_FINGER			0x01	/* /< Finger touch */
#define TOUCH_TYPE_GLOVE			0x02	/* /< Glove touch */
#define TOUCH_TYPE_LARGE			0x03	/* /< Large touch */

/*
  * Forward declaration
  */
struct fts_ts_info;

/*
  * Dispatch event handler
  */
typedef void (*event_dispatch_handler_t)
	(struct fts_ts_info *info, unsigned char *data);

/**
  * Struct which contains information about the HW platform and set up
  */
struct fts_hw_platform_data {
	int (*power)(bool on);
	int irq_gpio;
	int reset_gpio;
	struct drm_panel *panel;
	u8 mm2px;
};
/**
  * Struct contains FTS capacitive touch screen device information
  */
struct fts_ts_info {
	struct device            *dev;	/* /< Pointer to the structure device */
#ifdef I2C_INTERFACE
	struct i2c_client        *client;	/* /< I2C client structure */
#else
	struct spi_device        *client;	/* /< SPI client structure */
#endif
	struct fts_hw_platform_data *board;	/* /< HW info retrieved from
						 * device tree */
	struct regulator *vdd_reg;	/* /< DVDD power regulator */
	struct regulator *avdd_reg;	/* /< AVDD power regulator */

	struct pinctrl       *ts_pinctrl;		/* touch pin control state holder */
	struct pinctrl_state *pinctrl_state_active;	/* Active pin state*/
	struct pinctrl_state *pinctrl_state_suspend;	/* Suspend pin state*/
	struct pinctrl_state *pinctrl_state_release;	/* Release pin state*/

	ktime_t timestamp; /* time that the event was first received from the
		touch IC, acquired during hard interrupt, in CLOCK_MONOTONIC */
	struct mutex fts_int_mutex;
	bool irq_enabled;	/* Interrupt state */

	struct input_dev *input_dev; /* /< Input device structure */
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct goog_touch_interface *gti;
#endif
	event_dispatch_handler_t *event_dispatch_table;
	int resume_bit;	/* /< Indicate if screen off/on */
	unsigned int mode;	/* /< Device operating mode (bitmask: msb
				 * indicate if active or lpm) */
	unsigned long touch_id;	/* /< Bitmask for touch id (mapped to input
				 * slots) */
	bool sensor_sleep;	/* /< if true suspend was called while if false
				 * resume was called */
#ifndef FW_UPDATE_ON_PROBE
	struct delayed_work fwu_work;	/* /< Delayed work thread for fw update
					 * process */
	struct workqueue_struct  *fwu_workqueue;/* /< Fw update work
							 * queue */
#endif
	bool dma_mode;
	unsigned char evt_data[EVENT_DATA_SIZE];
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	int16_t *mutual_data;
	int mutual_data_size;
	int16_t *self_data;
	int self_data_size;
	int16_t *fw_ms_data;
#endif
};

extern int fts_proc_init(struct fts_ts_info *info);
extern int fts_proc_remove(void);
int fts_system_reset(struct fts_ts_info *info, int poll_event);
int fts_set_interrupt(struct fts_ts_info *info, bool enable);

#endif
