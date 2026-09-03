// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP coresight remote.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kconfig.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <gcip/gcip-coresight-remote.h>

#if GCIP_CORESIGHT_REMOTE_ENABLED
#include <hwtracing/coresight/coresight-remote.h>
#include <hwtracing/coresight/coresight-remote-protocol.h>

/* Masks for remote and fw IDs. */
#define GCIP_REMOTE_ID_MASK GENMASK(7, 0)
#define GCIP_FW_ID_MASK GENMASK(15, 8)

/* Compatible strings for lookup */
static const char *const gcip_etm_compatible = "google,coresight-remote-etm";
static const char *const gcip_etf_compatible = "google,coresight-remote-tmc-etf";
static const char *const gcip_funnel_compatible = "google,coresight-remote-funnel";
static const char *const gcip_replicator_compatible = "google,coresight-remote-replicator";

/**
 * gcip_count_components() - Count devices of a specific trace component type matching a fw_id
 * @dev: The parent device.
 * @parent_np: The device node to search within.
 * @fw_id: The firmware ID to search for.
 * @compatible: The compatible string to match.
 *
 * Return: The number of components found matching the fw_id and compatible string.
 */
static int gcip_count_components(struct device *dev, struct device_node *parent_np, u8 fw_id,
				 const char *compatible)
{
	struct device_node *child_np;
	int count = 0;
	u32 remote_id_prop;

	for_each_child_of_node(parent_np, child_np)
		if (of_device_is_compatible(child_np, compatible) &&
		    !of_property_read_u32(child_np, "remote-id", &remote_id_prop) &&
		    FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop) == fw_id)
			count++;

	return count;
}

/**
 * gcip_discover_components_generic() - Discover and allocate state arrays for components.
 * @dev: The parent device.
 * @parent_np: The device node to search within.
 * @fw_id: The firmware ID to search for.
 * @compatible: The compatible string to match.
 * @elem_size: The size of the state structure for this component type.
 * @out_count: Pointer to store the number of components found.
 *
 * Return: A pointer to the allocated array of component state structures,
 *         or NULL if none were found, or an ERR_PTR() on failure.
 */
static void *gcip_discover_components_generic(struct device *dev,
					      struct device_node *parent_np, u8 fw_id,
					      const char *compatible, size_t elem_size,
					      u32 *out_count)
{
	int count, ret;
	void *array;
	unsigned long *id_bitmap;
	struct device_node *child_np;
	struct gcip_coresight_base_state *base;
	u32 remote_id_prop;
	u8 remote_id;

	count = gcip_count_components(dev, parent_np, fw_id, compatible);
	*out_count = count;
	if (count == 0)
		return NULL;

	array = kcalloc(count, elem_size, GFP_KERNEL);
	if (!array)
		return ERR_PTR(-ENOMEM);

	id_bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!id_bitmap) {
		kfree(array);
		return ERR_PTR(-ENOMEM);
	}

	for_each_child_of_node(parent_np, child_np) {
		if (!of_device_is_compatible(child_np, compatible))
			continue;

		ret = of_property_read_u32(child_np, "remote-id", &remote_id_prop);
		if (ret) {
			dev_err(dev, "Node %pOFn missing remote-id property: %d\n", child_np, ret);
			goto error;
		}

		if (FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop) != fw_id)
			continue;

		remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
		if (remote_id >= count) {
			dev_err(dev, "Node %pOFn remote-id %u out of bounds (max %u)\n", child_np,
				remote_id, count - 1);
			ret = -EINVAL;
			goto error;
		}

		if (test_and_set_bit(remote_id, id_bitmap)) {
			dev_err(dev, "Duplicate remote-id %u found at %pOFn\n", remote_id,
				child_np);
			ret = -EINVAL;
			goto error;
		}

		/*
		 * The base state struct is always the first member of our component state structs
		 * (gcip_etm_state, gcip_etf_state, gcip_funnel_state, gcip_replicator_state). The
		 * rest of the struct has been zero-initialized by kcalloc.
		 */
		base = (void *)((u8 *)array + remote_id * elem_size);
		base->remote_id = remote_id;
	}

	kfree(id_bitmap);
	return array;

error:
	/*
	 * Release the node reference taken by @for_each_child_of_node() in case of jumping out
	 * inbetween from the for loop.
	 */
	of_node_put(child_np);
	kfree(id_bitmap);
	kfree(array);
	return ERR_PTR(ret);
}

/**
 * gcip_discover_trace_components() - Populates gcip_trace_components from device tree.
 * @dev: The parent device.
 * @parent_np: The device node to search within (e.g., &sswrp_aurdsp).
 * @components: The gcip_trace_components struct to populate.
 * @fw_id: The firmware ID to search for.
 *
 * The array index for each component type corresponds to the 'remote-id' property in the Device
 * Tree. This function validates that remote-ids are unique and within the range
 * [0, num_components - 1].
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_discover_trace_components(struct device *dev, struct device_node *parent_np,
					  struct gcip_trace_components *components, u8 fw_id)
{
	void *ptr;
	int ret = 0;

	ptr = gcip_discover_components_generic(dev, parent_np, fw_id, gcip_etm_compatible,
					       sizeof(struct gcip_etm_state),
					       &components->num_etms);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	components->etms = ptr;

	ptr = gcip_discover_components_generic(dev, parent_np, fw_id, gcip_etf_compatible,
					       sizeof(struct gcip_etf_state),
					       &components->num_etfs);
	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		goto err_etms;
	}
	components->etfs = ptr;

	ptr = gcip_discover_components_generic(dev, parent_np, fw_id, gcip_funnel_compatible,
					       sizeof(struct gcip_funnel_state),
					       &components->num_funnels);
	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		goto err_etfs;
	}
	components->funnels = ptr;

	ptr = gcip_discover_components_generic(dev, parent_np, fw_id, gcip_replicator_compatible,
					       sizeof(struct gcip_replicator_state),
					       &components->num_replicators);
	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		goto err_funnels;
	}
	components->replicators = ptr;

	return 0;

err_funnels:
	kfree(components->funnels);
	components->funnels = NULL;
err_etfs:
	kfree(components->etfs);
	components->etfs = NULL;
err_etms:
	kfree(components->etms);
	components->etms = NULL;
	return ret;
}

/**
 * gcip_free_trace_components() - Frees the memory allocated for trace components.
 * @components: The gcip_trace_components struct containing pointers to free.
 */
static void gcip_free_trace_components(struct gcip_trace_components *components)
{
	kfree(components->etms);
	components->etms = NULL;
	components->num_etms = 0;

	kfree(components->etfs);
	components->etfs = NULL;
	components->num_etfs = 0;

	kfree(components->funnels);
	components->funnels = NULL;
	components->num_funnels = 0;

	kfree(components->replicators);
	components->replicators = NULL;
	components->num_replicators = 0;
}

/**
 * gcip_coresight_remote_send_cmd_raw() - Sends a coresight command directly to firmware.
 * @coresight_remote: Pointer to the gcip_coresight_remote structure.
 * @fw_id: Firmware ID to send the command to.
 * @cmds: Pointer to the coresight remote bulk commands structure.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_coresight_remote_send_cmd_raw(struct gcip_coresight_remote *coresight_remote,
					      u8 fw_id,
					      struct gcip_coresight_remote_bulk_cmds *cmds)
{
	int ret;
	enum gcip_status_code rsp;

	ret = coresight_remote->send_kci(coresight_remote->kci_data[fw_id], cmds, &rsp);
	if (ret) {
		dev_err(coresight_remote->dev, "failed to send coresight remote kci: %d\n", ret);
		return ret;
	}

	if (rsp != GCIP_STATUS_CODE_OK) {
		ret = gcip_status_code_convert_to_errno(rsp);
		dev_err(coresight_remote->dev, "coresight remote error status: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * gcip_coresight_remote_send_cmd_if_powered() - Sends a coresight command only if the device is
 * currently powered on.
 * @coresight_remote: Pointer to the gcip_coresight_remote structure.
 * @fw_id: Firmware ID to send the command to.
 * @cmds: Pointer to the coresight remote bulk commands structure.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_coresight_remote_send_cmd_if_powered(struct gcip_coresight_remote *coresight_remote,
						     u8 fw_id,
						     struct gcip_coresight_remote_bulk_cmds *cmds)
{
	int ret = 0;

	if (!gcip_pm_get_if_powered(coresight_remote->pm, false)) {
		ret = gcip_coresight_remote_send_cmd_raw(coresight_remote, fw_id, cmds);
		gcip_pm_put(coresight_remote->pm);
	}
	return ret;
}

/**
 * gcip_count_pending_enabled_trace_components() - Count the number of enabled trace components in
 *                                                 an array.
 * @array: Pointer to the array of component state structures.
 * @num_elements: Number of elements in the array.
 * @elem_size: Size of a single element in the array.
 *
 * Return: The number of components that are currently enabled.
 */
static int gcip_count_pending_enabled_trace_components(const u8 *array, int num_elements,
						       size_t elem_size)
{
	int i, count = 0;
	const struct gcip_coresight_base_state *base;

	for (i = 0; i < num_elements; i++) {
		base = (const struct gcip_coresight_base_state *)(array + i * elem_size);
		if (base->is_enabled)
			count++;
	}
	return count;
}

/**
 * gcip_get_pending_enabled_trace_components_count() - Get total enabled trace components.
 * @components: Pointer to the trace components structure.
 *
 * Calculates the total number of coresight remote components that are currently
 * enabled and require their state to be restored on the firmware side.
 *
 * Return: Total count of enabled components.
 */
static int gcip_get_pending_enabled_trace_components_count(struct gcip_trace_components *components)
{
	int count = 0;

	count += gcip_count_pending_enabled_trace_components(
		(const u8 *)components->etms, components->num_etms, sizeof(*components->etms));
	count += gcip_count_pending_enabled_trace_components(
		(const u8 *)components->etfs, components->num_etfs, sizeof(*components->etfs));
	count += gcip_count_pending_enabled_trace_components((const u8 *)components->funnels,
							     components->num_funnels,
							     sizeof(*components->funnels));
	count += gcip_count_pending_enabled_trace_components((const u8 *)components->replicators,
							     components->num_replicators,
							     sizeof(*components->replicators));

	return count;
}

/**
 * gcip_execute_coresight_op() - Executes a coresight operation.
 * @coresight_remote: Pointer to the gcip_coresight_remote structure.
 * @fw_id: Firmware ID to send the command to.
 * @remote_id: Remote ID of the component to execute the operation on.
 * @dev_type: Type of the component to execute the operation on.
 * @opcode: Opcode of the operation to execute.
 * @fw_op_param: Parameter for the operation.
 * @dev_name: Name of the component to execute the operation on.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_execute_coresight_op(struct gcip_coresight_remote *coresight_remote, u8 fw_id,
				     u8 remote_id, enum coresight_remote_type dev_type,
				     enum coresight_remote_fw_opcode opcode, u32 fw_op_param,
				     const char *dev_name)
{
	int ret;
	struct gcip_coresight_remote_bulk_cmds cmds;

	cmds.num_commands = 1;
	cmds.commands[0] =
		coresight_remote_encode_framework_op(dev_type, remote_id, opcode, fw_op_param);

	ret = gcip_coresight_remote_send_cmd_if_powered(coresight_remote, fw_id, &cmds);

	dev_info(coresight_remote->dev,
		 "gcip coresight: %s %s (fw_id:%u, remote_id:%u, param:%d) %s\n", dev_name,
		 opcode == CORESIGHT_REMOTE_FW_OP_ENABLE ? "enable" : "disable", fw_id, remote_id,
		 fw_op_param, ret ? "failed" : "success");

	return ret;
}

/**
 * gcip_etm_ops_enable() - Enable the ETM source component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @data: Additional data passed for enable operation.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_etm_ops_enable(void *ctx, u32 remote_id_prop, void *data)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_etm_state *etm;
	int ret;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_etms)
		return -EINVAL;

	etm = &remote_coresight->components[fw_id].etms[remote_id];
	if (etm->base.is_enabled)
		return 0;

	ret = gcip_execute_coresight_op(remote_coresight, fw_id, remote_id,
					CORESIGHT_REMOTE_DEV_ETM, CORESIGHT_REMOTE_FW_OP_ENABLE, 0,
					"etm");
	if (!ret)
		etm->base.is_enabled = true;
	return ret;
}

/**
 * gcip_etm_ops_disable() - Disable the ETM component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 */
static void gcip_etm_ops_disable(void *ctx, u32 remote_id_prop)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_etm_state *etm;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_etms)
		return;

	etm = &remote_coresight->components[fw_id].etms[remote_id];
	if (!etm->base.is_enabled)
		return;

	if (!gcip_execute_coresight_op(remote_coresight, fw_id, remote_id, CORESIGHT_REMOTE_DEV_ETM,
				       CORESIGHT_REMOTE_FW_OP_DISABLE, 0, "etm"))
		etm->base.is_enabled = false;
}

static const struct coresight_remote_source_ops gcip_etm_ops = {
	.enable = gcip_etm_ops_enable,
	.disable = gcip_etm_ops_disable,
};

/**
 * gcip_etf_ops_enable() - Enable the ETF sink/link component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @mode: Mode to enable the ETF in (e.g., sink or link).
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_etf_ops_enable(void *ctx, u32 remote_id_prop, int mode)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_etf_state *etf;
	int ret;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_etfs)
		return -EINVAL;

	etf = &remote_coresight->components[fw_id].etfs[remote_id];

	ret = gcip_execute_coresight_op(remote_coresight, fw_id, remote_id,
					CORESIGHT_REMOTE_DEV_TMC, CORESIGHT_REMOTE_FW_OP_ENABLE,
					mode, "etf");
	if (!ret) {
		etf->base.is_enabled = true;
		etf->mode = mode;
	}
	return ret;
}

/**
 * gcip_etf_ops_disable() - Disable the ETF sink/link component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @mode: Mode to disable the ETF in (e.g., sink or link).
 */
static void gcip_etf_ops_disable(void *ctx, u32 remote_id_prop, int mode)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_etf_state *etf;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_etfs)
		return;

	etf = &remote_coresight->components[fw_id].etfs[remote_id];

	if (!gcip_execute_coresight_op(remote_coresight, fw_id, remote_id, CORESIGHT_REMOTE_DEV_TMC,
				       CORESIGHT_REMOTE_FW_OP_DISABLE, mode, "etf")) {
		etf->base.is_enabled = false;
		etf->mode = 0;
	}
}

static const struct coresight_remote_link_ops gcip_etf_link_ops = {
	.enable = gcip_etf_ops_enable,
	.disable = gcip_etf_ops_disable,
};

/**
 * gcip_funnel_ops_enable() - Enable the funnel component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @inport: Inport to enable the funnel in.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_funnel_ops_enable(void *ctx, u32 remote_id_prop, int inport)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_funnel_state *funnel;
	int ret;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_funnels)
		return -EINVAL;

	funnel = &remote_coresight->components[fw_id].funnels[remote_id];

	ret = gcip_execute_coresight_op(remote_coresight, fw_id, remote_id,
					CORESIGHT_REMOTE_DEV_FUNNEL, CORESIGHT_REMOTE_FW_OP_ENABLE,
					inport, "funnel");
	if (!ret) {
		funnel->base.is_enabled = true;
		funnel->active_ports_mask |= BIT(inport);
	}

	return ret;
}

/**
 * gcip_funnel_ops_disable() - Disable the funnel component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @inport: Inport to disable the funnel in.
 */
static void gcip_funnel_ops_disable(void *ctx, u32 remote_id_prop, int inport)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_funnel_state *funnel;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_funnels)
		return;

	funnel = &remote_coresight->components[fw_id].funnels[remote_id];

	if (!gcip_execute_coresight_op(remote_coresight, fw_id, remote_id,
				       CORESIGHT_REMOTE_DEV_FUNNEL, CORESIGHT_REMOTE_FW_OP_DISABLE,
				       inport, "funnel")) {
		funnel->active_ports_mask &= ~BIT(inport);
		if (!funnel->active_ports_mask)
			funnel->base.is_enabled = false;
	}
}

static const struct coresight_remote_link_ops gcip_funnel_link_ops = {
	.enable = gcip_funnel_ops_enable,
	.disable = gcip_funnel_ops_disable,
};

/**
 * gcip_replicator_ops_enable() - Enable the replicator component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @active_child_ports_mask: Active child ports mask to enable the replicator in.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int gcip_replicator_ops_enable(void *ctx, u32 remote_id_prop, int active_child_ports_mask)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_replicator_state *replicator;
	int ret;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_replicators)
		return -EINVAL;

	replicator = &remote_coresight->components[fw_id].replicators[remote_id];

	ret = gcip_execute_coresight_op(remote_coresight, fw_id, remote_id,
					CORESIGHT_REMOTE_DEV_REPLICATOR,
					CORESIGHT_REMOTE_FW_OP_ENABLE, 0, "replicator");
	if (!ret) {
		replicator->base.is_enabled = true;
		replicator->active_child_ports_mask |= BIT(active_child_ports_mask);
	}
	return ret;
}

/**
 * gcip_replicator_ops_disable() - Disable the replicator component.
 * @ctx: Context pointer containing the remote coresight structure.
 * @remote_id_prop: Remote ID property containing firmware and remote IDs.
 * @active_child_ports_mask: Active child ports mask to disable the replicator in.
 */
static void gcip_replicator_ops_disable(void *ctx, u32 remote_id_prop, int active_child_ports_mask)
{
	struct gcip_coresight_remote *remote_coresight = ctx;
	u8 fw_id = FIELD_GET(GCIP_FW_ID_MASK, remote_id_prop);
	u8 remote_id = FIELD_GET(GCIP_REMOTE_ID_MASK, remote_id_prop);
	struct gcip_replicator_state *replicator;

	if (fw_id >= remote_coresight->num_fw_targets ||
	    remote_id >= remote_coresight->components[fw_id].num_replicators)
		return;

	replicator = &remote_coresight->components[fw_id].replicators[remote_id];

	if (!gcip_execute_coresight_op(remote_coresight, fw_id, remote_id,
				       CORESIGHT_REMOTE_DEV_REPLICATOR,
				       CORESIGHT_REMOTE_FW_OP_DISABLE, 0, "replicator")) {
		replicator->active_child_ports_mask &= ~BIT(active_child_ports_mask);
		if (!replicator->active_child_ports_mask)
			replicator->base.is_enabled = false;
	}
}

static const struct coresight_remote_link_ops gcip_replicator_link_ops = {
	.enable = gcip_replicator_ops_enable,
	.disable = gcip_replicator_ops_disable,
};

static const struct coresight_remote_ops gcip_coresight_remote_ops = {
	.etm_ops = &gcip_etm_ops,
	.etf_link_ops = &gcip_etf_link_ops,
	.funnel_ops = &gcip_funnel_link_ops,
	.replicator_ops = &gcip_replicator_link_ops,
};

struct gcip_coresight_remote *
gcip_coresight_remote_register(const struct gcip_coresight_remote_args *args)
{
	struct gcip_coresight_remote *coresight_remote;
	struct platform_device *pdev;
	struct device_node *parent_node;
	int i, ret;

	if (!args || !args->dev || !args->send_kci || !args->pm || !args->num_fw_targets ||
	    args->num_fw_targets > GCIP_CORESIGHT_REMOTE_MAX_FW_TARGETS)
		return ERR_PTR(-EINVAL);

	coresight_remote = devm_kzalloc(args->dev, sizeof(*coresight_remote), GFP_KERNEL);
	if (!coresight_remote)
		return ERR_PTR(-ENOMEM);

	coresight_remote->dev = args->dev;
	coresight_remote->pm = args->pm;
	coresight_remote->num_fw_targets = args->num_fw_targets;
	coresight_remote->send_kci = args->send_kci;

	for (i = 0; i < args->num_fw_targets; i++)
		coresight_remote->kci_data[i] = args->kci_data[i];

	parent_node = of_get_parent(args->dev->of_node);
	if (!parent_node) {
		dev_warn(args->dev, "gcip coresight: parent node not found for %s\n",
			 args->dev->of_node->full_name);
		ret = -ENOENT;
		goto free_coresight_remote;
	}

	/* Discover trace components for each firmware target. */
	for (i = 0; i < args->num_fw_targets; i++) {
		ret = gcip_discover_trace_components(args->dev, parent_node,
						     &coresight_remote->components[i], i);
		if (ret) {
			while (i--)
				gcip_free_trace_components(&coresight_remote->components[i]);

			goto release_node_ref;
		}
	}
	of_node_put(parent_node);

	pdev = to_platform_device(args->dev);
	ret = devm_coresight_remote_ops_register(pdev, &gcip_coresight_remote_ops,
						 coresight_remote);
	if (ret) {
		for (i = 0; i < args->num_fw_targets; i++)
			gcip_free_trace_components(&coresight_remote->components[i]);

		goto free_coresight_remote;
	}

	return coresight_remote;

release_node_ref:
	of_node_put(parent_node);
free_coresight_remote:
	devm_kfree(args->dev, coresight_remote);
	return ERR_PTR(ret);
}

void gcip_coresight_remote_unregister(struct gcip_coresight_remote *coresight_remote)
{
	int i;

	if (IS_ERR_OR_NULL(coresight_remote))
		return;

	for (i = 0; i < coresight_remote->num_fw_targets; i++)
		gcip_free_trace_components(&coresight_remote->components[i]);
}

/**
 * gcip_encode_pending_commands() - Encodes all pending component states into bulk commands.
 * @components: Pointer to the trace components structure.
 * @bulk_cmds: Pointer to the bulk commands structure to populate.
 */
static void gcip_encode_pending_commands(struct gcip_trace_components *components,
					 struct gcip_coresight_remote_bulk_cmds *bulk_cmds)
{
	struct gcip_replicator_state *replicator;
	struct gcip_funnel_state *funnel;
	struct gcip_etf_state *etf;
	struct gcip_etm_state *etm;
	int j, count = 0;

	for (j = 0; j < components->num_replicators; j++) {
		replicator = &components->replicators[j];

		if (replicator->base.is_enabled)
			bulk_cmds->commands[count++] = coresight_remote_encode_framework_op(
				CORESIGHT_REMOTE_DEV_REPLICATOR, replicator->base.remote_id,
				CORESIGHT_REMOTE_FW_OP_ENABLE, replicator->active_child_ports_mask);
	}

	for (j = 0; j < components->num_funnels; j++) {
		funnel = &components->funnels[j];

		if (funnel->base.is_enabled)
			bulk_cmds->commands[count++] = coresight_remote_encode_framework_op(
				CORESIGHT_REMOTE_DEV_FUNNEL, funnel->base.remote_id,
				CORESIGHT_REMOTE_FW_OP_ENABLE, funnel->active_ports_mask);
	}

	for (j = 0; j < components->num_etfs; j++) {
		etf = &components->etfs[j];

		if (etf->base.is_enabled)
			bulk_cmds->commands[count++] = coresight_remote_encode_framework_op(
				CORESIGHT_REMOTE_DEV_TMC, etf->base.remote_id,
				CORESIGHT_REMOTE_FW_OP_ENABLE, etf->mode);
	}

	for (j = 0; j < components->num_etms; j++) {
		etm = &components->etms[j];

		if (etm->base.is_enabled)
			bulk_cmds->commands[count++] = coresight_remote_encode_framework_op(
				CORESIGHT_REMOTE_DEV_ETM, etm->base.remote_id,
				CORESIGHT_REMOTE_FW_OP_ENABLE, 0);
	}

	bulk_cmds->num_commands = count;
}

int gcip_coresight_remote_restore_state(struct gcip_coresight_remote *coresight_remote)
{
	int i, ret;
	int cmd_count;
	struct gcip_coresight_remote_bulk_cmds bulk_cmds;
	struct gcip_trace_components *components;

	if (IS_ERR_OR_NULL(coresight_remote))
		return -EOPNOTSUPP;

	for (i = 0; i < coresight_remote->num_fw_targets; i++) {
		components = &coresight_remote->components[i];
		cmd_count = gcip_get_pending_enabled_trace_components_count(components);
		if (!cmd_count)
			continue;

		if (cmd_count > GCIP_CORESIGHT_REMOTE_MAX_BULK_CMDS) {
			dev_err(coresight_remote->dev, "Too many pending commands (%d > max %d)\n",
				cmd_count, GCIP_CORESIGHT_REMOTE_MAX_BULK_CMDS);
			return -E2BIG;
		}

		memset(&bulk_cmds, 0, sizeof(bulk_cmds));
		gcip_encode_pending_commands(components, &bulk_cmds);

		ret = gcip_coresight_remote_send_cmd_raw(coresight_remote, i, &bulk_cmds);
		if (ret) {
			dev_err(coresight_remote->dev,
				"failed to restore coresight remote state for fw_id %d: %d\n", i,
				ret);
			break;
		}
	}
	return ret;
}

#endif /* GCIP_CORESIGHT_REMOTE_ENABLED */
