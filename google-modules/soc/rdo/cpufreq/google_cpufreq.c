// SPDX-License-Identifier: GPL-2.0-only
#include <dvfs-helper/google_dvfs_helper.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/google_vote_manager.h>
#include <soc/google/google-cdd.h>

#define CREATE_TRACE_POINTS
#include "cpufreq_trace.h"

#define CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(X) (0x8 * (X))

struct em_data {
	unsigned long freq;
	unsigned long power;
};

struct cpufreq_em_table {
	size_t em_table_size;
	struct em_data *em_data;
};

struct cpufreq_cpu_data {
	cpumask_var_t cpus;

	struct device *cpu_dev;
	struct cpufreq_frequency_table *freq_table;

	struct cpufreq_em_table cpufreq_em_table;
	bool have_static_opps;

	unsigned int old_freq;
	unsigned int freq;

	/* Prevent actual DVFS changes until extra votes added in probe are ready */
	bool dvfs_enabled;
};

struct cpufreq_data {
	struct device *dev;

	void __iomem *base_addr;
	struct regmap *regmap;

	struct cpufreq_cpu_data **cpu_data_arr;
	size_t cpu_data_arr_size;
	u8 *clk_domain;
};

static const struct regmap_config google_cpufreq_regmap_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32
};

/* TODO(b/229183416) figure out how to get real ASV bin value. */
#define DEFAULT_ASV_BIN 1
static int google_get_asv_bin(void)
{
	return DEFAULT_ASV_BIN;
}

static inline unsigned long google_cpufreq_find_freq(struct cpufreq_cpu_data *cpufreq_cpu_data,
						     unsigned int pf_state)
{
	struct cpufreq_frequency_table *pos;

	cpufreq_for_each_entry(pos, cpufreq_cpu_data->freq_table)
		if (pos->driver_data == pf_state)
			return pos->frequency;

	dev_warn(cpufreq_cpu_data->cpu_dev, "No matching frequency of pf_state %u\n", pf_state);
	return -ENOENT;
}

static int google_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct cpufreq_data *cpufreq_data = cpufreq_get_driver_data();
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	unsigned int cpu_id = policy->cpu;
	unsigned int pf_state;

	policy->fast_switch_possible = true;
	policy->dvfs_possible_from_any_cpu = true;
	cpufreq_cpu_data = cpufreq_data->cpu_data_arr[cpu_id];
	policy->driver_data = cpufreq_cpu_data;
	cpumask_copy(policy->cpus, cpufreq_cpu_data->cpus);
	policy->freq_table = cpufreq_cpu_data->freq_table;

	regmap_read(cpufreq_data->regmap,
		    CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(cpufreq_data->clk_domain[cpu_id]),
		    &pf_state);
	cpufreq_cpu_data->freq = google_cpufreq_find_freq(cpufreq_cpu_data, pf_state);

	dev_info(cpufreq_cpu_data->cpu_dev,
		 "Init cpu (%u) freq to (%u), pf_state(%u)\n", cpu_id,
		 cpufreq_cpu_data->freq, pf_state);

	return 0;
}

static void google_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	/*
	 * policy->driver_data free in google_cpufreq_data_exit
	 * policy->freq_table free in dev_pm_opp_free_cpufreq_table
	 */
	policy->fast_switch_possible = false;
}

static unsigned int google_cpufreq_get(unsigned int cpu)
{
	struct cpufreq_data *cpufreq_data = cpufreq_get_driver_data();
	unsigned long freq_from_csr;
	unsigned int pf_state;

	regmap_read(cpufreq_data->regmap,
		    CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(cpufreq_data->clk_domain[cpu]),
		    &pf_state);
	freq_from_csr = google_cpufreq_find_freq(cpufreq_data->cpu_data_arr[cpu], pf_state);
	/* Check if the freq in cache and freq in CSR are same. */
	if (cpufreq_data->cpu_data_arr[cpu]->freq != freq_from_csr)
		dev_warn(cpufreq_data->cpu_data_arr[cpu]->cpu_dev,
			 "The freq of cpu (%u) in cache: %u, freq from CSR: %lu\n", cpu,
			 cpufreq_data->cpu_data_arr[cpu]->freq, freq_from_csr);

	return freq_from_csr;
}

static int google_cpufreq_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct cpufreq_data *cpufreq_data = cpufreq_get_driver_data();
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;
	unsigned long freq = policy->freq_table[index].frequency;
	unsigned int pf_state = policy->freq_table[index].driver_data;

	if (!cpufreq_cpu_data->dvfs_enabled)
		return -EPERM;

	dev_dbg(cpufreq_cpu_data->cpu_dev, "Set cpu (%u) freq to (%lu)\n",
		policy->cpu, freq);
	cpufreq_cpu_data->old_freq = cpufreq_cpu_data->freq;
	cpufreq_cpu_data->freq = freq;
	regmap_write(cpufreq_data->regmap,
		     CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(cpufreq_data->clk_domain[policy->cpu]),
		     pf_state);

	return 0;
}

static unsigned int google_cpufreq_fast_switch(struct cpufreq_policy *policy,
					       unsigned int target_freq)
{
	struct cpufreq_data *cpufreq_data = cpufreq_get_driver_data();
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;
	unsigned int index, pf_state;

	if (!cpufreq_cpu_data->dvfs_enabled)
		return 0;

	dev_dbg(cpufreq_cpu_data->cpu_dev, "Set cpu (%u) freq to (%u)\n",
		policy->cpu, target_freq);
	cpufreq_cpu_data->old_freq = cpufreq_cpu_data->freq;
	cpufreq_cpu_data->freq = target_freq;
	index = cpufreq_table_find_index_ac(policy, target_freq, 0);
	pf_state = policy->freq_table[index].driver_data;
	regmap_write(cpufreq_data->regmap,
		     CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(cpufreq_data->clk_domain[policy->cpu]),
		     pf_state);
	trace_cpufreq_fastswitch(policy->cpu, target_freq);

	return target_freq;
}

static int __maybe_unused
google_cpufreq_get_cpu_power(struct device *cpu_dev, unsigned long *power,
			     unsigned long *kHz)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	struct cpufreq_em_table *cpufreq_em_table;
	struct cpufreq_policy *policy;
	int i;
	size_t em_table_size;

	policy = cpufreq_cpu_get_raw(cpu_dev->id);
	if (!policy)
		return 0;

	cpufreq_cpu_data = policy->driver_data;
	cpufreq_em_table = &cpufreq_cpu_data->cpufreq_em_table;
	em_table_size = cpufreq_em_table->em_table_size;

	for (i = 0; i < em_table_size; i++) {
		if (cpufreq_em_table->em_data[i].freq < *kHz)
			break;
	}
	if (i == 0) {
		dev_warn(cpu_dev, "Input freq (%lu) is too large. Use the largest power but will be inaccurate.",
			 *kHz);
	} else {
		i--;
	}

	*kHz = cpufreq_em_table->em_data[i].freq;
	*power = cpufreq_em_table->em_data[i].power;
	dev_info(cpu_dev, "Em item created: Freq (%lu) Power (%lu)\n",
		 *kHz, *power);

	return 0;
}

static void google_cpufreq_register_em(struct cpufreq_policy *policy)
{
	struct em_data_callback em_cb = EM_DATA_CB(google_cpufreq_get_cpu_power);
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;
	size_t em_table_size = cpufreq_cpu_data->cpufreq_em_table.em_table_size;

	em_dev_register_perf_domain(get_cpu_device(policy->cpu), em_table_size,
				    &em_cb, policy->cpus, true);
}

static struct freq_attr *google_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver google_cpufreq_driver = {
	.name = "google-cpufreq",
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK | CPUFREQ_IS_COOLING_DEV |
		 CPUFREQ_HAVE_GOVERNOR_PER_POLICY,
	.get = google_cpufreq_get,
	.target_index = google_cpufreq_set_target,
	.fast_switch = google_cpufreq_fast_switch,
	.register_em = google_cpufreq_register_em,
	.verify = cpufreq_generic_frequency_table_verify,
	.init = google_cpufreq_cpu_init,
	.exit = google_cpufreq_cpu_exit,
	.attr = google_cpufreq_attr,
};

/* TODO (b/229330089) support different type of core (Large/Mid/Small) */
static int init_cpufreq_em_table(struct device *dev,
				 struct cpufreq_cpu_data *cpufreq_cpu_data)
{
	struct device_node *node;
	struct device_node *em_data_node;
	struct cpufreq_em_table *cpufreq_em_table = &cpufreq_cpu_data->cpufreq_em_table;
	int phandle_cnt;
	int asv_bin_value;
	int freq_data_size;
	int power_data_size;
	int ret;
	int i;
	int cluster_num;
	u64 *freq_data;
	u64 *power_data;

	node = dev->of_node;

	phandle_cnt = of_count_phandle_with_args(node, "em-data", NULL);
	if (phandle_cnt < 0) {
		dev_err(dev, "Read em-data failed, ret %d\n", phandle_cnt);
		return phandle_cnt;
	}

	asv_bin_value = google_get_asv_bin();
	if (asv_bin_value < 0) {
		dev_warn(dev, "Get asv bin value failed, ret %d\n", asv_bin_value);
		dev_warn(dev, "Set asv bin to default value 0\n");
		asv_bin_value = 0;
	}

	if (asv_bin_value >= phandle_cnt) {
		dev_warn(dev, "Asv bin value (%d) is too large. The table size is %d\n",
			 asv_bin_value, phandle_cnt);
		dev_warn(dev, "Use default asv bin value (0) instead\n");
		asv_bin_value = 0;
	}

	/* TODO(b/389783082): remove 'case:6' when new cpufreq implementation is available */
	switch (cpumask_first(cpufreq_cpu_data->cpus)) {
	case 0:
		cluster_num = 0;
		google_cdd_freq(CDD_FREQ_DOMAIN_APC_LIT, &cpufreq_cpu_data->old_freq,
					    &cpufreq_cpu_data->freq);
		break;
	case 2:
		cluster_num = 1;
		google_cdd_freq(CDD_FREQ_DOMAIN_APC_MID0, &cpufreq_cpu_data->old_freq,
					    &cpufreq_cpu_data->freq);
		break;
	case 5:
		cluster_num = 2;
		google_cdd_freq(CDD_FREQ_DOMAIN_APC_MID1, &cpufreq_cpu_data->old_freq,
					    &cpufreq_cpu_data->freq);
		break;
	case 6:
		cluster_num = 2;
		google_cdd_freq(CDD_FREQ_DOMAIN_APC_BIG, &cpufreq_cpu_data->old_freq,
					    &cpufreq_cpu_data->freq);
		break;
	case 7:
		cluster_num = 3;
		google_cdd_freq(CDD_FREQ_DOMAIN_APC_BIG, &cpufreq_cpu_data->old_freq,
					    &cpufreq_cpu_data->freq);
		break;
	default:
		pr_err("em table init failed. Wrong CPU cluster\n");
		return -EINVAL;
	}

	em_data_node = of_parse_phandle(node, "em-data", cluster_num);
	if (!em_data_node) {
		dev_err(dev, "Can't find em-data\n");
		return -EINVAL;
	}

	freq_data_size = of_property_count_u64_elems(em_data_node, "frequency");
	if (freq_data_size < 0) {
		dev_err(dev, "Get frequency property failed, ret %d.\n", freq_data_size);
		ret = freq_data_size;
		goto put_em_data_node;
	}

	power_data_size = of_property_count_u64_elems(em_data_node, "power");
	if (power_data_size < 0) {
		dev_err(dev, "Get power property failed, ret %d.\n", power_data_size);
		ret = power_data_size;
		goto put_em_data_node;
	}

	if (freq_data_size != power_data_size) {
		dev_err(dev, "Em data wrong: size of freq table (%d) is different with size of power data (%d).\n",
			freq_data_size, power_data_size);
		ret = -EINVAL;
		goto put_em_data_node;
	}

	freq_data = devm_kcalloc(dev, freq_data_size, sizeof(u64), GFP_KERNEL);
	if (!freq_data) {
		ret = -ENOMEM;
		goto put_em_data_node;
	}

	power_data = devm_kcalloc(dev, power_data_size, sizeof(u64), GFP_KERNEL);
	if (!power_data) {
		ret = -ENOMEM;
		goto free_freq_data;
	}

	ret = of_property_read_u64_array(em_data_node, "frequency", freq_data,
					 freq_data_size);
	if (ret < 0) {
		dev_err(dev, "Em data wrong: failed to read frequency data, ret (%d).\n",
			ret);
		goto free_power_data;
	}

	ret = of_property_read_u64_array(em_data_node, "power", power_data,
					 power_data_size);
	if (ret < 0) {
		dev_err(dev, "Em data wrong: failed to read power data, ret (%d).\n",
			ret);
		goto free_power_data;
	}

	cpufreq_em_table->em_table_size = freq_data_size;
	cpufreq_em_table->em_data = devm_kcalloc(dev, freq_data_size,
						 sizeof(*cpufreq_em_table->em_data),
						 GFP_KERNEL);
	if (!cpufreq_em_table->em_data) {
		ret = -ENOMEM;
		goto free_power_data;
	}
	for (i = 0; i < freq_data_size; ++i) {
		cpufreq_em_table->em_data[i].freq = freq_data[i];
		cpufreq_em_table->em_data[i].power = power_data[i];
	}

free_power_data:
	devm_kfree(dev, power_data);
free_freq_data:
	devm_kfree(dev, freq_data);
put_em_data_node:
	of_node_put(em_data_node);

	return ret;
}

/* Modified from dev_pm_opp_init_cpufreq_table, but assign opp-level to driver data */
static int google_init_cpufreq_table(struct cpufreq_cpu_data *cpufreq_cpu_data)
{
	struct dev_pm_opp *opp;
	struct device *dev = cpufreq_cpu_data->cpu_dev;
	int i, max_opps, ret = 0;
	unsigned long rate;

	max_opps = dev_pm_opp_get_opp_count(dev);
	if (max_opps <= 0)
		return max_opps ? max_opps : -ENODATA;

	cpufreq_cpu_data->freq_table = devm_kcalloc(dev, (max_opps + 1),
						    sizeof(*cpufreq_cpu_data->freq_table),
						    GFP_KERNEL);
	if (!cpufreq_cpu_data->freq_table)
		return -ENOMEM;

	for (i = 0, rate = 0; i < max_opps; i++, rate++) {
		/* find next rate */
		opp = dev_pm_opp_find_freq_ceil(dev, &rate);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto out;
		}
		cpufreq_cpu_data->freq_table[i].driver_data = dev_pm_opp_get_level(opp);
		cpufreq_cpu_data->freq_table[i].frequency = rate / 1000;

		/* Is Boost/turbo opp ? */
		if (dev_pm_opp_is_turbo(opp))
			cpufreq_cpu_data->freq_table[i].flags = CPUFREQ_BOOST_FREQ;

		dev_pm_opp_put(opp);
	}

	cpufreq_cpu_data->freq_table[i].driver_data = i;
	cpufreq_cpu_data->freq_table[i].frequency = CPUFREQ_TABLE_END;

out:
	return ret;
}

static int cpufreq_data_init(struct cpufreq_data *cpufreq_data, int cpu)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	struct device *cpu_dev;
	struct device *dev = cpufreq_data->dev;
	unsigned int i;
	int ret;

	if (cpufreq_data->cpu_data_arr[cpu]) {
		dev_dbg(cpufreq_data->dev, "Data of cpu (%d) is initialized.\n",
			cpu);
		return 0;
	}

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return -EPROBE_DEFER;

	cpufreq_cpu_data = devm_kzalloc(dev, sizeof(*cpufreq_cpu_data), GFP_KERNEL);
	if (!cpufreq_cpu_data)
		return -ENOMEM;

	if (!alloc_cpumask_var(&cpufreq_cpu_data->cpus, GFP_KERNEL))
		return -ENOMEM;

	cpumask_set_cpu(cpu, cpufreq_cpu_data->cpus);
	cpufreq_cpu_data->cpu_dev = cpu_dev;

	/* Get all the cpus which shared the same opp. */
	ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, cpufreq_cpu_data->cpus);
	if (ret)
		goto err;

	/*
	 * Initialize OPP tables for all priv->cpus using DVFS helper. They will be
	 * shared by all CPUs which have marked their CPUs shared with OPP bindings.
	 *
	 * OPPs might be populated at runtime, don't fail for error here unless
	 * it is -EPROBE_DEFER.
	 */

	ret = dvfs_helper_add_opps_to_device(cpu_dev, cpu_dev->of_node);
	if (!ret)
		cpufreq_cpu_data->have_static_opps = true;
	else if (ret == -EPROBE_DEFER)
		goto err;

	/* Check if the opp count is valid. */
	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_err(cpu_dev, "OPP table can't be empty\n");
		ret = -ENODEV;
		goto err;
	}

	ret = google_init_cpufreq_table(cpufreq_cpu_data);
	if (ret) {
		dev_err(cpu_dev, "Failed to init cpufreq table: %d\n", ret);
		goto err;
	}

	init_cpufreq_em_table(dev, cpufreq_cpu_data);

	for_each_cpu(i, cpufreq_cpu_data->cpus) {
		cpufreq_data->cpu_data_arr[i] = cpufreq_cpu_data;
	}

	return 0;

err:
	if (cpufreq_cpu_data->have_static_opps)
		dev_pm_opp_of_cpumask_remove_table(cpufreq_cpu_data->cpus);

	free_cpumask_var(cpufreq_cpu_data->cpus);
	return ret;
}

static void cpufreq_data_exit(struct cpufreq_data *cpufreq_data, int cpu)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	int i;

	cpufreq_cpu_data = cpufreq_data->cpu_data_arr[cpu];
	if (!cpufreq_cpu_data)
		return;

	if (!cpufreq_cpu_data->freq_table)
		return;
	cpufreq_cpu_data->freq_table = NULL;

	if (cpufreq_cpu_data->have_static_opps)
		dev_pm_opp_of_cpumask_remove_table(cpufreq_cpu_data->cpus);

	for_each_cpu(i, cpufreq_cpu_data->cpus) {
		cpufreq_data->cpu_data_arr[i] = NULL;
	}

	free_cpumask_var(cpufreq_cpu_data->cpus);
}

static void google_cpufreq_data_exit_all(struct cpufreq_data *cpufreq_data)
{
	unsigned int i;

	for (i = 0; i < nr_cpu_ids; ++i)
		cpufreq_data_exit(cpufreq_data, i);
}

static void unregister_vote_track(void)
{
	struct cpufreq_policy *policy;

	for (int i = 0; i < nr_cpu_ids; ++i) {
		policy = cpufreq_cpu_get(i);
		if (!policy)
			continue;

		if (policy->cpu != i) {
			cpufreq_cpu_put(policy);
			continue;
		}
		vote_manager_remove_cpufreq(policy);
		google_unregister_cpufreq(policy);
		cpufreq_cpu_put(policy);
	}

}

static int google_cpufreq_platform_probe(struct platform_device *pdev)
{
	struct cpufreq_policy *policy;
	struct device *dev = &pdev->dev;
	struct cpufreq_data *cpufreq_data;
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	struct device_node *node = dev->of_node;
	unsigned int i;
	int ret;

	cpufreq_data = devm_kzalloc(dev, sizeof(*cpufreq_data), GFP_KERNEL);
	if (!cpufreq_data)
		return -ENOMEM;

	cpufreq_data->dev = dev;
	cpufreq_data->cpu_data_arr_size = nr_cpu_ids;
	cpufreq_data->cpu_data_arr = devm_kcalloc(dev, nr_cpu_ids,
						  sizeof(*cpufreq_data->cpu_data_arr),
						  GFP_KERNEL);
	if (!cpufreq_data->cpu_data_arr)
		return -ENOMEM;

	cpufreq_data->clk_domain = devm_kcalloc(dev, nr_cpu_ids, sizeof(u8), GFP_KERNEL);
	ret = of_property_read_u8_array(node, "cpufreq-clk-domain",
					(u8 *)cpufreq_data->clk_domain, nr_cpu_ids);
	if (ret < 0) {
		dev_err(dev, "Read clk-domain failed. ret %d\n", ret);
		return ret;
	}

	cpufreq_data->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cpufreq_data->base_addr))
		return PTR_ERR(cpufreq_data->base_addr);
	cpufreq_data->regmap = devm_regmap_init_mmio(dev, cpufreq_data->base_addr,
						     &google_cpufreq_regmap_cfg);
	if (IS_ERR(cpufreq_data->regmap))
		return PTR_ERR(cpufreq_data->regmap);
	platform_set_drvdata(pdev, cpufreq_data);

	for (i = 0; i < nr_cpu_ids; ++i) {
		ret = cpufreq_data_init(cpufreq_data, i);
		if (ret) {
			dev_err(dev, "Cpufreq_data_init for cpu (%d) failed. Ret (%d)\n",
				i, ret);
			goto err;
		}
	}

	google_cpufreq_driver.driver_data = cpufreq_data;
	ret = cpufreq_register_driver(&google_cpufreq_driver);
	if (ret < 0) {
		dev_err(dev, "Cpufreq_register_driver failed. Ret (%d)\n", ret);
		google_cpufreq_driver.driver_data = NULL;
		goto err;
	}

	for (i = 0; i < nr_cpu_ids; ++i) {
		policy = cpufreq_cpu_get(i);
		if (!policy) {
			dev_info(dev,
				 "cpufreq_cpu_get() for cpu (%d) doesn't return policy\n",
				 i);
			continue;
		}

		if (policy->cpu != i) {
			cpufreq_cpu_put(policy);
			continue;
		}
		ret = google_register_cpufreq(policy);
		if (ret < 0) {
			cpufreq_cpu_put(policy);
			goto remove_vote_track;
		}

		ret = vote_manager_init_cpufreq(policy);
		if (ret < 0) {
			cpufreq_cpu_put(policy);
			goto remove_vote_track;
		}

		cpufreq_cpu_data = cpufreq_data->cpu_data_arr[i];

		cpufreq_cpu_put(policy);
		cpufreq_update_limits(i);
		cpufreq_cpu_data->dvfs_enabled = true;
	}

	return 0;

remove_vote_track:
	unregister_vote_track();
	cpufreq_unregister_driver(&google_cpufreq_driver);
err:
	google_cpufreq_data_exit_all(cpufreq_data);
	return ret;
}

static void google_cpufreq_platform_remove(struct platform_device *pdev)
{
	struct cpufreq_data *cpufreq_data = platform_get_drvdata(pdev);

	unregister_vote_track();
	cpufreq_unregister_driver(&google_cpufreq_driver);
	google_cpufreq_driver.driver_data = NULL;

	google_cpufreq_data_exit_all(cpufreq_data);
}

static const struct of_device_id google_cpufreq_of_match_table[] = {
	{ .compatible = "google,cpufreq" },
	{},
};
MODULE_DEVICE_TABLE(of, google_cpufreq_of_match_table);

static struct platform_driver google_cpufreq_platform_driver = {
	.probe = google_cpufreq_platform_probe,
	.remove = google_cpufreq_platform_remove,
	.driver = {
		.name = "google-cpufreq",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_cpufreq_of_match_table),
	},
};
module_platform_driver(google_cpufreq_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google cpu frequency driver");
MODULE_LICENSE("GPL");
