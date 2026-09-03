// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/timekeeping.h>

#include <mem-qos/google_qos_box_reg_api.h>
#include "google_mem_qos.h"
#include "google_mem_qos_debugfs.h"
#include "google_qos_box.h"
#include "google_qos_box_of.h"

#define GOOGLE_QOS_BOX_AUTOSUSPEND_DELAY_MS		10

/*
 * Assume current QOS_POLICY[hw_policy_idx] register values are the same as which stored
 * in 'curr_policy', update QOS_POLICY[hw_policy_idx] registers only if the values are
 * different between 'curr_policy' and 'next_policy' to reduce register write latency.
 */
static int __update_hw_policy_regs(struct qos_box_dev *qos_box_dev, u32 hw_policy_idx,
				   struct qos_box_policy *next_policy,
				   struct qos_box_policy *curr_policy)
{
	size_t idx;
	u32 base, offset;

	base = QOS_POLICY_BLOCK(hw_policy_idx);

	for (idx = 0, offset = base;
	     offset < (base + QOS_POLICY_BLOCK_SIZE);
	     idx++, offset += 0x4) {
		if (next_policy->val[idx] != curr_policy->val[idx])
			writel(next_policy->val[idx], qos_box_dev->base_addr + offset);
	}

	return 0;
}

/*
 * Write all settings in 'policy' to QOS_POLICY[hw_policy_idx]
 */
static int __write_hw_policy_regs(struct qos_box_dev *qos_box_dev, u32 hw_policy_idx,
				  struct qos_box_policy *policy)
{
	struct device *dev = qos_box_dev->dev;
	size_t idx;
	u32 base, offset;

	base = QOS_POLICY_BLOCK(hw_policy_idx);

	dev_dbg(dev, "write HW policy regs, hw_policy_idx=%u, base=%08x\n",
		hw_policy_idx, base);

	for (idx = 0, offset = base;
	     offset < (base + QOS_POLICY_BLOCK_SIZE);
	     idx++, offset += 0x4) {
		writel(policy->val[idx], qos_box_dev->base_addr + offset);
	}

	return 0;
}

static int write_vc_map_cfg(struct qos_box_dev *qos_box_dev)
{
	struct qcfg *config = &qos_box_dev->config;

	writel(config->vc_map_cfg.val, qos_box_dev->base_addr + VC_MAP_CFG);

	return 0;
}

static int write_qcfg(struct qos_box_dev *qos_box_dev)
{
	int ret = 0;
	struct qcfg *config = &qos_box_dev->config;

	/* Do NOT write QOS_POLICY_CFG here */

	writel(config->bw_mon_cfg.val, qos_box_dev->base_addr + BW_MON_CFG);

	dev_dbg(qos_box_dev->dev, "%s: after write 1st reg\n", qos_box_dev->name);

	/* Update slot duration based on CYCLES_PER_SLOT */
	qos_box_dev->slot_dur_ns = config->bw_mon_cfg.cycles_per_slot * 1000;
	qos_box_dev->slot_dur_ns /= QOS_BOX_NS_PER_CYCLE;

	writel(config->vc_filter_cfg.val, qos_box_dev->base_addr + VC_FILTER_CFG);

	/* Write default QOS_POLICY[ACTIVE_QOS_HW_POLICY_IDX] with active_scenario settings */
	__write_hw_policy_regs(qos_box_dev, ACTIVE_QOS_HW_POLICY_IDX,
			       qos_box_dev->scenario_arr[qos_box_dev->active_scenario]);

	if (qos_box_dev->null_policy.enable) {
		ret = __write_hw_policy_regs(qos_box_dev, qos_box_dev->null_policy.idx,
					     &qos_box_dev->null_policy.policy);
		if (ret)
			return ret;
	}

	return 0;
}

static inline void apply_load_en_delay(struct qos_box_dev *qos_box_dev)
{
	struct qos_box_config_delay *load_en_delay = &qos_box_dev->load_en_delay;
	u64 curr_time_ns;
	u64 diff_time_ns;
	u32 delay_time_ns;

	if (!load_en_delay->enable)
		return;

	curr_time_ns = ktime_get_ns();
	diff_time_ns = curr_time_ns - load_en_delay->last_ts;

	if (diff_time_ns >= load_en_delay->delay_ns)
		return;

	delay_time_ns = (u32)(load_en_delay->delay_ns - diff_time_ns);

	ndelay(delay_time_ns);
}

static inline void update_load_en_last_ts(struct qos_box_dev *qos_box_dev)
{
	struct qos_box_config_delay *load_en_delay = &qos_box_dev->load_en_delay;

	if (load_en_delay->enable)
		load_en_delay->last_ts = ktime_get_ns();
}

static int __write_qos_policy_cfg_reg(struct qos_box_dev *qos_box_dev, u32 policy_index)
{
	u32 val;
	int ret = 0;

	val = FIELD_PREP(QOS_POLICY_CFG_INDEX_SEL_FIELD, policy_index) | QOS_POLICY_CFG_LOAD_EN;

	writel(val, qos_box_dev->base_addr + QOS_POLICY_CFG);

	dev_dbg(qos_box_dev->dev, "write QOS_POLICY_CFG = %08x\n", val);

	return ret;
}

static int write_qos_policy_cfg(struct qos_box_dev *qos_box_dev, u32 policy_index)
{
	int ret;

	apply_load_en_delay(qos_box_dev);

	if (qos_box_dev->null_policy.enable) {
		ret = __write_qos_policy_cfg_reg(qos_box_dev, qos_box_dev->null_policy.idx);
		if (ret) {
			dev_err(qos_box_dev->dev, "switch to null policy (%u) failed\n",
				qos_box_dev->null_policy.idx);
			return ret;
		}

		/* wait 1 slot duration */
		ndelay(qos_box_dev->slot_dur_ns);
	}

	ret = __write_qos_policy_cfg_reg(qos_box_dev, policy_index);
	if (ret) {
		dev_err(qos_box_dev->dev, "switch to target policy (%u) failed\n", policy_index);
		return ret;
	}

	/* wait 1 slot duration */
	ndelay(qos_box_dev->slot_dur_ns);

	update_load_en_last_ts(qos_box_dev);

	return 0;
}

/*
 * If a qos_box is attached to a genpd domain, invoke qos_box_init_qcfg only when RPM enabled
 */
static int qos_box_init_qcfg(struct qos_box_dev *qos_box_dev, bool is_reinit)
{
	int ret = 0;

	dev_dbg(qos_box_dev->dev, "%s: start write regs\n", qos_box_dev->name);

	if (!is_reinit && qos_box_dev->have_vc_map_cfg_init_val) {
		ret = write_vc_map_cfg(qos_box_dev);
		if (ret) {
			dev_err(qos_box_dev->dev, "Write VC_MAP_CFG failed, ret = %d\n", ret);
			goto out;
		}
	}

	ret = write_qcfg(qos_box_dev);
	if (ret) {
		dev_err(qos_box_dev->dev, "Write qcfg failed, ret = %d\n", ret);
		goto out;
	}

	ret = write_qos_policy_cfg(qos_box_dev, ACTIVE_QOS_HW_POLICY_IDX);
	if (ret)
		goto out;

out:
	return ret;
}

int qos_box_setting_restore(struct qos_box_dev *qos_box_dev)
{
	if (!qos_box_dev)
		return 0;

	return qos_box_init_qcfg(qos_box_dev, true);
}

int qos_box_rpm_get(struct qos_box_dev *qos_box_dev)
{
	if (pm_runtime_enabled(qos_box_dev->dev))
		return pm_runtime_resume_and_get(qos_box_dev->dev);

	return 0;
}

void qos_box_rpm_put(struct qos_box_dev *qos_box_dev)
{
	if (pm_runtime_enabled(qos_box_dev->dev)) {
		pm_runtime_mark_last_busy(qos_box_dev->dev);
		pm_runtime_put_autosuspend(qos_box_dev->dev);
	}
}

static int __config_hw_policy(struct qos_box_dev *qos_box_dev, u32 new_scenario)
{
	int ret = 0;

	__update_hw_policy_regs(qos_box_dev, ACTIVE_QOS_HW_POLICY_IDX,
				qos_box_dev->scenario_arr[new_scenario],
				qos_box_dev->scenario_arr[qos_box_dev->hw_scenario]);

	if (qos_box_dev->null_policy.enable) {
		__update_hw_policy_regs(qos_box_dev, qos_box_dev->null_policy.idx,
					&qos_box_dev->null_policy.policy,
					&qos_box_dev->por_policy);
	}

	/* HW register writes */
	ret = write_qos_policy_cfg(qos_box_dev, ACTIVE_QOS_HW_POLICY_IDX);
	if (ret)
		goto out;

	qos_box_dev->hw_scenario = new_scenario;

out:
	return ret;
}

static int google_qos_box_select_config(struct qos_box_dev *qos_box_dev, u32 scenario_idx)
{
	struct qos_box_dbg *dbg = qos_box_dev->dbg;
	int ret = 0;
	u32 next_scenario;
	unsigned long flags;

	if (!qos_box_dev)
		return 0;

	if (scenario_idx >= NUM_MEM_QOS_SCENARIO)
		return -EINVAL;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	next_scenario = qos_box_dev->scenario_arr[scenario_idx] ?
		scenario_idx :
		MEM_QOS_SCENARIO_DEFAULT;

	/* Write HW registers if debugfs isn't in control */
	if (dbg && !dbg->ctl) {
		if (qos_box_dev->is_rpm_active && qos_box_dev->hw_scenario != next_scenario)
			ret = __config_hw_policy(qos_box_dev, next_scenario);
	}

	qos_box_dev->active_scenario = next_scenario;

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	return ret;
}

static int __google_qos_box_reg_read_write(struct qos_box_dev *qos_box_dev,
					   u32 addr, u32 *val, bool is_read)
{
	struct qcfg *config = &qos_box_dev->config;
	int ret = 0;
	unsigned long flags;

	if (!qos_box_dev || !val)
		return -EINVAL;

	ret = qos_box_rpm_get(qos_box_dev);
	if (ret < 0)
		goto out;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	if (is_read) {
		*val = readl(qos_box_dev->base_addr + addr);
	} else {
		writel(*val, qos_box_dev->base_addr + addr);

		config->vc_map_cfg.val = *val;
	}

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	qos_box_rpm_put(qos_box_dev);

out:
	return ret;
}

static int google_qos_box_reg_read(struct qos_box_dev *qos_box_dev, u32 addr, u32 *val)
{
	return __google_qos_box_reg_read_write(qos_box_dev, addr, val, true);
}

static int google_qos_box_reg_write(struct qos_box_dev *qos_box_dev, u32 addr, u32 *val)
{
	return __google_qos_box_reg_read_write(qos_box_dev, addr, val, false);
}

static struct qos_box_ops __qos_box_ops = {
	.select_config = google_qos_box_select_config,
};

static int __google_qos_box_vc_map_cfg_read_write(struct qos_box_dev *qos_box_dev,
						  u32 *val, bool is_read)
{
	int ret = 0;

	if (!val || !qos_box_dev)
		return -EINVAL;

	if (is_read)
		ret = google_qos_box_reg_read(qos_box_dev, VC_MAP_CFG, val);
	else
		ret = google_qos_box_reg_write(qos_box_dev, VC_MAP_CFG, val);

	return ret;
}

int google_qos_box_vc_map_cfg_read(struct qos_box_dev *qos_box_dev, u32 *val)
{
	return __google_qos_box_vc_map_cfg_read_write(qos_box_dev, val, true);
}
EXPORT_SYMBOL_GPL(google_qos_box_vc_map_cfg_read);

struct qos_box_dev *get_qos_box_dev_by_index(struct device *consumer, int index)
{
	struct device_link *link;
	struct device_node *np_qos_box;
	struct device *dev;
	struct qos_box_dev *qos_box_dev;

	np_qos_box = of_parse_phandle(consumer->of_node, "google,qos_box_dev", index);
	if (!np_qos_box) {
		pr_err("%pOF is missing google,qos_box_dev[%d] property to link to qos_box\n",
		       consumer->of_node, index);
		return ERR_PTR(-EINVAL);
	}

	dev = driver_find_device_by_of_node(&google_qos_box_platform_driver.driver, np_qos_box);
	of_node_put(np_qos_box);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	link = device_link_add(consumer, dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!link) {
		dev_err(dev, "unable to add qos_box device link\n");
		return ERR_PTR(-EINVAL);
	}

	qos_box_dev = dev_get_drvdata(dev);
	return qos_box_dev;
}
EXPORT_SYMBOL_GPL(get_qos_box_dev_by_index);

struct qos_box_dev *get_qos_box_dev_by_name(struct device *consumer, const char *name)
{
	int index;

	index = of_property_match_string(consumer->of_node, "google,qos_box_dev_names", name);
	if (index < 0) {
		pr_err("%pOF fails to match string %s in google,qos_box_dev_names property\n",
		       consumer->of_node,
		       name);
		return ERR_PTR(index);
	}
	return get_qos_box_dev_by_index(consumer, index);
}
EXPORT_SYMBOL_GPL(get_qos_box_dev_by_name);

static int google_qos_box_rpm_suspend(struct device *dev)
{
	struct qos_box_dev *qos_box_dev = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	qos_box_dev->is_rpm_active = false;

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	return 0;
}

static int google_qos_box_rpm_resume(struct device *dev)
{
	struct qos_box_dev *qos_box_dev = dev_get_drvdata(dev);
	struct qos_box_dbg *dbg = qos_box_dev->dbg;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	qos_box_dev->is_rpm_active = true;

	/* Write HW registers if debugfs isn't in control */
	if (dbg && !dbg->ctl) {
		if (qos_box_dev->hw_scenario != qos_box_dev->active_scenario)
			ret = __config_hw_policy(qos_box_dev, qos_box_dev->active_scenario);
	}

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(google_qos_box_dev_pm_ops, google_qos_box_rpm_suspend,
				 google_qos_box_rpm_resume, NULL);

static void qos_box_debugfs_work_handler(struct work_struct *work)
{
	struct qos_box_dev *qos_box_dev =
		container_of(work, struct qos_box_dev, debugfs_work);
	struct device *dev = qos_box_dev->dev;
	int ret;

	qos_box_dev->dbg =
		devm_kzalloc(dev, sizeof(*qos_box_dev->dbg), GFP_KERNEL);
	if (!qos_box_dev->dbg) {
		dev_err(dev, "Failed to allocate memory for debugfs in work handler\n");
		return;
	}

	ret = qos_box_init_debugfs(qos_box_dev, qos_box_debugfs_entry_get());
	if (ret) {
		dev_err(dev, "Deferred qos_box_init_debugfs failed, ret = %d\n", ret);
	}
}

static int google_qos_box_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qos_box_dev *qos_box_dev;
	struct device_node *np;
	const struct qos_box_desc *desc;
	int ret = 0;

	qos_box_dev =
		devm_kzalloc(dev, sizeof(*qos_box_dev), GFP_KERNEL);
	if (!qos_box_dev)
		return -ENOMEM;

	qos_box_dev->dev = dev;
	qos_box_dev->ops = &__qos_box_ops;

	platform_set_drvdata(pdev, qos_box_dev);

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qos_box_dev->desc = *desc;

	np = dev->of_node;

	qos_box_dev->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qos_box_dev->base_addr))
		return PTR_ERR(qos_box_dev->base_addr);

	spin_lock_init(&qos_box_dev->lock);

	/* First read common proprties */
	if (of_qos_box_read_common_property(qos_box_dev, np) < 0)
		return -EINVAL;

	ret = of_property_read_string(np, "google,name", &qos_box_dev->name);
	if (ret) {
		dev_err(dev, "Read `google,name` property failed.\n");
		goto out;
	}

	if (of_qos_box_read_qcfg(qos_box_dev, np) < 0) {
		dev_err(dev, "Read qos box configurations from DT failed.\n");
		ret = -EINVAL;
		goto out;
	}

	/* Init default active_scenario before calling qos_box_init_qcfg() */
	qos_box_dev->active_scenario = MEM_QOS_SCENARIO_DEFAULT;
	qos_box_dev->hw_scenario = qos_box_dev->active_scenario;

	/* Init HW settings */
	ret = qos_box_init_qcfg(qos_box_dev, false);
	if (ret) {
		ret = -EINVAL;
		goto out;
	}

	/* rpm_active */
	qos_box_dev->is_rpm_active = true;

	/*
	 * Runtime pm initialization if this qos_box is within a genpd domain
	 *
	 * Call pm_runtime_enable() after all HW settings that need power domain ON are completed,
	 * then expose hooks to outside world such as mem_qos framework and debugfs
	 */
	if (dev->pm_domain) {
		pm_runtime_use_autosuspend(dev);
		pm_runtime_set_autosuspend_delay(dev, GOOGLE_QOS_BOX_AUTOSUSPEND_DELAY_MS);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	/* Register itself to mem_qos framework */
	ret = google_qos_box_dev_register(qos_box_dev);
	if (ret) {
		dev_err(dev, "qos_box_dev register failed, ret = %d\n", ret);
		goto out_rpm_disable;
	}

	/* debugfs */

	INIT_WORK(&qos_box_dev->debugfs_work, qos_box_debugfs_work_handler);
	if (!schedule_work(&qos_box_dev->debugfs_work))
		dev_warn(dev, "Failed to schedule debugfs initialization work. Debugfs may not be available.\n");

	return 0;

out_rpm_disable:
	if (pm_runtime_enabled(dev)) {
		pm_runtime_dont_use_autosuspend(dev);
		pm_runtime_disable(dev);
	}

out:
	return ret;
}

static void google_qos_box_platform_remove(struct platform_device *pdev)
{
	struct qos_box_dev *qos_box_dev = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	cancel_work_sync(&qos_box_dev->debugfs_work);
	qos_box_remove_debugfs(qos_box_dev);

	google_qos_box_dev_unregister(qos_box_dev);

	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
}

struct qos_box_desc rdo_qos_box_desc = {
	.version = QOS_BOX_VERSION_RDO,
	.num_hw_vc = 5,
};

struct qos_box_desc lga_qos_box_desc = {
	.version = QOS_BOX_VERSION_LGA,
	.num_hw_vc = 7,
};

static const struct of_device_id google_qos_box_of_match_table[] = {
	{ .compatible = "google,qos-box", .data = &rdo_qos_box_desc },
	{ .compatible = "google,lga-qos-box", .data = &lga_qos_box_desc },
	{}
};
MODULE_DEVICE_TABLE(of, google_qos_box_of_match_table);

struct platform_driver google_qos_box_platform_driver = {
	.probe = google_qos_box_platform_probe,
	.remove = google_qos_box_platform_remove,
	.driver = {
		.name = "google-qos-box",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&google_qos_box_dev_pm_ops),
		.of_match_table = of_match_ptr(google_qos_box_of_match_table),
	},
};

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google QoS Box driver");
MODULE_LICENSE("GPL");
