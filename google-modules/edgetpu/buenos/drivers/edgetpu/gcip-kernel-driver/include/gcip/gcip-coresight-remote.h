/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP coresight remote.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __GCIP_CORESIGHT_REMOTE_H__
#define __GCIP_CORESIGHT_REMOTE_H__

#include <linux/device.h>
#include <linux/err.h>
#include <linux/types.h>

#define GCIP_CORESIGHT_REMOTE_ENABLED \
	(IS_ENABLED(CONFIG_CORESIGHT_REMOTE) && IS_ENABLED(CONFIG_GCIP_CORESIGHT_REMOTE))

#if GCIP_CORESIGHT_REMOTE_ENABLED
#include <hwtracing/coresight/coresight-remote.h>
#endif /* GCIP_CORESIGHT_REMOTE_ENABLED */

#include <gcip/gcip-pm.h>
#include <gcip/gcip-status-code.h>

/**
 * Maximum number of firmware targets to which GCIP coresight remote will be propagating the
 * coresight commands.
 */
#define GCIP_CORESIGHT_REMOTE_MAX_FW_TARGETS 2

/**
 * The most significant 16 bits of @gcip_kci_dma_descriptor.flags are used to encode a magic value
 * that notifies the firmware's coresight remote layer that commands are embedded directly within
 * the @gcip_kci_dma_descriptor structure itself. While @gcip_coresight_bulk_cmds establishes the
 * standard KCI payload for coresight commands, IP drivers may utilize an optional optimization to
 * transmit a limited number of commands by repurposing the descriptor's memory (specifically the
 * address and size fields before flags). The maximum number of commands that can be embedded is
 * determined by the KCI descriptor layout (offsetof(struct gcip_kci_dma_descriptor, flags) /
 * sizeof(u32)).
 */
#define GCIP_CORESIGHT_REMOTE_COMMANDS_IN_KCI_DMA_DESCRIPTOR 0xDECD
#define GCIP_CORESIGHT_REMOTE_COMMANDS_IN_KCI_DMA_DESCRIPTOR_SHIFT 16

/**
 * LSB 16 bits of @gcip_kci_dma_descriptor.flags to encode the number of commands stored at
 * @gcip_kci_dma_descriptor memory.
 */
#define GCIP_CORESIGHT_REMOTE_NUM_COMMANDS_IN_KCI_DMA_DESCRIPTOR_MASK 0xFFFF

/**
 * Size of KCI payload is standardized to 64 bytes on firmware side.
 * @gcip_coresight_remote_bulk_cmds to reserve 4-byte for num_commands. This leaves 60 bytes for the
 * coresight remote commands array. 60 / sizeof(u32) = 15.
 */
#define GCIP_CORESIGHT_REMOTE_MAX_BULK_CMDS 15

/**
 * struct gcip_coresight_remote_bulk_cmds - KCI payload for bulk coresight remote commands.
 * @num_commands: Number of coresight remote commands.
 * @commands: Array of coresight remote commands.
 *
 * Used for sending bulk coresight remote commands to the firmware over KCI interface.
 */
struct gcip_coresight_remote_bulk_cmds {
	u32 num_commands;
	u32 commands[GCIP_CORESIGHT_REMOTE_MAX_BULK_CMDS];
} __packed;

/**
 * struct gcip_coresight_base_state - Base state for a coresight remote component.
 * @remote_id: Identifies the specific component instance to FW.
 * @is_enabled: Tracks if 'enable' has been called.
 * @reserved: Explicit padding for 4-byte alignment.
 */
struct gcip_coresight_base_state {
	u8 remote_id;
	bool is_enabled;
	u16 reserved;
};

/**
 * struct gcip_etm_state - State for an ETM (source) component.
 * @base: Base state of the component.
 */
struct gcip_etm_state {
	struct gcip_coresight_base_state base;
};

/**
 * struct gcip_etf_state - State for an ETF (Sink/Link) component.
 * @base: Base state of the component.
 * @mode: ETF mode of operation.
 */
struct gcip_etf_state {
	struct gcip_coresight_base_state base;
	u32 mode;
};

/**
 * struct gcip_funnel_state - State for a Funnel (Link) component.
 * @base: Base state of the component.
 * @active_ports_mask: Bitmask representing enabled input ports.
 */
struct gcip_funnel_state {
	struct gcip_coresight_base_state base;
	u32 active_ports_mask;
};

/**
 * struct gcip_replicator_state - State for a Replicator (Link) component.
 * @base: Base state of the component.
 * @active_child_ports_mask: Bitmask for enabled output paths.
 */
struct gcip_replicator_state {
	struct gcip_coresight_base_state base;
	u32 active_child_ports_mask;
};

/**
 * struct gcip_trace_components - Container for arrays of GCIP trace components for a single
 *                                firmware target.
 * @etms: Pointer to an array of ETM components.
 * @num_etms: Number of elements in the @etms array.
 * @etfs: Pointer to an array of ETF components.
 * @num_etfs: Number of elements in the @etfs array.
 * @funnels: Pointer to an array of Funnel components.
 * @num_funnels: Number of elements in the @funnels array.
 * @replicators: Pointer to an array of Replicator components.
 * @num_replicators: Number of elements in the @replicators array.
 *
 * This structure holds pointers to dynamically allocated arrays of state
 * for various CoreSight components, along with the number of elements
 * in each array.
 */
struct gcip_trace_components {
	struct gcip_etm_state *etms;
	u32 num_etms;

	struct gcip_etf_state *etfs;
	u32 num_etfs;

	struct gcip_funnel_state *funnels;
	u32 num_funnels;

	struct gcip_replicator_state *replicators;
	u32 num_replicators;
};

/**
 * struct gcip_coresight_remote - GCIP coresight remote structure.
 * @dev: Pointer to the device structure.
 * @pm: Pointer to the GCIP power management structure.
 * @num_fw_targets: Number of firmware targets.
 * @kci_data: Array of KCI data pointers for each firmware target.
 * @components: Array of trace components for each firmware target.
 * @send_kci: Function pointer to assist in propagating the coresight remote
 *            commands to the firmware.
 */
struct gcip_coresight_remote {
	struct device *dev;
	struct gcip_pm *pm;
	u32 num_fw_targets;
	void *kci_data[GCIP_CORESIGHT_REMOTE_MAX_FW_TARGETS];
	struct gcip_trace_components components[GCIP_CORESIGHT_REMOTE_MAX_FW_TARGETS];
	int (*send_kci)(void *kci_data, struct gcip_coresight_remote_bulk_cmds *cmds,
			enum gcip_status_code *rsp);
};

/**
 * struct gcip_coresight_remote_args - Arguments for registering GCIP coresight remote.
 * @dev: Pointer to the device structure.
 * @pm: Pointer to the GCIP power management structure.
 * @num_fw_targets: Number of firmware targets.
 * @kci_data: Array of KCI data pointers for each firmware target.
 * @send_kci: Function pointer to assist in propagating the coresight remote
 *            commands to the firmware.
 */
struct gcip_coresight_remote_args {
	struct device *dev;
	u32 num_fw_targets;
	struct gcip_pm *pm;
	void *kci_data[GCIP_CORESIGHT_REMOTE_MAX_FW_TARGETS];
	int (*send_kci)(void *kci_data, struct gcip_coresight_remote_bulk_cmds *cmds,
			enum gcip_status_code *rsp);
};

#if GCIP_CORESIGHT_REMOTE_ENABLED
/**
 * gcip_coresight_remote_register() - Registers the coresight remote provider layer with coresight
 *                                    remote framework.
 * @args: Arguments for registration.
 *
 * Return: On success, a pointer to the GCIP coresight remote. On failure, an ERR_PTR().
 */
struct gcip_coresight_remote *
gcip_coresight_remote_register(const struct gcip_coresight_remote_args *args);

/**
 * gcip_coresight_remote_unregister() - Unregisters the coresight remote.
 * @coresight_remote: The coresight remote to unregister.
 */
void gcip_coresight_remote_unregister(struct gcip_coresight_remote *coresight_remote);

/**
 * gcip_coresight_remote_restore_state() - Restores the state of the coresight remote components.
 * @coresight_remote: The coresight remote to restore.
 *
 * This function should be called early during MCU boot to restore the state of the coresight remote
 * components.
 *
 * Return: 0 on success, negative error code on failure.
 */
int gcip_coresight_remote_restore_state(struct gcip_coresight_remote *coresight_remote);

#else /* GCIP_CORESIGHT_REMOTE_ENABLED */

static inline struct gcip_coresight_remote *
gcip_coresight_remote_register(const struct gcip_coresight_remote_args *args)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void gcip_coresight_remote_unregister(struct gcip_coresight_remote *coresight_remote)
{
}

static inline int
gcip_coresight_remote_restore_state(struct gcip_coresight_remote *coresight_remote)
{
	return -EOPNOTSUPP;
}

#endif /* GCIP_CORESIGHT_REMOTE_ENABLED */

#endif /* __GCIP_CORESIGHT_REMOTE_H__ */
