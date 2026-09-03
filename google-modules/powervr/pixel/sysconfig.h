// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <linux/device.h>
#include <linux/clk.h>

#if defined(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif

#include <linux/miscdevice.h>

#include <pvrsrvkm/services/server/include/pvrsrv_device.h>
#include <pvrsrvkm/services/shared/include/devicemem_utils.h>

#include "pixel_fw_info_shared.h"
#include "debug.h"
#include "slc.h"
#include "of.h"
#include "gpu_uevent.h"

#define FW_ABI_VERSION 40
#define PIXEL_PA_RANGE_16K_PAGE_ABI_VERSION 15

struct pixel_gpu_secure;

enum pixel_gpu_power_state {
	PIXEL_GPU_POWER_STATE_OFF,
	PIXEL_GPU_POWER_STATE_PG,
	PIXEL_GPU_POWER_STATE_ON,
	PIXEL_GPU_POWER_STATE_COUNT, // also used as an "unknown power state" sentinel
};

struct pixel_gpu_power_state_stats {
	uint64_t entry_count;
	ktime_t cumulative_time_ns;
	ktime_t last_entry_ns;
	ktime_t last_exit_ns;
};

/*
 * Number of elements aligns with the number of columns for pf_state_rates under
 * gpu_pf_state device in the device tree
 */
enum GPU_PF_STATE_ELEMS {
	FAB_FABHBW_VOTE,
	FAB_MEMSS_VOTE,
	FAB_GMC_VOTE,
	FAB_TOTAL_VOTES,
};

struct pixel_gpu_device {
	struct device *dev;
	struct device *gpu_core_logic_pd;
	struct device *sswrp_gpu_pd;
	struct device_link *core_logic_link;
#if defined(SUPPORT_LINUX_DVFS)
	struct device_link *pf_state_link;
#endif
	struct notifier_block core_logic_notifier;
	struct notifier_block sswrp_notifier;
	bool notifiers_registered;

	struct clk *gpu_clk;
#if defined(CONFIG_DEVFREQ_THERMAL)
	struct devfreq_cooling_power gpu_power_ops;
#endif
	struct miscdevice mdev;
	PVRSRV_DEVICE_CONFIG *dev_config;
	/* Represents the physical memory mapping, used to map registers for FW access */
	DEVMEM_IMPORT* gpu_phys_mem_import;

	struct {
		struct mutex lock;
		int cur_state;
		struct pixel_gpu_power_state_stats stats[PIXEL_GPU_POWER_STATE_COUNT];
	} power_state;

#if defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD)
	struct uid_tis_data {
		unsigned int num_opp_frequencies;
		unsigned long *opp_frequencies;
		struct mutex lock;
		struct list_head head;
	} time_in_state;
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) */

	struct pixel_gpu_debug_info debug;

	struct pixel_of_properties of_properties;
	struct pixel_of_pdevs of_pdevs;

	u32 **fab_votes;
	u32 fab_votes_opp_count;

	struct slc_data slc_data;
#if defined(SUPPORT_TRUSTED_DEVICE)
	/* Used for IPC with gpu_secure trusty app */
	struct pixel_gpu_secure *gpu_secure;
#endif

	struct {
		/** @glue_csr: Mapping of MBA glue CSRs */
		u8 __iomem *glue_csr;

		/** @client_csr: Mapping of MBA client CSRs */
		u8 __iomem *client_csr;

		/** @n_clients: Number of client mailboxes */
		u32 n_clients;
	} mba;

	struct iif_manager *iif_mgr;

	struct pixel_fw_info fw_footer;

	struct gpu_uevent_ctx gpu_uevent_ctx;

	u32 scheduling_max_deferral;
};

unsigned int get_time_multiplier(void);

void set_time_multiplier(unsigned int multiplier);

/**
 * device_to_pixel() - get Pixel device from main GPU device
 * @dev: GPU device
 */
struct pixel_gpu_device *device_to_pixel(struct device *dev);

const char *rgx_context_reset_reason_str(RGX_CONTEXT_RESET_REASON reason);

const char *rgxfwif_dm_str(RGXFWIF_DM dm);
