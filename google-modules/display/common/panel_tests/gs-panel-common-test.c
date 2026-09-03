// SPDX-License-Identifier: MIT

#include "gs_panel/dcs_helper.h"
#include "gs_panel/gs_panel_test.h"
#include "trace/panel_trace.h"

#define MAX_SUPPORTED_PANEL 6

#define drm_to_gs_panel(panel) container_of(panel, struct gs_panel, base)

static u32 module_version = 1;

static struct gs_panel_test *test_impl[MAX_SUPPORTED_PANEL];

/* register creating nodes */
static struct gs_panel_register new_reg = {
	.pre_read_cmdset = {},
	.post_read_cmdset = {},
};

#define MAX_REG_NAME_SIZE 64

static ssize_t new_reg_name_write(struct file *file, const char __user *ubuf, size_t len,
				  loff_t *ppos)
{
	char buf[MAX_REG_NAME_SIZE] = { 0 };

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, len)))
		return -EFAULT;

	if (!new_reg.name) {
		new_reg.name = kmalloc(MAX_REG_NAME_SIZE, GFP_KERNEL);
		if (!new_reg.name)
			return -ENOMEM;
	}

	strscpy(new_reg.name, buf, len + 1);

	if (len > 0 && new_reg.name[len - 1] == '\n')
		new_reg.name[len - 1] = '\0';

	return len;
}

static const struct file_operations new_reg_name_fops = {
	.write = new_reg_name_write,
};

#define MAX_ALLOWED_REGISTER 0xFF

static int append_register(struct gs_panel_test *test, struct gs_panel_register *reg)
{
	struct gs_panel_register *registers;
	struct gs_panel_registers_desc *reg_desc;

	if (!gs_panel_test_has_registers_desc(test))
		return -EINVAL;

	reg_desc = test->test_desc->regs_desc;
	registers = reg_desc->registers;

	if (reg_desc->register_count >= MAX_ALLOWED_REGISTER) {
		dev_err(test->dev, "panel_test: reached maximum allowed register count\n");
		return -E2BIG;
	}

	reg_desc->register_count++;

	if (!registers) {
		reg_desc->registers = reg;
		return 0;
	}

	registers = krealloc(reg_desc->registers,
			     reg_desc->register_count * sizeof(struct gs_panel_register),
			     GFP_KERNEL);
	if (!registers)
		return -ENOMEM;

	registers[reg_desc->register_count - 1] = *reg;
	reg_desc->registers = registers;
	return 0;
}

/* gs_dsi_cmdset can not be modified at runtime */
struct panel_test_dsi_cmdset {
	u32 num_cmd;
	struct gs_dsi_cmd *cmds;
};
/* any change on struct gs_dsi_cmdset need to address this */
static_assert(sizeof(struct gs_dsi_cmdset) == sizeof(struct panel_test_dsi_cmdset));

struct panel_test_cmdset_data {
	struct panel_test_dsi_cmdset cmdset;
	struct gs_dsi_cmd current_command;
};

struct panel_test_cmdset_data new_reg_pre_read_data, new_reg_post_read_data;
struct panel_test_cmdset_data global_pre_read_data, global_post_read_data;

static int add_read_cmdset(struct gs_dsi_cmdset **cmdset, struct panel_test_dsi_cmdset *test_cmdset)
{
	struct gs_dsi_cmdset *retptr;
	struct panel_test_dsi_cmdset *cmdptr;

	if (!test_cmdset->num_cmd) {
		*cmdset = NULL;
		return 0;
	}

	cmdptr = kmalloc(sizeof(struct panel_test_dsi_cmdset), GFP_KERNEL);
	if (!cmdptr)
		return -ENOMEM;

	memcpy(cmdptr, test_cmdset, sizeof(*cmdptr));
	retptr = (struct gs_dsi_cmdset *)cmdptr;
	memset(test_cmdset, 0, sizeof(*test_cmdset));
	*cmdset = retptr;

	return 0;
}

static bool has_existing_register_in_debugfs(struct gs_panel_test *test, const char *name)
{
	struct dentry *test_root = test->debugfs_root, *regs_root, *reg_root;
	bool ret = false;

	regs_root = debugfs_lookup("regs", test_root);
	if (!regs_root)
		return false;

	reg_root = debugfs_lookup(name, regs_root);
	if (reg_root) {
		ret = true;
		dput(reg_root);
	}

	dput(regs_root);
	return ret;
}

static bool has_existing_register_with_name(struct gs_panel_test *test, const char *name)
{
	struct gs_panel_registers_desc *regs_desc;

	if (has_existing_register_in_debugfs(test, name))
		return true;

	if (!gs_panel_test_has_registers_desc(test))
		return false;

	regs_desc = test->test_desc->regs_desc;
	for (int i = 0; i < regs_desc->register_count; i++) {
		if (!strcmp(regs_desc->registers[i].name, name))
			return true;
	}

	return false;
}

static int add_new_register(struct gs_panel_test *test)
{
	struct gs_panel_register *reg;
	int name_length;

	if (!new_reg.name || !strlen(new_reg.name) || !new_reg.address)
		return -EINVAL;

	if (has_existing_register_with_name(test, new_reg.name)) {
		dev_err(test->dev, "panel_test: duplicate register name\n");
		return -EINVAL;
	}

	reg = kmalloc(sizeof(struct gs_panel_register), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	name_length = strlen(new_reg.name);
	reg->name = kmalloc(name_length + 1, GFP_KERNEL);
	if (!reg->name)
		return -ENOMEM;

	strscpy(reg->name, new_reg.name, name_length + 1);
	reg->address = new_reg.address;
	reg->size = new_reg.size > 1 ? new_reg.size : 1;
	reg->revision = new_reg.revision ?: PANEL_REV_ALL;

	if (add_read_cmdset(&reg->pre_read_cmdset, &new_reg_pre_read_data.cmdset) ||
	    add_read_cmdset(&reg->post_read_cmdset, &new_reg_post_read_data.cmdset))
		return -ENOMEM;

	return append_register(test, reg);
}

static int update_global_read_cmds(struct gs_panel_test *test)
{
	struct gs_panel_registers_desc *regs_desc = test->test_desc->regs_desc;

	if (add_read_cmdset(&regs_desc->global_pre_read_cmdset, &global_pre_read_data.cmdset) ||
	    add_read_cmdset(&regs_desc->global_post_read_cmdset, &global_post_read_data.cmdset))
		return -ENOMEM;

	return 0;
}

static ssize_t new_reg_done_write(struct file *file, const char __user *ubuf, size_t len,
				  loff_t *ppos)
{
	struct gs_panel_test *test = file->private_data;
	char buf[4];
	int ret;
	bool reg_add = false;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, len)))
		return -EFAULT;

	ret = kstrtobool_from_user(ubuf, len, &reg_add);
	if (ret)
		return ret;

	if (!new_reg.name || !new_reg.address)
		return -EINVAL;

	dev_dbg(test->dev, "panel_test: add %d name %s, address 0x%x size %d, rev: 0x%x\n", reg_add,
		new_reg.name ?: "NULL", new_reg.address, new_reg.size, new_reg.revision);

	ret = add_new_register(test);
	if (ret)
		return ret;

	memset(&new_reg, 0, sizeof(new_reg));

	return len;
}

static const struct file_operations new_reg_done_fops = {
	.open = simple_open,
	.write = new_reg_done_write,
};

static ssize_t parse_byte_buf(u8 *byte_array, size_t len, char *src)
{
	const char *skip = "\n ";
	size_t index = 0;
	int ret = 0;
	char *str;

	while (src && !ret && index < len) {
		str = strsep(&src, skip);
		if (*str != '\0') {
			ret = kstrtou8(str, 16, byte_array + index);
			index++;
		}
	}

	return ret ?: index;
}

#define MAX_CMD_SIZE 200

static struct gs_dsi_cmd create_dsi_cmd(u32 cmd_len, const u8 *cmd, u32 delay_ms,
					u32 panel_rev_bitmask, u16 flags, u8 type)
{
	struct gs_dsi_cmd cmds = { cmd_len, cmd, delay_ms, panel_rev_bitmask, flags, type };
	return cmds;
}

static int add_read_cmds(struct panel_test_cmdset_data *cmdset_data, char *payload, size_t count)
{
	struct panel_test_dsi_cmdset *read_cmdset = &cmdset_data->cmdset;
	struct gs_dsi_cmd *cmds, *base_cmds = &cmdset_data->current_command;
	u8 *cmd = kmalloc_array(count, sizeof(u8), GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	memcpy(cmd, payload, count);

	if (!base_cmds->panel_rev_bitmask)
		base_cmds->panel_rev_bitmask = PANEL_REV_ALL;

	if (read_cmdset->num_cmd > 0) {
		cmds = krealloc(read_cmdset->cmds,
				(read_cmdset->num_cmd + 1) * sizeof(struct gs_dsi_cmd), GFP_KERNEL);
	} else
		cmds = kmalloc(sizeof(struct gs_dsi_cmd), GFP_KERNEL);

	if (!cmds)
		return -ENOMEM;

	cmds[read_cmdset->num_cmd] = create_dsi_cmd(count, cmd, base_cmds->delay_ms,
						    base_cmds->panel_rev_bitmask, base_cmds->flags,
						    base_cmds->type);
	read_cmdset->cmds = cmds;
	read_cmdset->num_cmd++;
	memset(base_cmds, 0, sizeof(*base_cmds));
	return 0;
}

static ssize_t reg_read_cmd_write(struct file *file, const char __user *ubuf, size_t count,
				  loff_t *ppos)
{
	struct panel_test_cmdset_data *cmdset_data = file->private_data;
	char *buf, *payload;
	size_t len;
	int ret;

	buf = memdup_user_nul(ubuf, count);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	len = (count + 1) / 3;
	payload = kmalloc_array(len, sizeof(char), GFP_KERNEL);
	if (!payload) {
		kfree(buf);
		return -ENOMEM;
	}

	ret = parse_byte_buf(payload, len, buf);
	kfree(buf);

	if (!cmdset_data)
		return count;

	ret = add_read_cmds(cmdset_data, payload, len);

	return ret ?: count;
}

static const struct file_operations register_read_cmds_fops = {
	.open = simple_open,
	.write = reg_read_cmd_write,
};

static ssize_t population_complete_write(struct file *file, const char __user *ubuf, size_t len,
					 loff_t *ppos)
{
	struct gs_panel_test *test = file->private_data;
	char buf[4];
	int ret;
	bool populate_complete = false;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, len)))
		return -EFAULT;

	ret = kstrtobool_from_user(ubuf, len, &populate_complete);
	if (ret)
		return ret;

	ret = update_global_read_cmds(test);
	if (ret)
		return ret;

	ret = add_new_registers_to_debugfs(test, test->debugfs_root);
	return ret ?: len;
}

static const struct file_operations population_complete_fops = {
	.open = simple_open,
	.write = population_complete_write,
};

static int add_cmdset_creating_nodes(struct gs_panel_test *test, struct dentry *root,
				     const char *dir_name,
				     struct panel_test_cmdset_data *cmdset_data)
{
	struct gs_dsi_cmd *cmds = &cmdset_data->current_command;
	struct dentry *cmdset_root = debugfs_create_dir(dir_name, root);

	if (!cmdset_root)
		return -EFAULT;

	debugfs_create_u32("panel_rev_bitmask", 0600, cmdset_root, &cmds->panel_rev_bitmask);
	debugfs_create_u32("delay_ms", 0600, cmdset_root, &cmds->delay_ms);
	debugfs_create_u16("flags", 0600, cmdset_root, &cmds->flags);
	debugfs_create_u8("type", 0600, cmdset_root, &cmds->type);
	debugfs_create_file("cmd", 0200, cmdset_root, cmdset_data, &register_read_cmds_fops);

	return 0;
}

static int debugfs_add_register_creating_nodes(struct gs_panel_test *test, struct dentry *test_root)
{
	struct dentry *populate_root, *reg_root;

	debugfs_create_u32("version", 0400, test_root, &module_version);

	populate_root = debugfs_create_dir("create", test_root);
	if (!populate_root)
		return -EFAULT;

	reg_root = debugfs_create_dir("reg", populate_root);
	if (!reg_root)
		return -EFAULT;

	debugfs_create_bool("read_individually", 0600, reg_root, &new_reg.read_individually);
	debugfs_create_u32("size", 0600, reg_root, &new_reg.size);
	debugfs_create_u8("address", 0600, reg_root, &new_reg.address);
	debugfs_create_file("name", 0200, reg_root, &new_reg, &new_reg_name_fops);
	add_cmdset_creating_nodes(test, reg_root, "pre_read_cmds", &new_reg_pre_read_data);
	add_cmdset_creating_nodes(test, reg_root, "post_read_cmds", &new_reg_post_read_data);
	debugfs_create_u32("revision", 0600, reg_root, &new_reg.revision);
	debugfs_create_file("done", 0200, reg_root, test, &new_reg_done_fops);

	add_cmdset_creating_nodes(test, populate_root, "global_pre_read_cmds",
				  &global_pre_read_data);
	add_cmdset_creating_nodes(test, populate_root, "global_post_read_cmds",
				  &global_post_read_data);

	debugfs_create_file("complete", 0200, populate_root, test, &population_complete_fops);

	return 0;
}

static void common_test_debugfs_init(struct gs_panel_test *test, struct dentry *test_root)
{
	debugfs_add_register_creating_nodes(test, test_root);
}

static struct gs_panel_test_funcs common_test_func = {
	.debugfs_init = common_test_debugfs_init,
};

static int create_empty_test(struct gs_panel_test *test, struct gs_panel *ctx)
{
	test->dev = ctx->dev;
	test->test_desc = kmalloc(sizeof(struct gs_panel_test_desc), GFP_KERNEL);
	if (!test->test_desc)
		return -ENOMEM;

	test->test_desc->regs_desc = kmalloc(sizeof(struct gs_panel_registers_desc), GFP_KERNEL);
	if (!test->test_desc->regs_desc)
		return -ENOMEM;

	memset(test->test_desc->regs_desc, 0, sizeof(*test->test_desc->regs_desc));

	test->test_desc->query_desc = NULL;
	test->test_desc->test_funcs = &common_test_func;

	return 0;
}

static int panel_test_node_add(int panel_index, const char *node_path)
{
	struct drm_panel *panel;
	struct gs_panel *ctx;
	struct device_node *np;

	np = of_find_node_by_path(node_path);
	if (!np) {
		pr_err("panel_test: could not find node at path '%s'\n", node_path);
		return -EINVAL;
	}

	panel = of_drm_find_panel(np);
	of_node_put(np);
	if (!panel) {
		pr_err("panel_test: could not find panel\n");
		return -EINVAL;
	}

	ctx = drm_to_gs_panel(panel);

	if (ctx && ctx->gs_connector && (panel_index != ctx->gs_connector->panel_index)) {
		dev_err(ctx->dev, "panel_test: invalid panel_index\n");
		return -EINVAL;
	}

	if (test_impl[panel_index]) {
		gs_panel_test_remove_helper(test_impl[panel_index]);
		test_impl[panel_index] = NULL;
	}

	test_impl[panel_index] = kmalloc(sizeof(struct gs_panel_test), GFP_KERNEL);
	if (!test_impl[panel_index])
		return -ENOMEM;

	if (create_empty_test(test_impl[panel_index], ctx))
		return -ENOMEM;

	gs_panel_test_init_helper(ctx, test_impl[panel_index]);

	dev_info(ctx->dev, "panel_test: test node added for %s\n", node_path);

	return 0;
}

/* memory clear */

static void free_cmd(const struct gs_dsi_cmd *cmds)
{
	if (!cmds)
		return;

	if (cmds->cmd)
		kfree((void *)cmds->cmd);
}

static void free_cmdset(struct gs_dsi_cmdset *cmdset)
{
	if (!cmdset)
		return;

	if (!cmdset->cmds)
		goto cmdset;

	for (int i = 0; i < cmdset->num_cmd; i++)
		free_cmd(&cmdset->cmds[i]);

	kfree(cmdset->cmds);

cmdset:
	kfree(cmdset);
}

static void free_reg(struct gs_panel_register *reg)
{
	if (!reg)
		return;

	kfree(reg->name);
	free_cmdset(reg->pre_read_cmdset);
	free_cmdset(reg->post_read_cmdset);
}

static void free_regs_desc(struct gs_panel_registers_desc *regs_desc)
{
	if (!regs_desc)
		return;

	for (int i = 0; i < regs_desc->register_count; i++)
		free_reg(&regs_desc->registers[i]);

	kfree(regs_desc->registers);
	kfree(regs_desc);
}

static void free_test_desc(struct gs_panel_test_desc *test_desc)
{
	if (!test_desc)
		return;

	free_regs_desc(test_desc->regs_desc);
	kfree(test_desc);
}

static void free_test(struct gs_panel_test *test)
{
	if (!test)
		return;

	free_test_desc(test->test_desc);
	kfree(test);
}

static void panel_test_node_remove(int panel_index)
{
	gs_panel_test_remove_helper(test_impl[panel_index]);
	free_test(test_impl[panel_index]);
	test_impl[panel_index] = NULL;
}

/* driver init exit */

#define PANEL_PATH_LEN 128

static char panel0_path[PANEL_PATH_LEN] = { '\0' };
module_param_string(panel0_path, panel0_path, sizeof(panel0_path), 0644);
MODULE_PARM_DESC(panel0_path, "primary panel name");

static char panel1_path[PANEL_PATH_LEN] = { '\0' };
module_param_string(panel1_path, panel1_path, sizeof(panel1_path), 0644);
MODULE_PARM_DESC(panel1_path, "secondary panel name");

static int __init panel_test_driver_init(void)
{
	int panel_count = 0;

	if (!strlen(panel0_path) && !strlen(panel1_path)) {
		pr_err("panel_test: panel0_path or panel1_path must be provided\n");
		return -EINVAL;
	}

	PANEL_ATRACE_BEGIN("panel_test_driver_init");
	pr_info("panel_test: primary_panel: '%s', secondary_panel: '%s'\n", panel0_path,
		panel1_path);

	if (strlen(panel0_path) && panel_test_node_add(0, panel0_path) == 0)
		panel_count++;

	if (strlen(panel1_path) && panel_test_node_add(1, panel1_path) == 0)
		panel_count++;

	PANEL_ATRACE_END("panel_test_driver_init");
	return panel_count > 0 ? 0 : -EINVAL;
}

static void __exit panel_test_driver_exit(void)
{
	PANEL_ATRACE_BEGIN("panel_test_driver_exit");

	panel_test_node_remove(0);
	panel_test_node_remove(1);

	PANEL_ATRACE_END("panel_test_driver_exit");
	pr_info("panel_test: driver exit\n");
}

module_init(panel_test_driver_init);
module_exit(panel_test_driver_exit);

MODULE_AUTHOR("Safayat Ullah <safayat@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google panel test common driver");
MODULE_LICENSE("Dual MIT/GPL");
