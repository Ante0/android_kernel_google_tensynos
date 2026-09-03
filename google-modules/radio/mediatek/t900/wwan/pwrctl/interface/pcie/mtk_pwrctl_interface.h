// SPDX-License-Identifier: GPL-2.0 only.
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

enum pwrctl_controller_request {
        PWRCTL_CONTROLLER_REQUEST_SCAN_PORT = 0,
        PWRCTL_CONTROLLER_REQUEST_REMOVE_PORT,
        PWRCTL_CONTROLLER_REQUEST_SOFT_ON,
        PWRCTL_CONTROLLER_REQUEST_SOFT_OFF,
        PWRCTL_CONTROLLER_REQUEST_MAXNUM = PWRCTL_CONTROLLER_REQUEST_SOFT_OFF  // go/NOTYPO
};

extern int mtk_pwrctl_request_to_controller(struct pwrctl_mdev *mdev, int type);
extern int mtk_pwrctl_controller_pin_off(struct pwrctl_mdev *mdev);
extern int mtk_pwrctl_controller_disable_refclk(struct pwrctl_mdev *mdev);
extern int mtk_pwrctl_controller_disable_data_trans(struct pwrctl_mdev *mdev);
