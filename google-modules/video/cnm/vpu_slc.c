// SPDX-License-Identifier: GPL-2.0-only
/*
 * SLC operations for Codec3P
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Vinay Kalia <vinaykalia@google.com>
 * Author: Wen Chang Liu <wenchangliu@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/module.h>

#include "vpu_slc.h"

int vpu_ra_sid_set_pid(struct vpu_core *core, int slc_option)
{
	struct platform_device *pdev = to_platform_device(core->dev);
	struct device *dev = &pdev->dev;
	struct google_ra *ra0, *ra1;
	int i, sid, pid, rc = 0;
	static const unsigned char C3P_SID_LIST[] = {
		C3P_SID(4, 0, 0), C3P_SID(4, 1, 0), C3P_SID(0, 0, 1), C3P_SID(0, 1, 1),
		C3P_SID(0, 0, 2), C3P_SID(0, 1, 2), C3P_SID(0, 0, 3), C3P_SID(0, 1, 3),
		C3P_SID(0, 0, 4), C3P_SID(0, 1, 4), C3P_SID(0, 0, 5), C3P_SID(0, 1, 5),
		C3P_SID(0, 0, 6), C3P_SID(0, 1, 6), C3P_SID(0, 0, 7), C3P_SID(0, 1, 7)
	};

	ra0 = get_google_ra_by_index(dev, 0);
	if (IS_ERR(ra0)) {
		dev_err(dev, "Failed to get vpu ra0\n");
		return PTR_ERR(ra0);
	}

	ra1 = get_google_ra_by_index(dev, 1);

	if (IS_ERR(ra1)) {
		dev_err(dev, "Failed to get vpu ra1\n");
		return PTR_ERR(ra1);
	}

	/* SID -> PID Mapping */
	for (i = 0; i < sizeof(C3P_SID_LIST); i++) {
		sid = C3P_SID_LIST[i];
		if (((slc_option & VPU_SLC_OPTION_FW_VCPU) && (sid == C3P_SID(4, 0, 0))) ||
		    ((slc_option & VPU_SLC_OPTION_FW_VCPU) && (sid == C3P_SID(4, 1, 0))) ||
		    ((slc_option & VPU_SLC_OPTION_BITSTREAM_VCPU) && (sid == C3P_SID(0, 0, 1))) ||
		    ((slc_option & VPU_SLC_OPTION_BITSTREAM_VCPU) && (sid == C3P_SID(0, 1, 1))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_VCPU) && (sid == C3P_SID(0, 0, 2))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_VCPU) && (sid == C3P_SID(0, 1, 2))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_VCPU) && (sid == C3P_SID(0, 0, 3))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_VCPU) && (sid == C3P_SID(0, 1, 3))) ||
		    ((slc_option & VPU_SLC_OPTION_PIXEL_CORE) && (sid == C3P_SID(0, 0, 5))) ||
		    ((slc_option & VPU_SLC_OPTION_PIXEL_CORE) && (sid == C3P_SID(0, 1, 5))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_CORE) && (sid == C3P_SID(0, 0, 6))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_CORE) && (sid == C3P_SID(0, 1, 6)))) {
			pid = core->slc.pid;
			dev_dbg(dev, "Mapping ra0 C3P SID: 0x%x -> SLC PID: %d\n", sid, pid);
		} else {
			pid = 0;
		}

		rc = google_ra_sid_set_pid(ra0, sid, pid /* rpid */, pid /* wpid */);
		if (rc) {
			dev_err(dev, "Failed to set sid: %d -> pid: %d on ra0, rc: %d\n",
				sid, pid, rc);
			return rc;
		}

		if (((slc_option & VPU_SLC_OPTION_PIXEL_CORE) && (sid == C3P_SID(0, 0, 4))) ||
		    ((slc_option & VPU_SLC_OPTION_PIXEL_CORE) && (sid == C3P_SID(0, 1, 4))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_CORE) && (sid == C3P_SID(0, 0, 7))) ||
		    ((slc_option & VPU_SLC_OPTION_METADATA_CORE) && (sid == C3P_SID(0, 1, 7)))) {
			pid = core->slc.pid;
			dev_dbg(dev, "Mapping ra1 C3P SID: 0x%x -> SLC PID: %d\n", sid, pid);
		} else {
			pid = 0;
		}

		rc = google_ra_sid_set_pid(ra1, sid, pid /* rpid */, pid /* wpid */);
		if (rc) {
			dev_err(dev, "Failed to set sid: %d -> pid: %d on ra1, rc: %d\n",
				sid, pid, rc);
			return rc;
		}
	}
	return rc;
}

int vpu_pt_client_enable(struct vpu_core *core)
{
	int rc = 0;
	int slc_option = DEFAULT_SLC_OPTION;

	if (!core->slc.pt_hnd || core->debugfs.slc_disable)
		return 0;

	core->slc.pid = pt_client_enable(core->slc.pt_hnd, 0);
	if (core->slc.pid < 0) {
		pr_warn("Failed to get vpu pid\n");
		return core->slc.pid;
	}
	pr_info("Enabled SLC pid: %d\n", core->slc.pid);

	if (core->debugfs.slc_option)
		slc_option = core->debugfs.slc_option;
	pr_info("SLC option: %x\n", slc_option);

	rc = vpu_ra_sid_set_pid(core, slc_option);
	if (rc)
		vpu_pt_client_disable(core);
	return rc;
}

void vpu_pt_client_disable(struct vpu_core *core)
{
	if (core->slc.pt_hnd && core->slc.pid != PT_PTID_INVALID) {
		pt_client_disable(core->slc.pt_hnd, 0);
		pr_info("Disabled SLC pid: %d\n", core->slc.pid);
		core->slc.pid = PT_PTID_INVALID;
	}
}

int vpu_pt_client_register(struct device_node *node, struct vpu_core *core)
{
	int rc = 0;

	core->slc.pt_hnd = pt_client_register(node, NULL, NULL);
	core->slc.pid = PT_PTID_INVALID;
	if (IS_ERR(core->slc.pt_hnd)) {
		rc = PTR_ERR(core->slc.pt_hnd);
		core->slc.pt_hnd = NULL;
		pr_warn("Failed to register pt_client.\n");
		return rc;
	}
	return rc;
}

void vpu_pt_client_unregister(struct vpu_core *core)
{
	pt_client_unregister(core->slc.pt_hnd);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vinay Kalia <vinaykalia@google.com>");
