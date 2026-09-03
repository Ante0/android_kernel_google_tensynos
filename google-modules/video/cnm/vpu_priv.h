/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022 Google LLC.
 *
 * Author: Vinay Kalia <vinaykalia@google.com>
 */

#ifndef _VPU_PRIV_H_
#define _VPU_PRIV_H_

#include <linux/cdev.h>
#include <linux/pm_qos.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/kref.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/platform_data/sscoredump.h>
#include <linux/reset.h>

#if IS_ENABLED(CONFIG_VPU_ICC)
#include <interconnect/google_icc_helper.h>
#endif

#include "uapi/linux/vpu.h"

#include "wave6_regdefine.h"
#include "vpu_debugfs.h"
#include "vpu_secure.h"

#if IS_ENABLED(CONFIG_SLC_PARTITION_MANAGER)
#include <soc/google/pt.h>
#endif

#define MAX_NUM_INST 32
#define MAX_INTERRUPT_QUEUE 16

struct vpu_slc_manager {
#if IS_ENABLED(CONFIG_SLC_PARTITION_MANAGER)
	struct pt_handle *pt_hnd;
	ptid_t pid;
#endif
	size_t size;
};

struct vpu_icc_path {
	struct google_icc_path *path_vpu;
	struct google_icc_path *path_c3p;
};

struct vpu_dmabuf_info {
	struct list_head list;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	dma_addr_t iova;
	dma_addr_t pa;
	struct iosys_map vmap;
	size_t size;
	int fd;
	uint32_t skip_cmo;
};

struct vpu_dmabuf_list {
       struct list_head allocs;
       struct list_head mappings;
       struct mutex lock;
};

struct vpu_intr_queue {
	wait_queue_head_t wq;
	struct kfifo intr_pending_q;
	spinlock_t kfifo_lock;
};

struct vpu_devfreq {
	struct devfreq *df;
	struct dev_pm_qos_request qos_req;
};

enum vpu_power_status {
	POWER_OFF_RELEASED,
	POWER_ON,
	POWER_OFF_SLEEP,
};

struct vpu_restore_regs {
	u32 cmd_regs[W6_CMD_REG_COUNT];
	u32 vint_enable;
};

struct vpu_core {
	struct class *_class;
	struct cdev cdev;
	struct device *svc_dev;
	struct device *dev;
	struct device *pd_dev;
	dev_t devno;
	const char *fw_name;
	int irq;
	struct vpu_devfreq dev_freq;
	struct vpu_icc_path icc_path;
	unsigned int regs_size;
	phys_addr_t paddr;
	void __iomem *base;
	void __iomem *qch_base;
	unsigned int qchctl_active_mask_0_offset;
	void __iomem *lpcm_base;
	struct wakeup_source *init_wakelock;
	struct mutex lock;
	int inst_count;
	spinlock_t inst_lock;
	struct vpu_inst *instances[MAX_NUM_INST];
	struct vpu_dmabuf_list dmabuf_list[MAX_NUM_INST];
	struct vpu_dmabuf_info *fw_buf;
	struct vpu_dmabuf_info *fw_debug_buf;
	struct list_head allocs;
	struct vpu_debugfs debugfs;
	struct vpu_secure secure;
	struct vpu_slc_manager slc;
	struct vpu_intr_queue intr_queue;
	enum vpu_power_status power_status;
	struct notifier_block genpd_nb;
	bool vpu_power_cycled;
	bool user_idle;
	bool need_reload_fw;
	struct platform_device sscd_pdev;
	struct sscd_platform_data vpu_core_sscd_platdata;
	struct vpu_restore_regs restore_regs;
	struct reset_control *vpu_reset;
};

struct vpu_inst {
	struct vpu_core *core;
	uint32_t idx;
	struct vpu_intr_queue intr_queue;
	struct kref refcount;
};

#endif //_VPU_PRIV_H_
