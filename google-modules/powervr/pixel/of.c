// SPDX-License-Identifier: GPL-2.0

#include "of.h"
#include "sysconfig.h"

#include <pvrsrvkm/services/server/devices/volcanic/rgxdevice.h>

#include <linux/of_address.h>
#include <linux/of_platform.h>

#if defined(CONFIG_POWERVR_DISABLE_APM)
#define disable_apm (true)
#else
#define disable_apm (false)
#endif

static struct _of_phandles {
	struct device_node *gpu_core_logic_pd_node;
#if defined(SUPPORT_TRUSTED_DEVICE)
	struct device_node *gpu_memory_region_node;
#endif
} of_phandles;

static uint32_t read_u32_property_or_default(struct device *dev,
					     uint32_t default_value,
					     const char *path[],
					     size_t path_elements)
{
	uint32_t value = default_value;
	int last_element = path_elements - 1;
	struct device_node *node = dev->of_node;
	const char *property_name = path[last_element];
	int return_code;
	struct device_node *final_node;

	for (int index = 0; index < last_element; ++index) {
		struct device_node *last = node;
		node = of_get_child_by_name(last, path[index]);

		if (node == NULL) {
			PVR_DPF((PVR_DBG_CUSTOMER,
				"%s: unable to follow path to DT property %s "
				"(failed to find child '%s' on node '%s')",
				__func__, property_name, path[index],
				last->name));
			final_node = last;
			goto end;
		}

		if (last != dev->of_node) {
			of_node_put(last);
		}

	}

	return_code =
		of_property_read_u32_index(node, property_name, 0, &value);
	final_node = node;

	switch (return_code) {
	case 0:
		break;
	case -EINVAL:
		PVR_DPF((PVR_DBG_CUSTOMER, "%s: DT property %s does not exist",
			__func__, property_name));
		break;
	case -ENODATA:
		PVR_DPF((PVR_DBG_CUSTOMER, "%s: DT property %s does not have a value",
			__func__, property_name));
		break;
	case -EOVERFLOW:
		PVR_DPF((PVR_DBG_CUSTOMER, "%s: DT property %s has data that isn't large enough",
			__func__, property_name));
		break;
	default:
		PVR_DPF((PVR_DBG_CUSTOMER, "%s: DT property %s has unknown return code %d",
			__func__, property_name, return_code));
		break;
	}

end:
	if (final_node != dev->of_node) {
		of_node_put(final_node);
	}

	return value;
}

#define READ_U32_PROPERTY_OR_DEFAULT(DEV, DEFAULT_VALUE, ...) \
	(read_u32_property_or_default(DEV, DEFAULT_VALUE, (const char *[]){ __VA_ARGS__ }, \
				      sizeof((const char *[]){ __VA_ARGS__ }) / \
				      sizeof(const char *)))

#if defined(SUPPORT_TRUSTED_DEVICE)
int fill_carveout_resource(struct resource *carveout)
{
	/* Get the resource associated with the OF node */
	int rc = of_address_to_resource(of_phandles.gpu_memory_region_node, 0, carveout);

	of_node_put(of_phandles.gpu_memory_region_node);
	of_phandles.gpu_memory_region_node = NULL;

	return rc;
}
#endif

#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
static int init_gpu_pf_state_nodes(struct pixel_gpu_device *pixel_dev)
{
	struct device *dev = pixel_dev->dev;
	struct pixel_of_pdevs *of_pdevs = &pixel_dev->of_pdevs;
	struct device_node *gpu_pf_state_node;
	u32 *fab_votes_staging = NULL;
	int i;

	gpu_pf_state_node = of_parse_phandle(dev->of_node, "clocks", 0);
	if (!gpu_pf_state_node) {
		dev_err(dev, "failed to find clocks node");
		goto exit_error;
	}

	of_pdevs->gpu_pf_state_pdev = of_find_device_by_node(gpu_pf_state_node);
	if (!of_pdevs->gpu_pf_state_pdev) {
		dev_err(dev, "failed to find gpu_pf_state device");
		goto exit_error;
	}
	of_node_put(gpu_pf_state_node);
	gpu_pf_state_node = NULL;

	/* Read fabric votes from device tree */
	struct device *pf_state_dev = &of_pdevs->gpu_pf_state_pdev->dev;
	int count;
	int opp_count;

	count = of_property_count_elems_of_size(pf_state_dev->of_node,
		"pf_state_rates", sizeof(u32));
	if (count < 0) {
		dev_err(dev, "pf_state_rates not found in device tree\n");
		goto exit_error;
	}

	if (count % FAB_TOTAL_VOTES != 0) {
		dev_err(dev, "Incorrect number of fabric votes in device tree\n");
		goto exit_error;
	}

	opp_count = count / FAB_TOTAL_VOTES;

	fab_votes_staging = kcalloc(count, sizeof(u32), GFP_KERNEL);
	if (!fab_votes_staging)
		goto exit_error;

	if (of_property_read_u32_array(pf_state_dev->of_node,
		"pf_state_rates", fab_votes_staging, count)) {
		dev_err(dev, "failed to read pf_state_rates\n");
		goto exit_error_free;
	}

	pixel_dev->fab_votes = kcalloc(opp_count, sizeof(u32 *), GFP_KERNEL);
	if (!pixel_dev->fab_votes)
		goto exit_error_free;

	for (i = 0; i < opp_count; i++) {
		u32 fabhbw_vote = fab_votes_staging[i * FAB_TOTAL_VOTES + FAB_FABHBW_VOTE];
		u32 memss_vote = fab_votes_staging[i * FAB_TOTAL_VOTES + FAB_MEMSS_VOTE];
		u32 gmc_vote = fab_votes_staging[i * FAB_TOTAL_VOTES + FAB_GMC_VOTE];

		if (fabhbw_vote > 0xf || memss_vote > 0xf || gmc_vote > 0xf) {
			dev_err(dev, "Fabric vote exceeds protocol limit (0xf) at OPP %d: FABHBW=%u, MEMSS=%u, GMC=%u\n",
					i, fabhbw_vote, memss_vote, gmc_vote);
			goto exit_error_free_2d;
		}

		pixel_dev->fab_votes[i] = kcalloc(FAB_TOTAL_VOTES, sizeof(u32), GFP_KERNEL);
		if (!pixel_dev->fab_votes[i])
			goto exit_error_free_2d;

		pixel_dev->fab_votes[i][FAB_FABHBW_VOTE] = fabhbw_vote;
		pixel_dev->fab_votes[i][FAB_MEMSS_VOTE] = memss_vote;
		pixel_dev->fab_votes[i][FAB_GMC_VOTE] = gmc_vote;
	}

	pixel_dev->fab_votes_opp_count = opp_count;

	kfree(fab_votes_staging);
	return 0;

exit_error_free_2d:
	while (--i >= 0)
		kfree(pixel_dev->fab_votes[i]);
	kfree(pixel_dev->fab_votes);
exit_error_free:
	kfree(fab_votes_staging);
exit_error:
	if (gpu_pf_state_node)
		of_node_put(gpu_pf_state_node);
	return -1;
}
#endif

int init_pixel_of(struct pixel_gpu_device *pixel_dev)
{
	struct device *dev = pixel_dev->dev;
	struct pixel_of_properties *of_properties = &pixel_dev->of_properties;
	RGX_TIMING_INFORMATION *timing_info =
		((RGX_DATA *)pixel_dev->dev_config->hDevData)->psRGXTimingInfo;

#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
	if (init_gpu_pf_state_nodes(pixel_dev)) {
		dev_err(dev, "failed to init gpu_pf_state node");
		goto exit_error;
	}
#endif

	of_phandles.gpu_core_logic_pd_node =
		of_parse_phandle(dev->of_node, "power-domains",
				 PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD);
	if (!of_phandles.gpu_core_logic_pd_node) {
		dev_err(dev, "failed to find core logic node");
		goto exit_error;
	}

#if defined(SUPPORT_TRUSTED_DEVICE)
	of_phandles.gpu_memory_region_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!of_phandles.gpu_memory_region_node) {
		dev_err(dev, "failed to find memory-region node");
		goto exit_error;
	}
#endif

	// Obtain OF config
	of_properties->spu_gating =
		READ_U32_PROPERTY_OR_DEFAULT(dev, true, "config", "power",
					     "enable-firmware-managed-spu-power");

	if (disable_apm) {
		PVR_DPF((PVR_DBG_CUSTOMER, "APM disabled, overriding device tree"));
		of_properties->apm = false;
	} else {
		of_properties->apm =
			READ_U32_PROPERTY_OR_DEFAULT(dev, false, "config", "power",
						     "enable-active-power-management");
	}

	of_properties->pre_silicon = of_property_read_bool(dev->of_node, "pre-silicon");

	of_properties->emulator = of_property_read_bool(dev->of_node, "emulator");

	of_properties->virtual_platform = of_property_read_bool(dev->of_node, "virtual-platform");

	of_properties->jones_force_on = of_property_read_bool(of_phandles.gpu_core_logic_pd_node,
							      "force-on");

	of_properties->apm_latency =
		READ_U32_PROPERTY_OR_DEFAULT(dev, 33, "config", "power",
					     "active-power-management-latency-ms");

	of_properties->autosuspend_latency =
		READ_U32_PROPERTY_OR_DEFAULT(dev, 18, "config", "power",
					     "sswrp-autosuspend-latency-ms");

	of_properties->pdvfs_com_host = READ_U32_PROPERTY_OR_DEFAULT(dev, false,
					     "config", "dvfs", "pdvfs-com-host");

#if defined(SUPPORT_PDVFS)
	pixel_dev->dev_config->bPDVFSComHost = of_properties->pdvfs_com_host;
#endif

	// Set any global state based on OF
	if (of_properties->pre_silicon)
		set_time_multiplier(2500);

	timing_info->bEnableRDPowIsland    = of_properties->spu_gating;
	timing_info->bEnableActivePM       = of_properties->apm;
	timing_info->ui32ActivePMLatencyms = of_properties->apm_latency;

#if defined(RUNTIME_CHECK_EMULATOR_PLATFORM)
	pixel_dev->dev_config->bIsEmulatorPlatform = of_properties->emulator;
#endif
#if defined(RUNTIME_CHECK_VIRTUAL_PLATFORM)
	pixel_dev->dev_config->bIsVirtualPlatform = of_properties->virtual_platform;
#endif

	// Perform checks based on OF
	/*
		Jones should not be forced on in silicon under normal circumstances.
		Jones should not be forced on when APM is enabled - there will be no power saving.
	*/
	WARN_ON(of_properties->jones_force_on &&
		(!of_properties->pre_silicon || of_properties->apm));

	// Warn when SPU gating is disabled - this should never be turned off
	WARN_ON(!of_properties->spu_gating);

	// emulator or virtual_platform should only be set for pre_silicon
	WARN_ON(!of_properties->pre_silicon &&
		(of_properties->emulator || of_properties->virtual_platform));

	// emulator and virtual_platform should not both be set
	WARN_ON(of_properties->emulator && of_properties->virtual_platform);

	// Print OF config
	PVR_DPF((PVR_DBG_CUSTOMER, "DT configuration"));
	PVR_DPF((PVR_DBG_CUSTOMER, "Pre-Silicon Configuration: %d", of_properties->pre_silicon));
	PVR_DPF((PVR_DBG_CUSTOMER, "Emulator Configuration: %d", of_properties->emulator));
	PVR_DPF((PVR_DBG_CUSTOMER, "Virtual Platform Configuration: %d",
			of_properties->virtual_platform));
	PVR_DPF((PVR_DBG_CUSTOMER, "GPU Power Forced On: %d", of_properties->jones_force_on));
	PVR_DPF((PVR_DBG_CUSTOMER, "SPU Power Gating: %d", of_properties->spu_gating));
	PVR_DPF((PVR_DBG_CUSTOMER, "Active Power Management (APM): %d", of_properties->apm));
	PVR_DPF((PVR_DBG_CUSTOMER, "APM Latency: %u", of_properties->apm_latency));
	PVR_DPF((PVR_DBG_CUSTOMER, "Autosuspend Latency: %u", of_properties->autosuspend_latency));

	return 0;

exit_error:
	deinit_pixel_of(pixel_dev);
	return -1;
}

void deinit_pixel_of(struct pixel_gpu_device *pixel_dev)
{
#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
	int i;

	if (pixel_dev->fab_votes) {
		for (i = 0; i < pixel_dev->fab_votes_opp_count; i++)
			kfree(pixel_dev->fab_votes[i]);
		kfree(pixel_dev->fab_votes);
	}

	if (pixel_dev->of_pdevs.gpu_pf_state_pdev)
		put_device(&pixel_dev->of_pdevs.gpu_pf_state_pdev->dev);
#endif
#if defined(SUPPORT_TRUSTED_DEVICE)
	if (of_phandles.gpu_memory_region_node)
		of_node_put(of_phandles.gpu_memory_region_node);
#endif
	if (of_phandles.gpu_core_logic_pd_node)
		of_node_put(of_phandles.gpu_core_logic_pd_node);
}
