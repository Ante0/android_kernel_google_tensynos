// SPDX-License-Identifier: GPL-2.0-only
/*
 * IRM debugfs support
 */
#include "google_irm.h"
#include "google_irm_debugfs.h"
#include "google_irm_reg_internal.h"

#include <dt-bindings/interconnect/google,icc.h>

#include <linux/debugfs.h>
#include <linux/regmap.h>

static int control_set(void *data, u64 val)
{
	int ret = 0;
	struct irm_dbg_client *dbg_client = (struct irm_dbg_client *)data;
	struct irm_dev *irm_dev = dbg_client->dbg->irm_dev;
	struct irm_client *client = get_irm_client(dbg_client);

	mutex_lock(&client->mutex);

	dbg_client->ctl = (u8)(val >= 1);

	if (!dbg_client->ctl)
		ret = irm_vote_commit(irm_dev, dbg_client->id);

	mutex_unlock(&client->mutex);

	return ret;
}

static int control_get(void *data, u64 *val)
{
	struct irm_dbg_client *dbg_client = (struct irm_dbg_client *)data;
	struct irm_client *client = get_irm_client(dbg_client);

	mutex_lock(&client->mutex);

	*val = (u64)dbg_client->ctl;

	mutex_unlock(&client->mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(control, control_get, control_set, "%llu");

#define IRM_DEBUG_PRINT_READ		0
#define IRM_DEBUG_PRINT_WRITE		1

static inline void irm_summary_print_metrics(struct seq_file *buf)
{
	seq_puts(buf, "Metrics:\n");
	seq_puts(buf, "  read_avg: MBps\n");
	seq_puts(buf, "  read_peak: MBps\n");
	seq_puts(buf, "  write_avg: MBps\n");
	seq_puts(buf, "  write_peak: MBps\n");
	seq_puts(buf, "  latency: nanosecond\n");
}

static inline void irm_summary_print_avg_bw(struct seq_file *buf, struct irm_dev *irm_dev,
					    u32 base, u8 is_read)
{
	u32 offset, total_avg_bw;

	offset = (is_read == IRM_DEBUG_PRINT_READ) ? DVFS_REQ_RD_BW_AVG_GMC :
						     DVFS_REQ_WR_BW_AVG_GMC;
	regmap_read(irm_dev->regmap, base + offset, &total_avg_bw);

	seq_printf(buf, "    %s_avg: %u \n",
		is_read == IRM_DEBUG_PRINT_READ ? "read" : "write", total_avg_bw);
}

static inline void irm_summary_print_peak_bw(struct seq_file *buf, struct irm_dev *irm_dev,
					     u32 base, u8 is_read)
{
	u32 offset, val;

	offset = (is_read == IRM_DEBUG_PRINT_READ) ? DVFS_REQ_RD_BW_PEAK_GMC :
						     DVFS_REQ_WR_BW_PEAK_GMC;
	regmap_read(irm_dev->regmap, base + offset, &val);

	seq_printf(buf, "    %s_peak: %u\n",
		   is_read == IRM_DEBUG_PRINT_READ ? "read" : "write",
		   val);
}

static inline void irm_summary_print_latency(struct seq_file *buf, struct irm_dev *irm_dev,
					     u32 base)
{
	u32 val;

	regmap_read(irm_dev->regmap, base + DVFS_REQ_LATENCY_GMC, &val);

	seq_printf(buf, "    latency: %u\n", val);
}

static inline void irm_summary_print_rt_bw(struct seq_file *buf, struct irm_dev *irm_dev,
					    u32 base, u8 is_read)
{
	u32 offset, rt_bw;

	offset = (is_read == IRM_DEBUG_PRINT_READ) ? DVFS_REQ_RD_BW_RT_GMC :
						     DVFS_REQ_WR_BW_RT_GMC;
	regmap_read(irm_dev->regmap, base + offset, &rt_bw);

	seq_printf(buf, "    %s_rt_bw: %u \n",
		is_read == IRM_DEBUG_PRINT_READ ? "read" : "write", rt_bw);
}


/*
 * [15]: valid
 * [14:12] intermediate fabric 2
 * [11: 8] intermediate fabric 1
 * [ 7: 4] MEMSS
 * [ 3: 0] GMC
 */
#define NUM_MIN_FREQ_CLAMP		4
#define MIN_FREQ_CLAMP_VALID_MASK	BIT(15)

static u32 min_freq_clamp_mask[NUM_MIN_FREQ_CLAMP] = {
	GENMASK(3, 0),
	GENMASK(7, 4),
	GENMASK(11, 8),
	GENMASK(14, 12)
};

static inline void irm_summary_print_min_freq_clamp(struct seq_file *buf, struct irm_dev *irm_dev,
						    struct irm_client *client)
{
	u32 reg_val;
	u32 val;
	u32 idx;

	/*
	 * DVFS_REQ_MIN_CLAMP_GMC represents min_freq clamp levels for a client, it's written by
	 * other SW instances in the system such as GPU FW. The summary debugfs knob shows the
	 * corresponding min_freq clamp levels for debugging purposes.
	 */
	regmap_read(irm_dev->regmap,
		    client->base + GMC_OFFSET + DVFS_REQ_MIN_CLAMP_GMC,
		    &reg_val);

	for (idx = 0; idx < NUM_MIN_FREQ_CLAMP; idx++) {
		if (idx >= client->num_fabric)
			break;

		val = (reg_val & min_freq_clamp_mask[idx]) >> (4 * idx);

		if (reg_val & MIN_FREQ_CLAMP_VALID_MASK)
			seq_printf(buf, "    %s: %u\n", client->fabric_name_arr[idx], val);
		else
			seq_printf(buf, "    %s: no valid vote\n", client->fabric_name_arr[idx]);
	}
}

static int irm_summary_show(struct seq_file *buf, void *d)
{
	struct irm_dbg *dbg = (struct irm_dbg *)buf->private;
	struct irm_dev *irm_dev = dbg->irm_dev;
	struct irm_dbg_client *dbg_client;
	struct irm_client *client;
	u32 base;
	size_t i;

	irm_summary_print_metrics(buf);

	for (i = 0; i < dbg->num_client; i++) {
		dbg_client = &dbg->client[i];
		client = get_irm_client(dbg_client);

		base = client->base;

		const char *type_str = client->sync_type & GOOGLE_ICC_UPDATE_SYNC ?
					"sync" : "async";

		seq_printf(buf, "%s: %s\n", dbg_client->name, type_str);
		seq_puts(buf, "  GMC vote:\n");

		irm_summary_print_avg_bw(buf, irm_dev, base, IRM_DEBUG_PRINT_READ);
		irm_summary_print_peak_bw(buf, irm_dev, base, IRM_DEBUG_PRINT_READ);
		irm_summary_print_rt_bw(buf, irm_dev, base, IRM_DEBUG_PRINT_READ);
		irm_summary_print_avg_bw(buf, irm_dev, base, IRM_DEBUG_PRINT_WRITE);
		irm_summary_print_peak_bw(buf, irm_dev, base, IRM_DEBUG_PRINT_WRITE);
		irm_summary_print_rt_bw(buf, irm_dev, base, IRM_DEBUG_PRINT_WRITE);
		irm_summary_print_latency(buf, irm_dev, base);

#if IS_ENABLED(CONFIG_SOC_LGA)
		seq_puts(buf, "  GSLC vote:\n");

		irm_summary_print_avg_bw(buf, irm_dev, base + GSLC_OFFSET, IRM_DEBUG_PRINT_READ);
		irm_summary_print_peak_bw(buf, irm_dev, base + GSLC_OFFSET, IRM_DEBUG_PRINT_READ);
		irm_summary_print_avg_bw(buf, irm_dev, base + GSLC_OFFSET, IRM_DEBUG_PRINT_WRITE);
		irm_summary_print_peak_bw(buf, irm_dev, base + GSLC_OFFSET, IRM_DEBUG_PRINT_WRITE);
		irm_summary_print_latency(buf, irm_dev, base + GSLC_OFFSET);
#endif
		seq_puts(buf, "  Min_Freq_Clamps:\n");

		irm_summary_print_min_freq_clamp(buf, irm_dev, client);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(irm_summary);

DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_rd_bw_avg_gmc, DVFS_REQ_RD_BW_AVG_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_wr_bw_avg_gmc, DVFS_REQ_WR_BW_AVG_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_rd_bw_rt_gmc, DVFS_REQ_RD_BW_RT_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_wr_bw_rt_gmc, DVFS_REQ_WR_BW_RT_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_rd_bw_peak_gmc, DVFS_REQ_RD_BW_PEAK_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_wr_bw_peak_gmc, DVFS_REQ_WR_BW_PEAK_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_latency_gmc, DVFS_REQ_LATENCY_GMC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_min_clamp_gmc, DVFS_REQ_MIN_CLAMP_GMC);
#if IS_ENABLED(CONFIG_SOC_LGA)
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_rd_bw_avg_gslc, DVFS_REQ_RD_BW_AVG_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_wr_bw_avg_gslc, DVFS_REQ_WR_BW_AVG_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_rd_bw_rt_gslc, DVFS_REQ_RD_BW_RT_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_wr_bw_rt_gslc, DVFS_REQ_WR_BW_RT_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_rd_bw_peak_gslc, DVFS_REQ_RD_BW_PEAK_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_wr_bw_peak_gslc, DVFS_REQ_WR_BW_PEAK_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_latency_gslc, DVFS_REQ_LATENCY_GSLC);
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_ltv_gslc, DVFS_REQ_LTV_GSLC);
#endif
DEFINE_IRM_DEBUGFS_ATTRIBUTE(dvfs_req_trig, DVFS_REQ_TRIG);

static const struct debugfs_reg32 __dump_regs[] = {
	dump_register(DVFS_REQ_RD_BW_AVG_GMC),
	dump_register(DVFS_REQ_WR_BW_AVG_GMC),
	dump_register(DVFS_REQ_RD_BW_RT_GMC),
	dump_register(DVFS_REQ_WR_BW_RT_GMC),
	dump_register(DVFS_REQ_RD_BW_PEAK_GMC),
	dump_register(DVFS_REQ_WR_BW_PEAK_GMC),
	dump_register(DVFS_REQ_LATENCY_GMC),
	dump_register(DVFS_REQ_MIN_CLAMP_GMC),
#if IS_ENABLED(CONFIG_SOC_LGA)
	dump_register(DVFS_REQ_RD_BW_AVG_GSLC),
	dump_register(DVFS_REQ_WR_BW_AVG_GSLC),
	dump_register(DVFS_REQ_RD_BW_RT_GSLC),
	dump_register(DVFS_REQ_WR_BW_RT_GSLC),
	dump_register(DVFS_REQ_RD_BW_PEAK_GSLC),
	dump_register(DVFS_REQ_WR_BW_PEAK_GSLC),
	dump_register(DVFS_REQ_LATENCY_GSLC),
	dump_register(DVFS_REQ_LTV_GSLC),
#endif
	dump_register(DVFS_REQ_TRIG)
};

int irm_create_debugfs(struct irm_dev *irm_dev, int num_client, const char **client_name)
{
	struct irm_dbg *dbg = irm_dev->dbg;
	struct device *dev = irm_dev->dev;
	struct irm_dbg_client *dbg_client;
	struct irm_client *client;
	int idx;

	dbg->num_client = num_client;
	dbg->client = devm_kzalloc(dev, sizeof(*dbg->client) * num_client, GFP_KERNEL);
	if (!dbg->client)
		return -ENOMEM;

	dbg->base_dir = debugfs_create_dir("irm", NULL);
	dbg->irm_dev = irm_dev;

	debugfs_create_file("summary", 0400, dbg->base_dir, dbg, &irm_summary_fops);

	for (idx = 0; idx < num_client; idx++) {
		dbg_client = &dbg->client[idx];

		dbg_client->dir = debugfs_create_dir(client_name[idx], dbg->base_dir);
		dbg_client->dbg = dbg;
		dbg_client->id   = idx;
		dbg_client->name = client_name[idx];

		dbg_client->regset = devm_kzalloc(dev, sizeof(*dbg_client->regset), GFP_KERNEL);
		if (!dbg_client->regset)
			return -ENOMEM;

		dbg_client->regset->regs = __dump_regs;
		dbg_client->regset->nregs = ARRAY_SIZE(__dump_regs);
		client = get_irm_client(dbg_client);
		dbg_client->regset->base = irm_dev->base_addr + client->base;

		debugfs_create_regset32("regdump", 0444, dbg_client->dir, dbg_client->regset);

		debugfs_create_file("control", 0600, dbg_client->dir, dbg_client, &control);
		debugfs_create_file("dvfs_req_rd_bw_avg_gmc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_rd_bw_avg_gmc);
		debugfs_create_file("dvfs_req_wr_bw_avg_gmc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_wr_bw_avg_gmc);
		debugfs_create_file("dvfs_req_rd_bw_rt_gmc", 0600,
				    dbg_client->dir, dbg_client,
				    &dvfs_req_rd_bw_rt_gmc);
		debugfs_create_file("dvfs_req_wr_bw_rt_gmc", 0600,
				    dbg_client->dir, dbg_client,
				    &dvfs_req_wr_bw_rt_gmc);
		debugfs_create_file("dvfs_req_rd_bw_peak_gmc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_rd_bw_peak_gmc);
		debugfs_create_file("dvfs_req_wr_bw_peak_gmc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_wr_bw_peak_gmc);
		debugfs_create_file("dvfs_req_latency_gmc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_latency_gmc);
		debugfs_create_file("dvfs_req_min_clamp_gmc", 0600,
				    dbg_client->dir, dbg_client,
				    &dvfs_req_min_clamp_gmc);
#if IS_ENABLED(CONFIG_SOC_LGA)
		debugfs_create_file("dvfs_req_rd_bw_avg_gslc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_rd_bw_avg_gslc);
		debugfs_create_file("dvfs_req_wr_bw_avg_gslc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_wr_bw_avg_gslc);
		debugfs_create_file("dvfs_req_rd_bw_rt_gslc", 0600,
				    dbg_client->dir, dbg_client,
				    &dvfs_req_rd_bw_rt_gslc);
		debugfs_create_file("dvfs_req_wr_bw_rt_gslc", 0600,
				    dbg_client->dir, dbg_client,
				    &dvfs_req_wr_bw_rt_gslc);
		debugfs_create_file("dvfs_req_rd_bw_peak_gslc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_rd_bw_peak_gslc);
		debugfs_create_file("dvfs_req_wr_bw_peak_gslc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_wr_bw_peak_gslc);
		debugfs_create_file("dvfs_req_latency_gslc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_latency_gslc);
		debugfs_create_file("dvfs_req_ltv_gslc", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_ltv_gslc);
#endif
		debugfs_create_file("dvfs_req_trig", 0600,
				    dbg_client->dir, dbg_client, &dvfs_req_trig);
	}

	return 0;
}

void irm_remove_debugfs(struct irm_dev *irm_dev)
{
	debugfs_remove_recursive(irm_dev->dbg->base_dir);
}
