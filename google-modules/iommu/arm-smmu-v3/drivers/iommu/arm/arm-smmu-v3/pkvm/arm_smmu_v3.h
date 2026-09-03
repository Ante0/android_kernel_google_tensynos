/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_ARM_SMMU_V3_H
#define __KVM_ARM_SMMU_V3_H

#include <arm-smmu-v3/arm-smmu-v3-common.h>
#include <asm/kvm_asm.h>
#include <kvm/iommu.h>

/*
 * Parameters from the trusted host:
 * @mmio_addr		base address of the SMMU registers
 * @mmio_size		size of the registers resource
 *
 * Other members are filled and used at runtime by the SMMU driver.
 */
struct hyp_arm_smmu_v3_device {
	struct kvm_hyp_iommu	iommu;
	phys_addr_t		mmio_addr;
	size_t			mmio_size;
	unsigned long		features;

	void __iomem		*base;
	u32			cmdq_prod;
	u64			*cmdq_base;
	size_t			cmdq_log2size;
	/* strtab_cfg.l2.l2ptrs is not used, instead computed from L1 */
	struct arm_smmu_strtab_cfg strtab_cfg;
	size_t			oas;
	size_t			ias;
	size_t			pgsize_bitmap;
	size_t			ssid_bits;
	u32			options;
};

/* Only the first 16 bytes or 2 double words are relevant for stage-1 configuration. */
struct hyp_arm_smmu_v3_s1_ctx {
	__le64 ste[2];
};

struct hyp_arm_smmu_v3_cmd {
	__le64 data[CMDQ_ENT_DWORDS];
};

extern size_t kvm_nvhe_sym(kvm_hyp_arm_smmu_v3_count);
#define kvm_hyp_arm_smmu_v3_count kvm_nvhe_sym(kvm_hyp_arm_smmu_v3_count)

extern struct hyp_arm_smmu_v3_device *kvm_nvhe_sym(kvm_hyp_arm_smmu_v3_smmus);
#define kvm_hyp_arm_smmu_v3_smmus kvm_nvhe_sym(kvm_hyp_arm_smmu_v3_smmus)

#define for_each_smmu(smmu) \
	for ((smmu) = kvm_hyp_arm_smmu_v3_smmus; \
	     (smmu) != &kvm_hyp_arm_smmu_v3_smmus[kvm_hyp_arm_smmu_v3_count]; \
	     (smmu)++)

enum kvm_arm_smmu_domain_type {
	KVM_ARM_SMMU_DOMAIN_BYPASS = KVM_IOMMU_DOMAIN_IDMAP_TYPE,
	KVM_ARM_SMMU_DOMAIN_ANY = KVM_IOMMU_DOMAIN_ANY_TYPE,
	KVM_ARM_SMMU_DOMAIN_S1,
	KVM_ARM_SMMU_DOMAIN_S2,
	KVM_ARM_SMMU_DOMAIN_NESTED,
	KVM_ARM_SMMU_DOMAIN_MAX,
};

extern struct kvm_iommu_ops smmu_ops;

enum hyp_arm_smmu_v3_err_type {
	HYP_ARM_SMMU_V3_ERR_NONE,
	HYP_ARM_SMMU_V3_ERR_CMDQ_TIMEOUT, /* command queue time out */
};

struct hyp_arm_smmu_v3_err {
	pkvm_handle_t smmu_id;
	enum hyp_arm_smmu_v3_err_type type;
	union {
		struct {
			u32 cmdq_prod;
			u32 cmdq_cons;
			u64 cmdq_prod_sw;
		};
	};
} ____cacheline_aligned_in_smp;;

extern struct hyp_arm_smmu_v3_err *kvm_nvhe_sym(kvm_hyp_smmu_last_err);
#define kvm_hyp_smmu_last_err kvm_nvhe_sym(kvm_hyp_smmu_last_err)

struct hyp_arm_smmu_v3_global_config {
	phys_addr_t	block_region_start;
	size_t		block_region_size;
	bool		use_smc_s2;
	bool            s2_non_coherent_ttw;
};

extern struct hyp_arm_smmu_v3_global_config kvm_nvhe_sym(kvm_hyp_smmu_global_config);
#define kvm_hyp_smmu_global_config kvm_nvhe_sym(kvm_hyp_smmu_global_config)

#define ARM_SMMU_FORCE_CACHEABLE	BIT(0)
#define ARM_SMMU_TCU_PREFETCH_FORWARD	BIT(1)
#define ARM_SMMU_TCU_PREFETCH_BACKWARD	BIT(2)
#define ARM_SMMU_ATTACH_DEV_LAST_SID	BIT(3)
#define ARM_SMMU_DISABLE_CONTPTE	BIT(4)

/*
 * Nested domains must invalidate all stage-1 TLB entries when stage-2 mappings
 * change. If stage-1 only domains use the same VMID as nested domains, then
 * those TLB entries will also be invalidated when nested domains require
 * invalidation, which can negatively impact performance.
 *
 * Reserve VMID 1 so that nested domains don't share the same VMID as stage-1
 * only domains. This ensures that invalidations for stage-2 mappings only
 * affect stage-1 TLB entries associated with nested domains.
 */
#define KVM_SMMU_S1_DOMAIN_VMID		(KVM_IOMMU_DOMAIN_NR_START)
#define KVM_SMMU_DOMAIN_NR_START	(KVM_SMMU_S1_DOMAIN_VMID + 1)

#endif /* __KVM_ARM_SMMU_V3_H */
