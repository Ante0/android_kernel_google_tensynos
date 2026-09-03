// SPDX-License-Identifier: GPL-2.0

#include "dvfs.h"

#include <pvrsrvkm/services/server/include/osfunc.h>

#include <misc/sbbm.h>
#if defined(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif
#include <dvfs-helper/google_dvfs_helper.h>
#include <perf/core/google_vote_manager.h>

#if defined(SUPPORT_SOC_TIMER)
#include <soc/google/google_gtc.h>
#endif

#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)

/**
 * set_frequency() - Sets GPU frequency using clk
 * @hSysData:	private system-layer data
 * @ui32Freq:	the desired frequency
 *
 * This function sets GPU frequency using the common
 * clk interface
 */
static void set_frequency(IMG_HANDLE hSysData, IMG_UINT32 ui32Freq)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;
	struct clk *const gpu_clk = pixel_dev->gpu_clk;
	int ret;

	SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_SETTING_CLK, 0);
	ret = clk_set_rate(gpu_clk, (unsigned long)ui32Freq);
	SBBM_SIGNAL_UPDATE(SBB_SIG_GPU_SETTING_CLK, 1);

	if (ret) {
		dev_err(pixel_dev->dev, "%s: failed with %d setting target frequency of %u",
			__func__, ret, ui32Freq);
		return;
	}

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: %uHz", __func__, ui32Freq));
}

/**
 * set_voltage() - Unused function for setting GPU voltage
 * @hSysData:	private system-layer data
 * @ui32Volt:	the desired voltage
 *
 * This function is a NOP as the CPM handles voltage
 * based on the requested frequency
 */
static void set_voltage(IMG_HANDLE hSysData, IMG_UINT32 ui32Volt)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);
	PVR_UNREFERENCED_PARAMETER(ui32Volt);
}

static PVRSRV_ERROR register_dvfs(struct devfreq *psDevFreq)
{
	PVRSRV_ERROR err = PVRSRV_OK;

	if (vote_manager_init_devfreq(psDevFreq))
		err = PVRSRV_ERROR_INIT_FAILURE;

	return err;
}

static void unregister_dvfs(struct devfreq *psDevFreq)
{
	vote_manager_remove_devfreq(psDevFreq);
}

static IMG_UINT32 **get_fab_votes(IMG_HANDLE hSysData)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;

	return pixel_dev->fab_votes;
}
#endif

#if defined(SUPPORT_SOC_TIMER)
/**
 * read_soc_timer() - Read SOC timing value
 * @hSysData:	private system-layer data
 * Return: This function returns a register value representing
 * SoC time
 */
static IMG_UINT64 read_soc_timer(IMG_HANDLE hSysData)
{
	return goog_gtc_get_counter();
}
#endif

int init_pixel_dvfs(struct pixel_gpu_device *pixel_dev)
{
	PVRSRV_ERROR ret          = PVRSRV_OK;
#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_SOC_TIMER) || defined(SUPPORT_PDVFS)
	PVRSRV_DEVICE_CONFIG *cfg = pixel_dev->dev_config;
#endif

#if defined(SUPPORT_SOC_TIMER)
	cfg->pfnSoCTimerRead = read_soc_timer;
#endif

#if defined(SUPPORT_LINUX_DVFS)
	// TODO: Get this from DT
	cfg->sDVFS.sDVFSDeviceCfg.ui32PollMs             = 20;
	// The utilisation at which the GPU frequency will be boosted to maximum frequency
	cfg->sDVFS.sDVFSGovernorCfg.ui32UpThreshold      = 90;
	// Range size under which the utilisation can vary without triggering a DVFS transition
	cfg->sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif

#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
	/*
	 * clk device is required for:
	 * obtaining current clock rate
	 * DVFS (SUPPORT_LINUX_DVFS)
	 * cooling device (SUPPORT_LINUX_DVFS && CONFIG_DEVFREQ_THERMAL)
	 */
	pixel_dev->gpu_clk = devm_clk_get(pixel_dev->dev, "gpu_pf_state");
	/*
	 * If the clk device cannot be obtained, treat that as
	 * an error.
	 */
	if (IS_ERR_OR_NULL(pixel_dev->gpu_clk)) {
		dev_err(pixel_dev->dev, "%s: Failed to get GPU clk", __func__);
		ret = PVRSRV_ERROR_UNABLE_TO_GET_CLOCK;
		goto init_fail;
	}
#if defined(CONFIG_DEVFREQ_THERMAL)
	// b/289995706 provide an implementation for get_real_power
	pixel_dev->gpu_power_ops.get_real_power = NULL;
	cfg->sDVFS.sDVFSDeviceCfg.psPowerOps             = &pixel_dev->gpu_power_ops;
#endif

	cfg->sDVFS.sDVFSDeviceCfg.bIdleReq               = IMG_FALSE;
	cfg->sDVFS.sDVFSDeviceCfg.pfnDVFSRegister        = register_dvfs;
	cfg->sDVFS.sDVFSDeviceCfg.pfnSetFrequency        = set_frequency;
	cfg->sDVFS.sDVFSDeviceCfg.pfnSetVoltage          = set_voltage;
	cfg->sDVFS.sDVFSDeviceCfg.pfnGetFabVotes         = get_fab_votes;

	if (dvfs_helper_add_opps_to_device(pixel_dev->dev, pixel_dev->dev->of_node)) {
		dev_err(pixel_dev->dev, "%s: Failed to add opps from helper", __func__);
		ret = PVRSRV_ERROR_UNABLE_TO_RETRIEVE_INFO;
	}

init_fail:
#endif
	return ret;
}

void deinit_pixel_dvfs(struct pixel_gpu_device *pixel_dev)
{
#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
	PVRSRV_DEVICE_CONFIG *cfg = pixel_dev->dev_config;

	cfg->sDVFS.sDVFSDeviceCfg.pfnDVFSUnregister = unregister_dvfs;
#endif
}
