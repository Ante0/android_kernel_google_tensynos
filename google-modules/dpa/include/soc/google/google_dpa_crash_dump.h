/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_CRASH_DUMP_H
#define _GOOGLE_DPA_CRASH_DUMP_H

#include <soc/google/google_dpa.h>

/**
 * enum GOOGLE_DPA_CRASH_SEGMENT_FLAG - Flags for DPA crash dump memory segments.
 *
 * These flags are used in the crash dump to identify the type and origin of
 * different memory segments. They are packed into the lower bits of the APC
 * view address for ramdump processing.
 *
 * @GOOGLE_DPA_SF_NCP_BIT: Identifies the segment as NCP RAM content.
 * @GOOGLE_DPA_SF_NEP_BIT: Identifies the segment as NEP RAM content. For shared
 *                        regions like SRAM, both NCP and NEP bits should be set.
 * @GOOGLE_DPA_SF_REG_BIT: Marks the segment as a register note, used for
 *                         restoring CPU state during analysis.
 * @GOOGLE_DPA_SF_MASK: A mask to extract the flag bits.
 */
enum GOOGLE_DPA_CRASH_SEGMENT_FLAG {
	/* NCP RAM content. */
	GOOGLE_DPA_SF_NCP_BIT = BIT(0),
	/* NEP RAM content. */
	GOOGLE_DPA_SF_NEP_BIT = BIT(1),
	/* Register note for restoring cpu state. */
	GOOGLE_DPA_SF_REG_BIT = BIT(2),
	GOOGLE_DPA_SF_MASK = (0xF),
};

/**
 * struct google_dpa_mem_dump - Represents a memory region for a DPA crash dump.
 *
 * This structure is used to define a single contiguous memory region that will
 * be included in the DPA crash dump. Modules can register instances of this
 * structure to add custom memory dumps for debugging purposes.
 *
 * @flags:         Flags indicating the type of memory segment, using values
 *                 from enum GOOGLE_DPA_CRASH_SEGMENT_FLAG.
 * @data:          A pointer to the kernel buffer containing the memory dump data.
 * @len:           The length of the memory dump data in bytes.
 * @mcu_view_addr: The physical address of the memory region from the MCU's
 *                 perspective.
 * @apc_view_addr: The physical address of the memory region from the APC's
 *                 (Application Processor Core) perspective. This address should be
 *                 16-byte aligned, as the lowest 4 bits are used to storeflags from
 *                 GOOGLE_DPA_CRASH_SEGMENT_FLAG.
 * @node:          The list_head structure for linking this memory dump into a
 *                 list of other dumps.
 */
struct google_dpa_mem_dump {
	u8 flags;
	void *data;
	size_t len;
	u64 mcu_view_addr;
	u64 apc_view_addr;
	struct list_head node;
};

#if IS_ENABLED(CONFIG_SUBSYSTEM_COREDUMP)

/**
 * google_dpa_init_crash_dump_data() - Initializes a DPA memory dump structure.
 * @crash_dump_data: Pointer to the struct google_dpa_mem_dump to be initialized.
 * @flag:            Flags for the memory segment from enum
 *                   GOOGLE_DPA_CRASH_SEGMENT_FLAG.
 * @data:            Pointer to the buffer containing the crash dump data.
 * @mcu_view:        The physical address from the MCU's perspective.
 * @apc_view:        The physical address from the APC's perspective.
 * @len:             The length of the data buffer in bytes.
 *
 * This helper function initializes the fields of a google_dpa_mem_dump
 * structure and prepares its list head.
 */
void google_dpa_init_crash_dump_data(struct google_dpa_mem_dump *crash_dump_data, u8 flag,
				     void *data, u64 mcu_view, u64 apc_view, size_t len);

/**
 * google_dpa_register_crash_dump_data() - Registers a custom memory region for
 *                                         DPA crash dumps.
 * @dpa:             Pointer to the main google_dpa driver structure.
 * @crash_dump_data: Pointer to an initialized struct google_dpa_mem_dump that
 *                   describes the memory region to be included in the dump.
 *
 * Allows external modules to add their own memory regions to the DPA crash
 * dump. The provided @crash_dump_data structure must be properly initialized,
 * typically via google_dpa_init_crash_dump_data().
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int google_dpa_register_crash_dump_data(struct google_dpa *dpa,
					struct google_dpa_mem_dump *crash_dump_data);

/**
 * google_dpa_unregister_crash_dump_data() - Unregisters a custom memory region
 *                                           from DPA crash dumps.
 * @dpa:             Pointer to the main google_dpa driver structure.
 * @crash_dump_data: Pointer to the struct google_dpa_mem_dump that was
 *                   previously registered.
 *
 * This function removes a previously registered memory region from the list of
 * regions to be included in DPA crash dumps.
 */
void google_dpa_unregister_crash_dump_data(struct google_dpa *dpa,
					   struct google_dpa_mem_dump *crash_dump_data);

#else

static inline void google_dpa_init_crash_dump_data(struct google_dpa_mem_dump *crash_dump_data,
						   u8 flag, void *data, u64 mcu_view, u64 apc_view,
						   size_t len)
{
}

static inline int google_dpa_register_crash_dump_data(struct google_dpa *dpa,
						      struct google_dpa_mem_dump *crash_dump_data)
{
	return 0;
}

static inline void
google_dpa_unregister_crash_dump_data(struct google_dpa *dpa,
				      struct google_dpa_mem_dump *crash_dump_data)
{
}

#endif

#endif /* _GOOGLE_DPA_CRASH_DUMP_H */
