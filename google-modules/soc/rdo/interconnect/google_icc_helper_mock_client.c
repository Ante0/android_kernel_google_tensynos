// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver to provide debugfs knobs to enable calling ICC helper APIs by writing
 * the debugfs knobs
 *
 * For example, a user writes following debugfs knobs
 *   # cd /sys/kernel/debug/google_icc_helper_mock_client/<path>
 *   # echo 200 > read_avg_bw_vc_0
 *   # echo 300 > read_avg_bw_vc_3
 *   # echo 100 > read_avg_bw_gslc_vc_0
 *   # echo 1000 > read_latency_vc_1
 *   # echo 2000 > read_latency_gslc_vc_1
 *   # echo 1 > update_async
 *
 * The mock client driver calls following ICC helper APIs to <path>
 *   google_icc_set_read_bw_gmc(<path>, 200, 0, 0, 0);
 *   google_icc_set_read_bw_gmc(<path>, 300, 0, 0, 3);
 *   google_icc_set_read_bw_gslc(<path>, 100, 0, 0, 0);
 *   google_icc_set_read_latency_gmc(<path>, 1000, 0xFFFFFFFF, 1);
 *   google_icc_set_read_latency_gslc(<path>, 2000, 0xFFFFFFFF, 1);
 *   google_icc_update_constraint_async(<path>);
 *
 * Then the user writes following debugfs knobs
 *   # echo 100 > read_peak_bw_vc_0
 *   # echo 1 > update_async
 *
 * The mock client driver calls following ICC helper APIs to <path>
 *   google_icc_set_write_bw_gmc(<path>, 200, 100, 0, 0);
 *   google_icc_update_constraint_async(<path>);
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/debugfs.h>
#include <linux/interconnect.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <interconnect/google_icc_helper.h>

enum {
	ICC_HELPER_UPDATE_TYPE_ASYNC = 0,
	ICC_HELPER_UPDATE_TYPE_SYNC,
	ICC_HELPER_UPDATE_TYPE_NUM,
};

enum {
	ICC_HELPER_API_TYPE_READ_AVG_BW = 0,
	ICC_HELPER_API_TYPE_READ_PEAK_BW,
	ICC_HELPER_API_TYPE_READ_RT_BW,
	ICC_HELPER_API_TYPE_READ_LATENCY,
	ICC_HELPER_API_TYPE_READ_LTV,
	ICC_HELPER_API_TYPE_WRITE_AVG_BW,
	ICC_HELPER_API_TYPE_WRITE_PEAK_BW,
	ICC_HELPER_API_TYPE_WRITE_RT_BW,
	ICC_HELPER_API_TYPE_WRITE_LATENCY,
	ICC_HELPER_API_TYPE_WRITE_LTV,
	ICC_HELPER_API_TYPE_READ_AVG_BW_GSLC,
	ICC_HELPER_API_TYPE_READ_PEAK_BW_GSLC,
	ICC_HELPER_API_TYPE_READ_RT_BW_GSLC,
	ICC_HELPER_API_TYPE_READ_LATENCY_GSLC,
	ICC_HELPER_API_TYPE_READ_LTV_GSLC,
	ICC_HELPER_API_TYPE_WRITE_AVG_BW_GSLC,
	ICC_HELPER_API_TYPE_WRITE_PEAK_BW_GSLC,
	ICC_HELPER_API_TYPE_WRITE_RT_BW_GSLC,
	ICC_HELPER_API_TYPE_WRITE_LATENCY_GSLC,
	ICC_HELPER_API_TYPE_WRITE_LTV_GSLC,
	ICC_HELPER_API_TYPE_NUM,
};

static const char *node_name_format[ICC_HELPER_API_TYPE_NUM] = {
	"read_avg_bw_vc_%d",
	"read_peak_bw_vc_%d",
	"read_rt_bw_vc_%d",
	"read_latency_vc_%d",
	"read_ltv_vc_%d",
	"write_avg_bw_vc_%d",
	"write_peak_bw_vc_%d",
	"write_rt_bw_vc_%d",
	"write_latency_vc_%d",
	"write_ltv_vc_%d",
	"read_avg_bw_gslc_vc_%d",
	"read_peak_bw_gslc_vc_%d",
	"read_rt_bw_gslc_vc_%d",
	"read_latency_gslc_vc_%d",
	"read_ltv_gslc_vc_%d",
	"write_avg_bw_gslc_vc_%d",
	"write_peak_bw_gslc_vc_%d",
	"write_rt_bw_gslc_vc_%d",
	"write_latency_gslc_vc_%d",
	"write_ltv_gslc_vc_%d",
};

static const u32 param_default_val[ICC_HELPER_API_TYPE_NUM] = {
	0,
	0,
	0,
	U32_MAX,	/* latency */
	U32_MAX,	/* LTV */
	0,
	0,
	0,
	U32_MAX,	/* latency */
	U32_MAX,	/* LTV */
	0,
	0,
	0,
	U32_MAX,	/* latency */
	U32_MAX,	/* LTV */
	0,
	0,
	0,
	U32_MAX,	/* latency */
	U32_MAX,	/* LTV */
};

struct icc_helper_test_path {
	struct device *dev;
	struct dentry *dir;
	/* prevent data in elem_arr from being changed while used by icc_update_set_common() */
	struct mutex mutex;
	struct google_icc_path *icc_path;
	struct icc_helper_test_element **elem_arr;
};

struct icc_helper_test_element {
	struct icc_helper_test_path *test_path;
	bool is_update;
	u32 value;
};

static struct dentry *base_dir;
static u32 num_vc;
static struct icc_helper_test_path *test_path_arr;
static u32 update_err_code[ICC_HELPER_UPDATE_TYPE_NUM];

static int icc_param_set(void *data, u64 val)
{
	struct icc_helper_test_element *elem = (struct icc_helper_test_element *)data;
	struct icc_helper_test_path *test_path = elem->test_path;

	mutex_lock(&test_path->mutex);

	elem->value = (u32)val;
	elem->is_update = true;

	mutex_unlock(&test_path->mutex);

	return 0;
}

static int icc_param_get(void *data, u64 *val)
{
	struct icc_helper_test_element *elem = (struct icc_helper_test_element *)data;
	struct icc_helper_test_path *test_path = elem->test_path;

	mutex_lock(&test_path->mutex);

	*val = (u64)elem->value;

	mutex_unlock(&test_path->mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(icc_param, icc_param_get, icc_param_set, "%llu");

static int icc_update_set_common(void *data, u64 val, u8 update_type)
{
	struct icc_helper_test_path *test_path = (struct icc_helper_test_path *)data;
	struct icc_helper_test_element *avg_bw_elem;
	struct icc_helper_test_element *peak_bw_elem;
	struct icc_helper_test_element *latency_elem;
	struct icc_helper_test_element *ltv_elem;
	struct icc_helper_test_element *rt_bw_elem;
	int vc;
	int ret = 0;

	mutex_lock(&test_path->mutex);

	/* google_icc_set_read_bw_gmc */
	for (vc = 0; vc < num_vc; vc++) {
		avg_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_AVG_BW][vc];
		peak_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_PEAK_BW][vc];
		rt_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_RT_BW][vc];

		if (avg_bw_elem->is_update || peak_bw_elem->is_update || rt_bw_elem->is_update) {
			avg_bw_elem->is_update = false;
			peak_bw_elem->is_update = false;
			rt_bw_elem->is_update = false;

			ret = google_icc_set_read_bw_gmc(test_path->icc_path,
							 avg_bw_elem->value,
							 peak_bw_elem->value,
							 rt_bw_elem->value,
							 vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_read_bw_gmc() failed (%d)\n", ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_read_bw_gslc */
	for (vc = 0; vc < num_vc; vc++) {
		avg_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_AVG_BW_GSLC][vc];
		peak_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_PEAK_BW_GSLC][vc];
		rt_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_RT_BW_GSLC][vc];

		if (avg_bw_elem->is_update || peak_bw_elem->is_update || rt_bw_elem->is_update) {
			avg_bw_elem->is_update = false;
			peak_bw_elem->is_update = false;
			rt_bw_elem->is_update = false;

			ret = google_icc_set_read_bw_gslc(test_path->icc_path,
							  avg_bw_elem->value,
							  peak_bw_elem->value,
							  rt_bw_elem->value,
							  vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_read_bw_gslc() failed (%d)\n", ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_write_bw_gmc */
	for (vc = 0; vc < num_vc; vc++) {
		avg_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_AVG_BW][vc];
		peak_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_PEAK_BW][vc];
		rt_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_RT_BW][vc];

		if (avg_bw_elem->is_update || peak_bw_elem->is_update || rt_bw_elem->is_update) {
			avg_bw_elem->is_update = false;
			peak_bw_elem->is_update = false;
			rt_bw_elem->is_update = false;

			ret = google_icc_set_write_bw_gmc(test_path->icc_path,
							  avg_bw_elem->value,
							  peak_bw_elem->value,
							  rt_bw_elem->value,
							  vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_write_bw_gmc() failed (%d)\n", ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_write_bw_gslc */
	for (vc = 0; vc < num_vc; vc++) {
		avg_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_AVG_BW_GSLC][vc];
		peak_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_PEAK_BW_GSLC][vc];
		rt_bw_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_RT_BW_GSLC][vc];

		if (avg_bw_elem->is_update || peak_bw_elem->is_update || rt_bw_elem->is_update) {
			avg_bw_elem->is_update = false;
			peak_bw_elem->is_update = false;
			rt_bw_elem->is_update = false;

			ret = google_icc_set_write_bw_gslc(test_path->icc_path,
							   avg_bw_elem->value,
							   peak_bw_elem->value,
							   rt_bw_elem->value,
							   vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_write_bw_gslc() failed (%d)\n", ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_read_latency_gmc */
	for (vc = 0; vc < num_vc; vc++) {
		latency_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_LATENCY][vc];
		ltv_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_LTV][vc];

		if (latency_elem->is_update || ltv_elem->is_update) {
			latency_elem->is_update = false;
			ltv_elem->is_update = false;

			ret = google_icc_set_read_latency_gmc(test_path->icc_path,
							      latency_elem->value,
							      ltv_elem->value,
							      vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_read_latency_gmc() failed (%d)\n",
					ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_read_latency_gslc */
	for (vc = 0; vc < num_vc; vc++) {
		latency_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_LATENCY_GSLC][vc];
		ltv_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_READ_LTV_GSLC][vc];

		if (latency_elem->is_update || ltv_elem->is_update) {
			latency_elem->is_update = false;
			ltv_elem->is_update = false;

			ret = google_icc_set_read_latency_gslc(test_path->icc_path,
							       latency_elem->value,
							       ltv_elem->value,
							       vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_read_latency_gslc() failed (%d)\n",
					ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_write_latency_gmc */
	for (vc = 0; vc < num_vc; vc++) {
		latency_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_LATENCY][vc];
		ltv_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_LTV][vc];

		if (latency_elem->is_update || ltv_elem->is_update) {
			latency_elem->is_update = false;
			ltv_elem->is_update = false;

			ret = google_icc_set_write_latency_gmc(test_path->icc_path,
							       latency_elem->value,
							       ltv_elem->value,
							       vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_write_latency_gmc() failed (%d)\n",
					ret);
				goto unlock;
			}
		}
	}

	/* google_icc_set_write_latency_gslc */
	for (vc = 0; vc < num_vc; vc++) {
		latency_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_LATENCY_GSLC][vc];
		ltv_elem = &test_path->elem_arr[ICC_HELPER_API_TYPE_WRITE_LTV_GSLC][vc];

		if (latency_elem->is_update || ltv_elem->is_update) {
			latency_elem->is_update = false;
			ltv_elem->is_update = false;

			ret = google_icc_set_write_latency_gslc(test_path->icc_path,
								latency_elem->value,
								ltv_elem->value,
								vc);
			if (ret) {
				dev_err(test_path->dev,
					"google_icc_set_write_latency_gslc() failed (%d)\n",
					ret);
				goto unlock;
			}
		}
	}

	if (update_type == ICC_HELPER_UPDATE_TYPE_ASYNC)
		ret = google_icc_update_constraint_async(test_path->icc_path);
	else if (update_type == ICC_HELPER_UPDATE_TYPE_SYNC)
		ret = google_icc_update_constraint(test_path->icc_path);

	update_err_code[update_type] = abs(ret);

	if (ret) {
		dev_err(test_path->dev, "%s update failed (%d)\n",
			update_type == ICC_HELPER_UPDATE_TYPE_ASYNC ? "async" : "sync",
			ret);
		goto unlock;
	}

unlock:
	mutex_unlock(&test_path->mutex);
	return ret;
}

static int icc_update_get_common(void *data, u64 *val, u8 update_type)
{
	*val = (u64)update_err_code[update_type];
	return 0;
}

static int icc_update_set_sync(void *data, u64 val)
{
	return icc_update_set_common(data, val, ICC_HELPER_UPDATE_TYPE_SYNC);
}

static int icc_update_get_sync(void *data, u64 *val)
{
	return icc_update_get_common(data, val, ICC_HELPER_UPDATE_TYPE_SYNC);
}
DEFINE_DEBUGFS_ATTRIBUTE(icc_update_sync, icc_update_get_sync, icc_update_set_sync, "%llu");

static int icc_update_set_async(void *data, u64 val)
{
	return icc_update_set_common(data, val, ICC_HELPER_UPDATE_TYPE_ASYNC);
}

static int icc_update_get_async(void *data, u64 *val)
{
	return icc_update_get_common(data, val, ICC_HELPER_UPDATE_TYPE_ASYNC);
}
DEFINE_DEBUGFS_ATTRIBUTE(icc_update_async, icc_update_get_async, icc_update_set_async, "%llu");

static int __create_debugfs_path(struct device *dev, const char *path_name,
				 struct icc_helper_test_path *path)
{
	struct dentry *sub_dir;
	int i, vc;
	struct icc_helper_test_element *elem;
	char node_name[32];

	path->dev = dev;
	mutex_init(&path->mutex);

	sub_dir = debugfs_create_dir(path_name, base_dir);
	path->dir = sub_dir;

	/* init elem_arr */
	path->elem_arr = devm_kzalloc(dev,
				      sizeof(*path->elem_arr) * ICC_HELPER_API_TYPE_NUM,
				      GFP_KERNEL);
	if (!path->elem_arr)
		return -ENOMEM;

	for (i = 0; i < ICC_HELPER_API_TYPE_NUM; i++) {
		path->elem_arr[i] = devm_kzalloc(dev,
						 sizeof(*path->elem_arr[i]) * num_vc,
						 GFP_KERNEL);
		if (!path->elem_arr[i])
			return -ENOMEM;
	}

	/* get google_icc_path */
	path->icc_path = google_devm_of_icc_get(dev, path_name);
	if (IS_ERR_OR_NULL(path->icc_path)) {
		dev_err(dev, "google_devm_of_icc_get() failed: %s, ret = %ld\n",
			path_name, PTR_ERR(path->icc_path));
		return -EINVAL;
	}

	for (i = 0; i < ICC_HELPER_API_TYPE_NUM; i++) {
		for (vc = 0; vc < num_vc; vc++) {
			elem = &path->elem_arr[i][vc];

			elem->test_path = path;

			elem->value = param_default_val[i];

			snprintf(node_name, sizeof(node_name),
				 node_name_format[i], vc);
			debugfs_create_file(node_name, 0600, sub_dir, elem, &icc_param);
		}
	}

	debugfs_create_file("update_async", 0600, sub_dir, path, &icc_update_async);
	debugfs_create_file("update_sync", 0600, sub_dir, path, &icc_update_sync);

	return 0;
}

static int __create_debugfs(struct device *dev, const char **path_name_arr, int num_path)
{
	int idx;
	int ret = 0;

	base_dir = debugfs_create_dir("google_icc_helper_mock_client", NULL);

	test_path_arr = devm_kzalloc(dev, sizeof(*test_path_arr) * num_path, GFP_KERNEL);
	if (!test_path_arr) {
		ret = -ENOMEM;
		goto err;
	}

	for (idx = 0; idx < num_path; idx++) {
		ret = __create_debugfs_path(dev, path_name_arr[idx], &test_path_arr[idx]);
		if (ret)
			goto err;
	}

	return 0;
err:
	debugfs_remove(base_dir);
	return ret;
}

static int google_icc_helper_mock_client_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct device_node *np_irm_common_prop;
	const char **path_name;
	int num_path = 0;
	int ret = 0;

	np = dev->of_node;

	/* get num_vc */
	np_irm_common_prop = of_parse_phandle(np, "google,irm-commom-prop", 0);
	if (!np_irm_common_prop) {
		dev_err(dev, "Read google,irm-commom-prop failed.\n");
		ret = -EINVAL;
		goto out;
	}

	if (of_property_read_u32(np_irm_common_prop, "num_vc", &num_vc) < 0) {
		dev_err(dev, "Read num_vc failed.\n");
		ret = -EINVAL;
		goto out;
	}

	/* get supported icc_paths from interconnect-names*/
	num_path = of_property_count_strings(np, "interconnect-names");
	if (num_path <= 0) {
		dev_err(dev, "Read interconnect-names failed (%d).\n", num_path);
		ret = -EINVAL;
		goto out;
	}

	path_name = devm_kzalloc(dev,
				 sizeof(*path_name) * num_path,
				 GFP_KERNEL);
	if (!path_name) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_string_array(np, "interconnect-names", path_name, num_path);
	if (ret < 0) {
		dev_err(dev, "Read interconnect-names array failed (%d).\n", ret);
		ret = -EINVAL;
		goto out_free;
	}

	ret = __create_debugfs(dev, path_name, num_path);
	if (ret < 0)
		goto out_free;

	dev_info(dev, "device probed successfully\n");

out_free:
	devm_kfree(dev, path_name);
out:
	return ret;
}

static void
google_icc_helper_mock_client_platform_remove(struct platform_device *pdev)
{
	debugfs_remove(base_dir);
}

static const struct of_device_id google_icc_helper_mock_client_of_match_table[] = {
	{ .compatible = "google,icc-helper-mock-client" },
	{}
};
MODULE_DEVICE_TABLE(of, google_icc_helper_mock_client_of_match_table);

static struct platform_driver google_icc_helper_mock_client_platform_driver = {
	.probe = google_icc_helper_mock_client_platform_probe,
	.remove = google_icc_helper_mock_client_platform_remove,
	.driver = {
		.name = "google-icc-helper-mock-client",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_icc_helper_mock_client_of_match_table),
	},
};

module_platform_driver(google_icc_helper_mock_client_platform_driver);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google icc helper mock client driver");
MODULE_LICENSE("GPL");
