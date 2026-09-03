// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2023, MediaTek Inc.
 */

#include <linux/rtc.h>
#include <linux/sched/clock.h>
#include <linux/version.h>

#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_memlog.h"
#ifdef CONFIG_TX00_UT_MEMLOG
#include "ut_memlog_fake.h"
#endif

#define MTK_MEMLOG_RESET			1
#define TAG					"MEMLOG"

static unsigned long long mtk_memlog_mask = 0xffffffffffffffffull;
static unsigned int mtk_memlog_resize = 2;
static bool mtk_memlog_enable = true;
static u32 build_info_len;
static DEFINE_IDA(memlog_dev_ids);

#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
extern int mrdump_mini_add_extra_file(unsigned long vaddr, unsigned long paddr,
				      unsigned long size, const char *name);
#else
static int mrdump_mini_add_extra_file(unsigned long vaddr, unsigned long paddr,
				      unsigned long size, const char *name)
{
	return 0;
}
#endif

static void mtk_memlog_reset(struct mtk_md_dev *mdev, int region_act_id)
{
	struct mtk_memlog *mtk_mlog = mdev->memlog;
	struct mtk_memlog_region *region;
	unsigned char buf_idx;

	if (unlikely(!mtk_mlog || region_act_id >= mtk_mlog->region_cnt))
		return;

	region = &mtk_mlog->region[region_act_id];
	if (!(region->flag & MTK_MEMLOG_F_EXCLUSIVE)) {
		spin_lock_bh(&region->lock);
		for (buf_idx = 0; buf_idx < mtk_mlog->total_buf_cnt; buf_idx++) {
			memset(mtk_mlog->buffer[buf_idx] + region->base_offset, 0, region->len);
			region->pos = 0;
			region->buf_idx = 0;
		}
		spin_unlock_bh(&region->lock);
	} else {
		for (buf_idx = 0; buf_idx < mtk_mlog->total_buf_cnt; buf_idx++) {
			memset(mtk_mlog->buffer[buf_idx] + region->base_offset, 0, region->len);
			region->pos = 0;
			region->buf_idx = 0;
		}
	}
}

static void mtk_memlog_update_time(struct mtk_memlog *mtk_mlog)
{
	struct timespec64 ts64 = {0};
	u64 ts_nsec, rem_nsec;
	struct rtc_time rt;

	ktime_get_real_ts64(&ts64);
	ts64.tv_sec = ts64.tv_sec - (time64_t)sys_tz.tz_minuteswest * 60;
	rtc_time64_to_tm(ts64.tv_sec, &rt);
	ts_nsec = local_clock();
	rem_nsec = do_div(ts_nsec, 1000000000);
	snprintf(mtk_mlog->buffer[0] + build_info_len,
		 MTK_DFLT_HEADER_LEN >> mtk_mlog->buf_divide,
		 "[%d-%02d-%02d %02d:%02d:%02d.%03d],[%5lu.%06lu]\n",
		 rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
		 rt.tm_hour, rt.tm_min, rt.tm_sec, (unsigned int)ts64.tv_nsec / 1000,
		 (unsigned long)ts_nsec, (unsigned long)rem_nsec / 1000);
}

#ifdef CONFIG_MTK_MEMLOG_EVENT_SUPPORT
void mtk_memlog_add_info(struct memlog_event_msg *event_msg)
{
	struct memlog_add_info *add_info;

	add_info = (struct memlog_add_info *)(&event_msg->add_info);
	add_info->time_stamp = local_clock();
	add_info->cpu_id = get_cpu();
	put_cpu();
	add_info->pid_no = current->pid;
}
EXPORT_SYMBOL(mtk_memlog_add_info);

static void *mtk_memlog_req_address_actual(struct mtk_memlog *mtk_mlog,
					   struct mtk_memlog_region *region,
					   u32 event_size)
{
	unsigned char cur_buf_idx, next_buf_idx = 0;
	struct memlog_event_msg *event_msg;

	cur_buf_idx = region->buf_idx;

	if (region->pos + event_size > region->len) {
		if (cur_buf_idx + 1 == mtk_mlog->total_buf_cnt)
			next_buf_idx = 0;
		else
			next_buf_idx = cur_buf_idx + 1;
	}

	if (region->pos + event_size > region->len) {
		if ((cur_buf_idx + 1 < mtk_mlog->total_buf_cnt ||
		     region->flag & MTK_MEMLOG_F_RING) &&
		    event_size <= region->len) {
			region->pos = 0;
			region->buf_idx = next_buf_idx;
		} else {
			return NULL;
		}
	}

	event_msg = (struct memlog_event_msg *)(mtk_mlog->buffer[region->buf_idx] +
						region->base_offset + region->pos);

	if (region->flag & MTK_MEMLOG_F_ADDINFO)
		mtk_memlog_add_info(event_msg);

	region->pos += event_size;

	return (void *)event_msg;
}

/**
 * mtk_memlog_req_address() - Request memory address to record event
 * @mdev: pointer to mtk_md_dev
 * @region_id: id of the region where event is to be written
 * @event_size: size of the event to be written
 *
 * Return: return memory address on success, NULL on failure
 *
 */
void *mtk_memlog_req_address(struct mtk_md_dev *mdev,
			     enum mtk_memlog_region_id region_id, u32 event_size)
{
	struct mtk_memlog_region *region;
	struct mtk_memlog *mtk_mlog;
	void *event_address;
	int region_act_id;

	if (unlikely(!mdev || !mdev->memlog))
		return NULL;

	if (unlikely(region_id >= MTK_MEMLOG_RG_MAX ||
		     !test_bit(region_id, (const unsigned long *)&mtk_memlog_mask)))
		return NULL;

	mtk_mlog = mdev->memlog;
	region_act_id = mtk_mlog->region_id_tbl[region_id];
	region = &mtk_mlog->region[region_act_id];

	if (!(region->flag & MTK_MEMLOG_F_EXCLUSIVE)) {
		spin_lock_bh(&region->lock);
		event_address = mtk_memlog_req_address_actual(mtk_mlog, region, event_size);
		if (!event_address)
			spin_unlock_bh(&region->lock);
		return event_address;
	} else {
		return mtk_memlog_req_address_actual(mtk_mlog, region, event_size);
	}
}
EXPORT_SYMBOL(mtk_memlog_req_address);

/**
 * mtk_memlog_req_done() - Unlock the memlog region
 * @mdev: pointer to mtk_md_dev
 * @region_id: id of the region to be unlocked
 */
void mtk_memlog_req_done(struct mtk_md_dev *mdev,
			 enum mtk_memlog_region_id region_id)
{
	struct mtk_memlog_region *region;
	struct mtk_memlog *mtk_mlog;
	int region_act_id;

	if (unlikely(!mdev || !mdev->memlog))
		return;

	if (unlikely(region_id >= MTK_MEMLOG_RG_MAX ||
		     !test_bit(region_id, (const unsigned long *)&mtk_memlog_mask)))
		return;

	mtk_mlog = mdev->memlog;
	region_act_id = mtk_mlog->region_id_tbl[region_id];
	region = &mtk_mlog->region[region_act_id];

	if (!(region->flag & MTK_MEMLOG_F_EXCLUSIVE))
		spin_unlock_bh(&region->lock);
}
EXPORT_SYMBOL(mtk_memlog_req_done);

#endif

static void mtk_memlog_write_actual(struct mtk_memlog *mtk_mlog, struct mtk_memlog_region *region,
				    const char *fmt, ...)
{
	unsigned char cur_buf_idx, next_buf_idx = 0;
	u64 ts_nsec, rem_nsec;
	u32 write_len = 0;
	va_list args;
	int this_cpu;

	if (region->flag & MTK_MEMLOG_F_ADDINFO) {
		ts_nsec = local_clock();
		rem_nsec = do_div(ts_nsec, 1000000000);
		this_cpu = get_cpu();
		put_cpu();
		write_len = snprintf(region->tmp_log, MTK_MEMLOG_LINE_MAX_LENGTH,
				     "[%5lu.%06lu](%d)[%d:%s]",
				     (unsigned long)ts_nsec, (unsigned long)rem_nsec / 1000,
				     this_cpu, current->pid, current->comm);
	}

	va_start(args, fmt);
	write_len += vsnprintf(region->tmp_log + write_len,
			       MTK_MEMLOG_LINE_MAX_LENGTH - write_len, fmt, args);
	va_end(args);

	cur_buf_idx = region->buf_idx;
	if (region->pos + write_len > region->len) {
		if (cur_buf_idx + 1 == mtk_mlog->total_buf_cnt) {
			mtk_memlog_update_time(mtk_mlog);
			next_buf_idx = 0;
		} else {
			next_buf_idx = cur_buf_idx + 1;
		}
	}

	if (region->pos + write_len <= region->len) {
		memcpy(mtk_mlog->buffer[cur_buf_idx] + region->base_offset +
		       region->pos, region->tmp_log, write_len);
		region->pos += write_len;
	} else if ((cur_buf_idx + 1 < mtk_mlog->total_buf_cnt ||
		    region->flag & MTK_MEMLOG_F_RING) &&
		   write_len <= region->len) {
		memset(mtk_mlog->buffer[cur_buf_idx] + region->base_offset +
		       region->pos, 0, region->len - region->pos);
		region->pos = 0;
		memcpy(mtk_mlog->buffer[next_buf_idx] + region->base_offset +
		       region->pos, region->tmp_log, write_len);
		region->pos += write_len;
		region->buf_idx = next_buf_idx;
	}
}

void mtk_memlog_write(struct mtk_md_dev *mdev,
		      enum mtk_memlog_region_id region_id, const char *fmt, ...)
{
	struct mtk_memlog_region *region;
	struct mtk_memlog *mtk_mlog;
	struct va_format vaf = {
		.fmt = fmt,
	};
	int region_act_id;
	va_list args;

	if (unlikely(!mdev || !mdev->memlog))
		return;

	if (unlikely(region_id >= MTK_MEMLOG_RG_MAX ||
		     !test_bit(region_id, (const unsigned long *)&mtk_memlog_mask)))
		return;

	mtk_mlog = mdev->memlog;

	region_act_id = mtk_mlog->region_id_tbl[region_id];
	region = &mtk_mlog->region[region_act_id];
	va_start(args, fmt);
	vaf.va = &args;
	if (!(region->flag & MTK_MEMLOG_F_EXCLUSIVE)) {
		spin_lock_bh(&region->lock);
		mtk_memlog_write_actual(mtk_mlog, region, "%pV", &vaf);
		spin_unlock_bh(&region->lock);
	} else {
		mtk_memlog_write_actual(mtk_mlog, region, "%pV", &vaf);
	}
	va_end(args);
}
EXPORT_SYMBOL(mtk_memlog_write);

static int mtk_memlog_proc_open(struct inode *inode, struct file *file)
{
#if (KERNEL_VERSION(5, 17, 0) <= LINUX_VERSION_CODE)
	struct mtk_memlog *mtk_mlog = pde_data(inode);
#else
	struct mtk_memlog *mtk_mlog = PDE_DATA(inode);
#endif

	file->private_data = mtk_mlog;
	nonseekable_open(inode, file);

	return 0;
}

static int mtk_memlog_proc_close(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static ssize_t mtk_memlog_proc_read(struct file *file, char __user *buf,
				    size_t size, loff_t *ppos)
{
	struct mtk_memlog *mtk_mlog = file->private_data;
	u32 buf_offset, total_left;
	size_t buf_left, to_read;
	unsigned char buf_idx;
	int ret;

	if (!mtk_mlog || *ppos >= mtk_mlog->len)
		return 0;

	if (!*ppos)
		mtk_memlog_update_time(mtk_mlog);

	total_left = mtk_mlog->len - *ppos;
	buf_idx = *ppos / mtk_mlog->single_buf_size;
	buf_offset = *ppos % mtk_mlog->single_buf_size;
	buf_left = total_left % mtk_mlog->single_buf_size;
	if (buf_left == 0)
		buf_left = mtk_mlog->single_buf_size;

	to_read = min(buf_left, size);
	/* Last log line may be incomplete since the spin lock is not held here */
	ret = copy_to_user(buf, mtk_mlog->buffer[buf_idx] + buf_offset, to_read);
	if (ret) {
		MTK_ERR(mtk_mlog->mdev, "Failed to copy data to user, ret=%d", ret);
		return -EFAULT;
	}

	*ppos += to_read;
	return to_read;
}

static __poll_t mtk_memlog_proc_poll(struct file *file, struct poll_table_struct *poll)
{
	__poll_t mask;

	mask = EPOLLIN | EPOLLRDNORM;
	return mask;
}

#if (KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE)
static const struct proc_ops mtk_memlog_fops = {
	.proc_open = mtk_memlog_proc_open,
	.proc_release = mtk_memlog_proc_close,
	.proc_read = mtk_memlog_proc_read,
	.proc_poll = mtk_memlog_proc_poll,
};
#else
static const struct file_operations mtk_memlog_fops = {
	.open = mtk_memlog_proc_open,
	.release = mtk_memlog_proc_close,
	.read = mtk_memlog_proc_read,
	.poll = mtk_memlog_proc_poll,
};
#endif

static ssize_t memlog_dbg_ctrl(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_memlog *mtk_mlog = data;
	int err, cmd, i;

	err = kstrtoint(buf, 10, &cmd);
	if (err)
		return cnt;

	switch (cmd) {
	case MTK_MEMLOG_RESET:
		for (i = 0; i < mtk_mlog->region_cnt; i++)
			mtk_memlog_reset(mtk_mlog->mdev, i);
		mtk_memlog_write(mtk_mlog->mdev, MTK_MEMLOG_RG_COMMON,
				 "Memlog reset buffer done\n");
		break;
	default:
		break;
	}

	return cnt;
}

MTK_DBGFS(memlog_ctrl, NULL, memlog_dbg_ctrl);

static inline void mtk_memlog_dbgfs_init(struct mtk_memlog *mtk_mlog)
{
#define MEMLOG_DBGFS_NAME_LEN	32
	char name[MEMLOG_DBGFS_NAME_LEN] = {0};

	snprintf(name, MEMLOG_DBGFS_NAME_LEN, "tmi_log_ctrl%d", mtk_mlog->dev_id);
	mtk_mlog->dentry = mtk_dbgfs_create_dir(mtk_get_dev_dentry(mtk_mlog->mdev), name);
	if (!mtk_mlog->dentry)
		return;

	mtk_dbgfs_create_file(mtk_mlog->dentry, &mtk_dbgfs_memlog_ctrl, mtk_mlog);
}

static inline void mtk_memlog_dbgfs_exit(struct mtk_memlog *mtk_mlog)
{
	mtk_dbgfs_remove(mtk_mlog->dentry);
}

static int mtk_memlog_alloc_buffer(struct mtk_memlog *mtk_mlog,
				   unsigned char orig_buf_cnt, u32 orig_buf_size)
{
	unsigned char buf_cnt;
	int is_kalloc_buf = 1;

retry_alloc:
	buf_cnt = 0;
	while (buf_cnt < orig_buf_cnt << mtk_mlog->buf_divide) {
		if (is_kalloc_buf)
			mtk_mlog->buffer[buf_cnt] = kzalloc(orig_buf_size >>
							    mtk_mlog->buf_divide, GFP_KERNEL);
		else
			mtk_mlog->buffer[buf_cnt] = vzalloc(orig_buf_size >>
							    mtk_mlog->buf_divide);
		if (unlikely(!mtk_mlog->buffer[buf_cnt])) {
			MTK_ERR(mtk_mlog->mdev, "Failed to create memlog buffer, length:%d\n",
				orig_buf_size >> mtk_mlog->buf_divide);
			for (; buf_cnt > 0; buf_cnt--) {
				if (is_kalloc_buf)
					kfree(mtk_mlog->buffer[buf_cnt - 1]);
				else
					vfree(mtk_mlog->buffer[buf_cnt - 1]);
			}
			mtk_mlog->buf_divide++;
			if ((orig_buf_size >> mtk_mlog->buf_divide) < MTK_BUFFER_MIN_SIZE ||
			    (orig_buf_cnt << mtk_mlog->buf_divide) > MTK_MAX_MEMLOG_BUF_CNT) {
				if (is_kalloc_buf) {
					is_kalloc_buf = 0;
					mtk_mlog->buf_divide = 0;
				} else {
					return -ENOMEM;
				}
			}
			goto retry_alloc;
		}
		buf_cnt++;
	}
	return is_kalloc_buf;
}

int mtk_memlog_init(struct mtk_md_dev *mdev, char *build_time_str)
{
	char name[MTK_DFLT_MEMLOG_ATTR_NAME_LEN];
	unsigned char buf_cnt = 0, orig_buf_cnt;
	struct mtk_memlog_cfg *memlog_cfg;
	struct mtk_memlog *mtk_mlog;
	u32 pos = 0, orig_buf_size;
	u32 region_header_len;
	int i, ret, resize;

	if (!mtk_memlog_enable)
		return 0;

	memlog_cfg = mdev->utility_cfg->memlog_cfg;

	if (mtk_memlog_resize > 4) {
		MTK_ERR(mdev, "Failed to resize memlog, out of range, valid range:0 ~ 4\n");
		return -ENOMEM;
	}

	if (memlog_cfg->default_resize < 0 || memlog_cfg->default_resize > 4)
		resize = mtk_memlog_resize;
	else
		resize = max(mtk_memlog_resize, (unsigned int)memlog_cfg->default_resize);

	mtk_mlog = kzalloc(sizeof(*mtk_mlog) +
			   sizeof(struct mtk_memlog_region) * memlog_cfg->region_cnt,
			   GFP_KERNEL);
	if (unlikely(!mtk_mlog)) {
		MTK_ERR(mdev, "Failed to create memlog\n");
		return -ENOMEM;
	}

	mtk_mlog->mdev = mdev;
	mdev->memlog = mtk_mlog;
	mtk_mlog->region_cnt = memlog_cfg->region_cnt;
	mtk_mlog->region_id_tbl = memlog_cfg->region_id_tbl;

	mtk_mlog->buf_divide = 0;
	for (i = 0; i < mtk_mlog->region_cnt; i++)
		memlog_cfg->region_cfg[i].region_size =
			ALIGN_DOWN(memlog_cfg->region_cfg[i].region_size, L1_CACHE_BYTES);

	mtk_mlog->len = MTK_BUFFER_BASE_SIZE << resize;
	orig_buf_cnt =
		(mtk_mlog->len / MTK_BUDDY_MAX_SIZE) ? (mtk_mlog->len / MTK_BUDDY_MAX_SIZE) : 1;
	orig_buf_size = mtk_mlog->len / orig_buf_cnt;

	ret = mtk_memlog_alloc_buffer(mtk_mlog, orig_buf_cnt, orig_buf_size);
	if (ret < 0)
		goto err_alloc_buffer;

	mtk_mlog->is_kalloc_buf = ret;
	mtk_mlog->total_buf_cnt = orig_buf_cnt << mtk_mlog->buf_divide;
	mtk_mlog->single_buf_size = orig_buf_size >> mtk_mlog->buf_divide;
	while (buf_cnt < mtk_mlog->total_buf_cnt) {
		snprintf(name, MTK_DFLT_MEMLOG_ATTR_NAME_LEN, "tmi_buffer%d", buf_cnt);
		mrdump_mini_add_extra_file((unsigned long)mtk_mlog->buffer[buf_cnt],
					   mtk_mlog->is_kalloc_buf ?
					   __pa_nodebug(mtk_mlog->buffer[buf_cnt]) : 0,
					   mtk_mlog->single_buf_size, name);
		MTK_INFO(mdev, "memlog buffer[%d] addr:%p, size:%dKB\n", buf_cnt,
			 mtk_mlog->buffer[buf_cnt], mtk_mlog->single_buf_size >> 10);
		buf_cnt++;
	}

	ret = snprintf(mtk_mlog->buffer[0], MTK_DFLT_BUILD_INFO_HEADER_LEN, "%s\n", build_time_str);
	build_info_len = min(ret, MTK_DFLT_BUILD_INFO_HEADER_LEN);
	mtk_memlog_update_time(mtk_mlog);

	pos += MTK_DFLT_HEADER_LEN >> mtk_mlog->buf_divide;
	for (i = 0; i < mtk_mlog->region_cnt; i++) {
		mtk_mlog->region[i].base_offset = pos;
		buf_cnt = 0;
		while (buf_cnt < mtk_mlog->total_buf_cnt) {
			ret = snprintf(mtk_mlog->buffer[buf_cnt] + mtk_mlog->region[i].base_offset,
				       MTK_REGION_HEADER_LEN, "\n==========%s 0x%x==========\n",
				       memlog_cfg->region_cfg[i].name,
				       memlog_cfg->region_cfg[i].flag);
			buf_cnt++;
		}
		region_header_len = min(ret, MTK_REGION_HEADER_LEN);
		mtk_mlog->region[i].base_offset += region_header_len;
		mtk_mlog->region[i].tmp_log = kzalloc(MTK_MEMLOG_LINE_MAX_LENGTH, GFP_KERNEL);
		if (unlikely(!mtk_mlog->region[i].tmp_log)) {
			MTK_ERR(mdev, "Failed to create memlog tmp line buffer\n");
			goto err_alloc_tmp;
		}

		mtk_mlog->region[i].buf_idx = 0;
		mtk_mlog->region[i].flag = memlog_cfg->region_cfg[i].flag;
		mtk_mlog->region[i].len = (memlog_cfg->region_cfg[i].region_size /
					  (MTK_BUDDY_MAX_SIZE / mtk_mlog->single_buf_size)) -
					  region_header_len;
		if (!(mtk_mlog->region[i].flag & MTK_MEMLOG_F_EXCLUSIVE))
			spin_lock_init(&mtk_mlog->region[i].lock);
		pos += mtk_mlog->region[i].len + region_header_len;
	}

	mtk_mlog->dev_id = ida_alloc_range(&memlog_dev_ids, 0,
					   MTK_DFLT_MAX_MEMLOG_DEV_CNT - 1,
					   GFP_KERNEL);
	snprintf(name, MTK_DFLT_MEMLOG_ATTR_NAME_LEN, "tmi_log%d", mtk_mlog->dev_id);

	mtk_memlog_dbgfs_init(mtk_mlog);
	mtk_mlog->proc_entry = proc_create_data(name, 0440, NULL, &mtk_memlog_fops, mtk_mlog);
	if (unlikely(!mtk_mlog->proc_entry)) {
		MTK_ERR(mdev, "Failed to create memlog proc entry\n");
		goto err_memlog;
	}

	return 0;

err_memlog:
	ida_free(&memlog_dev_ids, mtk_mlog->dev_id);
err_alloc_tmp:
	for (i--; i >= 0; i--)
		kfree(mtk_mlog->region[i].tmp_log);

	for (buf_cnt = 0; buf_cnt < mtk_mlog->total_buf_cnt; buf_cnt++) {
		if (mtk_mlog->is_kalloc_buf)
			kfree(mtk_mlog->buffer[buf_cnt]);
		else
			vfree(mtk_mlog->buffer[buf_cnt]);
	}
err_alloc_buffer:
	kfree(mtk_mlog);
	return -ENOMEM;
}
EXPORT_SYMBOL(mtk_memlog_init);

void mtk_memlog_exit(struct mtk_md_dev *mdev)
{
	struct mtk_memlog *mtk_mlog;
	unsigned char buf_cnt;
	int i;

	if (!mdev->memlog)
		return;

	mtk_mlog = mdev->memlog;
	mtk_memlog_dbgfs_exit(mtk_mlog);
	ida_free(&memlog_dev_ids, mtk_mlog->dev_id);
	proc_remove(mtk_mlog->proc_entry);
	for (i = 0; i < mtk_mlog->region_cnt; i++)
		kfree(mtk_mlog->region[i].tmp_log);

	for (buf_cnt = 0; buf_cnt < mtk_mlog->total_buf_cnt; buf_cnt++) {
		if (mtk_mlog->is_kalloc_buf)
			kfree(mtk_mlog->buffer[buf_cnt]);
		else
			vfree(mtk_mlog->buffer[buf_cnt]);
	}

	kfree(mtk_mlog);
	mdev->memlog = NULL;
}
EXPORT_SYMBOL(mtk_memlog_exit);

module_param(mtk_memlog_resize, uint, 0644);
MODULE_PARM_DESC(mtk_memlog_resize, "The value is used to resize tmi log buffer.");
module_param(mtk_memlog_mask, ullong, 0644);
MODULE_PARM_DESC(mtk_memlog_mask, "The value is used to control mtk tmi log.");
module_param(mtk_memlog_enable, bool, 0644);
MODULE_PARM_DESC(mtk_memlog_enable, "This value is used to enable memlog\n");
