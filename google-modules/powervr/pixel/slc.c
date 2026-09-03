// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022-2023 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dev_printk.h>

/* Pixel integration includes */
#include <soc/google/acpm_ipc_ctrl.h>

#include <trace/hooks/systrace.h>

#include "rgx_common.h"
#include "slc.h"

/* Only map NS content to the SLC.
 * Do not map FW (OSID 0), as the TEE does not use the sentinel PBHA when creating its PTEs,
 * and 0 bypasses our LUT leading to spurious usage.
 * Do not map TZ content (OSID 7), the impact is unknown.
 * Do not map unused OSIDs (2-6).
 */
#define _MASK_N_BITS(n) (BIT(n) - 1)
#define OSID_MASK (_MASK_N_BITS(NUM_OSID) & BIT(1))
#define OSID_ENABLED(osid) (!!(BIT(osid) & OSID_MASK))

/**
 * DOC: For both LUTs, only 32-bit access is supported
 */
/**
 * lut_ioscatter32() - Scatter data from "real" memory space to IO memory space, using 32-bit writes
 *
 * @to:           Destination iomem address.
 * @from:         Source data array.
 * @write_stride: The stride to increment @to by before writing.
 *
 * The size of @from must be 4 byte aligned.
 */
#define lut_ioscatter32(to, from, write_stride)                                                    \
	do {                                                                                       \
		static_assert(sizeof(from) % sizeof(u32) == 0);                                    \
		int _i;                                                                            \
		for (_i = 0; _i < sizeof(from) / (sizeof(u32)); ++_i) {                            \
			writel_relaxed(((u32 *)(from))[_i], &((u32 __iomem *)to)[_i * write_stride]);   \
		}                                                                                  \
	} while (0)

/**
 * lut_ioscatter32_nonzero() - Like @lut_ioscatter32, but does not write zero values.
 *
 * @to:           Destination iomem address.
 * @from:         Source data array.
 * @write_stride: The stride to increment @to by before writing.
 *
 * The size of @from must be 4 byte aligned.
 */
#define lut_ioscatter32_nonzero(to, from, write_stride)                                            \
	do {                                                                                       \
		static_assert(sizeof(from) % sizeof(u32) == 0);                                    \
		int _i;                                                                            \
		u32 *_from = (u32 *)(from);                                                        \
		u32 __iomem *_to = (u32 __iomem *)(to);                                            \
		for (_i = 0; _i < sizeof(from) / (sizeof(u32)); ++_i) {                            \
			if (_from[_i] != 0)                                                        \
				writel_relaxed(_from[_i], &_to[_i * write_stride]);                \
		}                                                                                  \
	} while (0)

/*
 *  +------------------+    slc_enable   +------------------+
 *  |                  +----------------->                  |
 *  |  PENDING_DISABLE |                 |      ENABLED     |
 *  |                  <-----------------+                  |
 *  +--------+---------+   slc_disable   +---------^--------+
 *           |                                     |
 *           |                                     |
 *    pt_client_disable                    pt_client_enable
 *           |                                     |
 *           |                                     |
 *           |                                     |
 *  +--------v---------+    slc_enable   +---------+--------+
 *  |                  +----------------->                  |
 *  |     DISABLED     |                 |  PENDING_ENABLE  |
 *  |                  <-----------------+                  |
 *  +------------------+   slc_disable   +------------------+
 */
enum slc_partition_state {
	/* Partition disable request was received, and is pending */
	PENDING_DISABLE,
	/* Partition is currently disabled */
	DISABLED,
	/* Partition enable request was received, and is pending */
	PENDING_ENABLE,
	/* Partition is currently enabled */
	ENABLED,
};

// Return true if we transitioned to the new state
static bool slc_transition(struct slc_partition *partition,
			   enum slc_partition_state old,
			   enum slc_partition_state new)
{
	return atomic_cmpxchg(&partition->state, old, new) == old;
}


/**
 * init_partition - Register and initialize a partition with the SLC driver.
 *
 * @data:  The &struct slc_data tracking partition information.
 * @pt:    The &struct slc_partition to store the configured partition information.
 * @index: The index of the partition, relative to the DT node.
 *
 * Returns EINVAL on error, otherwise 0.
 */
static int init_partition(struct slc_data *data, struct slc_partition *pt, u32 index)
{
	ptid_t ptid;
	int err = -EINVAL;

	ptid = pt_client_enable(data->pt_handle, index);
	if (ptid == PT_PTID_INVALID) {
		dev_err(data->dev, "failed to enable pt: %d\n", index);
		goto err_exit;
	}

	/* This retains the allocated ptid */
	pt_client_disable_no_free(data->pt_handle, index);

	/* Success */
	err = 0;

	*pt = (struct slc_partition) {
		.index = index,
		.ptid = ptid,
		/* We only setup the write stream */
		.ra_entry = {
			.tbl_arpid = 0,
			.tbl_awpid = ptid,
		},
		.pbha = PBHA_WRITE_ALLOC,
	};

	atomic_set(&pt->state, DISABLED);

err_exit:
	return err;
}


/**
 * DOC: Partition mapping
 *
 * A two stage mapping exists, which ingests the PBHA and requester ID carried by a transaction to
 * determine the SLC partition to use.
 *
 * First, the PBHA (4 bit) + requester ID (5 bit) are fed into the REQ_PBHA_LUT - A table which maps
 * the 9 bit combined input to 5 bit intermediate index (often called the REQ_PBHA_ID).
 *
 * The intermediate index (5 bit) is combined with the OSID (3 bit) to produce an 8 bit index, which
 * is used by the RA LUT to look up the partition ID (often called a PTID or PID).
 *
 * Our scheme uses the following initial setup:
 *    + Set up REQ_PBHA_LUT to output PTID based solely on PBHA
 *    + Set up RA LUT to pass-through PTID for each OSID
 *
 * We get maximum flexibility using the above setup:
 *    + Default behaviour is opt in per page for any requester
 *    + Override at the requester ID level can be done in the REQ_PBHA_LUT, to map all traffic from
 *      a specific requester ID to a PTID
 *    + Override at the OSID level can be done in the RA LUT, to map all OSID traffic to a PTID
 *
 *                 OSID
 *          ──────────────────────────────────────┐
 *                                                │          ┌────────────────┐
 *                                                └──────────>                │  PTID
 *                                                           │     RA LUT     ├────────>
 *             requester ID   ┌────────────────┐  ┌──────────>                │
 *          ──────────────────>                │  │   PTID   └────────────────┘
 *                            │  REQ_PBHA_LUT  ├──┘
 *          ──────────────────>                │
 *                 PBHA       └────────────────┘
 *
 *
 * Additionally, read and write streams have their own table entries in the RA LUT. To model a write
 * allocate only partition, we must map only the write stream to our PTID in the relevant RA LUT
 * entry.
 * This is in contrast to previous designs where the partition itself encoded wa/ra controls.
 */

/**
 * map_partition_pbha - Program the shadow REQ_PBHA_LUT to map from internal PBHA to PTID.
 *
 * @data:  The &struct slc_data tracking partition information.
 * @pt:    The &struct slc_partition to map.
 * @requester:  The requester to map for.
 */
static void map_partition_pbha(struct slc_data *data, struct slc_partition *pt,
			       enum requester_id requester)
{
	/* Map the requester + PBHA combination to the requested PTID */
	data->shadow.req_pbha_lut[pt->pbha | (requester << 4)] = pt->ptid;
}

/**
 * map_partition_ra - Program the shadow RA LUTs to pass-through PTID.
 *
 * @data:  The &struct slc_data tracking partition information.
 * @pt:    The &struct slc_partition to map.
 * @requester:  The requester to map for.
 */
static void map_partition_ra(struct slc_data *data, struct slc_partition *pt, uint8_t osid)
{
	/* Map the PTID for the OSID */
	data->shadow.ra_lut[(osid) | (pt->ptid << 3)] = pt->ra_entry;
}

/**
 * init_mappings - Configure shadow LUTs to map PBHA + requester ID to PTID
 *
 * @data:  The &struct slc_data tracking partition information.
 */
static void init_mappings(struct slc_data *data)
{
	int requester, osid;

	/* First, zero out all LUTs */
	memset(data->shadow.req_pbha_lut, 0, sizeof(data->shadow.req_pbha_lut));
	memset(data->shadow.ra_lut, 0, sizeof(data->shadow.ra_lut));

	/* Map the write alloc partition for all requester IDs with SLC PBHA */
	for (requester = 0; requester < REQUESTER_ID__MAX; ++requester)
		map_partition_pbha(data, &data->partition, requester);
	/* Additionally, map `TILING` with default PBHA to use SLC as well */
	data->shadow.req_pbha_lut[PBHA_DEFAULT | (REQUESTER_ID_TILING << 4)] = data->partition.ptid;

	/* Map the write alloc partition for all enabled OSID */
	for (osid = 0; osid < NUM_OSID; ++osid) {
		if (!OSID_ENABLED(osid))
			continue;
		map_partition_ra(data, &data->partition, osid);
	}
}

/**
 * init_luts - Set up virtual mappings for REQ_PBHA_LUT and RA LUT CSRs.
 *
 * @data:  The &struct slc_data tracking partition information.
 *
 * Returns -EINVAL on error, otherwise 0.
 */
static int init_luts(struct slc_data *data)
{
	int ret = -EINVAL;
	char name[] = "ra_lut_0";
	int i;

	/* One RA LUT per GPU data port */
	for (i = 0; i < NUM_GPU_DATA_PORTS; ++i) {
		/* Name is a function of index */
		name[sizeof(name) - 2] = '0' + i;
		/* Each 32-bit RA reg stores mapping info for 2 PBHA values,
		 * so we simply use 16 bit access.
		 */
		data->ra_lut[i] =
			devm_platform_ioremap_resource_byname(to_platform_device(data->dev), name);

		if (IS_ERR(data->ra_lut[i]))
			goto exit;
	}

	/* Each requester has 4 group registers, each register maps 4 PBHA values.
	 * We map for 8 bit access to easily mutate individual table entries.
	 */
	data->req_pbha_lut = devm_platform_ioremap_resource_byname(to_platform_device(data->dev),
	                                                           "req_pbha_lut");
	if (IS_ERR(data->req_pbha_lut))
		goto exit;

	ret = 0;
exit:
	return ret;
}

/**
 * slc_program_lut - Program LUT CSRs using shadow values.
 *
 * @data:  The &struct slc_data tracking partition information.
 */
void slc_program_lut(struct slc_data *data)
{
	int port;

	ATRACE_BEGIN(__func__);
	/* The REQ_PBHA_LUT table CSRs should have a 4 byte stride, but instead have a 32 byte
	 * stride. We must adjust our table indexing to account for this.
	 */
	lut_ioscatter32(data->req_pbha_lut, data->shadow.req_pbha_lut, 8);

	for (port = 0; port < NUM_GPU_DATA_PORTS; ++port) {
		lut_ioscatter32_nonzero(data->ra_lut[port], data->shadow.ra_lut, 1);
	}

	ATRACE_END();
}

static void partition_transition_worker(struct work_struct *work)
{
	struct slc_data *data = container_of(work, struct slc_data, transition_work);

	if (slc_transition(&data->partition, PENDING_ENABLE, ENABLED)) {
		pt_client_enable(data->pt_handle, data->partition.index);
	} else if (slc_transition(&data->partition, PENDING_DISABLE, DISABLED)) {
		pt_client_disable_no_free(data->pt_handle, data->partition.index);
	}
}

/**
 * slc_init_data - Read all SLC partition information, init the partitions, and track within @data.
 *
 * @data:  The &struct slc_data tracking partition information.
 * @dev:   The platform device associated with the parent node.
 *
 * Return: On success, returns 0. On failure an error code is returned.
 */
int slc_init_data(struct slc_data *data, struct device *dev)
{
	int ret = -EINVAL;

	if (data == NULL || dev == NULL)
		goto err_exit;

	/* Inherit the platform device */
	data->dev = dev;

	INIT_WORK(&data->transition_work, partition_transition_worker);

	/* Register our node with the SLC driver.
	 * This detects our partitions defined within the DT.
	 */
	data->pt_handle = pt_client_register(data->dev->of_node, NULL, NULL);
	if (IS_ERR(data->pt_handle)) {
		ret = PTR_ERR(data->pt_handle);
		dev_err(data->dev, "pt_client_register failed with: %d\n", ret);
		if (ret == -ENOENT)  // gslc's probe is not completed yet
			ret = -EPROBE_DEFER;
		goto err_exit;
	}

	ret = init_luts(data);
	if (ret)
		goto pt_init_err_exit;

	ret = init_partition(data, &data->partition, 0);
	if (ret)
		goto pt_init_err_exit;

	init_mappings(data);

	return 0;

pt_init_err_exit:
	pt_client_unregister(data->pt_handle);

err_exit:
	return ret;
}

/**
 * slc_term_data - Tear down SLC partitions and free tracking data.
 *
 * @data:  The &struct slc_data tracking partition information.
 */
void slc_term_data(struct slc_data *data)
{
	/* Ensure any pending transition op is complete */
	cancel_work_sync(&data->transition_work);

	pt_client_disable(data->pt_handle, data->partition.index);

	pt_client_unregister(data->pt_handle);
}

/**
 * slc_enable - Enable SLC partition asynchronously.
 *
 * @data:  The &struct slc_data tracking partition information.
 *
 * No-op if the partition is already enabled or pending enablement.
 */
void slc_enable(struct slc_data *data)
{
	if (slc_transition(&data->partition, DISABLED, PENDING_ENABLE)) {
		queue_work(system_highpri_wq, &data->transition_work);
	} else if (slc_transition(&data->partition, PENDING_DISABLE, ENABLED)) {
		cancel_work(&data->transition_work);
	}
}

/**
 * slc_disable - Disable SLC partition asynchronously.
 *
 * @data:  The &struct slc_data tracking partition information.
 *
 * No-op if the partition is already disabled or pending disablement.
 */
void slc_disable(struct slc_data *data)
{
	if (slc_transition(&data->partition, ENABLED, PENDING_DISABLE)) {
		queue_work(system_highpri_wq, &data->transition_work);
	} else if (slc_transition(&data->partition, PENDING_ENABLE, DISABLED)) {
		cancel_work(&data->transition_work);
	}
}
