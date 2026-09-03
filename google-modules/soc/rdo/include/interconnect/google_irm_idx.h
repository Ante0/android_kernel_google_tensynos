/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_IDX_H
#define _GOOGLE_IRM_IDX_H

#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_SOC_LGA)
#include <dt-bindings/interconnect/google,lga,irm.h>
#elif IS_ENABLED(CONFIG_SOC_MBU)
#include <dt-bindings/interconnect/google,mbu,irm.h>
#endif

#endif /* _GOOGLE_IRM_IDX_H */
