#include "harness_doorbell.h"

#include <linux/list.h>
#include <linux/pm_runtime.h>

#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_doorbell.h>

#define WLAN_DOORBELL_NAME "nep_doorbell_0"
#define MODEM_DOORBELL_NAME "nep_doorbell_1"

static const char *harness_doorbell_name[NOA_HARNESS_DOORBELL_MAX] = {
	[NOA_HARNESS_DOORBELL_WLAN] = WLAN_DOORBELL_NAME,
	[NOA_HARNESS_DOORBELL_MODEM] = MODEM_DOORBELL_NAME,
};

static void noa_harness_doorbell_isr(void *data)
{
	struct noa_harness_doorbell *doorbell = data;
	struct noa_harness_isr_task *task;

	list_for_each_entry (task, &doorbell->isr_task_list, list) {
		queue_work(task->vdev->tm->wq, &task->vdev->work);
	}
}

int noa_harness_doorbell_init(struct noa_harness *tm)
{
	int i;
	struct device *dpa_dev = google_dpa_get_dpa_dev(tm->dpa);

	for (i = 0; i < NOA_HARNESS_DOORBELL_MAX; i++) {
		tm->doorbells[i].hw_doorbell =
			google_dpa_get_doorbell(dpa_dev, harness_doorbell_name[i]);
		if (!tm->doorbells[i].hw_doorbell) {
			dev_err(tm->dev, "Failed to get %s doorbell for %d\n",
				harness_doorbell_name[i], i);
			return -EINVAL;
		}
		tm->doorbells[i].hw_doorbell_dev =
			google_dpa_get_doorbell_dev(tm->doorbells[i].hw_doorbell);
		INIT_LIST_HEAD(&tm->doorbells[i].isr_task_list);
		pm_runtime_get_sync(tm->doorbells[i].hw_doorbell_dev);
	}
	return 0;
}

int noa_harness_doorbell_enable(struct noa_harness *tm)
{
	int ret;
	int i;
	for (i = 0; i < NOA_HARNESS_DOORBELL_MAX; i++) {
		if (!tm->doorbells[i].hw_doorbell)
			continue;
		google_dpa_doorbell_disable_doorbell(tm->doorbells[i].hw_doorbell, 0);
		ret = google_dpa_doorbell_enable_doorbell(tm->doorbells[i].hw_doorbell, 0,
							  noa_harness_doorbell_isr,
							  &tm->doorbells[i]);
		if (ret) {
			dev_err(tm->dev, "Failed to enable doorbell id: %d\n", i);
			return ret;
		}
	}
	return 0;
}

void noa_harness_doorbell_disable(struct noa_harness *tm)
{
	int i;

	for (i = 0; i < NOA_HARNESS_DOORBELL_MAX; i++) {
		if (!tm->doorbells[i].hw_doorbell)
			continue;
		google_dpa_doorbell_disable_doorbell(tm->doorbells[i].hw_doorbell, 0);
		pm_runtime_put(tm->doorbells[i].hw_doorbell_dev);
		tm->doorbells[i].hw_doorbell = NULL;
		tm->doorbells[i].hw_doorbell_dev = NULL;
	}
}

void *noa_harness_doorbell_get(struct noa_harness *tm, enum noa_harness_doorbell_id id)
{
	if (id >= NOA_HARNESS_DOORBELL_MAX)
		return NULL;
	return tm->doorbells[id].hw_doorbell;
}

void noa_harness_trigger_doorbell(void *doorbell)
{
	google_dpa_doorbell_ring_mcu_atomic_safe((struct google_dpa_doorbell *)doorbell, 0);
}

int noa_harness_doorbell_register_isr_task(struct noa_harness_doorbell *doorbell,
					   struct noa_harness_isr_task *task)
{
	if (!doorbell || !task)
		return -EINVAL;

	list_add_tail(&task->list, &doorbell->isr_task_list);
	return 0;
}

void noa_harness_doorbell_unregister_isr_task(struct noa_harness_isr_task *task)
{
	if (!task)
		return;

	list_del_init(&task->list);
}
