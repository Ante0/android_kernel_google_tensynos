// SPDX-License-Identifier: MIT

#include <gs_drm/gs_fault_event.h>

void gs_fault_event_emit(struct device *dev, enum gs_fault_event_type type,
					   u64 value)
{
	char type_str[32];
	char val_str[32];
	char **envp;

	snprintf(type_str, sizeof(type_str), "TYPE=%u", type);
	snprintf(val_str, sizeof(val_str), "VAL=%llu", value);
	envp = (char *[]){"EVENT=display_fault", type_str, val_str, NULL};

	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL_GPL(gs_fault_event_emit);

static void gs_fault_event_emit_work_func(struct work_struct *work)
{
	struct gs_fault_work_data *data = container_of(work, struct gs_fault_work_data, work);

	gs_fault_event_emit(data->dev, data->type, data->value);

	kfree(data);
}

void non_blocking_gs_fault_event_emit(struct device *dev,
			enum gs_fault_event_type type, u64 value)
{
	struct gs_fault_work_data *data;

	data = kmalloc(sizeof(*data), GFP_ATOMIC);
	if (!data)
		return;

	INIT_WORK(&data->work, gs_fault_event_emit_work_func);
	data->dev = dev;
	data->type = type;
	data->value = value;

	schedule_work(&data->work);
}
EXPORT_SYMBOL_GPL(non_blocking_gs_fault_event_emit);
