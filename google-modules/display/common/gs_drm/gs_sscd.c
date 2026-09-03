// SPDX-License-Identifier: MIT

#include <linux/module.h>
#include <linux/platform_data/sscoredump.h>
#include <gs_drm/gs_sscd.h>

#define DISPLAY_SSCD_MAX_NAME_LEN 32
#define CRASH_INFO_MAX_LEN 256
#define CRASH_PREFIX_MAX_LEN 64

static const struct display_sscd_version current_version = { 0, 3, "Unknown", "Unknown" };

static inline const char *get_common_driver_commit(void)
{
#if IS_ENABLED(CONFIG_MODULE_SCMVERSION)
	return THIS_MODULE->scmversion ? THIS_MODULE->scmversion : "scmversion missing";
#else
	return "Unknown";
#endif
}

/**
 * struct display_sscd_info - holds data for display_sscd utility
 *
 * This private struct contains both platform device data for the display_sscd
 * driver's use as well as some memory used in the coredump process
 */
struct display_sscd_info {
	/** @name: name of sscd driver device */
	char name[DISPLAY_SSCD_MAX_NAME_LEN];
	/** @crash_prefix: prefix to add to device's crash strings */
	char crash_prefix[CRASH_PREFIX_MAX_LEN];
	/** @pdev: allocated and registered platform device */
	struct platform_device pdev;
	/** @pdata: for use by core sscd utility; callback to report function */
	struct sscd_platform_data pdata;
	/** @parent_dev: parent device of the sscd info */
	struct device *parent_dev;
	/** @segs: output data structure for underlying sscd library */
	struct sscd_segment segs[MAX_DISPLAY_SSCD_SEG_COUNT + 1];
	/** @seg_count; how many (non-header) segments to output */
	u16 seg_count;
	/** @driver_scmversion: storage for driver-side scmversion string */
	char driver_scmversion[SCM_VERSION_STRING_MAX_LEN];
	/** @configs: input data structure for configuring outputs */
	struct display_sscd_section_config configs[MAX_DISPLAY_SSCD_SEG_COUNT];
};

static void display_sscd_release(struct device *dev)
{
}

/* Writing/Reporting */

static int display_sscd_write_header_segment(struct display_sscd_info *disp_sscd)
{
	struct sscd_segment *seg0;
	int i;
	struct display_sscd_version version = current_version;

	if (disp_sscd->seg_count > MAX_DISPLAY_SSCD_SEG_COUNT)
		return -EINVAL;

	scnprintf(version.common_scmversion, sizeof(version.common_scmversion), "%s",
		  get_common_driver_commit());
	scnprintf(version.driver_scmversion, sizeof(version.driver_scmversion), "%s",
		  disp_sscd->driver_scmversion);

	for (i = 0; i < disp_sscd->seg_count; ++i) {
		disp_sscd->configs[i].index = i + 1;
		disp_sscd->configs[i].version = version;
	}

	seg0 = &disp_sscd->segs[0];
	seg0->addr = &disp_sscd->configs[0];
	seg0->vaddr = seg0->addr;
	seg0->size = disp_sscd->seg_count * sizeof(struct display_sscd_section_config);
	/* Note that if seg_count == 0, we set up a segment with size 0 */

	return 0;
}

static int display_sscd_write_program_segments(struct display_sscd_info *disp_sscd)
{
	int i;

	if (disp_sscd->seg_count > MAX_DISPLAY_SSCD_SEG_COUNT)
		return -EINVAL;

	for (i = 0; i < disp_sscd->seg_count; ++i) {
		struct sscd_segment *seg = &disp_sscd->segs[i + 1];

		seg->addr = disp_sscd->configs[i].virt_addr;
		seg->vaddr = seg->addr;
		seg->paddr = (void *)disp_sscd->configs[i].phys_addr;
		seg->size = disp_sscd->configs[i].size;
	}

	return 0;
}

int display_sscd_report(struct display_sscd_info *disp_sscd, const char *reason)
{
	int sscd_rc;
	char crash_info[CRASH_INFO_MAX_LEN];

	if (!disp_sscd || !reason)
		return -EINVAL;

	if (strnlen(reason, CRASH_INFO_MAX_LEN) == 0) {
		dev_err(&disp_sscd->pdev.dev, "%s coredump failed: no reason string\n",
			disp_sscd->name);
		return -EINVAL;
	}

	if (disp_sscd->crash_prefix[0])
		scnprintf(crash_info, CRASH_INFO_MAX_LEN, "%s: %s", disp_sscd->crash_prefix,
			  reason);
	else
		strscpy(crash_info, reason, CRASH_INFO_MAX_LEN);

	if (!disp_sscd->pdata.sscd_report) {
		dev_err(&disp_sscd->pdev.dev, "%s coredump failed: no sscd driver\n",
			disp_sscd->name);
		return -EINVAL;
	}

	display_sscd_write_header_segment(disp_sscd);
	display_sscd_write_program_segments(disp_sscd);

	sscd_rc = disp_sscd->pdata.sscd_report(&disp_sscd->pdev, disp_sscd->segs,
					       disp_sscd->seg_count + 1, SSCD_FLAGS_ELFARM64HDR,
					       crash_info);

	return sscd_rc;
}
EXPORT_SYMBOL_GPL(display_sscd_report);

/* Configuration */

void display_sscd_configure_phys_regdump_section(struct display_sscd_section_config *out,
						 const char *name, void *virt_addr, u64 phys_addr,
						 u64 size)
{
	if (!out || !name)
		return;

	scnprintf(out->name, SEGMENT_MAX_NAME_LEN, "%s", name);
	out->seg_type = SEGMENT_TYPE_PHYS_REGDUMP;
	out->virt_addr = virt_addr;
	out->phys_addr = phys_addr;
	out->size = size;
}
EXPORT_SYMBOL_GPL(display_sscd_configure_phys_regdump_section);

void display_sscd_configure_phys_hexdump_section(struct display_sscd_section_config *out,
						 const char *name, void *virt_addr, u64 phys_addr,
						 u64 size)
{
	if (!out || !name)
		return;

	scnprintf(out->name, SEGMENT_MAX_NAME_LEN, "%s", name);
	out->seg_type = SEGMENT_TYPE_PHYS_HEXDUMP;
	out->virt_addr = virt_addr;
	out->phys_addr = phys_addr;
	out->size = size;
}
EXPORT_SYMBOL_GPL(display_sscd_configure_phys_hexdump_section);

void display_sscd_configure_virt_buffer_section(struct display_sscd_section_config *out,
						const char *name, void *virt_addr, u64 size)
{
	if (!out || !name)
		return;

	scnprintf(out->name, SEGMENT_MAX_NAME_LEN, "%s", name);
	out->seg_type = SEGMENT_TYPE_VIRTUAL_BUFFERS;
	out->virt_addr = virt_addr;
	out->phys_addr = 0;
	out->size = size;
}
EXPORT_SYMBOL_GPL(display_sscd_configure_virt_buffer_section);

void display_sscd_configure_log_buffer_section(struct display_sscd_section_config *out,
					       const char *name, void *virt_addr, u64 size)
{
	if (!out || !name)
		return;

	scnprintf(out->name, SEGMENT_MAX_NAME_LEN, "%s", name);
	out->seg_type = SEGMENT_TYPE_LOG_TEXT;
	out->virt_addr = virt_addr;
	out->phys_addr = 0;
	out->size = size;
}
EXPORT_SYMBOL_GPL(display_sscd_configure_log_buffer_section);

int display_sscd_configure_sections(struct display_sscd_info *disp_sscd,
				    struct display_sscd_section_config *configs, u16 num_configs)
{
	if (!disp_sscd || !configs || num_configs > MAX_DISPLAY_SSCD_SEG_COUNT)
		return -EINVAL;

	memcpy(disp_sscd->configs, configs,
	       num_configs * sizeof(struct display_sscd_section_config));

	disp_sscd->seg_count = num_configs;

	return 0;
}
EXPORT_SYMBOL_GPL(display_sscd_configure_sections);

int display_sscd_push_section(struct display_sscd_info *disp_sscd,
			      struct display_sscd_section_config *config)
{
	if (!disp_sscd || !config)
		return -EINVAL;

	if (disp_sscd->seg_count >= MAX_DISPLAY_SSCD_SEG_COUNT)
		return -EINVAL;

	memcpy(&(disp_sscd->configs[disp_sscd->seg_count]), config,
	       sizeof(struct display_sscd_section_config));
	disp_sscd->seg_count++;

	return disp_sscd->seg_count;
}
EXPORT_SYMBOL_GPL(display_sscd_push_section);

struct display_sscd_section_config display_sscd_pop_section(struct display_sscd_info *disp_sscd)
{
	struct display_sscd_section_config popped_section = {};

	if (!disp_sscd)
		return popped_section;

	if (disp_sscd->seg_count <= 0)
		return popped_section;

	disp_sscd->seg_count--;

	popped_section = disp_sscd->configs[disp_sscd->seg_count];
	return popped_section;
}
EXPORT_SYMBOL_GPL(display_sscd_pop_section);

/* Getters */

int display_sscd_get_segment_num(const struct display_sscd_info *disp_sscd)
{
	if (!disp_sscd)
		return -EINVAL;

	return disp_sscd->seg_count;
}
EXPORT_SYMBOL_GPL(display_sscd_get_segment_num);

struct display_sscd_section_config *
display_sscd_get_section_config_at(struct display_sscd_info *disp_sscd, size_t idx)
{
	if (!disp_sscd)
		return NULL;

	if (idx > disp_sscd->seg_count)
		return NULL;

	return &(disp_sscd->configs[idx - 1]);
}
EXPORT_SYMBOL_GPL(display_sscd_get_section_config_at);

/* Init/Deinit */

int display_sscd_device_initialize(struct device *parent_dev,
				   struct display_sscd_info **disp_sscd_addr,
				   const char *sscd_dev_suffix, const char *driver_version,
				   const char *crash_reason_prefix)
{
	struct display_sscd_info *disp_sscd;
	struct platform_device *pdev;
	int ret;
	const char *device_name;

	if (!parent_dev)
		return -EINVAL;

	device_name = dev_name(parent_dev);

	if (!disp_sscd_addr) {
		dev_err(parent_dev,
			"Null pointer passed to display_sscd_addr register function for %s\n",
			device_name);
		return -EINVAL;
	}

	/* Alloc sscd device information */
	disp_sscd = devm_kzalloc(parent_dev, sizeof(*disp_sscd), GFP_KERNEL);
	if (!disp_sscd)
		return -ENOMEM;

	/* Write common information into disp_sscd */
	disp_sscd->parent_dev = parent_dev;
	scnprintf(disp_sscd->name, DISPLAY_SSCD_MAX_NAME_LEN, "display-%s", sscd_dev_suffix);
	scnprintf(disp_sscd->driver_scmversion, SCM_VERSION_STRING_MAX_LEN, "%s", driver_version);
	if (crash_reason_prefix)
		strscpy(disp_sscd->crash_prefix, crash_reason_prefix, CRASH_PREFIX_MAX_LEN);

	/* Fill out platform device and register */
	pdev = &disp_sscd->pdev;

	pdev->name = disp_sscd->name;
	pdev->driver_override = SSCD_NAME;
	pdev->id = -1;
	pdev->dev.platform_data = &disp_sscd->pdata;
	pdev->dev.release = display_sscd_release;

	ret = platform_device_register(pdev);
	if (ret) {
		dev_err(parent_dev, "Failed to register coredump device for %s\n", device_name);
		platform_device_unregister(pdev);
		devm_kfree(parent_dev, disp_sscd);

		return ret;
	}

	*disp_sscd_addr = disp_sscd;
	return ret;
}
EXPORT_SYMBOL_GPL(display_sscd_device_initialize);

void display_sscd_device_free(struct display_sscd_info *disp_sscd)
{
	if (disp_sscd) {
		platform_device_unregister(&disp_sscd->pdev);
		devm_kfree(disp_sscd->parent_dev, disp_sscd);
	}
}
EXPORT_SYMBOL_GPL(display_sscd_device_free);
