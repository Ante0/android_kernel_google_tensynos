/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 */
#ifndef __EBU_PLATFORM_H__
#define __EBU_PLATFORM_H__

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/usb.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <ebu/ebu.h>
#include "ebu_google.h"

int mbu_google_ebu_setup(struct google_ebu *gebu, struct platform_device *pdev);
int mbu_reinit_ebu(struct google_ebu *gebu, enum usb_device_speed speed);

int lga_google_ebu_setup(struct google_ebu *gebu, struct platform_device *pdev);
int lga_reinit_ebu(struct google_ebu *gebu, enum usb_device_speed speed);

#endif /* __EBU_PLATFORM_H__ */
