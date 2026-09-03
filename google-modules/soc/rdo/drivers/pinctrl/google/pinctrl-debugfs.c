// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */
#include <linux/debugfs.h>
#include <core.h>

#include "pinctrl-debugfs.h"
#include "common.h"

bool fops_stats_en;
module_param(fops_stats_en, bool, false);

#define DEBUGFS_PINS_HEADER " idx |          SOC pin           | IRQ Trigger | Drv Strength | Direction | Value\n"
#define DEBUGFS_FOPS_HEADER " idx |       SOC pin        |   Dir input  |  Dir output  |   Dir get    |     Get      |     Set      |    Config\n"
#define DEBUGFS_VALID_MASK_HEADER " idx |          SOC pin           | Valid\n"

#define F_DENTRY(filp) ((filp)->f_path.dentry)
#define STR_ATTR_BUF_INIT_SIZE_B 1024

struct str_attr {
	int (*read_callback)(void *data, struct str_attr_file *attr_file);
	int (*write_callback)(void *data, const struct str_attr_buf *str_buf);
	struct str_attr_file read_buf;
	struct str_attr_buf write_buf;
	void *data;
	struct mutex mutex;
};

static int init_str_attr_file(struct str_attr_file *attr_file)
{
	attr_file->buf = kvmalloc(STR_ATTR_BUF_INIT_SIZE_B, GFP_KERNEL);
	if (!attr_file->buf)
		return -ENOMEM;

	attr_file->size = STR_ATTR_BUF_INIT_SIZE_B;
	attr_file->count = 0;

	return 0;
}

int str_attr_open(struct inode *inode, struct file *file,
		  int (*read_callback)(void *, struct str_attr_file *),
		  int (*write_callback)(void *, const struct str_attr_buf *))
{
	struct str_attr *attr;
	int ret;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	ret = init_str_attr_file(&attr->read_buf);
	if (ret == 0)
		attr->read_callback = read_callback;

	attr->write_callback = write_callback;
	attr->data = inode->i_private;
	mutex_init(&attr->mutex);

	file->private_data = attr;

	return nonseekable_open(inode, file);
}

int str_attr_release(struct inode *inode, struct file *file)
{
	struct str_attr *attr = file->private_data;

	kvfree(attr->read_buf.buf);
	kfree(attr);

	return 0;
}

static ssize_t __str_attr_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	struct str_attr *attr;
	ssize_t ret;

	attr = file->private_data;

	if (!attr->read_callback)
		return -EACCES;

	ret = mutex_lock_interruptible(&attr->mutex);
	if (ret)
		return ret;

	if (!(*ppos)) {
		/* first read */
		ret = attr->read_callback(attr->data, &attr->read_buf);
		if (ret)
			goto out;
	}

	ret = simple_read_from_buffer(buf, len, ppos, attr->read_buf.buf, attr->read_buf.count);

out:
	mutex_unlock(&attr->mutex);
	return ret;
}

static ssize_t __str_attr_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct str_attr *attr;
	int ret;

	attr = file->private_data;
	if (!attr->write_callback)
		return -EACCES;

	if (len > STR_ATTR_MAX_FILE_SIZE)
		return -EFBIG;

	attr->write_buf.buf = kvmalloc(len, GFP_KERNEL);
	if (!attr->write_buf.buf)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&attr->mutex);
	if (ret)
		goto out;

	ret = -EFAULT;
	if (copy_from_user(attr->write_buf.buf, buf, len - 1))
		goto out_locked;

	attr->write_buf.size = len;
	attr->write_buf.buf[len - 1] = '\0';

	ret = attr->write_callback(attr->data, &attr->write_buf);
	if (ret == 0)
		ret = len; /* on success, claim we got the whole input */

out_locked:
	mutex_unlock(&attr->mutex);
out:
	kvfree(attr->write_buf.buf);
	attr->write_buf.buf = NULL;
	attr->write_buf.size = 0;

	return ret;
}

ssize_t str_attr_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	struct dentry *dentry = F_DENTRY(file);
	ssize_t ret;

	ret = debugfs_file_get(dentry);
	if (unlikely(ret))
		return ret;
	ret = __str_attr_read(file, buf, len, ppos);
	debugfs_file_put(dentry);
	return ret;
}

ssize_t str_attr_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct dentry *dentry = F_DENTRY(file);
	ssize_t ret;

	ret = debugfs_file_get(dentry);
	if (unlikely(ret))
		return ret;
	ret = __str_attr_write(file, buf, len, ppos);
	debugfs_file_put(dentry);
	return ret;
}

static int expand_buffer(struct str_attr_file *attr_file)
{
	char *new_buf;

	if (attr_file->size >= STR_ATTR_MAX_FILE_SIZE)
		return -EFBIG;

	new_buf = kvrealloc(attr_file->buf, attr_file->size << 1, GFP_KERNEL);
	if (new_buf) {
		attr_file->buf = new_buf;
		attr_file->size <<= 1;
	} else {
		return -ENOMEM;
	}

	return 0;
}

void str_attr_puts(struct str_attr_file *attr_file, const char *s)
{
	int n = strlen(s);
	int free_capacity = attr_file->size - attr_file->count - 1;
	int ret;

	if (free_capacity == 0)
		return;

	/* If expand_buffer fails, just do a partial copy */
	if (n >= free_capacity && !expand_buffer(attr_file))
		free_capacity = attr_file->size - attr_file->count - 1;

	ret = strscpy(attr_file->buf + attr_file->count, s, free_capacity + 1);

	if (ret > 0)
		attr_file->count += ret;
}

void str_attr_printf(struct str_attr_file *attr_file, const char *fmt, ...)
{
	va_list args;
	int n;
	int free_capacity = attr_file->size - attr_file->count - 1;

	if (free_capacity == 0)
		return;

	va_start(args, fmt);
	n = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (n >= free_capacity && !expand_buffer(attr_file))
		free_capacity = attr_file->size - attr_file->count - 1;

	va_start(args, fmt);
	n = vscnprintf(attr_file->buf + attr_file->count, free_capacity + 1, fmt, args);
	va_end(args);

	attr_file->count += n;
}

static void print_str_to_file_or_log(const char *str, struct google_pinctrl *gctl,
				     struct seq_file *file, bool print_to_seq_file)
{
	if (print_to_seq_file)
		seq_puts(file, str);
	else
		dev_info(gctl->dev, "%s", str);
}

/**
 * pin_excl_reg() - Checks if pin DOESN'T CONTAIN a register (i.e. the pin excludes the register)
 * @pin_reg_flags: register exclusion flags for a pin
 * @reg: the register to test for exclusion
 *
 * This function checks if a register is excluded or not - i.e. if it is not present on a pin. Note
 * the inverted logic - if the register is not present/is excluded - the flag will be set to 1.
 * Otherwise, if it is present/isn't excluded - the exclusion flag will be set to 0.
 *
 * Note that flags layout should have the same ordering as members of the
 * google_pinctrl_register_idx enum
 */
static bool pin_excl_reg(struct google_pinctrl_registers_flags pin_reg_flags,
			 enum google_pinctrl_register_idx reg)
{
	return pin_reg_flags.val & BIT(reg);
}

static int dump_regs_form(struct google_pinctrl *gctl, struct seq_file *file)
{
	int ngroups, ret, n_pins_excl_regs;
	int pin_excl_regs_i = 0;
	const bool print_to_seq_file = (file != NULL);
	unsigned long flags;
	const char *dump_regs_header = gctl->info->common->dump_regs_header;
	/*
	 * `tmp_buf` is big enough to store `dump_regs_header`. This is guaranteed by asserts
	 * near the definition of headers.
	 */
	char tmp_buf[DUMP_REGS_BUF_SIZE + 2];
	const size_t buf_size = DUMP_REGS_BUF_SIZE;
	const struct google_pinctrl_soc_sswrp_info *info;
	const struct google_pingroup *pingroup;
	/* Flag layout ordering must be the same as members of google_pinctrl_register_idx enum */
	const struct google_pinctrl_registers_flags *pin_excl_regs;
	u32 reg_val;
	bool pin_excl;
	const u8 *regs = gctl->info->common->regs;
	unsigned int num_regs = gctl->info->common->num_regs;
	u8 reg;

	ret = google_pinctrl_get_csr_pd(gctl, -1);
	if (ret < 0)
		return ret;

	print_str_to_file_or_log(dump_regs_header, gctl, file,
				 print_to_seq_file);

	ngroups = gctl->info->num_groups;
	info = gctl->info;

	n_pins_excl_regs = info->npins_excl_regs;
	pin_excl_regs = n_pins_excl_regs == 0 ? NULL
					      : &info->pins_excl_regs[pin_excl_regs_i++];

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, -1);
		return ret;
	}

	for (int i = 0; i < ngroups; ++i) {
		size_t tmp_buf_count;

		pingroup = &gctl->info->groups[i];

		tmp_buf_count = scnprintf(tmp_buf, buf_size, "%5d %28s", pingroup->num,
					  pingroup->name);

		pin_excl = (pin_excl_regs != NULL) && (pin_excl_regs->pin_id == i);

		if (!google_pinctrl_pin_valid(gctl, i)) {
			tmp_buf_count += scnprintf(tmp_buf + tmp_buf_count,
						   buf_size - tmp_buf_count,
						   " INACCESSIBLE/INVALID FROM KERNEL");
			goto drf_skip_lp;
		}
		for (int j = 0; j < num_regs; ++j) {
			reg = regs[j];

			if (!pin_excl || !pin_excl_reg(*pin_excl_regs, reg)) {
				reg_val = google_readl(get_reg2offset(reg), gctl, pingroup);
				tmp_buf_count += scnprintf(tmp_buf + tmp_buf_count,
							   buf_size - tmp_buf_count, " %#010X",
							   reg_val);
			} else {
				tmp_buf_count += scnprintf(tmp_buf + tmp_buf_count,
							   buf_size - tmp_buf_count, " %10s", "-");
			}
		}

drf_skip_lp:
		tmp_buf_count = min(tmp_buf_count, buf_size - 2);

		tmp_buf[tmp_buf_count++] = '\n';
		tmp_buf[tmp_buf_count] = '\0';

		print_str_to_file_or_log(tmp_buf, gctl, file,
					 print_to_seq_file);

		if (pin_excl) {
			pin_excl_regs = pin_excl_regs_i < n_pins_excl_regs
						? &info->pins_excl_regs[pin_excl_regs_i++] : NULL;
		}
	}

	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, -1);

	return 0;
}

int dump_regs_logs(struct google_pinctrl *gctl)
{
	return dump_regs_form(gctl, NULL);
}

static int dump_regs_show(struct seq_file *s, void *p)
{
	struct google_pinctrl *gctl = s->private;

	return dump_regs_form(gctl, s);
}
DEFINE_SHOW_ATTRIBUTE(dump_regs);

static int trigger_irq_set(void *data, u64 pin_index)
{
	struct google_pinctrl *gctl = data;
	const struct google_pingroup *pingroup;
	struct generic_ixr_t itr_reg;
	unsigned long flags;
	int ret;

	if (pin_index >= gctl->info->num_groups)
		return -EINVAL;

	ret = google_pinctrl_get_csr_pd(gctl, pin_index);
	if (ret < 0)
		return ret;

	pingroup = &gctl->info->groups[pin_index];

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, pin_index);
		return ret;
	}

	itr_reg.layout.irq = 0;
	google_writel(get_reg2offset(ITR_ID), itr_reg.val, gctl, pingroup);

	itr_reg.layout.irq = 1;
	google_writel(get_reg2offset(ITR_ID), itr_reg.val, gctl, pingroup);

	itr_reg.layout.irq = 0;
	google_writel(get_reg2offset(ITR_ID), itr_reg.val, gctl, pingroup);

	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, pin_index);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(trigger_irq_fops, NULL, trigger_irq_set, "%llu\n");

static int rpm_count_show(struct seq_file *s, void *p)
{
	struct google_pinctrl *gctl = s->private;
	int i, idx;

	seq_printf(s, "rpm use_cnt: %d\n", atomic_read(&gctl->dev->power.usage_count));
	seq_printf(s, "rpm_get_cnt: %d\n", atomic_read(&gctl->rpm_get_count));
	seq_printf(s, "rpm_put_cnt: %d\n", atomic_read(&gctl->rpm_put_count));
	seq_printf(s, "rpm_susp_cnt: %d\n", atomic_read(&gctl->rpm_suspend_count));
	seq_printf(s, "rpm_resm_cnt: %d\n", atomic_read(&gctl->rpm_resume_count));
	seq_printf(s, "sys_susp_cnt: %d\n", atomic_read(&gctl->system_suspend_count));
	seq_printf(s, "sys_resm_cnt: %d\n", atomic_read(&gctl->system_resume_count));
	seq_printf(s, "rpm status: %d\n", gctl->dev->power.runtime_status);

	seq_printf(s, "Global max runtime suspend interval (ms): %lld.%03lld\n",
		   ktime_to_ms(gctl->rpm_max_suspend_time),
		   ktime_to_us(gctl->rpm_max_suspend_time) % 1000);

	seq_printf(s, "Last %d runtime suspend/resume intervals (ms): ", RPM_STATS_SIZE);
	idx = (gctl->rpm_suspend_resume_idx - 1 + RPM_STATS_SIZE) % RPM_STATS_SIZE;
	for (i = 0; i < RPM_STATS_SIZE; i++) {
		seq_printf(s, "%lld.%03lld ",
			ktime_to_ms(gctl->rpm_suspend_resume_times[idx]),
			ktime_to_us(gctl->rpm_suspend_resume_times[idx]) % 1000);
		idx = (idx - 1 + RPM_STATS_SIZE) % RPM_STATS_SIZE;
	}
	seq_puts(s, "\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rpm_count);

static int fops_count_show(struct seq_file *s, void *p)
{
	struct google_pinctrl *gctl = s->private;
	int ngroups = gctl->info->num_groups;

	if (!gctl->g_pingroups_fops_stats)
		return -EINVAL;

	seq_puts(s, DEBUGFS_FOPS_HEADER);

	for (int i = 0; i < ngroups; ++i) {
		const struct google_pingroup *g = &gctl->info->groups[i];

		seq_printf(s, "%5d %22s %14d %14d %14d %14d %14d %14d\n",
			   g->num, g->name,
			   atomic_read(&gctl->g_pingroups_fops_stats[i].dir_input_count),
			   atomic_read(&gctl->g_pingroups_fops_stats[i].dir_output_count),
			   atomic_read(&gctl->g_pingroups_fops_stats[i].dir_get_count),
			   atomic_read(&gctl->g_pingroups_fops_stats[i].get_count),
			   atomic_read(&gctl->g_pingroups_fops_stats[i].set_count),
			   atomic_read(&gctl->g_pingroups_fops_stats[i].config_count)
		);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fops_count);

static int debugfs_direction_write_callback(void *data, const struct str_attr_buf *str_buf)
{
	struct google_pinctrl *gctl = data;
	const struct google_pingroup *pingroup;
	int ngroups;
	unsigned int index;
	char direction[5];
	struct param_t param_reg;
	struct txdata_t tx_reg;
	u32 param_ie, txdata_oe;
	unsigned long flags, param_format_type;
	int ret;

	ngroups = gctl->info->num_groups;

	ret = sscanf(str_buf->buf, "%u %4s", &index, direction);
	if (ret != 2 || index >= ngroups)
		return -EINVAL;

	if (strcmp(direction, "none") == 0) {
		param_ie = 0;
		txdata_oe = 0;
	} else if (strcmp(direction, "in") == 0) {
		param_ie = 1;
		txdata_oe = 0;
	} else if (strcmp(direction, "out") == 0) {
		param_ie = 0;
		txdata_oe = 1;
	} else if (strcmp(direction, "bi") == 0) {
		param_ie = 1;
		txdata_oe = 1;
	} else {
		return -EINVAL;
	}

	ret = google_pinctrl_get_csr_pd(gctl, index);
	if (ret < 0)
		return ret;

	pingroup = &gctl->info->groups[index];
	param_format_type = (unsigned long) gctl->info->pins[index].drv_data;

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, index);
		return ret;
	}

	param_reg.val = google_readl(get_reg2offset(PARAM_ID), gctl, pingroup);
	tx_reg.val = google_readl(get_reg2offset(TXDATA_ID), gctl, pingroup);

	SET_PARAM_FIELD(param_reg, param_format_type, ie, param_ie);
	tx_reg.layout.oe = txdata_oe;

	google_writel(get_reg2offset(PARAM_ID), param_reg.val, gctl, pingroup);
	google_writel(get_reg2offset(TXDATA_ID), tx_reg.val, gctl, pingroup);

	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, index);

	return 0;
}
DEFINE_STRING_ATTRIBUTE(debugfs_direction, NULL, debugfs_direction_write_callback);

static int debugfs_drive_strength_write_callback(void *data, const struct str_attr_buf *str_buf)
{
	struct google_pinctrl *gctl = data;
	const struct google_pingroup *pingroup;
	int ngroups;
	unsigned int index, drive_strength;
	struct param_t param_reg;
	unsigned long flags, param_format_type;
	int ret;

	ngroups = gctl->info->num_groups;

	ret = sscanf(str_buf->buf, "%u %u", &index, &drive_strength);
	if (ret != 2 || index >= ngroups || drive_strength > 15)
		return -EINVAL;

	ret = google_pinctrl_get_csr_pd(gctl, index);
	if (ret < 0)
		return ret;

	pingroup = &gctl->info->groups[index];
	param_format_type = (unsigned long) gctl->info->pins[index].drv_data;

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, index);
		return ret;
	}

	param_reg.val = google_readl(get_reg2offset(PARAM_ID), gctl, pingroup);
	SET_PARAM_FIELD(param_reg, param_format_type, drv, drive_strength);

	google_writel(get_reg2offset(PARAM_ID), param_reg.val, gctl, pingroup);

	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, index);

	return 0;
}
DEFINE_STRING_ATTRIBUTE(debugfs_drive_strength, NULL, debugfs_drive_strength_write_callback);

static int debugfs_value_write_callback(void *data, const struct str_attr_buf *str_buf)
{
	struct google_pinctrl *gctl = data;
	const struct google_pingroup *pingroup;
	int ngroups;
	unsigned int index, csr_pad_val;
	struct rxdata_t rxdata_reg;
	unsigned long flags;
	int ret;

	ngroups = gctl->info->num_groups;

	ret = sscanf(str_buf->buf, "%u %u", &index, &csr_pad_val);
	if (ret != 2 || index >= ngroups || csr_pad_val > 1)
		return -EINVAL;

	ret = google_pinctrl_get_csr_pd(gctl, index);
	if (ret < 0)
		return ret;

	pingroup = &gctl->info->groups[index];

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, index);
		return ret;
	}

	rxdata_reg.val = google_readl(get_reg2offset(RXDATA_ID), gctl, pingroup);
	rxdata_reg.layout.pad_val = csr_pad_val;

	google_writel(get_reg2offset(RXDATA_ID), rxdata_reg.val, gctl, pingroup);

	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, index);

	return 0;
}
DEFINE_STRING_ATTRIBUTE(debugfs_value, NULL, debugfs_value_write_callback);

static const char *get_direction(u32 param_ie, u32 txdata_oe)
{
	if (param_ie == 0 && txdata_oe == 0)
		return "none";
	else if (param_ie == 1 && txdata_oe == 0)
		return "in";
	else if (param_ie == 0 && txdata_oe == 1)
		return "out";
	else if (param_ie == 1 && txdata_oe == 1)
		return "bi";
	else
		return "err?";
}

static bool pin_has_reg(const struct google_pinctrl_registers_flags *pin_excl_regs, bool pin_excl,
			enum google_pinctrl_register_idx reg)
{
	return !pin_excl || !pin_excl_reg(*pin_excl_regs, reg);
}

static void pin_show_excluded_reg_print(struct google_pinctrl *gctl,
					const struct google_pingroup *pgroup,
					const struct google_pinctrl_registers_flags *pin_excl_regs,
					bool pin_excl, enum google_pinctrl_register_idx reg,
					struct seq_file *s, unsigned int fmt_width,
					unsigned int pin_index, unsigned long param_format_type,
					struct param_t param_reg)
{
	u32 reg_val;
	u32 param_ie = 0;
	struct txdata_t tx_reg;
	struct rxdata_t rx_reg;

	if (!pin_has_reg(pin_excl_regs, pin_excl, reg)) {
		seq_printf(s, " %*s", fmt_width, "-");
		return;
	}

	reg_val = google_readl(get_reg2offset(reg), gctl, pgroup);

	switch (reg) {
	case PARAM_ID:
		param_format_type = (unsigned long)gctl->info->pins[pin_index].drv_data;
		param_reg.val = reg_val;
		GET_PARAM_FIELD(param_reg, param_format_type, drv, reg_val);
		break;
	case TXDATA_ID:
		GET_PARAM_FIELD(param_reg, param_format_type, ie, param_ie);
		tx_reg.val = reg_val;
		seq_printf(s, " %*s", fmt_width, get_direction(param_ie, tx_reg.layout.oe));
		return;
	case RXDATA_ID:
		rx_reg.val = reg_val;
		reg_val = rx_reg.layout.pad_val;
		break;
	case ISR_ID:
		break;
	default:
		return;
	}

	seq_printf(s, " %*u", fmt_width, reg_val);

	if (reg == PARAM_ID) {
		pin_show_excluded_reg_print(gctl, pgroup, pin_excl_regs, pin_excl, TXDATA_ID, s, 11,
					    pin_index, param_format_type, param_reg);
	}
}

static int pins_show(struct seq_file *s, void *p)
{
	struct google_pinctrl *gctl = s->private;
	int ngroups, ret, n_pins_excl_regs;
	int pin_excl_regs_i = 0;
	unsigned long flags;
	const struct google_pinctrl_soc_sswrp_info *info;
	const struct google_pingroup *pgroup;
	const struct google_pinctrl_registers_flags *pin_excl_regs;
	bool pin_excl;
	struct param_t param_reg = {
		.val = 0
	};

	ret = google_pinctrl_get_csr_pd(gctl, -1);
	if (ret < 0)
		return ret;

	seq_puts(s, DEBUGFS_PINS_HEADER);

	ngroups = gctl->info->num_groups;
	info = gctl->info;

	n_pins_excl_regs = info->npins_excl_regs;
	pin_excl_regs = n_pins_excl_regs == 0 ? NULL : &info->pins_excl_regs[pin_excl_regs_i++];

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, -1);
		return ret;
	}

	for (int i = 0; i < ngroups; ++i) {
		pgroup = &gctl->info->groups[i];
		pin_excl = (pin_excl_regs != NULL) && (pin_excl_regs->pin_id == i);

		seq_printf(s, "%5d %28s", pgroup->num, pgroup->name);

		pin_show_excluded_reg_print(gctl, pgroup, pin_excl_regs, pin_excl, ISR_ID, s, 13,
					    i, 0, param_reg);
		pin_show_excluded_reg_print(gctl, pgroup, pin_excl_regs, pin_excl, PARAM_ID, s, 14,
					    i, 0, param_reg);
		pin_show_excluded_reg_print(gctl, pgroup, pin_excl_regs, pin_excl, RXDATA_ID, s, 7,
					    i, 0, param_reg);
		seq_puts(s, "\n");

		if (pin_excl) {
			pin_excl_regs = pin_excl_regs_i < n_pins_excl_regs
						? &info->pins_excl_regs[pin_excl_regs_i++] : NULL;
		}
	}

	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, -1);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pins);

static int valid_mask_show(struct seq_file *s, void *p)
{
	struct google_pinctrl *gctl = s->private;
	int ret, ngroups, valid_mask_n, tmp_bits_n, i;
	unsigned long flags;
	const struct google_pingroup *pingroup;
	u8 reg_val;

	ret = google_pinctrl_get_csr_pd(gctl, -1);
	if (ret < 0)
		return ret;

	ngroups = gctl->info->num_groups;

	ret = google_pinctrl_trylock(gctl, &flags, false);
	if (ret < 0) {
		google_pinctrl_put_csr_pd(gctl, -1);
		return ret;
	}

	if (!gctl->chip.valid_mask) {
		seq_puts(s, "NULL\n");
		ret = 0;
		goto vms_out;
	}

	tmp_bits_n = sizeof(*gctl->chip.valid_mask) * 8;
	valid_mask_n = (ngroups + tmp_bits_n - 1) / tmp_bits_n;
	for (i = 0; i < valid_mask_n; ++i) {
		seq_printf(s, "[%5d - %5d]: %#lX\n",
			   i * tmp_bits_n, (i + 1) * tmp_bits_n, gctl->chip.valid_mask[i]);
	}

	seq_puts(s, "\n");
	seq_puts(s, DEBUGFS_VALID_MASK_HEADER);
	for (i = 0; i < ngroups; ++i) {
		pingroup = &gctl->info->groups[i];
		reg_val = !!test_bit(pingroup->num, gctl->chip.valid_mask);
		seq_printf(s, "%5d %28s %7d\n", pingroup->num, pingroup->name, reg_val);
	}

vms_out:
	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, -1);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(valid_mask);

static int pins_rpm_stats_rd_cb(void *data, struct str_attr_file *attr_file)
{
	struct google_pinctrl *gctl = data;
	const struct google_pingroup *g;
	const struct pin_rpm_stats *pin_stats;
	struct pin_rpm_stats *stats_cpy;
	struct pin_desc *pin_core_desc;
	char tmp_buf[28];
	const char *owner;
	int ngroups, i, ret;
	unsigned long flags, pin_guid;

	raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);

	if (!gctl->pins_rpm_stats) {
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);
		goto out;
	}

	ngroups = gctl->info->num_groups;

	raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);
	stats_cpy = kvcalloc(ngroups, sizeof(*gctl->pins_rpm_stats), GFP_KERNEL);
	if (!stats_cpy)
		return -ENOMEM;
	raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);

	if (!gctl->pins_rpm_stats) {
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);
		kvfree(stats_cpy);
		goto out;
	}

	memcpy(stats_cpy, gctl->pins_rpm_stats, ngroups * sizeof(*gctl->pins_rpm_stats));

	raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

	str_attr_printf(attr_file, "%5s|%28s|%10s|%10s|%10s|%s\n",
			"idx", "SOC pin", "rpm_get", "rpm_put", "get/sec", "owner");

	for (i = 0; i < ngroups; ++i) {
		g = &gctl->info->groups[i];
		pin_stats = &stats_cpy[i];
		pin_guid = gctl->info->pins[i].number;

		owner = NULL;

		mutex_lock(&gctl->pctl->mutex);
		pin_core_desc = pin_desc_get(gctl->pctl, pin_guid);
		if (pin_core_desc) {
			if (pin_core_desc->mux_owner)
				owner = pin_core_desc->mux_owner;
			else if (pin_core_desc->gpio_owner)
				owner = pin_core_desc->gpio_owner;
		}

		if (owner)
			ret = strscpy(tmp_buf, owner, sizeof(tmp_buf));
		mutex_unlock(&gctl->pctl->mutex);

		str_attr_printf(attr_file, "%5d %28.28s %10llu %10llu %10llu %s\n",
				g->num, g->name,
				pin_stats->rpm_get_cnt, pin_stats->rpm_put_cnt,
				pin_stats->gets_per_sec, owner ? tmp_buf : "none");
	}

	kvfree(stats_cpy);
	return 0;

out:
	str_attr_puts(attr_file, "feature not enabled\n");
	return 0;
}

static int pins_rpm_stats_w_cb(void *data, const struct str_attr_buf *str_buf)
{
	struct google_pinctrl *gctl = data;
	struct pin_rpm_stats *tmp;
	int ngroups, ret;
	unsigned long flags;
	bool enable;

	ret = kstrtobool(str_buf->buf, &enable);
	if (ret)
		return ret;

	ngroups = gctl->info->num_groups;

	raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);

	tmp = gctl->pins_rpm_stats;
	if (enable && !tmp && !gctl->pins_rpm_stats_alloc) {
		gctl->pins_rpm_stats_alloc = true;
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

		tmp = kvcalloc(ngroups, sizeof(*gctl->pins_rpm_stats), GFP_KERNEL);
		if (!tmp) {
			raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);
			gctl->pins_rpm_stats_alloc = false;
			raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

			return -ENOMEM;
		}

		raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);
		gctl->pins_rpm_stats = tmp;
		gctl->pins_rpm_stats_alloc = false;
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);
	} else if (!enable && tmp) {
		gctl->pins_rpm_stats = NULL;
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

		kvfree(tmp);
	} else {
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);
	}

	return 0;
}
DEFINE_STRING_ATTRIBUTE(pins_rpm_stats, pins_rpm_stats_rd_cb, pins_rpm_stats_w_cb);

static int google_pinctrl_debugfs_create_file(const char *name, umode_t mode, struct dentry *parent,
					      void *data, const struct file_operations *fops)
{
	struct google_pinctrl *gctl = data;
	struct dentry *tmp;
	int ret = 0;

	tmp = debugfs_create_file(name, mode, parent, data, fops);
	if (IS_ERR_OR_NULL(tmp)) {
		ret = !tmp ? -ENOMEM : PTR_ERR(tmp);
		dev_err(gctl->dev, "Failed to create %s: %d\n", name, ret);
	}

	return ret;
}

static int google_pinctrl_debugfs_create_bool(const char *name, umode_t mode, struct dentry *parent,
					      struct google_pinctrl *gctl, bool *bool_var_ptr)
{
	struct dentry *tmp;
	int ret;

	debugfs_create_bool(name, mode, parent, bool_var_ptr);
	tmp = debugfs_lookup(name, parent);
	if (IS_ERR_OR_NULL(tmp)) {
		ret = !tmp ? -ENOMEM : PTR_ERR(tmp);
		dev_err(gctl->dev, "Failed to create %s: %d\n", name, ret);
	} else {
		ret = 0;
		dput(tmp);
	}

	return ret;
}

int google_pinctrl_init_debugfs(struct google_pinctrl *gctl, struct platform_device *pdev,
				unsigned int num_groups)
{
	struct dentry **pinctl_de = &gctl->de;
	unsigned int success_cnt = 0;

	atomic_set(&gctl->rpm_get_count, 0);
	atomic_set(&gctl->rpm_put_count, 0);
	atomic_set(&gctl->rpm_suspend_count, 0);
	atomic_set(&gctl->rpm_resume_count, 0);
	atomic_set(&gctl->system_suspend_count, 0);
	atomic_set(&gctl->system_resume_count, 0);

	if (fops_stats_en) {
		gctl->g_pingroups_fops_stats = devm_kzalloc(&pdev->dev,
				num_groups * sizeof(*gctl->g_pingroups_fops_stats), GFP_KERNEL);
	}

	*pinctl_de = debugfs_create_dir(dev_name(gctl->pctl->dev), NULL);
	if (IS_ERR_OR_NULL(*pinctl_de)) {
		dev_err(gctl->dev, "Failed to create debugfs\n");
		*pinctl_de = NULL;
		return -EIO;
	}

	success_cnt += !google_pinctrl_debugfs_create_file("dump-registers", 0400, *pinctl_de, gctl,
							   &dump_regs_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("trigger-irq", 0200, *pinctl_de, gctl,
							   &trigger_irq_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("rpm-stats", 0400, *pinctl_de, gctl,
							   &rpm_count_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("direction", 0200, *pinctl_de, gctl,
							   &debugfs_direction_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("drive-strength", 0200, *pinctl_de, gctl,
							   &debugfs_drive_strength_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("value", 0200, *pinctl_de, gctl,
							   &debugfs_value_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("pins", 0400, *pinctl_de, gctl,
							   &pins_fops);
	success_cnt += !google_pinctrl_debugfs_create_file("valid-mask", 0400, *pinctl_de, gctl,
							   &valid_mask_fops);

	success_cnt += !google_pinctrl_debugfs_create_bool("suspend-dump", 0600, *pinctl_de,
							   gctl, &gctl->suspend_dump_enabled);
	success_cnt += !google_pinctrl_debugfs_create_bool("resume-dump", 0600, *pinctl_de,
							   gctl, &gctl->resume_dump_enabled);
	success_cnt += !google_pinctrl_debugfs_create_bool("gpio-set-check", 0600, *pinctl_de,
							   gctl, &gctl->set_check_enabled);

	raw_spin_lock_init(&gctl->pins_rpm_stats_lock);
	success_cnt += !google_pinctrl_debugfs_create_file("pins-rpm-stats", 0600, *pinctl_de, gctl,
							   &pins_rpm_stats_fops);

	if (gctl->g_pingroups_fops_stats) {
		success_cnt += !google_pinctrl_debugfs_create_file("fops-stats", 0400, *pinctl_de,
								   gctl, &fops_count_fops);
	}

	if (success_cnt == 0)
		debugfs_remove(*pinctl_de);

	return 0;
}

void google_pinctrl_remove_recursive_debugfs(struct google_pinctrl *gctl)
{
	struct pin_rpm_stats *tmp;
	unsigned long flags;

	debugfs_remove_recursive(gctl->de);

	raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);
	tmp = gctl->pins_rpm_stats;
	gctl->pins_rpm_stats = NULL;
	raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

	kvfree(tmp);
}

void google_pinctrl_debugfs_suspend_dump_regs(struct google_pinctrl *gctl)
{
	bool tmp_suspend_dump_enabled;
	unsigned long flags;
	int ret;

	atomic_inc(&gctl->system_suspend_count);

	raw_spin_lock_irqsave(&gctl->lock, flags);
	tmp_suspend_dump_enabled = gctl->suspend_dump_enabled;
	raw_spin_unlock_irqrestore(&gctl->lock, flags);

	if (tmp_suspend_dump_enabled) {
		ret = dump_regs_logs(gctl);
		if (ret < 0)
			dev_err(gctl->dev, "Failed to dump logs: %d\n", ret);
	}
}

void google_pinctrl_debugfs_resume_dump_regs(struct google_pinctrl *gctl)
{
	if (gctl->resume_dump_enabled)
		dump_regs_logs(gctl);
}

int google_pinctrl_debugfs_inc_cnt(struct google_pinctrl *gctl,
				   enum PINCTRL_DEBUGFS_GCTL_CNT cnt_sel, int pin)
{
	struct pin_rpm_stats *pstat = NULL;
	ktime_t now, tmp_start;
	unsigned long flags;
	int ret = 0;
	const int ngroups = gctl->info->num_groups;

	if (pin >= ngroups) {
		dev_err(gctl->dev, "pin index %d out of bounds %d", pin, ngroups);
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&gctl->pins_rpm_stats_lock, flags);

	if (gctl->pins_rpm_stats && pin >= 0)
		pstat = &gctl->pins_rpm_stats[pin];
	else
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

	switch (cnt_sel) {
	case RPM_GET_CNT:
		atomic_inc(&gctl->rpm_get_count);

		if (pstat) {
			++(pstat->rpm_get_cnt);

			now = ktime_get_boottime();
			tmp_start = pstat->sec_start;

			if (now >= (tmp_start + NSEC_PER_SEC)) {
				pstat->gets_per_sec = pstat->gets_sec_cnt;
				pstat->gets_sec_cnt = 0;
				pstat->sec_start = now;
			}

			if (now >= (tmp_start + 2 * NSEC_PER_SEC))
				pstat->gets_per_sec = 0;

			++(pstat->gets_sec_cnt);
		}

		break;
	case RPM_PUT_CNT:
		atomic_inc(&gctl->rpm_put_count);

		if (pstat)
			++(pstat->rpm_put_cnt);

		break;
	case RPM_SUSP_CNT:
		atomic_inc(&gctl->rpm_suspend_count);
		break;
	case RPM_RESM_CNT:
		atomic_inc(&gctl->rpm_resume_count);
		break;
	case SYS_SUSP_CNT:
		atomic_inc(&gctl->system_suspend_count);
		break;
	case SYS_RESM_CNT:
		atomic_inc(&gctl->system_resume_count);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	if (pstat)
		raw_spin_unlock_irqrestore(&gctl->pins_rpm_stats_lock, flags);

	return ret;
}

int google_pinctrl_debugfs_inc_fops_cnt(struct google_pinctrl *gctl,
					enum PINCTRL_DEBUGFS_FOPS_CNT cnt_sel, unsigned int g_sel)
{
	if (!gctl->g_pingroups_fops_stats)
		return -EFAULT;

	switch (cnt_sel) {
	case DIR_I_CNT:
		atomic_inc(&gctl->g_pingroups_fops_stats[g_sel].dir_input_count);
		break;
	case DIR_O_CNT:
		atomic_inc(&gctl->g_pingroups_fops_stats[g_sel].dir_output_count);
		break;
	case DIR_GET_CNT:
		atomic_inc(&gctl->g_pingroups_fops_stats[g_sel].dir_get_count);
		break;
	case GET_CNT:
		atomic_inc(&gctl->g_pingroups_fops_stats[g_sel].get_count);
		break;
	case SET_CNT:
		atomic_inc(&gctl->g_pingroups_fops_stats[g_sel].set_count);
		break;
	case CFG_CNT:
		atomic_inc(&gctl->g_pingroups_fops_stats[g_sel].config_count);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
