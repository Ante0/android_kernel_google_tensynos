/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 HiSilicon Limited.
 */

#ifndef _KERNEL_IOMMU_BENCHMARK_H
#define _KERNEL_IOMMU_BENCHMARK_H

#define IOMMU_MAP_BENCHMARK       _IOWR('d', 1, struct iommu_map_benchmark)
#define IOMMU_MAP_MAX_THREADS     1024
#define IOMMU_MAP_MAX_SECONDS     300
#define IOMMU_MAP_MAX_TRANS_DELAY (10 * NSEC_PER_MSEC)

struct iommu_map_benchmark {
	__u64 avg_map_ns; /* average map latency in ns */
	__u64 map_stddev; /* standard deviation of map latency */
	__u64 avg_unmap_ns; /* as above */
	__u64 unmap_stddev;
	__u32 threads; /* how many threads will do map/unmap in parallel */
	__u32 seconds; /* how long the test will last */
	__s32 node; /* which numa node this benchmark will run on */
	__u32 dma_bits; /* DMA addressing capability */
	__u32 dma_trans_ns; /* time for DMA transmission in ns */
	__u32 num_pages;  /* how many PAGE_SIZE will do map/unmap once a time */
	__u32 num_mappings;
};
#endif /* _KERNEL_IOMMU_BENCHMARK_H */
