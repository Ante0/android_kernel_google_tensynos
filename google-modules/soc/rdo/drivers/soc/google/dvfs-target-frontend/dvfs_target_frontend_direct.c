// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 */

#include <dvfs-helper/google_dvfs_helper.h>
#include <linux/bitfield.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/pm_domain.h>
#include <linux/spinlock.h>
#include <perf/mbfs.h>
#include <soc/google/goog_power_controller.h>

#include "dvfs_target_frontend_direct.h"

/* CPMFS nodes */
#define TARGET_FE_DOMAINS_MBFS_PATH "cpm/pwr/dvfs/tgt_fe"
#define TARGET_FE_PFSM_CFG_ADDRESS "csr.cfg"
#define TARGET_FE_PFSM_START_ADDRESS "csr.start"
#define TARGET_FE_PFSM_STATUS_ADDRESS "csr.status"
#define TARGET_FE_PFSM_OUTSTANDING_PF_ADDRESS "csr.outstand_pf"
#define TARGET_FE_PF_LEVEL_INIT_ADDRESS "pf_level.init"
#define TARGET_FE_DOMAIN_NAME_MAX_LEN 8

/* LPCM PFSM attributes */
#define PFSM_REG_SIZE 0x04
#define PFSM_CFG_PFSM_MODE_FIELD BIT(0)
#define PFSM_CFG_SW_PFS_TARGET_FIELD GENMASK(5, 1)
#define PFSM_START_START_FIELD BIT(0)
#define PFSM_STATUS_CURR_STATE_FIELD GENMASK(4, 0)
#define PFSM_STATUS_STATE_VALID_FIELD BIT(5)
#define PFSM_STATUS_PFSM_STS_FIELD BIT(6)
#define PFSM_STATUS_SW_SEQ_DONE_FIELD BIT(7)
#define PFSM_STATUS_PFSM_ERR_FIELD BIT(8)
#define PFSM_STATUS_SEQ_ERR_FIELD BIT(9)
#define PFSM_OUTSTANDING_PF_STATE_PENDING_FIELD BIT(0)
#define PFSM_OUTSTANDING_PF_STATE_OUTSTANDING_FIELD GENMASK(5, 1)

/*
 * Number of subsequent polls and delay required to confirm that PFSM is idle.
 * Source: b/414282372#comment48
 */
#define PFSM_IDLE_POLL_NUMBER 4
#define PFSM_IDLE_TIMEOUT_US 1000
#define PFSM_IDLE_INTERVAL_US 0

#define PD_NAME_MAX_SIZE 32

struct pfsm_registers {
	void __iomem *cfg_addr;
	void __iomem *start_addr;
	void __iomem *status_addr;
	void __iomem *outstanding_pf_addr;
};

struct target_fe_domain_info {
	const char *name;
	struct pfsm_registers pfsm_regs;
	uint8_t pf_level_cached;
	enum power_state pd_state;
	spinlock_t lock;
	struct notifier_block genpd_nb;
};

/* Common dev for logging */
struct device *dev;

static struct target_fe_domain_info target_fe_domains[PWRBLK_MAX_ID];

static void _dvfs_fe_set_pf_level_direct(struct target_fe_domain_info *domain_info);

/*
 * GenPD notifiers are used to inform target frontend about changes of power state.
 * It prevents from unpowered access of LPCM registers.
 * When domain is off desired pf_level is cached and later set on power on.
 */
static int genpd_notifier_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	unsigned long flags;
	struct target_fe_domain_info *domain_info =
		container_of(nb, struct target_fe_domain_info, genpd_nb);

	dev_dbg(dev, "%s: Received notification: %lu\n",
		domain_info->name, action);

	spin_lock_irqsave(&domain_info->lock, flags);
	switch (action) {
	case GENPD_NOTIFY_ON:
		domain_info->pd_state = PD_STATE_ON;
		_dvfs_fe_set_pf_level_direct(domain_info);
		break;
	case GENPD_NOTIFY_PRE_OFF:
		domain_info->pd_state = PD_STATE_OFF;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&domain_info->lock, flags);


	return NOTIFY_OK;
}

static struct target_fe_domain_info *get_domain_info(uint8_t domain_id)
{
	if (domain_id >= PWRBLK_MAX_ID)
		return ERR_PTR(-EINVAL);

	return &target_fe_domains[domain_id];
}

/* Helpers to poll for idle PFSM, according to b/414282372#comment48 */
struct pfsm_idle_helper {
	bool idle;
	int pfsm_status_reg_val;
};

static struct pfsm_idle_helper
pfsm_status_idle_helper(struct pfsm_registers *pfsm_regs)
{
	struct pfsm_idle_helper ret;
	int pfsm_status_reg_val[PFSM_IDLE_POLL_NUMBER];

	for (int i = 0; i < PFSM_IDLE_POLL_NUMBER; i++)
		pfsm_status_reg_val[i] = readl(pfsm_regs->status_addr);

	for (int i = 0; i < PFSM_IDLE_POLL_NUMBER; i++) {
		if (pfsm_status_reg_val[i] & PFSM_STATUS_PFSM_STS_FIELD) {
			ret.idle = false;
			ret.pfsm_status_reg_val = pfsm_status_reg_val[i];
			return ret;
		}
	}

	ret.idle = true;
	ret.pfsm_status_reg_val =
		pfsm_status_reg_val[PFSM_IDLE_POLL_NUMBER - 1];

	return ret;
}

/* Set pf level on LPCM, based on b/414282372#comment48 */
static void lpcm_set_pf_level(struct target_fe_domain_info *domain_info,
			      uint8_t pf_level)
{
	uint32_t pfsm_cfg, pfsm_start;
	struct pfsm_registers *pfsm_regs = &domain_info->pfsm_regs;
	struct pfsm_idle_helper pfsm_idle;

	readx_poll_timeout(pfsm_status_idle_helper, pfsm_regs, pfsm_idle,
			   pfsm_idle.idle == true, PFSM_IDLE_INTERVAL_US,
			   PFSM_IDLE_TIMEOUT_US);

	if (!pfsm_idle.idle || (pfsm_idle.pfsm_status_reg_val &
				PFSM_STATUS_STATE_VALID_FIELD) == 0) {
		dev_err(dev, "LPCM initial idle poll timeout, status: 0x%X\n",
			pfsm_idle.pfsm_status_reg_val);
		return;
	}

	pfsm_cfg = FIELD_PREP(PFSM_CFG_PFSM_MODE_FIELD, 0b1) |
		   FIELD_PREP(PFSM_CFG_SW_PFS_TARGET_FIELD, pf_level);
	writel(pfsm_cfg, pfsm_regs->cfg_addr);

	pfsm_start = FIELD_PREP(PFSM_START_START_FIELD, 0b1);
	writel(pfsm_start, pfsm_regs->start_addr);

	readx_poll_timeout(pfsm_status_idle_helper, pfsm_regs, pfsm_idle,
			   pfsm_idle.idle == true, PFSM_IDLE_INTERVAL_US,
			   PFSM_IDLE_TIMEOUT_US);

	if (!pfsm_idle.idle || (pfsm_idle.pfsm_status_reg_val &
				PFSM_STATUS_STATE_VALID_FIELD) == 0) {
		dev_err(dev, "LPCM final idle poll timeout, status: 0x%X\n",
			pfsm_idle.pfsm_status_reg_val);
		return;
	}

	if (FIELD_GET(PFSM_STATUS_PFSM_ERR_FIELD,
		      pfsm_idle.pfsm_status_reg_val) == 1 ||
	    FIELD_GET(PFSM_STATUS_SEQ_ERR_FIELD,
		      pfsm_idle.pfsm_status_reg_val) == 1) {
		dev_err(dev, "LPCM ERR field set, status: 0x%X\n",
			pfsm_idle.pfsm_status_reg_val);
		return;
	}

	dev_dbg(dev, "LPCM set pf level success %d for domain %s\n",
		pf_level, domain_info->name);
}

/* Get pf level from LPCM */
static uint8_t lpcm_get_pf_level(struct target_fe_domain_info *domain_info)
{
	uint32_t pfsm_status = readl(domain_info->pfsm_regs.status_addr);

	dev_dbg(dev, "LPCM pf level get %d for domain %s\n",
		(int)FIELD_GET(PFSM_STATUS_CURR_STATE_FIELD, pfsm_status),
		domain_info->name);

	return FIELD_GET(PFSM_STATUS_CURR_STATE_FIELD, pfsm_status);
}

/* Check if pf level is outstanding on LPCM */
static uint8_t
lpcm_is_level_outstanding(struct target_fe_domain_info *domain_info,
			  uint8_t pf_level)
{
	uint32_t reg_val = readl(domain_info->pfsm_regs.outstanding_pf_addr);

	dev_dbg(dev, "LPCM outstanding pf level get 0X%X for domain %s\n",
		(int)reg_val, domain_info->name);

	return FIELD_GET(PFSM_OUTSTANDING_PF_STATE_PENDING_FIELD, reg_val) &&
	       (FIELD_GET(PFSM_OUTSTANDING_PF_STATE_OUTSTANDING_FIELD,
			  reg_val) == pf_level);
}

/* Initialize domain info, fill LPCM addresses, init pf level */
static int initialize_domain(int domain_id, const char *domain_name)
{
	union mbfs_client_handle domain_folder_h;
	enum mbfs_error_code mbfs_ret;
	union val64 file_content;
	int ret = 0;
	struct target_fe_domain_info *domain_info;
	char domain_path[sizeof(TARGET_FE_DOMAINS_MBFS_PATH) + // base path length plus '\0'
					1 + // for '/'
					TARGET_FE_DOMAIN_NAME_MAX_LEN];

	/* Get domain handle */
	domain_info = get_domain_info(domain_id);
	if (IS_ERR(domain_info))
		return PTR_ERR(domain_info);

	domain_info->name = domain_name;
	domain_info->genpd_nb.notifier_call = genpd_notifier_cb;

	/* Prepare MBFS config path */
	scnprintf(domain_path, sizeof(domain_path), "%s/%s",
		 TARGET_FE_DOMAINS_MBFS_PATH, domain_name);
	mbfs_ret = mbfs_get_handle(domain_path, &domain_folder_h);
	if (mbfs_ret) {
		dev_err(dev, "MBFS, Failed to get handle for %s, err %d\n",
			domain_path, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	/* Read config provided by CPM */
	mbfs_ret = mbfs_read_child_by_name(domain_folder_h,
					   TARGET_FE_PFSM_CFG_ADDRESS,
					   &file_content);
	if (mbfs_ret) {
		dev_err(dev, "MBFS, Failed to read %s, err %d\n",
			TARGET_FE_PFSM_CFG_ADDRESS, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	domain_info->pfsm_regs.cfg_addr =
		devm_ioremap(dev, file_content.number, PFSM_REG_SIZE);
	if (!domain_info->pfsm_regs.cfg_addr)
		return -ENOMEM;

	mbfs_ret = mbfs_read_child_by_name(domain_folder_h,
					   TARGET_FE_PFSM_START_ADDRESS,
					   &file_content);
	if (mbfs_ret) {
		dev_err(dev, "MBFS, Failed to read %s, err %d\n",
			TARGET_FE_PFSM_START_ADDRESS, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	domain_info->pfsm_regs.start_addr =
		devm_ioremap(dev, file_content.number, PFSM_REG_SIZE);
	if (!domain_info->pfsm_regs.start_addr)
		return -ENOMEM;


	mbfs_ret = mbfs_read_child_by_name(domain_folder_h,
					   TARGET_FE_PFSM_STATUS_ADDRESS,
					   &file_content);
	if (mbfs_ret) {
		dev_err(dev, "MBFS, Failed to read %s, err %d\n",
			TARGET_FE_PFSM_STATUS_ADDRESS, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	domain_info->pfsm_regs.status_addr =
		devm_ioremap(dev, file_content.number, PFSM_REG_SIZE);
	if (!domain_info->pfsm_regs.status_addr)
		return -ENOMEM;

	mbfs_ret = mbfs_read_child_by_name(domain_folder_h,
					   TARGET_FE_PFSM_OUTSTANDING_PF_ADDRESS,
					   &file_content);
	if (mbfs_ret) {
		dev_err(dev, "MBFS, Failed to read %s, err %d\n",
			TARGET_FE_PFSM_OUTSTANDING_PF_ADDRESS, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	domain_info->pfsm_regs.outstanding_pf_addr =
		devm_ioremap(dev, file_content.number, PFSM_REG_SIZE);
	if (!domain_info->pfsm_regs.outstanding_pf_addr)
		return -ENOMEM;

	mbfs_ret = mbfs_read_child_by_name(domain_folder_h,
					   TARGET_FE_PF_LEVEL_INIT_ADDRESS,
					   &file_content);
	if (mbfs_ret) {
		dev_err(dev, "MBFS, Failed to read %s, err %d\n",
			TARGET_FE_PF_LEVEL_INIT_ADDRESS, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	domain_info->pf_level_cached = file_content.number;
	spin_lock_init(&domain_info->lock);

	return ret;
}

/* Read and initialize target domains provided by CPM */
static int initialize_available_domains(void)
{
	union mbfs_client_handle parent_handle;
	struct mbfs_client_node_desc parent_desc;
	union mbfs_client_handle child_handle;
	struct mbfs_client_node_desc child_desc;
	enum mbfs_error_code mbfs_ret;
	struct dvfs_domain_info *domain_info;
	int ret = 0;

	mbfs_ret = mbfs_get_handle(TARGET_FE_DOMAINS_MBFS_PATH, &parent_handle);
	if (mbfs_ret) {
		dev_err(dev, "MBFS: Failed to get handle %s, err %d\n",
			TARGET_FE_DOMAINS_MBFS_PATH, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	mbfs_ret = mbfs_get_node_desc(parent_handle, &parent_desc);
	if (mbfs_ret) {
		dev_err(dev, "MBFS: Failed to get descriptor for parent %s, err %d\n",
			TARGET_FE_DOMAINS_MBFS_PATH, mbfs_ret);
		return mbfs_error2linux(mbfs_ret);
	}

	for (uint32_t i = 0; i < parent_desc.num_subfolders; i++) {
		mbfs_ret = mbfs_get_nth_child(parent_handle, MBFS_FOLDER, i,
					      &child_handle);
		if (mbfs_ret) {
			dev_err(dev, "MBFS: Failed to get %u-th child handle, err %d\n",
				i, mbfs_ret);
			return mbfs_error2linux(mbfs_ret);
		}

		mbfs_ret = mbfs_get_node_desc(child_handle, &child_desc);
		if (mbfs_ret) {
			dev_err(dev, "MBFS: Failed to get descriptor for child %u, err %d\n",
				i, mbfs_ret);
			return mbfs_error2linux(mbfs_ret);
		}

		domain_info = dvfs_helper_get_domain(child_desc.name);
		if (!domain_info) {
			dev_err(dev, "No data in dvfs helper for domain '%s'\n",
				child_desc.name);
			return -EINVAL;
		}

		ret = initialize_domain(domain_info->pwrblk_id,
					domain_info->name);
		if (ret < 0) {
			dev_err(dev, "Failed to initialize domain %s (%d), err %d\n",
				domain_info->name, domain_info->pwrblk_id, ret);
			return ret;
		}

		dev_info(dev, "Initialized domain %s (%d)\n", domain_info->name,
			 domain_info->pwrblk_id);
	}
	return ret;
}

/* Utilize power controller API to read init state and register to GenPD */
int dvfs_fe_prepare_domain_direct(int domain_id)
{
	int ret;
	struct target_fe_domain_info *domain_info = get_domain_info(domain_id);
	char full_name[PD_NAME_MAX_SIZE];
	unsigned long flags;

	snprintf(full_name, PD_NAME_MAX_SIZE, "sswrp_%s_pd",
		 domain_info->name);

	ret = register_pd_notifier_by_name(full_name, &domain_info->genpd_nb);
	/*
	 * Function returns -EINVAL for domains not registered to GenPD,
	 * meaning they are always on and won't get notifications.
	 */
	if (ret == -EINVAL) {
		domain_info->pd_state = PD_STATE_ON;
	} else if (ret) {
		dev_warn(dev, "Failed to add notifier for PM domain %s\n",
			 full_name);
		return ret;
	}

	spin_lock_irqsave(&domain_info->lock, flags);
	ret = get_pd_state_by_name(full_name, &domain_info->pd_state);
	spin_unlock_irqrestore(&domain_info->lock, flags);
	return ret;
}

/*
 * Private set function, writes to LPCM only if the domain is powered on
 * and the requested PF level is not already outstanding.
 */
static void _dvfs_fe_set_pf_level_direct(struct target_fe_domain_info *domain_info)
{
	if (domain_info->pd_state != PD_STATE_ON) {
		dev_dbg(dev, "Domain %s offline, pf level %d cached\n",
			domain_info->name, domain_info->pf_level_cached);
		return;
	}

	if (lpcm_is_level_outstanding(domain_info, domain_info->pf_level_cached)) {
		dev_dbg(dev, "PF level %d is already outstanding for domain %s\n",
			domain_info->pf_level_cached, domain_info->name);
		return;
	}

	lpcm_set_pf_level(domain_info, domain_info->pf_level_cached);
}

void dvfs_fe_set_pf_level_direct(int domain_id, uint8_t pf_level)
{
	unsigned long flags;
	struct target_fe_domain_info *domain_info = get_domain_info(domain_id);

	if (IS_ERR(domain_info)) {
		dev_err(dev, "Set PF level error: %ld", PTR_ERR(domain_info));
		return;
	}

	spin_lock_irqsave(&domain_info->lock, flags);
	domain_info->pf_level_cached = pf_level;
	_dvfs_fe_set_pf_level_direct(domain_info);
	spin_unlock_irqrestore(&domain_info->lock, flags);
}

uint8_t dvfs_fe_get_pf_level_direct(int domain_id)
{
	uint8_t ret_val = 0;
	unsigned long flags;
	struct target_fe_domain_info *domain_info = get_domain_info(domain_id);

	if (IS_ERR(domain_info))
		return PTR_ERR(domain_info);

	spin_lock_irqsave(&domain_info->lock, flags);
	if (domain_info->pd_state != PD_STATE_ON) {
		dev_dbg(dev,
			"Domain %d is offline, get cached pf level %d\n",
			domain_id, domain_info->pf_level_cached);
		ret_val = domain_info->pf_level_cached;
	} else {
		ret_val = lpcm_get_pf_level(domain_info);
	}
	spin_unlock_irqrestore(&domain_info->lock, flags);

	return ret_val;
}

int dvfs_target_frontend_direct_init(struct device *target_dev)
{
	int ret;

	if (!target_dev)
		return -ENODEV;

	dev = target_dev;

	ret = initialize_available_domains();
	if (ret < 0) {
		dev_err(dev, "Failed to initialize DVFS domains, err %d.\n",
			ret);
		return ret;
	}

	return 0;
}

void dvfs_target_frontend_direct_exit(void)
{
	dev = NULL;
}

MODULE_AUTHOR("Karol Radwan <kradwan@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DVFS Target Frontend Direct");
