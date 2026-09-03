/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2020, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*****************************************************************************
*
* File Name: focaltech_gestrue.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
#include <goog_touch_interface.h>
#endif

#include "focaltech_core.h"

/******************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/

/*****************************************************************************
* Static variables
*****************************************************************************/

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static ssize_t fts_gesture_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = fts_data;

    mutex_lock(&ts_data->input_dev->mutex);
    fts_read_reg(FTS_REG_GESTURE_EN, &val);
    count = snprintf(buf, PAGE_SIZE, "Gesture Mode:%s\n",
                     ts_data->gesture_mode ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Reg(0xD0)=%d\n", val);
    mutex_unlock(&ts_data->input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    struct fts_ts_data *ts_data = fts_data;

    mutex_lock(&ts_data->input_dev->mutex);
    if (FTS_SYSFS_ECHO_ON(buf)) {
        FTS_DEBUG("enable gesture");
        ts_data->gesture_mode = ENABLE;
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        FTS_DEBUG("disable gesture");
        ts_data->gesture_mode = DISABLE;
    }
    mutex_unlock(&ts_data->input_dev->mutex);

    return count;
}

static inline u16 get_gesture_coordinate(u16 value)
{
  return FTS_TOUCH_HIRES(swab16(value));
}


static ssize_t fts_gesture_buf_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    struct input_dev *input_dev = fts_data->input_dev;
    struct fts_gesture_st *gesture = &fts_data->fts_gesture_data;

    mutex_lock(&input_dev->mutex);
    count = snprintf(buf, PAGE_SIZE, "Gesture ID:%d\n", gesture->gesture_id);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture Points Buffer:\n");

    count += snprintf(buf + count, PAGE_SIZE, "(%4d,%4d) ",
                      gesture->coordinate_x, gesture->coordinate_y);
    count += snprintf(buf + count, PAGE_SIZE, "\n");
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_buf_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    return -EPERM;
}


/* sysfs gesture node
 *   read example: cat  fts_gesture_mode       ---read gesture mode
 *   write example:echo 1 > fts_gesture_mode   --- write gesture mode to 1
 *
 */
static DEVICE_ATTR(fts_gesture_mode, S_IRUGO | S_IWUSR, fts_gesture_show,
                   fts_gesture_store);
/*
 *   read example: cat fts_gesture_buf        --- read gesture buf
 */
static DEVICE_ATTR(fts_gesture_buf, S_IRUGO | S_IWUSR,
                   fts_gesture_buf_show, fts_gesture_buf_store);

static struct attribute *fts_gesture_mode_attrs[] = {
    &dev_attr_fts_gesture_mode.attr,
    &dev_attr_fts_gesture_buf.attr,
    NULL,
};

static struct attribute_group fts_gesture_group = {
    .attrs = fts_gesture_mode_attrs,
};

static int fts_create_gesture_sysfs(struct device *dev)
{
    int ret = 0;

    ret = sysfs_create_group(&dev->kobj, &fts_gesture_group);
    if (ret) {
        FTS_ERROR("gesture sys node create fail");
        sysfs_remove_group(&dev->kobj, &fts_gesture_group);
        return ret;
    }

    return 0;
}

/*****************************************************************************
* Name: fts_gesture_readdata
* Brief: Read information about gesture: enable flag/gesture points..., if ges-
*        ture enable, save gesture points' information, and report to OS.
*        It will be called this function every intrrupt when FTS_GESTURE_EN = 1
*
*        gesture data length: 1(enable) + 1(reserve) + 2(header) + 6 * 4
* Input: ts_data   - global struct data
*        is_report - whether report gesture event or not.
* Output:
* Return: 0 - read gesture data successfully, the report data is gesture data
*         1 - tp not in suspend/gesture not enable in TP FW
*         -Exx - error
*****************************************************************************/
int fts_gesture_readdata(struct fts_ts_data *ts_data)
{
    struct fts_gesture_st *gesture = &fts_data->fts_gesture_data;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
    struct gti_fw_status_data data;
#endif
    u8 cmd[2] = { 0 };

    cmd[0] = FTS_GESTURE_MAJOR_MINOR;

    /*if (!ts_data->suspended) {
        return -EINVAL;
    }*/
    fts_read(cmd, 1, gesture->data, sizeof(struct fts_gesture_st));

    gesture->coordinate_x = get_gesture_coordinate(gesture->coordinate_x);
    gesture->coordinate_y = get_gesture_coordinate(gesture->coordinate_y);
    gesture->x_valid = get_gesture_coordinate(gesture->x_valid);
    gesture->y_valid = get_gesture_coordinate(gesture->y_valid);
    gesture->x_final = get_gesture_coordinate(gesture->x_final);
    gesture->y_final = get_gesture_coordinate(gesture->y_final);
    gesture->move_distance = get_gesture_coordinate(gesture->move_distance);

    if (ts_data->log_level >= 1) {
      FTS_ERROR(
          "gesture_id=0x%x, x=%d, y=%d,"
          "major=%d, minor=%d, orientation=%d\n",
          gesture->gesture_id,
          gesture->coordinate_x, gesture->coordinate_y,
          gesture->major, gesture->minor, gesture->orientation);
    }

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
    switch (gesture->gesture_id) {
      case FTS_GESTURE_ID_STTW: {
        data.gesture_event.type = GTI_GESTURE_STTW;
        data.gesture_event.x = gesture->coordinate_x;
        data.gesture_event.y = gesture->coordinate_y;
        data.gesture_event.major = gesture->major;
        data.gesture_event.minor = gesture->minor;
        data.gesture_event.angle = gesture->orientation;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_GESTURE_EVENT,
                                      &data);
        break;
      }
      case FTS_GESTURE_ID_LPTW_DOWN: {
        data.gesture_event.type = GTI_GESTURE_LPTW;
        data.gesture_event.x = gesture->coordinate_x;
        data.gesture_event.y = gesture->coordinate_y;
        data.gesture_event.major = gesture->major;
        data.gesture_event.minor = gesture->minor;
        data.gesture_event.angle = gesture->orientation;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_GESTURE_EVENT,
                                      &data);
        break;
      }
      case FTS_STTW_INVALID_GESTURE_OUT_OF_RANGE: {
        data.invalid_gesture_event.type = GTI_STTW_INVALID_GESTURE_OUT_OF_RANGE;

        struct sttw_out_of_range_payload *payload =
            &data.invalid_gesture_event.payload.sttw_out_of_range;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_STTW_INVALID_GESTURE_SHIFT: {
        data.invalid_gesture_event.type = GTI_STTW_INVALID_GESTURE_SHIFT;

        struct sttw_shift_payload *payload =
            &data.invalid_gesture_event.payload.sttw_shift;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->x_final = gesture->x_final;
        payload->y_final = gesture->y_final;
        payload->distance = gesture->move_distance;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_STTW_INVALID_GESTURE_PALM: {
        data.invalid_gesture_event.type = GTI_STTW_INVALID_GESTURE_PALM;

        struct sttw_palm_payload *payload =
            &data.invalid_gesture_event.payload.sttw_palm;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->node_count = gesture->node_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_STTW_INVALID_GESTURE_MULTI_TOUCH: {
        data.invalid_gesture_event.type = GTI_STTW_INVALID_GESTURE_MULTI_TOUCH;

        struct sttw_multi_touch_payload *payload =
            &data.invalid_gesture_event.payload.sttw_multi_touch;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->finger_count = gesture->finger_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_STTW_INVALID_GESTURE_LONG_PRESS: {
        data.invalid_gesture_event.type = GTI_STTW_INVALID_GESTURE_LONG_PRESS;

        struct sttw_long_press_payload *payload =
            &data.invalid_gesture_event.payload.sttw_long_press;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->frame_count = gesture->frame_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_STTW_INVALID_GESTURE_RAPID_TOUCH: {
        data.invalid_gesture_event.type = GTI_STTW_INVALID_GESTURE_RAPID_TOUCH;

        struct sttw_rapid_touch_payload *payload =
            &data.invalid_gesture_event.payload.sttw_rapid_touch;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->frame_count = gesture->frame_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_LPTW_INVALID_GESTURE_OUT_OF_RANGE: {
        data.invalid_gesture_event.type = GTI_LPTW_INVALID_GESTURE_OUT_OF_RANGE;

        struct lptw_out_of_range_payload *payload =
            &data.invalid_gesture_event.payload.lptw_out_of_range;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->x_final = gesture->x_final;
        payload->y_final = gesture->y_final;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_LPTW_INVALID_GESTURE_SHIFT: {
        data.invalid_gesture_event.type = GTI_LPTW_INVALID_GESTURE_SHIFT;

        struct lptw_shift_payload *payload =
            &data.invalid_gesture_event.payload.lptw_shift;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->x_valid = gesture->x_valid;
        payload->y_valid = gesture->x_valid;
        payload->x_final = gesture->x_final;
        payload->y_final = gesture->y_final;
        payload->distance = gesture->move_distance;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_LPTW_INVALID_GESTURE_TOO_SMALL: {
        data.invalid_gesture_event.type = GTI_LPTW_INVALID_GESTURE_TOO_SMALL;

        struct lptw_too_small_payload *payload =
            &data.invalid_gesture_event.payload.lptw_too_small;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->node_count = gesture->node_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_LPTW_INVALID_GESTURE_PALM: {
        data.invalid_gesture_event.type = GTI_LPTW_INVALID_GESTURE_PALM;

        struct lptw_palm_payload *payload =
            &data.invalid_gesture_event.payload.lptw_palm;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->node_count = gesture->node_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      case FTS_LPTW_INVALID_GESTURE_RAPID_TOUCH: {
        data.invalid_gesture_event.type = GTI_LPTW_INVALID_GESTURE_RAPID_TOUCH;

        struct lptw_rapid_touch_payload *payload =
            &data.invalid_gesture_event.payload.lptw_rapid_touch;

        payload->x_down = gesture->coordinate_x;
        payload->y_down = gesture->coordinate_y;
        payload->frame_count = gesture->frame_count;
        goog_notify_fw_status_changed(ts_data->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
        break;
      }
      default:
        FTS_DEBUG("Unknown gesture id: 0x%x", gesture->gesture_id);
        break;
    }

#endif

    return 0;
}

int fts_gesture_init(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();
    fts_create_gesture_sysfs(ts_data->dev);

    memset(&ts_data->fts_gesture_data, 0, sizeof(struct fts_gesture_st));
    ts_data->gesture_mode = FTS_GESTURE_EN;

    FTS_FUNC_EXIT();
    return 0;
}

int fts_gesture_exit(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();
    sysfs_remove_group(&ts_data->dev->kobj, &fts_gesture_group);
    FTS_FUNC_EXIT();
    return 0;
}
