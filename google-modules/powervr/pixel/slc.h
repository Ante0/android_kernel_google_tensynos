/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2022-2023 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#pragma once

#include <linux/atomic.h>
#include <soc/google/pt.h>

/**
 * DOC: SLC partition management
 *
 * Key definitions:
 * + Partition index - The unique index of a partition, relative to the dt node that owns it.
 *                     This index is used when communicating with the underlying SLC driver.
 * + ptid - This is the HW level ID associated with an enabled partition. These id's are allocated
 *          at partition enable time. The GPU driver will never directly use the ptid, but will
 *          track it.
 *          External analysis of the caching behavior (e.g. hit and eviction counters), are
 *          associated with a ptid, not a physical partition index.
 *          This driver attempts to hold on to any allocated ptids until driver termination to make
 *          profiling of caching performance easier.
 * + PBHA - Acronym: Page Based Hardware Attributes. Every physical partition has a PBHA value
 *          associated with it. We insert these attributes into PTEs so that transactions with a
 *          page carry the PBHA within their high bits.
 *          Transactions with PBHA bits set are intercepted by the SLC, where the corresponding
 *          partition and it's caching behavior (Read/write alloc etc.) are looked up and applied to
 *          the transaction.
 */
#define NUM_GPU_DATA_PORTS   (3)
#define NUM_PTID             (32)
#define NUM_PBHA             (16)
#define NUM_OSID             (8)

/**
 * DOC: PBHA
 *
 * IPA[39:36] as GPU internal PBHA
 *
 * The main purpose of GPU internal PBHA is to index the RA internal LUT for driving the PID, which
 * enables the GPU driver to conduct the page-based PID assignment for SLC.
 * The 'PBHA' used in this specification is the 4-MSB of IPA.
 */
#define PBHA_BIT_POS     (36)
#define PBHA_MASK        (NUM_PBHA - 1)
#define PBHA_WRITE_ALLOC (0x5)
#define PBHA_DEFAULT     PBHA_MASK

enum requester_id {
	REQUESTER_ID_IPP = 0,
	REQUESTER_ID_DCE,
	REQUESTER_ID_TDM,
	REQUESTER_ID_PM,
	REQUESTER_ID_CDM,
	REQUESTER_ID_META_GARTEN,
	REQUESTER_ID_META_DMA,
	REQUESTER_ID_TILING,
	REQUESTER_ID__UNKNOWN1,
	REQUESTER_ID__UNKNOWN2,
	REQUESTER_ID_MCU,
	REQUESTER_ID_TCU,
	REQUESTER_ID_PBE,
	REQUESTER_ID__UNKNOWN3,
	REQUESTER_ID_IPF,
	REQUESTER_ID__UNKNOWN4,
	REQUESTER_ID_ISP,
	REQUESTER_ID_GEOM,
	REQUESTER_ID__UNKNOWN5,
	REQUESTER_ID__UNKNOWN6,
	REQUESTER_ID__UNKNOWN7,
	REQUESTER_ID_TPF,
	REQUESTER_ID_PDSRW,
	REQUESTER_ID_PDS,
	REQUESTER_ID_USC,
	REQUESTER_ID__UNKNOWN8,
	REQUESTER_ID__UNKNOWN9,
	REQUESTER_ID__UNKNOWN10,
	REQUESTER_ID__UNKNOWN11,
	REQUESTER_ID_FBM,
	REQUESTER_ID__UNKNOWN12,
	REQUESTER_ID_MMU,
	REQUESTER_ID__MAX,
};

/**
 * struct ra_table_entry - Bitfield for RA LUT entries.
 */
struct ra_table_entry {
	/** @tbl_arpid: partition ID value to be assigned to the client read stream */
	u8 tbl_arpid    : 6;
	/** @ign_pt_arpid: selection to choose between initiator PID value vs RA table PID value */
	u8 ign_pt_arpid : 1;
	u8              : 0;
	/** @tbl_awpid: partition ID value to be assigned to the client write stream */
	u8 tbl_awpid    : 6;
	/** @ign_pt_awpid: selection to choose between initiator PID value vs RA table PID value */
	u8 ign_pt_awpid : 1;
	u8              : 0;
};
static_assert(sizeof(struct ra_table_entry) == 2, "incorrect ra table entry size");

/**
 * struct slc_partition - Structure for tracking partition state.
 */
struct slc_partition {
	/** @index: The active partition ID for this virtual partition */
	u32 index;

	/** @ptid: The active partition ID for this virtual partition */
	ptid_t ptid;

	/** @ra_entry: Encodes the ptid for ra/wa or both */
	struct ra_table_entry ra_entry;

	/** @pbha: The page based HW attributes for this partition */
	ptpbha_t pbha;

	/** @state: Current state of the SLC partition */
	atomic_t state;
};

/**
 * struct slc_data - Structure for tracking SLC context.
 */
struct slc_data {
	/** @pt_handle: Link to ACPM SLC partition data */
	struct pt_handle *pt_handle;

	/** @partition: Information specific to an individual SLC partition */
	struct slc_partition partition;

	/** @dev: Inherited pointer to device attached */
	struct device *dev;

	/** @transition_work: Work item used to queue asynchronous SLC partition transition ops. */
	struct work_struct transition_work;

	/** @req_pbha_lut: REQ_PBHA_LUT CSRs - see @shadow.req_pbha_lut */
	u32 __iomem *req_pbha_lut;

	/** @ra_lut: RA LUT CSRs - see @shadow.ra_lut */
	u32 __iomem *ra_lut[NUM_GPU_DATA_PORTS];

	struct {
		/** @shadow.req_pbha_lut: RA LUT from internal pbha (+OSID) to SLC PID */
		u8 req_pbha_lut[REQUESTER_ID__MAX * NUM_PBHA];

		/** @shadow.ra_lut: RA LUT from internal pbha (+OSID) to SLC PID */
		struct ra_table_entry ra_lut[NUM_OSID * NUM_PTID];
	} shadow;
};

#if defined(CONFIG_POWERVR_PIXEL_SLC)
int slc_init_data(struct slc_data *data, struct device *dev);

void slc_term_data(struct slc_data *data);

void slc_program_lut(struct slc_data *data);

void slc_enable(struct slc_data *data);

void slc_disable(struct slc_data *data);
#else
static __maybe_unused int slc_init_data(struct slc_data *data, struct device *dev)
{
	return 0;
}

static __maybe_unused void slc_term_data(struct slc_data *data)
{
}

static __maybe_unused void slc_program_lut(struct slc_data *data)
{
}

static __maybe_unused void slc_enable(struct slc_data *data)
{
}

static __maybe_unused void slc_disable(struct slc_data *data)
{
}
#endif /* defined(CONFIG_POWERVR_PIXEL_SLC) */
