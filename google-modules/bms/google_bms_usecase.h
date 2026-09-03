/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024 Google, LLC
 *
 */

#ifndef GOOGLE_BMS_USECASE_H_
#define GOOGLE_BMS_USECASE_H_

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/klist.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define BMS_USECASE_MAX_ENTRIES 20
#define BMS_USECASE_INTERMEDIATE_STR "Intermediate"

struct gsu_usecase_config_t {
	int usecase;
	int val;
	const char *name;
	/* Add new fields after this */
	bool is_wireless;
	bool is_wired;
	bool is_otg;
	bool is_charging;
	bool is_cp;
	int chg_index; /* this is just used to determine if a charger has changed */

	bool is_standby;

	/* do not add after */
	struct hlist_node hnode;
};

enum bms_usecase_charger_index {
	BMS_USECASE_CHARGER_INDEX_INVALID = -1,
	BMS_USECASE_CHARGER_INDEX_MAIN, /* default */
	BMS_USECASE_CHARGER_INDEX_CP,
	BMS_USECASE_CHARGER_INDEX_HYBRID,
};

/*
 * Usecase Allocations
 *  -2: BMS_USECASE_NO_HOPS (reserved for usecase hop function)
 *  -1: GSU_RAW_MODE
 *   0: GSU_MODE_STANDBY
 *   1-199: gs101/gs201 Standard Usecases
 * 200-299: USB Charge Extended Usecases
 * 300-399: WLC Extended Usecases
 */
#define FOREACH_GSU_USECASE(S)	\
	S(BMS_USECASE_NO_HOPS, -2, "NO_HOPS", .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),	\
	/* raw mode, default, */								\
	S(GSU_RAW_MODE, -1, "RAW", .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),		\
												\
	S(GSU_MODE_STANDBY, 0, "Standby", .is_standby = true,					\
	.chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	/* 1-1 wired mode 0x4 */								\
	S(GSU_MODE_USB_CHG, 1, "USB", .is_wired = true),					\
	/* 1-1 wired mode 0x5 */								\
	S(GSU_MODE_USB_CHG_CHARGE_ENABLED, 201, "USB_CHG", .is_wired = true,			\
	 .is_charging = true),									\
	/* 1-2 wired mode 0x0/0x1 */								\
	S(GSU_MODE_USB_DC, 2, "USB_DC", .is_wired = true, .is_charging = true, .is_cp = true,	\
	 .chg_index = BMS_USECASE_CHARGER_INDEX_CP),						\
												\
	S(GSU_MODE_USB_CHG_HYBRID, 221, "USB_CHG_HYBRID", .is_wired = true,			\
	.is_charging = true, .chg_index = BMS_USECASE_CHARGER_INDEX_HYBRID),			\
	/* 2-1, 1041, */									\
	S(GSU_MODE_USB_CHG_WLC_TX, 3, "USB_CHG_RTX", .is_wired = true, .is_charging = true),	\
												\
	/* 3-1, mode 0x4 */									\
	S(GSU_MODE_WLC_RX, 5, "WLC_RX", .is_wireless = true),					\
	/* 3-1, mode 0x5 */									\
	S(GSU_MODE_WLC_RX_CHARGE_ENABLED, 305, "WLC_RX_CHG", .is_wireless = true,		\
	 .is_charging = true),									\
	S(GSU_MODE_WLC_RX_STDBY, 301, "WLC_RX_STDBY", .is_wireless = true, .is_standby = true,	\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	/* WLC spoofed */									\
	S(GSU_MODE_WLC_RX_SPOOFED, 300,	"WLC_RX_SPOOF", .is_wireless = true,			\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
												\
	S(GSU_MODE_WLC_RX_HYBRID, 325, "WLC_RX_HYBRID", .is_wireless = true,			\
	 .is_charging = true, .is_cp = false, .chg_index = BMS_USECASE_CHARGER_INDEX_HYBRID),	\
	/* 3-2, mode 0x0 */									\
	S(GSU_MODE_WLC_DC, 6, "WLC_DC", .is_wireless = true, .is_charging = true,		\
	 .is_cp = true, .chg_index = BMS_USECASE_CHARGER_INDEX_CP),				\
	S(GSU_MODE_USB_OTG_WLC_DC, 306, "OTG_WLC_DC", .is_wireless = true, .is_otg = true,	\
	 .is_charging = true, .is_cp = true, .chg_index = BMS_USECASE_CHARGER_INDEX_CP),	\
												\
	/* 7, 524, */										\
	S(GSU_MODE_USB_OTG_WLC_RX, 7, "OTG_WLC_RX", .is_wireless = true, .is_wired = false,	\
	 .is_otg = true, .is_charging = false, .is_cp = false),					\
	S(GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED, 307, "OTG_WLC_RX_CHG",			\
	  .is_wireless = true, .is_wired = false, .is_otg = true, .is_charging = true,		\
	  .is_cp = false),									\
												\
	S(GSU_MODE_USB_OTG_WLC_RX_HYBRID, 329, "OTG_WLC_HYBRID", .is_wireless = true,		\
	 .is_otg = true, .is_charging = true, .chg_index = BMS_USECASE_CHARGER_INDEX_HYBRID),	\
	/* 5-1, 516,*/										\
	S(GSU_MODE_USB_OTG, 9, "OTG", .is_otg = true,						\
	 .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	S(GSU_MODE_USB_OTG_FRS, 10, "OTG_FRS", .is_otg = true,					\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	/* 6-2, 1056, */									\
	S(GSU_MODE_WLC_TX, 11, "RTX", .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),		\
	S(GSU_MODE_USB_OTG_WLC_TX, 12, "OTG_RTX", .is_otg = true,				\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	S(GSU_MODE_USB_WLC_RX, 13, "USB_WLC_RX_CHG", .is_wireless = true,			\
	  .is_charging = true,),								\
												\
	S(GSU_MODE_DOCK, 14, "DOCK", .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),		\
	S(GSU_MODE_DOCK_CHARGE_ENABLED, 314, "DOCK_CHG", .is_charging = true),			\
												\
	/* check max77779_wcin_is_valid if modifying pogo vout */				\
	S(GSU_MODE_POGO_VOUT, 15, "POGO_VOUT", .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),	\
	S(GSU_MODE_USB_CHG_POGO_VOUT, 16, "USB_CHG_POGO_VOUT"),					\
	S(GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED, 216, "USB_CHG_POGO_VOUT",			\
	 .is_charging = true),									\
	S(GSU_MODE_USB_OTG_POGO_VOUT, 17, "USB_OTG_POGO_VOUT", .is_otg = true,			\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
												\
	/* ifpmic firmware update */								\
	S(GSU_MODE_FWUPDATE, 18, "IFPMIC_FWUPDATE",						\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	/* WLC fwupdate */									\
	S(GSU_MODE_WLC_FWUPDATE, 19, "WLC_FWUPDATE",						\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
												\
	/* indicates input_suspend and plugged in */						\
	S(GSU_MODE_STANDBY_BUCK_ON, 100, "STANDBY_BUCK_ON", .is_wired = true,			\
	 .is_standby = true, .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),			\
	/* WLC present but charger not selected */						\
	S(GSU_MODE_WLC_PRESENT, 101, "WLC_PRESENT", .is_wireless = true, .is_standby = true,	\
	 .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\
	S(GSU_MODE_INPUT_SUSPEND, 102, "INPUT_SUSPEND", .is_standby = true,			\
	  .chg_index = BMS_USECASE_CHARGER_INDEX_INVALID),					\

#define GSU_USECASE_CONFIG(...) { __VA_ARGS__ }							\

#define GSU_USECASE_ENUM_CONFIG(usecase, val, ...)						\
	usecase = val										\

enum gsu_usecases {
	FOREACH_GSU_USECASE(GSU_USECASE_ENUM_CONFIG)
};

/*
 * BMS usecase mode callback value parsing
 * based on uint32_t size but can be extended to use uint64_t if needed
 */
#define BMS_USECASE_BFF(name, h, l) \
static inline uint32_t _ ## name ## _set(uint32_t r, uint8_t v) \
{ \
	return ((r & ~GENMASK(h, l)) | v << l); \
} \
\
static inline uint32_t _ ## name ## _get(uint32_t r) \
{ \
	return ((r & GENMASK(h, l)) >> l); \
}

#define BMS_USECASE_MODE_SHIFT	0
#define BMS_USECASE_MODE_MASK	(0xffff << 0)
#define BMS_USECASE_MODE_CLEAR	(~(0xffff << 0))
#define BMS_USECASE_META_SHIFT	16
/* All metadata */
#define BMS_USECASE_META_MASK	(0xffff << 16)
#define BMS_USECASE_META_CLEAR	(~(0xffff << 16))

#define BMS_USECASE_META_ASYNC_SHIFT	16
#define BMS_USECASE_META_ASYNC_MASK	(0x1 << 16)
#define BMS_USECASE_META_ASYNC_CLEAR	(~(0x1 << 16))
#define BMS_USECASE_META_CHG_SEL_SHIFT	17
#define BMS_USECASE_META_CHG_SEL_MASK	(0x3 << 17)
#define BMS_USECASE_META_CHG_SEL_CLEAR	(~(0x3 << 17))

BMS_USECASE_BFF(bms_usecase_mode, 15, 0)
BMS_USECASE_BFF(bms_usecase_meta, 31, 16)
BMS_USECASE_BFF(bms_usecase_meta_async, 16, 16)
BMS_USECASE_BFF(bms_usecase_meta_chg_sel, 18, 17)

#define BMS_USECASE_CHG_SEL_INDEX	0
#define BMS_USECASE_CHG_SEL_INDEX_MASK	(0x3 << 0)
#define BMS_USECASE_CHG_SEL_INDEX_CLEAR	(~(0x3 << 0))
#define BMS_USECASE_CHG_SEL_TYPE	2
#define BMS_USECASE_CHG_SEL_TYPE_MASK	(0x3 << 2)
#define BMS_USECASE_CHG_SEL_TYPE_CLEAR	(~(0x3 << 2))

BMS_USECASE_BFF(bms_usecase_chg_sel_index, 1, 0)
BMS_USECASE_BFF(bms_usecase_chg_sel_type, 3, 2)

#define GSU_MODE_FWUPDATE_MASK (0x1 << 0)
#define GSU_MODE_WLC_FWUPDATE_MASK (0x1 << 1)

enum bms_usecase_status {
	BMS_USECASE_STATUS_UNINITIALIZED = 0,
	BMS_USECASE_STATUS_NEW,
	BMS_USECASE_STATUS_ORPHAN, /* New status for ownership transfer */
};

enum bms_usecase_state {
	BMS_USECASE_UNINITIALIZED = 0,
	BMS_USECASE_INITIAL,
	BMS_USECASE_INTERMEDIATE,
	BMS_USECASE_FORCE_USECASE,
};

/*
 * mode_callback: async entries free all
 * mode_callback: nope cb free entry only
 * mode_callback: blocking entries remove from list only
 * post_election_work: blocking entries free entry only
 */
enum bms_usecase_free_state {
	BMS_USECASE_FREE_ALL = 0,
	BMS_USECASE_FREE_FROM_LIST,
	BMS_USECASE_FREE_ENTRY,
};

/* ----------------------------------------------------------------------------
 * GS101 usecases
 * Platform specific, will need to be moved outside the driver.
 *
 * Case	USB_chg USB_otg	WLC_chg	WLC_TX	PMIC_Charger	Ext_B	LSx	Name
 * ----------------------------------------------------------------------------
 * 1-1	1	0	x	0	IF-PMIC-VBUS	0	0/0	USB_CHG
 * 1-2	2	0	x	0	DC VBUS		0	0/0	USB_DC
 * 2-1	1	0	0	1	IF-PMIC-VBUS	2	0/1	USB_CHG_WLC_TX
 * 2-2	2	0	0	1	DC CHG		2	0/1	USB_DC_WLC_TX
 * 3-1	0	0	1	0	IF-PMIC-WCIN	0	0/0	WLC_RX
 * 3-2	0	0	2	0	DC WCIN		0	0/0	WLC_DC
 * 4-1	0	1	1	0	IF-PMIC-WCIN	1	1/0	USB_OTG_WLC_RX
 * 4-2	0	1	2	0	DC WCIN		1	1/0	USB_OTG_WLC_DC
 * 5-1	0	1	0	0	0		1	1/0	USB_OTG
 * 5-2	0	1	0	0	OTG 5V		0	0/0	USB_OTG_FRS
 * 6-2	0	0	0	1	0		2	0/1	WLC_TX
 * 7-2	0	1	0	1	MW OTG 5V	2	0/1	USB_OTG_WLC_TX
 * 8	0	0	0	0	0		0	0/0	IDLE
 * ----------------------------------------------------------------------------
 *
 * Ext_Boost = 0 off, 1 = OTG 5V, 2 = WTX 7.5
 * USB_chg = 0 off, 1 = on, 2 = PPS
 * WLC_chg = 0 off, 1 = on, 2 = PPS
 */
struct bms_usecase_foreach_cb_data {
	const char *reason;

	int chgr_on;	/* CC_MAX != 0 */
	bool stby_on;	/* on disconnect, mode=0 */
	bool charge_done;
	bool charge_off;

	int chgin_off;	/* input_suspend, mode=0 */
	int wlcin_off;	/* input_suspend, mode=0 */
	int usb_wlc;	/* input_suspend, mode=0 */

	/* wlc_on is the same as wlc_rx */
	bool buck_on;	/* wired power in (chgin_on) from TCPCI */

	bool otg_on;	/* power out, usually external */
	bool frs_on;	/* power out, internal boost */

	int wlc_rx;	/* charging wireless */
	bool wlc_rx_stby;
	bool wlc_tx;	/* battery share */

	bool defender_enabled;

	int dc_on;	/* DC requested - wired or wireless */

	u32 raw_value;	/* hard override */
	bool use_raw;

	uint8_t fwupdate_on; /* enter firmware update mode */

	bool pogo_vin;	/* power in, pogo */
	bool pogo_vout;	/* power out, pogo */

	uint8_t chg_sel; /* charger selection */
};

struct bms_usecase_entry {
	int id;
	int usecase;

	char *reason;
	long value;

	enum bms_usecase_status status;

	struct completion completion;
	atomic_t completion_count;

	enum bms_usecase_state state;
	struct bms_usecase_foreach_cb_data *cb_data;
	bool trigger_cb;
	struct device *dev;

	bool processed_hop;

	int mode_cb_ret;
	int complete_uc_ret;
	int uc_work_ret;

	struct klist_node list_node;
};

struct bms_usecase_data {
	int cur_usecase;
	bool next_uc_charge_off;
	int next_usecase;
	struct bms_usecase_foreach_cb_data next_cb_data;

	struct klist queue;
	struct mutex queue_lock;
	struct delayed_work usecase_work;
	struct mutex usecase_work_lock;

	struct bms_usecase_entry pool[BMS_USECASE_MAX_ENTRIES];
	struct mutex pool_lock;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *de;
	struct delayed_work work;
#endif
	struct list_head subscribers;
	struct list_head completion_list;

	struct bms_usecase_chg_data *chg_data;
	struct wakeup_source *usecase_wake_lock;

	struct mutex mode_lock;
	struct gvotable_election *mode_votable;
	struct delayed_work mode_rerun_work;
};

typedef int (*bms_usecase_cb_get_hops)(void *uc_data, int from_uc, int to_uc);
typedef int (*bms_usecase_cb_get_usecase)(void *uc_data,
					  struct bms_usecase_foreach_cb_data *cb_data);

struct bms_usecase_chg_data {
	struct device *dev;
	int num_devices;
	void *uc_data;
	bms_usecase_cb_get_hops cb_get_hops;
	bms_usecase_cb_get_usecase cb_get_usecase;
};

typedef void (*bms_usecase_notify_cb)(void *data, enum gsu_usecases from_usecase,
				      enum gsu_usecases to_usecase);
typedef int (*bms_usecase_completion_cb)(void *data, struct bms_usecase_entry *entry);


struct bms_usecase_notify_cbs {
	bms_usecase_notify_cb uc_setup_cb;
	bms_usecase_notify_cb uc_transition_cb;
	bms_usecase_notify_cb uc_changed_cb;
};

int bms_usecase_init(struct bms_usecase_chg_data *chg_data);
void bms_usecase_remove(void);
int bms_usecase_get_usecase(void);
int bms_usecase_register_notifiers(void *data, struct bms_usecase_notify_cbs cbs,
				   const char *identifier);
int bms_usecase_register_completion_cb(void *data, bms_usecase_completion_cb from_uc_cb,
				       bms_usecase_completion_cb to_uc_cb);
const char *bms_usecase_to_str(enum gsu_usecases usecase);
bool bms_usecase_is_uc_wireless(enum gsu_usecases usecase);
bool bms_usecase_is_uc_wired(enum gsu_usecases usecase);
bool bms_usecase_is_uc_charging_enabled(enum gsu_usecases usecase);
bool bms_usecase_next_uc_charge_off(void);
bool bms_usecase_is_chg_changed(enum gsu_usecases from_uc, enum gsu_usecases to_uc);
bool bms_usecase_is_uc_cp(enum gsu_usecases usecase);
bool bms_usecase_is_uc_otg(enum gsu_usecases usecase);
bool bms_usecase_is_uc_standby(enum gsu_usecases usecase);
int bms_usecase_get_cur_chg_sel(void);
#endif
