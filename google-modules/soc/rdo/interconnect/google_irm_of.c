// SPDX-License-Identifier: GPL-2.0-only
/*
 * IRM driver DT parsing routines
 *
 * Copyright (C) 2024 Google LLC.
 */

#include "google_irm_of.h"
#include "google_irm_idx_internal.h"

static int __of_google_irm_parse_clients(struct irm_dev *irm_dev)
{
	struct device_node *np, *child_np;
	struct device *dev;
	struct irm_client *client;
	int num_fabric_name;
	u32 idx;
	bool is_defined[IRM_IDX_NUM];
	int ret;

	dev = irm_dev->dev;
	np = dev->of_node;

	for_each_available_child_of_node(np, child_np) {
		if (of_property_read_u32(child_np, "google,irm-client-index", &idx) < 0) {
			dev_err(dev, "Read google,irm-client-index failed.\n");
			return -EINVAL;
		}

		if (idx >= IRM_IDX_NUM) {
			dev_err(dev, "Invalid google,irm-client-index value (%u)\n", idx);
			return -EINVAL;
		}

		if (is_defined[idx]) {
			dev_err(dev, "Duplicate google,irm-client-index value (%u) in %s\n",
				idx, of_node_full_name(child_np));
		}

		is_defined[idx] = true;
		client = &irm_dev->client[idx];

		num_fabric_name = of_property_count_strings(child_np, "google,fabric-names");
		if (num_fabric_name <= 0) {
			dev_err(dev, "Client %d: read google,fabric-names failed (%d).\n",
				idx, num_fabric_name);
			return -EINVAL;
		}
		client->num_fabric = (u32)num_fabric_name;

		client->fabric_name_arr = devm_kcalloc(dev, client->num_fabric,
						       sizeof(*client->fabric_name_arr),
						       GFP_KERNEL);
		if (!client->fabric_name_arr)
			return -ENOMEM;

		ret = of_property_read_string_array(child_np, "google,fabric-names",
						    client->fabric_name_arr, client->num_fabric);
		if (ret < 0) {
			dev_err(dev, "Client %d: read google,fabric-names array failed (%d).\n",
				idx, ret);
			return -EINVAL;
		}
	}

	return 0;
}

int of_google_irm_init(struct irm_dev *irm_dev)
{
	if (!irm_dev)
		return -EINVAL;

	return __of_google_irm_parse_clients(irm_dev);
}
