// SPDX-License-Identifier: GPL-2.0

#define NS_IN_MS (1000000)

#include <linux/kernel.h>
#include <linux/pm_opp.h>
#include <linux/sysfs.h>

#include "pvrsrv_device.h"
#include "rgxdevice.h"

#include "genpd.h"
#include "uid_time_in_state.h"

struct tis_entry {
	struct list_head list;
	IMG_UINT32 uid;
	IMG_UINT64 tis_opp_ns[];
};

static void uid_time_in_state_add_period_locked(struct pixel_gpu_device *pixel_dev,
					 IMG_UINT32 uid,
					 int opp_index,
					 IMG_UINT64 time_ns)
{
	struct uid_tis_data *tis = &pixel_dev->time_in_state;
	struct tis_entry *entry = NULL;
	bool found = false;

	if (!list_empty(&tis->head)) {
		list_for_each_entry(entry, &tis->head, list) {
			if (entry->uid == uid) {
				found = true;
				break;
			}
		}
	}
	if (!found) {
		entry = kzalloc(struct_size(entry, tis_opp_ns, tis->num_opp_frequencies),
				GFP_KERNEL);
		if (entry == NULL) {
			dev_err(pixel_dev->dev,
					"%s: Could not allocate time-in-state UID entry",
					__func__);
			return;
		}
		entry->uid = uid;
		list_add(&entry->list, &tis->head);
	}
	entry->tis_opp_ns[opp_index] += time_ns;
}

void work_period_callback(IMG_HANDLE hSysData,
			  IMG_UINT32 uid,
			  IMG_UINT32 frequency,
			  IMG_UINT64 time_ns)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;
	struct uid_tis_data *tis = &pixel_dev->time_in_state;
	int opp_index = 0;

	for (opp_index = 0; opp_index < tis->num_opp_frequencies; opp_index++) {
		if (tis->opp_frequencies[opp_index] == frequency)
			break;
	}
	if (opp_index >= tis->num_opp_frequencies) {
		dev_warn_once(pixel_dev->dev, "%s: frequency %u not found", __func__, frequency);
		return;
	}

	mutex_lock(&tis->lock);
	uid_time_in_state_add_period_locked(pixel_dev, uid, opp_index, time_ns);
	mutex_unlock(&tis->lock);
}

static ssize_t uid_time_in_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	struct uid_tis_data *tis = &pixel_dev->time_in_state;
	struct tis_entry *entry = NULL;
	int at = 0;

	at += sysfs_emit_at(buf, at, "uid:");
	for (int i = 0; i < tis->num_opp_frequencies; i++)
		at += sysfs_emit_at(buf, at, " %lu", tis->opp_frequencies[i]);
	at += sysfs_emit_at(buf, at, "\n");

	mutex_lock(&tis->lock);
	if (!list_empty(&tis->head)) {
		list_for_each_entry(entry, &tis->head, list) {
			at += sysfs_emit_at(buf, at, "%u:", entry->uid);
			for (int i = 0;
			     i < tis->num_opp_frequencies;
			     i++) {
				at += sysfs_emit_at(buf,
						    at,
						    " %llu",
						    entry->tis_opp_ns[i] / NS_IN_MS);
			}
			at += sysfs_emit_at(buf, at, "\n");
		}
	}
	mutex_unlock(&tis->lock);
	return at;
}

static DEVICE_ATTR_RO(uid_time_in_state);

static int get_opp_freq_with_table(struct device *dev,
				   unsigned int *freq_count,
				   unsigned long **frequencies)
{
	struct dev_pm_opp *opp;
	unsigned long freq = 0;
	int num_opp = 0;
	int i = 0;

	num_opp = dev_pm_opp_get_opp_count(dev);
	if (!num_opp) {
		dev_err(dev, "%s: Could not obtain the number of GPU opps", __func__);
		return PVRSRV_ERROR_INIT_FAILURE;
	}
	*freq_count = num_opp;

	*frequencies = kcalloc(num_opp,
			      sizeof(unsigned long),
			      GFP_KERNEL);
	if (*frequencies == NULL) {
		dev_err(dev,
			"%s: Could not allocate space for time-in-state UID frequencies",
			__func__);
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	while (i < num_opp && !IS_ERR(opp = dev_pm_opp_find_freq_ceil(dev, &freq))) {
		(*frequencies)[i++] = freq++;
		dev_pm_opp_put(opp);
	}

	return 0;
}

static int get_opp_frequencies(struct device *dev,
			       unsigned int *freq_count,
			       unsigned long **frequencies)
{
	bool load_opps = (dev_pm_opp_get_opp_count(dev) <= 0);
	int result;

	if (load_opps) {
		result = dev_pm_opp_of_add_table(dev);
		if (result) {
			dev_err(dev, "%s: Failed to add opp table: %d", __func__, result);
			return result;
		}
	}

	result = get_opp_freq_with_table(dev, freq_count, frequencies);

	if (load_opps) {
		dev_pm_opp_of_remove_table(dev);
	}

	return result;
}

int init_pixel_uid_tis(struct pixel_gpu_device *pixel_dev)
{
	struct uid_tis_data *tis = &pixel_dev->time_in_state;

	tis->num_opp_frequencies = 0;
	if (device_create_file(pixel_dev->dev, &dev_attr_uid_time_in_state)) {
		dev_err(pixel_dev->dev, "%s: Could not create uid_time_in_state", __func__);
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	if (get_opp_frequencies(pixel_dev->dev,
				&tis->num_opp_frequencies,
				&tis->opp_frequencies)) {
		dev_err(pixel_dev->dev, "%s: Could not obtain GPU opp frequencies", __func__);
		goto fail_init;
	}
	if (tis->num_opp_frequencies == 0) {
		dev_err(pixel_dev->dev, "%s: Found no GPU opps", __func__);
		goto fail_init;
	}

	INIT_LIST_HEAD(&tis->head);
	mutex_init(&tis->lock);
	return PVRSRV_OK;
fail_init:
	device_remove_file(pixel_dev->dev, &dev_attr_uid_time_in_state);
	return PVRSRV_ERROR_INIT_FAILURE;
}

void deinit_pixel_uid_tis(struct pixel_gpu_device *pixel_dev)
{
	struct uid_tis_data *tis = &pixel_dev->time_in_state;
	struct tis_entry *entry = NULL;
	struct tis_entry *temp_entry = NULL;

	mutex_lock(&tis->lock);
	if (!list_empty(&tis->head)) {
		list_for_each_entry_safe(entry, temp_entry, &tis->head, list) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
	kfree(tis->opp_frequencies);
	mutex_unlock(&tis->lock);
	mutex_destroy(&tis->lock);
}
