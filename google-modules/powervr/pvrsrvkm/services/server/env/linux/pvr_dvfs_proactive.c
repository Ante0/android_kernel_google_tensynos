/*************************************************************************/ /*!
@File           pvr_dvfs_proactive.c
@Title          PowerVR devfreq device common utilities
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Proactive DVFS kernel/OS code
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(NO_HARDWARE)

#include <linux/devfreq.h>
#include <linux/version.h>
#include <linux/device.h>
#include <drm/drm.h>
#if defined(CONFIG_PM_OPP)
#include <linux/pm_opp.h>
#endif
#include <linux/pm_qos.h>

#include "pvrsrv.h"

/*
 * Proactive DVFS support code with a devfreq driver wrapper around
 * the FW governor implementation. The devfreq wrapper is agnostic
 * to the underlying algorithm but proactive is anticipated to be
 * the optimal governor.
 */
#include "power.h"
#include "pvrsrv_device.h"

#include "rgxdevice.h"
#include "rgxinit.h"
#include "rgxpdvfs.h"

#include "pvr_dvfs.h"
#include "pvr_dvfs_proactive.h"
#include "pvr_dvfs_common.h"

#include "kernel_compatibility.h"

#if defined(SUPPORT_PDVFS_DEVFREQ)
#if !defined(CONFIG_PM_DEVFREQ)
#error "PVR DVFS governor requires kernel support for devfreq (CONFIG_PM_DEVFREQ = 1)"
#endif
#include "governor.h"

int pvr_governor_init(void);
void pvr_governor_exit(void);
PVRSRV_ERROR PDVFSSendFirmwareCommand(struct device *dev,
	                             unsigned int event,
	                             unsigned int *data);

void PDVFSUpdatePollingInterval(unsigned int *data);

/* governor.h API for use with firmware-based DVFS */
int devfreq_update_target(struct devfreq *devfreq, unsigned long freq);
#endif

#define HZ_PER_KHZ		(1000)

/*
 * Custom governor event to transfer the PDVFS config to firmware
 * governor.
 */
#define DEVFREQ_GOV_FIRMWARE_CAPACITY			(100)
#define DEVFREQ_GOV_FIRMWARE_MINFREQ			(101)
#define DEVFREQ_GOV_FIRMWARE_MAXFREQ			(102)
#define DEVFREQ_GOV_UPDATE_UP_THRESHOLD			(103)
#define DEVFREQ_GOV_UPDATE_DOWN_DIFFERENTIAL	(104)

/* Setting min_freq or max_freq to zero resets the current constraint */
#define DVFS_CONSTRAINTS_RESET_VALUE		(0)

/*************************************************************************/ /*!
@Function       InitPDVFS

@Description    Initialise the device for Proactive DVFS support, stage 1.
                Prepares the OPP table from the devicetree, if enabled.

@Input          psDeviceNode       Device node
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
#if defined(SUPPORT_PDVFS)
PVRSRV_ERROR InitPDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
#if !(defined(CONFIG_PM_OPP) && defined(CONFIG_OF))
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);

	return PVRSRV_OK;
#else
#if defined(SUPPORT_PDVFS_DEVFREQ)
	IMG_PDVFS_DEVICE       *psPDVFSDevice;
#endif
	IMG_DVFS_DEVICE_CFG    *psDVFSDeviceCfg = NULL;
	struct device          *psDev;
	int                     err;

	if (!psDeviceNode)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_ASSERT(psDeviceNode->psDevConfig);

	psDev = psDeviceNode->psDevConfig->pvOSDevice;
#if defined(SUPPORT_PDVFS_DEVFREQ)
	psPDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;
#endif
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice.eState = PVR_DVFS_STATE_INIT_PENDING;

	/*
	 * Setup the OPP table from the device tree for Proactive DVFS.
	 * Only add the OPP table in case it has not been pre-filled already.
	 */
	if (dev_pm_opp_get_opp_count(psDev) <= 0)
	{
		err = dev_pm_opp_of_add_table(psDev);
	}
	else
	{
		err = 0;
	}

	if (err == 0)
	{
		psDVFSDeviceCfg->bDTConfig = IMG_TRUE;
	}
	else
	{
		/*
		 * If there are no device tree or system layer provided operating points
		 * then return an error
		 */
		if (psDVFSDeviceCfg->pasOPPTable)
		{
			PVR_DPF((PVR_DBG_WARNING, "Using system opp points."));
			psDVFSDeviceCfg->bDTConfig = IMG_FALSE;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "No system or device tree opp points found, %d", err));
			return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
		}
	}

#if defined(SUPPORT_PDVFS_DEVFREQ)
	/* Create the PVR governor which wraps the governor logic in the firmware */
	err = pvr_governor_init();
	if (err != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to init PVR governor, %d", err));
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}
	psPDVFSDevice->bGovernorReady = true;
#endif
	psDVFSDeviceCfg->ui32UpThresholdInPct = 90;
	psDVFSDeviceCfg->ui32DownDifferentialInPct = 5;
	return PVRSRV_OK;
#endif
}

/*************************************************************************/ /*!
@Function       DeinitPDVFS

@Description    De-Initialise the device for Proactive DVFS support.

@Input          psDeviceNode       Device node
@Return         None
*/ /**************************************************************************/
void DeinitPDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
#if !(defined(CONFIG_PM_OPP) && defined(CONFIG_OF))
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
#else
#if defined(SUPPORT_PDVFS_DEVFREQ)
	IMG_PDVFS_DEVICE       *psPDVFSDevice;
#endif
	IMG_DVFS_DEVICE_CFG    *psDVFSDeviceCfg = NULL;
	struct device          *psDev = NULL;

	/* Check the device exists */
	if (!psDeviceNode)
	{
		return;
	}

	psDev = psDeviceNode->psDevConfig->pvOSDevice;
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

#if defined(SUPPORT_PDVFS_DEVFREQ)
	psPDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;
	if (psPDVFSDevice->bGovernorReady)
	{
		pvr_governor_exit();
		psPDVFSDevice->bGovernorReady = false;
	}
#endif
	if (psDVFSDeviceCfg->bDTConfig)
	{
		/*
		 * Remove OPP entries for this device; only static entries from
		 * the device tree are present.
		 */
		dev_pm_opp_of_remove_table(psDev);
	}
#endif
}

#if defined(SUPPORT_PDVFS_DEVFREQ)
#if !defined(CONFIG_PM_DEVFREQ)
#error "PVR Proactive DVFS governor requires kernel support for devfreq (CONFIG_PM_DEVFREQ = 1)"
#endif

/* DEVFREQ governor name */
#define DEVFREQ_GOV_PVR_CUSTOM		"pvr_firmware"


static int pvr_governor_get_target(struct devfreq *devfreq_dev,
								   unsigned long *freq)
{
	/* implemented in firmware.. */

	return 0;
}

static int pvr_governor_event_handler(struct devfreq *devfreq_dev,
									  unsigned int event, void *data)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!devfreq_dev)
	{
		pr_err("%s: devfreq_dev not ready.\n", __func__);
		return -ENODEV;
	}

	/*
	 * We cannot take the deviceId here, as the DRM device
	 * may not be initialised. Null pointer in
	 * struct drm_device *ddev = dev_get_drvdata(dev)
	 */

	switch (event) {
	case DEVFREQ_GOV_START:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_START event.\n"));
		break;

	case DEVFREQ_GOV_STOP:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_STOP event.\n"));
		break;

	case DEVFREQ_GOV_UPDATE_INTERVAL:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_INTERVAL event.\n"));
		if (*(unsigned int *)data > 500)
		{
			return -EINVAL;
		}
		PDVFSUpdatePollingInterval((unsigned int*) data);
		eError = PDVFSSendFirmwareCommand(&devfreq_dev->dev, event, data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_SUSPEND event.\n"));
		break;

	case DEVFREQ_GOV_RESUME:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_RESUME event.\n"));
		break;
	/*
	 * Custom events
	 */
	case DEVFREQ_GOV_FIRMWARE_CAPACITY:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_FIRMWARE_CAPACITY event.\n"));
		eError = PDVFSSendFirmwareCommand(&devfreq_dev->dev, event, data);
		break;
	case DEVFREQ_GOV_FIRMWARE_MINFREQ:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_FIRMWARE_MINFREQ event.\n"));
		eError = PDVFSSendFirmwareCommand(&devfreq_dev->dev, event, data);
		break;
	case DEVFREQ_GOV_FIRMWARE_MAXFREQ:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_FIRMWARE_MAXFREQ event.\n"));
		eError = PDVFSSendFirmwareCommand(&devfreq_dev->dev, event, data);
		break;
	case DEVFREQ_GOV_UPDATE_UP_THRESHOLD:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_UPDATE_UP_THRESHOLD event.\n"));
		eError = PDVFSSendFirmwareCommand(&devfreq_dev->dev, event, data);
		break;
	case DEVFREQ_GOV_UPDATE_DOWN_DIFFERENTIAL:
		PVR_DPF((PVR_DBG_CUSTOMER, "GOV_UPDATE_DOWN_DIFFERENTIAL event.\n"));
		eError = PDVFSSendFirmwareCommand(&devfreq_dev->dev, event, data);
		break;
	default:
		dev_err(&devfreq_dev->dev, "Unknown event.\n");
		break;
	}

	return 0;
}

static struct devfreq_governor pvr_custom_governor = {
	.name = DEVFREQ_GOV_PVR_CUSTOM,
	.get_target_freq = pvr_governor_get_target,
	.event_handler   = pvr_governor_event_handler,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0))
	.attrs = DEVFREQ_GOV_ATTR_POLLING_INTERVAL
		| DEVFREQ_GOV_ATTR_TIMER,
#else
	.immutable = true,
#endif
};

int pvr_governor_init(void)
{
	int ret;

	ret = devfreq_add_governor(&pvr_custom_governor);
	if (ret)
	{
		pr_err("%s: failed to install governor %d\n", __func__, ret);
	}

	return ret;
}

void pvr_governor_exit(void)
{
	int ret;

	ret = devfreq_remove_governor(&pvr_custom_governor);
	if (ret)
	{
		pr_err("Failed to remove governor (%u)\n", ret);
	}
}



static IMG_INT32 devfreq_target(struct device *dev, unsigned long *requested_freq, IMG_UINT32 flags)
{
	IMG_UINT32		ui32Freq, ui32Volt;
	struct dev_pm_opp *opp;

	/* Target clock freq is calculated in the FW, here we sync the devfreq view of the GPU clock */
	PVR_DPF((PVR_DBG_CUSTOMER,
	         "Frequency notification from firmware-based governor: %lu\n",
	         *requested_freq));

	opp = devfreq_recommended_opp(dev, requested_freq, flags);
	if (IS_ERR(opp)) {
		PVR_DPF((PVR_DBG_ERROR, "Invalid OPP"));
		return PTR_ERR(opp);
	}

	ui32Freq = dev_pm_opp_get_freq(opp);
	ui32Volt = dev_pm_opp_get_voltage(opp);
	PVR_DPF((PVR_DBG_CUSTOMER,
	         "Requested new voltage %u and freq %u\n", ui32Volt, ui32Freq));

	dev_pm_opp_put(opp);

	return 0;
}

static IMG_INT32 devfreq_cur_freq(struct device *dev, unsigned long *freq)
{
	int deviceId = GetDevID(dev);
	PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(deviceId);
	RGX_DATA *psRGXData = NULL;

	/* Check the device is registered */
	if (!psDeviceNode)
	{
		return -ENODEV;
	}

	psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;

	/* Check the RGX device is initialised */
	if (!psRGXData)
	{
		return -ENODATA;
	}

	*freq = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	return 0;
}

static struct devfreq_dev_profile img_devfreq_proactive =
{
	.polling_ms         = 10,
	.target             = devfreq_target,
	.get_dev_status     = NULL,		/* not used in the UM governor */
	.get_cur_freq       = devfreq_cur_freq,
};

/*************************************************************************/ /*!
@Function       NotifyCoreClkChange

@Description    Update the userspace devfreq after the firmware-based
                governor has recalculated the core frequency.

@Input          psDeviceNode       Device node
@Input          ui32NewFreq        New GPU clock frequency
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
#if defined(SUPPORT_PDVFS_DEVFREQ)
static PVRSRV_ERROR NotifyCoreClkChange(PPVRSRV_DEVICE_NODE psDeviceNode, IMG_UINT32 ui32NewFreq)
{
	int err = 0;

	IMG_PDVFS_DEVICE *psPDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;

	if (psPDVFSDevice->eState != PVR_DVFS_STATE_READY)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* Update devfreq UM driver */
	mutex_lock(&psPDVFSDevice->psDevFreq->lock);

	err = devfreq_update_target(psPDVFSDevice->psDevFreq, ui32NewFreq);
	if (err)
	{
		pr_err("%s: failed to notify governor %d\n", __func__, err);
	}

	mutex_unlock(&psPDVFSDevice->psDevFreq->lock);

	if (err)
	{
		return TO_IMG_ERR(err);
	}
	return PVRSRV_OK;
}
#endif

#if defined(SUPPORT_DVFS_RUNTIME_CONFIG) && defined(SUPPORT_PDVFS_HEADROOM_EXT)
static ssize_t capacity_headroom_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(
		GetDevID(dev->parent));
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
		psDVFSDeviceCfg->i32CapacityHeadroom);
}

static ssize_t capacity_headroom_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(
		GetDevID(dev->parent));
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	struct devfreq *df = psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice.psDevFreq;

	if (kstrtoint(buf, 0, &psDVFSDeviceCfg->i32CapacityHeadroom))
		return -EINVAL;

	df->governor->event_handler(df, DEVFREQ_GOV_FIRMWARE_CAPACITY, &psDVFSDeviceCfg->i32CapacityHeadroom);
	return count;
}

static DEVICE_ATTR_RW(capacity_headroom);

static int RegisterHeadroomFile(struct devfreq *devfreq)
{
	int ret = sysfs_create_file(&devfreq->dev.kobj, &dev_attr_capacity_headroom.attr);
	if (ret < 0)
	{
		dev_warn(&devfreq->dev, "Unable to create capacity headroom file");
	}
	return ret;
}

static void UnregisterHeadroomFile(struct devfreq *devfreq)
{
	sysfs_remove_file(&devfreq->dev.kobj, &dev_attr_capacity_headroom.attr);
}
#endif /* SUPPORT_DVFS_RUNTIME_CONFIG */

static IMG_BOOL validate_simpleondemand_parameters(
	IMG_UINT32 ui32UpThresholdInPct,
	IMG_UINT32 ui32DownDifferentialInPct)
{
	return (ui32UpThresholdInPct <= 100 &&
		ui32DownDifferentialInPct <= ui32UpThresholdInPct);
}

/*************************************************************************/ /*!
@Function       up_threshold_show

@Description    Report the current reactive up threshold to userspace

@Input          dev               OS device node (child of devfreq node)
@Input          attr              the device attributes (unused)
@Input          buf               the buffer to write into

@Return			the number of bytes printed into buf, or error
*/ /**************************************************************************/
static ssize_t up_threshold_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	PVRSRV_DEVICE_NODE *psDeviceNode =
		PVRSRVGetDeviceInstanceByKernelDevID(GetDevID(dev->parent));
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
		psDVFSDeviceCfg->ui32UpThresholdInPct);
}

/*************************************************************************/ /*!
@Function       up_threshold_store

@Description    Update up threshold and notify the firmware if it's on

@Input          dev               OS device node (child of devfreq node)
@Input          attr              the device attributes (unused)
@Input          buf               the buffer to read from
@Input          count             number of valid bytes in buf

@Return			the number of bytes read from buf, or error
*/ /**************************************************************************/
static ssize_t up_threshold_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(
		GetDevID(dev->parent));
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	struct devfreq *df = psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice.psDevFreq;
	IMG_UINT32 ui32UpThresholdInPct;

	if (kstrtoint(buf, 0, &ui32UpThresholdInPct))
		return -EINVAL;

	if (!validate_simpleondemand_parameters(
		ui32UpThresholdInPct,
		psDVFSDeviceCfg->ui32DownDifferentialInPct))
	{
		dev_warn(
		    dev,
		    "Invalid up threshold (%u%%) and down differential (%u%%)",
		    ui32UpThresholdInPct,
		    psDVFSDeviceCfg->ui32DownDifferentialInPct);
		return -EINVAL;
	}

	psDVFSDeviceCfg->ui32UpThresholdInPct = ui32UpThresholdInPct;

	df->governor->event_handler(df, DEVFREQ_GOV_UPDATE_UP_THRESHOLD,
				    &psDVFSDeviceCfg->ui32UpThresholdInPct);

	return count;
}

static DEVICE_ATTR_RW(up_threshold);

/*************************************************************************/ /*!
@Function       down_differential_show

@Description    Report the current reactive down differential to userspace

@Input          dev               OS device node (child of devfreq node)
@Input          attr              the device attributes (unused)
@Input          buf               the buffer to write into

@Return			the number of bytes printed into buf, or error
*/ /**************************************************************************/
static ssize_t down_differential_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	PVRSRV_DEVICE_NODE *psDeviceNode =
		PVRSRVGetDeviceInstanceByKernelDevID(GetDevID(dev->parent));
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
		psDVFSDeviceCfg->ui32DownDifferentialInPct);
}

/*************************************************************************/ /*!
@Function       down_differential_store

@Description    Update down differential and notify the firmware if it's on

@Input          dev               OS device node (child of devfreq node)
@Input          attr              the device attributes (unused)
@Input          buf               the buffer to read from
@Input          count             number of valid bytes in buf

@Return			the number of bytes read from buf, or error
*/ /**************************************************************************/
static ssize_t down_differential_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(
		GetDevID(dev->parent));
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	struct devfreq *df = psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice.psDevFreq;

	IMG_UINT32 ui32DownDifferentialInPct;

	if (kstrtouint(buf, 0, &ui32DownDifferentialInPct))
		return -EINVAL;

	if (!validate_simpleondemand_parameters(
		psDVFSDeviceCfg->ui32UpThresholdInPct,
		ui32DownDifferentialInPct))
	{
		dev_warn(dev, "Invalid up threshold (%u%%) "
			"and down differential (%u%%)",
			psDVFSDeviceCfg->ui32UpThresholdInPct,
			ui32DownDifferentialInPct);
		return -EINVAL;
	}

	psDVFSDeviceCfg->ui32DownDifferentialInPct = ui32DownDifferentialInPct;

	df->governor->event_handler(
	    df, DEVFREQ_GOV_UPDATE_DOWN_DIFFERENTIAL,
	    &psDVFSDeviceCfg->ui32DownDifferentialInPct);

	return count;
}

static DEVICE_ATTR_RW(down_differential);

static int RegisterPDVFSParametersFile(struct devfreq *devfreq)
{
	int ret = sysfs_create_file(&devfreq->dev.kobj, &dev_attr_up_threshold.attr);
	if (ret < 0)
	{
		dev_warn(&devfreq->dev, "Unable to create up threshold file");
	}
	ret = sysfs_create_file(&devfreq->dev.kobj, &dev_attr_down_differential.attr);
	if (ret < 0)
	{
		dev_warn(&devfreq->dev, "Unable to create down differential file");
	}
	return ret;
}

static void UnregisterPDVFSParametersFile(struct devfreq *devfreq)
{
	sysfs_remove_file(&devfreq->dev.kobj, &dev_attr_up_threshold.attr);
	sysfs_remove_file(&devfreq->dev.kobj, &dev_attr_down_differential.attr);
}

struct dvfs_notifier_block
{
	struct notifier_block nb;
	struct device *dev;
	struct devfreq *devfreq_dev;
};

#if defined(SUPPORT_PDVFS)
static inline IMG_PDVFS_DEVICE *PVRSRVGetPDVFSDevice(struct dvfs_notifier_block *dvfs_nb)
{
	int deviceId = GetDevID(dvfs_nb->dev);
	PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(deviceId);

	return &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;
}
#endif
/*************************************************************************/ /*!
@Function       NotifyMinFrequencyChange

@Description    Notifier for PM QOS min/max freq constraint change.

@Input          nb       Notifier block
@Input          uiFreq   new constraint (kHz)
@Input          data     null
@Return         0 on success or error code
*/ /**************************************************************************/
static int NotifyMinFrequencyChange(struct notifier_block *nb,
			unsigned long uiFreq, void *data)
{
	struct dvfs_notifier_block *dvfs_nb = container_of(nb, struct dvfs_notifier_block, nb);
	struct devfreq *df = dvfs_nb->devfreq_dev;
	struct dev_pm_opp *opp;
	unsigned int level;
	int err;

	PVR_UNREFERENCED_PARAMETER(data);

	if (!dvfs_nb->dev) {
		return -EINVAL;
	}

	uiFreq *= HZ_PER_KHZ;
	opp = devfreq_recommended_opp(dvfs_nb->dev, &uiFreq, 0);
	if (IS_ERR(opp)) {
		dev_warn(dvfs_nb->dev, "Invalid OPP");
		return -EINVAL;
	}

	err = FindOPPFreq(dvfs_nb->dev,
	                  img_devfreq_proactive.freq_table,
	                  uiFreq,
	                  &level);
	if (err) {
		dev_warn(dvfs_nb->dev, "Requested frequency %lu not found.", uiFreq);
		return err;
	}

	/* decrement ref taken in devfreq_recommended_opp */
	dev_pm_opp_put(opp);

	/* Notify event_handler to forward request to firmware. The frequency
	 * request should exactly match one of the OPP levels. */
	df->governor->event_handler(df, DEVFREQ_GOV_FIRMWARE_MINFREQ, &uiFreq);
#if defined(SUPPORT_PDVFS)
	PVRSRVGetPDVFSDevice(dvfs_nb)->ulMinFreq = uiFreq;
#endif
	return 0;
}

static struct dvfs_notifier_block img_pm_qos_minfreq_notifier =
{
	.nb =
	{
		.notifier_call = NotifyMinFrequencyChange,
		.next = NULL,
		.priority = 0,
	},
	.dev = NULL,
};

/*************************************************************************/ /*!
@Function       NotifyMaxFrequencyChange

@Description    Notifier for PM QOS min/max freq constraint change.

@Input          nb       Notifier block
@Input          uiFreq   new constraint (kHz)
@Input          data     null
@Return         0 on success or error code
*/ /**************************************************************************/
static int NotifyMaxFrequencyChange(struct notifier_block *nb,
			unsigned long uiFreq, void *data)
{
	struct dvfs_notifier_block *dvfs_nb = container_of(nb, struct dvfs_notifier_block, nb);
	struct devfreq *df = dvfs_nb->devfreq_dev;
	struct dev_pm_opp *opp;
	unsigned int level;
	int err;

	PVR_UNREFERENCED_PARAMETER(data);

	if (!dvfs_nb->dev) {
		return -EINVAL;
	}

	uiFreq *= HZ_PER_KHZ;
	opp = devfreq_recommended_opp(dvfs_nb->dev, &uiFreq, 0);
	if (IS_ERR(opp)) {
		dev_warn(dvfs_nb->dev, "Invalid OPP");
		return -EINVAL;
	}

	err = FindOPPFreq(dvfs_nb->dev,
	                  img_devfreq_proactive.freq_table,
	                  uiFreq,
	                  &level);
	if (err) {
		dev_warn(dvfs_nb->dev, "Requested frequency %lu not found.", uiFreq);
		return err;
	}

	/* decrement ref taken in devfreq_recommended_opp */
	dev_pm_opp_put(opp);

	/* Notify event_handler to forward request to firmware. The frequency
	 * request should exactly match one of the OPP levels. */
	df->governor->event_handler(df, DEVFREQ_GOV_FIRMWARE_MAXFREQ, &uiFreq);
#if defined(SUPPORT_PDVFS)
	PVRSRVGetPDVFSDevice(dvfs_nb)->ulMaxFreq = uiFreq;
#endif
	return 0;
}

static struct dvfs_notifier_block img_pm_qos_maxfreq_notifier =
{
	.nb =
	{
		.notifier_call = NotifyMaxFrequencyChange,
		.next = NULL,
		.priority = 0,
	},
	.dev = NULL,
};

/*************************************************************************/ /*!
@Function       PDVFSSendFirmwareCommand

@Description    Send PDVFS config update to the firmware. The action is dependent
                on the PDVFS algorithm, which can be simple on-demand or
                proactive using workload characteristics.

@Input          dev       Linux OS device
@Input          event     Config event
@Input          data      Config value
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR PDVFSSendFirmwareCommand(struct device *dev,
	                                  unsigned int event,
	                                  unsigned int *data)
{
#if defined(SUPPORT_FW_OPP_TABLE) && defined(CONFIG_OF) && defined(CONFIG_PM_OPP)
	int deviceId;
	int level;
	int err;
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	deviceId = GetDevID(dev->parent);
	psDeviceNode = PVRSRVGetDeviceInstanceByKernelDevID(deviceId);
	psDevInfo = psDeviceNode->pvDevice;

	if ((event == DEVFREQ_GOV_FIRMWARE_MINFREQ) ||
	    (event == DEVFREQ_GOV_FIRMWARE_MAXFREQ))
	{
		/* Handle the reset event separately as the OPP lookup will fail. */
		if (*data == DVFS_CONSTRAINTS_RESET_VALUE)
		{
			eError = PDVFSResetFrequencyConstraints(psDevInfo);
		}
	}

	switch (event)
	{
		case DEVFREQ_GOV_UPDATE_INTERVAL:
		{
#if defined(SUPPORT_PDVFS_POLLINT_EXT)
			PVR_DPF((PVR_DBG_MESSAGE, "Send polling interval = %u msec", *data));
			eError = PDVFSSetReactivePollingInterval(psDevInfo, *data);
#endif
			break;
		}
		case DEVFREQ_GOV_UPDATE_UP_THRESHOLD:
		{
			PVR_DPF((PVR_DBG_MESSAGE, "Send up threshold = %u percent", *data));
			eError = PDVFSSetUpThreshold(psDevInfo, *data);
			break;
		}
		case DEVFREQ_GOV_UPDATE_DOWN_DIFFERENTIAL:
		{
			PVR_DPF((PVR_DBG_MESSAGE, "Send down differential = %u percent", *data));
			eError = PDVFSSetDownDifferential(psDevInfo, *data);
			break;
		}
		case DEVFREQ_GOV_FIRMWARE_CAPACITY:
		{
#if defined(SUPPORT_PDVFS_HEADROOM_EXT)
			PVR_DPF((PVR_DBG_MESSAGE, "Send capacity headroom = %d Hz", *(signed int*) data));
			PDVFSSetFrequencyHeadroom(psDevInfo, *(signed int*) data);
#endif
			break;
		}
		case DEVFREQ_GOV_FIRMWARE_MINFREQ:
		{
			err = FindOPPFreq(dev->parent,
							  img_devfreq_proactive.freq_table,
							  (unsigned long) *data,
							  &level);
			if (err)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: No valid OPP level for freq %u (%d)", __func__, *data, err));
				eError = PVRSRV_ERROR_OPP_NOT_FOUND;
				goto _exit;
			}
			PVR_DPF((PVR_DBG_MESSAGE, "Send QOS constraint: min OPP level %d, freq %u Hz.", level, *data));
			eError = PDVFSLimitMinFrequency(psDevInfo, level);
			break;
		}
		case DEVFREQ_GOV_FIRMWARE_MAXFREQ:
		{
			err = FindOPPFreq(dev->parent,
							  img_devfreq_proactive.freq_table,
							  (unsigned long) *data,
							  &level);
			if (err)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: No valid OPP level for freq %u (%d)", __func__, *data, err));
				eError = PVRSRV_ERROR_OPP_NOT_FOUND;
				goto _exit;
			}
			PVR_DPF((PVR_DBG_MESSAGE, "Send QOS constraint: max OPP level %d, freq %u Hz.", level, *data));
			eError = PDVFSLimitMaxFrequency(psDevInfo, level);
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unexpected firmware event (%u)", __func__, event));
		}
	}

_exit:
	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(dev);
	PVR_UNREFERENCED_PARAMETER(event);
	PVR_UNREFERENCED_PARAMETER(data);
	return PVRSRV_ERROR_NOT_SUPPORTED;
#endif
}

/*************************************************************************/ /*!
@Function       PDVFSUpdatePollingInterval

@Description    Update polling interval in the firmware simple on-demand governor.

@Input          data       New polling interval (msec)
@Return         none
*/ /**************************************************************************/
void PDVFSUpdatePollingInterval(unsigned int *data)
{
	img_devfreq_proactive.polling_ms = *data;
}

/*************************************************************************/ /*!
@Function       RegisterPDVFSDevice

@Description    Initialise the device for Proactive DVFS support, stage 2.
                Create the 'devfreq' device with custom PVR firmware governor and
                set a default configuration.

@Input          psDeviceNode       Device node
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RegisterPDVFSDevice(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_PDVFS_DEVICE       *psPDVFSDevice = NULL;
	IMG_DVFS_DEVICE_CFG    *psDVFSDeviceCfg = NULL;
	RGX_TIMING_INFORMATION *psRGXTimingInfo = NULL;
	struct device          *psDev;
	struct pvr_opp_freq_table	pvr_freq_table = {0};
	unsigned long           min_freq = 0, max_freq = 0, min_volt = 0;
	PVRSRV_ERROR            eError;
	int                     err = 0;

	if (!psDeviceNode)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice.eState != PVR_DVFS_STATE_INIT_PENDING)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "Proactive DVFS initialise not yet pending for device node %p",
				 psDeviceNode));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	psDev = psDeviceNode->psDevConfig->pvOSDevice;
	psPDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	psRGXTimingInfo = ((RGX_DATA *)psDeviceNode->psDevConfig->hDevData)->psRGXTimingInfo;
	psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice.eState = PVR_DVFS_STATE_READY;

	err = GetOPPValues(psDev, &min_freq, &min_volt, &max_freq, &pvr_freq_table);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to read OPP points, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}

	img_devfreq_proactive.freq_table = pvr_freq_table.freq_table;
	img_devfreq_proactive.max_state = pvr_freq_table.num_levels;
	img_devfreq_proactive.initial_freq = min_freq;

	/* create the devfreq device */
	psPDVFSDevice->psDevFreq = devm_devfreq_add_device(psDev,
													   &img_devfreq_proactive,
													   DEVFREQ_GOV_PVR_CUSTOM,
													   NULL);

	if (IS_ERR(psPDVFSDevice->psDevFreq))
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "Failed to add as devfreq device %p, %ld",
				 psPDVFSDevice->psDevFreq,
				 PTR_ERR(psPDVFSDevice->psDevFreq)));
		eError = TO_IMG_ERR(PTR_ERR(psPDVFSDevice->psDevFreq));
		goto err_exit;
	}

#if defined(SUPPORT_PDVFS)
	if (psDVFSDeviceCfg->pfnDVFSRegister)
	{
		eError = psDVFSDeviceCfg->pfnDVFSRegister(psPDVFSDevice->psDevFreq);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to register DVFS callbacks"));
			goto err_exit;
		}
	}
	devfreq_get_freq_range(psPDVFSDevice->psDevFreq,
			       &psPDVFSDevice->ulMinFreq,
			       &psPDVFSDevice->ulMaxFreq);
#endif
	/*
	 * Register the devfreq userspace governor notification.
	 * Equivalent to entering 'set_freq_store' from userspace.
	 */
	psDVFSDeviceCfg->pfnNotifyCoreClkChange = NotifyCoreClkChange;

#if defined(SUPPORT_DVFS_RUNTIME_CONFIG)
	err = RegisterPDVFSParametersFile(psPDVFSDevice->psDevFreq);
	if (err)
	{
		eError = TO_IMG_ERR(err);
		goto err_exit_pdvfs_parameters;
	}
#if defined(SUPPORT_PDVFS_HEADROOM_EXT)
	err = RegisterHeadroomFile(psPDVFSDevice->psDevFreq);
	if (err)
	{
		eError = TO_IMG_ERR(err);
		goto err_exit_headroom;
	}
#endif
#endif

	err = devfreq_register_opp_notifier(psDev, psPDVFSDevice->psDevFreq);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to register opp notifier, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit_opp_notifier;
	}
#if defined(SUPPORT_PDVFS)
	if (!psPDVFSDevice)
	{
		PVR_DPF((PVR_DBG_ERROR, "IMG_PDVFS_DEVICE pointer is NULL"));
		goto err_exit_qos_notifier;
	}
#endif

	/* Register the min/max frequency notifiers */
	img_pm_qos_minfreq_notifier.dev = psDev;
	img_pm_qos_minfreq_notifier.devfreq_dev = psPDVFSDevice->psDevFreq;
	img_pm_qos_maxfreq_notifier.dev = psDev;
	img_pm_qos_maxfreq_notifier.devfreq_dev = psPDVFSDevice->psDevFreq;

	err = dev_pm_qos_add_notifier(psDev, &img_pm_qos_minfreq_notifier.nb, DEV_PM_QOS_MIN_FREQUENCY);
	if (err == 0)
	{
		err = dev_pm_qos_add_notifier(psDev, &img_pm_qos_maxfreq_notifier.nb, DEV_PM_QOS_MAX_FREQUENCY);
	}

	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to register pm_qos notifier, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit_qos_notifier;
	}

	dev_info(psDev, "%s: devfreq device registered.", __func__);
	return PVRSRV_OK;

err_exit_qos_notifier:
	(void) dev_pm_qos_remove_notifier(psDev, &img_pm_qos_maxfreq_notifier.nb,
				DEV_PM_QOS_MAX_FREQUENCY);
	(void) dev_pm_qos_remove_notifier(psDev, &img_pm_qos_minfreq_notifier.nb,
				DEV_PM_QOS_MIN_FREQUENCY);
	(void) devfreq_unregister_opp_notifier(psDev, psPDVFSDevice->psDevFreq);

err_exit_opp_notifier:
#if defined(SUPPORT_DVFS_RUNTIME_CONFIG) && defined(SUPPORT_PDVFS_HEADROOM_EXT)
	UnregisterHeadroomFile(psPDVFSDevice->psDevFreq);
	UnregisterPDVFSParametersFile(psPDVFSDevice->psDevFreq);

err_exit_headroom:
err_exit_pdvfs_parameters:
#endif
	devm_devfreq_remove_device(psDev, psPDVFSDevice->psDevFreq);
	psPDVFSDevice->psDevFreq = NULL;
err_exit:
	return eError;
}

/*************************************************************************/ /*!
@Function       UnregisterPDVFSDevice

@Description    De-Initialise the device for Proactive DVFS support, stage 2.
                Remove the custom 'devfreq' device.

@Input          psDeviceNode       Device node
@Return         None
*/ /**************************************************************************/
void UnregisterPDVFSDevice(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_PDVFS_DEVICE *psPDVFSDevice = NULL;
	IMG_DVFS_DEVICE_CFG *psDVFSDeviceCfg = NULL;
	struct device *psDev = NULL;
	IMG_INT32 iError;

	/* Check the device exists */
	if (!psDeviceNode)
	{
		return;
	}

	PVRSRV_VZ_RETN_IF_MODE(GUEST, DEVNODE, psDeviceNode);

	psPDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;
	psDev = psDeviceNode->psDevConfig->pvOSDevice;

	if (!psPDVFSDevice)
	{
		return;
	}

	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	if (psDVFSDeviceCfg->pfnDVFSUnregister)
	{
		psDVFSDeviceCfg->pfnDVFSUnregister(psPDVFSDevice->psDevFreq);
	}

	if (psPDVFSDevice->psDevFreq)
	{
		iError = dev_pm_qos_remove_notifier(psDev, &img_pm_qos_minfreq_notifier.nb,
				DEV_PM_QOS_MIN_FREQUENCY);
		if (iError < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to unregister pm_qos min_freq notifier"));
		}
		iError = dev_pm_qos_remove_notifier(psDev, &img_pm_qos_maxfreq_notifier.nb,
				DEV_PM_QOS_MAX_FREQUENCY);
		if (iError < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to unregister pm_qos max_freq notifier"));
		}
		iError = devfreq_unregister_opp_notifier(psDev, psPDVFSDevice->psDevFreq);
		if (iError < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to unregister OPP notifier"));
		}

#if defined(SUPPORT_DVFS_RUNTIME_CONFIG)
		UnregisterPDVFSParametersFile(psPDVFSDevice->psDevFreq);
#if defined(SUPPORT_PDVFS_HEADROOM_EXT)
		UnregisterHeadroomFile(psPDVFSDevice->psDevFreq);
#endif
#endif
		devm_devfreq_remove_device(psDev, psPDVFSDevice->psDevFreq);
		psPDVFSDevice->psDevFreq = NULL;
	}

	psPDVFSDevice->eState = PVR_DVFS_STATE_DEINIT;
}

#endif /* SUPPORT_PDVFS_DEVFREQ */
/*************************************************************************/ /*!
@Function       ResumePDVFS

@Description    Restore firmware state controlled by the DVFS governor after power on.

@Input          psDeviceNode       Device node
*/ /**************************************************************************/
PVRSRV_ERROR ResumePDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_PDVFS_DEVICE *psPDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sPDVFSDevice;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	struct device *psDev = psDeviceNode->psDevConfig->pvOSDevice;
	PVRSRV_ERROR eError;
	PVRSRV_DEV_POWER_STATE ePowerState;
	int level;
	int err;

	eError = PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState);
	if (eError != PVRSRV_OK)
	{
		dev_warn(psDev, "Device power state unknown in PDVFS startup, error %d", eError);
		return eError;
	}
	err = FindOPPFreq(psDev,
							img_devfreq_proactive.freq_table,
							psPDVFSDevice->ulMinFreq,
							&level);
	PDVFSLimitMinFrequencyLocked(psDevInfo, level);
	err = FindOPPFreq(psDev,
							img_devfreq_proactive.freq_table,
							psPDVFSDevice->ulMaxFreq,
							&level);
	PDVFSLimitMaxFrequencyLocked(psDevInfo, level);

	return PVRSRV_OK;
}
#endif /* SUPPORT_PDVFS */

#endif /* !NO_HARDWARE */
