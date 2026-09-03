// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/vmalloc.h>

#include <soc/google/google_auth_image_format.h>
#include <soc/google/google_dpa_ctrl.h>

#include "google_dpa_boot.h"
#include "google_dpa_crash_dump_internal.h"
#include "google_dpa_ctrl_internal.h"
#include "google_dpa_elf_loader.h"
#include "google_dpa_internal.h"
#include "google_dpa_log_proxy.h"
#include "google_dpa_rpc_internal.h"
#include "google_dpa_secure.h"
#include "google_dpa_tea_proxy.h"
#define CREATE_TRACE_POINTS
#include "trace/events/google_dpa.h"
#include "google_dpa_ring_service_proxy_internel.h"
#include "services/google_dpa_services.h"

#define GOOGLE_DPA_ELF_RESOURCE_TABLE_SECTION_NAME ".resource_table"
/*
 * - The first version of firmware format is IMAGE_CONFIG_V1 and BODY_FORMAT_V1
 *   The NCP and NEP firmware files are separated in the first version
 * - The second version of firmware format is IMAGE_CONFIG_V2 and BODY_FORMAT_V1
 *   The NCP and NEP firmware is merged into a single file in the second version
 *
 * The image config version specifies the structure of the image config region
 * in the auth image.
 * The image config v1 does not have any info in the image config.
 * The image config v2 has info about the offset and size of NCP/NEP firmware in
 * the image body.
 *
 * The image body format version specifies the firmware image format in the
 * image body. V1 format is an ELF file.
 */
#define GOOGLE_DPA_IMAGE_CONFIG_V1 0
#define GOOGLE_DPA_IMAGE_CONFIG_V2 2
#define GOOGLE_DPA_IMAGE_BODY_FORMAT_V1 0

#define GOOGLE_DPA_POWER_OFF_TIMEOUT_MS 1000

#define GOOGLE_DPA_LPM_PSM_STATUS_MASK GENMASK(4, 0)
#define GOOGLE_DPA_LPM_WAIT_COMPLETED_PS_VALUE 0x0
#define GOOGLE_DPA_LPM_VALID BIT_MASK(4)
#define GOOGLE_DPA_LPM_POLL_INTERVAL_US 1000
#define GOOGLE_DPA_LPM_POLL_TIMEOUT_US (10 * 1000)

#define GOOGLE_DPA_SHARED_INFO_MAGIC 0xB002C0DE
#define GOOGLE_DPA_SHARED_BOOT_INFO_NCP_START_NEP 0xbbdaabb1
#define GOOGLE_DPA_SHARED_BOOT_INTO_NEP_BOOT_COMPLETION 0xbeefcc01
#define GOOGLE_DPA_BOOT_COMPLETION_POLL_INTERVAL_US (10000) // 10ms

#define GOOGLE_DPA_RPC_READY_TIMEOUT_MS 500
#define GOOGLE_DPA_RPC_READY_POLL_INTERVAL_MS 10

#define GOOGLE_DPA_CPM_IOVA 0x0E800000
#define GOOGLE_DPA_CPM_PHYS_ADDR 0x0E800000
#define GOOGLE_DPA_CPM_SIZE 0x3F4000 // minimum region for CSRs
static_assert(PAGE_ALIGNED(GOOGLE_DPA_CPM_SIZE), "GOOGLE_DPA_CPM_SIZE must be aligned to pages");

#define GOOGLE_DPA_AOSS_AON_GIA_IOVA 0x0E600000
#define GOOGLE_DPA_AOSS_AON_GIA_PHYS_ADDR 0x0E600000
#define GOOGLE_DPA_AOSS_AON_GIA_SIZE 0x00020000

struct google_dpa_shared_boot_info {
	u32 ncp_msg;
	u32 nep_msg;
} __packed;

struct google_dpa_image_config {
	u32 config_version;
	u32 image_format_version;
	u32 ncp_image_offset;
	u32 ncp_image_length;
	u32 nep_image_offset;
	u32 nep_image_length;
} __packed;

static const struct google_dpa_image_config *
get_image_config(const struct google_auth_image_header *auth_header)
{
	u32 generation = auth_header->header_v2.generation;

	switch (generation) {
	case 2:
		return (const struct google_dpa_image_config *)auth_header->header_v2.image_config;
	default:
		return NULL;
	}
}

static inline int google_dpa_poll_psm(struct google_dpa *dpa, u32 ps_value)
{
	int ret;
	u32 val;
	u32 expected = ps_value | GOOGLE_DPA_LPM_VALID;

	ret = readl_poll_timeout(dpa->lpm_base + dpa->lpm_psm_status_offset, val,
				 ((val & GOOGLE_DPA_LPM_PSM_STATUS_MASK) == expected),
				 GOOGLE_DPA_LPM_POLL_INTERVAL_US, GOOGLE_DPA_LPM_POLL_TIMEOUT_US);

	return ret;
}

static inline int google_dpa_release_from_wait_state(struct google_dpa *dpa)
{
	int ret;

	writel(0x1, dpa->lpm_base + dpa->lpm_ncp_wait_complete_offset);

	trace_google_dpa_boot_release(dpa);

	ret = google_dpa_poll_psm(dpa, GOOGLE_DPA_LPM_WAIT_COMPLETED_PS_VALUE);
	writel(0x0, dpa->lpm_base + dpa->lpm_ncp_wait_complete_offset);

	return ret;
}

struct google_dpa_shared_info __iomem *google_dpa_get_shared_info(struct google_dpa *dpa)
{
	bool is_iomem;

	return google_dpa_da_to_va_internal(dpa, &dpa->ncp, dpa->shared_info_device_address,
					    sizeof(struct google_dpa_shared_info), &is_iomem);
}

int google_dpa_get_shared_ring_info_device_addr(struct google_dpa *dpa, u32 *out_device_addr)
{
	struct google_dpa_shared_info *shared_info;

	shared_info = google_dpa_get_shared_info(dpa);
	if (!shared_info)
		return -EFAULT;

	*out_device_addr = readl(&shared_info->ring_info_addr);
	return 0;
}
EXPORT_SYMBOL_GPL(google_dpa_get_shared_ring_info_device_addr);

static inline int google_dpa_wait_for_boot_completion(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct google_dpa_shared_info __iomem *shared_info_addr;
	struct google_dpa_shared_boot_info __iomem *boot_info_addr;
	u32 boot_info_device_addr;
	u32 shared_info_magic;
	bool is_iomem;
	u32 val;
	int ret;

	shared_info_addr = google_dpa_get_shared_info(dpa);
	if (!shared_info_addr) {
		dev_err(dev, "Failed to get shared info address.\n");
		return -EINVAL;
	}

	shared_info_magic = readl(&shared_info_addr->magic);
	if (shared_info_magic != GOOGLE_DPA_SHARED_INFO_MAGIC) {
		dev_err(dev, "Invalid magic value of shared info.\n");
		return -EINVAL;
	}

	boot_info_device_addr = readl(&shared_info_addr->boot_info_addr);
	boot_info_addr = google_dpa_da_to_va_internal(dpa, &dpa->ncp, boot_info_device_addr,
						      sizeof(struct google_dpa_shared_boot_info),
						      &is_iomem);
	if (!boot_info_addr) {
		dev_err(dev,
			"Failed to translate device address of boot_info into virtual address.\n");
		return -EINVAL;
	}

	ret = readl_poll_timeout(&boot_info_addr->ncp_msg, val,
				 (val == GOOGLE_DPA_SHARED_BOOT_INFO_NCP_START_NEP),
				 GOOGLE_DPA_BOOT_COMPLETION_POLL_INTERVAL_US,
				 dpa->fw_boot_completion_timeout_us);
	if (ret) {
		dev_err(dev, "Failed to get NCP boot completion message.\n");
		return ret;
	}

	ret = readl_poll_timeout(&boot_info_addr->nep_msg, val,
				 (val == GOOGLE_DPA_SHARED_BOOT_INTO_NEP_BOOT_COMPLETION),
				 GOOGLE_DPA_BOOT_COMPLETION_POLL_INTERVAL_US,
				 dpa->fw_boot_completion_timeout_us);
	if (ret) {
		dev_err(dev, "Failed to get NEP boot completion message.\n");
		return ret;
	}

	trace_google_dpa_boot_complete(dpa);

	return 0;
}

int google_dpa_power_domain_notify_callback(struct notifier_block *nb, unsigned long action,
					    void *data)
{
	struct google_dpa *dpa = container_of(nb, struct google_dpa, power_domain_notify);

	switch (action) {
	case GENPD_NOTIFY_OFF:
		complete(&dpa->power_off_completion);
		return 0;
	default:
		return 0;
	}
}

/*
 * Calls pm_runtime_put and wait for the completion of power-off.
 *
 * DPA driver have to wait for the power off before rebooting DPA.
 * pm_runtime_put_sync() is not enough because it does not ensure that the
 * power-off is performed. DPA driver waits for the power-off by using genpd's
 * notifier function.
 **/
static inline int pm_runtime_put_and_power_off_sync(struct google_dpa *dpa, bool is_graceful)
{
	struct device *dev = dpa->dev;
	int ret;
	unsigned long time_left;

	reinit_completion(&dpa->power_off_completion);

	if (!is_graceful) {
		/*
		 * Request force-poweroff. The normal power off means DPA block will be
		 * eventually powered off. DPA firmware decides when to go to the low
		 * power state. The synced power off forcefully power off the DPA block.
		 **/
		dev_pm_genpd_synced_poweroff(dpa->dev);
		if (dpa->pd_vdev)
			dev_pm_genpd_synced_poweroff(dpa->pd_vdev);
	} else {
		/*
		* Request poweroff, DPA starts the shutdown process after sending ACK to AP.
		**/
		ret = dpa_rpc_power_service_poweroff(dpa->dev, google_dpa_rpc_ncp_client());
		if (ret < 0) {
			dev_err(dev, "dpa_rpc_power_service_poweroff to ncp failed with ret=%d\n",
				ret);
			return ret;
		}
		ret = dpa_rpc_power_service_poweroff(dpa->dev, google_dpa_rpc_nep_client());
		if (ret < 0) {
			dev_err(dev, "dpa_rpc_power_service_poweroff to nep failed with ret=%d\n",
				ret);
			return ret;
		}
		/**
		 * De-init RPC serices provided to DPA, to avoid the faulting sequence
		 * 1. AP sends a power-off request to DPA
		 * 2. DPA sends ACK to AP
		 * 3. AP removes the vote
		 * 4. DPA sends log to AP via RPC
		 * 5. AP receives IRQ but it is delayed somehow
		 * 6. DPA performs the actual power-off sequence and DPA block is turned off
		 * 7. AP processes IRQ and try to access DPA SRAM but it is turned off
		*/
		if (dpa->fw_state == GOOGLE_DPA_FW_RUNNING) {
			google_dpa_log_proxy_cancel(dpa);
			google_dpa_rpc_deinit(dpa);
		}
	}

	ret = pm_runtime_put_sync_suspend(dev);

	if (ret < 0) {
		dev_err(dev, "pm_runtime_put failed with ret=%d\n", ret);
		return ret;
	}

	// for graceful shutdown, DPA could crash along the way
	dev_info(dev, "DPA power-off requested.\n");

	time_left = wait_for_completion_timeout(&dpa->power_off_completion,
						msecs_to_jiffies(GOOGLE_DPA_POWER_OFF_TIMEOUT_MS));

	if (time_left == 0) {
		dev_err(dev, "DPA power-off timed out.\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static const struct google_dpa_image_config *
google_dpa_parse_auth_image(struct google_dpa *dpa, const struct firmware *fw,
			    struct firmware *out_image_body)
{
	struct device *dev = dpa->dev;
	const struct google_auth_image_header *auth_header;
	const struct google_dpa_image_config *image_config;

	if (fw->size < GOOGLE_AUTH_IMAGE_FORMAT_HEADER_SIZE + sizeof(*image_config))
		return ERR_PTR(-EINVAL);

	auth_header = (const struct google_auth_image_header *)fw->data;
	image_config = get_image_config(auth_header);
	if (!image_config) {
		dev_err(dev, "Failed to get image config.\n");
		return ERR_PTR(-EINVAL);
	}

	out_image_body->data = fw->data + GOOGLE_AUTH_IMAGE_FORMAT_HEADER_SIZE;
	out_image_body->size = fw->size - GOOGLE_AUTH_IMAGE_FORMAT_HEADER_SIZE;

	return image_config;
}

static int google_dpa_parse_elf(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				const struct firmware *elf_fw)
{
	struct device *dev = dpa->dev;
	int ret;

	ret = google_dpa_elf_sanity_check(dpa, mcu, elf_fw);
	if (ret != 0) {
		dev_err(dev, "Invalid ELF image.\n");
		return ret;
	}

	ret = google_dpa_elf_load_rsc_table(dpa, mcu, elf_fw);
	if (ret != 0) {
		dev_err(dev, "Failed to load a resource table from an ELF file.\n");
		return ret;
	}

	ret = google_dpa_handle_resources(dpa, mcu);
	if (ret != 0) {
		dev_err(dev, "Failed to process resources in NCP ELF file.\n");
		goto release_rsc_table;
	}

	return 0;

release_rsc_table:
	google_dpa_release_resource_table(mcu);

	return ret;
}

struct iommu_mapping {
	struct list_head list;
	phys_addr_t base;
	size_t size;
	unsigned long iova;
	int prot;
};

static void google_dpa_cleanup_iommu_mappings(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct iommu_domain *domain;
	size_t unmapped;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain || domain->type == IOMMU_DOMAIN_IDENTITY)
		return;

	struct iommu_mapping *mapping, *next;

	dev_dbg(dev, "Cleaning up static iommu mappings\n");
	list_for_each_entry_safe(mapping, next, &dpa->iommu_mappings, list) {
		unmapped = iommu_unmap(domain, mapping->iova, mapping->size);
		if (unlikely(unmapped != mapping->size))
			dev_warn(dev, "Unmapping IOVA %pad size (%#zx) only unmapped %#zx]\n",
				 &mapping->iova, mapping->size, unmapped);
		list_del(&mapping->list);
		kfree(mapping);
	}
}

static int google_dpa_configure_iommu_mappings(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct iommu_domain *domain;
	struct iommu_mapping *mapping;
	int ret = 0;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain || domain->type == IOMMU_DOMAIN_IDENTITY)
		return -EINVAL;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	dev_dbg(dev, "Creating static iommu mappings\n");
	/*
	 * TODO(397126501): Configure iommu mappings in the firmware image instead of
	 * hardcoding the mappings.
	 */
	ret = iommu_map(domain, GOOGLE_DPA_CPM_IOVA, GOOGLE_DPA_CPM_PHYS_ADDR, GOOGLE_DPA_CPM_SIZE,
			IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_READ | IOMMU_MMIO, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "Could not map CPM address space\n");
		kfree(mapping);
		return -EINVAL;
	}

	mapping->base = GOOGLE_DPA_CPM_PHYS_ADDR;
	mapping->size = GOOGLE_DPA_CPM_SIZE;
	mapping->iova = GOOGLE_DPA_CPM_IOVA;
	mapping->prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_READ | IOMMU_MMIO;
	list_add(&mapping->list, &dpa->iommu_mappings);

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		google_dpa_cleanup_iommu_mappings(dpa);
		return -ENOMEM;
	}

	ret = iommu_map(domain, GOOGLE_DPA_AOSS_AON_GIA_IOVA, GOOGLE_DPA_AOSS_AON_GIA_PHYS_ADDR, GOOGLE_DPA_AOSS_AON_GIA_SIZE,
			IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_READ | IOMMU_MMIO, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "Could not map AOSS AON address space\n");
		google_dpa_cleanup_iommu_mappings(dpa);
		kfree(mapping);
		return -EINVAL;
	}

	mapping->base = GOOGLE_DPA_AOSS_AON_GIA_PHYS_ADDR;
	mapping->size = GOOGLE_DPA_AOSS_AON_GIA_SIZE;
	mapping->iova = GOOGLE_DPA_AOSS_AON_GIA_IOVA;
	mapping->prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_READ | IOMMU_MMIO;
	list_add(&mapping->list, &dpa->iommu_mappings);

	int prot = IOMMU_WRITE | IOMMU_READ;
	int mem_count;
	struct device_node *rmem_np;
	struct reserved_mem *rmem;
	bool en;
	unsigned long i;

	mem_count = of_count_phandle_with_args(dev->of_node, "memory-region", NULL);
	if (mem_count <= 0)
		return 0;

	for (i = 0; i < mem_count; i++) {
		rmem_np = of_parse_phandle(dev->of_node, "memory-region", i);
		if (!rmem_np)
			continue;

		en = of_device_is_available(rmem_np);
		if (!en)
			continue;

		rmem = of_reserved_mem_lookup(rmem_np);
		if (!rmem || !rmem->base || !rmem->size)
			continue;

		const __be32 *maps;
		int prop_size;

		maps = of_get_property(rmem_np, "iommu-addresses", &prop_size);
		if (!maps || prop_size == 0)
			continue;

		mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
		if (!mapping) {
			google_dpa_cleanup_iommu_mappings(dpa);
			return -ENOMEM;
		}

		struct device_node *np;
		u32 phandle;
		phys_addr_t iova;
		size_t length;

		phandle = be32_to_cpup(maps++);
		np = of_find_node_by_phandle(phandle);
		maps = of_translate_dma_region(np, maps, &iova, &length);
		if (length == 0 || length != rmem->size)
			continue;

		dev_info(dev, "IOMMU Mapping: 0x%llx -> 0x%llx (0x%llx)\n", rmem->base, iova,
			 rmem->size);
		ret = iommu_map(domain, iova, rmem->base, rmem->size, prot, GFP_KERNEL);
		if (ret) {
			dev_err(dev, "Cannot create IOMMU map for 0x%llx\n", iova);
			kfree(mapping);
			google_dpa_cleanup_iommu_mappings(dpa);
			return -ENOMEM;
		}

		mapping->base = rmem->base;
		mapping->size = rmem->size;
		mapping->iova = iova;
		mapping->prot = prot;
		list_add(&mapping->list, &dpa->iommu_mappings);
	}
	return 0;
}

static int google_dpa_ns_load_elf_and_release_from_wait_state(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct firmware ncp_elf = {
		.data = dpa->ncp.fw_image.elf_data,
		.size = dpa->ncp.fw_image.elf_size,
	};
	struct firmware nep_elf = {
		.data = dpa->nep.fw_image.elf_data,
		.size = dpa->nep.fw_image.elf_size,
	};
	int ret;

	ret = google_dpa_elf_load_segments(dpa, &dpa->ncp, &ncp_elf);
	if (ret != 0) {
		dev_err(dev, "Failed to load NCP firmware to NCP's TCM/SRAM\n");
		return ret;
	}
	ret = google_dpa_elf_load_segments(dpa, &dpa->nep, &nep_elf);
	if (ret != 0) {
		dev_err(dev, "Failed to load NEP firmware to NEP's TCM/SRAM\n");
		return ret;
	}

	ret = google_dpa_release_from_wait_state(dpa);
	if (ret != 0) {
		dev_err(dev, "Failed to release NCP from the wait state.\n");
		return ret;
	}

	return ret;
}

static int google_dpa_s_load_elf_and_release_from_wait_state(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	if (!dpa->fw_img_carveout.pa)
		return -EFAULT;

	trace_google_dpa_boot_secure_boot(dpa);

	ret = google_dpa_secure_boot(dpa->secure_chan);
	if (ret) {
		dev_err(dev, "Secure boot failed\n");
		return ret;
	}

	return ret;
}

static int google_dpa_load_elf_and_release_from_wait_state(struct google_dpa *dpa)
{
	if (dpa->use_secure_boot)
		return google_dpa_s_load_elf_and_release_from_wait_state(dpa);
	else
		return google_dpa_ns_load_elf_and_release_from_wait_state(dpa);
}

static int google_dpa_poll_for_rpc_health(struct google_dpa *dpa, PwRpcClient *client)
{
	struct device *dev = dpa->dev;
	ktime_t timeout = ktime_add_ms(ktime_get(), GOOGLE_DPA_RPC_READY_TIMEOUT_MS);
	int ret;

	for (;;) {
		ret = dpa_rpc_check_health(dev, client);
		if (!ret)
			return 0;

		if (ktime_after(ktime_get(), timeout))
			break;

		msleep(GOOGLE_DPA_RPC_READY_POLL_INTERVAL_MS);
	}

	return ret ? -ETIMEDOUT : 0;
}

static int google_dpa_wait_for_rpc_ready(struct google_dpa *dpa)
{
	int ret;

	ret = google_dpa_poll_for_rpc_health(dpa, google_dpa_rpc_ncp_client());
	if (ret) {
		dev_err(dpa->dev, "Timed out waiting for NCP RPC ready\n");
		return -ETIMEDOUT;
	}

	ret = google_dpa_poll_for_rpc_health(dpa, google_dpa_rpc_nep_client());
	if (ret) {
		dev_err(dpa->dev, "Timed out waiting for NEP RPC ready\n");
		return -ETIMEDOUT;
	}

	trace_google_dpa_boot_rpc_ready(dpa);

	return ret;
}

static int enable_dpa_uart_clock(struct google_dpa *dpa)
{
	int ret = 0;
#ifdef USE_PIXEL_CPM
	ret = reset_control_deassert(dpa->uart_clk_reset);
	if (ret) {
		dev_err(dpa->dev, "Failed to deassert DPA UART clock reset signal.\n");
		return ret;
	}
	ret = clk_prepare_enable(dpa->uart_clk);
	if (ret) {
		dev_err(dpa->dev, "Failed to enable DPA UART clock.\n");
		return ret;
	}
#endif // USE_PIXEL_CPM
	return ret;
}

static void disable_dpa_uart_clock(struct google_dpa *dpa)
{
#ifdef USE_PIXEL_CPM
	clk_disable_unprepare(dpa->uart_clk);
#endif // USE_PIXEL_CPM
}

static int google_dpa_start_locked(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	if (dpa->fw_state != GOOGLE_DPA_FW_AUTHENTICATED) {
		dev_err(dev, "Invalid state to start DPA firmware.\n");
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		dev_err(dev, "Failed to turn on power-domains.\n");
		return ret;
	}

	ret = enable_dpa_uart_clock(dpa);
	if (ret)
		goto runtime_pm_put;

	ret = google_dpa_load_elf_and_release_from_wait_state(dpa);
	if (ret)
		goto runtime_pm_put;
	dpa->fw_state = GOOGLE_DPA_FW_NCP_RELEASED;

	ret = google_dpa_wait_for_boot_completion(dpa);
	if (ret) {
		dev_err(dev, "Failed to get the boot completion message from DPA.\n");
		goto runtime_pm_put;
	}

	ret = google_dpa_rpc_init(dpa);
	if (ret) {
		dev_err(dpa->dev, "Failed to init RPC client (ret=%d).\n", ret);
		goto runtime_pm_put;
	}

	// test RPC functionality as soon as possible
	ret = google_dpa_wait_for_rpc_ready(dpa);
	if (ret)
		goto rpc_deinit;

	if (dpa->use_log_proxy) {
		ret = google_dpa_log_proxy_listen(dpa);
		if (ret) {
			dev_err(dev, "Failed to listen DPA log (ret=%d).\n", ret);
			goto rpc_deinit;
		}
	}

	ret = google_dpa_tea_proxy_init(dpa);
	if (ret) {
		dev_err(dev, "Failed to init DPA tea proxy (ret=%d).\n", ret);
		goto log_proxy_cancel;
	}

	ret = google_dpa_ring_service_proxy_init(dpa);
	if (ret) {
		dev_err(dev, "Failed to init DPA ring service proxy (ret=%d).\n", ret);
		goto log_proxy_cancel;
	}

	ret = google_dpa_wait_for_rpc_ready(dpa);
	if (ret)
		goto log_proxy_cancel;

	google_dpa_ctrl_set_state(NOA_STATE_READY);

	dpa->fw_state = GOOGLE_DPA_FW_RUNNING;

	/*
	 * Remove the DPA power vote. DPA itself decides when to go to the low
	 * power state.
	 **/
	pm_runtime_put(dev);

	return 0;
log_proxy_cancel:
	google_dpa_log_proxy_cancel(dpa);
rpc_deinit:
	google_dpa_rpc_deinit(dpa);
runtime_pm_put:
	pm_runtime_put(dev);

	return ret;
}

static int google_dpa_copy_fw_image(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				    const struct firmware *fw, u8 *target_va, phys_addr_t target_pa,
				    size_t target_len, size_t *out_consumed)
{
	struct device *dev = dpa->dev;
	struct google_dpa_mcu_image *image = &mcu->fw_image;
	u8 *aligned_va;
	phys_addr_t aligned_pa;
	ptrdiff_t align_offset;
	size_t len;

	/* The image has to be page aligned for GSA authentication. */
	aligned_va = PTR_ALIGN(target_va, PAGE_SIZE);
	align_offset = aligned_va - target_va;
	aligned_pa = target_pa + align_offset;
	len = target_len - align_offset;
	if (align_offset + fw->size > target_len) {
		dev_err(dev, "The firmware image size is too large (%zu < %zu).\n", target_len,
			align_offset + fw->size);
		return -EINVAL;
	}
	memcpy(aligned_va, fw->data, fw->size);
	image->data = aligned_va;
	image->pa = aligned_pa;
	image->size = fw->size;
	*out_consumed = align_offset + fw->size;

	image->elf_data = image->data + GOOGLE_AUTH_IMAGE_FORMAT_HEADER_SIZE;
	image->elf_size = image->size - GOOGLE_AUTH_IMAGE_FORMAT_HEADER_SIZE;

	return 0;
}

static int google_dpa_prepare_fw_img_carveout(struct google_dpa *dpa, size_t len)
{
	/*
	 * dpa->fw_img_carveout.va is not NULL when a reserved DRAM region is
	 * defined in the device tree. The predefined reserved regions is
	 * required for the secure boot. In case of Non secure boot, we can
	 * fallback to the dynamically allocated memory if the reserved region
	 * does not exist.
	 */
	if (dpa->fw_img_carveout.va == NULL) {
		if (dpa->use_secure_boot) {
			dev_err(dpa->dev, "Secure boot requires carved out memory.\n");
			return -EINVAL;
		}
		/*
		 * Allocate two additional pages to guarantee page alignment for
		 * firmware images and sufficient space for two separate images.
		 */
		dpa->fw_img_carveout.len = len + PAGE_SIZE * 2;
		dpa->fw_img_carveout.va = vmalloc(dpa->fw_img_carveout.len);
		dpa->fw_img_carveout.pa = 0;
		if (!dpa->fw_img_carveout.va) {
			dev_err(dpa->dev, "Failed to allocate fw image memory.\n");
			return -ENOMEM;
		}
	}

	return 0;
}

// TODO(b/426465018): Remove this function after the migration
static int google_dpa_load_fw_image_to_carveout_v1(struct google_dpa *dpa,
						   const struct firmware *ncp_fw,
						   const struct firmware *nep_fw)
{
	struct device *dev = dpa->dev;
	u8 *carveout_va;
	phys_addr_t carveout_pa;
	size_t carveout_len;
	size_t consumed;
	int ret;

	ret = google_dpa_prepare_fw_img_carveout(dpa, ncp_fw->size + nep_fw->size);
	if (ret)
		return ret;

	carveout_va = dpa->fw_img_carveout.va;
	carveout_pa = dpa->fw_img_carveout.pa;
	carveout_len = dpa->fw_img_carveout.len;

	ret = google_dpa_copy_fw_image(dpa, &dpa->ncp, ncp_fw, carveout_va, carveout_pa,
				       carveout_len, &consumed);
	if (ret) {
		dev_err(dev, "Failed to copy NCP image to the carveout region.\n");
		return ret;
	}

	carveout_va += consumed;
	carveout_pa += consumed;
	carveout_len -= consumed;

	ret = google_dpa_copy_fw_image(dpa, &dpa->nep, nep_fw, carveout_va, carveout_pa,
				       carveout_len, &consumed);
	if (ret) {
		dev_err(dev, "Failed to copy NEP image to the carveout region.\n");
		return ret;
	}

	/* flush all data to the carvout region to pass the data to DPA TZ App correctly. */
	wmb();

	return 0;
}

static int
google_dpa_load_fw_image_to_carveout_v2(struct google_dpa *dpa, const struct firmware *noa_fw,
					const struct google_dpa_image_config *image_config,
					size_t image_body_offset, phys_addr_t *out_image_pa)
{
	struct device *dev = dpa->dev;
	u8 *carveout_va;
	phys_addr_t carveout_pa;
	size_t carveout_len;
	u8 *aligned_va;
	phys_addr_t aligned_pa;
	ptrdiff_t align_offset;
	size_t len;
	int ret;

	ret = google_dpa_prepare_fw_img_carveout(dpa, noa_fw->size);
	if (ret)
		return ret;

	carveout_va = dpa->fw_img_carveout.va;
	carveout_pa = dpa->fw_img_carveout.pa;
	carveout_len = dpa->fw_img_carveout.len;

	aligned_va = PTR_ALIGN(carveout_va, PAGE_SIZE);
	align_offset = aligned_va - carveout_va;
	aligned_pa = carveout_pa + align_offset;
	len = carveout_len - align_offset;
	if (align_offset + noa_fw->size > carveout_len) {
		dev_err(dev, "The firmware image size is too large (%zu < %zu).\n", carveout_len,
			align_offset + noa_fw->size);
		return -EINVAL;
	}
	dev_info(dev, "carveout_pa: 0x%llx (0x%zx).\n", carveout_pa, carveout_len);
	memcpy(aligned_va, noa_fw->data, noa_fw->size);
	*out_image_pa = aligned_pa;

	/* flush all data to the carvout region to pass the data to DPA TZ App correctly. */
	wmb();

	dpa->ncp.fw_image.elf_data =
		aligned_va + image_body_offset + image_config->ncp_image_offset;
	dpa->ncp.fw_image.elf_size = image_config->ncp_image_length;
	dpa->nep.fw_image.elf_data =
		aligned_va + image_body_offset + image_config->nep_image_offset;
	dpa->nep.fw_image.elf_size = image_config->nep_image_length;

	return 0;
}

// TODO(b/426465018): Remove this function after the migration
static int google_dpa_get_firmware_image_v1(struct google_dpa *dpa,
					    const struct firmware **out_ncp_fw,
					    const struct firmware **out_nep_fw)
{
	struct device *dev = dpa->dev;
	struct google_dpa_mcu *ncp = &dpa->ncp;
	struct google_dpa_mcu *nep = &dpa->nep;
	int ret;

	ret = request_firmware(out_ncp_fw, ncp->firmware_name, dev);
	if (ret) {
		dev_err(dev, "Failed to get NCP image from the filesystem.\n");
		return ret;
	}

	ret = request_firmware(out_nep_fw, nep->firmware_name, dev);
	if (ret) {
		dev_err(dev, "Failed to get NEP image from the filesystem.\n");
		goto release_ncp;
	}

	return ret;

release_ncp:
	release_firmware(*out_ncp_fw);

	return ret;
}

// TODO(b/426465018): Remove this function after the migration
static int google_dpa_request_image_authentication_v1(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct google_dpa_secure_fw_image ncp_img, nep_img;
	int ret;

	if (!dpa->fw_img_carveout.pa)
		return -EFAULT;

	ncp_img.pa = dpa->ncp.fw_image.pa;
	ncp_img.size = dpa->ncp.fw_image.size;
	nep_img.pa = dpa->nep.fw_image.pa;
	nep_img.size = dpa->nep.fw_image.size;
	dev_dbg(dev, "Requesting image authentication.\n");
	ret = google_dpa_secure_img_auth_v1(dpa->secure_chan, &ncp_img, &nep_img);
	if (ret) {
		dev_err(dev, "Image authentication failed.\n");
		return ret;
	}

	return ret;
}

static int google_dpa_request_image_authentication_v2(struct google_dpa *dpa, phys_addr_t image_pa,
						      size_t image_size)
{
	struct google_dpa_secure_fw_image dpa_image;

	if (!dpa->fw_img_carveout.pa)
		return -EFAULT;

	dpa_image.pa = image_pa;
	dpa_image.size = image_size;

	return google_dpa_secure_img_auth_v2(dpa->secure_chan, &dpa_image);
}

// TODO(b/426465018): Remove this function after the migration
static bool google_dpa_is_valid_image_config_v1(const struct google_dpa_image_config *image_config)
{
	return image_config->config_version == GOOGLE_DPA_IMAGE_CONFIG_V1 &&
	       image_config->image_format_version == GOOGLE_DPA_IMAGE_BODY_FORMAT_V1;
}

// TODO(b/426465018): Remove this function after the migration
static int google_dpa_parse_firmware_v1(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	const struct firmware *ncp_fw, *nep_fw;
	struct firmware ncp_elf, nep_elf;
	const struct google_dpa_image_config *ncp_img_cfg, *nep_img_cfg;
	int ret;

	ret = google_dpa_get_firmware_image_v1(dpa, &ncp_fw, &nep_fw);
	if (ret)
		return ret;

	ncp_img_cfg = google_dpa_parse_auth_image(dpa, ncp_fw, &ncp_elf);
	if (IS_ERR(ncp_img_cfg)) {
		ret = PTR_ERR(ncp_img_cfg);
		goto release_fw;
	}
	if (!google_dpa_is_valid_image_config_v1(ncp_img_cfg)) {
		dev_err(dev, "Invalid NCP Image config version or image format version.\n");
		ret = -EINVAL;
		goto release_fw;
	}

	nep_img_cfg = google_dpa_parse_auth_image(dpa, nep_fw, &nep_elf);
	if (IS_ERR(nep_img_cfg)) {
		ret = PTR_ERR(nep_img_cfg);
		goto release_fw;
	}
	if (!google_dpa_is_valid_image_config_v1(nep_img_cfg)) {
		dev_err(dev, "Invalid NEP Image config version or image format version.\n");
		ret = -EINVAL;
		goto release_fw;
	}

	ret = google_dpa_parse_elf(dpa, &dpa->ncp, &ncp_elf);
	if (ret != 0) {
		dev_err(dev, "Failed to parse NCP firmware.\n");
		goto release_fw;
	}
	ret = google_dpa_parse_elf(dpa, &dpa->nep, &nep_elf);
	if (ret != 0) {
		dev_err(dev, "Failed to parse NEP firmware.\n");
		goto release_ncp_resources;
	}

	ret = google_dpa_load_fw_image_to_carveout_v1(dpa, ncp_fw, nep_fw);
	if (ret != 0) {
		dev_err(dev, "Failed to load fw image to carveout region.\n");
		goto release_nep_resources;
	}

	ret = google_dpa_configure_iommu_mappings(dpa);
	if (ret) {
		dev_err(dev, "Failed to configure iommu mappings.\n");
		goto free_fw_carveout;
	}

	if (dpa->use_secure_boot) {
		ret = google_dpa_request_image_authentication_v1(dpa);
		if (ret)
			goto cleanup_iommu_mappings;
	}

	goto release_fw;

cleanup_iommu_mappings:
	google_dpa_cleanup_iommu_mappings(dpa);
free_fw_carveout:
	if (!dpa->fw_img_carveout.pa)
		vfree(dpa->fw_img_carveout.va);
release_nep_resources:
	google_dpa_release_resources(dpa, &dpa->nep);
	google_dpa_release_resource_table(&dpa->nep);
release_ncp_resources:
	google_dpa_release_resources(dpa, &dpa->ncp);
	google_dpa_release_resource_table(&dpa->ncp);
release_fw:
	/* This path is used by both success and failure case */
	release_firmware(nep_fw);
	release_firmware(ncp_fw);

	return ret;
}

static int google_dpa_get_ncp_and_nep_image(struct google_dpa *dpa,
					    const struct firmware *image_body,
					    const struct google_dpa_image_config *image_config,
					    struct firmware *ncp_fw, struct firmware *nep_fw)
{
	struct device *dev = dpa->dev;
	size_t offset, len;

	if (image_config->config_version != GOOGLE_DPA_IMAGE_CONFIG_V2 ||
	    image_config->image_format_version != GOOGLE_DPA_IMAGE_BODY_FORMAT_V1) {
		dev_err(dev, "Invalid Image config version (%d) or image format version (%d).\n",
			image_config->config_version, image_config->image_format_version);
		return -EINVAL;
	}

	offset = image_config->ncp_image_offset;
	len = image_config->ncp_image_length;
	if (offset + len > image_body->size) {
		dev_err(dev, "The firmware image is truncated.\n");
		return -EINVAL;
	}

	offset = image_config->nep_image_offset;
	len = image_config->nep_image_length;
	if (offset + len > image_body->size) {
		dev_err(dev, "The firmware image is truncated.\n");
		return -EINVAL;
	}

	ncp_fw->data = image_body->data + image_config->ncp_image_offset;
	ncp_fw->size = image_config->ncp_image_length;

	nep_fw->data = image_body->data + image_config->nep_image_offset;
	nep_fw->size = image_config->nep_image_length;

	return 0;
}

static int google_dpa_parse_firmware_v2(struct google_dpa *dpa)
{
	const struct google_dpa_image_config *image_config;
	struct device *dev = dpa->dev;
	const struct firmware *noa_fw;
	struct firmware image_body, ncp_elf, nep_elf;
	phys_addr_t image_pa;
	int ret;

	ret = request_firmware(&noa_fw, dpa->firmware_name, dev);
	if (ret) {
		dev_err(dev, "Failed to get firmware image from the filesystem.\n");
		return ret;
	}

	image_config = google_dpa_parse_auth_image(dpa, noa_fw, &image_body);
	if (IS_ERR(image_config)) {
		ret = PTR_ERR(image_config);
		goto release_fw;
	}

	ret = google_dpa_get_ncp_and_nep_image(dpa, &image_body, image_config, &ncp_elf, &nep_elf);
	if (ret)
		goto release_fw;

	ret = google_dpa_parse_elf(dpa, &dpa->ncp, &ncp_elf);
	if (ret != 0) {
		dev_err(dev, "Failed to parse NCP firmware.\n");
		goto release_fw;
	}
	ret = google_dpa_parse_elf(dpa, &dpa->nep, &nep_elf);
	if (ret != 0) {
		dev_err(dev, "Failed to parse NEP firmware.\n");
		goto release_ncp_resources;
	}

	ret = google_dpa_load_fw_image_to_carveout_v2(dpa, noa_fw, image_config,
						      image_body.data - noa_fw->data, &image_pa);
	if (ret != 0) {
		dev_err(dev, "Failed to load fw image to carveout region.\n");
		goto release_nep_resources;
	}

	ret = google_dpa_configure_iommu_mappings(dpa);
	if (ret) {
		dev_err(dev, "Failed to configure iommu mappings.\n");
		goto free_fw_carveout;
	}

	if (dpa->use_secure_boot) {
		dev_dbg(dev, "Requesting image authentication with pa: 0x%llx, size:0x%zx.\n",
			image_pa, noa_fw->size);
		ret = google_dpa_request_image_authentication_v2(dpa, image_pa, noa_fw->size);
		if (ret) {
			dev_err(dev, "Image authentication failed (ret=%d).\n", ret);
			goto cleanup_iommu_mappings;
		}
	}

	goto release_fw;

cleanup_iommu_mappings:
	google_dpa_cleanup_iommu_mappings(dpa);
free_fw_carveout:
	if (!dpa->fw_img_carveout.pa)
		vfree(dpa->fw_img_carveout.va);
release_nep_resources:
	google_dpa_release_resources(dpa, &dpa->nep);
	google_dpa_release_resource_table(&dpa->nep);
release_ncp_resources:
	google_dpa_release_resources(dpa, &dpa->ncp);
	google_dpa_release_resource_table(&dpa->ncp);
release_fw:
	/* This path is used by both success and failure case */
	release_firmware(noa_fw);

	return ret;
}

static int google_dpa_boot_locked(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	if (dpa->fw_state == GOOGLE_DPA_FW_UNLOADED) {
		/* TODO(b/426465018): Always use v2 after the migration */
		if (dpa->use_merged_firmware_image)
			ret = google_dpa_parse_firmware_v2(dpa);
		else
			ret = google_dpa_parse_firmware_v1(dpa);
		if (ret) {
			dev_err(dev, "Failed to parse firmware.\n");
			return ret;
		}
		dpa->fw_state = GOOGLE_DPA_FW_AUTHENTICATED;
	}

	if (dpa->fw_state == GOOGLE_DPA_FW_AUTHENTICATED) {
		ret = google_dpa_start_locked(dpa);
		if (ret) {
			dev_err(dev, "Failed to boot DPA firmware.\n");
			return ret;
		}
	} else {
		dev_err(dev, "Invalid firmware state.\n");
		return -EINVAL;
	}

	return ret;
}

int google_dpa_boot(struct google_dpa *dpa)
{
	int ret;

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret)
		return ret;

	ret = google_dpa_boot_locked(dpa);

	mutex_unlock(&dpa->mutex);

	return ret;
}

static void google_dpa_teardown_runtime_services(struct google_dpa *dpa)
{
	if (dpa->use_log_proxy)
		google_dpa_log_proxy_cancel(dpa);

	google_dpa_rpc_deinit(dpa);
}

/*
 * The caller has to ensure that DPA device is active before calling this
 * function (i.e. call pm_runtime_get()).
 */
static int google_dpa_force_power_off_locked(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	if (dpa->fw_state != GOOGLE_DPA_FW_RUNNING && dpa->fw_state != GOOGLE_DPA_FW_NCP_RELEASED &&
	    dpa->fw_state != GOOGLE_DPA_FW_POWER_OFF_FAILED &&
	    dpa->fw_state != GOOGLE_DPA_FW_CRASHED) {
		dev_err(dev, "Invalid firmware state to perform force power off.\n");
		return -EINVAL;
	}

	if (dpa->fw_state == GOOGLE_DPA_FW_RUNNING)
		google_dpa_teardown_runtime_services(dpa);

	dev_dbg(dev, "Forcefully turning off DPA power.\n");
	ret = pm_runtime_put_and_power_off_sync(dpa, /*is_graceful*/ false);
	if (ret) {
		dev_err(dev, "Failed to force power off DPA block: %d.\n", ret);
		/*
		 * The force power off failed.
		 * User can request the force power off again later.
		 */
		dpa->fw_state = GOOGLE_DPA_FW_POWER_OFF_FAILED;
		return ret;
	}

	disable_dpa_uart_clock(dpa);

	dpa->fw_state = GOOGLE_DPA_FW_AUTHENTICATED;

	return ret;
}

/*
 * The caller has to ensure that DPA device is active before calling this
 * function (i.e. call pm_runtime_get()).
 */
static int google_dpa_graceful_power_off_locked(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	if (dpa->fw_state != GOOGLE_DPA_FW_RUNNING) {
		dev_err(dev, "Invalid firmware state to perform graceful power off.\n");
		return -EINVAL;
	}

	// RPC cannot be de-inited here, we still need RPC to request DPA to poweroff

	dev_dbg(dev, "Gracefully turning off DPA power.\n");

	ret = pm_runtime_put_and_power_off_sync(dpa, /*is_graceful*/ true);

	// these are the same as forceful shutdown

	if (ret) {
		dev_err(dev, "Failed to power off DPA block: %d.\n", ret);
		/*
		 * The power off failed.
		 * User can request the power off again later.
		 */
		dpa->fw_state = GOOGLE_DPA_FW_POWER_OFF_FAILED;
		return ret;
	}

	disable_dpa_uart_clock(dpa);

	dpa->fw_state = GOOGLE_DPA_FW_AUTHENTICATED;

	return ret;
}

static int google_dpa_crash_reboot_locked(struct google_dpa *dpa, void *reg_dump,
					  unsigned int reg_dump_len)
{
	struct device *dev = dpa->dev;
	enum google_dpa_fw_state prev_state;
	int ret;

	if (dpa->fw_state != GOOGLE_DPA_FW_RUNNING && dpa->fw_state != GOOGLE_DPA_FW_NCP_RELEASED) {
		dev_err(dev, "Crash reboot is requested in the invalid state.");
		return -EINVAL;
	}

	prev_state = dpa->fw_state;
	dpa->fw_state = GOOGLE_DPA_FW_CRASHED;

	if (prev_state == GOOGLE_DPA_FW_RUNNING)
		google_dpa_teardown_runtime_services(dpa);

	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		dev_err(dev, "Failed to cast a vote for DPA power: %d\n", ret);
		return ret;
	}

	google_dpa_collect_crashdump_locked(dpa, reg_dump, reg_dump_len);

	// TODO(b/444137198): The WLAN and Modem drivers begin their release process immediately
	// after a crash. The ideal long-term solution is to split the crash process into two
	// events: "Crash" and "Release Resource". Since this requires a corresponding fix from
	// the Wi-Fi and Modem teams, we've implemented a temporary workaround by triggering the
	// "Crash" event after the crash dump is complete.
	google_dpa_ctrl_set_state(NOA_STATE_CRASH);

	ret = google_dpa_force_power_off_locked(dpa);
	if (ret)
		return ret;

	dev_dbg(dev, "Rebooting DPA firmware.\n");
	ret = google_dpa_start_locked(dpa);
	if (ret)
		dev_err(dev, "DPA reboot failed.\n");

	return ret;
}

static int google_dpa_shutdown_locked(struct google_dpa *dpa, bool is_graceful)
{
	struct device *dev = dpa->dev;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		dev_err(dev, "Failed to cast a vote for DPA power: %d\n", ret);
		return ret;
	}

	if (is_graceful)
		ret = google_dpa_graceful_power_off_locked(dpa);
	else
		ret = google_dpa_force_power_off_locked(dpa);

	google_dpa_ctrl_set_state(NOA_STATE_UNAVAILABLE);

	return ret;
}

int google_dpa_shutdown(struct google_dpa *dpa, bool is_graceful)
{
	int ret;

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret)
		return ret;

	ret = google_dpa_shutdown_locked(dpa, is_graceful);
	mutex_unlock(&dpa->mutex);

	return ret;
}

static int google_dpa_unload_locked(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	if (dpa->fw_state != GOOGLE_DPA_FW_AUTHENTICATED) {
		dev_err(dev, "Unload is requested in the invalid state.\n");
		return -EINVAL;
	}

	if (dpa->use_secure_boot) {
		ret = google_dpa_secure_img_unload(dpa->secure_chan);
		if (ret) {
			dev_err(dev, "DPA TZ Image unload request failed.\n");
			return ret;
		}
	}
	google_dpa_cleanup_iommu_mappings(dpa);
	google_dpa_release_resources(dpa, &dpa->nep);
	google_dpa_release_resource_table(&dpa->nep);
	google_dpa_release_resources(dpa, &dpa->ncp);
	google_dpa_release_resource_table(&dpa->ncp);

	dpa->fw_state = GOOGLE_DPA_FW_UNLOADED;

	return 0;
}

int google_dpa_unload(struct google_dpa *dpa)
{
	int ret;

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret)
		return ret;

	ret = google_dpa_unload_locked(dpa);

	mutex_unlock(&dpa->mutex);

	return ret;
}

void google_dpa_crash_callback(void *reg_dump, unsigned int reg_dump_len, void *priv_data)
{
	struct google_dpa *dpa = priv_data;
	struct device *dev = dpa->dev;
	int ret;

	dev_err(dev, "Received DPA crash notification. Rebooting DPA firmware.\n");

	mutex_lock(&dpa->mutex);

	/*
	 * TODO(b/380804919): Find a better reboot policy.
	 *                    DPA might crashes repeatedly in a short period due
	 *                    to hardware issues. In that case, it is better to
	 *                    give up reboot.
	 **/
	ret = google_dpa_crash_reboot_locked(dpa, reg_dump, reg_dump_len);
	if (ret)
		dev_err(dev, "Failed to reboot DPA firmware.\n");

	mutex_unlock(&dpa->mutex);
}

int google_dpa_boot_cleanup(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int err = 0;

	mutex_lock(&dpa->mutex);

	if (dpa->fw_state == GOOGLE_DPA_FW_UNLOADED ||
	    dpa->fw_state == GOOGLE_DPA_FW_AUTHENTICATED) {
		/*
		 * Workaround for HW limitation of the power transition.
		 * Linux calls pm_runtime_get_sync() before removing a device.
		 * It means that pm_runtime_get_sync() on DPA device is called
		 * before DPA driver's remove() and the DPA block gets stuck at
		 * the WAIT state if DPA firmware is not released from the wait
		 * state. Boot DPA firmware to avoid getting stuck at WAIT
		 * state.
		 */
		dev_info(dev, "Booting DPA to cleanly tear down DPA block in remove().\n");
		err = google_dpa_boot_locked(dpa);
		if (err)
			dev_err(dev, "Failed to boot DPA firmware.\n");
	}

	if (dpa->fw_state == GOOGLE_DPA_FW_RUNNING || dpa->fw_state == GOOGLE_DPA_FW_NCP_RELEASED ||
	    dpa->fw_state == GOOGLE_DPA_FW_POWER_OFF_FAILED) {
		/*
		 * TODO(b/412265207): Try graceful shutdown in RUNNING state.
		 *                    Fallback to the force shutdown. If it fails.
		 */
		err = google_dpa_shutdown_locked(dpa, /*is_graceful*/ false);
		if (err) {
			dev_err(dev, "Failed to shutdown DPA.\n");
			goto unlock;
		}
		dpa->fw_state = GOOGLE_DPA_FW_AUTHENTICATED;
	}
	if (dpa->fw_state == GOOGLE_DPA_FW_AUTHENTICATED) {
		err = google_dpa_unload_locked(dpa);
		if (err) {
			dev_err(dev, "Failed to unload firmware image.\n");
			goto unlock;
		}
	}

unlock:
	mutex_unlock(&dpa->mutex);

	return err;
}
