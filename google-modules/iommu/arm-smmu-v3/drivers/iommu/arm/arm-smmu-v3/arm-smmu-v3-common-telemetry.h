/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ARM_SMMU_V3_COMMON_TELEMETRY_H
#define _ARM_SMMU_V3_COMMON_TELEMETRY_H

#include <linux/mutex.h>

#include "pkvm/hyp-arm-smmu-v3-common-telemetry.h"

/* There is a circular dependency around this struct, and hence forward-declare here */
struct arm_smmu_device;

enum smmu_driver_mode {
	NON_PKVM_MODE_DRIVER,
	PKVM_MODE_DRIVER,
};

enum smmu_domain_type {
	DOMAIN_TYPE_UNATTACHED,
	DOMAIN_TYPE_S1,
	DOMAIN_TYPE_S2,
	DOMAIN_TYPE_NESTED,
};

enum evtq_fault_type {
	SMMU_EVTQ_BAD_STREAMID_CONFIG,
	SMMU_EVTQ_STE_FETCH_FAULT,
	SMMU_EVTQ_BAD_STE_CONFIG,
	SMMU_EVTQ_STREAM_DISABLED_FAULT,
	SMMU_EVTQ_BAD_SUBSTREAMID_CONFIG,
	SMMU_EVTQ_CD_FETCH_FAULT,
	SMMU_EVTQ_BAD_CD_CONFIG,
	SMMU_EVTQ_TRANSLATION_FAULT,
	SMMU_EVTQ_ADDR_SIZE_FAULT,
	SMMU_EVTQ_ACCESS_FAULT,
	SMMU_EVTQ_PERMISSION_FAULT,
	SMMU_EVTQ_VMS_FETCH_FAULT,
	SMMU_EVTQ_UNKNOWN_FAULT,
	SMMU_EVTQ_FAULT_COUNT
};

enum smmu_gerror_type {
	SMMU_GERROR_SFM,
	SMMU_GERROR_EVTQ_ABT,
	SMMU_GERROR_NUM,
};

/**
 * struct kvm_arm_smmu_domain_telemetry - Holding domain telemetry data from host side pKVM driver
 *
 * @domain_id - Uniqe domain id associated with each domain in pKVM mode
 * @domain - Host side arm smmu domain structure
 * @map_sg_count - Total count of iommu_map_sg() calls
 * @sg_len_total - Total length of all sg lists passed to iommu_map_sg()
 * @iova_min - Min IOVA ever mapped to this domain
 * @iova_max - Max IOVA ever mapped to this domain
 * @hasdt (hyp_arm_smmu_domain_telemetry) - Pointing to hyp accessed domain telemetry structure
 */
struct kvm_arm_smmu_domain_telemetry {
	int domain_id;
	struct kvm_arm_smmu_domain *domain;
	atomic64_t map_sg_count;
	atomic64_t map_count;
	atomic64_t unmap_count;
	atomic64_t sg_len_total;
	atomic64_t iova_min;
	atomic64_t iova_max;
	struct hyp_arm_smmu_domain_telemetry *hasdt;
};

/**
 * struct arm_smmu_domain_telemetry - Holding domain telemetry data for non-pKVM driver
 *
 * @domain - Pointer back to the arm smmu domain
 */
struct arm_smmu_domain_telemetry {
	struct arm_smmu_domain *domain;
};

/**
 * struct iova_pa_alignment_stats - Per-domain stats for IOVA/PA alignment.
 *
 * Tracks the number of times an IOVA->PA mapping was unaligned for a given
 * block size, indicating a missed opportunity for block mapping.
 */
struct iova_pa_alignment_stats {
	atomic64_t unaligned_64k_mappings;
	atomic64_t unaligned_2m_mappings;
	atomic64_t unaligned_32m_mappings;
	atomic64_t unaligned_512m_mappings;
	atomic64_t unaligned_1g_mappings;
};

/**
 * struct arm_smmu_domain_telemetry_common - A common domain telemetry structure across both modes
 *                                           of SMMU driver
 *
 * @mode: Operating mode for the domain (from enum smmu_driver_mode)
 * @type: Type of domain (S1, S2, or unattached)
 * @domain_kobj: kobj for per domain sysfs
 * @domain_lock: Protects domain pointer
 * @ptw_lock: Serializes PTW walks
 * @alignment_stats: Per-domain stats for IOVA/PA alignment
 * @data: This holds actual telemetry data for the given domain. Now, this structure is either
 *         going to fill by non-pKVM mode or pKVM mode. Telemetry data holding structures for both
 *         mode are different. Use union to save some struct space as a domain will be exclusive
 *         to one of the operating mode of the driver.
 *
 *         kasdt = kvm_arm_smmu_domain_telemetry
 *         asdt = arm_smmu_domain_telemetry
 */
struct arm_smmu_domain_telemetry_common {
	enum smmu_driver_mode mode;
	enum smmu_domain_type type;
	struct kobject domain_kobj;
	struct mutex domain_lock; /* Protects domain pointer */
	struct mutex ptw_lock;    /* Serializes PTW walks */
	struct iova_pa_alignment_stats alignment_stats;
	/*
	 * A domain would be either operating under pKVM mode
	 * or non-pKVM mode. Not both.
	 */
	union domain_telemetry_mode {
		struct kvm_arm_smmu_domain_telemetry kasdt;
		struct arm_smmu_domain_telemetry asdt;
	} data;
};

#define MAX_PTW_RETRY_COUNT 10

/**
 * struct kvm_arm_smmu_device_telemetry - Holding device telemetry data from host side pKVM driver
 * @device_id: device identifier from host perspective
 * @evtq_fault_counts: Counter per fault type
 * @gerrors: Array of gerror counters
 * @hasdevt: (hyp_arm_smmu_device_telemetry) - Pointing to hyp accessed device telemetry structure
 * @dev: Pointer to struct device for this smmu. This helps in printing device specific logs
 */
struct kvm_arm_smmu_device_telemetry {
	int device_id;
	atomic64_t evtq_fault_counts[SMMU_EVTQ_FAULT_COUNT];
	atomic64_t gerrors[SMMU_GERROR_NUM];
	struct hyp_arm_smmu_device_telemetry *hasdevt;
	struct device *dev;
};

/**
 * struct arm_smmu_device_telemetry - Holding device telemetry data for non-pKVM driver
 */
struct arm_smmu_device_telemetry {
	// TODO: add something
};

/**
 * struct arm_smmu_device_telemetry_common - A common device telemetry structure across both modes
 * @device_kobj: kobject for device telemetry sysfs entries
 * @mode: Mode of driver: 0 for non-pKVM, 1 for pKVM
 * @data:
 *         kasdevt = kvm_arm_smmu_device_telemetry
 *         asdevt = arm_smmu_device_telemetry
 */
struct arm_smmu_device_telemetry_common {
	struct kobject device_kobj;
	int mode;
	union device_telemetry_mode {
		struct kvm_arm_smmu_device_telemetry kasdevt;
		struct arm_smmu_device_telemetry asdevt;
	} data;
};

/* Collection of telemetry data recording APIs */
void arm_smmu_tlm_rec_failed_cookie_alloc(void);
void arm_smmu_dom_tlm_inc_map_sg_cnt(struct arm_smmu_domain_telemetry_common *asdtc);
void arm_smmu_dom_tlm_rec_sg_len(struct arm_smmu_domain_telemetry_common *asdtc,
				 unsigned int sg_list_len);
void arm_smmu_dom_tlm_rec_iova_range(struct arm_smmu_domain_telemetry_common *asdtc, u64 iova,
				     size_t size);
void arm_smmu_dom_tlm_rec_iova_pa_alignment(struct arm_smmu_domain_telemetry_common *asdtc,
					    unsigned long iova, phys_addr_t paddr, size_t size);

void arm_smmu_dom_tlm_rec_domain_id(struct arm_smmu_domain_telemetry_common *asdtc,
				    pkvm_handle_t domain_id, bool nested);
void arm_smmu_dev_tlm_rec_dev_id(struct arm_smmu_device_telemetry_common *asdevtc,
				 pkvm_handle_t device_id);
void arm_smmu_dev_tlm_rec_evtq_fault(struct arm_smmu_device_telemetry_common *asdevtc, u8 evt_id);
void arm_smmu_dev_tlm_inc_gerror_cnt(struct arm_smmu_device_telemetry_common *asdevtc,
				     enum smmu_gerror_type type);

/**
 * arm_smmu_device_telemetry_alloc - Allocate and initialize telemetry for a device
 * @smmu_device: pointer to the arm_smmu_device that is being tracked
 * @mode: indicates if the device is for pKVM or not
 *
 * Return: 0 on success, < 0 on failure
 */
struct arm_smmu_device_telemetry_common
*arm_smmu_device_telemetry_alloc(struct arm_smmu_device *smmu_device, int mode);

/**
 * arm_smmu_device_telemetry_free - Free telemetry for a device
 * @smmu_device: pointer to the arm_smmu_device that is being tracked
 * @mode: indicates if the device is for pKVM or not
 */
void arm_smmu_device_telemetry_free(struct arm_smmu_device *smmu_device, int mode);

void arm_smmu_telemetry_register_hvc(void);
extern int ptw_hvc_num;

void arm_smmu_dom_tlm_map_end(struct arm_smmu_domain_telemetry_common *asdtc);
void arm_smmu_dom_tlm_unmap_end(struct arm_smmu_domain_telemetry_common *asdtc);

/* API to copy shared_tel pointer from kvm driver to telemetry file */
void arm_smmu_set_shared_telemetry_ptr(struct hyp_shared_arm_smmu_telemetry *shared_tel);

/**
 * arm_smmu_domain_telemetry_alloc - Allocate and initialize telemetry for a domain
 * @domain: pointer to the arm_smmu_domain that is being tracked
 * @mode: indicates if the domain is for pKVM or not
 *
 * Return: 0 on success, <0 on failure
 */
int arm_smmu_domain_telemetry_alloc(void *domain, int mode);

/**
 * arm_smmu_domain_telemetry_free - Free telemetry for a domain
 * @domain: pointer to the arm_smmu_domain that is being tracked
 * @mode: indicates if the domain is for pKVM or not
 */
void arm_smmu_domain_telemetry_free(void *domain, int mode);

#endif /* _ARM_SMMU_V3_COMMON_TELEMETRY_H */
