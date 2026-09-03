// SPDX-License-Identifier: GPL-2.0-only
/*
 * Interface to the GSLC PT driver
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <soc/google/pt.h>

#include "gslc_cpm_mba.h"
#include "gslc_partition.h"
#include "gslc_pt_ops.h"

enum pt_property_index {
	PT_PROPERTY_INDEX_CFG = 0,
	PT_PROPERTY_INDEX_SIZE = 1,
	PT_PROPERTY_INDEX_PRIORITY = 2,
	PT_PROPERTY_INDEX_PBHA = 3
};

static ptid_t gslc_cpm_alloc(void *data, int property_index, void *resize_data,
			     void (*resize)(void *data, size_t size));
static void gslc_cpm_free(void *data, ptid_t ptid);
static void gslc_cpm_enable(void *data, ptid_t ptid);
static void gslc_cpm_disable(void *data, ptid_t ptid);
static int gslc_cpm_mutate(void *data, ptid_t ptid, void *resize_data,
			   int new_property_index);
static int gslc_cpm_pbha(void *data, ptid_t ptid);
static int gslc_cpm_ioctl(void *data, int arg_cnt, int *args);

static struct pt_ops gslc_cpm_ops = {
	.alloc = gslc_cpm_alloc,
	.free = gslc_cpm_free,
	.enable = gslc_cpm_enable,
	.disable = gslc_cpm_disable,
	.mutate = gslc_cpm_mutate,
	.pbha = gslc_cpm_pbha,
	.ioctl = gslc_cpm_ioctl,
};

static int gslc_cpm_ioctl(void *data, int arg_cnt, int *args)
{
	/* TODO(b/290227649) Replace the debugfs with this interface */
	return 0;
}

/** gslc_cpm_apply_pid_changes() - Modifies a partition using the mutate command
 *  @gslc_dev:	The GSLC platform device.
 *  @ptid:	The partition ID that needs to be changed
 *  @zero_size:	Flag to indicate if partition size needs to be 0 or actual size
 *
 *  @return: Valid ptid on success, PT_PTID_INVALID on failure
 */
static int gslc_cpm_apply_pid_changes(struct gslc_dev *gslc_dev,
				      ptid_t ptid, bool zero_size)
{
	int mut_pid;
	struct gslc_mba_raw_msg req = { 0 };
	struct gslc_partition_mutate_req *mut_req =
				(struct gslc_partition_mutate_req *)(&req);
	if (ptid < 0 || ptid > PT_PTID_MAX)
		return PT_PTID_INVALID;

	mut_req->cmd = CMD_ID_PARTITION_MUTATE;
	mut_req->cfg = gslc_dev->pid_data[ptid].cfg;
	mut_req->priority = gslc_dev->pid_data[ptid].priority;
	mut_req->pid = ptid;
	mut_req->size = zero_size ? 0 : gslc_dev->pid_data[ptid].size;
	mut_pid = gslc_client_partition_req(gslc_dev, &req);
	if (mut_pid != ptid) {
		dev_err(gslc_dev->dev, "Mutated PID mismatch Expected: %d, Received: %d\n",
			ptid,
			mut_pid);
		return PT_PTID_INVALID;
	}
	return ptid;
}

/** gslc_cpm_alloc() - Allocates a partition ID
 *  @data:		GSLC platform device
 *  @property_index:	DT property index
 *  @resize_data:	Resize callback data
 *  @resize:		Resize callback for the partition
 *
 * Return:  Valid partition ID on success, PT_PTID_INVALID on error
 * Life of a PID (partition):
 * - gslc_cpm_alloc will create/allocate PID
 * - gslc_cpm_disable will move its size to zero.
 * - gslc_cpm_enable will move its size to the expected one from a disabled state
 * - gslc_cpm_free will destroy/free it.
 */
static ptid_t gslc_cpm_alloc(void *data, int property_index, void *resize_data,
			     void (*resize)(void *data, size_t size))
{
	struct gslc_dev *gslc_dev = (struct gslc_dev *)data;
	u32 cfg;
	u32 size;
	u32 priority;
	ptid_t ptid;
	unsigned long flags;
	struct gslc_mba_raw_msg req = { 0 };
	struct gslc_partition_en_req *en_req = (struct gslc_partition_en_req *)(&req);

	if (pt_driver_get_property_value(gslc_dev->pt_driver, property_index,
					 PT_PROPERTY_INDEX_CFG, &cfg) < 0)
		return PT_PTID_INVALID;
	if (pt_driver_get_property_value(gslc_dev->pt_driver, property_index,
					 PT_PROPERTY_INDEX_SIZE, &size) < 0)
		return PT_PTID_INVALID;
	if (pt_driver_get_property_value(gslc_dev->pt_driver, property_index,
					 PT_PROPERTY_INDEX_PRIORITY, &priority) < 0)
		return PT_PTID_INVALID;

	en_req->cmd = CMD_ID_PARTITION_ENABLE;
	en_req->cfg = cfg;
	en_req->priority = priority;
	en_req->size = size;
	ptid = gslc_client_partition_req(gslc_dev, &req);

	if (ptid < 0 || ptid > PT_PTID_MAX)
		return PT_PTID_INVALID;

	spin_lock_irqsave(&gslc_dev->pid_lock, flags);
	gslc_dev->pid_data[ptid].cfg = cfg;
	gslc_dev->pid_data[ptid].priority = priority;
	gslc_dev->pid_data[ptid].size = size;
	gslc_dev->pid_data[ptid].data = resize_data;
	gslc_dev->pid_data[ptid].resize = resize;
	spin_unlock_irqrestore(&gslc_dev->pid_lock, flags);

	/* TODO(b/195123651): Check for partition size changes*/
	dev_dbg(gslc_dev->dev, "Allocated ptid %d\n", ptid);
	return (int)ptid;
}

/** gslc_cpm_mutate() - Modifies a partition using the mutate command
 *  @data:		GSLC platform device
 *  @ptid:		Partition ID to be modified
 *  @resize_data:	Resize callback data
 *  @new_property_index:	DT property index
 */
static ptid_t gslc_cpm_mutate(void *data, ptid_t ptid, void *resize_data,
			      int new_property_index)
{
	struct gslc_dev *gslc_dev = (struct gslc_dev *)data;
	u32 cfg;
	u32 size;
	u32 priority;

	if (ptid < 0 || ptid > PT_PTID_MAX)
		return PT_PTID_INVALID;

	if (pt_driver_get_property_value(gslc_dev->pt_driver, new_property_index,
					 PT_PROPERTY_INDEX_CFG, &cfg) < 0)
		return PT_PTID_INVALID;
	if (pt_driver_get_property_value(gslc_dev->pt_driver, new_property_index,
					 PT_PROPERTY_INDEX_SIZE, &size) < 0)
		return PT_PTID_INVALID;
	if (pt_driver_get_property_value(gslc_dev->pt_driver, new_property_index,
					 PT_PROPERTY_INDEX_PRIORITY, &priority) < 0)
		return PT_PTID_INVALID;

	gslc_dev->pid_data[ptid].data = resize_data;
	gslc_dev->pid_data[ptid].cfg = cfg;
	gslc_dev->pid_data[ptid].size = size;
	gslc_dev->pid_data[ptid].priority = priority;
	ptid = gslc_cpm_apply_pid_changes(gslc_dev, ptid, false);

	/* TODO(b/195123651): Check for partition size changes*/
	return ptid;
}

static ptpbha_t gslc_cpm_pbha(void *data, ptid_t ptid)
{
	/* Not used */
	return 0;
}

/** gslc_cpm_free() - Frees a partition using the disable command
 *  @data:	GSLC platform device
 *  @ptid:	Partition ID to be modified
 */
static void gslc_cpm_free(void *data, ptid_t ptid)
{
	struct gslc_dev *gslc_dev = (struct gslc_dev *)data;
	unsigned long flags;
	u8 dis_pid = 0;
	struct gslc_mba_raw_msg req = { 0 };
	struct gslc_partition_dis_req *dis_req =
			(struct gslc_partition_dis_req *)(&req);
	if (ptid < 0 || ptid > PT_PTID_MAX)
		return;

	dis_req->cmd = CMD_ID_PARTITION_DISABLE;
	dis_req->pid = ptid;
	dis_pid = gslc_client_partition_req(gslc_dev, &req);

	WARN_ON(dis_pid != ptid);

	/* TODO(b/195123651): Check for partition size changes*/

	spin_lock_irqsave(&gslc_dev->pid_lock, flags);
	memset(&gslc_dev->pid_data[ptid], 0, sizeof(gslc_dev->pid_data[ptid]));
	spin_unlock_irqrestore(&gslc_dev->pid_lock, flags);
}

/** gslc_cpm_enable() - Enables a partition using the mutate command
 *  (PID is allocated using the alloc cmd. Enable only changes the size)
 *  @data:	GSLC platform device
 *  @ptid:	Partition ID to be modified
 */
static void gslc_cpm_enable(void *data, ptid_t ptid)
{
	struct gslc_dev *gslc_dev = (struct gslc_dev *)data;

	ptid = gslc_cpm_apply_pid_changes(gslc_dev, ptid, false);

	/* TODO(b/195123651): Check for partition size changes*/

	dev_dbg(gslc_dev->dev, "Enabled ptid %d\n", ptid);
}

/** gslc_cpm_disable() - Disables a partition using the mutate command
 *  (PID is freed using the free cmd. Disable only changes the size to zero)
 *  @data:	GSLC platform device
 *  @ptid:	Partition ID to be modified
 */
static void gslc_cpm_disable(void *data, ptid_t ptid)
{
	struct gslc_dev *gslc_dev = (struct gslc_dev *)data;

	gslc_cpm_apply_pid_changes(gslc_dev, ptid, true);

	/* TODO(b/195123651): Check for partition size changes*/
}

/** gslc_cpm_pt_driver_init() - Registers the pt driver
 *  @gslc_dev:	The GSLC platform device.
 */
int gslc_cpm_pt_driver_init(struct gslc_dev *gslc_dev)
{
	gslc_dev->pt_driver = pt_driver_register(gslc_dev->dev->of_node,
						 &gslc_cpm_ops, gslc_dev);
	WARN_ON(!gslc_dev->pt_driver);

	/* TODO(b/290174682): Integrate with GSLC GEM performance monitoring */

	return 0;
}
