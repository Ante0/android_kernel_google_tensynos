/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform data for the PE26100 buck charger driver.
 */

#ifndef _PE26100_CHG_BUCK_CHARGER_H_
#define _PE26100_CHG_BUCK_CHARGER_H_

#include "gbms_irq.h"

#define PE26100_BUCK_MAX_INPUT_VOLTAGE 9000000

enum pe26100_buck_mode {
	PE26100_BUCK_MODE_INVALID,
	PE26100_BUCK_MODE_WIRED,
	PE26100_BUCK_MODE_WIRELESS,
};

enum pe26100_buck_aicl_status {
	PE26100_BUCK_AICL_STATUS_DEFAULT,
	PE26100_BUCK_AICL_STATUS_RUNNING,
	PE26100_BUCK_AICL_STATUS_CANCELLED,
	PE26100_BUCK_AICL_STATUS_COMPLETE,
};

struct pe26100_buck_charger {
	struct device *dev;
	struct device *core;

	bool init_complete;

	struct wakeup_source *aicl_wake_lock;
	struct delayed_work aicl_work;
	bool aicl_enabled;
	enum pe26100_buck_aicl_status aicl_status;
	int aicl_ilim_offset;

	struct delayed_work init_work;

	struct mutex mode_lock;
	enum pe26100_buck_mode cur_buck_mode;

	struct gpio_desc *irq_gpio;
	int irq_int;
	bool irq_enabled;
	gbms_irq_t irq_data;

	/* Charger sub-IRQ routing */
	struct irq_domain *domain;
	struct mutex irq_lock;
	uint8_t mask;

	struct power_supply *psy;

	const char *wcin_name;
	struct power_supply *wcin_psy;

	struct mutex charge_lock;
	int cc_max;
	int ilim;
	int fv_uv;
	int wlc_ilim;

	int input_uv;

	struct gvotable_election *mode_votable;
	struct gvotable_election *dc_avail_votable;
};

int pe26100_buck_is_online(const enum pe26100_buck_mode buck_mode);
int pe26100_buck_set_online(bool online, const enum pe26100_buck_mode mode);
void pe26100_buck_set_init_complete(bool complete);
void pe26100_buck_set_active(bool active);
int pe26100_buck_apply_charger_current_max_ua(void);
int pe26100_buck_apply_regulation_voltage(void);
int pe26100_buck_apply_ilim_max_ua(const enum pe26100_buck_mode mode);
void pe26100_buck_enable_irq(bool enable);
int pe26100_buck_vote_dc_avail(int vote, int enable);
void pe26100_buck_charger_schedule_aicl_work(bool enable);
#endif /* PE26100_CHG_BUCK_CHARGER */
