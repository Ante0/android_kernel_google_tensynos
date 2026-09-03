// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 HiSilicon Limited.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>
#include "iommu_map_benchmark.h"

#define NSEC_PER_MSEC	1000000L

int main(int argc, char **argv)
{
	struct iommu_map_benchmark map;
	int fd, opt;
	/* default single thread, run 20 seconds on NUMA_NO_NODE */
	int threads = 1, seconds = 20, node = -1;
	/* default dma mask 32bit */
	int bits = 32, xdelay = 0;
	/* default 1 page*/
	int num_pages = 1;
	int num_mappings = 0;

	int cmd = IOMMU_MAP_BENCHMARK;
	char *p;

	while ((opt = getopt(argc, argv, "t:s:n:b:x:p:m:")) != -1) {
		switch (opt) {
		case 't':
			threads = atoi(optarg);
			break;
		case 's':
			seconds = atoi(optarg);
			break;
		case 'n':
			node = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'x':
			xdelay = atoi(optarg);
			break;
		case 'p':
			num_pages = atoi(optarg);
			break;
		case 'm':
			num_mappings = atoi(optarg);
			break;
		default:
			return -1;
		}
	}

	if (threads <= 0 || threads > IOMMU_MAP_MAX_THREADS) {
		fprintf(stderr, "invalid number of threads, must be in 1-%d\n",
			IOMMU_MAP_MAX_THREADS);
		exit(1);
	}

	if (seconds <= 0 || seconds > IOMMU_MAP_MAX_SECONDS) {
		fprintf(stderr, "invalid number of seconds, must be in 1-%d\n",
			IOMMU_MAP_MAX_SECONDS);
		exit(1);
	}

	if (xdelay < 0 || xdelay > IOMMU_MAP_MAX_TRANS_DELAY) {
		fprintf(stderr, "invalid transmit delay, must be in 0-%ld\n",
			IOMMU_MAP_MAX_TRANS_DELAY);
		exit(1);
	}

	/* suppose the mininum DMA zone is 1MB in the world */
	if (bits < 20 || bits > 64) {
		fprintf(stderr, "invalid dma mask bit, must be in 20-64\n");
		exit(1);
	}

	if (num_pages < 1) {
		fprintf(stderr, "invalid number of pages\n");
		exit(1);
	}

	fd = open("/sys/kernel/debug/iommu_map_benchmark", O_RDWR);
	if (fd == -1) {
		perror("open");
		exit(1);
	}

	memset(&map, 0, sizeof(map));
	map.seconds = seconds;
	map.threads = threads;
	map.node = node;
	map.dma_bits = bits;
	map.dma_trans_ns = xdelay;
	map.num_pages = num_pages;
	map.num_mappings = num_mappings;

	if (ioctl(fd, cmd, &map)) {
		perror("ioctl");
		exit(1);
	}

	printf("iommu mapping benchmark: threads:%d seconds:%d node:%d pages:%d mappings:%d\n",
	       threads, seconds, node, num_pages, num_mappings);
	printf("average map latency(ns):%lu standard deviation:%lu\n",
	       (unsigned long)map.avg_map_ns, (unsigned long)map.map_stddev);
	printf("average unmap latency(ns):%lu standard deviation:%lu\n",
	       (unsigned long)map.avg_unmap_ns, (unsigned long)map.unmap_stddev);

	return 0;
}
