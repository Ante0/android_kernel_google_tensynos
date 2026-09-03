// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 */

#include <dvfs-frontend/dvfs_target_frontend.h>
#include <dvfs-helper/google_dvfs_helper.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/devfreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/seq_file.h>
#include <linux/units.h>
#include <perf/core/apc_irm.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/google_vote_manager.h>
#include <perf/core/perf_domain.h>
#include <perf/mbfs.h>
#include <soc/google/google-cdd.h>
#include <trace/events/power.h>

#define CREATE_TRACE_POINTS
#include "cpufreq_trace.h"

#define MBFS_ERR_TO_ERR_PTR(mbfs_ret) ERR_PTR(mbfs_error2linux(mbfs_ret))
#define DOMAIN_NAME_LEN (sizeof(union val64))
#define GMC_MAX_PF_LEVELS 16

static bool devfreqs_are_ready;
static struct dentry *debugfs_dir;
static struct dentry *gmc_memss_debug_dir;

static const char *dev_name_suffix[NUM_GS_DEVFREQ_TYPES] = {
	[GS_TARGET_DEVFREQ] = "target",
	[GS_CLAMP_DEVFREQ] = "clamp",
	[GS_RECOMMENDED_DEVFREQ] = NULL,
};

/* Forward declarations */
struct perf_domain;


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

	const char *dvfs_name;
	struct perf_domain *domain_data;

};

struct gs_cpufreq_data {
	struct device *dev;
	struct cpufreq_cpu_data **cpu_data_arr;
	size_t cpu_data_arr_size;

};

struct gmc_memss_irm_map {
	struct irm_client_t *irm_client;
	char gmc_domain_name[DOMAIN_NAME_LEN + 1];
	char memss_domain_name[DOMAIN_NAME_LEN + 1];
	struct device *gmc_dev;
	struct device *memss_dev;
	unsigned int gmc_to_memss_pf_map[GMC_MAX_PF_LEVELS];
	u64 *raw_gmc_memss_table;
	int num_table_entries;
	struct irm_subclient_t subclient;
};

struct gs_devfreq_data {
	struct device dev;
	struct devfreq *devfreq;
	struct devfreq_dev_profile devfreq_profile;

	struct mutex dvfs_update_lock;
	u64 cur_freq_Hz;
	struct notifier_block min_freq_nb;
	struct notifier_block max_freq_nb;
	struct perf_domain *domain_data;

	enum gs_devfreq_type type;

	/* Prevent actual DVFS changes until extra votes added in probe are ready */
	bool dvfs_enabled;

	struct list_head list;

	bool fast_devfreq_vote_enabled;
	struct fast_devfreq_vote_data fvote_data;

	// fast vote from PM QoS
	struct fast_devfreq_vote fdf_pm_qos_vote;
};


struct perf_domain {
	char domain_folder_name[MAX_MBFS_NAME_LENGTH + 1];
	char dvfs_helper_name[DOMAIN_NAME_LEN + 1];
	int domain_id;

	/* if target cpufreq is needed */
	bool cf_target_needed;

	/* if clamping devfreq is needed */
	bool df_clamp_needed;

	/* if target devfreq is needed */
	bool df_target_needed;

	/*
	 * Handle of a specific perf domain.
	 * Exposed to google_vote_manager for softmin/max vote registration
	 */
	union mbfs_client_handle perf_domain_handle;

	/*
	 * Handle used for clamping devfreq min usable opp voting
	 * i.e. maximum usable frequency in kernel point of view
	 */
	union mbfs_client_handle min_opp_handle;

	/*
	 * Handle used for clamping devfreq max usable opp voting
	 * i.e. minimum usable frequency in kernel point of view
	 */
	union mbfs_client_handle max_opp_handle;
	union mbfs_client_handle cur_opp_handle;

	struct list_head list;
};

static struct class *perf_domain_class;

static LIST_HEAD(cpu_perf_domain_list);

static LIST_HEAD(gs_devfreq_data_list);
static DEFINE_MUTEX(devfreq_data_lock);

static struct gmc_memss_irm_map gmc_memss_map;

static s64 gs_devfreq_get_freq_from_level(struct gs_devfreq_data *df_data, unsigned int pf_level);

static int gmc_memss_map_show(struct seq_file *m, void *v)
{
	struct gmc_memss_irm_map *map = &gmc_memss_map;
	int i;
	s64 gmc_freq, memss_freq;

	if (!map->gmc_dev || !map->memss_dev) {
		seq_puts(m, "GMC or MEMSS device not linked\n");
		return 0;
	}

	seq_puts(m, "GMC PF | GMC Freq (KHz) | MEMSS PF | MEMSS Freq (KHz)\n");
	seq_puts(m, "-------|----------------|----------|------------------\n");

	for (i = 0; i < GMC_MAX_PF_LEVELS; i++) {
		unsigned int memss_pf = map->gmc_to_memss_pf_map[i];

		gmc_freq = gs_devfreq_get_freq_from_level(
			container_of(map->gmc_dev, struct gs_devfreq_data, dev), i);
		if (gmc_freq < 0)
			continue;

		memss_freq = gs_devfreq_get_freq_from_level(
			container_of(map->memss_dev, struct gs_devfreq_data, dev), memss_pf);
		if (memss_freq < 0)
			continue;

		seq_printf(m, "%6d | %14lld | %8d | %16lld\n",
				 i, gmc_freq / 1000, memss_pf, memss_freq / 1000);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(gmc_memss_map);

static inline int google_cpufreq_find_freq(struct cpufreq_cpu_data *cpufreq_cpu_data,
					   unsigned int pf_state, unsigned int *freq)
{
	struct cpufreq_frequency_table *pos;

	cpufreq_for_each_entry(pos, cpufreq_cpu_data->freq_table) {
		if (pos->driver_data == pf_state) {
			*freq = pos->frequency;
			return 0;
		}
	}

	dev_warn(cpufreq_cpu_data->cpu_dev, "No matching frequency of pf_state %u\n", pf_state);
	return -ENOENT;
}

static int google_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct perf_domain *domain;
	struct gs_cpufreq_data *cpufreq_data = cpufreq_get_driver_data();
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	unsigned int cpu_id = policy->cpu;
	unsigned int pf_state;
	unsigned int freq;
	int ret;

	policy->fast_switch_possible = true;
	policy->dvfs_possible_from_any_cpu = true;
	cpufreq_cpu_data = cpufreq_data->cpu_data_arr[cpu_id];
	policy->driver_data = cpufreq_cpu_data;
	cpumask_copy(policy->cpus, cpufreq_cpu_data->cpus);
	policy->freq_table = cpufreq_cpu_data->freq_table;

	domain = cpufreq_cpu_data->domain_data;
	pf_state = dvfs_fe_get_pf_level(domain->domain_id);

	ret = google_cpufreq_find_freq(cpufreq_cpu_data, pf_state, &freq);
	if (ret)
		return ret;
	cpufreq_cpu_data->freq = freq;

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
	struct gs_cpufreq_data *cpufreq_data = cpufreq_get_driver_data();
	unsigned int freq_from_csr;
	unsigned int pf_state;
	struct cpufreq_cpu_data *cpufreq_cpu_data = cpufreq_data->cpu_data_arr[cpu];
	struct perf_domain *domain = cpufreq_cpu_data->domain_data;
	int ret;

	pf_state = dvfs_fe_get_pf_level(domain->domain_id);

	ret = google_cpufreq_find_freq(cpufreq_cpu_data, pf_state, &freq_from_csr);
	if (ret)
		/* "Return 0 on error" is appropriate for the get func of cpufreq_driver. */
		return 0;

	/* Check if the freq in cache and freq in CSR are same. */
	if (cpufreq_cpu_data->freq != freq_from_csr)
		dev_warn(cpufreq_data->cpu_data_arr[cpu]->cpu_dev,
			 "The freq of cpu (%u) in cache: %u, freq from CSR: %u\n", cpu,
			 cpufreq_cpu_data->freq, freq_from_csr);

	return freq_from_csr;
}

static int google_cpufreq_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;

	unsigned long freq = policy->freq_table[index].frequency;
	unsigned int pf_state = policy->freq_table[index].driver_data;
	struct perf_domain *domain = cpufreq_cpu_data->domain_data;

	if (!cpufreq_cpu_data->dvfs_enabled)
		return -EPERM;

	dev_dbg(cpufreq_cpu_data->cpu_dev, "Set cpu (%u) freq to (%lu)\n",
		policy->cpu, freq);
	cpufreq_cpu_data->old_freq = cpufreq_cpu_data->freq;
	cpufreq_cpu_data->freq = freq;

	dvfs_fe_set_pf_level(domain->domain_id, pf_state);

	return 0;
}

static unsigned int google_cpufreq_fast_switch(struct cpufreq_policy *policy,
					       unsigned int target_freq)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;
	struct perf_domain *domain = cpufreq_cpu_data->domain_data;
	unsigned int index, pf_state;

	if (!cpufreq_cpu_data->dvfs_enabled)
		return 0;

	dev_dbg(cpufreq_cpu_data->cpu_dev, "Set cpu (%u) freq to (%u)\n",
		policy->cpu, target_freq);
	cpufreq_cpu_data->old_freq = cpufreq_cpu_data->freq;
	cpufreq_cpu_data->freq = target_freq;
	index = cpufreq_table_find_index_ac(policy, target_freq, 0);
	pf_state = policy->freq_table[index].driver_data;

	dvfs_fe_set_pf_level(domain->domain_id, pf_state);
	trace_cpufreq_fastswitch(policy->cpu, target_freq);

	return target_freq;
}

static int __maybe_unused
em_get_cpu_power(struct device *cpu_dev, unsigned long *power,
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
	struct em_data_callback em_cb = EM_DATA_CB(em_get_cpu_power);
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;
	size_t em_table_size = cpufreq_cpu_data->cpufreq_em_table.em_table_size;

	em_dev_register_perf_domain(get_cpu_device(policy->cpu), em_table_size,
				    &em_cb, policy->cpus, true);
}

static ssize_t show_fw_freq(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data = policy->driver_data;
	struct perf_domain *domain = cpufreq_cpu_data->domain_data;
	enum mbfs_error_code mbfs_ret;
	union val64 content;
	unsigned int freq_khz;
	int ret;

	if (is_mbfs_handle_invalid(domain->cur_opp_handle))
		return -ENODEV;

	mbfs_ret = mbfs_read_file(domain->cur_opp_handle, &content);
	if (mbfs_ret)
		return mbfs_error2linux(mbfs_ret);

	ret = google_cpufreq_find_freq(cpufreq_cpu_data, content.number, &freq_khz);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", freq_khz);
}
cpufreq_freq_attr_ro(fw_freq);

static struct freq_attr *google_cpufreq_attrs[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&fw_freq,
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
	.attr = google_cpufreq_attrs,
};

/* TODO (b/229330089) support different type of core (Large/Mid/Small) */
static int init_cpufreq_em_table(struct device *dev,
				 struct cpufreq_cpu_data *cpufreq_cpu_data)
{
	int freq_data_size;
	int i, j;
	struct cpufreq_em_table *cpufreq_em_table = &cpufreq_cpu_data->cpufreq_em_table;
	struct perf_domain *domain = cpufreq_cpu_data->domain_data;
	struct device *cpu_dev = cpufreq_cpu_data->cpu_dev;
	int ret = 0;

	google_cdd_freq_by_name(domain->domain_folder_name, &cpufreq_cpu_data->old_freq,
				&cpufreq_cpu_data->freq);

	freq_data_size = dev_pm_opp_get_opp_count(cpu_dev);
	if (freq_data_size <= 0) {
		dev_err(cpu_dev, "OPP table can't be empty\n");
		ret = -ENODEV;
		goto out;
	}
	cpufreq_em_table->em_table_size = freq_data_size;
	cpufreq_em_table->em_data = devm_kcalloc(dev, freq_data_size,
						 sizeof(*cpufreq_em_table->em_data),
						 GFP_KERNEL);
	if (!cpufreq_em_table->em_data) {
		ret = -ENOMEM;
		goto out;
	}

	/* Reverse freq_table as em framework requires table in increasing order */
	j = freq_data_size - 1;
	for (i = 0; i < freq_data_size; ++i) {
		cpufreq_em_table->em_data[i].freq =
				cpufreq_cpu_data->freq_table[j - i].frequency;
		/**
		 * Temporarily set the Power equal to freq to ensure a simple em registration.
		 * this 'artificial' power data is later overridden by the pixel-em driver.
		 */
		cpufreq_em_table->em_data[i].power =
				cpufreq_cpu_data->freq_table[j - i].frequency;
	}

out:
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

static int cpufreq_cpu_data_init(struct gs_cpufreq_data *cpufreq_data, int cpu)
{
	struct cpufreq_cpu_data *cpufreq_cpu_data;
	struct device *cpu_dev;
	struct device *dev = cpufreq_data->dev;
	struct perf_domain *domain, *tmp;
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

	ret = of_property_read_string(cpu_dev->of_node, "dvfs-helper-domain-name",
				&cpufreq_cpu_data->dvfs_name);
	if (ret) {
		dev_err(dev, "Could not find domain name in dts");
		return ret;
	}

	list_for_each_entry_safe(domain, tmp, &cpu_perf_domain_list, list) {
		if (strcmp(cpufreq_cpu_data->dvfs_name, domain->domain_folder_name) == 0) {
			cpufreq_cpu_data->domain_data = domain;
			ret = dvfs_fe_prepare_domain(domain->domain_id);
			if (ret) {
				dev_err(dev, "Failed to prepare domain '%s'! [%d]\n",
					cpufreq_cpu_data->dvfs_name, ret);
				return ret;
			}
		}
	}

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

static void cpufreq_data_exit(struct gs_cpufreq_data *cpufreq_data, int cpu)
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

static void google_cpufreq_data_exit_all(struct gs_cpufreq_data *cpufreq_data)
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

static int init_cpufreq_data(struct device *dev)
{
	struct cpufreq_policy *policy;
	struct gs_cpufreq_data *cpufreq_data;
	unsigned int i;
	int ret;

	cpufreq_data = devm_kzalloc(dev, sizeof(struct gs_cpufreq_data), GFP_KERNEL);
	if (!cpufreq_data)
		return -ENOMEM;

	cpufreq_data->dev = dev;
	cpufreq_data->cpu_data_arr_size = nr_cpu_ids;
	cpufreq_data->cpu_data_arr = devm_kcalloc(dev, nr_cpu_ids,
						  sizeof(*cpufreq_data->cpu_data_arr),
						  GFP_KERNEL);
	if (!cpufreq_data->cpu_data_arr)
		return -ENOMEM;

	for (i = 0; i < nr_cpu_ids; ++i) {
		ret = cpufreq_cpu_data_init(cpufreq_data, i);
		if (ret) {
			dev_err(dev, "cpufreq_cpu_data for cpu (%d) failed. Ret (%d)\n",
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
		struct cpufreq_cpu_data *cpufreq_cpu_data;
		struct perf_domain *domain;

		policy = cpufreq_cpu_get(i);
		if (!policy) {
			dev_info(dev, "cpufreq_cpu_get() for cpu (%d) doesn't return policy\n", i);
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
		cpufreq_cpu_data = cpufreq_data->cpu_data_arr[i];
		domain = cpufreq_cpu_data->domain_data;
		ret = vote_manager_init_cpufreq_with_mbfs(policy, domain->perf_domain_handle);
		if (ret < 0) {
			cpufreq_cpu_put(policy);
			goto remove_vote_track;
		}
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

static void deinit_cpufreq_data(struct device *dev)
{
	struct gs_cpufreq_data *cpufreq_data = cpufreq_get_driver_data();

	unregister_vote_track();
	cpufreq_unregister_driver(&google_cpufreq_driver);
	google_cpufreq_driver.driver_data = NULL;

	google_cpufreq_data_exit_all(cpufreq_data);
}

/*
 * gs_perf_domain_find_devfreq - find devfreq by name
 * @name: domain name
 * @type: type of the devfreq
 *
 * Search the list of devfreq_data and return the matched devfreq
 * by comparing the name and type, or return error pointer
 *
 * Behavior of GS_RECOMMENDED_DEVFREQ:
 *  - If target devfreq exists, return it,
 *  - Otherwise, return clamp devfreq
 */
struct devfreq *gs_perf_domain_find_devfreq(const char *name, enum gs_devfreq_type type)
{
	struct gs_devfreq_data *df;
	enum gs_devfreq_type selected_type;
	struct devfreq *ret_devfreq = ERR_PTR(-ENODEV);

	if (!devfreqs_are_ready)
		return ERR_PTR(-EPROBE_DEFER);

	if (type == GS_RECOMMENDED_DEVFREQ)
		selected_type = GS_TARGET_DEVFREQ;
	else
		selected_type = type;

	mutex_lock(&devfreq_data_lock);
	list_for_each_entry(df, &gs_devfreq_data_list, list) {
		if (strcmp(name, df->domain_data->domain_folder_name) == 0) {
			if (df->type == selected_type) {
				mutex_unlock(&devfreq_data_lock);
				return df->devfreq;
			}
			if (type == GS_RECOMMENDED_DEVFREQ)
				ret_devfreq = df->devfreq;
		}
	}
	mutex_unlock(&devfreq_data_lock);
	return ret_devfreq;
}
EXPORT_SYMBOL_GPL(gs_perf_domain_find_devfreq);

static int gs_devfreq_get_cur_freq(struct device *parent,
	unsigned long *freq)
{
	struct gs_devfreq_data *data = container_of(parent, struct gs_devfreq_data, dev);

	*freq = data->cur_freq_Hz;

	return 0;
}

static s64 gs_devfreq_get_freq_from_level(struct gs_devfreq_data *df_data, unsigned int pf_level)
{
	struct perf_domain *pdomain = df_data->domain_data;
	const struct dvfs_domain_info *dvfs_domain =
			dvfs_helper_get_domain(pdomain->dvfs_helper_name);

	return dvfs_helper_lvl_to_freq_exact(dvfs_domain, pf_level);
}

static int gs_devfreq_get_pf_level_floor(struct gs_devfreq_data *df_data, unsigned long target_freq)
{
	struct perf_domain *pdomain = df_data->domain_data;
	const struct dvfs_domain_info *dvfs_domain =
			dvfs_helper_get_domain(pdomain->dvfs_helper_name);

	return dvfs_helper_freq_to_lvl_floor(dvfs_domain, target_freq);
}

static int gs_devfreq_get_pf_level_ceil(struct gs_devfreq_data *df_data, unsigned long target_freq)
{
	struct perf_domain *pdomain = df_data->domain_data;
	const struct dvfs_domain_info *dvfs_domain =
			dvfs_helper_get_domain(pdomain->dvfs_helper_name);

	return dvfs_helper_freq_to_lvl_ceil(dvfs_domain, target_freq);
}

static int do_frequency_change_raw(struct gs_devfreq_data *df_data)
{
	struct fast_devfreq_vote_data *fvote_data = &df_data->fvote_data;
	struct perf_domain *pdomain = df_data->domain_data;
	unsigned long vote_min, vote_max;
	unsigned long adjusted_target_freq;
	s64 actual_freq;
	int pf_level, pf_level_lb;

	vote_min = dev_pm_qos_read_value(&df_data->dev, DEV_PM_QOS_MIN_FREQUENCY);
	vote_max = dev_pm_qos_read_value(&df_data->dev, DEV_PM_QOS_MAX_FREQUENCY);

	vote_min = HZ_PER_KHZ * vote_min;
	vote_max = HZ_PER_KHZ * vote_max;

	for (int i = 0; i < MAX_FAST_DEVFREQ_VOTE; i++) {
		if (fvote_data->fdf_votes[i] != NULL &&
		    fvote_data->fdf_votes[i]->min_freq_Hz > vote_min)
			vote_min = fvote_data->fdf_votes[i]->min_freq_Hz;
	}

	if (vote_min > vote_max)
		adjusted_target_freq = vote_max;
	else
		adjusted_target_freq = vote_min;

	/*
	 * get_pf_level should obtain the pf_level from
	 * the manually embedded OPP<-> pf_level conversion table
	 */
	pf_level_lb = gs_devfreq_get_pf_level_floor(df_data, vote_max);
	pf_level = gs_devfreq_get_pf_level_ceil(df_data, adjusted_target_freq);

	if (pf_level <= pf_level_lb)
		pf_level = pf_level_lb;

	actual_freq = gs_devfreq_get_freq_from_level(df_data, pf_level);

	if (actual_freq != df_data->cur_freq_Hz) {
		if (strcmp(df_data->domain_data->domain_folder_name,
						 gmc_memss_map.gmc_domain_name) == 0) {
			if (gmc_memss_map.gmc_dev && gmc_memss_map.memss_dev &&
				gmc_memss_map.irm_client) {
				unsigned int memss_pf;

				if (pf_level >= GMC_MAX_PF_LEVELS) {
					dev_warn_once(&df_data->dev,
						      "pf_level %d exceeds max %d, clamping\n",
						      pf_level, GMC_MAX_PF_LEVELS - 1);
					pf_level = GMC_MAX_PF_LEVELS - 1;
				}

				memss_pf = gmc_memss_map.gmc_to_memss_pf_map[pf_level];

				set_irm_subclient_pf_req_gmc(&gmc_memss_map.subclient, pf_level);
				set_irm_subclient_pf_req_memss(&gmc_memss_map.subclient, memss_pf);

				stage_irm_subclient_vote(&gmc_memss_map.subclient);

				if (publish_irm_vote(gmc_memss_map.irm_client))
					dev_err(&df_data->dev, "Failed to publish IRM vote for GMC/MEMSS\n");
				else
					df_data->cur_freq_Hz = actual_freq;
			} else
				dev_err(&df_data->dev, "IRM client is not ready\n");
		} else {
			dvfs_fe_set_pf_level(pdomain->domain_id, pf_level);
			df_data->cur_freq_Hz = actual_freq;
		}
	}

	return 0;
}

static int do_frequency_change(struct gs_devfreq_data *df_data)
{
	struct fast_devfreq_vote_data *fvote_data = &df_data->fvote_data;
	unsigned long flags;

	spin_lock_irqsave(&fvote_data->fvote_lock, flags);

	do_frequency_change_raw(df_data);

	spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);

	return 0;
}

static int gs_devfreq_target(struct device *parent,
	unsigned long *freq, u32 flags)
{
	struct gs_devfreq_data *df = container_of(parent, struct gs_devfreq_data, dev);

	if (!df->dvfs_enabled) {
		pr_debug("DVFS not enabled\n");
		return 0;
	}

	*freq = dvfs_helper_round_rate(
			dvfs_helper_get_domain_from_id(df->domain_data->domain_id), *freq);

	update_fast_devfreq_vote(&df->fdf_pm_qos_vote, *freq / HZ_PER_KHZ);

	return 0;
}

static int gs_devfreq_target_no_op(struct device *parent,
	unsigned long *freq, u32 flags)
{
	struct gs_devfreq_data *data = container_of(parent, struct gs_devfreq_data, dev);
	*freq = dvfs_helper_round_rate(
		dvfs_helper_get_domain_from_id(data->domain_data->domain_id), *freq);

	data->cur_freq_Hz = *freq;
	return 0;
}

int update_fast_devfreq_vote(struct fast_devfreq_vote *fdf_vote,
			     unsigned long target_freq)
{
	struct gs_devfreq_data *df_data = (struct gs_devfreq_data *) fdf_vote->df->data;
	struct fast_devfreq_vote_data *fvote_data = &df_data->fvote_data;
	unsigned long flags;

	if (!df_data->dvfs_enabled) {
		pr_debug("DVFS not enabled\n");
		return -EPERM;
	}

	spin_lock_irqsave(&fvote_data->fvote_lock, flags);

	fdf_vote->min_freq_Hz = HZ_PER_KHZ * target_freq;
	do_frequency_change_raw(df_data);

	spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(update_fast_devfreq_vote);

static int gs_target_devfreq_update_max_freq(struct notifier_block *nb, unsigned long event,
					      void *data)
{
	struct gs_devfreq_data *df_data = container_of(nb, struct gs_devfreq_data,
						       max_freq_nb);

	if (!df_data->dvfs_enabled) {
		pr_debug("DVFS not enabled\n");
		return NOTIFY_STOP;
	}

	do_frequency_change(df_data);

	return NOTIFY_OK;
}

static int __update_min_freq(struct gs_devfreq_data *df)
{
	u32 pf_level;
	s32 min_freq;
	unsigned long min_freq_hz;
	struct dev_pm_opp *opp;
	enum mbfs_error_code mbfs_ret;
	u32 flags = 0;
	union val64 file_content = { 0 };
	struct perf_domain *domain = df->domain_data;

	min_freq = dev_pm_qos_read_value(&df->dev, DEV_PM_QOS_MIN_FREQUENCY);
	if (min_freq < 0) {
		dev_err(&df->dev, "failed to read minfreq, err: %d\n", min_freq);
		goto out;
	}

	min_freq_hz = (unsigned long)HZ_PER_KHZ * min_freq;

	flags &= ~DEVFREQ_FLAG_LEAST_UPPER_BOUND; /* Use GLB */
	opp = devfreq_recommended_opp(&df->dev, &min_freq_hz, flags);

	pf_level = dev_pm_opp_get_level(opp);
	dev_pm_opp_put(opp);

	file_content.number = pf_level;
	mbfs_ret = mbfs_write_file(domain->max_opp_handle, file_content);
	if (mbfs_ret) {
		dev_err(&df->dev, "Failed to send max freq vote\n");
		return NOTIFY_DONE;
	}

out:
	return NOTIFY_OK;
}

static int gs_clamp_devfreq_update_min_freq(struct notifier_block *nb, unsigned long event,
					      void *data)
{
	struct gs_devfreq_data *df = container_of(nb, struct gs_devfreq_data, min_freq_nb);
	int ret;

	mutex_lock(&df->dvfs_update_lock);
	/*
	 * If DVFS is not yet enabled, do not act on this frequency vote yet.
	 * NB: The vote itself is not lost; it is cached by the PM QoS framework
	 * and will be automatically factored into frequency aggregation as
	 * soon as DVFS is enabled.
	 */
	if (!df->dvfs_enabled) {
		dev_dbg(&df->dev, "Skipping min frequency vote; DVFS not enabled\n");
		mutex_unlock(&df->dvfs_update_lock);
		return NOTIFY_DONE;
	}
	ret =  __update_min_freq(df);
	mutex_unlock(&df->dvfs_update_lock);

	return ret;
}

static int __update_max_freq(struct gs_devfreq_data *df)
{
	u32 pf_level;
	s32 max_freq;
	unsigned long max_freq_hz;
	struct dev_pm_opp *opp;
	enum mbfs_error_code mbfs_ret;
	u32 flags = 0;
	union val64 file_content = { 0 };
	struct perf_domain *domain = df->domain_data;

	max_freq = dev_pm_qos_read_value(&df->dev, DEV_PM_QOS_MAX_FREQUENCY);
	if (max_freq < 0) {
		dev_err(&df->dev, "failed to read maxfreq, err: %d\n", max_freq);
		goto out;
	}

	max_freq_hz = (unsigned long)HZ_PER_KHZ * max_freq;

	flags |= DEVFREQ_FLAG_LEAST_UPPER_BOUND; /* Use LUB */
	opp = devfreq_recommended_opp(&df->dev, &max_freq_hz, flags);

	pf_level = dev_pm_opp_get_level(opp);
	dev_pm_opp_put(opp);

	file_content.number = pf_level;
	mbfs_ret = mbfs_write_file(domain->min_opp_handle, file_content);
	if (mbfs_ret) {
		dev_err(&df->dev, "Failed to send max freq vote\n");
		return NOTIFY_DONE;
	}

out:
	return NOTIFY_OK;
}

static int gs_clamp_devfreq_update_max_freq(struct notifier_block *nb, unsigned long event, void *data)
{
	struct gs_devfreq_data *df = container_of(nb, struct gs_devfreq_data, max_freq_nb);
	int ret;
	/*
	 * If DVFS is not yet enabled, do not act on this frequency vote yet.
	 * NB: The vote itself is not lost; it is cached by the PM QoS framework
	 * and will be automatically factored into frequency aggregation as
	 * soon as DVFS is enabled.
	 */
	mutex_lock(&df->dvfs_update_lock);
	if (!df->dvfs_enabled) {
		dev_dbg(&df->dev, "Skipping max frequency vote; DVFS not enabled\n");
		mutex_unlock(&df->dvfs_update_lock);
		return NOTIFY_DONE;
	}
	ret = __update_max_freq(df);
	mutex_unlock(&df->dvfs_update_lock);

	return ret;
}

static int fast_devfreq_votes_show(struct seq_file *m, void *unused)
{
	struct fast_devfreq_vote_data *fvote_data = m->private;
	unsigned long flags;

	spin_lock_irqsave(&fvote_data->fvote_lock, flags);

	for (int i = 0; i < MAX_FAST_DEVFREQ_VOTE; i++) {
		if (fvote_data->fdf_votes[i] != NULL) {
			seq_printf(m, "  %s: %lu\n",
				fvote_data->fdf_votes[i]->vote_name,
				fvote_data->fdf_votes[i]->min_freq_Hz);
		}
	}

	spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fast_devfreq_votes);

int add_fast_devfreq_vote(struct devfreq *df, const char *vote_name,
			  struct fast_devfreq_vote *fdf_vote)
{
	struct gs_devfreq_data *df_data = (struct gs_devfreq_data *) df->data;
	struct fast_devfreq_vote_data *fvote_data;
	int add_idx = -1;
	unsigned long flags;

	if (df_data)
		fvote_data = &df_data->fvote_data;
	else {
		pr_err("Invalid devfreq device\n");
		return -1;
	}

	if (!df_data->fast_devfreq_vote_enabled) {
		pr_err("Fast devfreq vote is not available\n");
		return -1;
	}

	fdf_vote->vote_name = kstrdup(vote_name, GFP_KERNEL);

	spin_lock_irqsave(&fvote_data->fvote_lock, flags);

	for (int i = 0; i < MAX_FAST_DEVFREQ_VOTE; i++) {
		if (fvote_data->fdf_votes[i] == NULL) {
			add_idx = i;
			break;
		}
	}

	if (add_idx < 0) {
		spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);
		return -1;
	}

	fdf_vote->df = df;
	fdf_vote->min_freq_Hz = 0;
	fvote_data->fdf_votes[add_idx] = fdf_vote;

	spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(add_fast_devfreq_vote);

int remove_fast_devfreq_vote(struct fast_devfreq_vote *fdf_vote)
{
	struct gs_devfreq_data *df_data = (struct gs_devfreq_data *) fdf_vote->df->data;
	struct fast_devfreq_vote_data *fvote_data = &df_data->fvote_data;
	int remove_idx = -1;
	unsigned long flags;

	spin_lock_irqsave(&fvote_data->fvote_lock, flags);

	for (int i = 0; i < MAX_FAST_DEVFREQ_VOTE; i++) {
		if (fdf_vote == fvote_data->fdf_votes[i]) {
			remove_idx = i;
			break;
		}
	}

	if (remove_idx < 0) {
		spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);
		return -1;
	}

	fvote_data->fdf_votes[remove_idx]->df = NULL;
	fvote_data->fdf_votes[remove_idx]->min_freq_Hz = 0;
	fvote_data->fdf_votes[remove_idx] = NULL;

	spin_unlock_irqrestore(&fvote_data->fvote_lock, flags);

	kfree(fdf_vote->vote_name);

	return 0;
}
EXPORT_SYMBOL_GPL(remove_fast_devfreq_vote);

static struct perf_domain *init_perf_domain(union mbfs_client_handle perf_domains_folder_h,
		int index, struct device *dev)
{
	struct perf_domain *domain;
	enum mbfs_error_code mbfs_ret;
	union mbfs_client_handle perf_domain_h;
	struct mbfs_client_node_desc desc;
	union val64 content;

	mbfs_ret = mbfs_get_nth_child(perf_domains_folder_h, MBFS_FOLDER, index, &perf_domain_h);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get child for perf domain\n");
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}

	domain = devm_kzalloc(dev, sizeof(struct perf_domain), GFP_KERNEL);
	if (!domain)
		return ERR_PTR(-ENOMEM);

	domain->perf_domain_handle = perf_domain_h;

	mbfs_ret = mbfs_get_node_desc(perf_domain_h, &desc);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get node desc for perf domain subfolder\n");
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}
	strscpy(domain->domain_folder_name, desc.name, sizeof(domain->domain_folder_name));

	mbfs_ret = mbfs_read_child_by_name(perf_domain_h, "DvfsHelperName", &content);
	if (mbfs_ret) {
		dev_err(dev, "Failed to read DvfsHelperName for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}
	memcpy(domain->dvfs_helper_name, content.text, DOMAIN_NAME_LEN);
	domain->dvfs_helper_name[DOMAIN_NAME_LEN] = '\0';

	mbfs_ret = mbfs_read_child_by_name(perf_domain_h, "DvfsHelperId", &content);
	if (mbfs_ret) {
		dev_err(dev, "Failed to read DvfsHelperId for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}
	domain->domain_id = content.number;

	mbfs_ret = mbfs_read_child_by_name(perf_domain_h, "cf.TargetNeeded", &content);
	if (mbfs_ret) {
		dev_err(dev, "Failed to read cf.TargetNeeded for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}
	if (content.number)
		domain->cf_target_needed = true;

	mbfs_ret = mbfs_read_child_by_name(perf_domain_h, "df.ClampNeeded", &content);
	if (mbfs_ret) {
		dev_err(dev, "Failed to read df.ClampNeeded for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}
	if (content.number)
		domain->df_clamp_needed = true;

	mbfs_ret = mbfs_read_child_by_name(perf_domain_h, "df.TargetNeeded", &content);
	if (mbfs_ret) {
		dev_err(dev, "Failed to read df.TargetNeeded for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}
	if (content.number)
		domain->df_target_needed = true;

	mbfs_ret =
		mbfs_get_child_by_name(perf_domain_h, "agg.min.devfreq", &domain->min_opp_handle);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get handle for agg.min.devfreq for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}

	mbfs_ret =
		mbfs_get_child_by_name(perf_domain_h, "agg.max.devfreq", &domain->max_opp_handle);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get handle for agg.max.devfreq for perf domain %s\n",
			domain->domain_folder_name);
		return MBFS_ERR_TO_ERR_PTR(mbfs_ret);
	}

	mbfs_ret =
		mbfs_get_child_by_name(perf_domain_h, "CurOpp", &domain->cur_opp_handle);
	if (mbfs_ret) {
		dev_warn(dev, "Failed to get handle for CurOpp for perf domain %s\n",
			domain->domain_folder_name);
		domain->cur_opp_handle = MBFS_INVALID_HANDLE;
	}

	return domain;
}

static int init_fast_devfreq_vote_data(struct fast_devfreq_vote_data *fvote_data,
				       struct devfreq *devfreq)
{
	int i;

	if (!devfreq)
		return -EINVAL;

	spin_lock_init(&fvote_data->fvote_lock);

	fvote_data->df = devfreq;

	for (i = 0; i < MAX_FAST_DEVFREQ_VOTE; i++)
		fvote_data->fdf_votes[i] = NULL;

	return 0;
}

static ssize_t fw_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct devfreq *df = container_of(dev, struct devfreq, dev);
	struct gs_devfreq_data *df_data = (struct gs_devfreq_data *)df->data;
	struct perf_domain *domain = df_data->domain_data;
	enum mbfs_error_code mbfs_ret;
	union val64 content;
	s64 freq;

	if (is_mbfs_handle_invalid(domain->cur_opp_handle))
		return -ENODEV;

	mbfs_ret = mbfs_read_file(domain->cur_opp_handle, &content);
	if (mbfs_ret)
		return mbfs_error2linux(mbfs_ret);

	freq = gs_devfreq_get_freq_from_level(df_data, content.number);
	if (freq < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%lld\n", freq);
}
static DEVICE_ATTR_RO(fw_freq);

static struct attribute *perf_domain_dev_attrs[] = {
	&dev_attr_fw_freq.attr,
	NULL,
};

static const struct attribute_group perf_domain_dev_attr_group = {
	.attrs = perf_domain_dev_attrs,
};

static int init_devfreq_data(struct device *parent,
		struct perf_domain *domain, enum gs_devfreq_type type)
{
	int err;
	struct gs_devfreq_data *df = devm_kzalloc(parent,
			sizeof(struct gs_devfreq_data), GFP_KERNEL);
	struct device *dev = &df->dev;
	struct dentry *debugfs_file;

	if (!df) {
		dev_err(parent, "Failed to allocate space for gs_devfreq_data\n");
		return -ENOMEM;
	}

	df->domain_data = domain;
	df->type = type;

	mutex_init(&df->dvfs_update_lock);
	df->dev.parent = parent;
	df->dev.class = perf_domain_class;
	INIT_LIST_HEAD(&df->list);

	dev_set_name(dev, "%s_%s", domain->domain_folder_name, dev_name_suffix[type]);
	err = device_register(dev);
	if (err) {
		put_device(dev);
		dev_err(parent, "Unable to register perf_domain device for %s\n",
			domain->domain_folder_name);
		goto out;
	}

	err = dvfs_helper_add_opps_to_device_by_name(dev, domain->dvfs_helper_name);
	if (err)
		goto out;

	df->devfreq_profile.get_cur_freq = gs_devfreq_get_cur_freq;
	switch (type) {
	case GS_TARGET_DEVFREQ:
		df->devfreq_profile.target = gs_devfreq_target;

		err = dvfs_fe_prepare_domain(domain->domain_id);
		if (err) {
			dev_err(dev, "failed to prepare for domain: %d\n", domain->domain_id);
			goto out;
		}

		break;

	case GS_CLAMP_DEVFREQ:
		df->devfreq_profile.target = gs_devfreq_target_no_op;

		df->min_freq_nb.notifier_call = gs_clamp_devfreq_update_min_freq;
		err = dev_pm_qos_add_notifier(dev, &df->min_freq_nb,
				DEV_PM_QOS_MIN_FREQUENCY);
		if (err) {
			dev_err(dev, "failed to add min_freq notifier\n");
			goto out;
		}

		df->max_freq_nb.notifier_call = gs_clamp_devfreq_update_max_freq;
		err = dev_pm_qos_add_notifier(dev, &df->max_freq_nb,
				DEV_PM_QOS_MAX_FREQUENCY);
		if (err) {
			dev_err(dev, "failed to add max_freq notifier\n");
			goto remove_min_nb;
		}
		break;
	default:
		break;
	}

	df->devfreq = devm_devfreq_add_device(dev,
		&df->devfreq_profile, DEVFREQ_GOV_POWERSAVE, df);

	if (type == GS_TARGET_DEVFREQ) {
		df->fast_devfreq_vote_enabled = true;
		init_fast_devfreq_vote_data(&df->fvote_data, df->devfreq);

		// target frequency vote via PM QoS becomes one of fast devfreq vote.
		err = add_fast_devfreq_vote(df->devfreq, "pm_qos", &df->fdf_pm_qos_vote);
		if (err)
			goto out;

		// Tracking MAX change via PM QoS Interface
		df->max_freq_nb.notifier_call = gs_target_devfreq_update_max_freq;
		err = dev_pm_qos_add_notifier(dev, &df->max_freq_nb,
				DEV_PM_QOS_MAX_FREQUENCY);
		if (err) {
			dev_err(dev, "failed to add max_freq notifier\n");
			goto remove_min_nb;
		}
	}

	if (!df->fast_devfreq_vote_enabled && strcmp("gmc", df->domain_data->domain_folder_name) == 0) {
		df->fast_devfreq_vote_enabled = true;
		init_fast_devfreq_vote_data(&df->fvote_data, df->devfreq);

		// target frequency vote via PM QoS becomes one of fast devfreq vote.
		err = add_fast_devfreq_vote(df->devfreq, "pm_qos", &df->fdf_pm_qos_vote);
		if (err)
			goto out;
	}

	if (IS_ERR(df->devfreq)) {
		dev_err(dev, "failed to add devfreq device\n");
		err =  PTR_ERR(df->devfreq);
		goto remove_max_nb;
	}

	err = google_register_devfreq(df->devfreq);
	if (err)
		goto remove_max_nb;

	err = devm_device_add_group(&df->devfreq->dev, &perf_domain_dev_attr_group);
	if (err)
		goto unregister_devfreq;

	err = vote_manager_init_devfreq_with_mbfs(df->devfreq, domain->perf_domain_handle);
	if (err)
		goto unregister_devfreq;

	mutex_lock(&df->dvfs_update_lock);
	/*
	 * Enable DVFS now that the vote manager is initialized.
	 */
	df->dvfs_enabled = true;
	/*
	 * Force an update to write aggregation of the min/max votes into the MBFS file.
	 */
	__update_max_freq(df);
	__update_min_freq(df);
	mutex_unlock(&df->dvfs_update_lock);

	list_add_tail(&df->list, &gs_devfreq_data_list);

	debugfs_file = debugfs_create_file(dev_name(dev), 0444, debugfs_dir,
					   &df->fvote_data, &fast_devfreq_votes_fops);

	if (!debugfs_file) {
		pr_err("%s: Fail to create debugfs node to print out fast_devfreq_vote\n",
		       dev_name(dev));
	}

	return 0;

unregister_devfreq:
	google_unregister_devfreq(df->devfreq);
remove_max_nb:
	dev_pm_qos_remove_notifier(dev, &df->max_freq_nb, DEV_PM_QOS_MAX_FREQUENCY);
remove_min_nb:
	dev_pm_qos_remove_notifier(dev, &df->min_freq_nb, DEV_PM_QOS_MIN_FREQUENCY);
out:
	return err;
}

static void deinit_devfreq_data(struct gs_devfreq_data *df)
{
	struct device *dev = &df->dev;

	if (df->type == GS_CLAMP_DEVFREQ) {
		dev_pm_qos_remove_notifier(dev, &df->min_freq_nb, DEV_PM_QOS_MIN_FREQUENCY);
		dev_pm_qos_remove_notifier(dev, &df->max_freq_nb, DEV_PM_QOS_MAX_FREQUENCY);
	}

	if (df->fast_devfreq_vote_enabled)
		remove_fast_devfreq_vote(&df->fdf_pm_qos_vote);

	if (!IS_ERR_OR_NULL(df->devfreq)) {
		df->dvfs_enabled = false;
		vote_manager_remove_devfreq(df->devfreq);
		google_unregister_devfreq(df->devfreq);
		devm_devfreq_remove_device(dev, df->devfreq);
	}

	list_del(&df->list);
}

static void populate_gmc_memss_pf_map(struct device *dev)
{
	struct gmc_memss_irm_map *map = &gmc_memss_map;
	int i, gmc_pf, memss_pf;
	unsigned long gmc_freq, memss_freq;

	if (!map->gmc_dev || !map->memss_dev)
		return;

	for (i = 0; i < GMC_MAX_PF_LEVELS; i++)
		map->gmc_to_memss_pf_map[i] = 0;

	for (i = 0; i < map->num_table_entries; i++) {
		gmc_freq = map->raw_gmc_memss_table[i * 2];
		memss_freq = map->raw_gmc_memss_table[i * 2 + 1];

		gmc_pf = gs_devfreq_get_pf_level_ceil(container_of(map->gmc_dev,
		 struct gs_devfreq_data, dev), gmc_freq);
		memss_pf = gs_devfreq_get_pf_level_ceil(container_of(map->memss_dev,
		 struct gs_devfreq_data, dev), memss_freq);

		if (gmc_pf >= 0 && gmc_pf < GMC_MAX_PF_LEVELS && memss_pf >= 0)
			map->gmc_to_memss_pf_map[gmc_pf] = (unsigned int)memss_pf;
	}

	/* Fill gaps in the map */
	for (i = 1; i < GMC_MAX_PF_LEVELS; i++) {
		if (map->gmc_to_memss_pf_map[i] == 0)
			map->gmc_to_memss_pf_map[i] = map->gmc_to_memss_pf_map[i - 1];
		else
			map->gmc_to_memss_pf_map[i] =
				max(map->gmc_to_memss_pf_map[i], map->gmc_to_memss_pf_map[i - 1]);
	}
	dev_info(dev, "Successfully built GMC-MEMSS PF map\n");
}

static void link_gmc_memss_devices(struct device *dev)
{
	struct gs_devfreq_data *df;
	struct gmc_memss_irm_map *map = &gmc_memss_map;

	if (!devfreqs_are_ready || map->gmc_dev || map->memss_dev || !map->irm_client)
		return;

	mutex_lock(&devfreq_data_lock);
	list_for_each_entry(df, &gs_devfreq_data_list, list) {
		if (strcmp(map->gmc_domain_name,
							df->domain_data->domain_folder_name) == 0) {
			map->gmc_dev = &df->dev;
			dev_info(dev, "Linked GMC device: %s", dev_name(map->gmc_dev));
		}
		if (strcmp(map->memss_domain_name,
							df->domain_data->domain_folder_name) == 0) {
			map->memss_dev = &df->dev;
			dev_info(dev, "Linked MEMSS device: %s\n", dev_name(map->memss_dev));
		}
	}
	mutex_unlock(&devfreq_data_lock);

	if (!map->gmc_dev)
		dev_err(dev, "Failed to link GMC device: %s", map->gmc_domain_name);
	if (!map->memss_dev)
		dev_err(dev, "Failed to link MEMSS device: %s", map->memss_domain_name);

	if (map->gmc_dev && map->memss_dev)
		populate_gmc_memss_pf_map(dev);
}

static void gs_perf_domain_remove_irm_subclient(void *data)
{
	struct gmc_memss_irm_map *map = data;

	if (map->irm_client) {
		remove_irm_subclient(&map->subclient);
		map->irm_client = NULL;
	}
}

static int parse_gmc_memss_table(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *gmc_np, *memss_np;
	struct gmc_memss_irm_map *map = &gmc_memss_map;
	const char *gmc_dev_name, *memss_dev_name;
	int i, ret, temp_len, num_u64_entries;

	/* find the irm_gmc_freq node */
	np = of_find_compatible_node(NULL, NULL, "google,irm-devfreq");
	if (!np) {
		dev_info(dev, "No google,irm-devfreq node found\n");
		return -ENODEV;
	}

	gmc_np = of_get_child_by_name(np, "gmc_dvfs");
	if (!gmc_np) {
		dev_err(dev, "Failed to find gmc_dvfs node\n");
		ret = -ENODEV;
		goto put_np;
	}
	ret = of_property_read_string(gmc_np, "dvfs-helper-domain-name", &gmc_dev_name);
	if (ret) {
		dev_err(dev, "Failed to read gmc_dvfs,dvfs-helper-domain-name: %d\n", ret);
		of_node_put(gmc_np);
		goto put_np;
	}
	strscpy(map->gmc_domain_name, "gmc", sizeof(map->gmc_domain_name));

	of_node_put(gmc_np);

	memss_np = of_get_child_by_name(np, "memss_dvfs");
	if (!memss_np) {
		dev_err(dev, "Failed to find memss_dvfs node\n");
		ret = -ENODEV;
		goto put_np;
	}
	ret = of_property_read_string(memss_np, "dvfs-helper-domain-name", &memss_dev_name);
	if (ret) {
		dev_err(dev, "Failed to read memss_dvfs,dvfs-helper-domain-name: %d\n", ret);
		of_node_put(memss_np);
		goto put_np;
	}
	strscpy(map->memss_domain_name, memss_dev_name, sizeof(map->memss_domain_name));
	of_node_put(memss_np);

	map->irm_client = get_irm_client("apc");
	if (IS_ERR(map->irm_client)) {
		dev_err(dev, "Failed to get IRM client: %ld\n",
					PTR_ERR(map->irm_client));
		ret = PTR_ERR(map->irm_client);
		goto put_np;
	}

	ret = add_irm_subclient(map->irm_client, &map->subclient, "gmc_memss_coord");
	if (ret) {
		dev_err(dev, "Failed to add GMC-MEMSS subclient: %d\n", ret);
		goto err_irm_client;
	}

	ret = devm_add_action_or_reset(dev, gs_perf_domain_remove_irm_subclient, map);
	if (ret) {
		dev_err(dev, "Failed to add devm action for subclient: %d\n", ret);
		goto err_irm_client;
	}

	if (!of_find_property(np, "gmc-memss-table", &temp_len)) {
		dev_err(dev, "gmc-memss-table property not found\n");
		ret = -ENOENT;
		goto put_np;
	}

	if (temp_len % sizeof(u64) != 0) {
		dev_err(dev, "gmc-memss-table size is not a multiple of 8 bytes\n");
		ret = -EINVAL;
		goto put_np;
	}
	num_u64_entries = temp_len / sizeof(u64);

	if (num_u64_entries % 2 != 0) {
		dev_err(dev, "gmc-memss-table must have an even number of u64 entries\n");
		ret = -EINVAL;
		goto put_np;
	}
	map->num_table_entries = num_u64_entries / 2;

	if (map->num_table_entries == 0) {
		dev_warn(dev, "gmc-memss-table is empty\n");
		ret = -EINVAL;
		goto put_np;
	}

	map->raw_gmc_memss_table = devm_kcalloc(dev, num_u64_entries, sizeof(u64), GFP_KERNEL);
	if (!map->raw_gmc_memss_table) {
		ret = -ENOMEM;
		goto put_np;
	}

	ret = of_property_read_u64_array(
		np, "gmc-memss-table", map->raw_gmc_memss_table, num_u64_entries);

	if (ret) {
		dev_err(dev, "Failed to read gmc-memss-table as u64 array: %d\n", ret);
		goto put_np;
	}

	dev_info(dev, "Successfully parsed GMC-MEMSS table with %d pairs\n",
					map->num_table_entries);

	for (i = 0; i < map->num_table_entries; i++) {
		dev_info(dev, "	Pair %d: GMC %llu, MEMSS %llu\n", i,
			 map->raw_gmc_memss_table[i * 2], map->raw_gmc_memss_table[i * 2 + 1]);
	}
	ret = 0;
	goto put_np;

err_irm_client:
	map->irm_client = NULL;
put_np:
	of_node_put(np);
	return ret;
}

static int perf_domain_probe(struct platform_device *pdev)
{
	const char *path_string;
	union mbfs_client_handle perf_domains_folder_h;
	struct mbfs_client_node_desc desc;
	enum mbfs_error_code mbfs_ret;
	int num_perf_domains;
	struct device *dev = &pdev->dev;
	int ret = 0;

	ret = of_property_read_string(dev->of_node, "mbfs-perf-domains-path", &path_string);
	if (ret) {
		dev_info(dev, "mbfs-perf-domains-path property not found in dts\n");
		return ret;
	}

	mbfs_ret = mbfs_get_handle(path_string, &perf_domains_folder_h);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get handle for %s: %s\n", path_string,
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	mbfs_ret = mbfs_get_node_desc(perf_domains_folder_h, &desc);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get node desc for perf domains folder %s\n", path_string);
		return mbfs_error2linux(mbfs_ret);
	}

	num_perf_domains = desc.num_subfolders;

	mutex_lock(&devfreq_data_lock);
	for (int i = 0; i < num_perf_domains; i++) {
		struct perf_domain *domain = init_perf_domain(perf_domains_folder_h, i, dev);

		if (IS_ERR(domain)) {
			dev_err(dev,
				"Failed to init domain under %dth perf domain subfolder, err = %ld\n",
				i, PTR_ERR(domain));
			continue;
		}

		if (domain->df_clamp_needed) {
			ret = init_devfreq_data(dev, domain, GS_CLAMP_DEVFREQ);
			dev_err(dev, "Failed to init clamp devfreq for perf domain %s, err = %d\n",
				domain->domain_folder_name, ret);
		}
		if (domain->df_target_needed) {
			ret = init_devfreq_data(dev, domain, GS_TARGET_DEVFREQ);
			dev_err(dev, "Failed to init target devfreq for perf domain %s, err = %d\n",
				domain->domain_folder_name, ret);
		}
		if (domain->cf_target_needed)
			list_add_tail(&domain->list, &cpu_perf_domain_list);
	}
	mutex_unlock(&devfreq_data_lock);

	devfreqs_are_ready = true;

	// Parse the table to get domain names and the map entries
	ret = parse_gmc_memss_table(dev);
	if (ret && ret != -ENODEV && ret != -ENOENT)
		dev_err(dev, "Failed to parse GMC-MEMSS table: %d\n", ret);

	// Link devices after all devfreqs are up
	link_gmc_memss_devices(dev);

	if (!list_empty(&cpu_perf_domain_list)) {
		ret = init_cpufreq_data(dev);
		if (ret)
			dev_err(dev, "Failed to init cpufreq, err = %d\n", ret);
	}
	return ret;
}

static void perf_domain_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gs_devfreq_data *df, *tmp;

	dev_info(dev, "Removing device\n");
	mutex_lock(&devfreq_data_lock);
	list_for_each_entry_safe(df, tmp, &gs_devfreq_data_list, list) {
		deinit_devfreq_data(df);
	}

	gmc_memss_map.gmc_dev = NULL;
	gmc_memss_map.memss_dev = NULL;
	mutex_unlock(&devfreq_data_lock);

	devfreqs_are_ready = false;

	if (!list_empty(&cpu_perf_domain_list))
		deinit_cpufreq_data(dev);
}

static const struct of_device_id perf_domain_of_match_table[] = {
	{
		.compatible = "google,gs_perf_domain",
	},
	{},
};
MODULE_DEVICE_TABLE(of, perf_domain_of_match_table);

struct platform_driver perf_domain_driver = {
	.driver = {
		.name = "gs-perf-domain-drv",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(perf_domain_of_match_table),
	},
	.probe  = perf_domain_probe,
	.remove = perf_domain_remove,
};

static int __init perf_domain_init(void)
{
	int ret = 0;

	debugfs_dir = debugfs_create_dir("fast_devfreq_vote", NULL);
	perf_domain_class = class_create("perf_domain");
	if (IS_ERR(perf_domain_class)) {
		pr_err("%s: couldn't create class\n", __FILE__);
		return PTR_ERR(perf_domain_class);
	}

	ret = platform_driver_register(&perf_domain_driver);
	if (ret) {
		pr_err("perf_domain: Failed to register driver\n");
		return ret;
	}

	gmc_memss_debug_dir = debugfs_create_dir("gmc_memss", NULL);
	if (gmc_memss_debug_dir) {
		debugfs_create_file("mapping_table", 0444, gmc_memss_debug_dir,
				  NULL, &gmc_memss_map_fops);
	}

	return 0;
}

static void __exit perf_domain_exit(void)
{
	debugfs_remove_recursive(gmc_memss_debug_dir);
	debugfs_remove_recursive(debugfs_dir);
	platform_driver_unregister(&perf_domain_driver);
	class_destroy(perf_domain_class);
}

module_init(perf_domain_init);
module_exit(perf_domain_exit);
MODULE_AUTHOR("Ziyi Cui <ziyic@google.com>");
MODULE_DESCRIPTION("perf domain driver");
MODULE_LICENSE("GPL");
