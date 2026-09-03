// SPDX-License-Identifier: GPL-2.0

#include "genpd.h"

#include <pvrsrvkm/services/server/env/linux/power_trace_events.h>
#include <pvrsrvkm/services/server/devices/rgxpower.h>
#include <pvrsrvkm/services/server/devices/volcanic/rgxdevice.h>

#include <misc/sbbm.h>

#include <linux/notifier.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sysfs.h>
#include <trace/hooks/systrace.h>

#if defined(CONFIG_POWERVR_DISABLE_PM_RUNTIME)
#define disable_pm_runtime (true)
#else
#define disable_pm_runtime (false)
#endif

static int init_genpd_sysfs(struct pixel_gpu_device *pixel_dev);
static void deinit_genpd_sysfs(struct pixel_gpu_device *pixel_dev);

#if defined(PVR_DPF_CUSTOMER_DEBUG_ON)
static const char *action_to_string(unsigned long action)
{
	switch (action) {
	case GENPD_NOTIFY_PRE_OFF:
		return "GENPD_NOTIFY_PRE_OFF";
	case GENPD_NOTIFY_OFF:
		return "GENPD_NOTIFY_OFF";
	case GENPD_NOTIFY_PRE_ON:
		return "GENPD_NOTIFY_PRE_ON";
	case GENPD_NOTIFY_ON:
		return "GENPD_NOTIFY_ON";
	default:
		return "UNKNOWN";
	}
}
#endif

static const char *power_state_str(int state)
{
	switch (state) {
	case PIXEL_GPU_POWER_STATE_OFF: return "off";
	case PIXEL_GPU_POWER_STATE_PG:	return "pg";
	case PIXEL_GPU_POWER_STATE_ON:	return "on";
	default:			return "unknown";
	}
}

static void update_power_state(struct pixel_gpu_device *pixel_dev, int new_state)
{
	int cur_state;
	struct pixel_gpu_power_state_stats *cur_stats;
	struct pixel_gpu_power_state_stats *new_stats;
	ktime_t now = ktime_get();

	mutex_lock(&pixel_dev->power_state.lock);
	cur_state = pixel_dev->power_state.cur_state;
	cur_stats = cur_state < PIXEL_GPU_POWER_STATE_COUNT
			    ? &pixel_dev->power_state.stats[cur_state]
			    : NULL;
	new_stats = &pixel_dev->power_state.stats[new_state];

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: %s -> %s", __func__,
		power_state_str(cur_state), power_state_str(new_state)));

#if defined(PVRSRV_ANDROID_TRACE_GPU_POWER_STATE)
	trace_gpu_power_state(cur_state,
			      new_state);
#endif

	if (cur_state == new_state) {
		dev_warn(pixel_dev->dev,
			 "unexpectedly transitioning to current power state (state: %d)",
			 new_state);
		mutex_unlock(&pixel_dev->power_state.lock);
		return;
	}
	if ((cur_state == PIXEL_GPU_POWER_STATE_OFF && new_state == PIXEL_GPU_POWER_STATE_ON) ||
	    (cur_state == PIXEL_GPU_POWER_STATE_ON && new_state == PIXEL_GPU_POWER_STATE_OFF)) {
		dev_warn(pixel_dev->dev,
			 "unexpectedly transitioning directly between ON and OFF (cur=%d new=%d)",
			 cur_state, new_state);
	}

	if (cur_stats) {
		ktime_t dt = ktime_sub(now, cur_stats->last_entry_ns);
		cur_stats->cumulative_time_ns = ktime_add(cur_stats->cumulative_time_ns, dt);
		cur_stats->last_exit_ns = now;
	}
	new_stats->last_entry_ns = now;
	new_stats->entry_count += 1;
	pixel_dev->power_state.cur_state = new_state;

	mutex_unlock(&pixel_dev->power_state.lock);
}

/**
 * genpd_notify_gpu() - Sets power state based on core logic pd
 * @core_logic_notifier: unused linux notification block
 * @action:	the new genpd state
 * @data:	unused private data
 * This function updates the system layer view of
 * the current core logic power state. It is called whenever
 * genpd requests a new power state for the core logic pd
 */
static int genpd_notify_gpu(struct notifier_block *core_logic_notifier,
			    unsigned long action, void *data)
{
	struct pixel_gpu_device *pixel_dev =
		container_of(core_logic_notifier, struct pixel_gpu_device,
			     core_logic_notifier);

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: GPU Core Logic %s", __func__,
		action_to_string(action)));

	switch (action) {
	case GENPD_NOTIFY_PRE_ON:
		ATRACE_BEGIN("GPU core-logic power-on");
		break;
	case GENPD_NOTIFY_ON:
		update_power_state(pixel_dev, PIXEL_GPU_POWER_STATE_ON);
		ATRACE_END();
		break;
	case GENPD_NOTIFY_PRE_OFF:
		ATRACE_BEGIN("GPU core-logic power-off");
		break;
	case GENPD_NOTIFY_OFF:
		update_power_state(pixel_dev, PIXEL_GPU_POWER_STATE_PG);
		ATRACE_END();
		break;
	default:
		break;
	};

	pixel_gpu_debug_update_genpd_state(
		pixel_dev, PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD, action);

	return NOTIFY_DONE;
}

static int genpd_notify_sswrp(struct notifier_block *sswrp_notifier,
			      unsigned long action, void *data)
{
	struct pixel_gpu_device *pixel_dev = container_of(
		sswrp_notifier, struct pixel_gpu_device, sswrp_notifier);

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: GPU SSWRP %s", __func__,
		action_to_string(action)));

	switch (action) {
	case GENPD_NOTIFY_PRE_ON:
		ATRACE_BEGIN("GPU SSWRP power-on");
		break;
	case GENPD_NOTIFY_ON:
		update_power_state(pixel_dev, PIXEL_GPU_POWER_STATE_PG);
		ATRACE_END();
		break;
	case GENPD_NOTIFY_PRE_OFF:
		ATRACE_BEGIN("GPU SSWRP power-off");
		break;
	case GENPD_NOTIFY_OFF:
		update_power_state(pixel_dev, PIXEL_GPU_POWER_STATE_OFF);
		ATRACE_END();
		break;
	default:
		break;
	}

	pixel_gpu_debug_update_genpd_state(
		pixel_dev, PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD, action);

	return NOTIFY_DONE;
}

static struct device *attach_pm_domain(struct device *dev,
				       enum pixel_gpu_pm_domain pm_domain)
{
	struct device *pm_domain_dev =
		dev_pm_domain_attach_by_id(dev, pm_domain);

	if (IS_ERR_OR_NULL(pm_domain_dev)) {
		dev_err(dev,
			"%s: attaching PM domain index %d resulted in error "
			"code %ld",
			__func__, pm_domain, PTR_ERR(pm_domain_dev));
	}

	return pm_domain_dev;
}

#define POWER_FLAGS_BUFFER_SIZE 4

#if defined(PVR_DPF_CUSTOMER_DEBUG_ON)
static const char *power_flags_to_string(PVRSRV_POWER_FLAGS power_flags,
					 char *buffer, size_t buffer_size)
{
	if (buffer_size != POWER_FLAGS_BUFFER_SIZE) {
		goto end;
	}

	if (power_flags == PVRSRV_POWER_FLAGS_NONE) {
		buffer[0] = '-';
		buffer[1] = 0;
	} else {
		size_t index = 0;

		if (BITMASK_HAS(power_flags, PVRSRV_POWER_FLAGS_FORCED)) {
			buffer[index++] = 'F';
		}
		if (BITMASK_HAS(power_flags, PVRSRV_POWER_FLAGS_OSPM_SUSPEND_REQ)) {
			buffer[index++] = 'S';
		}
		if (BITMASK_HAS(power_flags, PVRSRV_POWER_FLAGS_OSPM_RESUME_REQ)) {
			buffer[index++] = 'R';
		}
		buffer[index] = 0;
	}

end:
	return buffer;
}
#endif

/**
 * Helper to decrement the usage count of a power domain.
 *
 * This may or may not actually power down the domain, but the
 * usage count is always decremented.
 *
 * Errors are logged with the appropriate log level.
 */
static void power_down_domain(struct device *dev, struct device *pd,
		int (*func)(struct device *), const char *func_name,
		const char *pd_name)
{
	int ret = func(pd);

	if (ret == -EAGAIN || ret == -EBUSY) {
		dev_info(dev, "%s(%s) deferred as PM status was active: %d",
				func_name, pd_name, ret);
	} else if (ret) {
		dev_err(dev, "%s(%s) failed: %d", func_name, pd_name, ret);
	}
}
#define POWER_DOWN_DOMAIN(pd_name, func, pd) \
	power_down_domain(pixel_dev->dev, pd, func, #func, pd_name)

static PVRSRV_ERROR pre_power_state(IMG_HANDLE sys_data,
				    PVRSRV_SYS_POWER_STATE new_power_state,
				    PVRSRV_SYS_POWER_STATE current_power_state,
				    PVRSRV_POWER_FLAGS power_flags)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)sys_data;
#if defined(PVR_DPF_CUSTOMER_DEBUG_ON)
	char buffer[POWER_FLAGS_BUFFER_SIZE];
#endif

	if (disable_pm_runtime)
		return PVRSRV_OK;

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: %s -> %s (flags: %s)", __func__,
		PVRSRVSysPowerStateToString(current_power_state),
		PVRSRVSysPowerStateToString(new_power_state),
		power_flags_to_string(power_flags, buffer, sizeof(buffer))));

	if (!(current_power_state == PVRSRV_SYS_POWER_STATE_ON &&
	      new_power_state == PVRSRV_SYS_POWER_STATE_OFF))
		return PVRSRV_OK;

	if (BITMASK_HAS(power_flags, PVRSRV_POWER_FLAGS_OSPM_SUSPEND_REQ)) {
		PVR_DPF((PVR_DBG_CUSTOMER, "%s: handling transition ON -> OFF", __func__));

		SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_POWERING_OFF, 0);
		ATRACE_BEGIN("GPU power-off ON->OFF");

		POWER_DOWN_DOMAIN("GPU", pm_runtime_put, pixel_dev->dev);

		POWER_DOWN_DOMAIN("SSWRP", pm_runtime_put_sync_suspend, pixel_dev->sswrp_gpu_pd);

		ATRACE_END();
		SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_POWERING_OFF, 1);
	} else {
		PVR_DPF((PVR_DBG_CUSTOMER, "%s: handling transition ON -> PG", __func__));

		/* By the time we get here there's already been some idle
		 * delay, so it's okay to do the core logic power-off
		 * synchronously; that will avoid waking up some other core
		 * and could avoid scheduling delays before power-gate.
		 * Power-gating is low-latency so there's no need to make it
		 * asynchronous.
		 */

		SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_POWERING_OFF, 0);
		ATRACE_BEGIN("GPU power-off ON->PG");

		POWER_DOWN_DOMAIN("GPU", pm_runtime_put_sync, pixel_dev->dev);
		pm_runtime_mark_last_busy(pixel_dev->sswrp_gpu_pd);
		POWER_DOWN_DOMAIN("SSWRP", pm_runtime_put_autosuspend, pixel_dev->sswrp_gpu_pd);

		ATRACE_END();
		SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_POWERING_OFF, 1);
	}
	slc_disable(&pixel_dev->slc_data);

	/* We always put our usage counts on the power domains, whether or not
	 * they actually power off successfully -- so as far as the caller
	 * is concerned, the power off always succeeds (and thus a power on is
	 * needed before accessing the GPU again).
	 */
	return PVRSRV_OK;
}

static PVRSRV_ERROR post_power_state(IMG_HANDLE sys_data,
				     PVRSRV_SYS_POWER_STATE new_power_state,
				     PVRSRV_SYS_POWER_STATE current_power_state,
				     PVRSRV_POWER_FLAGS power_flags)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)sys_data;
	struct device *const dev = pixel_dev->dev;
#if defined(PVR_DPF_CUSTOMER_DEBUG_ON)
	char buffer[POWER_FLAGS_BUFFER_SIZE];
#endif
	PVRSRV_ERROR err = PVRSRV_OK;
	int ret;

	if (disable_pm_runtime)
		return PVRSRV_OK;

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: %s -> %s (flags: %s)", __func__,
		PVRSRVSysPowerStateToString(current_power_state),
		PVRSRVSysPowerStateToString(new_power_state),
		power_flags_to_string(power_flags, buffer, sizeof(buffer))));

	if (!(current_power_state == PVRSRV_SYS_POWER_STATE_OFF &&
	      new_power_state == PVRSRV_SYS_POWER_STATE_ON))
		return PVRSRV_OK;

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: handling transition -> ON", __func__));

	SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_POWERING_ON, 0); /* Begin */
	ATRACE_BEGIN("GPU power-on OFF->ON");

	/* We should make sure that we hold a reference to the sswrp by the time
	 * the child gpu_core_logic_pd domain is powered on. So we use an async
	 * get for sswrp_gpu_pd; any waiting or errors will be handled in the
	 * synchronous power-on of gpu_core_logic_pd.
	 */
	ret = pm_runtime_get(pixel_dev->sswrp_gpu_pd);
	/* pm_runtime_get returns EINPROGRESS if there is no active suspend
	 * request or there is an active asynchronous suspend request.
	 * Continue even when it returns EINPROGRESS
	 */
	if (ret < 0 && ret != -EINPROGRESS) {
		dev_err(dev, "pm_runtime_get(SSWRP) failed: %d", ret);
		err = PVRSRV_ERROR_SYSTEM_POWER_CHANGE_FAILURE;
	} else {
		/* Power on gpu_core_logic_pd synchronously, by way of the GPU device */
		ret = pm_runtime_resume_and_get(pixel_dev->dev);
		if (ret) {
			dev_err(dev, "pm_runtime_resume_and_get(GPU) failed: %d", ret);
			err = PVRSRV_ERROR_SYSTEM_POWER_CHANGE_FAILURE;
		}
	}

	ATRACE_END();
	SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_POWERING_ON, 1); /* End */

	if (PVRSRV_OK == err) {
		slc_enable(&pixel_dev->slc_data);
		/* SLC LUTs are not retained across SSWRP power-off */
		slc_program_lut(&pixel_dev->slc_data);
	} else {
		/* pm_runtime_get() always increments the SSWRP domain's usage count,
		 * even if it fails.  Therefore we always need to decrement it again
		 * here, if the power transition as a whole failed.
		 */
		ret = pm_runtime_put(pixel_dev->sswrp_gpu_pd);
		(void)ret;
	}

	return err;
}

void deinit_genpd(struct pixel_gpu_device *pixel_dev)
{
	deinit_genpd_sysfs(pixel_dev);

	if (pixel_dev->notifiers_registered) {
		dev_pm_genpd_remove_notifier(pixel_dev->gpu_core_logic_pd);
		dev_pm_genpd_remove_notifier(pixel_dev->sswrp_gpu_pd);
	}

#if defined(SUPPORT_LINUX_DVFS)
	if (pixel_dev->pf_state_link) {
		device_link_del(pixel_dev->pf_state_link);
	}
#endif

	if (pixel_dev->core_logic_link) {
		device_link_del(pixel_dev->core_logic_link);
	}

	if (!IS_ERR_OR_NULL(pixel_dev->gpu_core_logic_pd)) {
		dev_pm_domain_detach(pixel_dev->gpu_core_logic_pd, true);
	}

	if (!IS_ERR_OR_NULL(pixel_dev->sswrp_gpu_pd)) {
		dev_pm_domain_detach(pixel_dev->sswrp_gpu_pd, true);
	}
}

int init_genpd(struct pixel_gpu_device *pixel_dev)
{
	struct device *dev = pixel_dev->dev;
	int ret = -ENODEV;

	if (disable_pm_runtime)
		BUG_ON(!pixel_dev->of_properties.jones_force_on);

	mutex_init(&pixel_dev->power_state.lock);
	mutex_lock(&pixel_dev->power_state.lock);
	if (pixel_dev->of_properties.jones_force_on) {
		pixel_dev->power_state.cur_state = PIXEL_GPU_POWER_STATE_ON;
	} else {
		/*
		 * Depending on which supplier devices (e.g. smmu, clk) are configured in the
		 * devicetree, sswrp_gpu_pd might initially be on or off here (OFF or PG
		 * state). Rather than trying to figure that out, we'll just leave the
		 * current state as "unknown" until we see the first genpd notifier. That
		 * also avoids trying to update cumulative time for the previous state when
		 * we don't know when that state was entered.
		 */
		pixel_dev->power_state.cur_state = PIXEL_GPU_POWER_STATE_COUNT;
	}
	mutex_unlock(&pixel_dev->power_state.lock);

	pixel_dev->dev_config->pfnPrePowerState  = pre_power_state;
	pixel_dev->dev_config->pfnPostPowerState = post_power_state;

	pixel_dev->core_logic_notifier.notifier_call = genpd_notify_gpu;
	pixel_dev->sswrp_notifier.notifier_call = genpd_notify_sswrp;

	/* As the GPU DT node references TWO power-domains, the GPU device will not
	 * be automatically attached to either of them.  Instead, create virtual
	 * devices for them here.
	 */
	pixel_dev->sswrp_gpu_pd = attach_pm_domain(dev, PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD);
	pixel_dev->gpu_core_logic_pd = attach_pm_domain(dev, PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD);

	if (IS_ERR_OR_NULL(pixel_dev->gpu_core_logic_pd) ||
	    IS_ERR_OR_NULL(pixel_dev->sswrp_gpu_pd)) {
		dev_err(dev, "failed to attach genpd domain(s) - cannot power on GPU");
		goto err;
	}

	/* Create a dev_link between the GPU device and gpu_core_logic_pd to
	 * ensure whenever the GPU is powered on, gpu_core_logic_pd will be up.
	 * Note that gpu_core_logic_pd in turn requires sswrp_gpu_pd.
	 */
	pixel_dev->core_logic_link = device_link_add(pixel_dev->dev,
			pixel_dev->gpu_core_logic_pd, DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
	if (!pixel_dev->core_logic_link) {
		dev_err(dev, "failed to make dev_link between GPU and gpu_core_logic_pd");
		goto err;
	}

#if defined(SUPPORT_LINUX_DVFS)
	/* Create a dev_link between the GPU device and gpu_pf_state driver to
	 * ensure whenever the GPU is powered on, gpu_pf_state will be resumed.
	 */
	pixel_dev->pf_state_link = device_link_add(pixel_dev->dev,
						   &pixel_dev->of_pdevs.gpu_pf_state_pdev->dev,
						   DL_FLAG_PM_RUNTIME);
	put_device(&pixel_dev->of_pdevs.gpu_pf_state_pdev->dev);
	pixel_dev->of_pdevs.gpu_pf_state_pdev = NULL;

	if (!pixel_dev->pf_state_link) {
		dev_err(dev, "failed to make dev_link between GPU and gpu_pf_state");
		goto err;
	}
#endif

	if (!disable_pm_runtime) {
		pm_runtime_use_autosuspend(pixel_dev->sswrp_gpu_pd);
		pm_runtime_set_autosuspend_delay(pixel_dev->sswrp_gpu_pd,
						 pixel_dev->of_properties.autosuspend_latency);

		ret = dev_pm_genpd_add_notifier(pixel_dev->sswrp_gpu_pd,
						&pixel_dev->sswrp_notifier);
		if (ret) {
			dev_warn(dev, "failed to attach sswrp_gpu_pd notifier: code %d", ret);
			goto err;
		}

		ret = dev_pm_genpd_add_notifier(pixel_dev->gpu_core_logic_pd,
						&pixel_dev->core_logic_notifier);
		if (ret) {
			dev_warn(dev, "failed to attach gpu_core_logic_pd notifier: code %d", ret);
			dev_pm_genpd_remove_notifier(pixel_dev->sswrp_gpu_pd);
			goto err;
		}

		pixel_dev->notifiers_registered = true;

		pm_runtime_enable(pixel_dev->dev);
	}

	/* treat sysfs init failures as non-fatal */
	(void)init_genpd_sysfs(pixel_dev);

	return 0;

err:
	deinit_genpd(pixel_dev);
	return -ENODEV;
}

static ssize_t current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	int cur_state;

	mutex_lock(&pixel_dev->power_state.lock);
	cur_state = pixel_dev->power_state.cur_state;
	mutex_unlock(&pixel_dev->power_state.lock);

	return sysfs_emit(buf, "%s\n", power_state_str(cur_state));
}

/* Without this #undef, we end up with a sysfs file named "get_current()" */
#undef current
static DEVICE_ATTR_RO(current);
/* from <linux>/include/asm-generic/current.h */
#define current get_current()

static ssize_t states_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int at = 0;
	int i;

	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++) {
		at += sysfs_emit_at(buf, at, "%s%s", i == 0 ? "" : " ",
				    power_state_str(i));
	}
	at += sysfs_emit_at(buf, at, "\n");

	return at;
}
static DEVICE_ATTR_RO(states);

static ssize_t entry_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	uint64_t entry_count[PIXEL_GPU_POWER_STATE_COUNT];
	int at = 0;
	int i;

	mutex_lock(&pixel_dev->power_state.lock);
	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++)
		entry_count[i] = pixel_dev->power_state.stats[i].entry_count;
	mutex_unlock(&pixel_dev->power_state.lock);

	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++) {
		at += sysfs_emit_at(buf, at, "%s%llu", i == 0 ? "" : " ",
				    entry_count[i]);
	}
	at += sysfs_emit_at(buf, at, "\n");

	return at;
}
static DEVICE_ATTR_RO(entry_count);

static ssize_t time_in_state_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	ktime_t tis_ns[PIXEL_GPU_POWER_STATE_COUNT];
	int at = 0;
	int i;

	mutex_lock(&pixel_dev->power_state.lock);
	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++)
		tis_ns[i] = pixel_dev->power_state.stats[i].cumulative_time_ns;
	mutex_unlock(&pixel_dev->power_state.lock);

	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++)
		at += sysfs_emit_at(buf, at, "%s%lld", i == 0 ? "" : " ",
				    ktime_to_ms(tis_ns[i]));
	at += sysfs_emit_at(buf, at, "\n");

	return at;
}
static DEVICE_ATTR_RO(time_in_state_ms);

static ssize_t last_entry_ms_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	ktime_t entry_ns[PIXEL_GPU_POWER_STATE_COUNT];
	int at = 0;
	int i;

	mutex_lock(&pixel_dev->power_state.lock);
	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++)
		entry_ns[i] = pixel_dev->power_state.stats[i].last_entry_ns;
	mutex_unlock(&pixel_dev->power_state.lock);

	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++) {
		at += sysfs_emit_at(buf, at, "%s%lld", i == 0 ? "" : " ",
				    ktime_to_ms(entry_ns[i]));
	}
	at += sysfs_emit_at(buf, at, "\n");

	return at;
}
static DEVICE_ATTR_RO(last_entry_ms);

static ssize_t last_exit_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	ktime_t exit_ns[PIXEL_GPU_POWER_STATE_COUNT];
	int at = 0;
	int i;

	mutex_lock(&pixel_dev->power_state.lock);
	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++)
		exit_ns[i] = pixel_dev->power_state.stats[i].last_exit_ns;
	mutex_unlock(&pixel_dev->power_state.lock);

	for (i = 0; i < PIXEL_GPU_POWER_STATE_COUNT; i++) {
		at += sysfs_emit_at(buf, at, "%s%lld", i == 0 ? "" : " ",
				    ktime_to_ms(exit_ns[i]));
	}
	at += sysfs_emit_at(buf, at, "\n");

	return at;
}
static DEVICE_ATTR_RO(last_exit_ms);

static struct attribute *power_state_attrs[] = {
	&dev_attr_current.attr,
	&dev_attr_states.attr,
	&dev_attr_entry_count.attr,
	&dev_attr_time_in_state_ms.attr,
	&dev_attr_last_entry_ms.attr,
	&dev_attr_last_exit_ms.attr,
	NULL,
};

static const struct attribute_group power_state_attr_group = {
	.name = "power_state",
	.attrs = power_state_attrs,
};

// Valid APM latency values are in the range [-1, 2^32). An APM latency of -1
// means that APM is turned off.
static ssize_t apm_latency_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	RGX_TIMING_INFORMATION *timing_info =
		((RGX_DATA *)pixel_dev->dev_config->hDevData)->psRGXTimingInfo;

	if (timing_info->bEnableActivePM)
		return sysfs_emit(buf, "%u\n",
				    timing_info->ui32ActivePMLatencyms);

	return sysfs_emit(buf, "-1\n");
}

static ssize_t apm_latency_ms_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	RGX_TIMING_INFORMATION *timing_info =
		((RGX_DATA *)pixel_dev->dev_config->hDevData)->psRGXTimingInfo;
	long long apm_latency_ms;
	PVRSRV_ERROR err;

	if (kstrtoll(buf, 0, &apm_latency_ms))
		return -EINVAL;

	if ((apm_latency_ms < 0) || (__UINT32_MAX__ < apm_latency_ms))
		return -EINVAL;

	if (timing_info->ui32ActivePMLatencyms == apm_latency_ms)
		return count;

	dev_dbg(pixel_dev->dev, "%s: changing APM latency from %u to %lld",
		__func__, timing_info->ui32ActivePMLatencyms, apm_latency_ms);

	err = RGXAPMLatencyChange(pixel_dev->dev_config->psDevNode,
				  apm_latency_ms,
				  /* bActivePMLatencyPersistant= */ true);

	if (err == PVRSRV_OK) {
		timing_info->ui32ActivePMLatencyms = apm_latency_ms;
	} else {
		dev_err(pixel_dev->dev, "%s: could not change APM latency: %s",
			__func__, PVRSRVGetErrorString(err));
		return -EIO;
	}

	return count;
}
static DEVICE_ATTR_RW(apm_latency_ms);

static int init_genpd_sysfs(struct pixel_gpu_device *pixel_dev)
{
	int result = 0; /* return first error or success */
	int ret;

	ret = sysfs_create_group(&pixel_dev->dev->kobj, &power_state_attr_group);
	if (ret != 0) {
		dev_warn(pixel_dev->dev, "failed to create power_state sysfs group: %d", ret);
		if (result == 0)
			result = ret;
	}

	ret = sysfs_create_file(&pixel_dev->dev->kobj, &dev_attr_apm_latency_ms.attr);
	if (ret != 0) {
		dev_warn(pixel_dev->dev, "failed to create apm sysfs file: %d", ret);
		if (result == 0)
			result = ret;
	}

	return result;
}

static void deinit_genpd_sysfs(struct pixel_gpu_device *pixel_dev)
{
	sysfs_remove_file(&pixel_dev->dev->kobj, &dev_attr_apm_latency_ms.attr);
	sysfs_remove_group(&pixel_dev->dev->kobj, &power_state_attr_group);
}
