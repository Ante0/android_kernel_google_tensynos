#ifndef _HARNESS_DRIVER_MAPPER_H_
#define _HARNESS_DRIVER_MAPPER_H_

#include "noa_test_harness.h"

/**
 * struct noa_harness_mapping_params - Parameters for memory mapping operations.
 * @cpu_addr:	The CPU virtual address to be mapped.
 * @size:		The size of the memory region to be mapped.
 */
struct noa_harness_mapping_params {
	void *cpu_addr;
	size_t size;
};

/**
 * noa_harness_memory_mapper_init() - Initializes the memory mapper.
 * @mapper: Pointer to the memory mapper structure.
 * @dev: Pointer to the device structure.
 * @dpa: Pointer to the Google DPA structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_memory_mapper_init(struct noa_harness_memory_mapper *mapper, struct device *dev,
				   struct google_dpa *dpa);

/**
 * noa_harness_memory_mapper_deinit() - Deinitializes the memory mapper.
 * @mapper: Pointer to the memory mapper structure.
 */
void noa_harness_memory_mapper_deinit(struct noa_harness_memory_mapper *mapper);

/**
 * noa_harness_memory_mapper_remap() - Maps a CPU address to a DPA-visible address.
 * @mapper: Pointer to the memory mapper structure.
 * @params: Pointer to the mapping parameters.
 * @dpa_va: Pointer to store the resulting DPA virtual address.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_memory_mapper_remap(struct noa_harness_memory_mapper *mapper,
				    struct noa_harness_mapping_params *params, u64 *dpa_va);

/**
 * noa_harness_memory_mapper_unmap() - Unmaps a previously mapped memory region.
 * @mapper: Pointer to the memory mapper structure.
 * @params: Pointer to the mapping parameters identifying the region to unmap.
 */
void noa_harness_memory_mapper_unmap(struct noa_harness_memory_mapper *mapper,
				     struct noa_harness_mapping_params *params);

#endif /* _HARNESS_DRIVER_MAPPER_H_ */
