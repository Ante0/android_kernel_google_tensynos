// SPDX-License-Identifier: GPL-2.0
/*
 * pKVM host driver for the Arm SMMUv3
 *
 * Copyright (C) 2022 Linaro Ltd.
 */
#include <asm/kvm_pkvm.h>
#include <asm/kvm_mmu.h>

#include <arm-smmu-v3/io-pgtable.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/panic_notifier.h>
#include <linux/pci.h>
#include <linux/pixel-dma-iommu.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "arm-smmu-v3.h"
#include "arm-smmu-v3-common-telemetry.h"
#include "pkvm/arm_smmu_v3.h"

extern struct kvm_iommu_ops kvm_nvhe_sym(smmu_ops);


#ifdef MODULE
static unsigned long                   pkvm_module_token;

#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x)) *)(pkvm_el2_mod_va(&kvm_nvhe_sym(x), pkvm_module_token)))

int kvm_nvhe_sym(smmu_init_hyp_module)(const struct pkvm_module_ops *ops);
#else
#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x)) *)(kern_hyp_va(lm_alias(&kvm_nvhe_sym(x)))))
#endif

static size_t				kvm_arm_smmu_cur;
static size_t				kvm_arm_smmu_count;
static struct hyp_arm_smmu_v3_device	*kvm_arm_smmu_array;
static struct hyp_arm_smmu_v3_err	*kvm_arm_smmu_v3_err;
static struct host_arm_smmu_device	**host_arm_smmu_array;
static DEFINE_IDA(kvm_arm_smmu_domain_ida);
static DEFINE_PER_CPU(local_lock_t, err_lock) = INIT_LOCAL_LOCK(err_lock);

#define UNFINALIZED_DOMAIN		(-1)
static struct hyp_shared_arm_smmu_telemetry *kvm_shared_arm_smmu_telemetry;
/*
 * Pre allocated pages that can be used from the EL2 part of the driver from atomic
 * context, ideally used for page table pages for identity domains.
 */
static int atomic_pages;
module_param(atomic_pages, int, 0);

/*
 * Load pKVM SMMUv3 module, but without probing, so the kernel driver takes over
 * control over the SMMUs, this is useful during bring up, where we can have prebuilts
 * that can run with and without the module and can just be toggled from kernel cmdline.
 */
static bool disable;
module_param(disable, bool, 0);

/* Use SMC to notify TZ about stage-2 identity map modifications. */
static bool smc_s2;
module_param(smc_s2, bool, 0);

static struct platform_driver kvm_arm_smmu_driver;

static struct arm_smmu_device *
kvm_arm_smmu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev;

	dev = driver_find_device_by_fwnode(&kvm_arm_smmu_driver.driver, fwnode);
	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static struct iommu_ops kvm_arm_smmu_ops;
static const struct iommu_domain_ops kvm_arm_smmu_nested_domain_ops;

static int kvm_arm_smmu_streams_cmp_key(const void *lhs, const struct rb_node *rhs)
{
	struct kvm_arm_smmu_stream *stream_rhs = rb_entry(rhs, struct kvm_arm_smmu_stream, node);
	const u32 *sid_lhs = lhs;

	if (*sid_lhs < stream_rhs->id)
		return -1;
	if (*sid_lhs > stream_rhs->id)
		return 1;
	return 0;
}

static int kvm_arm_smmu_streams_cmp_node(struct rb_node *lhs, const struct rb_node *rhs)
{
	return kvm_arm_smmu_streams_cmp_key(&rb_entry(lhs, struct kvm_arm_smmu_stream, node)->id,
					    rhs);
}

static int kvm_arm_smmu_insert_master(struct arm_smmu_device *smmu,
				      struct kvm_arm_smmu_master *master)
{
	int i;
	int ret = 0;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(master->dev);

	master->streams = kcalloc(fwspec->num_ids, sizeof(*master->streams), GFP_KERNEL);
	if (!master->streams)
		return -ENOMEM;
	master->num_streams = fwspec->num_ids;

	mutex_lock(&smmu->streams_mutex);
	for (i = 0; i < fwspec->num_ids; i++) {
		struct kvm_arm_smmu_stream *new_stream = &master->streams[i];
		struct rb_node *existing;
		u32 sid = fwspec->ids[i];

		new_stream->id = sid;
		new_stream->master = master;

		existing = rb_find_add(&new_stream->node, &smmu->streams,
				       kvm_arm_smmu_streams_cmp_node);
		if (existing) {
			struct kvm_arm_smmu_master *existing_master = rb_entry(existing,
									struct kvm_arm_smmu_stream,
									node)->master;

			/* Bridged PCI devices may end up with duplicated IDs */
			if (existing_master == master)
				continue;

			dev_warn(master->dev,
				 "Aliasing StreamID 0x%x (from %s) unsupported, expect DMA to be broken\n",
				 sid, dev_name(existing_master->dev));
			ret = -ENODEV;
			break;
		}
	}

	if (ret) {
		for (i--; i >= 0; i--)
			rb_erase(&master->streams[i].node, &smmu->streams);
		kfree(master->streams);
	}
	mutex_unlock(&smmu->streams_mutex);

	return ret;
}

static void kvm_arm_smmu_remove_master(struct kvm_arm_smmu_master *master)
{
	int i;
	struct arm_smmu_device *smmu = master->smmu;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(master->dev);

	if (!smmu || !master->streams)
		return;

	mutex_lock(&smmu->streams_mutex);
	for (i = 0; i < fwspec->num_ids; i++)
		rb_erase(&master->streams[i].node, &smmu->streams);
	mutex_unlock(&smmu->streams_mutex);

	kfree(master->streams);
}

static int kvm_arm_smmu_parse_tcu_prefetch(struct device *of_dev,
					   struct kvm_arm_smmu_master *master,
					   bool iommu_tcu_prefetch_forward)
{
	u32 *sids;
	int count;
	int ret;
	const char *iommu_tcu_prefetch_type = iommu_tcu_prefetch_forward ?
						"iommu-tcu-prefetch-forward-sids" :
						"iommu-tcu-prefetch-backward-sids";

	count = of_property_count_u32_elems(of_dev->of_node, iommu_tcu_prefetch_type);
	if (count <= 0)
		return 0;

	sids = kcalloc(count, sizeof(*sids), GFP_KERNEL);
	if (!sids)
		return -ENOMEM;

	ret = of_property_read_u32_array(of_dev->of_node, iommu_tcu_prefetch_type, sids, count);
	if (ret)
		goto out;

	for (int i = 0; i < count; i++) {
		for (int j = 0; j < master->num_streams; j++) {
			if (sids[i] == master->streams[j].id) {
				if (iommu_tcu_prefetch_forward)
					master->streams[j].tcu_prefetch_forward = true;
				else
					master->streams[j].tcu_prefetch_backward = true;
				break;
			}
		}
	}

out:
	kfree(sids);
	return ret;
}

static int kvm_arm_smmu_parse_tcu_prefetch_forward(struct device *of_dev,
						   struct kvm_arm_smmu_master *master)
{
	return kvm_arm_smmu_parse_tcu_prefetch(of_dev, master, true);
}

static int kvm_arm_smmu_parse_tcu_prefetch_backward(struct device *of_dev,
						    struct kvm_arm_smmu_master *master)
{
	return kvm_arm_smmu_parse_tcu_prefetch(of_dev, master, false);
}

static struct iommu_device *kvm_arm_smmu_probe_device(struct device *dev)
{
	struct device *of_dev = dev;
	struct device *bridge = NULL;
	struct arm_smmu_device *smmu;
	struct kvm_arm_smmu_master *master;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int ret;

	if (WARN_ON_ONCE(dev_iommu_priv_get(dev)))
		return ERR_PTR(-EBUSY);

	smmu = kvm_arm_smmu_get_by_fwnode(fwspec->iommu_fwnode);
	if (!smmu)
		return ERR_PTR(-ENODEV);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	master->dev = dev;
	master->smmu = smmu;

	ret = kvm_arm_smmu_insert_master(smmu, master);
	if (ret)
		goto err_free_master;

	if (dev_is_pci(dev)) {
		bridge = arm_smmu_pci_get_host_bridge_device(to_pci_dev(dev));

		if (bridge->parent)
			of_dev = bridge->parent;
	}

	device_property_read_u32(of_dev, "pasid-num-bits", &master->ssid_bits);
	master->ssid_bits = min(smmu->ssid_bits, master->ssid_bits);
	xa_init(&master->domains);
	master->idmapped = device_property_read_bool(of_dev, "iommu-idmapped");
	master->force_cacheable = device_property_read_bool(of_dev, "iommu-force-cacheable");
	master->single_page_size = device_property_read_bool(of_dev, "iommu-single-page-size");
	master->nested_translations = device_property_read_bool(of_dev,
								"iommu-nested-translations");
	master->non_coherent_ttw = device_property_read_bool(of_dev, "iommu-non-coherent-ttw");

	ret = kvm_arm_smmu_parse_tcu_prefetch_forward(of_dev, master);
	if (ret)
		goto err_put_bridge;

	ret = kvm_arm_smmu_parse_tcu_prefetch_backward(of_dev, master);
	if (ret)
		goto err_put_bridge;

	if (bridge)
		arm_smmu_pci_put_host_bridge_device(bridge);

	dev_iommu_priv_set(dev, master);
	if (!device_link_add(dev, smmu->dev,
			     DL_FLAG_PM_RUNTIME |
			     DL_FLAG_AUTOREMOVE_SUPPLIER)) {
		ret = -ENOLINK;
		goto err_remove_master;
	}

	return &smmu->iommu;

err_put_bridge:
	if (bridge)
		arm_smmu_pci_put_host_bridge_device(bridge);
err_remove_master:
	kvm_arm_smmu_remove_master(master);
err_free_master:
	kfree(master);
	return ERR_PTR(ret);
}

static struct kvm_arm_smmu_domain *__kvm_arm_smmu_domain_alloc(void)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = kzalloc(sizeof(*kvm_smmu_domain), GFP_KERNEL);

	if (!kvm_smmu_domain)
		return ERR_PTR(-ENOMEM);

	mutex_init(&kvm_smmu_domain->init_mutex);
	spin_lock_init(&kvm_smmu_domain->masters_lock);

	if (arm_smmu_domain_telemetry_alloc(kvm_smmu_domain, PKVM_MODE_DRIVER)) {
		kfree(kvm_smmu_domain);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Initialize domain_id of just allocated domains to -1. This will be updated later
	 * during domain attach time.
	 */
	arm_smmu_dom_tlm_rec_domain_id(kvm_smmu_domain->telemetry, UNFINALIZED_DOMAIN, false);

	return kvm_smmu_domain;
}

static struct iommu_domain *kvm_arm_smmu_domain_alloc(unsigned int type)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain;

	if (type != IOMMU_DOMAIN_IDENTITY &&
	    type != IOMMU_DOMAIN_BLOCKED)
		return ERR_PTR(-EOPNOTSUPP);

	kvm_smmu_domain = __kvm_arm_smmu_domain_alloc();
	if (IS_ERR(kvm_smmu_domain))
		return ERR_CAST(kvm_smmu_domain);

	return &kvm_smmu_domain->domain;
}

static struct iommu_domain *kvm_arm_smmu_domain_alloc_paging(struct device *dev)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = __kvm_arm_smmu_domain_alloc();
	struct kvm_arm_smmu_master *master;

	if (IS_ERR(kvm_smmu_domain))
		return ERR_CAST(kvm_smmu_domain);

	if (dev) {
		master = dev_iommu_priv_get(dev);
		if (master->nested_translations)
			kvm_smmu_domain->domain.ops = &kvm_arm_smmu_nested_domain_ops;
	}

	return &kvm_smmu_domain->domain;
}

static bool kvm_arm_smmu_is_domain_nested(struct kvm_arm_smmu_domain *kvm_smmu_domain)
{
	return kvm_smmu_domain->domain.ops == &kvm_arm_smmu_nested_domain_ops;
}

/*
 * These TLB maintenance functions are only called for nested domains as defined in the function
 * above either by the IOMMU core or the io-pgtable layer in the kernel, which is also only
 * used for nested domains.
 */
static void kvm_arm_smmu_iotlb_sync(struct iommu_domain *domain, struct iommu_iotlb_gather *gather)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);

	if (!gather->pgsize)
		return;

	kvm_iommu_iotlb_inv_nested_domain(kvm_smmu_domain->id, gather->start,
					  gather->end - gather->start + 1, gather->pgsize, true);
}

static void kvm_arm_smmu_tlb_inv_context(void *cookie)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = cookie;

	/*
	 * This can be called by the io-pgtable core via free_io_pgtable_ops() in case of an error
	 * in kvm_arm_smmu_domain_finalize() before the domain has been fully setup, so catch that
	 * case by testing for the smmu pointer field.
	 *
	 * iova 0 and size ULONG_MAX translates to a TLB flush for this domain, so none of the
	 * subsequent parameters are relevant.
	 */
	if (kvm_smmu_domain->smmu)
		kvm_iommu_iotlb_inv_nested_domain(kvm_smmu_domain->id, 0, ULONG_MAX, 0, false);
}

static void kvm_arm_smmu_tlb_inv_walk(unsigned long iova, size_t size,
				      size_t granule, void *cookie)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = cookie;

	kvm_iommu_iotlb_inv_nested_domain(kvm_smmu_domain->id, iova, size, granule, false);
}

static void kvm_arm_smmu_tlb_inv_page_nosync(struct iommu_iotlb_gather *gather,
					     unsigned long iova, size_t granule,
					     void *cookie)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = cookie;
	struct iommu_domain *domain = &kvm_smmu_domain->domain;

	if (gather)
		iommu_iotlb_gather_add_page(domain, gather, iova, granule);
}

static const struct iommu_flush_ops kvm_arm_smmu_flush_ops = {
	.tlb_flush_all	= kvm_arm_smmu_tlb_inv_context,
	.tlb_flush_walk	= kvm_arm_smmu_tlb_inv_walk,
	.tlb_add_page	= kvm_arm_smmu_tlb_inv_page_nosync,
};

static int kvm_arm_smmu_domain_finalize(struct kvm_arm_smmu_domain *kvm_smmu_domain,
					struct kvm_arm_smmu_master *master)
{
	int ret = 0;
	struct arm_smmu_device *smmu = master->smmu;
	unsigned int max_domains;
	enum kvm_arm_smmu_domain_type type;
	struct io_pgtable_cfg cfg;
	struct io_pgtable_ops *pgtbl_ops = NULL;
	unsigned long ias;
	bool nested = kvm_arm_smmu_is_domain_nested(kvm_smmu_domain);

	if (kvm_smmu_domain->smmu && (kvm_smmu_domain->smmu != smmu))
		return -EINVAL;

	if (kvm_smmu_domain->smmu)
		return 0;

	if (kvm_smmu_domain->domain.type == IOMMU_DOMAIN_IDENTITY) {
		kvm_smmu_domain->id = KVM_IOMMU_DOMAIN_IDMAP_ID;
		/*
		 * Identity domains doesn't use the DMA API, so no need to
		 * set the  domain aperture.
		 */
		goto out;
	}

	if (nested && !(smmu->features & ARM_SMMU_FEAT_NESTING))
		return -EINVAL;

	/*
	 * Default to stage-1 and use it for nested domains as well, since the kernel's IOMMU API
	 * can only control the stage-1 mappings.
	 */
	if (smmu->features & ARM_SMMU_FEAT_TRANS_S1) {
		unsigned long quirks = 0;
		bool coherent_walk;
		struct device *iommu_dev = smmu->dev;

		if (master->single_page_size)
			quirks |= IO_PGTABLE_QUIRK_DISABLE_CONTPTE;

		coherent_walk = (smmu->features & ARM_SMMU_FEAT_COHERENCY) &&
				!(smmu->options & ARM_SMMU_OPT_NON_COHERENT_TTW) &&
				!master->non_coherent_ttw;
		if ((smmu->features & ARM_SMMU_FEAT_COHERENCY) && !coherent_walk) {
			iommu_dev = &smmu->non_coherent_dev;
			dev_info(master->dev, "SMMU is using non-coherent stage 1 translation table\n");
		}

		ias = (smmu->features & ARM_SMMU_FEAT_VAX) ? 52 : 48;
		cfg = (struct io_pgtable_cfg) {
			.fmt = ARM_64_LPAE_S1,
			.quirks = quirks,
			.pgsize_bitmap = smmu->pgsize_bitmap,
			.ias = min_t(unsigned long, ias, VA_BITS),
			.oas = smmu->ias,
			.coherent_walk = coherent_walk,
			.tlb = &kvm_arm_smmu_flush_ops,
			.iommu_dev = iommu_dev,
		};

		if (nested) {
			pgtbl_ops = alloc_io_pgtable_ops_pixel(cfg.fmt, &cfg, kvm_smmu_domain);
			if (!pgtbl_ops)
				return -ENOMEM;

			type = KVM_ARM_SMMU_DOMAIN_NESTED;
		} else {
			ret = io_pgtable_configure_pixel(&cfg);
			if (ret)
				return ret;

			type = KVM_ARM_SMMU_DOMAIN_S1;
		}
		kvm_smmu_domain->domain.pgsize_bitmap = cfg.pgsize_bitmap;
		kvm_smmu_domain->domain.geometry.aperture_end = (1UL << cfg.ias) - 1;
		max_domains = 1 << smmu->asid_bits;
	} else {
		cfg = (struct io_pgtable_cfg) {
			.fmt = ARM_64_LPAE_S2,
			.pgsize_bitmap = smmu->pgsize_bitmap,
			.ias = smmu->ias,
			.oas = smmu->oas,
			.coherent_walk = smmu->features & ARM_SMMU_FEAT_COHERENCY,
		};
		ret = io_pgtable_configure_pixel(&cfg);
		if (ret)
			return ret;

		type = KVM_ARM_SMMU_DOMAIN_S2;
		kvm_smmu_domain->domain.pgsize_bitmap = cfg.pgsize_bitmap;
		kvm_smmu_domain->domain.geometry.aperture_end = (1UL << cfg.ias) - 1;
		max_domains = 1 << smmu->vmid_bits;
	}
	kvm_smmu_domain->domain.geometry.force_aperture = true;

	if (master->single_page_size)
		kvm_smmu_domain->domain.pgsize_bitmap &= (SZ_4K | SZ_16K | SZ_64K);

	/*
	 * The hypervisor uses the domain_id for asid/vmid so it has to be
	 * unique, and it has to be in range of this smmu, which can be
	 * either 8 or 16 bits.
	 */
	ret = ida_alloc_range(&kvm_arm_smmu_domain_ida, KVM_SMMU_DOMAIN_NR_START,
			      min(KVM_IOMMU_MAX_DOMAINS, max_domains), GFP_KERNEL);
	if (ret < 0) {
		free_io_pgtable_ops_pixel(pgtbl_ops);
		return ret;
	}

	kvm_smmu_domain->id = ret;

	ret = kvm_iommu_alloc_domain(kvm_smmu_domain->id, type);
	if (ret) {
		ida_free(&kvm_arm_smmu_domain_ida, kvm_smmu_domain->id);
		free_io_pgtable_ops_pixel(pgtbl_ops);
		return ret;
	}

out:
	arm_smmu_dom_tlm_rec_domain_id(kvm_smmu_domain->telemetry, kvm_smmu_domain->id, nested);
	kvm_smmu_domain->pgtbl_ops = pgtbl_ops;
	kvm_smmu_domain->smmu = smmu;
	return ret;
}

static void kvm_arm_smmu_domain_free(struct iommu_domain *domain)
{
	int ret;
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);
	struct arm_smmu_device *smmu = kvm_smmu_domain->smmu;

	if (smmu && (kvm_smmu_domain->domain.type != IOMMU_DOMAIN_IDENTITY)) {
		free_io_pgtable_ops_pixel(kvm_smmu_domain->pgtbl_ops);
		ret = kvm_iommu_free_domain(kvm_smmu_domain->id);
		ida_free(&kvm_arm_smmu_domain_ida, kvm_smmu_domain->id);
	}

	arm_smmu_domain_telemetry_free(kvm_smmu_domain, PKVM_MODE_DRIVER);
	kfree(kvm_smmu_domain);
}

static void *kvm_arm_smmu_alloc_hyp_shared_page(void)
{
	void *addr;
	int ret;

	addr = (void *)__get_free_page(GFP_KERNEL);
	if (!addr)
		return ERR_PTR(-ENOMEM);

	ret = kvm_call_hyp_nvhe(__pkvm_host_share_hyp, virt_to_pfn(addr), 1);
	if (ret) {
		free_page((unsigned long)addr);
		return ERR_PTR(ret);
	}

	return addr;
}

static void kvm_arm_smmu_free_hyp_shared_page(void *buf)
{
	if (!buf)
		return;

	WARN_ON(kvm_call_hyp_nvhe(__pkvm_host_unshare_hyp, virt_to_pfn((void *)buf), 1));
	free_page((unsigned long)buf);
}

static int kvm_arm_smmu_sync_cd_entry(struct kvm_arm_smmu_master *master,
				      struct hyp_arm_smmu_v3_cmd *cmd, u32 ssid)
{
	struct host_arm_smmu_device *host_smmu = smmu_to_host(master->smmu);
	int i, ret = 0;

	cmd->data[0] = cpu_to_le64(FIELD_PREP(CMDQ_0_OP, CMDQ_OP_CFGI_CD) |
				   FIELD_PREP(CMDQ_CFGI_0_SSID, ssid));

	cmd->data[1] = cpu_to_le64(FIELD_PREP(CMDQ_CFGI_1_LEAF, true));

	for (i = 0; i < master->num_streams; i++) {
		int sid = master->streams[i].id;

		cmd->data[0] &= cpu_to_le64(~CMDQ_CFGI_0_SID);
		cmd->data[0] |= cpu_to_le64(FIELD_PREP(CMDQ_CFGI_0_SID, sid));
		ret = kvm_iommu_nested_cfg_sync(host_smmu->id, cmd, sizeof(*cmd));
		if (ret)
			return ret;
	}

	return ret;
}

static int kvm_arm_smmu_write_cd_entry(struct kvm_arm_smmu_master *master, u32 ssid,
				       struct arm_smmu_cd *cdptr, const struct arm_smmu_cd *target)
{
	struct hyp_arm_smmu_v3_cmd *cmd = NULL;
	int ret = 0;

	if (ssid) {
		cmd = kvm_arm_smmu_alloc_hyp_shared_page();
		if (IS_ERR(cmd))
			return PTR_ERR(cmd);

		cdptr->data[3] = target->data[3];
		cdptr->data[2] = target->data[2];
		cdptr->data[1] = target->data[1];

		/* STE is live. */
		ret = kvm_arm_smmu_sync_cd_entry(master, cmd, ssid);
		if (ret)
			goto out;

		WRITE_ONCE(cdptr->data[0], target->data[0]);

		ret = kvm_arm_smmu_sync_cd_entry(master, cmd, ssid);
		if (ret)
			goto out;

		master->cd_table.used_ssids++;
	} else {
		cdptr->data[3] = target->data[3];
		cdptr->data[2] = target->data[2];
		cdptr->data[1] = target->data[1];
		WRITE_ONCE(cdptr->data[0], target->data[0]);
	}

out:
	kvm_arm_smmu_free_hyp_shared_page(cmd);
	return ret;
}

static int kvm_arm_smmu_alloc_cd_tables(struct kvm_arm_smmu_master *master)
{
	size_t l1size;
	size_t max_contexts;
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	cd_table->s1cdmax = master->ssid_bits;
	max_contexts = 1 << cd_table->s1cdmax;

	cd_table->s1fmt = STRTAB_STE_0_S1FMT_LINEAR;
	cd_table->linear.num_ents = max_contexts;

	l1size = max_contexts * sizeof(struct arm_smmu_cd);
	cd_table->linear.table = dma_alloc_coherent(smmu->dev, l1size,
						    &cd_table->cdtab_dma,
						    GFP_KERNEL);
	if (!cd_table->linear.table)
		return -ENOMEM;

	return 0;
}

struct arm_smmu_cd *kvm_arm_smmu_get_cd_ptr(struct kvm_arm_smmu_master *master, u32 ssid)
{
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	if (!arm_smmu_cdtab_allocated(cd_table))
		return NULL;

	return &cd_table->linear.table[ssid];
}

static struct arm_smmu_cd *kvm_arm_smmu_alloc_cd_ptr(struct kvm_arm_smmu_master *master, u32 ssid)
{
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	might_sleep();
	iommu_group_mutex_assert(master->dev);

	if (!arm_smmu_cdtab_allocated(cd_table)) {
		if (kvm_arm_smmu_alloc_cd_tables(master))
			return NULL;
	}

	/* Only support linear CD tables for now. */
	return kvm_arm_smmu_get_cd_ptr(master, ssid);
}

static void kvm_arm_smmu_free_cd_tables(struct kvm_arm_smmu_master *master)
{
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	dma_free_coherent(smmu->dev, cd_table->linear.num_ents * sizeof(struct arm_smmu_cd),
			  cd_table->linear.table, cd_table->cdtab_dma);
}

static int kvm_arm_smmu_clear_cd_entry(struct kvm_arm_smmu_master *master, int ssid)
{
	struct hyp_arm_smmu_v3_cmd *cmd;
	struct arm_smmu_cd *cdptr = kvm_arm_smmu_get_cd_ptr(master, ssid);
	int ret;

	if (!cdptr)
		return -EINVAL;

	cmd = kvm_arm_smmu_alloc_hyp_shared_page();
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

	cdptr->data[0] = 0;
	ret = kvm_arm_smmu_sync_cd_entry(master, cmd, ssid);
	if (ret)
		return ret;

	cdptr->data[1] = 0;
	cdptr->data[2] = 0;
	cdptr->data[3] = 0;

	ret = kvm_arm_smmu_sync_cd_entry(master, cmd, ssid);
	if (!ret)
		master->cd_table.used_ssids--;
	kvm_arm_smmu_free_hyp_shared_page(cmd);
	return ret;
}

static int kvm_arm_smmu_detach_dev_pasid(struct host_arm_smmu_device *host_smmu,
					 struct kvm_arm_smmu_master *master,
					 ioasid_t pasid)
{
	int i, ret;
	unsigned long flags;
	struct arm_smmu_device *smmu = &host_smmu->smmu;
	struct kvm_arm_smmu_domain *domain = xa_load(&master->domains, pasid);

	if (!domain)
		return 0;

	spin_lock_irqsave(&domain->masters_lock, flags);
	for (i = 0; i < MAX_MASTER_DEVICES_PER_DOMAIN; i++) {
		if (domain->masters[i] == master) {
			domain->masters[i] = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&domain->masters_lock, flags);
	if (kvm_arm_smmu_is_domain_nested(domain)) {
		/* Ensure other pasids are detached. */
		if (pasid == 0 && master->cd_table.used_ssids)
			return -EINVAL;

		ret = kvm_arm_smmu_clear_cd_entry(master, pasid);
		if (ret)
			dev_err(smmu->dev, "cannot clear CD entry for device %s: %d\n",
				dev_name(master->dev), ret);
	}

	for (i = 0; i < master->num_streams; i++) {
		int sid = master->streams[i].id;

		if (kvm_arm_smmu_is_domain_nested(domain))
			ret = kvm_iommu_detach_dev_nested(host_smmu->id, domain->id, sid, pasid);
		else
			ret = kvm_iommu_detach_dev(host_smmu->id, domain->id, sid, pasid);
		if (ret) {
			dev_err(smmu->dev, "cannot detach device %s (0x%x): %d\n",
				dev_name(master->dev), sid, ret);
			break;
		}
	}

	/*
	 * smmu->streams_mutex is taken to provide synchronization with respect to
	 * kvm_arm_smmu_handle_event(), since that acquires the same lock. Taking the
	 * lock makes domain removal atomic with respect to domain usage when reporting
	 * faults related to a domain to an IOMMU client driver. This makes it so that
	 * the domain doesn't go away while it is being used in the fault reporting
	 * logic.
	 */
	mutex_lock(&smmu->streams_mutex);
	xa_erase(&master->domains, pasid);
	mutex_unlock(&smmu->streams_mutex);

	return ret;
}

static int kvm_arm_smmu_detach_dev(struct host_arm_smmu_device *host_smmu,
				   struct kvm_arm_smmu_master *master)
{
	return kvm_arm_smmu_detach_dev_pasid(host_smmu, master, 0);
}

static void kvm_arm_smmu_remove_dev_pasid(struct device *dev, ioasid_t pasid,
					  struct iommu_domain *domain)
{
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct host_arm_smmu_device *host_smmu = smmu_to_host(master->smmu);

	kvm_arm_smmu_detach_dev_pasid(host_smmu, master, pasid);
}

static void kvm_arm_smmu_release_device(struct device *dev)
{
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct host_arm_smmu_device *host_smmu = smmu_to_host(master->smmu);

	kvm_arm_smmu_detach_dev(host_smmu, master);
	xa_destroy(&master->domains);
	kvm_arm_smmu_remove_master(master);
	if (arm_smmu_cdtab_allocated(&master->cd_table))
		kvm_arm_smmu_free_cd_tables(master);
	kfree(master);
	iommu_fwspec_free(dev);
}

static struct hyp_arm_smmu_v3_s1_ctx *kvm_arm_smmu_prepare_ste(struct kvm_arm_smmu_master *master,
							       struct kvm_arm_smmu_domain *domain,
							       ioasid_t pasid)
{
	struct arm_smmu_cd cd_target;
	struct arm_smmu_ste ste_target;
	const struct io_pgtable_cfg *pgtbl_cfg =
		&io_pgtable_ops_to_pgtable(domain->pgtbl_ops)->cfg;
	struct arm_smmu_cd *cdptr;
	struct hyp_arm_smmu_v3_s1_ctx *s1_ctx;
	int ret;

	/* PASID 0 is the first to initialize the CD table. */
	if (pasid == 0) {
		cdptr = kvm_arm_smmu_alloc_cd_ptr(master, pasid);
		if (!cdptr)
			return ERR_PTR(-ENOMEM);
	} else {
		cdptr = kvm_arm_smmu_get_cd_ptr(master, pasid);
		if (!cdptr)
			return ERR_PTR(-EINVAL);
	}

	s1_ctx = kvm_arm_smmu_alloc_hyp_shared_page();
	if (IS_ERR(s1_ctx))
		return s1_ctx;

	/* Stall is not supported at the moment. */
	arm_smmu_make_s1_cd_common(&cd_target, pgtbl_cfg, false, domain->id);

	ret = kvm_arm_smmu_write_cd_entry(master, pasid, cdptr, &cd_target);
	if (ret) {
		kvm_arm_smmu_free_hyp_shared_page(s1_ctx);
		return ERR_PTR(ret);
	}

	arm_smmu_make_cdtable_ste_common(&ste_target, master->smmu, &master->cd_table, false, false,
					 STRTAB_STE_1_S1DSS_SSID0);

	s1_ctx->ste[0] = ste_target.data[0];
	s1_ctx->ste[1] = ste_target.data[1];
	return s1_ctx;
}

static int kvm_arm_smmu_set_dev_pasid(struct iommu_domain *domain,
				      struct device *dev, ioasid_t pasid)
{
	int i, ret;
	struct arm_smmu_device *smmu;
	struct host_arm_smmu_device *host_smmu;
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);
	unsigned long flags = 0;
	struct hyp_arm_smmu_v3_s1_ctx *s1_ctx = NULL;

	if (!master)
		return -ENODEV;

	smmu = master->smmu;
	host_smmu = smmu_to_host(smmu);

	ret = kvm_arm_smmu_detach_dev_pasid(host_smmu, master, pasid);
	if (ret || (domain->type == IOMMU_DOMAIN_BLOCKED))
		return ret;

	mutex_lock(&kvm_smmu_domain->init_mutex);
	ret = kvm_arm_smmu_domain_finalize(kvm_smmu_domain, master);
	mutex_unlock(&kvm_smmu_domain->init_mutex);
	if (ret)
		return ret;

	if (master->force_cacheable)
		flags |= ARM_SMMU_FORCE_CACHEABLE;

	if (master->single_page_size)
		flags |= ARM_SMMU_DISABLE_CONTPTE;

	if (kvm_arm_smmu_is_domain_nested(kvm_smmu_domain)) {
		s1_ctx = kvm_arm_smmu_prepare_ste(master, kvm_smmu_domain, pasid);
		if (IS_ERR(s1_ctx))
			return PTR_ERR(s1_ctx);

		dev_info(smmu->dev, "Attaching nested domain to iommu client: %s\n",
			 dev_name(master->dev));
	}

	for (i = 0; i < master->num_streams; i++) {
		int sid = master->streams[i].id;
		unsigned long stream_flags = flags;

		if (master->streams[i].tcu_prefetch_forward)
			stream_flags |= ARM_SMMU_TCU_PREFETCH_FORWARD;
		else if (master->streams[i].tcu_prefetch_backward)
			stream_flags |= ARM_SMMU_TCU_PREFETCH_BACKWARD;

		if (i == master->num_streams - 1)
			stream_flags |= ARM_SMMU_ATTACH_DEV_LAST_SID;

		if (kvm_arm_smmu_is_domain_nested(kvm_smmu_domain)) {
			ret = kvm_iommu_attach_dev_nested(host_smmu->id, kvm_smmu_domain->id, sid,
							  pasid, stream_flags, s1_ctx,
							  sizeof(*s1_ctx));
		} else {
			if (master->nested_translations) {
				dev_err(smmu->dev, "cannot attach single stage domain to device %s (0x%x) which is configured for nested translation\n",
					dev_name(dev), sid);
				ret = -EINVAL;
				goto out_ret;
			}
			ret = kvm_iommu_attach_dev(host_smmu->id, kvm_smmu_domain->id,
						   sid, pasid, master->ssid_bits, stream_flags);
		}
		if (ret) {
			dev_err(smmu->dev, "cannot attach device %s (0x%x): %d\n",
				dev_name(dev), sid, ret);
			goto out_ret;
		}
	}
	ret = xa_insert(&master->domains, pasid, kvm_smmu_domain, GFP_KERNEL);
	if (!ret) {
		int i;
		bool space_found = false;
		unsigned long flags;

		spin_lock_irqsave(&kvm_smmu_domain->masters_lock, flags);
		for (i = 0; i < MAX_MASTER_DEVICES_PER_DOMAIN; i++) {
			if (!kvm_smmu_domain->masters[i]) {
				kvm_smmu_domain->masters[i] = master;
				kvm_smmu_domain->pasid[i] = pasid;
				space_found = true;
				break;
			}
		}
		spin_unlock_irqrestore(&kvm_smmu_domain->masters_lock, flags);
		WARN_ON(!space_found);
	}

out_ret:
	if (ret)
		kvm_arm_smmu_detach_dev(host_smmu, master);
	kvm_arm_smmu_free_hyp_shared_page(s1_ctx);
	return ret;
}

static int kvm_arm_smmu_attach_dev(struct iommu_domain *domain,
				   struct device *dev)
{
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);
	unsigned long pasid = 0;

	/* All pasids must be removed first. */
	if (xa_find_after(&master->domains, &pasid, ULONG_MAX, XA_PRESENT))
		return -EBUSY;

	return kvm_arm_smmu_set_dev_pasid(domain, dev, 0);
}

static int kvm_arm_smmu_def_domain_type(struct device *dev)
{
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);

	if (master->idmapped && atomic_pages)
		return IOMMU_DOMAIN_IDENTITY;
	return 0;
}

static bool kvm_arm_smmu_capable(struct device *dev, enum iommu_cap cap)
{
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);

	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return master->smmu->features & ARM_SMMU_FEAT_COHERENCY;
	case IOMMU_CAP_NOEXEC:
	default:
		return false;
	}
}

static int kvm_arm_smmu_map_pages(struct iommu_domain *domain,
				  unsigned long iova, phys_addr_t paddr,
				  size_t pgsize, size_t pgcount, int prot,
				  gfp_t gfp, size_t *total_mapped)
{
	int ret;
	size_t size = pgsize * pgcount;
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);

	if (!kvm_smmu_domain->smmu)
		return -ENODEV;

	if (prot & IOMMU_GFP_KERNEL) {
		gfp = GFP_KERNEL;
		prot &= ~IOMMU_GFP_KERNEL;
	}

	arm_smmu_dom_tlm_rec_iova_pa_alignment(kvm_smmu_domain->telemetry, iova, paddr, size);
	ret = kvm_iommu_map_pages(kvm_smmu_domain->id, iova, paddr, pgsize,
				  pgcount, prot, gfp, total_mapped);
	arm_smmu_dom_tlm_map_end(kvm_smmu_domain->telemetry);

	arm_smmu_dom_tlm_rec_iova_range(kvm_smmu_domain->telemetry, iova, *total_mapped);
	return ret;
}

static int kvm_arm_smmu_map_pages_nested(struct iommu_domain *domain, unsigned long iova,
					 phys_addr_t paddr, size_t pgsize, size_t pgcount, int prot,
					 gfp_t gfp, size_t *total_mapped)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);
	struct io_pgtable_ops *ops = kvm_smmu_domain->pgtbl_ops;
	int ret;

	if (!ops)
		return -ENODEV;

	if (prot & IOMMU_GFP_KERNEL) {
		gfp = GFP_KERNEL;
		prot &= ~IOMMU_GFP_KERNEL;
	}

	ret = ops->map_pages(ops, iova, paddr, pgsize, pgcount, prot, gfp, total_mapped);
	arm_smmu_dom_tlm_map_end(kvm_smmu_domain->telemetry);

	return ret;
}

static void kvm_arm_smmu_consume_err(struct arm_smmu_device *smmu)
{
	int cpu = raw_smp_processor_id();
	struct hyp_arm_smmu_v3_err *err = &kvm_arm_smmu_v3_err[cpu];

	lockdep_assert_held(this_cpu_ptr(&err_lock));

	if (err->type == HYP_ARM_SMMU_V3_ERR_CMDQ_TIMEOUT) {
		dev_err(smmu->dev, "cmdq time out: hw prod 0x%x hw cons 0x%x sw prod 0x%llx",
		       err->cmdq_prod, err->cmdq_cons, err->cmdq_prod_sw);
		err->type = HYP_ARM_SMMU_V3_ERR_NONE; /* Consumed */
	}
}

static size_t kvm_arm_smmu_unmap_pages(struct iommu_domain *domain,
				       unsigned long iova, size_t pgsize,
				       size_t pgcount,
				       struct iommu_iotlb_gather *iotlb_gather)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);
	size_t unmapped;
	unsigned long flags;

	if (!kvm_smmu_domain->smmu)
		return 0;

	unmapped = kvm_iommu_unmap_pages(kvm_smmu_domain->id, iova, pgsize, pgcount);
	local_lock_irqsave(&err_lock, flags);
	kvm_arm_smmu_consume_err(kvm_smmu_domain->smmu);
	local_unlock_irqrestore(&err_lock, flags);
	arm_smmu_dom_tlm_unmap_end(kvm_smmu_domain->telemetry);

	return unmapped;
}

static size_t kvm_arm_smmu_unmap_pages_nested(struct iommu_domain *domain, unsigned long iova,
					      size_t pgsize, size_t pgcount,
					      struct iommu_iotlb_gather *iotlb_gather)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);
	struct io_pgtable_ops *ops = kvm_smmu_domain->pgtbl_ops;
	size_t unmapped;
	unsigned long flags;

	if (!ops)
		return 0;

	unmapped = ops->unmap_pages(ops, iova, pgsize, pgcount, iotlb_gather);
	local_lock_irqsave(&err_lock, flags);
	kvm_arm_smmu_consume_err(kvm_smmu_domain->smmu);
	local_unlock_irqrestore(&err_lock, flags);
	arm_smmu_dom_tlm_unmap_end(kvm_smmu_domain->telemetry);

	return unmapped;
}

static int kvm_arm_smmu_iotlb_sync_map(struct iommu_domain *domain,
				       unsigned long iova, size_t size)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);

	if (kvm_smmu_domain->smmu->options & ARM_SMMU_OPT_NON_COHERENT_TTW)
		kvm_call_hyp_nvhe(__pkvm_host_iommu_iotlb_sync_map,
				  kvm_smmu_domain->id, iova, size);

	return 0;
}

static phys_addr_t kvm_arm_smmu_iova_to_phys(struct iommu_domain *domain,
					     dma_addr_t iova)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);

	if (!kvm_smmu_domain->smmu)
		return 0;

	return kvm_iommu_iova_to_phys(kvm_smmu_domain->id, iova);
}

static phys_addr_t kvm_arm_smmu_iova_to_phys_nested(struct iommu_domain *domain, dma_addr_t iova)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);
	struct io_pgtable_ops *ops = kvm_smmu_domain->pgtbl_ops;

	if (!ops)
		return 0;

	return ops->iova_to_phys(ops, iova);
}

static void kvm_arm_smmu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(domain);

	if (kvm_smmu_domain->smmu)
		kvm_arm_smmu_tlb_inv_context(kvm_smmu_domain);
}

struct kvm_arm_smmu_map_sg {
	struct iommu_map_cookie_sg cookie;
	struct kvm_iommu_sg *sg;
	unsigned int ptr;
	unsigned long iova;
	unsigned long iova_moving;
	int prot;
	gfp_t gfp;
	unsigned int nents;
	size_t total_mapped;
	size_t size; /* Total size of entries not mapped yet. */
};

static struct iommu_map_cookie_sg *kvm_arm_smmu_alloc_cookie_sg(unsigned long iova,
								int prot,
								unsigned int nents,
								gfp_t gfp)
{
	int ret;
	struct kvm_arm_smmu_map_sg *map_sg;

	/*
	 * For small scatter-gather lists, the overhead of batching (allocating
	 * a page and sharing/unsharing it with the hypervisor) can outweigh
	 * the performance benefits. Since sharing/unsharing adds two
	 * hypercalls on top of the map_sg call, batching is only beneficial
	 * for lists with more than three entries.
	 */
	if (nents < 4)
		return NULL;

	if (prot & IOMMU_GFP_KERNEL) {
		gfp = GFP_KERNEL;
		prot &= ~IOMMU_GFP_KERNEL;
	}

	map_sg = kzalloc(sizeof(*map_sg), gfp);

	if (!map_sg) {
		arm_smmu_tlm_rec_failed_cookie_alloc();
		return NULL;
	}
	/* Limit list size to a single page. */
	map_sg->nents = kvm_iommu_sg_nents_round(1);
	map_sg->sg = kvm_iommu_sg_alloc(map_sg->nents, gfp);
	if (!map_sg->sg) {
		kfree(map_sg);
		arm_smmu_tlm_rec_failed_cookie_alloc();
		return NULL;
	}
	map_sg->iova = iova;
	map_sg->iova_moving = iova;
	map_sg->prot = prot;
	map_sg->gfp = gfp;
	ret = kvm_iommu_share_hyp_sg(map_sg->sg, map_sg->nents);
	if (ret) {
		kvm_iommu_sg_free(map_sg->sg, map_sg->nents);
		kfree(map_sg);
		arm_smmu_tlm_rec_failed_cookie_alloc();
		return NULL;
	}

	return &map_sg->cookie;
}

static int kvm_arm_smmu_add_deferred_map_sg(struct iommu_map_cookie_sg *cookie,
					    phys_addr_t paddr, size_t pgsize, size_t pgcount)
{
	struct kvm_arm_smmu_map_sg *map_sg = container_of(cookie, struct kvm_arm_smmu_map_sg,
							  cookie);
	struct kvm_iommu_sg *sg = map_sg->sg;
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(map_sg->cookie.domain);
	size_t mapped;
	size_t size = pgsize * pgcount;

	/* Out of space, flush the list. */
	if (map_sg->nents == map_sg->ptr) {
		arm_smmu_dom_tlm_rec_sg_len(kvm_smmu_domain->telemetry, map_sg->nents);
		mapped = kvm_iommu_map_sg(kvm_smmu_domain->id, sg, map_sg->iova, map_sg->ptr,
					  map_sg->prot, map_sg->gfp);
		/*
		 * Something went wrong, undo the mappings from the current sg list,
		 * leaving total mapped as it would be unmapped from core code as
		 * kvm_arm_smmu_consume_deferred_map_sg() would return total_mapped.
		 */
		if (mapped != map_sg->size) {
			iommu_unmap(&kvm_smmu_domain->domain, map_sg->iova, mapped);
			/*
			 * The core code will try to consume the list in the error path
			 * don't attempt to map this list again as it already failed, so
			 * no need to waste time.
			 */
			map_sg->ptr = 0;
			return -EINVAL;
		}
		arm_smmu_dom_tlm_rec_iova_range(kvm_smmu_domain->telemetry, map_sg->iova, mapped);
		map_sg->ptr = 0;
		map_sg->iova += mapped;
		map_sg->iova_moving = map_sg->iova;
		map_sg->total_mapped += mapped;
		map_sg->size = 0;
	}

	sg[map_sg->ptr].phys = paddr;
	sg[map_sg->ptr].pgsize = pgsize;
	sg[map_sg->ptr].pgcount = pgcount;
	map_sg->size += pgsize * pgcount;

	arm_smmu_dom_tlm_rec_iova_pa_alignment(kvm_smmu_domain->telemetry, map_sg->iova_moving,
					       paddr, size);

	map_sg->ptr++;
	map_sg->iova_moving += size;
	return 0;
}

static size_t kvm_arm_smmu_consume_deferred_map_sg(struct iommu_map_cookie_sg *cookie)
{
	struct kvm_arm_smmu_map_sg *map_sg = container_of(cookie, struct kvm_arm_smmu_map_sg,
							  cookie);
	size_t mapped;

	struct kvm_iommu_sg *sg = map_sg->sg;
	struct kvm_arm_smmu_domain *kvm_smmu_domain = to_kvm_smmu_domain(map_sg->cookie.domain);
	size_t total_mapped = map_sg->total_mapped;

	arm_smmu_dom_tlm_inc_map_sg_cnt(kvm_smmu_domain->telemetry);
	arm_smmu_dom_tlm_rec_sg_len(kvm_smmu_domain->telemetry, map_sg->ptr);
	/* Might be cleared from error path. */
	if (map_sg->ptr)
		total_mapped += kvm_iommu_map_sg(kvm_smmu_domain->id, sg, map_sg->iova, map_sg->ptr,
						 map_sg->prot, map_sg->gfp);

	/* Telemetry quirks */
	mapped = total_mapped - map_sg->total_mapped;
	arm_smmu_dom_tlm_rec_iova_range(kvm_smmu_domain->telemetry, map_sg->iova, mapped);
	kvm_iommu_unshare_hyp_sg(sg, map_sg->nents);
	kvm_iommu_sg_free(sg, map_sg->nents);
	kfree(map_sg);
	return total_mapped;
}

static struct iommu_ops kvm_arm_smmu_ops = {
	.capable		= kvm_arm_smmu_capable,
	.device_group		= arm_smmu_device_group,
	.of_xlate		= arm_smmu_of_xlate,
	.get_resv_regions	= arm_smmu_get_resv_regions,
	.probe_device		= kvm_arm_smmu_probe_device,
	.release_device		= kvm_arm_smmu_release_device,
	.domain_alloc_paging	= kvm_arm_smmu_domain_alloc_paging,
	.domain_alloc		= kvm_arm_smmu_domain_alloc,
	.pgsize_bitmap		= -1UL,
	.remove_dev_pasid	= kvm_arm_smmu_remove_dev_pasid,
	.owner			= THIS_MODULE,
	.def_domain_type	= kvm_arm_smmu_def_domain_type,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= kvm_arm_smmu_attach_dev,
		.free		= kvm_arm_smmu_domain_free,
		.map_pages	= kvm_arm_smmu_map_pages,
		.unmap_pages	= kvm_arm_smmu_unmap_pages,
		.iotlb_sync_map = kvm_arm_smmu_iotlb_sync_map,
		.iova_to_phys	= kvm_arm_smmu_iova_to_phys,
		.set_dev_pasid	= kvm_arm_smmu_set_dev_pasid,
		.alloc_cookie_sg = kvm_arm_smmu_alloc_cookie_sg,
		.add_deferred_map_sg = kvm_arm_smmu_add_deferred_map_sg,
		.consume_deferred_map_sg = kvm_arm_smmu_consume_deferred_map_sg,
	}
};

static const struct iommu_domain_ops kvm_arm_smmu_nested_domain_ops = {
	.attach_dev		= kvm_arm_smmu_attach_dev,
	.free			= kvm_arm_smmu_domain_free,
	.map_pages		= kvm_arm_smmu_map_pages_nested,
	.unmap_pages		= kvm_arm_smmu_unmap_pages_nested,
	.iotlb_sync		= kvm_arm_smmu_iotlb_sync,
	.iova_to_phys		= kvm_arm_smmu_iova_to_phys_nested,
	.set_dev_pasid		= kvm_arm_smmu_set_dev_pasid,
	.flush_iotlb_all	= kvm_arm_smmu_flush_iotlb_all,
};

static bool kvm_arm_smmu_validate_features(struct arm_smmu_device *smmu)
{
	unsigned int required_features =
		ARM_SMMU_FEAT_TT_LE		|
		ARM_SMMU_FEAT_COHERENCY;
	unsigned int forbidden_features =
		ARM_SMMU_FEAT_STALL_FORCE;
	unsigned int keep_features =
		ARM_SMMU_FEAT_2_LVL_STRTAB	|
		ARM_SMMU_FEAT_2_LVL_CDTAB	|
		ARM_SMMU_FEAT_TT_LE		|
		ARM_SMMU_FEAT_SEV		|
		ARM_SMMU_FEAT_COHERENCY		|
		ARM_SMMU_FEAT_TRANS_S1		|
		ARM_SMMU_FEAT_TRANS_S2		|
		ARM_SMMU_FEAT_VAX		|
		ARM_SMMU_FEAT_RANGE_INV		|
		ARM_SMMU_FEAT_PMCG_PAGE0_2000	|
		ARM_SMMU_FEAT_NESTING;
	unsigned int known_options =
		ARM_SMMU_OPT_SKIP_PREFETCH	|
		ARM_SMMU_OPT_PAGE0_REGS_ONLY	|
		ARM_SMMU_OPT_MSIPOLL		|
		ARM_SMMU_OPT_CMDQ_FORCE_SYNC	|
		ARM_SMMU_OPT_OVR_INSTCFG_DATA	|
		ARM_SMMU_OPT_RPM_DISABLE	|
		ARM_SMMU_OPT_NON_COHERENT_TTW	|
		ARM_SMMU_OPT_SYNC_FW		|
		ARM_SMMU_OPT_NON_COHERENT_S2_TTW;

	if (smmu->options & ARM_SMMU_OPT_PAGE0_REGS_ONLY) {
		dev_err(smmu->dev, "unsupported layout\n");
		return false;
	}

	if ((smmu->features & required_features) != required_features) {
		dev_err(smmu->dev, "missing features 0x%x\n",
			required_features & ~smmu->features);
		return false;
	}

	if (smmu->features & forbidden_features) {
		dev_err(smmu->dev, "features 0x%x forbidden\n",
			smmu->features & forbidden_features);
		return false;
	}

	/*
	 * At the time of writing this driver, these are the known options
	 * that are supported and understood by the driver, any new unsupported
	 * option might break the driver or undermine its security.
	 */
	if (smmu->options & ~known_options) {
		dev_err(smmu->dev, "unknown options found 0x%x\n",
			smmu->options & ~known_options);
		return false;
	}

	smmu->features &= keep_features;

	return true;
}

static struct kvm_arm_smmu_master *kvm_arm_smmu_find_master(struct arm_smmu_device *smmu, u32 sid)
{
	struct rb_node *node;

	lockdep_assert_held(&smmu->streams_mutex);

	node = rb_find(&sid, &smmu->streams, kvm_arm_smmu_streams_cmp_key);
	if (!node)
		return NULL;
	return rb_entry(node, struct kvm_arm_smmu_stream, node)->master;
}

static void kvm_arm_smmu_decode_event(struct arm_smmu_device *smmu, u64 *raw,
				      struct arm_smmu_event *event)
{
	struct kvm_arm_smmu_master *master;

	event->id = FIELD_GET(EVTQ_0_ID, raw[0]);
	event->sid = FIELD_GET(EVTQ_0_SID, raw[0]);
	event->ssv = FIELD_GET(EVTQ_0_SSV, raw[0]);
	event->ssid = event->ssv ? FIELD_GET(EVTQ_0_SSID, raw[0]) : IOMMU_NO_PASID;
	event->privileged = FIELD_GET(EVTQ_1_PnU, raw[1]);
	event->instruction = FIELD_GET(EVTQ_1_InD, raw[1]);
	event->s2 = FIELD_GET(EVTQ_1_S2, raw[1]);
	event->read = FIELD_GET(EVTQ_1_RnW, raw[1]);
	event->stag = FIELD_GET(EVTQ_1_STAG, raw[1]);
	event->stall = FIELD_GET(EVTQ_1_STALL, raw[1]);
	event->class = FIELD_GET(EVTQ_1_CLASS, raw[1]);
	event->iova = FIELD_GET(EVTQ_2_ADDR, raw[2]);
	event->ipa = raw[3] & EVTQ_3_IPA;
	event->fetch_addr = raw[3] & EVTQ_3_FETCH_ADDR;
	event->ttrnw = FIELD_GET(EVTQ_1_TT_READ, raw[1]);
	event->class_tt = false;
	event->dev = NULL;

	if (event->id == EVT_ID_PERMISSION_FAULT)
		event->class_tt = (event->class == EVTQ_1_CLASS_TT);

	mutex_lock(&smmu->streams_mutex);
	master = kvm_arm_smmu_find_master(smmu, event->sid);
	if (master)
		event->dev = get_device(master->dev);
	mutex_unlock(&smmu->streams_mutex);
}

static int kvm_arm_smmu_handle_event(struct arm_smmu_device *smmu, u64 *evt,
				     struct arm_smmu_event *event)
{
	int ret = 0;
	struct kvm_arm_smmu_master *master;
	struct kvm_arm_smmu_domain *smmu_domain;

	switch (event->id) {
	case EVT_ID_TRANSLATION_FAULT:
	case EVT_ID_ADDR_SIZE_FAULT:
	case EVT_ID_ACCESS_FAULT:
	case EVT_ID_PERMISSION_FAULT:
		break;
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&smmu->streams_mutex);
	master = kvm_arm_smmu_find_master(smmu, event->sid);
	if (!master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	smmu_domain = xa_load(&master->domains, event->ssid);
	if (!smmu_domain) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = report_iommu_fault(&smmu_domain->domain, master->dev, event->iova,
				 event->read ? IOMMU_FAULT_READ : IOMMU_FAULT_WRITE);

out_unlock:
	mutex_unlock(&smmu->streams_mutex);
	return ret;
}

static void kvm_arm_smmu_dump_ptes(struct arm_smmu_device *smmu,
				   struct arm_smmu_event *event,
				   struct ratelimit_state *rs)
{
	struct kvm_arm_smmu_master *master;
	struct kvm_arm_smmu_domain *smmu_domain;
	struct io_pgtable_ops *ops;
	struct arm_lpae_io_pgtable_walk_data wd = { 0 };
	struct io_pgtable_walk_common walk_data = {
		.data = &wd,
	};

	switch (event->id) {
	case EVT_ID_TRANSLATION_FAULT:
	case EVT_ID_ADDR_SIZE_FAULT:
	case EVT_ID_ACCESS_FAULT:
	case EVT_ID_PERMISSION_FAULT:
		break;
	default:
		return;
	}

	if (!__ratelimit(rs))
		return;

	mutex_lock(&smmu->streams_mutex);
	master = kvm_arm_smmu_find_master(smmu, event->sid);
	if (!master)
		goto out_unlock;

	smmu_domain = xa_load(&master->domains, event->ssid);
	if (!smmu_domain)
		goto out_unlock;
	if (!kvm_arm_smmu_is_domain_nested(smmu_domain))
		goto out_unlock;
	ops = smmu_domain->pgtbl_ops;
	if (!ops || !ops->pgtable_walk)
		goto out_unlock;

	ops->pgtable_walk(ops, event->iova, 1, &walk_data);
	dev_err(smmu->dev, "ptes: [0]: %#016llx [1]: %#016llx [2]: %#016llx [3]: %#016llx last level: %d\n",
		wd.ptes[0], wd.ptes[1], wd.ptes[2], wd.ptes[3], wd.level - 1);

out_unlock:
	mutex_unlock(&smmu->streams_mutex);
}
static irqreturn_t kvm_arm_smmu_evt_handler(int irq, void *dev)
{
	int ret;
	struct arm_smmu_device *smmu = dev;
	struct host_arm_smmu_device *host_smmu = smmu_to_host(smmu);
	struct arm_smmu_queue *q = &smmu->evtq.q;
	struct arm_smmu_ll_queue *llq = &q->llq;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	u64 evt[EVTQ_ENT_DWORDS];
	struct arm_smmu_event event = {0};
	struct arm_smmu_device_telemetry_common *asdevtc = host_smmu->telemetry;

	ret = pm_runtime_resume_and_get(smmu->dev);
	if (ret < 0) {
		dev_err(smmu->dev, "Failed to resume device: %d\n", ret);
		return IRQ_NONE;
	}

	do {
		while (!queue_remove_raw(q, evt)) {
			kvm_arm_smmu_decode_event(smmu, evt, &event);
			arm_smmu_dev_tlm_rec_evtq_fault(asdevtc, event.id);
			if (kvm_arm_smmu_handle_event(smmu, evt, &event)) {
				arm_smmu_dump_event(smmu, evt, &event, &rs);
				kvm_arm_smmu_dump_ptes(smmu, &event, &rs);
			}
			put_device(event.dev);
			cond_resched();
		}

		/*
		 * Not much we can do on overflow, so scream and pretend we're
		 * trying harder.
		 */
		if (queue_sync_prod_in(q) == -EOVERFLOW)
			dev_err(smmu->dev, "EVTQ overflow detected -- events lost\n");
	} while (!queue_empty(llq));

	/* Sync our overflow flag, as we believe we're up to speed */
	queue_sync_cons_ovf(q);
	pm_runtime_put(smmu->dev);
	return IRQ_HANDLED;
}

static irqreturn_t kvm_arm_smmu_gerror_handler(int irq, void *dev)
{
	u32 gerror, gerrorn, active;
	struct arm_smmu_device *smmu = dev;
	struct host_arm_smmu_device *host_smmu = smmu_to_host(smmu);

	if (pm_runtime_get_if_active(smmu->dev) == 0) {
		dev_err(smmu->dev, "Unable to handle global error interrupt because device not runtime active\n");
		return IRQ_NONE;
	}

	gerror = readl_relaxed(smmu->base + ARM_SMMU_GERROR);
	gerrorn = readl_relaxed(smmu->base + ARM_SMMU_GERRORN);

	active = gerror ^ gerrorn;
	if (!(active & GERROR_ERR_MASK)) {
		pm_runtime_put(smmu->dev);
		return IRQ_NONE; /* No errors pending */
	}

	dev_warn(smmu->dev,
		 "unexpected global error reported (0x%08x), this could be serious\n",
		 active);

	/* There is no API to reconfigure the device at the moment.*/
	if (active & GERROR_SFM_ERR) {
		dev_err(smmu->dev, "device has entered Service Failure Mode!\n");
		arm_smmu_dev_tlm_inc_gerror_cnt(host_smmu->telemetry, SMMU_GERROR_SFM);
	}

	if (active & GERROR_MSI_GERROR_ABT_ERR)
		dev_warn(smmu->dev, "GERROR MSI write aborted\n");

	if (active & GERROR_MSI_PRIQ_ABT_ERR)
		dev_warn(smmu->dev, "PRIQ MSI write aborted\n");

	if (active & GERROR_MSI_EVTQ_ABT_ERR)
		dev_warn(smmu->dev, "EVTQ MSI write aborted\n");

	if (active & GERROR_MSI_CMDQ_ABT_ERR)
		dev_warn(smmu->dev, "CMDQ MSI write aborted\n");

	if (active & GERROR_PRIQ_ABT_ERR)
		dev_err(smmu->dev, "PRIQ write aborted -- events may have been lost\n");

	if (active & GERROR_EVTQ_ABT_ERR) {
		dev_err(smmu->dev, "EVTQ write aborted -- events may have been lost\n");
		arm_smmu_dev_tlm_inc_gerror_cnt(host_smmu->telemetry, SMMU_GERROR_EVTQ_ABT);
	}

	if (active & GERROR_CMDQ_ERR) {
		dev_err(smmu->dev, "CMDQ ERR -- Hypervisor cmdq corrupted?\n");
		BUG();
	}

	writel(gerror, smmu->base + ARM_SMMU_GERRORN);
	pm_runtime_put(smmu->dev);
	return IRQ_HANDLED;
}

static irqreturn_t kvm_arm_smmu_pri_handler(int irq, void *dev)
{
	struct arm_smmu_device *smmu = dev;

	dev_err(smmu->dev, "PRI not supported in KVM driver!\n");

	return IRQ_HANDLED;
}

static int kvm_arm_smmu_device_reset(struct host_arm_smmu_device *host_smmu)
{
	int ret;
	u32 reg;
	struct arm_smmu_device *smmu = &host_smmu->smmu;
	u32 irqen_flags = IRQ_CTRL_EVTQ_IRQEN | IRQ_CTRL_GERROR_IRQEN;

	reg = readl_relaxed(smmu->base + ARM_SMMU_CR0);
	if (reg & CR0_SMMUEN)
		dev_warn(smmu->dev, "SMMU currently enabled! Resetting...\n");

	/* Disable bypass */
	host_smmu->boot_gbpa = readl_relaxed(smmu->base + ARM_SMMU_GBPA);
	ret = arm_smmu_update_gbpa(smmu, GBPA_ABORT, 0);
	if (ret)
		return ret;

	ret = arm_smmu_device_disable(smmu);
	if (ret)
		return ret;

	/* Stream table */
	arm_smmu_write_strtab(smmu);

	/* Command queue */
	writeq_relaxed(smmu->cmdq.q.q_base, smmu->base + ARM_SMMU_CMDQ_BASE);

	/* Event queue */
	writeq_relaxed(smmu->evtq.q.q_base, smmu->base + ARM_SMMU_EVTQ_BASE);
	writel_relaxed(smmu->evtq.q.llq.prod, smmu->page1 + ARM_SMMU_EVTQ_PROD);
	writel_relaxed(smmu->evtq.q.llq.cons, smmu->page1 + ARM_SMMU_EVTQ_CONS);

	/* Disable IRQs first */
	ret = arm_smmu_write_reg_sync(smmu, 0, ARM_SMMU_IRQ_CTRL,
				      ARM_SMMU_IRQ_CTRLACK);
	if (ret) {
		dev_err(smmu->dev, "failed to disable irqs\n");
		return ret;
	}

	/*
	 * We don't support combined irqs for now, no specific reason, they are uncommon
	 * so we just try to avoid bloating the code.
	 */
	if (smmu->combined_irq)
		dev_err(smmu->dev, "Combined irqs not supported by this driver\n");
	else
		arm_smmu_setup_unique_irqs(smmu, kvm_arm_smmu_evt_handler,
					   kvm_arm_smmu_gerror_handler,
					   kvm_arm_smmu_pri_handler);

	if (smmu->features & ARM_SMMU_FEAT_PRI)
		irqen_flags |= IRQ_CTRL_PRIQ_IRQEN;

	/* Enable interrupt generation on the SMMU */
	ret = arm_smmu_write_reg_sync(smmu, irqen_flags,
				      ARM_SMMU_IRQ_CTRL, ARM_SMMU_IRQ_CTRLACK);
	if (ret)
		dev_warn(smmu->dev, "failed to enable irqs\n");

	return 0;
}

static int kvm_arm_probe_scmi_pd(struct device_node *scmi_node,
				 struct kvm_power_domain *pd)
{
	int ret;
	struct resource res;
	struct of_phandle_args args;

	pd->type = KVM_POWER_DOMAIN_ARM_SCMI;

	ret = of_parse_phandle_with_args(scmi_node, "shmem", NULL, 0, &args);
	if (ret)
		return ret;

	ret = of_address_to_resource(args.np, 0, &res);
	if (ret)
		goto out_put_nodes;

	ret = of_property_read_u32(scmi_node, "arm,smc-id",
				   &pd->arm_scmi.smc_id);
	if (ret)
		goto out_put_nodes;

	/*
	 * The shared buffer is unmapped from the host while a request is in
	 * flight, so it has to be on its own page.
	 */
	if (!IS_ALIGNED(res.start, SZ_64K) || resource_size(&res) < SZ_64K) {
		ret = -EINVAL;
		goto out_put_nodes;
	}

	pd->arm_scmi.shmem_base = res.start;
	pd->arm_scmi.shmem_size = resource_size(&res);

out_put_nodes:
	of_node_put(args.np);
	return ret;
}

/* TODO: Move this. None of it is specific to SMMU */
static int kvm_arm_probe_power_domain(struct device *dev,
				      struct kvm_power_domain *pd)
{
	int ret;
	struct device_node *parent;
	struct of_phandle_args args;

	if (!of_get_property(dev->of_node, "power-domains", NULL))
		return 0;

	ret = of_parse_phandle_with_args(dev->of_node, "power-domains",
					 "#power-domain-cells", 0, &args);
	if (ret)
		return ret;

	parent = of_get_parent(args.np);
	if (parent && of_device_is_compatible(parent, "arm,scmi-smc") &&
	    args.args_count > 0) {
		pd->arm_scmi.domain_id = args.args[0];
		ret = kvm_arm_probe_scmi_pd(parent, pd);
	} else {
		dev_warn(dev, "Unknown PM method for %pOF, using HVC\n",
			 args.np);
		pd->type = KVM_POWER_DOMAIN_HOST_HVC;
		pd->device_id = kvm_arm_smmu_cur;
	}
	of_node_put(parent);
	of_node_put(args.np);
	return ret;
}

static int kvm_arm_smmu_probe(struct platform_device *pdev)
{
	int ret;
	size_t iosize;
	phys_addr_t ioaddr;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;
	struct host_arm_smmu_device *host_smmu;
	struct hyp_arm_smmu_v3_device *hyp_smmu;

	if (kvm_arm_smmu_cur >= kvm_arm_smmu_count)
		return -ENOSPC;

	hyp_smmu = &kvm_arm_smmu_array[kvm_arm_smmu_cur];

	host_smmu = devm_kzalloc(dev, sizeof(*host_smmu), GFP_KERNEL);
	if (!host_smmu)
		return -ENOMEM;

	smmu = &host_smmu->smmu;
	smmu->dev = dev;

	device_initialize(&smmu->non_coherent_dev);
	smmu->non_coherent_dev.dma_coherent = false;
	dma_coerce_mask_and_coherent(&smmu->non_coherent_dev, DMA_BIT_MASK(64));

	ret = arm_smmu_fw_probe(pdev, smmu);
	if (ret)
		return ret;

	mutex_init(&smmu->streams_mutex);
	smmu->streams = RB_ROOT;

	ret = kvm_arm_probe_power_domain(dev, &host_smmu->power_domain);
	if (ret)
		return ret;

	ret = arm_smmu_device_ioremap(pdev, smmu, &ioaddr, &iosize);
	if (ret)
		return ret;

	if (iosize < SZ_128K) {
		dev_err(dev, "unsupported MMIO region size (%zx)\n", iosize);
		return -EINVAL;
	}

	host_smmu->id = kvm_arm_smmu_cur;
	host_arm_smmu_array[host_smmu->id] = host_smmu;

	arm_smmu_probe_irq(pdev, smmu);

	ret = arm_smmu_device_hw_probe(smmu);
	if (ret)
		return ret;

	if (!kvm_arm_smmu_validate_features(smmu))
		return -ENODEV;

	if (kvm_arm_smmu_ops.pgsize_bitmap == -1UL)
		kvm_arm_smmu_ops.pgsize_bitmap = smmu->pgsize_bitmap;
	else
		kvm_arm_smmu_ops.pgsize_bitmap |= smmu->pgsize_bitmap;

	ret = arm_smmu_init_one_queue(smmu, &smmu->cmdq.q, smmu->base,
				      ARM_SMMU_CMDQ_PROD, ARM_SMMU_CMDQ_CONS,
				      CMDQ_ENT_DWORDS, "cmdq");
	if (ret)
		return ret;

	/* evtq */
	ret = arm_smmu_init_one_queue(smmu, &smmu->evtq.q, smmu->page1,
				      ARM_SMMU_EVTQ_PROD, ARM_SMMU_EVTQ_CONS,
				      EVTQ_ENT_DWORDS, "evtq");
	if (ret)
		return ret;

	ret = arm_smmu_init_strtab(smmu);
	if (ret)
		return ret;

	ret = kvm_arm_smmu_device_reset(host_smmu);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, smmu);

	if (smmu->options & ARM_SMMU_OPT_NON_COHERENT_S2_TTW)
		kvm_hyp_smmu_global_config.s2_non_coherent_ttw = true;

	/* Hypervisor parameters */
	hyp_smmu->pgsize_bitmap = smmu->pgsize_bitmap;
	hyp_smmu->oas = smmu->oas;
	hyp_smmu->ias = smmu->ias;
	hyp_smmu->mmio_addr = ioaddr;
	hyp_smmu->mmio_size = iosize;
	hyp_smmu->features = smmu->features;
	hyp_smmu->iommu.power_domain = host_smmu->power_domain;
	hyp_smmu->ssid_bits = smmu->ssid_bits;
	hyp_smmu->options = smmu->options;

	kvm_arm_smmu_cur++;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Take a reference to keep the SMMU powered on while the hypervisor
	 * initializes it.
	 */
	pm_runtime_resume_and_get(dev);

	host_smmu->telemetry = arm_smmu_device_telemetry_alloc(smmu, PKVM_MODE_DRIVER);
	if (host_smmu->telemetry)
		arm_smmu_dev_tlm_rec_dev_id(host_smmu->telemetry, host_smmu->id);

	return arm_smmu_register_iommu(smmu, &kvm_arm_smmu_ops, ioaddr);
}

static void kvm_arm_smmu_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);
	struct host_arm_smmu_device *host_smmu = smmu_to_host(smmu);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	/*
	 * There was an error during hypervisor setup. The hyp driver may
	 * have already enabled the device, so disable it.
	 */
	arm_smmu_device_disable(smmu);
	arm_smmu_update_gbpa(smmu, host_smmu->boot_gbpa, GBPA_ABORT);
	arm_smmu_unregister_iommu(smmu);
	host_arm_smmu_array[host_smmu->id] = NULL;
	arm_smmu_device_telemetry_free(smmu, PKVM_MODE_DRIVER);
	host_smmu->telemetry = NULL;
}

static int kvm_arm_smmu_suspend(struct device *dev)
{
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	struct host_arm_smmu_device *host_smmu = smmu_to_host(smmu);

	dev_dbg(dev, "Suspending device\n");
	if (host_smmu->power_domain.type == KVM_POWER_DOMAIN_HOST_HVC)
		return pkvm_iommu_suspend(dev);
	return 0;
}

static int kvm_arm_smmu_resume(struct device *dev)
{
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	struct host_arm_smmu_device *host_smmu = smmu_to_host(smmu);

	dev_dbg(dev, "Resuming device\n");
	if (host_smmu->power_domain.type == KVM_POWER_DOMAIN_HOST_HVC)
		return pkvm_iommu_resume(dev);
	return 0;
}

static const struct dev_pm_ops kvm_arm_smmu_pm_ops = {
	SET_RUNTIME_PM_OPS(kvm_arm_smmu_suspend, kvm_arm_smmu_resume, NULL)
};

static const struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v3", },
	{ },
};

static struct platform_driver kvm_arm_smmu_driver = {
	.driver = {
		.name = "kvm-arm-smmu-v3",
		.of_match_table = arm_smmu_of_match,
		.pm = &kvm_arm_smmu_pm_ops,
	},
	.remove = kvm_arm_smmu_remove,
};

static int kvm_arm_smmu_array_alloc(void)
{
	int smmu_order, err_size;
	struct device_node *np;

	kvm_arm_smmu_count = 0;
	for_each_compatible_node(np, NULL, "arm,smmu-v3")
		if (of_device_is_available(np))
			kvm_arm_smmu_count++;

	if (!kvm_arm_smmu_count)
		return 0;

	host_arm_smmu_array = kcalloc(kvm_arm_smmu_count,
				      sizeof(*host_arm_smmu_array), GFP_KERNEL);
	if (!host_arm_smmu_array)
		return -ENOMEM;

	/* Allocate the parameter list shared with the hypervisor */
	smmu_order = get_order(kvm_arm_smmu_count * sizeof(*kvm_arm_smmu_array));
	kvm_arm_smmu_array = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						      smmu_order);
	if (!kvm_arm_smmu_array)
		return -ENOMEM;

	err_size = NR_CPUS * sizeof(*kvm_arm_smmu_v3_err);
	kvm_arm_smmu_v3_err = (void *)alloc_pages_exact(err_size, GFP_KERNEL | __GFP_ZERO);
	if (!kvm_arm_smmu_v3_err)
		return -ENOMEM;
	return 0;
}

static void kvm_arm_smmu_array_free(void)
{
	int order;

	order = get_order(kvm_arm_smmu_count * sizeof(*kvm_arm_smmu_array));
	free_pages((unsigned long)kvm_arm_smmu_array, order);
}

extern void __kvm_nvhe_pkvm_ptw_hvc_call(struct user_pt_regs *);

void arm_smmu_telemetry_register_hvc(void)
{
	if (ptw_hvc_num >= 0 || !pkvm_module_token)
		return;

	ptw_hvc_num = pkvm_register_el2_mod_call((dyn_hcall_t)__kvm_nvhe_pkvm_ptw_hvc_call,
						 pkvm_module_token);
	/* Negative return value indicates an error from pKVM registration */
	if (ptw_hvc_num <= 0)
		pr_err("Failed to register HVC call for PTW - %d\n", ptw_hvc_num);
}

static int kvm_arm_smmu_telemetry_alloc(void)
{
	kvm_shared_arm_smmu_telemetry = (void *)alloc_pages_exact(
			sizeof(*kvm_shared_arm_smmu_telemetry), GFP_KERNEL | __GFP_ZERO);
	if (!kvm_shared_arm_smmu_telemetry)
		return -ENOMEM;

	kvm_hyp_shared_arm_smmu_telemetry = kvm_shared_arm_smmu_telemetry;
	arm_smmu_set_shared_telemetry_ptr(kvm_hyp_shared_arm_smmu_telemetry);
	return 0;
}

static void kvm_arm_smmu_telemetry_free(void)
{
	int order;

	order = get_order(sizeof(*kvm_shared_arm_smmu_telemetry));
	free_pages((unsigned long)kvm_shared_arm_smmu_telemetry, order);
}

static int smmu_put_device(struct device *dev, void *data)
{
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);

	/* Keep RPM disable powered on. */
	if (!(smmu->options & ARM_SMMU_OPT_RPM_DISABLE))
		pm_runtime_put(dev);

	return 0;
}

static void smmu_determine_atomic_pages(void)
{
	struct sysinfo i;
	unsigned long totalram_bytes;

	/* Use value set on kernel command line */
	if (atomic_pages)
		return;

	si_meminfo(&i);
	totalram_bytes = i.totalram * i.mem_unit;

	/* 8 GB devices */
	if (totalram_bytes <= 8UL * SZ_1G)
		atomic_pages = 4081;
	/* 12 GB devices */
	else if (totalram_bytes <= 12UL * SZ_1G)
		atomic_pages = 5679;
	/* >=16 GB devices */
	else
		atomic_pages = 7676;
}

static int smmu_alloc_atomic_mc(struct kvm_hyp_memcache *atomic_mc)
{
	int ret;
#ifndef MODULE
	u64 i;
	phys_addr_t start, end;

	/*
	 * Allocate pages to cover mapping with PAGE_SIZE for all memory
	 * Then allocate extra for 1GB of MMIO.
	 * Add 10 extra pages as we map the rest with first level blocks
	 * for PAGE_SIZE = 4KB, that should cover 5TB of address space.
	 */
	for_each_mem_range(i, &start, &end) {
		atomic_pages += __hyp_pgtable_max_pages((end - start) >> PAGE_SHIFT);
	}

	atomic_pages += __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT) + 10;
#else
	smmu_determine_atomic_pages();
#endif

	/* Module didn't set that parameter. */
	if (!atomic_pages)
		return 0;

	/* For PGD*/
	ret = topup_hyp_memcache(atomic_mc, 1, 3);
	if (ret)
		return ret;
	ret = topup_hyp_memcache(atomic_mc, atomic_pages, 0);
	if (ret)
		return ret;
	pr_info("smmuv3: Allocated %d MiB for atomic usage\n",
		(atomic_pages << PAGE_SHIFT) / SZ_1M);
	/* Topup hyp alloc so IOMMU driver can allocate domains. */
	__pkvm_topup_hyp_alloc(1);

	return ret;
}

static int smmu_panic_err_dump(struct notifier_block *self,
			       unsigned long v, void *p)
{
	int cpu;

	for_each_present_cpu(cpu) {
		struct hyp_arm_smmu_v3_err *err = &kvm_arm_smmu_v3_err[cpu];
		struct host_arm_smmu_device *host_smmu = host_arm_smmu_array[err->smmu_id];
		struct device *dev = NULL;

		if (host_smmu)
			dev = host_smmu->smmu.dev;

		if (err->type == HYP_ARM_SMMU_V3_ERR_CMDQ_TIMEOUT) {
			dev_err(dev, "cmdq time out: hw prod 0x%x hw cons 0x%x sw prod 0x%llx",
				err->cmdq_prod, err->cmdq_cons, err->cmdq_prod_sw);
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block smmu_panic_block = {
	.notifier_call = smmu_panic_err_dump,
};

static int kvm_arm_smmu_v3_init_block_region(void)
{
	struct device_node *np;
	struct resource res;
	u64 start, size;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "pkvm,smmu-v3-block-region");
	if (!np)
		return 0;

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		pr_err("pKVM SMMUv3 invalid block region\n");
		return -EINVAL;
	}

	start = res.start;
	size = resource_size(&res);
	if (!PAGE_ALIGNED(start) || !PAGE_ALIGNED(size)) {
		pr_err("pKVM SMMUv3 block region %pr not properly aligned\n", &res);
		return -EINVAL;
	}

	kvm_hyp_smmu_global_config.block_region_start = start;
	kvm_hyp_smmu_global_config.block_region_size = size;

	return 0;
}

/*
 * Drop the PM references of the SMMU taken at probe
 * after it's guaranteed the hypervisor as initialized the SMMUs.
 */
static int kvm_arm_smmu_v3_post_init(void)
{
	if (!kvm_arm_smmu_count)
		return 0;

	WARN_ON(driver_for_each_device(&kvm_arm_smmu_driver.driver, NULL,
				       NULL, smmu_put_device));
	atomic_notifier_chain_register(&panic_notifier_list, &smmu_panic_block);
	return 0;
}

static void kvm_arm_smmu_v3_init_use_smc_s2(void)
{
	struct hyp_arm_smmu_v3_device *smmu;

	if (!smc_s2)
		return;

	for_each_smmu(smmu)
		if (smmu->options & ARM_SMMU_OPT_SYNC_FW) {
			kvm_hyp_smmu_global_config.use_smc_s2 = true;
			return;
		}
}

static int kvm_arm_smmu_v3_init_global_config(void)
{
	pr_info("SMMUv3: non-coherent stage 2 translation table walks: %s\n",
		str_yes_no(kvm_hyp_smmu_global_config.s2_non_coherent_ttw));
	kvm_arm_smmu_v3_init_use_smc_s2();
	return kvm_arm_smmu_v3_init_block_region();
}

static int kvm_arm_smmu_v3_init_drv(void)
{
	struct kvm_hyp_memcache atomic_mc;
	int ret;

	if (disable) {
		pr_warn("Skip probing pKVM SMMUv3 due to disable param.\n");
		return 0;
	}
	/*
	 * Check whether any device owned by the host is behind an SMMU.
	 */
	ret = kvm_arm_smmu_array_alloc();
	if (ret || !kvm_arm_smmu_count)
		return ret;

	ret = kvm_arm_smmu_telemetry_alloc();
	if (ret)
		goto err_free;

	ret = platform_driver_probe(&kvm_arm_smmu_driver, kvm_arm_smmu_probe);
	if (ret)
		goto free_telemetry;

	if (kvm_arm_smmu_cur != kvm_arm_smmu_count) {
		/* A device exists but failed to probe */
		ret = -EUNATCH;
		goto free_telemetry;
	}

#ifdef MODULE
	ret = pkvm_load_el2_module(kvm_nvhe_sym(smmu_init_hyp_module),
				   &pkvm_module_token);

	if (ret) {
		pr_err("Failed to load SMMUv3 IOMMU EL2 module: %d\n", ret);
		return ret;
	}

	arm_smmu_telemetry_register_hvc();
#endif
	/*
	 * These variables are stored in the nVHE image, and won't be accessible
	 * after KVM initialization. Ownership of kvm_arm_smmu_array will be
	 * transferred to the hypervisor as well.
	 *
	 * kvm_hyp_smmu_last_err is shared between hypervisor and host.
	 */
	kvm_hyp_arm_smmu_v3_smmus = kvm_arm_smmu_array;
	kvm_hyp_arm_smmu_v3_count = kvm_arm_smmu_count;
	kvm_hyp_smmu_last_err = kvm_arm_smmu_v3_err;

	init_hyp_memcache(&atomic_mc);

	ret = smmu_alloc_atomic_mc(&atomic_mc);
	if (ret)
		goto free_telemetry;

	ret = kvm_arm_smmu_v3_init_global_config();
	if (ret)
		goto free_telemetry;

	ret = kvm_iommu_init_hyp(ksym_ref_addr_nvhe(smmu_ops), &atomic_mc);
	if (ret)
		goto free_telemetry;

	/* Preemptively allocate the identity domain. */
	if (atomic_pages) {
		ret = kvm_iommu_alloc_domain(KVM_IOMMU_DOMAIN_IDMAP_ID,
					     KVM_IOMMU_DOMAIN_IDMAP_TYPE);
		if (ret)
			goto free_telemetry;
	}
	return kvm_arm_smmu_v3_post_init();

free_telemetry:
	kvm_arm_smmu_telemetry_free();
err_free:
	kvm_arm_smmu_array_free();
	return ret;
}

static void kvm_arm_smmu_v3_remove_drv(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &smmu_panic_block);
	platform_driver_unregister(&kvm_arm_smmu_driver);
}

static pkvm_handle_t kvm_arm_smmu_v3_id(struct device *dev)
{
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	struct host_arm_smmu_device *host_smmu = smmu_to_host(smmu);

	return host_smmu->id;
}

static pkvm_handle_t kvm_arm_v3_id_by_of(struct device_node *np)
{
	struct device *dev;

	dev = driver_find_device_by_of_node(&kvm_arm_smmu_driver.driver, np);
	if (!dev)
		return 0;

	put_device(dev);

	return kvm_arm_smmu_v3_id(dev);
}

static int kvm_arm_smmu_v3_num_ids(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec)
		return -ENODEV;

	return fwspec->num_ids;
}

static int kvm_arm_smmu_v3_device_id(struct device *dev, u32 idx,
				     pkvm_handle_t *out_iommu, u32 *out_sid)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct kvm_arm_smmu_master *master = dev_iommu_priv_get(dev);

	if (!fwspec || !master)
		return -ENODEV;
	if (idx >= fwspec->num_ids)
		return -ENOENT;
	*out_sid = fwspec->ids[idx];
	*out_iommu = kvm_arm_smmu_v3_id(master->smmu->dev);

	return 0;
}

static struct kvm_iommu_driver kvm_smmu_v3_ops = {
	.init_driver = kvm_arm_smmu_v3_init_drv,
	.remove_driver = kvm_arm_smmu_v3_remove_drv,
	.get_iommu_id_by_of = kvm_arm_v3_id_by_of,
	.get_device_iommu_num_ids = kvm_arm_smmu_v3_num_ids,
	.get_device_iommu_id = kvm_arm_smmu_v3_device_id,
};

static int kvm_arm_smmu_v3_register(void)
{
	if (!is_protected_kvm_enabled())
		return 0;

	return kvm_iommu_register_driver(&kvm_smmu_v3_ops);
};

/*
 * Register must be run before de-privliage before kvm_iommu_init_driver
 * for module case, it should be loaded using pKVM early loading which
 * loads it before this point.
 * For builtin drivers we use core_initcall
 */
#ifdef MODULE
module_init(kvm_arm_smmu_v3_register);
#else
core_initcall(kvm_arm_smmu_v3_register);
#endif
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ARM SMMUv3 pKVM support");
