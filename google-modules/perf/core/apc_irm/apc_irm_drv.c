// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <mailbox/protocols/mba/apc/common/service_ids.h>
#include <perf/mbfs.h>
#include <soc/google/goog_mba_cpm_iface.h>

#include "apc_irm_debug.h"
#include "apc_irm_plat.h"
#include "include/perf/core/apc_irm.h"

#define CREATE_TRACE_POINTS
#include "apc_irm_trace.h"

#define CPM_TASK_TIMEOUT_MS 10000

#define MAX_APC_IRM_CLIENT_ID 32
#define CPM_IRM_PATH "cpm/perf/irm"
#define IRM_PATH_MAX 128

struct irm_client_t {
	const char *name;
	int id;
	bool synchronous_via_apc;
	/* Offset of the first register of the client from irm_base */
	u32 base_reg_offset;

	spinlock_t lock; /* Protects list, staged_vote, published_vote */

	/*
	 * Serializes vote updates for synchronous clients.
	 *
	 * Vote updates on synchronous clients wait for confirmation from the FW that the vote has
	 * been fully propagated. No vote update for the same client is allowed while waiting for
	 * such a confirmation. As this may be slow (millisecond-scale), a mutex is used for
	 * synchronizations to allow calling tasks to sleep while waiting for the FW's response.
	 */
	struct mutex trigger_mutex;
	struct completion cpm_done;

	struct list_head subclient_list;
	struct irm_vote_t published_vote;
	struct dentry *client_dir;
};

static struct dentry *irm_debugfs_root;
static void __iomem *irm_base;
static struct irm_client_t *clients[MAX_APC_IRM_CLIENT_ID];
static DEFINE_MUTEX(clients_lock);
static struct cpm_iface_client *cpm_client;
static bool sync_clients_supported = true;
static bool irm_ready;

/*
 * Initializes the vote with default values.
 *
 * Frequency clamp fields are initialized to max, as they are aggregated
 * by taking the minimum. All other fields (like bandwidths, which are
 * aggregated by summation) are implicitly zero-initialized by the compiler.
 */
static void irm_init_vote(struct irm_vote_t *vote)
{
	*vote = (struct irm_vote_t){
		.pf_gmc = CPM_IRM_FREQ_CLAMP_GMC_MASK,
		.pf_memss = CPM_IRM_FREQ_CLAMP_MEMSS_MASK,
		.pf_int_ancestor = CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_MASK,
		.pf_int_descendant = CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_MASK,
	};
}

/*
 * Mailbox callback for synchronous updates
 */
static void irm_mbox_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct cpm_iface_payload *cpm_msg = msg;
	u32 *payload = cpm_msg->payload;
	// Index 1 contains the mask of completed synchronous client updates
	unsigned long client_mask = payload[1];
	unsigned long i;

	for_each_set_bit(i, &client_mask, MAX_APC_IRM_CLIENT_ID) {
		if (clients[i])
			complete(&clients[i]->cpm_done);
	}
}

static int irm_vote_show(struct seq_file *s, void *v)
{
	struct irm_vote_t *vote = s->private;

	seq_printf(s, "avg_read_gmc_bw: %u\n", vote->avg_read_gmc_bw);
	seq_printf(s, "peak_read_gmc_bw: %u\n", vote->peak_read_gmc_bw);
	seq_printf(s, "rt_read_gmc_bw: %u\n", vote->rt_read_gmc_bw);
	seq_printf(s, "avg_write_gmc_bw: %u\n", vote->avg_write_gmc_bw);
	seq_printf(s, "peak_write_gmc_bw: %u\n", vote->peak_write_gmc_bw);
	seq_printf(s, "rt_write_gmc_bw: %u\n", vote->rt_write_gmc_bw);
	seq_printf(s, "pf_gmc: 0x%x\n", vote->pf_gmc);
	seq_printf(s, "pf_memss: 0x%x\n", vote->pf_memss);
	seq_printf(s, "pf_int_ancestor: 0x%x\n", vote->pf_int_ancestor);
	seq_printf(s, "pf_int_descendant: 0x%x\n", vote->pf_int_descendant);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(irm_vote);

struct irm_client_t *get_irm_client(const char *client_name)
{
	struct irm_client_t *client = NULL;
	char path[IRM_PATH_MAX];
	union mbfs_client_handle handle;
	union val64 val;
	int id, ret;
	int i;
	u32 base_reg_offset;
	bool synchronous_via_apc;

	if (!irm_ready)
		return ERR_PTR(-EPROBE_DEFER);

	mutex_lock(&clients_lock);

	// Check if already loaded
	for (i = 0; i < MAX_APC_IRM_CLIENT_ID; i++) {
		if (clients[i] && strcmp(clients[i]->name, client_name) == 0) {
			client = clients[i];
			goto out;
		}
	}

	// Not loaded, try to load from mbfs
	ret = snprintf(path, sizeof(path), "%s/%s", CPM_IRM_PATH, client_name);
	if (ret >= sizeof(path)) {
		pr_err("IRM: path too long for client %s\n", client_name);
		goto out;
	}

	ret = mbfs_get_handle(path, &handle);
	if (ret != MBFS_OK)
		goto out;

	// Read ID
	ret = mbfs_read_child_by_name(handle, "index", &val);
	if (ret != MBFS_OK) {
		pr_err("IRM: Failed to read index for client %s\n", client_name);
		goto out;
	}
	id = (int)val.number;

	if (id < 0 || id >= MAX_APC_IRM_CLIENT_ID) {
		pr_err("IRM: Invalid id %d for client %s\n", id, client_name);
		goto out;
	}

	if (clients[id]) {
		// ID collision or race?
		client = clients[id];
		goto out;
	}

	ret = mbfs_read_child_by_name(handle, "baseRegOffset", &val);
	if (ret != MBFS_OK) {
		pr_err("IRM: Failed to read baseRegOffset for client %s\n", client_name);
		goto out;
	}
	base_reg_offset = (u32)val.number;

	// Read sync/async type
	ret = mbfs_read_child_by_name(handle, "apcSync", &val);
	if (ret != MBFS_OK) {
		pr_err("IRM: Failed to read apcSync for client %s\n", client_name);
		goto out;
	}
	synchronous_via_apc = (u32)val.number;

	if (synchronous_via_apc && !sync_clients_supported) {
		pr_err("IRM: Synchronous client %s not supported\n", client_name);
		client = ERR_PTR(-EOPNOTSUPP);
		goto out;
	}

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		client = ERR_PTR(-ENOMEM);
		goto out;
	}

	client->name = kstrdup(client_name, GFP_KERNEL);
	if (!client->name) {
		kfree(client);
		client = ERR_PTR(-ENOMEM);
		goto out;
	}

	client->id = id;
	client->base_reg_offset = base_reg_offset;
	client->synchronous_via_apc = synchronous_via_apc;

	spin_lock_init(&client->lock);
	mutex_init(&client->trigger_mutex);
	init_completion(&client->cpm_done);
	INIT_LIST_HEAD(&client->subclient_list);

	client->client_dir = debugfs_create_dir(client->name, irm_debugfs_root);
	debugfs_create_file("published_vote", 0444, client->client_dir, &client->published_vote,
			    &irm_vote_fops);

	clients[id] = client;

out:
	mutex_unlock(&clients_lock);
	if (!client && ret != MBFS_OK && ret != MBFS_PROBE_DEFER)
		return ERR_PTR(-ENODEV);
	if (!client && ret == MBFS_PROBE_DEFER)
		return ERR_PTR(-EPROBE_DEFER);

	return client;
}
EXPORT_SYMBOL_GPL(get_irm_client);

int add_irm_subclient(struct irm_client_t *client, struct irm_subclient_t *subclient,
		      const char *subclient_name)
{
	struct irm_subclient_t *pos;
	unsigned long flags;
	char *name;

	if (!client || !subclient || !subclient_name)
		return -EINVAL;

	name = kstrdup(subclient_name, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	spin_lock_irqsave(&client->lock, flags);
	list_for_each_entry(pos, &client->subclient_list, list) {
		if (strcmp(pos->name, subclient_name) == 0) {
			spin_unlock_irqrestore(&client->lock, flags);
			kfree(name);
			return -EEXIST;
		}
	}

	subclient->name = name;
	subclient->irm_client = client;
	irm_init_vote(&subclient->working_vote);
	irm_init_vote(&subclient->staged_vote);

	list_add(&subclient->list, &client->subclient_list);
	spin_unlock_irqrestore(&client->lock, flags);

	subclient->subclient_dir = debugfs_create_dir(subclient->name, client->client_dir);
	debugfs_create_file("staged_vote", 0444, subclient->subclient_dir, &subclient->staged_vote,
			    &irm_vote_fops);

	return 0;
}
EXPORT_SYMBOL_GPL(add_irm_subclient);

void remove_irm_subclient(struct irm_subclient_t *subclient)
{
	struct irm_client_t *client;
	unsigned long flags;

	if (!subclient)
		return;

	client = subclient->irm_client;
	if (!client)
		return;

	spin_lock_irqsave(&client->lock, flags);
	list_del(&subclient->list);
	spin_unlock_irqrestore(&client->lock, flags);

	debugfs_remove_recursive(subclient->subclient_dir);
	kfree(subclient->name);
}
EXPORT_SYMBOL_GPL(remove_irm_subclient);

int get_irm_client_id(const struct irm_client_t *client)
{
	return client ? client->id : -EINVAL;
}
EXPORT_SYMBOL_GPL(get_irm_client_id);

bool is_irm_client_synchronous(const struct irm_client_t *client)
{
	return client ? client->synchronous_via_apc : false;
}
EXPORT_SYMBOL_GPL(is_irm_client_synchronous);

void get_irm_client_published_vote(struct irm_client_t *client, struct irm_vote_t *vote)
{
	unsigned long flags;

	if (!client || !vote)
		return;

	spin_lock_irqsave(&client->lock, flags);
	*vote = client->published_vote;
	spin_unlock_irqrestore(&client->lock, flags);
}
EXPORT_SYMBOL_GPL(get_irm_client_published_vote);

u32 get_irm_register_value_from_mbfs(struct irm_client_t *client, const char *filename)
{
	char path[IRM_PATH_MAX];
	union mbfs_client_handle handle;
	union val64 val;
	int ret;

	if (!client || !filename)
		return 0;

	ret = snprintf(path, sizeof(path), "%s/%s", CPM_IRM_PATH, client->name);
	if (ret >= sizeof(path)) {
		pr_err("IRM: path too long for client %s\n", client->name);
		return 0;
	}

	ret = mbfs_get_handle(path, &handle);
	if (ret != MBFS_OK) {
		pr_err("%s: failed to get mbfs handle for %s\n", __func__, path);
		return 0;
	}

	ret = mbfs_read_child_by_name(handle, filename, &val);
	if (ret != MBFS_OK) {
		pr_err("%s: failed to read %s\n", __func__, filename);
		return 0;
	}

	return (u32)val.number;
}
EXPORT_SYMBOL_GPL(get_irm_register_value_from_mbfs);

/* Bandwidth setters */

void set_irm_subclient_average_read_gmc_bandwidth(struct irm_subclient_t *subclient,
						  u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.avg_read_gmc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_average_read_gmc_bandwidth);

void set_irm_subclient_peak_read_gmc_bandwidth(struct irm_subclient_t *subclient,
					       u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.peak_read_gmc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_peak_read_gmc_bandwidth);

void set_irm_subclient_rt_read_gmc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.rt_read_gmc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_rt_read_gmc_bandwidth);

void set_irm_subclient_average_write_gmc_bandwidth(struct irm_subclient_t *subclient,
						   u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.avg_write_gmc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_average_write_gmc_bandwidth);

void set_irm_subclient_peak_write_gmc_bandwidth(struct irm_subclient_t *subclient,
						u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.peak_write_gmc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_peak_write_gmc_bandwidth);

void set_irm_subclient_rt_write_gmc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.rt_write_gmc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_rt_write_gmc_bandwidth);

void set_irm_subclient_pf_req_intermediate_ancestor_fab(struct irm_subclient_t *subclient,
							u8 pf_level)
{
	if (subclient)
		subclient->working_vote.pf_int_ancestor = pf_level;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_pf_req_intermediate_ancestor_fab);

void set_irm_subclient_pf_req_intermediate_descendant_fab(struct irm_subclient_t *subclient,
							  u8 pf_level)
{
	if (subclient)
		subclient->working_vote.pf_int_descendant = pf_level;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_pf_req_intermediate_descendant_fab);

void set_irm_subclient_pf_req_memss(struct irm_subclient_t *subclient, u8 pf_level)
{
	if (subclient)
		subclient->working_vote.pf_memss = pf_level;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_pf_req_memss);

void set_irm_subclient_pf_req_gmc(struct irm_subclient_t *subclient, u8 pf_level)
{
	if (subclient)
		subclient->working_vote.pf_gmc = pf_level;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_pf_req_gmc);

void clear_irm_subclient_vote(struct irm_subclient_t *subclient)
{
	if (subclient)
		irm_init_vote(&subclient->working_vote);
}
EXPORT_SYMBOL_GPL(clear_irm_subclient_vote);

void stage_irm_subclient_vote(struct irm_subclient_t *subclient)
{
	struct irm_client_t *client;
	unsigned long flags;

	if (!subclient)
		return;

	client = subclient->irm_client;

	spin_lock_irqsave(&client->lock, flags);
	subclient->staged_vote = subclient->working_vote;
	spin_unlock_irqrestore(&client->lock, flags);
}
EXPORT_SYMBOL_GPL(stage_irm_subclient_vote);

static void trace_irm_vote(struct irm_client_t *client, struct irm_vote_t *vote)
{
	/*
	 * client->id can be replaced with a constant u8 value to put the votes of the
	 * same kind from different clients on the same track in Perfetto.
	 */
	trace_published_vote_bw_gmc(client->name, client->id, vote);
	trace_published_vote_max_opp(client->name, client->id, vote);
#if IS_ENABLED(CONFIG_SOC_LGA)
	trace_published_vote_bw_gslc(client->name, client->id, vote);
#endif
}

static void aggregate_votes(struct irm_client_t *client, struct irm_vote_t *total)
{
	struct irm_subclient_t *sc;

	memset(total, 0, sizeof(*total));
	irm_init_vote(total);

	list_for_each_entry(sc, &client->subclient_list, list) {
		total->avg_read_gmc_bw += sc->staged_vote.avg_read_gmc_bw;
		total->peak_read_gmc_bw += sc->staged_vote.peak_read_gmc_bw;
		total->rt_read_gmc_bw += sc->staged_vote.rt_read_gmc_bw;
		total->avg_write_gmc_bw += sc->staged_vote.avg_write_gmc_bw;
		total->peak_write_gmc_bw += sc->staged_vote.peak_write_gmc_bw;
		total->rt_write_gmc_bw += sc->staged_vote.rt_write_gmc_bw;

		total->pf_gmc = min(total->pf_gmc, sc->staged_vote.pf_gmc);
		total->pf_memss = min(total->pf_memss, sc->staged_vote.pf_memss);
		total->pf_int_ancestor =
			min(total->pf_int_ancestor, sc->staged_vote.pf_int_ancestor);
		total->pf_int_descendant =
			min(total->pf_int_descendant, sc->staged_vote.pf_int_descendant);

#if IS_ENABLED(CONFIG_SOC_LGA)
		total->avg_read_gslc_bw += sc->staged_vote.avg_read_gslc_bw;
		total->peak_read_gslc_bw += sc->staged_vote.peak_read_gslc_bw;
		total->rt_read_gslc_bw += sc->staged_vote.rt_read_gslc_bw;
		total->avg_write_gslc_bw += sc->staged_vote.avg_write_gslc_bw;
		total->peak_write_gslc_bw += sc->staged_vote.peak_write_gslc_bw;
		total->rt_write_gslc_bw += sc->staged_vote.rt_write_gslc_bw;
#endif
	}
}

int publish_irm_vote(struct irm_client_t *client)
{
	struct irm_vote_t total_vote;
	unsigned long flags;
	int ret = 0;

	if (!client || !irm_base)
		return -EINVAL;

	if (client->synchronous_via_apc) {
		mutex_lock(&client->trigger_mutex);

		// Snapshot votes
		spin_lock_irqsave(&client->lock, flags);
		aggregate_votes(client, &total_vote);
		client->published_vote = total_vote;
		spin_unlock_irqrestore(&client->lock, flags);

		apply_vote(irm_base + client->base_reg_offset, client->synchronous_via_apc,
			   &total_vote);

		trace_irm_vote(client, &total_vote);

		// Wait for ACK
		long time_left = wait_for_completion_timeout(&client->cpm_done,
							     msecs_to_jiffies(CPM_TASK_TIMEOUT_MS));
		if (time_left == 0) {
			panic("%s: Timed out waiting for CPM ack for client %s\n", __func__,
			      client->name);
		}

		mutex_unlock(&client->trigger_mutex);
	} else {
		spin_lock_irqsave(&client->lock, flags);
		aggregate_votes(client, &total_vote);
		client->published_vote = total_vote;
		apply_vote(irm_base + client->base_reg_offset, client->synchronous_via_apc,
			   &total_vote);
		trace_irm_vote(client, &total_vote);
		spin_unlock_irqrestore(&client->lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(publish_irm_vote);

static int apc_irm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	union mbfs_client_handle handle;
	union val64 val;
	phys_addr_t phys_addr;
	size_t size;
	int ret;

	ret = mbfs_get_handle(CPM_IRM_PATH, &handle);
	if (ret != MBFS_OK) {
		if (ret == MBFS_PROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(dev, "failed to get mbfs handle for %s: %d\n", CPM_IRM_PATH, ret);
		return -ENODEV;
	}

	ret = mbfs_read_child_by_name(handle, "socViewBarStart", &val);
	if (ret != MBFS_OK) {
		dev_err(dev, "failed to read socViewBarStart: %d\n", ret);
		return -ENODEV;
	}
	phys_addr = (phys_addr_t)val.number;

	ret = mbfs_read_child_by_name(handle, "rangeApcClients", &val);
	if (ret != MBFS_OK) {
		dev_err(dev, "failed to read rangeApcClients: %d\n", ret);
		return -ENODEV;
	}
	size = (size_t)val.number;

	irm_base = devm_ioremap(dev, phys_addr, size);
	if (!irm_base)
		return -ENOMEM;

	if (of_property_read_bool(dev->of_node, "google,disable-sync-clients"))
		sync_clients_supported = false;

	if (sync_clients_supported) {
		cpm_client = cpm_iface_request_client(dev, APC_COMMON_SERVICE_ID_MIPM,
						      irm_mbox_rx_callback, NULL);
		if (IS_ERR(cpm_client)) {
			int ret = PTR_ERR(cpm_client);

			if (ret == -EPROBE_DEFER)
				dev_dbg(dev, "cpm interface not ready. Try again later\n");
			else
				dev_err(dev, "failed to request cpm mailbox client err %d\n", ret);
			return ret;
		}
	}

	/*
	 * We intentionally do not check the return value here. Debugfs is
	 * optional, and the debugfs APIs are designed to safely handle
	 * error pointers (ERR_PTR) if creation fails.
	 */
	irm_debugfs_root = debugfs_create_dir("apc_irm", NULL);
	irm_ready = true;

	dev_info(dev, "APC IRM driver probed (sync_clients_supported=%d)\n",
		 sync_clients_supported);

	return 0;
}

static void apc_irm_remove(struct platform_device *pdev)
{
	irm_ready = false;
	debugfs_remove_recursive(irm_debugfs_root);
	if (cpm_client && !IS_ERR(cpm_client))
		cpm_iface_free_client(cpm_client);
}

static const struct of_device_id apc_irm_of_match[] = { { .compatible = "google,apc-irm" }, {} };
MODULE_DEVICE_TABLE(of, apc_irm_of_match);

static struct platform_driver apc_irm_driver = {
	.probe = apc_irm_probe,
	.remove = apc_irm_remove,
	.driver = {
		.name = "apc_irm",
		.of_match_table = apc_irm_of_match,
	},
};
module_platform_driver(apc_irm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google APC IRM Driver");
