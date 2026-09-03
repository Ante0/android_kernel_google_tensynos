// SPDX-License-Identifier: GPL-2.0
/*
 * Google BMS Common Usecase Driver
 *
 * Copyright 2024 Google LLC
 *
 */

#include <linux/debugfs.h>
#include <linux/hashtable.h>

#include <misc/gvotable.h>

#include "google_bms.h"
#include "google_bms_usecase.h"

struct bms_usecase_notify_data {
	void *data;
	struct bms_usecase_notify_cbs cbs;
	char identifier[GVOTABLE_MAX_REASON_LEN];
	struct list_head list;
};

struct bms_usecase_completion_data {
	void *data;
	bms_usecase_completion_cb from_uc_cb;
	bms_usecase_completion_cb to_uc_cb;
	struct list_head list;
};

static struct bms_usecase_data *singleton_bms_uc_data;

static struct gsu_usecase_config_t _gsu_usecase_config[] = {
	FOREACH_GSU_USECASE(GSU_USECASE_CONFIG)
};

#define BMS_USECASE_NUM_USECASES (sizeof(_gsu_usecase_config) / sizeof(struct gsu_usecase_config_t))

/* 1 << 5 = 32 entries */
#define BMS_USECASE_HASHTABLE_SIZE	5
DECLARE_HASHTABLE(gsu_usecase_table, BMS_USECASE_HASHTABLE_SIZE);

static void bms_usecase_init_uc_hash_table(void)
{
	int i;

	for (i = 0; i < BMS_USECASE_NUM_USECASES; i++) {
		hash_add(gsu_usecase_table, &_gsu_usecase_config[i].hnode,
			 _gsu_usecase_config[i].usecase);
	}
}

const char *bms_usecase_to_str(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->name;
	}

	return "unknown";
}
EXPORT_SYMBOL_GPL(bms_usecase_to_str);

bool bms_usecase_is_uc_wireless(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->is_wireless;
	}

	if (singleton_bms_uc_data)
		dev_err(singleton_bms_uc_data->chg_data->dev, "%s: Error could not find usecase %d\n",
			__func__, usecase);

	return false;
}
EXPORT_SYMBOL_GPL(bms_usecase_is_uc_wireless);

bool bms_usecase_is_uc_wired(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->is_wired;
	}

	if (singleton_bms_uc_data)
		dev_err(singleton_bms_uc_data->chg_data->dev, "%s: Error could not find usecase %d\n",
			__func__, usecase);

	return false;
}
EXPORT_SYMBOL_GPL(bms_usecase_is_uc_wired);

bool bms_usecase_is_uc_standby(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->is_standby;
	}

	if (singleton_bms_uc_data)
		dev_err(singleton_bms_uc_data->chg_data->dev, "%s: Error could not find usecase %d\n",
			__func__, usecase);

	return true;
}
EXPORT_SYMBOL_GPL(bms_usecase_is_uc_standby);

bool bms_usecase_is_uc_charging_enabled(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config && config->usecase == usecase)
			return config->is_charging;
	}

	if (singleton_bms_uc_data)
		dev_err(singleton_bms_uc_data->chg_data->dev, "%s: Error could not find usecase %d\n",
			__func__, usecase);

	return false;
}
EXPORT_SYMBOL_GPL(bms_usecase_is_uc_charging_enabled);

bool bms_usecase_next_uc_charge_off(void)
{
	return singleton_bms_uc_data && singleton_bms_uc_data->next_uc_charge_off;
}
EXPORT_SYMBOL_GPL(bms_usecase_next_uc_charge_off);

static int bms_usecase_get_chg_sel(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->chg_index;
	}

	return BMS_USECASE_CHARGER_INDEX_INVALID;
}

bool bms_usecase_is_chg_changed(enum gsu_usecases from_uc, enum gsu_usecases to_uc)
{
	return bms_usecase_get_chg_sel(from_uc) != bms_usecase_get_chg_sel(to_uc);
}
EXPORT_SYMBOL_GPL(bms_usecase_is_chg_changed);

/*
 * CHG sel is already pre-processed in get_usecase to be the correct index
 */
int bms_usecase_get_cur_chg_sel(void)
{
	struct bms_usecase_data *bms_uc_data = singleton_bms_uc_data;
	struct bms_usecase_foreach_cb_data *cb_data = &bms_uc_data->next_cb_data;
	const int chg_sel = bms_usecase_get_chg_sel(bms_uc_data->next_usecase);

	if (chg_sel > BMS_USECASE_CHARGER_INDEX_MAIN)
		return _bms_usecase_chg_sel_index_get(cb_data->chg_sel);
	return chg_sel;
}
EXPORT_SYMBOL_GPL(bms_usecase_get_cur_chg_sel);

bool bms_usecase_is_uc_cp(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->is_cp;
	}

	if (singleton_bms_uc_data)
		dev_err(singleton_bms_uc_data->chg_data->dev, "%s: Error could not find usecase %d\n",
			__func__, usecase);

	return false;
}
EXPORT_SYMBOL_GPL(bms_usecase_is_uc_cp);

bool bms_usecase_is_uc_otg(enum gsu_usecases usecase)
{
	struct gsu_usecase_config_t *config;

	hash_for_each_possible(gsu_usecase_table, config, hnode, usecase) {
		if (config && config->usecase == usecase)
			return config->is_otg;
	}

	if (singleton_bms_uc_data)
		dev_err(singleton_bms_uc_data->chg_data->dev, "%s: Error could not find usecase %d\n",
			__func__, usecase);

	return false;
}
EXPORT_SYMBOL_GPL(bms_usecase_is_uc_otg);

static struct bms_usecase_entry *bms_usecase_alloc_node(struct bms_usecase_data *bms_uc_data,
							enum bms_usecase_state state,
							const char *reason,
							long value)
{
	int i;
	struct bms_usecase_entry *entry = NULL;
	struct device *dev;

	if (!singleton_bms_uc_data)
		return NULL;

	dev = bms_uc_data->chg_data->dev;

	mutex_lock(&bms_uc_data->pool_lock);
	for (i = 0; i < BMS_USECASE_MAX_ENTRIES; i++) {
		struct bms_usecase_entry *tmp = &bms_uc_data->pool[i];

		if (tmp->state != BMS_USECASE_UNINITIALIZED)
			continue;

		entry = tmp;
		entry->state = state;
		entry->status = BMS_USECASE_STATUS_NEW;
		strscpy(entry->reason, reason, GVOTABLE_MAX_REASON_LEN);
		entry->value = value;

		dev_dbg(dev, "Allocating node:%d state:%d reason:%s value:0x%lx\n",
			entry->id, state, entry->reason, entry->value);
		break;
	}
	mutex_unlock(&bms_uc_data->pool_lock);

	return entry;
}

static struct bms_usecase_entry *bms_usecase_get_new_node(struct bms_usecase_data *bms_uc_data,
						   const char *reason,
						   long value)
{
	return bms_usecase_alloc_node(bms_uc_data, BMS_USECASE_INITIAL,
				      reason, value);
}

static struct bms_usecase_entry*
bms_usecase_get_intermediate_node(struct bms_usecase_data *bms_uc_data)
{
	struct bms_usecase_entry *entry = bms_usecase_alloc_node(bms_uc_data,
								 BMS_USECASE_INTERMEDIATE,
								 BMS_USECASE_INTERMEDIATE_STR,
								 _bms_usecase_meta_async_set(0, 1));
	if (!entry)
		return entry;

	entry->cb_data->reason = BMS_USECASE_INTERMEDIATE_STR;
	return entry;
}

static int bms_usecase_append_standby_to_queue(struct bms_usecase_data *bms_uc_data, bool buck_on)
{
	struct bms_usecase_entry *entry = bms_usecase_get_intermediate_node(bms_uc_data);

	if (!entry)
		return -ENOMEM;

	mutex_lock(&bms_uc_data->queue_lock);
	entry->usecase = buck_on ? GSU_MODE_STANDBY_BUCK_ON : GSU_MODE_STANDBY;
	klist_add_head(&entry->list_node, &bms_uc_data->queue);
	mutex_unlock(&bms_uc_data->queue_lock);

	return 0;
}

static struct bms_usecase_entry *bms_usecase_get_entry(struct bms_usecase_data *bms_uc_data,
						       int entry_id)
{
	if (entry_id >= BMS_USECASE_MAX_ENTRIES || entry_id < 0)
		return NULL;

	return &bms_uc_data->pool[entry_id];
}

static int bms_usecase_add_hop(struct bms_usecase_data *bms_uc_data,
			       const int from_uc,
			       const int to_uc,
			       struct bms_usecase_entry *orig_entry,
			       int *hops)
{
	int temp_uc, ret;
	struct bms_usecase_entry *new_entry;
	void *uc_data = bms_uc_data->chg_data->uc_data;
	struct device *dev = bms_uc_data->chg_data->dev;

	dev_dbg(dev, "%s: from_uc:%d to_uc:%d\n", __func__, from_uc, to_uc);

	if (!bms_uc_data->chg_data->cb_get_hops || (from_uc == to_uc))
		return 0;

	temp_uc = bms_uc_data->chg_data->cb_get_hops(uc_data, from_uc, to_uc);
	if (temp_uc == BMS_USECASE_NO_HOPS)
		return 0;
	if (temp_uc < 0) {
		dev_err(dev, "error in hop func:%d\n", temp_uc);
		return temp_uc;
	}

	ret = bms_usecase_add_hop(bms_uc_data, temp_uc, to_uc, orig_entry, hops);
	if (ret == -ENOMEM)
		return ret;

	new_entry = bms_usecase_get_intermediate_node(bms_uc_data);
	if (!new_entry) {
		dev_err(dev, "No mem in pool\n");
		return -ENOMEM;
	}

	new_entry->usecase = temp_uc;
	new_entry->processed_hop = true;
	/* entry->value and cb-data->value are unused in this case */

	dev_dbg(dev, "Adding hop node:%d usecase:%d\n", new_entry->id,
		new_entry->usecase);

	/* queue lock is already held */
	klist_add_head(&new_entry->list_node, &bms_uc_data->queue);

	*hops += 1;

	return 0;
}

static int bms_usecase_add_hops(struct bms_usecase_data *bms_uc_data,
				struct bms_usecase_entry *entry)
{
	int ret;
	int hops = 0;

	if (entry->processed_hop)
		return 0;

	mutex_lock(&bms_uc_data->queue_lock);

	ret = bms_usecase_add_hop(bms_uc_data, bms_uc_data->cur_usecase,
				  entry->usecase, entry, &hops);
	if (ret == 0)
		entry->processed_hop = true;

	mutex_unlock(&bms_uc_data->queue_lock);

	return ret ? ret : hops;
}

static struct klist_node *bms_usecase_queue_next(struct bms_usecase_data *bms_uc_data,
					  struct klist_iter *iter)
{
	struct klist_node *node;

	mutex_lock(&bms_uc_data->queue_lock);
	node = klist_next(iter);
	mutex_unlock(&bms_uc_data->queue_lock);

	return node;
}

static void bms_usecase_add_tail_locked(struct bms_usecase_data *bms_uc_data,
					struct bms_usecase_entry *entry)
{
	struct device *dev = bms_uc_data->chg_data->dev;

	dev_dbg(dev, "Adding node:%d blocking:%d\n", entry->id,
		!_bms_usecase_meta_async_get(entry->value));

	klist_add_tail(&entry->list_node, &bms_uc_data->queue);
}

static void bms_usecase_add_tail(struct bms_usecase_data *bms_uc_data,
				 struct bms_usecase_entry *entry)
{
	mutex_lock(&bms_uc_data->queue_lock);
	bms_usecase_add_tail_locked(bms_uc_data, entry);
	mutex_unlock(&bms_uc_data->queue_lock);
}

static void bms_usecase_up(struct bms_usecase_entry *entry)
{
	dev_dbg(entry->dev, "%s: node:%d\n", __func__, entry->id);

	atomic_inc(&entry->completion_count);
	complete(&entry->completion);
}

static void bms_usecase_reset_completion(struct bms_usecase_entry *entry)
{
	reinit_completion(&entry->completion);
	atomic_set(&entry->completion_count, 0);
}

static int bms_usecase_down(struct bms_usecase_entry *entry)
{
	int ret, complete_count;

	dev_dbg(entry->dev, "%s: node:%d\n", __func__, entry->id);

	ret = wait_for_completion_interruptible(&entry->completion);
	if (ret < 0) {
		dev_err(entry->dev, "Error! Can not complete on node:%d ret:%d\n",
			entry->id, ret);
		return ret;
	}
	complete_count = atomic_read(&entry->completion_count);

	ret = atomic_dec_and_test(&entry->completion_count);
	if (!ret) {
		dev_err(entry->dev, "Error! Atomic complete count mismatch %d->%d ... resetting state on node:%d\n",
			complete_count, atomic_read(&entry->completion_count), entry->id);
		bms_usecase_reset_completion(entry);
	}
	return 0;
}

static void bms_usecase_free_node_locked(struct bms_usecase_data *bms_uc_data,
				  struct bms_usecase_entry *entry,
				  enum bms_usecase_free_state free_state)
{
	struct device *dev = bms_uc_data->chg_data->dev;

	dev_dbg(dev, "Freeing node:%d free_state:%d\n", entry->id, free_state);

	if (free_state == BMS_USECASE_FREE_FROM_LIST || free_state == BMS_USECASE_FREE_ALL) {
		klist_remove(&entry->list_node);

		if (!_bms_usecase_meta_async_get(entry->value) &&
			entry->status != BMS_USECASE_STATUS_ORPHAN)
			bms_usecase_up(entry);
	}

	if (free_state == BMS_USECASE_FREE_ENTRY || free_state == BMS_USECASE_FREE_ALL) {
		mutex_lock(&bms_uc_data->pool_lock);
		entry->usecase = 0;
		entry->processed_hop = false;
		entry->state = BMS_USECASE_UNINITIALIZED;
		entry->status = BMS_USECASE_STATUS_UNINITIALIZED;
		entry->value = 0;
		entry->mode_cb_ret = 0;
		entry->uc_work_ret = 0;
		entry->complete_uc_ret = 0;
		entry->trigger_cb = false;

		/* completion_count and completion state are not reset */
		memset(entry->cb_data, 0, sizeof(*entry->cb_data));
		memset(entry->reason, 0, GVOTABLE_MAX_REASON_LEN);
		mutex_unlock(&bms_uc_data->pool_lock);
	}
}

/*
 * When free_state is BMS_USECASE_FREE_FROM_LIST or BMS_USECASE_FREE_ALL, requires
 * &bms_uc_data->queue_lock to be held
 */
static void bms_usecase_free_node(struct bms_usecase_data *bms_uc_data,
				  struct bms_usecase_entry *entry,
				  enum bms_usecase_free_state free_state)
{
	mutex_lock(&bms_uc_data->queue_lock);
	bms_usecase_free_node_locked(bms_uc_data, entry, free_state);
	mutex_unlock(&bms_uc_data->queue_lock);
}

static void bms_usecase_clear_queue(struct bms_usecase_data *bms_uc_data, int err)
{
	struct klist_iter iter;
	struct klist_node *node;
	struct bms_usecase_entry *entry;
	struct device *dev = bms_uc_data->chg_data->dev;

	dev_warn(dev, "Clearing queue\n");
	mutex_lock(&bms_uc_data->queue_lock);

	klist_iter_init(&bms_uc_data->queue, &iter);

	/* klist_next requires &bms_uc_data->queue_lock */
	node = klist_next(&iter);
	while (node) {
		entry = container_of(node, struct bms_usecase_entry, list_node);

		node = klist_next(&iter);
		entry->uc_work_ret = err;
		bms_usecase_free_node_locked(bms_uc_data, entry,
				      !_bms_usecase_meta_async_get(entry->value));
	}

	klist_iter_exit(&iter);
	mutex_unlock(&bms_uc_data->queue_lock);
}

int bms_usecase_get_usecase(void)
{
	if (!singleton_bms_uc_data)
		return -EINVAL;

	return singleton_bms_uc_data->cur_usecase;
}
EXPORT_SYMBOL_GPL(bms_usecase_get_usecase);

/* First step to convert votes to a usecase and a setting for mode */
static int bms_usecase_foreach_callback(void *data, const char *reason, void *vote)
{
	struct bms_usecase_foreach_cb_data *cb_data = data;
	int mode = _bms_usecase_mode_get((long)vote); /* max77779_mode is an int election */
	int temp;

	switch (mode) {
	/* SYSTEM modes can add complex transactions */

	/* MAX77779: on disconnect */
	case GBMS_CHGR_MODE_STBY_ON:
		if (!cb_data->stby_on)
			cb_data->reason = reason;
		pr_debug("%s: STBY_ON %s vote=0x%x\n",
			 __func__, reason ? reason : "<>", mode);
		cb_data->stby_on += 1;
		break;
	/* USB+WLCIN, factory only */
	case GBMS_CHGR_MODE_USB_WLC_RX:
		pr_debug("%s: USB_WLC_RX %s vote=0x%x\n",
			 __func__, reason ? reason : "<>", mode);
		if (!cb_data->usb_wlc)
			cb_data->reason = reason;
		cb_data->usb_wlc += 1;
		break;
	/* input_suspend => 0 ilim */
	case GBMS_CHGR_MODE_CHGIN_OFF:
		if (!cb_data->chgin_off)
			cb_data->reason = reason;
		pr_debug("%s: CHGIN_OFF %s vote=0x%x\n", __func__,
			 reason ? reason : "<>", mode);
		cb_data->chgin_off += 1;
		break;
	/* input_suspend => DC_SUSPEND */
	case GBMS_CHGR_MODE_WLCIN_OFF:
		if (!cb_data->wlcin_off)
			cb_data->reason = reason;
		pr_debug("%s: WLCIN_OFF %s vote=0x%x\n", __func__,
			 reason ? reason : "<>", mode);
		cb_data->wlcin_off += 1;
		if (strcmp(reason, MSC_PWR_VOTER) == 0)
			cb_data->defender_enabled = true;
		break;
	/* MAX77779: charging on via CC_MAX (needs inflow, buck_on on) */
	case GBMS_CHGR_MODE_CHGR_BUCK_ON:
		if (!cb_data->chgr_on)
			cb_data->reason = reason;
		pr_debug("%s: CHGR_BUCK_ON %s vote=0x%x\n", __func__,
			 reason ? reason : "<>", mode);
		cb_data->chgr_on += 1;
		break;
	/* USB: present, charging controlled via GBMS_CHGR_MODE_CHGR_BUCK_ON */
	case GBMS_USB_BUCK_ON:
		if (!cb_data->buck_on)
			cb_data->reason = reason;
		pr_debug("%s: BUCK_ON %s vote=0x%x\n", __func__,
			 reason ? reason : "<>", mode);
		cb_data->buck_on += 1;
		break;
	/* USB: OTG, source, fast role swap case */
	case GBMS_USB_OTG_FRS_ON:
		if (!cb_data->frs_on)
			cb_data->reason = reason;
		pr_debug("%s: FRS_ON vote=0x%x\n", __func__, mode);
		cb_data->frs_on += 1;
		break;
	/* USB: boost mode, source, normally external boost */
	case GBMS_USB_OTG_ON:
		if (!cb_data->otg_on)
			cb_data->reason = reason;
		pr_debug("%s: OTG_ON %s vote=0x%x\n", __func__,
			 reason ? reason : "<>", mode);
		cb_data->otg_on += 1;
		break;
	/* DC Charging: mode=0, set CP_EN */
	case GBMS_CHGR_MODE_CHGR_DC_USB:
		if (!cb_data->dc_on)
			cb_data->reason = reason;
		pr_debug("%s: DC_ON_USB vote=0x%x\n", __func__, mode);
		cb_data->dc_on = GBMS_CHGR_SEL_USB;
		break;
	case GBMS_CHGR_MODE_CHGR_DC_WLC:
		if (!cb_data->dc_on)
			cb_data->reason = reason;
		pr_debug("%s: DC_ON_WLC vote=0x%x\n", __func__, mode);
		cb_data->dc_on = GBMS_CHGR_SEL_WIRELESS;
		break;
	/* WLC Tx */
	case GBMS_CHGR_MODE_WLC_TX:
		if (!cb_data->wlc_tx)
			cb_data->reason = reason;
		pr_debug("%s: WLC_TX vote=%x\n", __func__, mode);
		cb_data->wlc_tx += 1;
		break;
	/* WLC_RX */
	case GBMS_CHGR_MODE_WLC_RX:
		if (!cb_data->wlc_rx)
			cb_data->reason = reason;
		pr_debug("%s: WLC_RX vote=%x\n", __func__, mode);
		cb_data->wlc_rx += 1;
		break;
	case GBMS_CHGR_MODE_WLC_RX_STDBY:
		if (!cb_data->wlc_rx)
			cb_data->reason = reason;
		pr_debug("%s: WLC_RX_STDBY vote=%x\n", __func__, mode);
		cb_data->wlc_rx_stby = true;
		cb_data->wlc_rx += 1;
		break;
	case GBMS_CHGR_MODE_FWUPDATE_BOOST_ON:
		pr_debug("%s: FWUPDATE vote=%x\n", __func__, mode);
		cb_data->fwupdate_on |= GSU_MODE_FWUPDATE_MASK;
		break;
	case GBMS_CHGR_MODE_WLC_FWUPDATE:
		pr_debug("%s: WLC FWUPDATE vote=%x\n", __func__, mode);
		cb_data->fwupdate_on |= GSU_MODE_WLC_FWUPDATE_MASK;
		break;
	case GBMS_POGO_VIN:
		if (!cb_data->pogo_vin)
			cb_data->reason = reason;
		pr_debug("%s: POGO VIN vote=%x\n", __func__, mode);
		cb_data->pogo_vin += 1;
		break;
	case GBMS_POGO_VOUT:
		if (!cb_data->pogo_vout)
			cb_data->reason = reason;
		pr_debug("%s: POGO VOUT vote=%x\n", __func__, mode);
		cb_data->pogo_vout += 1;
		break;
	case GBMS_CHG_SEL_WLC: /* Only one voter can vote this, does not affect vote reason */
		pr_debug("%s: CHG_SEL_WLC vote=%x\n", __func__, mode);
		temp = _bms_usecase_meta_chg_sel_get((long)vote);
		cb_data->chg_sel = _bms_usecase_chg_sel_index_set(0, temp) |
				   _bms_usecase_chg_sel_type_set(0, GBMS_CHGR_SEL_WIRELESS);
		break;
	case GBMS_CHG_SEL_USB: /* Only one voter can vote this, does not affect vote reason */
		pr_debug("%s: CHG_SEL_USB vote=%x\n", __func__, mode);
		temp = _bms_usecase_meta_chg_sel_get((long)vote);
		cb_data->chg_sel = _bms_usecase_chg_sel_index_set(0, temp) |
				   _bms_usecase_chg_sel_type_set(0, GBMS_CHGR_SEL_USB);
		break;
	case GBMS_CHG_OFF:
		pr_debug("%s: GBMS_CHG_OFF vote=%x\n", __func__, mode);
		cb_data->charge_off = true;
		break;
	default:
		/* Direct raw modes last come first served */
		pr_debug("%s: RAW vote=0x%x\n", __func__, mode);
		if (cb_data->use_raw)
			break;
		cb_data->raw_value = (long)mode;
		cb_data->reason = reason;
		cb_data->use_raw = true;
		break;
	}

	return 1;
}

/*
 * I am using a comparator_none, need scan all the votes to determine the actual.
 *
 * This function should only return errors before bms_usecase_get_new_node
 * otherwise, set entry->ret to the error and handle in bms_usecase_post_election_work
 */
static int bms_usecase_mode_callback(struct gvotable_election *el,
				     const char *trigger, void *value)
{
	struct bms_usecase_data *bms_uc_data = gvotable_get_data(el);
	struct device *dev = bms_uc_data->chg_data->dev;
	struct bms_usecase_foreach_cb_data *cb_data;
	int use_case, ret = 0;
	struct bms_usecase_entry *entry;

	__pm_stay_awake(bms_uc_data->usecase_wake_lock);
	mutex_lock(&bms_uc_data->mode_lock);

	entry = bms_usecase_get_new_node(bms_uc_data,
			gvotable_get_most_recent_reason(el),
			GVOTABLE_PTR_TO_INT(gvotable_get_most_recent_vote(el)));
	if (!entry) {
		dev_err(dev, "%s couldn't allocate entry\n", __func__);
		return -ENOMEM;
	}

	cb_data = entry->cb_data;
	cb_data->reason = trigger;

	/* now scan all the reasons, accumulate in cb_data */
	ret = gvotable_election_for_each(el, bms_usecase_foreach_callback, cb_data);
	if (!ret) {
		dev_dbg(dev, "%s: nope callback\n", __func__);
		entry->mode_cb_ret = BMS_USECASE_NO_HOPS;
		return entry->id;
	}

	dev_info(dev, "%s:%s raw=%d stby_on=%d, dc_on=%d, chgr_on=%d, buck_on=%d, otg_on=%d, wlc_tx=%d wlc_rx=%d usb_wlc=%d chgin_off=%d wlcin_off=%d frs_on=%d fwupdate=%d pogo_vout=%d, pogo_vin=%d chg_sel:%d chg_off:%d\n",
		__func__, trigger ? trigger : "<>",
		cb_data->use_raw, cb_data->stby_on, cb_data->dc_on,
		cb_data->chgr_on, cb_data->buck_on, cb_data->otg_on,
		cb_data->wlc_tx, cb_data->wlc_rx, cb_data->usb_wlc,
		cb_data->chgin_off, cb_data->wlcin_off, cb_data->frs_on, cb_data->fwupdate_on,
		cb_data->pogo_vout, cb_data->pogo_vin, cb_data->chg_sel, cb_data->charge_off);

	use_case = bms_uc_data->chg_data->cb_get_usecase(bms_uc_data->chg_data->uc_data,
							 cb_data);
	if (use_case < GSU_RAW_MODE) {
		entry->mode_cb_ret = use_case;
		return entry->id;
	}

	entry->usecase = use_case;

	return entry->id;
}

static void bms_usecase_mode_rerun_work(struct work_struct *work)
{
	struct bms_usecase_data *bms_uc_data = container_of(work, struct bms_usecase_data,
							    mode_rerun_work.work);

	gvotable_run_election(bms_uc_data->mode_votable, true);
}

/*
 * mode callback will return the entry_id that corresponds to the specific used entry
 */
static int bms_usecase_post_election_work(struct gvotable_election *el, void *d, int ret)
{
	struct bms_usecase_data *bms_uc_data = d;
	struct device *dev = bms_uc_data->chg_data->dev;
	struct bms_usecase_entry *entry;
	bool blocking;

	/* error occurred, just return out */
	if (ret < 0) {
		dev_err(dev, "Error in mode_callback ret:%d\n", ret);
		goto unlock;
	}

	entry = bms_usecase_get_entry(bms_uc_data, ret);
	if (!entry) {
		dev_err(dev, "Error retrieving entry %d\n", ret);
		ret = -EINVAL;
		goto unlock;
	}

	if (entry->mode_cb_ret < 0) {
		bms_usecase_free_node(bms_uc_data, entry, BMS_USECASE_FREE_ENTRY);
		ret = entry->mode_cb_ret == BMS_USECASE_NO_HOPS ? 0 : entry->mode_cb_ret;
		goto unlock;
	}

	blocking = !_bms_usecase_meta_async_get(entry->value);
	bms_usecase_add_tail(bms_uc_data, entry);

	mutex_unlock(&bms_uc_data->mode_lock);

	schedule_delayed_work(&bms_uc_data->usecase_work, 0);

	if (blocking) {
		ret = bms_usecase_down(entry);

		/*
		 * If wait is interrupted (ret < 0), check if the node is still
		 * in the queue.
		 * 1. If in queue: Set status=ORPHAN so worker frees it (FREE_ALL).
		 * 2. If not in queue: Worker is done, we must free it (FREE_ENTRY).
		 */
		if (ret < 0) {
			mutex_lock(&bms_uc_data->queue_lock);
			/* Check if list node is still attached */
			if (klist_node_attached(&entry->list_node)) {
				/* Use ORPHAN status to handle ownership transfer safely */
				entry->status = BMS_USECASE_STATUS_ORPHAN;
				mutex_unlock(&bms_uc_data->queue_lock);
				dev_warn(dev, "Blocking wait interrupted, orphaning node %d\n",
					 entry->id);

				/* Return error to caller as requested */
				ret = -EAGAIN;
				goto relax;
			}
			mutex_unlock(&bms_uc_data->queue_lock);
		} else {
			ret = entry->uc_work_ret;
		}
		bms_usecase_free_node(bms_uc_data, entry, BMS_USECASE_FREE_ENTRY);
	}

	goto relax;

unlock:
	mutex_unlock(&bms_uc_data->mode_lock);

relax:
	__pm_relax(bms_uc_data->usecase_wake_lock);
	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static const char *bms_usecase_state_to_str(enum bms_usecase_state state)
{
	switch (state) {
	case BMS_USECASE_UNINITIALIZED:
		return "Uninitialized";
	case BMS_USECASE_INITIAL:
		return "Initial";
	case BMS_USECASE_INTERMEDIATE:
		return BMS_USECASE_INTERMEDIATE_STR;
	default:
		return "Unknown";
	}
}

static void bms_usecase_process_work(struct work_struct *work)
{
	struct bms_usecase_data *bms_uc_data =
		container_of(work, struct bms_usecase_data, work.work);
	struct klist_iter iter;
	struct klist_node *node;
	struct device *dev = bms_uc_data->chg_data->dev;
	bool reschedule = false, err = false;
	int hops;

	mutex_lock(&bms_uc_data->usecase_work_lock);

	klist_iter_init(&bms_uc_data->queue, &iter);

	node = bms_usecase_queue_next(bms_uc_data, &iter);
	while (node) {
		struct bms_usecase_entry *entry =
			container_of(node, struct bms_usecase_entry, list_node);

		hops = bms_usecase_add_hops(bms_uc_data, entry);
		if (hops < 0) {
			dev_err(dev, "Error adding hops (%d)\n", hops);
			err = true;
			break;
		}
		if (hops) {
			reschedule = true;
			break;
		}

		dev_info(dev, "Usecase:%s(%d)->%s(%d) state:%s Node:%d\n",
			 bms_usecase_to_str(bms_uc_data->cur_usecase), bms_uc_data->cur_usecase,
			 bms_usecase_to_str(entry->usecase), entry->usecase,
			 bms_usecase_state_to_str(entry->state),
			 entry->id);

		/* set usecase */
		bms_uc_data->cur_usecase = entry->usecase;

		node = bms_usecase_queue_next(bms_uc_data, &iter);

		bms_usecase_free_node(bms_uc_data, entry,
				      !_bms_usecase_meta_async_get(entry->value));
	}

	klist_iter_exit(&iter);

	if (err)
		bms_usecase_clear_queue(bms_uc_data, err);

	if (reschedule) {
		dev_info(dev, "Rescheduling\n");

		schedule_delayed_work(&bms_uc_data->work, 0);
	}

	mutex_unlock(&bms_uc_data->usecase_work_lock);
}

static ssize_t debug_bms_usecase_get_queue(struct file *filp, char __user *buf,
					   size_t count, loff_t *ppos)
{
	struct bms_usecase_data *bms_uc_data = (struct bms_usecase_data *)filp->private_data;
	char *tmp;
	int len = 0;
	struct klist_iter iter;
	struct klist_node *node;

	tmp = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&bms_uc_data->queue_lock);

	if (list_empty(&bms_uc_data->queue.k_list)) {
		len += sysfs_emit_at(tmp, len, "list empty\n");
		goto free;
	}

	klist_iter_init(&bms_uc_data->queue, &iter);
	for (node = klist_next(&iter); node; node = klist_next(&iter)) {
		struct bms_usecase_entry *entry =
			container_of(node, struct bms_usecase_entry, list_node);

		len += sysfs_emit_at(tmp, len,
				     "node:%d usecase:%d state:%s status:%d reason:%s value:0x%x value_meta:0x%x\n",
				     entry->id, entry->usecase,
				     bms_usecase_state_to_str(entry->state),
				     entry->status,
				     entry->reason,
				     _bms_usecase_mode_get(entry->value),
				     _bms_usecase_meta_get(entry->value));
	}
	klist_iter_exit(&iter);

free:
	len = simple_read_from_buffer(buf, count, ppos, tmp, strlen(tmp));

	mutex_unlock(&bms_uc_data->queue_lock);

	kfree(tmp);

	return len;
}

static ssize_t debug_bms_usecase_add_queue(struct file *filp,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct bms_usecase_data *bms_uc_data = (struct bms_usecase_data *)filp->private_data;
	struct device *dev = bms_uc_data->chg_data->dev;
	struct bms_usecase_entry *entry;
	const int mem_size = count + 1;
	char *tmp, *cur, *saved_ptr;
	int ret, val, i;

	tmp = kzalloc(mem_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp, mem_size, ppos, user_buf, count);
	if (ret < 0) {
		dev_err(dev, "%s couldn't write to buffer ret:%d\n", __func__, ret);
		return ret;
	}

	mutex_lock(&bms_uc_data->queue_lock);
	for (saved_ptr = tmp, i = 0; i < BMS_USECASE_MAX_ENTRIES; i++) {
		cur = strsep(&saved_ptr, " ");
		if (!cur)
			break;

		ret = kstrtoint(cur, 0, &val);
		if (ret < 0) {
			break;
		}

		entry = bms_usecase_get_new_node(bms_uc_data, "DEBUGFS",
						 _bms_usecase_meta_async_set(val, 1));
		if (!entry) {
			dev_err(dev, "%s couldn't allocate entry\n", __func__);
			ret = -ENOMEM;
			break;
		}
		entry->usecase = val;
		bms_usecase_add_tail_locked(bms_uc_data, entry);
	}
	mutex_unlock(&bms_uc_data->queue_lock);

	kfree(tmp);
	return ret < 0 ? ret : count;
}

BATTERY_DEBUG_ATTRIBUTE(debug_bms_usecase_queue_fops, debug_bms_usecase_get_queue,
			debug_bms_usecase_add_queue);

static ssize_t debug_bms_usecase_get_pool(struct file *filp, char __user *buf,
					  size_t count, loff_t *ppos)
{
	struct bms_usecase_data *bms_uc_data = (struct bms_usecase_data *)filp->private_data;
	char *tmp;
	int i = 0, len = 0;

	tmp = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&bms_uc_data->pool_lock);

	for (i = 0; i < BMS_USECASE_MAX_ENTRIES; i++) {
		struct bms_usecase_entry *entry = &bms_uc_data->pool[i];

		len += sysfs_emit_at(tmp, len,
				     "%2d: node:%d usecase:%d state:%s status:%d reason:%s value:0x%x value_meta:0x%x\n",
				     i, entry->id, entry->usecase,
				     bms_usecase_state_to_str(entry->state),
				     entry->status,
				     entry->reason,
				     _bms_usecase_mode_get(entry->value),
				     _bms_usecase_meta_get(entry->value));
	}

	len = simple_read_from_buffer(buf, count, ppos, tmp, strlen(tmp));

	mutex_unlock(&bms_uc_data->pool_lock);

	kfree(tmp);

	return len;
}

BATTERY_DEBUG_ATTRIBUTE(debug_bms_usecase_pool_fops, debug_bms_usecase_get_pool, NULL);

static int debug_bms_usecase_process_queue(void *d, u64 val)
{
	struct bms_usecase_data *bms_uc_data = (struct bms_usecase_data *)d;

	if (val)
		schedule_delayed_work(&bms_uc_data->work, 0);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_bms_usecase_process_queue_fops,
			NULL,
			debug_bms_usecase_process_queue, "%llu\n");

static ssize_t debug_bms_usecase_get_cur_usecase(struct file *filp, char __user *buf,
						 size_t count, loff_t *ppos)
{
	char *tmp;
	int len = 0;

	tmp = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	sysfs_emit_at(tmp, len, "%s\n", bms_usecase_to_str(bms_usecase_get_usecase()));

	len = simple_read_from_buffer(buf, count, ppos, tmp, strlen(tmp));

	kfree(tmp);

	return len;
}

BATTERY_DEBUG_ATTRIBUTE(debug_bms_usecase_cur_usecase_fops, debug_bms_usecase_get_cur_usecase,
			NULL);

static int debug_bms_usecase_get_cur_charger(void *d, u64 *val)
{
	*val = bms_usecase_get_cur_chg_sel();
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_bms_usecase_cur_charger_fops,
			debug_bms_usecase_get_cur_charger,
			NULL, "%llu\n");

static void bms_usecase_debugfs_init(struct bms_usecase_data *bms_uc_data)
{
	bms_uc_data->de = debugfs_create_dir("google_bms_usecase", 0);
	if (!bms_uc_data->de)
		return;

	debugfs_create_file("process_queue", 0400, bms_uc_data->de, bms_uc_data,
			    &debug_bms_usecase_process_queue_fops);
	debugfs_create_file("queue", 0400, bms_uc_data->de, bms_uc_data,
			    &debug_bms_usecase_queue_fops);
	debugfs_create_file("pool", 0400, bms_uc_data->de, bms_uc_data,
			    &debug_bms_usecase_pool_fops);
	debugfs_create_file("cur_usecase", 0400, bms_uc_data->de, bms_uc_data,
			    &debug_bms_usecase_cur_usecase_fops);
	debugfs_create_file("cur_chg", 0400, bms_uc_data->de, bms_uc_data,
			    &debug_bms_usecase_cur_charger_fops);
}
#endif

int bms_usecase_register_notifiers(void *data, struct bms_usecase_notify_cbs cbs,
				   const char *identifier)
{
	struct bms_usecase_notify_data *notify_data;
	int ret = 0;

	if (!singleton_bms_uc_data)
		return -EAGAIN;

	mutex_lock(&singleton_bms_uc_data->usecase_work_lock);

	notify_data = kzalloc(sizeof(*notify_data), GFP_KERNEL);
	if (!notify_data) {
		ret = -ENOMEM;
		goto unlock;
	}

	notify_data->data = data;
	notify_data->cbs = cbs;
	strscpy(notify_data->identifier, identifier, GVOTABLE_MAX_REASON_LEN);

	list_add(&notify_data->list, &singleton_bms_uc_data->subscribers);

	cbs.uc_changed_cb(data, 0, bms_usecase_get_usecase());

unlock:
	mutex_unlock(&singleton_bms_uc_data->usecase_work_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bms_usecase_register_notifiers);

/*
 * from_uc_cb is an optional parameter used when order of completion matters
 * from_uc_cb will always run before to_uc_cb
 */
int bms_usecase_register_completion_cb(void *data, bms_usecase_completion_cb from_uc_cb,
				       bms_usecase_completion_cb to_uc_cb)
{
	struct bms_usecase_completion_data *completion_data;
	int ret = 0;
	struct bms_usecase_data *bms_uc_data = singleton_bms_uc_data;
	struct device *dev;

	if (!bms_uc_data)
		return -EAGAIN;

	dev = bms_uc_data->chg_data->dev;

	mutex_lock(&bms_uc_data->usecase_work_lock);

	completion_data = kzalloc(sizeof(*completion_data), GFP_KERNEL);
	if (!completion_data) {
		ret = -ENOMEM;
		goto unlock;
	}

	completion_data->data = data;
	completion_data->from_uc_cb = from_uc_cb;
	completion_data->to_uc_cb = to_uc_cb;

	list_add(&completion_data->list, &bms_uc_data->completion_list);

	/* all completion callbacks registered, init mode_callback */
	if (list_count_nodes(&bms_uc_data->completion_list) == bms_uc_data->chg_data->num_devices) {
		/* votes might change mode */
		bms_uc_data->mode_votable = gvotable_create_int_election(NULL, NULL,
									 bms_usecase_mode_callback,
									 bms_uc_data);
		if (!bms_uc_data->mode_votable) {
			dev_err(dev, "no mode votable\n");
			ret = -ENXIO;
			list_del(&completion_data->list);
			kfree(completion_data);
			goto unlock;
		}

		gvotable_set_vote2str(bms_uc_data->mode_votable, gvotable_v2s_uint);
		gvotable_register_post_election_work(bms_uc_data->mode_votable, bms_uc_data,
			bms_usecase_post_election_work);
		/* will use gvotable_get_default() when available */
		gvotable_set_default(bms_uc_data->mode_votable, (void *)GSU_MODE_STANDBY);
		gvotable_set_restrict_cb_invocation(bms_uc_data->mode_votable, true);
		gvotable_election_set_name(bms_uc_data->mode_votable, GBMS_MODE_VOTABLE);
	}

unlock:
	mutex_unlock(&bms_uc_data->usecase_work_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(bms_usecase_register_completion_cb);

/* requires bms_usecase_work_lock */
static void bms_usecase_unregister_notifiers(struct bms_usecase_data *bms_uc_data)
{
	struct bms_usecase_notify_data *sub, *tmp;

	list_for_each_entry_safe(sub, tmp, &bms_uc_data->subscribers, list) {
		list_del(&sub->list);
		kfree(sub);
	}
}

/* requires bms_usecase_work_lock */
static void bms_usecase_unregister_completion_cbs(struct bms_usecase_data *bms_uc_data)
{
	struct bms_usecase_completion_data *sub, *tmp;

	list_for_each_entry_safe(sub, tmp, &bms_uc_data->completion_list, list) {
		list_del(&sub->list);
		kfree(sub);
	}
}

/* requires bms_usecase_work_lock */
static void bms_usecase_uc_changed_notify(struct bms_usecase_data *bms_uc_data,
					  enum gsu_usecases from_uc, enum gsu_usecases to_uc)
{
	struct bms_usecase_notify_data *sub;
	struct device *dev = bms_uc_data->chg_data->dev;

	list_for_each_entry(sub, &bms_uc_data->subscribers, list) {
		if (sub->cbs.uc_changed_cb) {
			dev_dbg(dev, "%s: ====== %s START =======\n", __func__, sub->identifier);
			sub->cbs.uc_changed_cb(sub->data, from_uc, to_uc);
			dev_dbg(dev, "%s: ====== %s END =======\n", __func__, sub->identifier);
		}
	}
}

/* requires bms_usecase_work_lock */
static void bms_usecase_uc_setup_notify(struct bms_usecase_data *bms_uc_data,
					enum gsu_usecases from_uc, enum gsu_usecases to_uc)
{
	struct bms_usecase_notify_data *sub;
	struct device *dev = bms_uc_data->chg_data->dev;

	list_for_each_entry(sub, &bms_uc_data->subscribers, list) {
		if (sub->cbs.uc_setup_cb) {
			dev_dbg(dev, "%s: ====== %s START =======\n", __func__, sub->identifier);
			sub->cbs.uc_setup_cb(sub->data, from_uc, to_uc);
			dev_dbg(dev, "%s: ====== %s END =======\n", __func__, sub->identifier);
		}
	}
}

/* requires bms_usecase_work_lock */
static void bms_usecase_uc_transition_notify(struct bms_usecase_data *bms_uc_data,
					     enum gsu_usecases from_uc, enum gsu_usecases to_uc)
{
	struct bms_usecase_notify_data *sub;
	struct device *dev = bms_uc_data->chg_data->dev;

	list_for_each_entry(sub, &bms_uc_data->subscribers, list) {
		if (sub->cbs.uc_transition_cb) {
			dev_dbg(dev, "%s: ====== %s START =======\n", __func__, sub->identifier);
			sub->cbs.uc_transition_cb(sub->data, from_uc, to_uc);
			dev_dbg(dev, "%s: ====== %s END =======\n", __func__, sub->identifier);
		}
	}
}

/* requires bms_usecase_work_lock */
static int bms_usecase_complete_usecase(struct bms_usecase_data *bms_uc_data,
					struct bms_usecase_entry *entry,
					enum gsu_usecases from_uc, enum gsu_usecases to_uc)
{
	struct bms_usecase_completion_data *sub;

	/* From usecase */
	list_for_each_entry(sub, &bms_uc_data->completion_list, list) {
		if (sub->from_uc_cb) {
			entry->complete_uc_ret = sub->from_uc_cb(sub->data, entry);
			if (entry->complete_uc_ret)
				return entry->complete_uc_ret;
		}
	}

	if (entry->trigger_cb)
		bms_usecase_uc_transition_notify(bms_uc_data, from_uc, to_uc);

	/* To usecase */
	list_for_each_entry(sub, &bms_uc_data->completion_list, list) {
		entry->complete_uc_ret = sub->to_uc_cb(sub->data, entry);
		if (entry->complete_uc_ret)
			return entry->complete_uc_ret;
	}

	return 0;
}

static void bms_usecase_work(struct work_struct *work)
{
	struct bms_usecase_data *bms_uc_data = container_of(work, struct bms_usecase_data,
							    usecase_work.work);
	struct device *dev = bms_uc_data->chg_data->dev;
	struct bms_usecase_foreach_cb_data *cb_data = &bms_uc_data->next_cb_data;
	struct klist_iter iter;
	struct klist_node *node;
	bool reschedule = false, to_standby = false;
	int hops, ret, from_uc, to_uc;
	int clear_queue = 0;
	bool buck_on = false;

	__pm_stay_awake(bms_uc_data->usecase_wake_lock);

	mutex_lock(&bms_uc_data->usecase_work_lock);

	klist_iter_init(&bms_uc_data->queue, &iter);

	node = bms_usecase_queue_next(bms_uc_data, &iter);
	while (node) {
		bool trigger_cb;
		struct bms_usecase_entry *entry =
			container_of(node, struct bms_usecase_entry, list_node);

		if (entry->complete_uc_ret) {
			clear_queue = entry->complete_uc_ret;
			break;
		}

		hops = bms_usecase_add_hops(bms_uc_data, entry);
		if (hops < 0) {
			dev_err(dev, "Error adding hops (%d)\n", hops);
			clear_queue = hops;
			break;
		}
		if (hops) {
			reschedule = true;
			break;
		}

		from_uc = bms_usecase_get_usecase();
		to_uc = entry->usecase;
		trigger_cb = (from_uc != to_uc) || (cb_data->chg_sel != entry->cb_data->chg_sel);
		entry->trigger_cb = trigger_cb;
		memcpy(cb_data, entry->cb_data, sizeof(*cb_data));
		bms_uc_data->next_usecase = entry->usecase;
		bms_uc_data->next_uc_charge_off = cb_data->charge_off;

		if (entry->trigger_cb)
			bms_usecase_uc_setup_notify(bms_uc_data, from_uc, to_uc);

		ret = bms_usecase_complete_usecase(bms_uc_data, entry, from_uc, to_uc);
		if (ret < 0) {
			dev_err(dev, "Error setting usecase (%d)\n", ret);

			reschedule = true;
			to_standby = (ret != -EAGAIN);
			buck_on = cb_data->buck_on;
			break;
		}

		/* the election is an int election */
		if (!cb_data->reason)
			cb_data->reason = "<>";

		/* this changes the trigger */
		ret = gvotable_election_set_result(bms_uc_data->mode_votable, cb_data->reason,
						   (void *)(uintptr_t)entry->usecase);
		if (ret < 0)
			dev_err(dev, "cannot update election %d\n", ret);

		/* set usecase */
		bms_uc_data->cur_usecase = entry->usecase;

		node = bms_usecase_queue_next(bms_uc_data, &iter);
		/* usecase entry data is invalid after this point */

		/*
		 * Check for ownership transfer via the orphan status.
		 * Must hold queue_lock to ensure atomic check-and-free relative to
		 * the caller setting the flag.
		 */
		mutex_lock(&bms_uc_data->queue_lock);
		if (!_bms_usecase_meta_async_get(entry->value) &&
		    entry->status == BMS_USECASE_STATUS_ORPHAN) {
			/*
			 * Explicitly reset the completion state for orphaned nodes.
			 * The submitter was interrupted and left an unconsumed signal,
			 * which must be cleared before the node returns to the pool.
			 */
			bms_usecase_reset_completion(entry);
			bms_usecase_free_node_locked(bms_uc_data, entry, BMS_USECASE_FREE_ALL);
		} else {
			bms_usecase_free_node_locked(bms_uc_data, entry,
						!_bms_usecase_meta_async_get(entry->value));
		}
		mutex_unlock(&bms_uc_data->queue_lock);

		if (trigger_cb)
			bms_usecase_uc_changed_notify(bms_uc_data, from_uc, to_uc);
	}

	klist_iter_exit(&iter);

	if (to_standby)
		clear_queue = bms_usecase_append_standby_to_queue(bms_uc_data, buck_on);

	if (!!clear_queue) {
		bms_usecase_clear_queue(bms_uc_data, clear_queue);
		schedule_delayed_work(&bms_uc_data->mode_rerun_work, msecs_to_jiffies(50));
	}

	if (reschedule) {
		dev_info(dev, "Rescheduling\n");
		schedule_delayed_work(&bms_uc_data->usecase_work, 0);
	}

	mutex_unlock(&bms_uc_data->usecase_work_lock);

	__pm_relax(bms_uc_data->usecase_wake_lock);
}

void bms_usecase_remove(void)
{
	struct bms_usecase_data *bms_uc_data = singleton_bms_uc_data;

	bms_usecase_unregister_notifiers(bms_uc_data);
	bms_usecase_unregister_completion_cbs(bms_uc_data);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	debugfs_remove(bms_uc_data->de);
#endif /* CONFIG_DEBUG_FS */

	mutex_lock(&bms_uc_data->pool_lock);
	singleton_bms_uc_data = NULL;
	mutex_unlock(&bms_uc_data->pool_lock);

	gvotable_destroy_election(bms_uc_data->mode_votable);
	cancel_delayed_work(&bms_uc_data->usecase_work);
	bms_usecase_clear_queue(bms_uc_data, 0);

	wakeup_source_unregister(bms_uc_data->usecase_wake_lock);

	mutex_destroy(&bms_uc_data->pool_lock);
	mutex_destroy(&bms_uc_data->queue_lock);
	mutex_destroy(&bms_uc_data->usecase_work_lock);
	mutex_destroy(&bms_uc_data->mode_lock);
}
EXPORT_SYMBOL_GPL(bms_usecase_remove);

int bms_usecase_init(struct bms_usecase_chg_data *chg_data)
{
	int i;
	struct device *dev = chg_data->dev;
	struct bms_usecase_data *bms_uc_data;

	bms_uc_data = devm_kzalloc(dev, sizeof(*bms_uc_data), GFP_KERNEL);
	if (!bms_uc_data) {
		dev_err(dev, "Error allocating bms_usecase_chg_data!!!\n");
		return -ENOMEM;
	}

	bms_uc_data->chg_data = chg_data;

	bms_uc_data->usecase_wake_lock = wakeup_source_register(NULL, "bms-usecase");
	if (!bms_uc_data->usecase_wake_lock) {
		dev_err(dev, "Failed to register wakeup source\n");
		return -ENODEV;
	}

	INIT_LIST_HEAD(&bms_uc_data->subscribers);
	INIT_LIST_HEAD(&bms_uc_data->completion_list);

	klist_init(&bms_uc_data->queue, NULL, NULL);
	bms_usecase_init_uc_hash_table();

	for (i = 0; i < BMS_USECASE_MAX_ENTRIES; i++) {
		struct bms_usecase_entry *entry = &bms_uc_data->pool[i];

		entry->id = i;
		entry->dev = dev;
		entry->status = BMS_USECASE_STATUS_UNINITIALIZED;
		init_completion(&entry->completion);
		atomic_set(&entry->completion_count, 0);

		entry->reason = devm_kzalloc(dev, GVOTABLE_MAX_REASON_LEN, GFP_KERNEL);
		if (!entry->reason)
			return -ENOMEM;
		entry->cb_data = devm_kzalloc(dev,
					      sizeof(struct bms_usecase_foreach_cb_data),
					      GFP_KERNEL);
		if (!entry->cb_data)
			return -ENOMEM;
	}

	mutex_init(&bms_uc_data->usecase_work_lock);
	mutex_init(&bms_uc_data->queue_lock);
	mutex_init(&bms_uc_data->pool_lock);
	mutex_init(&bms_uc_data->mode_lock);

	INIT_DELAYED_WORK(&bms_uc_data->usecase_work, bms_usecase_work);
	INIT_DELAYED_WORK(&bms_uc_data->mode_rerun_work, bms_usecase_mode_rerun_work);

#if defined(CONFIG_DEBUG_FS)
	INIT_DELAYED_WORK(&bms_uc_data->work, bms_usecase_process_work);

	bms_usecase_debugfs_init(bms_uc_data);
#endif

	singleton_bms_uc_data = bms_uc_data;

	dev_info(dev, "%s complete\n", __func__);

	return 0;
}
EXPORT_SYMBOL_GPL(bms_usecase_init);
