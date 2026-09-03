// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include "kvm_s2mpu.h"

#include <linux/arm-smccc.h>

#include <nvhe/iommu.h>
#include <nvhe/memory.h>
#include <nvhe/mm.h>
#include <nvhe/spinlock.h>
#include <nvhe/trap_handler.h>
#include "io-mpt-s2mpu.h"

#ifdef memset
#undef memset
#endif

#ifdef memcpy
#undef memcpy
#endif

#define PA_MAX				((phys_addr_t)SZ_1G * NR_GIGABYTES)

#define SYNC_MAX_RETRIES		5
#define SYNC_TIMEOUT			5
#define SYNC_TIMEOUT_MULTIPLIER		3

/* x2 - flags, x3 - reserved */
#define SMC_S2MPU_TZ_REGISTER			(0x820020F0U)
/* x2 - gb, x3 - pa of the table */
#define SMC_S2MPU_TZ_UPDATE_MPT_ADDR		(0x820020F1U)
/* x2 - start b, x3 - end b */
#define SMC_S2MPU_TZ_UPDATE_IDMAP		(0x820020F2U)
/* sets TZ context count */
#define SMC_S2MPU_TZ_ALLOCATE_CTX		(0x820020F3U)
/* performs suspend/resume ops */
#define SMC_S2MPU_TZ_SET_STATE			(0x820020F4U)
#define SMC_S2MPU_TZ_INVALIDATE_COMPLETE	(0x820020F5U)
#define SMC_S2MPU_TZ_MAX			(0x820020FFU)

#define SET_STATE_ACTIVE		(1)
#define SET_STATE_SUSPENDED		(0)

#define CTX_CFG_ENTRY(ctxid, nr_ctx, vid) \
	(CONTEXT_CFG_VALID_VID_CTX_VID(ctxid, vid) \
	 | (((ctxid) < (nr_ctx)) ? CONTEXT_CFG_VALID_VID_CTX_VALID(ctxid) : 0))

/* kvm_s2mpu.h makes all preprocessor checks about the version correctness. */
#ifdef S2MPU_V9
#define INIT_S2MPU(dev)					__initialize_ctx(dev)
#define HOST_MMIO_REG_ACCESS_MASK(off, is_w)		host_mmio_reg_access_mask_v9(off, is_w)
#define SET_CONTROL_REGS(dev)				__set_control_regs_v9(dev)
#else /*v1, v2*/
#define INIT_S2MPU(dev)					__initialize(dev)
#define HOST_MMIO_REG_ACCESS_MASK(off, is_w)		host_mmio_reg_access_mask_v1_v2(off, is_w)
#define SET_CONTROL_REGS(dev)				__set_control_regs(dev)
#endif

#define for_each_s2mpu(s2mpu) \
	for ((s2mpu) = kvm_hyp_s2mpus; \
	     (s2mpu) != &kvm_hyp_s2mpus[kvm_hyp_s2mpu_count]; \
	     (s2mpu)++)

#define for_each_sync_dev(sync_dev, s2mpu) \
	for ((sync_dev) = (s2mpu)->sync_devs; \
	     (sync_dev) != &(s2mpu)->sync_devs[(s2mpu)->num_sync_devs]; \
	     (sync_dev)++)

struct pkvm_iommu *kvm_hyp_s2mpus;
size_t __ro_after_init kvm_hyp_s2mpu_count;

static const struct s2mpu_mpt_ops *mpt_ops;
static const struct pkvm_module_ops *mod_ops;
struct mpt *kvm_hyp_mpt;
/* LDFW has support for S2MPU */
static bool ldfw_s2mpu_support;
/* LDFW supports S2MPU and at least one S2MPU device has a TZ sibling. */
static bool use_s2mpu_sec;

/* Abstraction for function used from module ops. */
#define CALL_FROM_OPS(fn, ...)		mod_ops->fn(__VA_ARGS__)

#define __pkvm_hyp_donate_host(x, y)	CALL_FROM_OPS(hyp_donate_host, x, y)
#define __pkvm_host_donate_hyp(x, y)	CALL_FROM_OPS(host_donate_hyp, x, y, false)
#define ___pkvm_host_donate_hyp(x, y, z) \
					CALL_FROM_OPS(host_donate_hyp, x, y, z)
#define iommu_snapshot_host_stage2(x)	CALL_FROM_OPS(iommu_snapshot_host_stage2, x)
#define kvm_iommu_init_device(x)	CALL_FROM_OPS(iommu_init_device, x)
#ifdef kern_hyp_va
#undef kern_hyp_va
#endif
#define kern_hyp_va(x)			((u32 *)CALL_FROM_OPS(kern_hyp_va, (unsigned long)x))

#ifdef __hyp_pa
#undef __hyp_pa
#endif
#define __hyp_pa(x)			CALL_FROM_OPS(hyp_pa, x)

#ifdef hyp_phys_to_virt
#undef hyp_phys_to_virt
#endif
#define hyp_phys_to_virt(x)		CALL_FROM_OPS(hyp_va, x)

void *memset(void *dst, int c, size_t count)
{
	return CALL_FROM_OPS(memset, dst, c, count);
}

void *memcpy(void *dst, const void *src, size_t count)
{
	return CALL_FROM_OPS(memcpy, dst, src, count);
}

static struct mpt host_mpt;

static struct pkvm_iommu *to_s2mpu(struct kvm_hyp_iommu *iommu)
{
	return container_of(iommu, struct pkvm_iommu, iommu);
}

static struct kvm_hyp_iommu *s2mpu_id_to_iommu(pkvm_handle_t s2mpu_id)
{
	if (s2mpu_id >= kvm_hyp_s2mpu_count)
		return NULL;
	s2mpu_id = array_index_nospec(s2mpu_id, kvm_hyp_s2mpu_count);

	return &kvm_hyp_s2mpus[s2mpu_id].iommu;
}

static inline enum mpt_prot prot_to_mpt(enum kvm_pgtable_prot prot)
{
	return ((prot & IOMMU_READ) ? MPT_PROT_R : 0) |
	       ((prot & IOMMU_WRITE) ? MPT_PROT_W : 0);
}

static bool is_version(struct pkvm_iommu *dev, u32 version)
{
	struct s2mpu_drv_data *data = &dev->data;

	return (data->version & VERSION_CHECK_MASK) == version;
}

static int call_ldfw(struct pkvm_iommu *dev, u32 cmd, unsigned long arg1,
		     unsigned long arg2)
{
#ifdef S2MPU_V9
	int ret;
	struct arm_smccc_res res;
	u64 pa;

	/* Use the device index or physical address as a handle for the Secure side */
	if (!dev)
		pa = 0;
	else
		pa = dev->pa;

	arm_smccc_1_1_smc(cmd, pa, arg1, arg2, 0, 0, 0, 0, &res);
	ret = (int)res.a0;

	return ret;
#else
	return -1; /* Not supported by default */
#endif
}

static u32 __context_cfg_valid_vid(struct pkvm_iommu *dev, u32 vid_bmap)
{
	struct s2mpu_drv_data *data = &dev->data;
	u8 ctx_vid[NR_CTX_IDS] = { 0 };
	unsigned int vid, ctx = 0;
	unsigned int num_ctx;
	u32 res;

	/* Only initialize once. */
	if (data->context_cfg_valid_vid)
		return data->context_cfg_valid_vid;

	num_ctx = readl_relaxed(dev->va + REG_NS_NUM_CONTEXT) & NUM_CONTEXT_MASK;
	while (vid_bmap) {
		/* Break if we cannot allocate more. */
		if (ctx >= num_ctx)
			break;

		vid = __ffs(vid_bmap);
		vid_bmap &= ~BIT(vid);
		ctx_vid[ctx++] = vid;
	}

	/* The following loop was unrolled so bitmasks are constant. */
	BUILD_BUG_ON(NR_CTX_IDS != 8);
	res = CTX_CFG_ENTRY(0, ctx, ctx_vid[0])
	    | CTX_CFG_ENTRY(1, ctx, ctx_vid[1])
	    | CTX_CFG_ENTRY(2, ctx, ctx_vid[2])
	    | CTX_CFG_ENTRY(3, ctx, ctx_vid[3])
	    | CTX_CFG_ENTRY(4, ctx, ctx_vid[4])
	    | CTX_CFG_ENTRY(5, ctx, ctx_vid[5])
	    | CTX_CFG_ENTRY(6, ctx, ctx_vid[6])
	    | CTX_CFG_ENTRY(7, ctx, ctx_vid[7]);

	data->context_cfg_valid_vid = res;
	return res;
}

static int __maybe_unused __initialize_ctx(struct pkvm_iommu *dev)
{
	u32 ssmt_valid_vid_bmap, ctx_cfg;

	/* Assume all VIDs may be generated by the connected SSMTs for now. */
	ssmt_valid_vid_bmap = ALL_VIDS_BITMAP;
	ctx_cfg = __context_cfg_valid_vid(dev, ssmt_valid_vid_bmap);
	if (!ctx_cfg)
		return -EINVAL;

	/*
	 * Write CONTEXT_CFG_VALID_VID configuration before touching L1ENTRY*
	 * registers. Writes to those registers are ignored unless there is
	 * a context ID allocated to the corresponding VID (v2 only).
	 */
	writel_relaxed(ctx_cfg, dev->va + REG_NS_CONTEXT_CFG_VALID_VID);
	return 0;
}

static int __maybe_unused __initialize(struct pkvm_iommu *dev)
{
	struct s2mpu_drv_data *data = &dev->data;

	if (!data->version)
		data->version = readl_relaxed(dev->va + REG_NS_VERSION);

	switch (data->version & VERSION_CHECK_MASK) {
	case S2MPU_VERSION_1:
		return 0;
	case S2MPU_VERSION_2:
		return __initialize_ctx(dev);
	default:
		return -EINVAL;
	}
}

static void __maybe_unused __set_control_regs(struct pkvm_iommu *dev)
{
	u32 ctrl0 = 0, irq_vids;

	/*
	 * Note: We set the values of CTRL0, CTRL1 and CFG registers here but we
	 * still rely on the correctness of their reset values. S2MPUs *must*
	 * reset to a state where all DMA traffic is blocked until the hypervisor
	 * writes its configuration to the S2MPU. A malicious EL1 could otherwise
	 * attempt to bypass the permission checks in the window between powering
	 * on the S2MPU and this function being called.
	 */

	/* Enable the S2MPU, otherwise all traffic would be allowed through. */
	ctrl0 |= CTRL0_ENABLE;

	/*
	 * Enable interrupts on fault for all VIDs. The IRQ must also be
	 * specified in DT to get unmasked in the GIC.
	 */
	ctrl0 |= CTRL0_INTERRUPT_ENABLE;
	irq_vids = ALL_VIDS_BITMAP;

	/* Return SLVERR/DECERR to device on permission fault. */
	ctrl0 |= is_version(dev, S2MPU_VERSION_2) ? CTRL0_FAULT_RESP_TYPE_DECERR
						  : CTRL0_FAULT_RESP_TYPE_SLVERR;

	writel_relaxed(irq_vids, dev->va + REG_NS_INTERRUPT_ENABLE_PER_VID_SET);
	writel_relaxed(0, dev->va + REG_NS_CFG);
	writel_relaxed(0, dev->va + REG_NS_CTRL1);
	writel_relaxed(ctrl0, dev->va + REG_NS_CTRL0);
}

static void __maybe_unused __set_control_regs_v9(struct pkvm_iommu *dev)
{
	/* Return DECERR to device on permission fault. */
	writel_relaxed(ALL_VIDS_BITMAP,
		       dev->va + REG_NS_V9_CTRL_ERR_RESP_T_PER_VID_SET);
	/*
	 * Enable interrupts on fault for all VIDs. The IRQ must also be
	 * specified in DT to get unmasked in the GIC.
	 */
	writel_relaxed(ALL_VIDS_BITMAP,
		       dev->va + REG_NS_INTERRUPT_ENABLE_PER_VID_SET);
	writel_relaxed(0, dev->va + REG_NS_CTRL0);
	/* Enable the S2MPU, otherwise all traffic would be allowed through. */
	writel_relaxed(ALL_VIDS_BITMAP,
		       dev->va + REG_NS_V9_CTRL_PROT_EN_PER_VID_SET);
	writel_relaxed(0, dev->va + REG_NS_V9_CFG_MPTW_ATTRIBUTE);
}

/*
 * Poll the given SFR until its value has all bits of a given mask set.
 * Returns true if successful, false if not successful after a given number of
 * attempts.
 */
static bool __wait_until(void __iomem *addr, u32 mask, size_t max_attempts)
{
	size_t i;

	for (i = 0; i < max_attempts; i++) {
		if ((readl_relaxed(addr) & mask) == mask)
			return true;
	}
	return false;
}

/* Poll the given SFR as long as its value has all bits of a given mask set. */
static void __wait_while(void __iomem *addr, u32 mask)
{
	while ((readl_relaxed(addr) & mask) == mask)
		continue;
}

static void __sync_cmd_start(void *dev_va)
{
	writel_relaxed(SYNC_CMD_SYNC, dev_va + REG_NS_SYNC_CMD);
}

static void __invalidation_barrier_slow(void *dev_va)
{
	size_t i, timeout;

	/*
	 * Check if the SYNC_COMP_COMPLETE bit has been set for individual
	 * devices. If not, fall back to non-parallel invalidation.
	 */
	if (readl_relaxed(dev_va + REG_NS_SYNC_COMP) & SYNC_COMP_COMPLETE)
		return;

	/*
	 * Wait for transactions to drain if SysMMU_SYNCs were registered.
	 * Assumes that they are in the same power domain as the S2MPU.
	 *
	 * The algorithm will try initiating the SYNC if the SYNC_COMP_COMPLETE
	 * bit has not been set after a given number of attempts, increasing the
	 * timeout exponentially each time. If this cycle fails a given number
	 * of times, the algorithm will give up completely to avoid deadlock.
	 */
	timeout = SYNC_TIMEOUT;
	for (i = 0; i < SYNC_MAX_RETRIES; i++) {
		__sync_cmd_start(dev_va);
		if (__wait_until(dev_va + REG_NS_SYNC_COMP, SYNC_COMP_COMPLETE, timeout))
			break;
		timeout *= SYNC_TIMEOUT_MULTIPLIER;
	}
}

/* Initiate invalidation barrier. */
static void __invalidation_barrier_init(struct pkvm_iommu *dev)
{
	if (dev->flags & S2MPU_HAS_SYNC) {
		__sync_cmd_start(dev->va + REG_NS_V9_SYNC_DEV_OFFSET);
	} else {
		struct s2mpu_sync_dev *sync_dev;

		for_each_sync_dev(sync_dev, dev)
			__sync_cmd_start(sync_dev->va);
	}
}

/* Wait for invalidation to complete. */
static void __invalidation_barrier_complete(struct pkvm_iommu *dev)
{
	if (dev->flags & S2MPU_HAS_SYNC) {
		__invalidation_barrier_slow(dev->va + REG_NS_V9_SYNC_DEV_OFFSET);
	} else {
		struct s2mpu_sync_dev *sync_dev;

		for_each_sync_dev(sync_dev, dev)
			__invalidation_barrier_slow(sync_dev->va);
	}

	/* Must not access SFRs while S2MPU is busy invalidating */
	if (is_version(dev, S2MPU_VERSION_2) || is_version(dev, S2MPU_VERSION_9)) {
		__wait_while(dev->va + REG_NS_STATUS,
			     STATUS_BUSY | STATUS_ON_INVALIDATING);
	}
}

static void __all_invalidation(struct pkvm_iommu *dev)
{
	writel_relaxed(INVALIDATION_INVALIDATE, dev->va + REG_NS_ALL_INVALIDATION);
	__invalidation_barrier_init(dev);
	__invalidation_barrier_complete(dev);
}

static void __range_invalidation_init(struct pkvm_iommu *dev, phys_addr_t first_byte,
				      phys_addr_t last_byte)
{
	u32 start_ppn = first_byte >> RANGE_INVALIDATION_PPN_SHIFT;
	u32 end_ppn = last_byte >> RANGE_INVALIDATION_PPN_SHIFT;

	writel_relaxed(start_ppn, dev->va + REG_NS_RANGE_INVALIDATION_START_PPN);
	writel_relaxed(end_ppn, dev->va + REG_NS_RANGE_INVALIDATION_END_PPN);
	writel_relaxed(INVALIDATION_INVALIDATE, dev->va + REG_NS_RANGE_INVALIDATION);
	__invalidation_barrier_init(dev);
}

/*
 * Initialize S2MPU device and set all GB regions to 1G granularity with
 * given protection bits.
 */
static int initialize_with_prot(struct pkvm_iommu *dev, enum mpt_prot prot)
{
	int ret;

	ret = INIT_S2MPU(dev);
	if (ret)
		return ret;

	mpt_ops->init_with_prot(dev->va, prot);
	__all_invalidation(dev);

	/* Set control registers, enable the S2MPU. */
	SET_CONTROL_REGS(dev);
	return 0;
}

/*
 * Initialize S2MPU device, set L2 table addresses and configure L1TABLE_ATTR
 * registers according to the given MPT struct.
 */
static int initialize_with_mpt(struct pkvm_iommu *dev, struct mpt *mpt)
{
	int ret;

	ret = INIT_S2MPU(dev);
	if (ret)
		return ret;

	mpt_ops->init_with_mpt(dev->va, mpt);
	__all_invalidation(dev);

	/* Set control registers, enable the S2MPU. */
	SET_CONTROL_REGS(dev);
	return 0;
}

static bool to_valid_range(phys_addr_t *start, phys_addr_t *end)
{
	phys_addr_t new_start = *start;
	phys_addr_t new_end = *end;

	if (new_end > PA_MAX)
		new_end = PA_MAX;

	new_start = ALIGN_DOWN(new_start, SMPT_GRAN);
	new_end = ALIGN(new_end, SMPT_GRAN);

	if (new_start >= new_end)
		return false;

	*start = new_start;
	*end = new_end;
	return true;
}

static void __mpt_idmap_prepare(struct mpt *mpt, phys_addr_t first_byte,
				phys_addr_t last_byte, enum mpt_prot prot)
{
	mpt_ops->prepare_range(mpt, first_byte, last_byte, prot);
}

static void __mpt_idmap_apply(struct pkvm_iommu *dev, struct mpt *mpt,
			      phys_addr_t first_byte, phys_addr_t last_byte)
{
	unsigned int first_gb = first_byte / SZ_1G;
	unsigned int last_gb = last_byte / SZ_1G;

	mpt_ops->apply_range(dev->va, mpt, first_gb, last_gb);
	/* Initiate invalidation, completed in __mdt_idmap_complete. */
	__range_invalidation_init(dev, first_byte, last_byte);
}

static void __mpt_idmap_complete(struct pkvm_iommu *dev, struct mpt *mpt)
{
	__invalidation_barrier_complete(dev);
}

static void s2mpu_host_stage2_idmap_prepare(phys_addr_t start, phys_addr_t end,
					    enum kvm_pgtable_prot prot)
{
	if (!to_valid_range(&start, &end))
		return;
	__mpt_idmap_prepare(&host_mpt, start, end - 1, prot_to_mpt(prot));
}

static void s2mpu_host_stage2_idmap_apply(struct pkvm_iommu *dev,
					  phys_addr_t start, phys_addr_t end)
{
	if (!to_valid_range(&start, &end))
		return;

	if (dev->flags & S2MPU_DENY_ALL)
		return;

	__mpt_idmap_apply(dev, &host_mpt, start, end - 1);
}

static void s2mpu_host_stage2_idmap_complete(struct pkvm_iommu *dev)
{
	if (dev->flags & S2MPU_DENY_ALL)
		return;

	__mpt_idmap_complete(dev, &host_mpt);
}

static int s2mpu_resume(struct kvm_hyp_iommu *iommu)
{
	int ret;
	struct pkvm_iommu *dev = to_s2mpu(iommu);
	/*
	 * Initialize the S2MPU with the host stage-2 MPT. It is paramount
	 * that the S2MPU reset state is enabled and blocking all traffic,
	 * otherwise the host would not be forced to call the resume HVC
	 * before issuing DMA traffic.
	 */
	if (dev->flags & S2MPU_DENY_ALL)
		return 0;

	if (dev->flags & S2MPU_HAS_TZ_SIBLING)
		call_ldfw(dev, SMC_S2MPU_TZ_ALLOCATE_CTX, 0, 0);
	ret = initialize_with_mpt(dev, &host_mpt);
	if (dev->flags & S2MPU_HAS_TZ_SIBLING && ret == 0)
		call_ldfw(dev, SMC_S2MPU_TZ_SET_STATE, SET_STATE_ACTIVE, 0);

	return ret;
}

static int s2mpu_suspend(struct kvm_hyp_iommu *iommu)
{
	int ret;
	struct pkvm_iommu *dev = to_s2mpu(iommu);
	/*
	 * Stop updating the S2MPU when the host informs us about the intention
	 * to suspend it. Writes to powered-down MMIO registers would trigger
	 * SErrors in EL1 otherwise. However, hyp must put S2MPU back to
	 * blocking state first, in case the host does not actually power it
	 * down and continues issuing DMA traffic.
	 */
	ret = initialize_with_prot(dev, MPT_PROT_NONE);
	if (dev->flags & S2MPU_HAS_TZ_SIBLING && ret == 0)
		call_ldfw(dev, SMC_S2MPU_TZ_SET_STATE, SET_STATE_SUSPENDED, 0);

	return ret;
}

static u32 __maybe_unused host_mmio_reg_access_mask_v9(size_t off, bool is_write)
{
	const u32 no_access = 0;
	const u32 read_write = (u32)(-1);
	const u32 read_only = is_write ? no_access : read_write;
	const u32 write_only = is_write ? read_write : no_access;

	switch (off) {
	case REG_NS_V9_READ_STLB:
		return write_only & (V9_READ_STLB_MASK_TYPEA|V9_READ_STLB_MASK_TYPEB);
	case REG_NS_V9_READ_MPTC:
		return write_only & V9_READ_MPTC_MASK;
	case REG_NS_V9_READ_PTLB:
		return write_only & V9_READ_PTLB_MASK;
	};
	return read_only;
}

static u32 __maybe_unused host_mmio_reg_access_mask_v1_v2(size_t off, bool is_write)
{
	const u32 no_access = 0;
	const u32 read_write = (u32)(-1);
	const u32 read_only = is_write ? no_access : read_write;
	const u32 write_only = is_write ? read_write : no_access;

	switch (off) {
	/* Allow reading MPTC entries for debugging. That involves:
	 *   - writing (set,way) to READ_MPTC
	 *   - reading READ_MPTC_*
	 */
	case REG_NS_READ_MPTC:
		return write_only & READ_MPTC_MASK;
	};
	return read_only;
}

static u32 host_mmio_reg_access_mask(size_t off, bool is_write)
{
	const u32 no_access  = 0;
	const u32 read_write = (u32)(-1);
	const u32 write_only = is_write ? read_write : no_access;

	switch (off) {
	/* Allow EL1 IRQ handler to clear interrupts. */
	case REG_NS_INTERRUPT_CLEAR:
		return write_only & ALL_VIDS_BITMAP;
	}
	/* Check version-specific registers. */
	return HOST_MMIO_REG_ACCESS_MASK(off, is_write);
}

static bool s2mpu_host_dabt_handler(struct pkvm_iommu *dev,
				    struct user_pt_regs *regs,
				    u32 esr, size_t off)
{
	bool is_write = esr & ESR_ELx_WNR;
	unsigned int len = BIT((esr & ESR_ELx_SAS) >> ESR_ELx_SAS_SHIFT);
	int rd = (esr & ESR_ELx_SRT_MASK) >> ESR_ELx_SRT_SHIFT;
	u32 mask;

	/* Only handle MMIO access with u32 size and alignment. */
	if ((len != sizeof(u32)) || (off & (sizeof(u32) - 1)))
		return false;

	mask = host_mmio_reg_access_mask(off, is_write);
	if (!mask)
		return false;

	if (is_write)
		writel_relaxed(regs->regs[rd] & mask, dev->va + off);
	else
		regs->regs[rd] = readl_relaxed(dev->va + off);
	return true;
}

static bool s2mpu_drv_host_dabt_handler(struct user_pt_regs *regs,
					u64 esr, u64 addr)
{
	struct pkvm_iommu *dev;

	for_each_s2mpu(dev) {
		if (addr < dev->pa || addr >= dev->pa + dev->size)
			continue;
		return s2mpu_host_dabt_handler(dev, regs, esr, addr - dev->pa);
	}
	return false;
}

static int s2mpu_register_dev(struct pkvm_iommu *dev)
{
	int ret;
	struct s2mpu_sync_dev *sync_dev;

	if (dev->size != S2MPU_MMIO_SIZE)
		return -EINVAL;

	ret = ___pkvm_host_donate_hyp(dev->pa >> PAGE_SHIFT,
				      dev->size >> PAGE_SHIFT,
				      true);
	if (ret)
		return ret;

	for_each_sync_dev(sync_dev, dev) {
		ret = ___pkvm_host_donate_hyp(sync_dev->pa >> PAGE_SHIFT,
					      SYSMMU_SYNC_MMIO_SIZE >> PAGE_SHIFT,
					      true);
		if (ret)
			return ret;
		sync_dev->va = hyp_phys_to_virt(sync_dev->pa);
	}

	dev->va = hyp_phys_to_virt(dev->pa);

	ret = kvm_iommu_init_device(&dev->iommu);
	if (ret)
		return ret;

	if (dev->flags & S2MPU_HAS_TZ_SIBLING && ldfw_s2mpu_support) {
		if (call_ldfw(dev, SMC_S2MPU_TZ_REGISTER, dev->flags, 0) != 0)
			dev->flags &= ~S2MPU_HAS_TZ_SIBLING;
		else
			use_s2mpu_sec = true;
	} else if (dev->flags & S2MPU_HAS_TZ_SIBLING) {
		dev->flags &= ~S2MPU_HAS_TZ_SIBLING;
	}

	return ret;
}

static int s2mpu_init(void)
{
	u32 *smpt;
	phys_addr_t pa;
	unsigned int gb;
	int ret = 0;
	int smpt_nr_pages, smpt_size;
	struct mpt *in_mpt = (struct mpt *)kern_hyp_va(kvm_hyp_mpt);
	struct pkvm_iommu *dev;
	size_t s2mpu_arr_size = PAGE_ALIGN(sizeof(*kvm_hyp_s2mpus) * kvm_hyp_s2mpu_count);

	/* Get page table operations for this version. */
	mpt_ops = s2mpu_get_mpt_ops(mod_ops);
	if (!mpt_ops)
		return -EINVAL;

	smpt_size = mpt_ops->smpt_size();
	smpt_nr_pages = smpt_size / PAGE_SIZE;

	/* Take ownership of all SMPT buffers. This will also map them in. */
	for_each_gb(gb) {
		smpt = kern_hyp_va(in_mpt->fmpt[gb].smpt);
		pa = __hyp_pa(smpt);

		if (!IS_ALIGNED(pa, smpt_size)) {
			ret = -EINVAL;
			break;
		}

		ret = __pkvm_host_donate_hyp(pa >> PAGE_SHIFT, smpt_nr_pages);
		if (ret)
			break;

		host_mpt.fmpt[gb] = (struct fmpt){
			.smpt = smpt,
			.gran_1g = true,
			.prot = MPT_PROT_NONE,
		};
	}

	/* Try to return memory back if there was an error. */
	if (ret) {
		for_each_gb(gb) {
			smpt = host_mpt.fmpt[gb].smpt;
			if (!smpt)
				break;

			WARN_ON(__pkvm_hyp_donate_host(__hyp_pa(smpt) >> PAGE_SHIFT,
						       smpt_nr_pages));
		}
		memset(&host_mpt, 0, sizeof(host_mpt));
		return ret;
	}

	kvm_hyp_s2mpus = (struct pkvm_iommu *)kern_hyp_va(kvm_hyp_s2mpus);

	ret = __pkvm_host_donate_hyp(__hyp_pa(kvm_hyp_s2mpus) >> PAGE_SHIFT,
				     s2mpu_arr_size >> PAGE_SHIFT);
	if (ret)
		return ret;

	/* Pass MPT addresses to LDFW. Assume ldfw_s2mpu_support true,
	 * if SMC calls fails disable it
	 */
	ldfw_s2mpu_support = true;
	for_each_gb(gb) {
		phys_addr_t pa = __hyp_pa(host_mpt.fmpt[gb].smpt);

		if (pa) {
			if (call_ldfw(NULL, SMC_S2MPU_TZ_UPDATE_MPT_ADDR, gb, pa) != 0) {
				ldfw_s2mpu_support = false;
				break;
			}
		}
	}

	for_each_s2mpu(dev) {
		ret = s2mpu_register_dev(dev);
		if (ret)
			goto out_unregister;
	}

	return iommu_snapshot_host_stage2(NULL);

out_unregister:
	for_each_s2mpu(dev) {
		struct s2mpu_sync_dev *sync_dev;

		if (dev->va)
			WARN_ON(__pkvm_hyp_donate_host(dev->pa >> PAGE_SHIFT,
				dev->size >> PAGE_SHIFT));
		for_each_sync_dev(sync_dev, dev)
			if (sync_dev->va)
				WARN_ON(__pkvm_hyp_donate_host(sync_dev->pa >> PAGE_SHIFT,
							       SYSMMU_SYNC_MMIO_SIZE >>
							       PAGE_SHIFT));
	}
	WARN_ON(__pkvm_hyp_donate_host(__hyp_pa(kvm_hyp_s2mpus) >> PAGE_SHIFT,
				       s2mpu_arr_size >> PAGE_SHIFT));
	return ret;
}

static int s2mpu_alloc_domain(struct kvm_hyp_iommu_domain *domain, int type)
{
	WARN_ON(1);
	return 0;
}

static void s2mpu_free_domain(struct kvm_hyp_iommu_domain *domain)
{
	WARN_ON(1);
}

static void s2mpu_host_stage2_idmap(struct kvm_hyp_iommu_domain *domain,
				    phys_addr_t start, phys_addr_t end, int prot)
{
	struct pkvm_iommu *dev;

	/* Ignore large addresses. */
	end = min(end, (u64)SZ_1G * NR_GIGABYTES);
	if (start >= end)
		return;

	s2mpu_host_stage2_idmap_prepare(start, end, prot);
	for_each_s2mpu(dev) {
		if (!dev->iommu.power_is_off)
			s2mpu_host_stage2_idmap_apply(dev, start, end);
	}

	if (use_s2mpu_sec)
		call_ldfw(NULL, SMC_S2MPU_TZ_UPDATE_IDMAP, start, end);

	for_each_s2mpu(dev) {
		if (!dev->iommu.power_is_off)
			s2mpu_host_stage2_idmap_complete(dev);
	}

	if (use_s2mpu_sec)
		call_ldfw(NULL, SMC_S2MPU_TZ_INVALIDATE_COMPLETE, 0, 0);
}

struct kvm_iommu_ops s2mpu_hyp_ops = {
	.init				= s2mpu_init,
	.get_iommu_by_id		= s2mpu_id_to_iommu,
	.alloc_domain			= s2mpu_alloc_domain,
	.free_domain			= s2mpu_free_domain,
	.suspend			= s2mpu_suspend,
	.resume				= s2mpu_resume,
	.host_stage2_idmap		= s2mpu_host_stage2_idmap,
	.dabt_handler			= s2mpu_drv_host_dabt_handler,
};

/* Validate that module ops used within this driver are not null. */
static bool validate_mod_ops(const struct pkvm_module_ops *ops)
{
	if (!ops ||
	    !ops->flush_dcache_to_poc ||
	    !ops->hyp_donate_host ||
	    !ops->host_donate_hyp ||
	    !ops->kern_hyp_va ||
	    !ops->hyp_pa ||
	    !ops->memset ||
	    !ops->memcpy)
		return false;

	return true;
}

#ifdef S2MPU_V9
static bool smc_filter(struct user_pt_regs *pt)
{
	u32 func_id = (u32)pt->regs[0];

	if (func_id >= SMC_S2MPU_TZ_REGISTER && func_id <= SMC_S2MPU_TZ_MAX) {
		pt->regs[0] = SMCCC_RET_NOT_SUPPORTED;
		return true;
	}

	return false;
}
#endif

/* Called when EL1 loads the module. */
int s2mpu_hyp_init(const struct pkvm_module_ops *ops)
{
	mod_ops = ops;
	if (!validate_mod_ops(mod_ops))
		return -EINVAL;

#ifdef S2MPU_V9
	/* Register SMC handler to block EL1 from calling S2MPU SMCs */
	CALL_FROM_OPS(register_host_smc_handler, smc_filter);
#endif
	return 0;
}
