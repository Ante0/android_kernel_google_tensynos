/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2024 Google LLC.
 *
 * Device Tree binding constants for GOOGLE Thermal
 */
#ifndef _GOOGLE_THERMAL_DEF_H
#define _GOOGLE_THERMAL_DEF_H

/* SW thermal zones definition of Google thermal driver */
#define TZ_BIG		(0)
#define TZ_BIG_MID	(1)
#define TZ_MID		(2)
#define TZ_LIT		(3)
#define TZ_GPU		(4)
#define TZ_TPU		(5)
#define TZ_AUR		(6)
#define TZ_ISP		(7)
#define TZ_MEM		(8)
#define TZ_AOC		(9)
#define TZ_MIDLL	(10)
#define TZ_CME		(11)
#define TZ_DSU		(12)
#define TZ_ISPFE	(13)
#define TZ_ISPBE	(14)
#define TZ_C3P		(15)
#define TZ_AOSS		(16)
#define TZ_MAX		(17)

/* HW Cdev definitions. Align with hardware cdev definition in CPM mbox header. */
#define HW_DT_CDEV_BIG		(0)
#define HW_DT_CDEV_BIG_MID	(1)
#define HW_DT_CDEV_MID		(2)
#define HW_DT_CDEV_LIT		(3)
#define HW_DT_CDEV_GPU		(4)
#define HW_DT_CDEV_TPU		(5)
#define HW_DT_CDEV_AUR		(6)
#define HW_DT_CDEV_MIDLL	(10)

/* Gpowercap node types */
#define GPOWERCAP_NODE_CPU		0
#define GPOWERCAP_NODE_DEVFREQ		1
/* All actual nodes should be before virtual type node. */
#define GPOWERCAP_NODE_VIRTUAL		2
#define GPOWERCAP_NODE_VIRTUAL_VOLTAGE	3
#define GPOWERCAP_NODE_VIRTUAL_WEIGHTS	4

#endif // _GOOGLE_THERMAL_DEF_H
