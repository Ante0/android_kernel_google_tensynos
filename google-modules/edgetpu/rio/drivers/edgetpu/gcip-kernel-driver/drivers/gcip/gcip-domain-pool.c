// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP IOMMU domain allocator.
 *
 * Copyright (C) 2022-2025 Google LLC
 */

 #include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#include <linux/of.h>

#include <gcip/gcip-domain-pool.h>
#include <gcip/gcip-iommu.h>

int gcip_domain_pool_init(struct gcip_domain_pool *pool, struct device *dev, int size,
			  enum gcip_iommu_domain_type domain_type, size_t granule)
{
	struct gcip_iommu_domain *gdomain;
	struct iommu_domain *domain;
	dma_addr_t space_daddr, reserved_daddr;
	size_t space_size, reserved_size;
	u32 pasid_num_bits;
	int ret;
	int i;

	if (!size || !is_power_of_2(granule))
		return -EINVAL;

	ret = of_property_read_u32(dev->of_node, "pasid-num-bits", &pasid_num_bits);
	if (ret) {
		dev_err(dev, "Failed to fetch pasid-num-bits (%d)\n", ret);
		return ret;
	}

	ret = gcip_iommu_get_space_config(dev, &space_daddr, &space_size, &reserved_daddr,
					       &reserved_size);
	if (ret)
		return ret;

	pool->size = size;
	pool->dev = dev;
	pool->min_pasid = 1;
	pool->max_pasid = BIT(pasid_num_bits) - 1;

	ida_init(&pool->pasid_pool);
	ida_init(&pool->idp);

	pool->array = devm_kcalloc(dev, size, sizeof(*pool->array), GFP_KERNEL);
	if (!pool->array) {
		ret = -ENOMEM;
		goto err_free_ida;
	}

	for (i = 0; i < size; i++) {
		domain = iommu_domain_alloc(dev->bus);
		if (!domain)
			domain = ERR_PTR(-ENOMEM);
		if (IS_ERR(domain)) {
			dev_err(pool->dev, "Failed to allocate iommu domain %d of %u\n", i + 1,
				size);
			ret = PTR_ERR(domain);
			goto err_free_domain;
		}

		gdomain = gcip_iommu_domain_create(dev, domain, domain_type, space_daddr,
						   space_size, reserved_daddr, reserved_size,
						   granule);
		if (IS_ERR(gdomain)) {
			iommu_domain_free(domain);
			ret = PTR_ERR(gdomain);
			goto err_free_domain;
		}

		pool->array[i] = gdomain;
	}

	return 0;

err_free_domain:
	while (i--) {
		domain = pool->array[i]->domain;
		gcip_iommu_domain_destroy(pool->array[i]);
		iommu_domain_free(domain);
	}
err_free_ida:
	devm_kfree(pool->dev, pool->array);
	ida_destroy(&pool->idp);
	ida_destroy(&pool->pasid_pool);

	return ret;
}

void gcip_domain_pool_exit(struct gcip_domain_pool *pool)
{
	struct iommu_domain *domain;
	int i;

	for (i = 0; i < pool->size; i++) {
		domain = pool->array[i]->domain;
		gcip_iommu_domain_destroy(pool->array[i]);
		iommu_domain_free(domain);
	}

	devm_kfree(pool->dev, pool->array);
	ida_destroy(&pool->idp);
	ida_destroy(&pool->pasid_pool);
}

struct gcip_iommu_domain *gcip_domain_pool_alloc(struct gcip_domain_pool *pool)
{
	int id;

	id = ida_alloc_max(&pool->idp, pool->size - 1, GFP_KERNEL);

	if (id < 0) {
		dev_err(pool->dev, "No more domains available from pool of size %u\n", pool->size);
		return ERR_PTR(-ENOSPC);
	}

	dev_dbg(pool->dev, "Allocated domain from pool with id = %d\n", id);

	return pool->array[id];
}

void gcip_domain_pool_free(struct gcip_domain_pool *pool, struct gcip_iommu_domain *domain)
{
	int id;

	for (id = 0; id < pool->size; id++) {
		if (pool->array[id] == domain) {
			dev_dbg(pool->dev, "Released domain from pool with id = %d\n", id);
			ida_free(&pool->idp, id);
			return;
		}
	}
	dev_err(pool->dev, "Domain not found in pool\n");
}

int gcip_domain_pool_attach(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain)
{
	int ret;
	int pasid;

	if (gdomain->pasid != IOMMU_PASID_INVALID)
		/* Already attached. */
		return gdomain->pasid;

	pasid = ida_alloc_range(&pool->pasid_pool, pool->min_pasid, pool->max_pasid, GFP_KERNEL);
	if (pasid < 0)
		return pasid;

	ret = iommu_attach_device_pasid(gdomain->domain, pool->dev, pasid);
	if (ret) {
		ida_free(&pool->pasid_pool, pasid);
		return ret;
	}

	gdomain->pasid = pasid;

	return ret;
}

void gcip_domain_pool_detach(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain)
{
	if (gdomain->pasid == IOMMU_PASID_INVALID)
		return;

	iommu_detach_device_pasid(gdomain->domain, pool->dev, gdomain->pasid);
	ida_free(&pool->pasid_pool, gdomain->pasid);
	gdomain->pasid = IOMMU_PASID_INVALID;
}
