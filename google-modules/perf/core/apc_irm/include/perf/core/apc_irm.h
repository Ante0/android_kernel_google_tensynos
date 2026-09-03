/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PERF_CORE_APC_IRM_H
#define _PERF_CORE_APC_IRM_H

#include <linux/list.h>
#include <linux/types.h>

/*
 * Usage Example:
 *
 * static void devm_irm_subclient_release(struct device *dev, void *res)
 * {
 *         remove_irm_subclient(res);
 * }
 *
 * // 1. Get a handle to your client and subclient (e.g., in your driver's probe function)
 * struct irm_client_t *client = get_irm_client("ispSet0");
 * if (IS_ERR(client))
 *         return PTR_ERR(client);
 *
 * // An alternative is to embed `struct irm_subclient_t` into your driver's existing
 * // structure.
 * struct irm_subclient_t *my_sc = devres_alloc(devm_irm_subclient_release, sizeof(*my_sc), GFP_KERNEL);
 * if (!my_sc)
 *         return -ENOMEM;
 *
 * int ret = add_irm_subclient(client, my_sc, "my_component");
 * if (ret) {
 *         devres_free(my_sc);
 *         return ret;
 * }
 * devres_add(dev, my_sc);
 *
 * // 2. When performance requirements change:
 * // Set bandwidth requirements (e.g., 500 MB/s avg, 1000 MB/s peak)
 * set_irm_subclient_average_read_gmc_bandwidth(my_sc, 500);
 * set_irm_subclient_peak_read_gmc_bandwidth(my_sc, 1000);
 *
 * // Set performance level requirements (optional)
 * set_irm_subclient_pf_req_gmc(my_sc, 5);
 *
 * // 3. Stage the vote (commits these values to the subclient's staged state)
 * stage_irm_subclient_vote(my_sc);
 *
 * // 4. Publish the vote (aggregates all subclients of 'ispSet0' and updates hardware)
 * int ret = publish_irm_vote(client);
 * if (ret)
 *         pr_err("Failed to publish IRM vote: %d\n", ret);
 */

struct irm_client_t;
struct dentry;

struct irm_vote_t {
	u32 avg_read_gmc_bw;
	u32 peak_read_gmc_bw;
	u32 rt_read_gmc_bw;
	u32 avg_write_gmc_bw;
	u32 peak_write_gmc_bw;
	u32 rt_write_gmc_bw;
	u8 pf_gmc;
	u8 pf_memss;
	u8 pf_int_ancestor;
	u8 pf_int_descendant;

#if IS_ENABLED(CONFIG_SOC_LGA)
	u32 avg_read_gslc_bw;
	u32 peak_read_gslc_bw;
	u32 rt_read_gslc_bw;
	u32 avg_write_gslc_bw;
	u32 peak_write_gslc_bw;
	u32 rt_write_gslc_bw;
#endif
};

struct irm_subclient_t {
	const char *name;
	struct irm_client_t *irm_client;
	struct list_head list;

	struct irm_vote_t working_vote;
	struct irm_vote_t staged_vote;

	struct dentry *subclient_dir;
};

/*
 * Retrieve an irm_client_t pointer for the given name. Client names
 * match the ones listed in cpm/perf/irm/ .
 * Returns -ENOPROBE if the MBFS layer is not ready yet.
 */
struct irm_client_t *get_irm_client(const char *client_name);

/*
 * Register a subclient with the given client.
 * Subclient names may be chosen arbitrarily by clients, but must be unique
 * for the client in question.
 */
int add_irm_subclient(struct irm_client_t *client, struct irm_subclient_t *subclient,
		      const char *subclient_name);

/*
 * Unregister a subclient.
 */
void remove_irm_subclient(struct irm_subclient_t *subclient);

int get_irm_client_id(const struct irm_client_t *client);
bool is_irm_client_synchronous(const struct irm_client_t *client);

/*
 * IRM Vote Setters
 * These APIs modify the working copy of the subclient's vote.
 */

/* Read Bandwidth Setters */
void set_irm_subclient_average_read_gmc_bandwidth(struct irm_subclient_t *subclient,
						  u32 bandwidth_MBps);
void set_irm_subclient_peak_read_gmc_bandwidth(struct irm_subclient_t *subclient,
					       u32 bandwidth_MBps);
void set_irm_subclient_rt_read_gmc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps);

/* Write Bandwidth Setters */
void set_irm_subclient_average_write_gmc_bandwidth(struct irm_subclient_t *subclient,
						   u32 bandwidth_MBps);
void set_irm_subclient_peak_write_gmc_bandwidth(struct irm_subclient_t *subclient,
						u32 bandwidth_MBps);
void set_irm_subclient_rt_write_gmc_bandwidth(struct irm_subclient_t *subclient,
					      u32 bandwidth_MBps);

/*
 * Performance Level (PF) Setters
 * These APIs set the minimum performance level requirement for various fabrics.
 * The hardware expects these values to fit within specific bit widths.
 * Values exceeding the maximum will be clamped before writing to the hardware.
 */

/* Max value: 0x7 (7). Higher values will be clamped to 7. */
void set_irm_subclient_pf_req_intermediate_descendant_fab(struct irm_subclient_t *subclient,
							  u8 pf_level);

/* Max value: 0xF (15). Higher values will be clamped to 15. */
void set_irm_subclient_pf_req_intermediate_ancestor_fab(struct irm_subclient_t *subclient,
							u8 pf_level);

/* Max value: 0xF (15). Higher values will be clamped to 15. */
void set_irm_subclient_pf_req_memss(struct irm_subclient_t *subclient, u8 pf_level);

/* Max value: 0xF (15). Higher values will be clamped to 15. */
void set_irm_subclient_pf_req_gmc(struct irm_subclient_t *subclient, u8 pf_level);

#if IS_ENABLED(CONFIG_SOC_LGA)
/* Read Bandwidth Setters */
void set_irm_subclient_average_read_gslc_bandwidth(struct irm_subclient_t *subclient,
						   u32 bandwidth_MBps);
void set_irm_subclient_peak_read_gslc_bandwidth(struct irm_subclient_t *subclient,
						u32 bandwidth_MBps);
void set_irm_subclient_rt_read_gslc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps);

/* Write Bandwidth Setters */
void set_irm_subclient_average_write_gslc_bandwidth(struct irm_subclient_t *subclient,
						     u32 bandwidth_MBps);
void set_irm_subclient_peak_write_gslc_bandwidth(struct irm_subclient_t *subclient,
						u32 bandwidth_MBps);
void set_irm_subclient_rt_write_gslc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps);
#endif

/*
 * Clear all bandwidth and performance level requirements for the subclient
 * (reset to defaults). Note that this only updates the working copy;
 * stage_irm_subclient_vote() must still be called.
 */
void clear_irm_subclient_vote(struct irm_subclient_t *subclient);

/*
 * Internally stage vote for later publication (by copying the working_vote struct
 * to the staged_vote struct).
 * This allows to handle multiple subclient vote updates with a single IRM vote
 * publication, while keeping vote updates atomic / internally consistent.
 * This function never blocks (it only acquires a spinlock) and is safe to call
 * from atomic context.
 */
void stage_irm_subclient_vote(struct irm_subclient_t *subclient);

/*
 * Publish the aggregation of all staged subclient votes through IRM registers.
 * For synchronous clients, the call may block, and will only return once
 * the vote has fully propagated through the DVFS stack.
 * For asynchronous clients, the call will never block (and is safe to call
 * from atomic context), and may return before the vote has fully taken effect.
 *
 * The function panics if the synchronous wait times out.
 */
int publish_irm_vote(struct irm_client_t *client);

#endif /* _PERF_CORE_APC_IRM_H */
