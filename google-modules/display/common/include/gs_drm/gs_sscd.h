/* SPDX-License-Identifier: MIT */
#ifndef _GS_SSCD_H_
#define _GS_SSCD_H_

#include <linux/platform_device.h>

#define MAX_DISPLAY_SSCD_SEG_COUNT 16
#define SEGMENT_MAX_NAME_LEN 64
#define SCM_VERSION_STRING_MAX_LEN 32

struct display_sscd_info;

/**
 * enum display_sscd_segment_type - broad type this segment matches
 * @SEGMENT_TYPE_PHYS_REGDUMP: Dump of physical memory space representing
 *                             registers on hardware components
 * @SEGMENT_TYPE_PHYS_HEXDUMP: Dump of physical memory space, already output in
 *                             hex format into virtual memory buffers
 * @SEGMENT_TYPE_VIRTUAL_BUFFERS: Dump of virtual memory buffers
 * @SEGMENT_TYPE_LOG_TEXT: Text dump of log contents
 * @SEGMENT_TYPE_MAX: Maximum number of segment types (do not use directly)
 *
 * This list may be expanded. Each type should indicate some hints about how to
 * decode the dumped binary in the segment.
 */
enum display_sscd_segment_type {
	SEGMENT_TYPE_PHYS_REGDUMP = 0,
	SEGMENT_TYPE_PHYS_HEXDUMP,
	SEGMENT_TYPE_VIRTUAL_BUFFERS,
	SEGMENT_TYPE_LOG_TEXT,
	SEGMENT_TYPE_MAX,
};

/**
 * struct display_sscd_version - version encoding for coredump artifact
 *
 * Supports both a major.minor schema (to distinguish coredumps made alongside
 * larger changes to the display_sscd API) and tagging with version-control
 * information tied to both the common display_sscd repository and the client
 * driver repository
 *
 * @major: major verson of framework
 * @minor: minor version of framework
 * @common_scmversion: commit version of the common display_sscd repository
 * @driver_scmversion: commit version of the client driver repository
 */
struct display_sscd_version {
	u8 major;
	u8 minor;
	char common_scmversion[SCM_VERSION_STRING_MAX_LEN];
	char driver_scmversion[SCM_VERSION_STRING_MAX_LEN];
};

/**
 * struct display_sscd_section_config - description of later segment
 *
 * This is used to store information about subsequent coredump ELF segments;
 * it will be written to the first program segment in the resulting binary.
 *
 * Some values in the struct are meant as inputs on the kernel side;
 * others will be filled in and retrievable during binary decode.
 *
 * Notably, all members must be stored "by-value;" no pointer contents
 * will be maintained as part of the dump.
 */
struct display_sscd_section_config {
	/* OUTPUTS */
	/** @version: version of segment encoding */
	struct display_sscd_version version;
	/**
	 * @index: Which segment in the binary this config header refers to.
	 *         Because the first segment is the collection of config headers,
	 *         this will start at index 1 for the first dumped segment
	 *         after the header segment
	 */
	u16 index;

	/* INPUTS */
	/** @name: Descriptive name of the segment */
	char name[SEGMENT_MAX_NAME_LEN];
	/**
	 * @seg_type: type of the contained segment, to inform the style of
	 * parsing required for the binary.
	 */
	enum display_sscd_segment_type seg_type;
	/** @virt_addr: virtual address of memory segment to dump. Required. */
	void *virt_addr;
	/**
	 * @phys_addr: physical address of memory segment to dump.
	 * Optional; purely for record-keeping
	 */
	u64 phys_addr;
	/** @size: Size of memory to dump. Required. */
	u64 size;
};

/**
 * SSCD_GET_DRIVER_VERSION() - Client driver-side version resolution
 * Macro used to retrieve scmversion (or equivalent) string from client driver
 *
 * Return: scmversion string, as applicable
 */
#if IS_ENABLED(CONFIG_MODULE_SCMVERSION)
#define SSCD_GET_DRIVER_VERSION() \
	(THIS_MODULE->scmversion ? THIS_MODULE->scmversion : "scmversion missing")
#else
#define SSCD_GET_DRIVER_VERSION() ("Unknown")
#endif

/**
 * display_sscd_report() - Dumps previously-configured sections of memory
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 * @reason: Short description of the reason for the dump.
 *          This gets included as part of the log.
 *
 * This function will read the configured sections of memory from the
 * disp_sscd object and dump their contents. It will not validate its access to
 * the described memory locations in any way before attempting to read them.
 *
 * Note that this contains a blocking call that should not be called in an
 * interrupt context.
 */
int display_sscd_report(struct display_sscd_info *disp_sscd, const char *reason);

/**
 * display_sscd_device_initialize() - initializes a display sscd device
 * @parent_dev: Parent device of the dump device
 * @disp_sscd_addr: Output argument; pointer to address used to store reference
 *                  to display_sscd_info handle.
 * @sscd_dev_suffix: Suffix to add to sscd device name. Must be unique for each sscd device.
 *                   Device name will be "display-<sscd_dev_suffix>"
 * @driver_version: output of SSCD_GET_DRIVER_VERSION() macro from client
 * @crash_reason_prefix: Prefix to add to crash reason string. Optional.
 *
 * Notably, this will use the parent device to `devm_kzalloc` the required
 * memory for the struct display_sscd_info. This memory is freed by the
 * companion call to display_sscd_device_free(), as well as handle other
 * platform device registration/unregistration.
 *
 * Return: 0 on success, negative value on error
 */
int display_sscd_device_initialize(struct device *parent_dev,
				   struct display_sscd_info **disp_sscd_addr,
				   const char *sscd_dev_suffix, const char *driver_version,
				   const char *crash_reason_prefix);

/**
 * display_sscd_device_free() - Frees an instantiated display_sscd device
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 *
 * This unregisters the display_sscd device and frees the previously-allocated
 * memory associated with it.
 */
void display_sscd_device_free(struct display_sscd_info *disp_sscd);

/**
 * display_sscd_configure_phys_regdump_section() - configures section for physical register dump
 * @out: section config to write to
 * @name: Name of segment
 * @virt_addr: virtual address of memory to dump
 * @phys_addr: physical address of registers to dump (optional)
 * @size: size of memory to dump
 */
void display_sscd_configure_phys_regdump_section(struct display_sscd_section_config *out,
						 const char *name, void *virt_addr, u64 phys_addr,
						 u64 size);

/**
 * display_sscd_configure_phys_hexdump_section() - configures section for hexdumped phys registers
 *
 * This method gets around access issues to the physical register space by
 * accepting virtual buffers into which the physical register contents have
 * already been dumped. It may be that some registers are skipped with this
 * approach, by design.
 *
 * @out: section config to write to
 * @name: Name of segment
 * @virt_addr: virtual address of memory to dump
 * @phys_addr: physical address of registers to dump (optional)
 * @size: size of memory to dump (the virtual memory, not physical)
 */
void display_sscd_configure_phys_hexdump_section(struct display_sscd_section_config *out,
						 const char *name, void *virt_addr, u64 phys_addr,
						 u64 size);

/**
 * display_sscd_configure_virt_buffer_section() - configures section for virtual buffers
 * @out: section config to write to
 * @name: Name of segment
 * @virt_addr: address of virtual buffer to dump
 * @size: size of memory to dump
 */
void display_sscd_configure_virt_buffer_section(struct display_sscd_section_config *out,
						const char *name, void *virt_addr, u64 size);

/**
 * display_sscd_configure_log_buffer_section() - configures section for log text buffers
 * @out: section config to write to
 * @name: Name of segment
 * @virt_addr: address of virtual buffer to dump
 * @size: size of memory to dump
 *
 * This is nearly identical to the virtual-buffer function, except that it
 * specifies that the contents are meant to be interpreted as log text,
 * rather than binary data in a virtual buffer.
 */
void display_sscd_configure_log_buffer_section(struct display_sscd_section_config *out,
					       const char *name, void *virt_addr, u64 size);

/**
 * display_sscd_configure_sections() - Configures sscd using descriptive section structs
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 * @configs: array of configs filled in either manually or via the convenience
 *           functions above describing sections of memory to include in coredump
 * @num_configs: number of configs used to describe sections
 *
 * Note that this will functionally overwrite a previous configuration applied
 * by display_sscd_configure_sections(); to add to a previous config, consider
 * display_sscd_push_section().
 *
 * Return: 0 on success, negative value otherwise
 */
int display_sscd_configure_sections(struct display_sscd_info *disp_sscd,
				    struct display_sscd_section_config *configs, u16 num_configs);

/**
 * display_sscd_get_segment_num() - Gets number of configured segments
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 *
 * Return: number of segments of memory configured to be dumped by sscd
 */
int display_sscd_get_segment_num(const struct display_sscd_info *disp_sscd);

/**
 * display_sscd_get_section_config_at() - Retrieves a previously-configured section config
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 * @idx: Section config index to retrieve (1-indexed, not 0-indexed)
 *
 * Possibly useful if the client library wishes to retrieve details
 * regarding a previously-configured section
 *
 * Return: NULL if index out of bounds or other error; else, pointer to
 *         previously-configured section config
 */
struct display_sscd_section_config *
display_sscd_get_section_config_at(struct display_sscd_info *disp_sscd, size_t idx);

/**
 * display_sscd_push_section() - Pushes a section config onto the stack of configs
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 * @config: section config to add to the stack
 *
 * Note that the index in the return value is 1-indexed, rather than 0-indexed
 *
 * Return: index of the added config on success, or negative value on error
 */
int display_sscd_push_section(struct display_sscd_info *disp_sscd,
			      struct display_sscd_section_config *config);

/**
 * display_sscd_pop_section() - Pops a section config off of the stack of configs
 * @disp_sscd: Handle instantiated by display_sscd_device_initialize()
 *
 * Return: the contents of the popped section. May be empty if no configs to pop.
 */
struct display_sscd_section_config display_sscd_pop_section(struct display_sscd_info *disp_sscd);

#endif /* _GS_SSCD_H_ */
