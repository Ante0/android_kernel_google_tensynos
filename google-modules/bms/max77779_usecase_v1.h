/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google, LLC
 *
 */

#ifndef MAX77779_USECASE_V1_H_
#define MAX77779_USECASE_V1_H_

#include "google_bms_usecase.h"
#include "max77779_usecase.h"

#define MAX77779_CHG_CNFG_05_WCSM_ILIM_1400_MA 0xA
#define MAX77779_CHG_TX_RETRIES 10

struct max77779_uc_v1_data {
	u8 reg;					/* max77779 charger reg */
	bool mode_cb_debounce;			/* debounce mode callback */
	struct gpio_desc *bst_on;		/* ext boost */
	struct gpio_desc *ext_bst_mode;		/* ext boost mode */
	struct gpio_desc *ext_bst_ctl;		/* SEQ VENDOR_EXTBST.EXT_BST_EN */
	struct regulator *ext_bst_supply;
	struct mutex ext_bst_lock;
	bool rx_otg_en;				/* enable WLC_RX -> WLC_RX + OTG case */
	bool otg_wlc_dc_en;			/* enables OTG_WLC_DC usecase */
	bool ext_otg_only;			/* use external OTG only */
	int dc_sw_gpio;				/* WLC-DC switch enable */
	struct gpio_desc *pogo_vout_en;		/* pogo 5V vout */

	int vin_is_valid;			/* MAX20339 STATUS1.vinvalid */

	struct gpio_desc *wlc_en;		/* wlcrx/chgin coex */
	int wlc_vbus_en;			/* b/202526678 */
	bool chrg_byp_en;			/* charger mode 0x1 */
	bool slow_wlc_ilim;	/* ILIM SPEED to slow during WLC */
	bool wlc_spoof;
	struct gpio_desc *wlc_spoof_gpio;	/* wlcrx thermal throttle */
	u32 wlc_spoof_vbyp;			/* wlc spoof VBYP */
	bool ext_rx_otg;			/* ext boost for WLC_RX and OTG */
	bool ext_bst_on;			/* ext boost state */

	struct device *dev;
	struct device *core;
	int init_done;

	struct gpio_desc *rtx_ready; /* rtx ready gpio from wireless */
	struct gpio_desc *rtx_available; /* rtx supported gpio from wlc, usecase set for UI */

	bool dcin_is_dock;

	struct gvotable_election *wlc_spoof_votable;

	struct max77779_usecase_data common_data;
};

enum wlc_state_t {
	WLC_DISABLED = 0,
	WLC_ENABLED = 1,
	WLC_SPOOFED = 2,
};

int max77779_usecase_v1_setup_usecases(void **uc_data, struct device *dev);
int max77779_usecase_v1_usecase_remove(void *uc_data);
#endif /* MAX77779_USECASE_V1_H_ */
