/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google, LLC
 *
 */

#ifndef MAX77779_CHARGER_H_
#define MAX77779_CHARGER_H_

#include <linux/gpio.h>
#if IS_ENABLED(CONFIG_GPIOLIB)
#include <linux/gpio/driver.h>
#endif

#define MAX77779_COP_SENSE_RESISTOR_VAL 2 /* 2mOhm */
#define MAX77779_COP_MAX_VALUE (0xffff * 1000 / MAX77779_COP_SENSE_RESISTOR_VAL)
#define MAX77779_COP_WARN_THRESHOLD 105 /* Percentage */
#define MAX77779_COP_MIN_DEBOUNCE_TIME_MS 16
#define MAX77779_CHG_NUM_IRQS 16
#define MAX77779_WCIN_INLIM_DEFAULT_DEBOUNCE 3
#define MAX77779_WCIN_EMA_TIME_MS 500
#define MAX77779_WCIN_EMA_ALPHA_THOUSANDTHS 400
#define MAX77779_DC_ICL_STEP 25000

#define MAX77779_WLC_SPOOF_VBYP_OF_STRING "max77779,wlc-spoof-vbyp"

struct max77779_chgr_data {
	struct device *dev;

	struct delayed_work init_work;

	/* Charger sub-IRQ routing for COP */
	struct irq_domain	*domain;
	uint32_t		mask;
	uint32_t		mask_u;  /* pending updates */
	struct mutex 		irq_lock;

	struct power_supply *psy;
	struct power_supply *wcin_psy;
	struct power_supply *chgin_psy;

	struct power_supply *wlc_psy;
	struct power_supply *fg_psy;
	struct regmap *regmap;

	struct gvotable_election *mode_votable;

	struct delayed_work check_fet_work;

	bool wlcin_is_child;
	struct gvotable_election *dc_icl_votable;
	struct gvotable_election *dc_suspend_votable;
	struct gvotable_election *wlc_spoof_votable;

	struct delayed_work cop_enable_work;
	uint32_t cop_warn;
	uint32_t cc_max;
	int input_uv;
	uint32_t orig_ilim;
	struct mutex ilim_lock;

	/* wcin inlim tracking */
	struct delayed_work wcin_inlim_work;
	struct delayed_work wcin_ema_work;
	uint32_t wcin_inlim_t;
	int wcin_inlim_flag;
	int wcin_inlim_debounce_count;
	uint32_t wcin_inlim_headroom;
	uint32_t wcin_inlim_step;
	uint32_t wcin_inlim_debounce_thresh;
	uint32_t wcin_soft_icl;
	uint32_t wcin_inlim_en;
	uint32_t wcin_inlim_avail;
	uint32_t dc_icl;
	int wcin_ema;
	bool wcin_ema_disable;
	uint32_t wcin_ema_t;
	uint32_t wcin_ema_alpha;
	struct mutex wcin_inlim_lock;

#if IS_ENABLED(CONFIG_GPIOLIB)
	struct gpio_chip gpio;
#endif

	bool charge_done;
	bool chgin_input_suspend;
	bool wcin_input_suspend;
	bool thm2_sts;
	bool cpm_exists;

	int irq_gpio;
	int irq_int;
	bool disable_internal_irq_handler;

	struct device *pmic_dev;

	struct dentry *de;

	atomic_t insel_cnt;
	bool insel_clear;	/* when set, irq clears CHGINSEL_MASK */

	atomic_t early_topoff_cnt;

	struct mutex io_lock;
	struct mutex prot_lock;
	bool init_complete;
	//struct wakeup_source *inlim_wake_lock;

	int fship_dtls;
	bool online;
	bool wden;

	/* Force to change FCCM mode during OTG at high battery voltage */
	bool otg_changed;

	/* debug interface, register to read or write */
	u32 debug_reg_address;

	struct gvotable_election *msc_last_votable;
	int chg_term_voltage;
	int chg_term_volt_debounce;

	bool msc_pwr_voter_active;

	struct thermal_zone_device *chg_vs_thm2_tz;

	/* control cv reporting in wlc inlim */
	int prev_vbatt;
	u8 prev_chg_type;
	int fv_cv_margin;
	int fv_cv_debounce;
	int fv_cv_dcr;
	bool wlc_inlim_cv;
};
int max77779_charger_init(struct max77779_chgr_data *data);
void max77779_charger_remove(struct max77779_chgr_data *data);
bool max77779_chg_is_reg(struct device *dev, unsigned int reg);
int max77779_wcin_is_online(struct max77779_chgr_data *data);
#if IS_ENABLED(CONFIG_PM)
int max77779_charger_pm_suspend(struct device *dev);
int max77779_charger_pm_resume(struct device *dev);
#endif
int max77779_get_charge_enabled(struct max77779_chgr_data *data, int *enabled);
#endif
