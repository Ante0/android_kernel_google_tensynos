/* SPDX-License-Identifier: GPL */
/*
 * Google Touch Interface - Power Management
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _GTI_PM_H_
#define _GTI_PM_H_

/*-----------------------------------------------------------------------------
 * enums.
 */

#ifndef __CODESONAR__
#define ENUM_U32 : u32
#else
#define ENUM_U32
#endif

enum gti_pm_state ENUM_U32 {
	GTI_PM_SUSPEND = 0,
	GTI_PM_RESUME,
};

#define GTI_PM_WAKELOCK_TYPE_LOCK_MASK 0xFFFF
/**
 * @brief: wakelock type.
 */
enum gti_pm_wakelock_type ENUM_U32 {
	GTI_PM_WAKELOCK_TYPE_SCREEN_ON = (1 << 0),
	GTI_PM_WAKELOCK_TYPE_IRQ = (1 << 1),
	GTI_PM_WAKELOCK_TYPE_FW_UPDATE = (1 << 2),
	GTI_PM_WAKELOCK_TYPE_SYSFS = (1 << 3),
	GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE = (1 << 4),
	GTI_PM_WAKELOCK_TYPE_BUGREPORT = (1 << 5),
	GTI_PM_WAKELOCK_TYPE_OFFLOAD_REPORT = (1 << 6),
	GTI_PM_WAKELOCK_TYPE_SENSOR_DATA = (1 << 7),
	GTI_PM_WAKELOCK_TYPE_FW_SETTINGS = (1 << 8),
	GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST = (1 << 9),
	GTI_PM_WAKELOCK_TYPE_RESUME_RUNNING = (1 << 10),
};

#undef ENUM_U32

/*-----------------------------------------------------------------------------
 * Structures.
 */

struct gti_pm_ops {
	int (*resume)(void *private_data);
	int (*suspend)(void *private_data);
};

/**
 * struct gti_pm - power manager for GTI.
 * @state_update_work: a work to update pm state.
 * @event_wq: a work queue to run suspend/resume work.
 * @locks: the lock state.
 * @lock_mutex: protect the lock state.
 * @state: GTI pm state.
 * @new_state: New GTI pm state to be updated to.
 * @enabled: Boolean value to represent if GTI PM is active.
 * @update_state: Boolean value if state needs to be updated.
 * @resume: callback for notifying resume.
 * @suspend: callback for notifying suspend.
 */
struct gti_pm {
	struct work_struct state_update_work;
	struct workqueue_struct *event_wq;
	struct device *dev;

	u32 locks;
	struct mutex lock_mutex;
	enum gti_pm_state state;
	enum gti_pm_state new_state;
	bool enabled;
	bool update_state;

	void *private_data;
	struct gti_pm_ops *ops;
};

/*-----------------------------------------------------------------------------
 * Forward declarations.
 */

int gti_pm_wake_lock_nosync_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type,
				     bool skip_pm_resume);
int gti_pm_wake_lock_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type,
			      bool skip_pm_resume);
int gti_pm_wake_unlock_nosync_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type);
int gti_pm_wake_unlock_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type);
bool gti_pm_wake_check_locked_internal(struct gti_pm *pm, enum gti_pm_wakelock_type type);
u32 gti_pm_wake_get_locks_internal(struct gti_pm *pm);
int gti_pm_state_update(struct gti_pm *pm);

int gti_pm_remove(struct gti_pm *pm);
int gti_pm_probe(struct gti_pm *pm, struct device *dev, struct gti_pm_ops *ops, void *private_data);

#endif // _GTI_PM_H_
