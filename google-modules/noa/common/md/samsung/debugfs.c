// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEP/NCP Modem DebugFs
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>
 */
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <common/core.h>
#include <ncp/md/samsung/ncp_md_fw.h>
#include <noa.h>

#define to_entry(_kobj) (container_of(_kobj, struct noa_core_entry, kobj))
#define to_attr(_attr) (container_of(_attr, struct noa_md_kobj_attr, attr))

struct noa_md_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct noa_md_client *, char *);
	ssize_t (*store)(struct noa_md_client *, const char *, size_t count);
};

static ssize_t noa_md_dump_dev_q(struct noa_md_client *p_client, char *buf)
{
	struct ncp_md_adaptor *p_adaptor =
		p_client->p_md_adaptor;
	struct noa_pktproc_queue_dl *p_dev_q =
		p_adaptor->p_noa_ppa_dl->dev_q[0];
	struct mr_pktproc_queue_ul *p_dev_q_ul =
		p_adaptor->p_mr_ppa_ul->dev_q[1];
	struct noa_ring *p_noa2md_ring =
		p_adaptor->p_noa2md_ring;
	int len = PAGE_SIZE;
	int cnt = 0;
	cnt += scnprintf(buf + cnt, len - cnt, "==== MD DEV DL Q INFO ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "fore(%d), rear(%d), done(%d)\n",
		*p_dev_q->fore_ptr,
		*p_dev_q->rear_ptr,
		p_dev_q->done_ptr);

	cnt += scnprintf(buf + cnt, len - cnt, "==== MD DEV UL Q[1] INFO ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "fore(%d), rear(%d), done(%d), ul ref id[%d](%d)\n",
		*p_dev_q_ul->fore_ptr,
		*p_dev_q_ul->rear_ptr,
		p_dev_q_ul->done_ptr,
		p_noa2md_ring->read,
		get_ul_ref_idx(p_noa2md_ring->read));
	return cnt;
}

static struct noa_md_kobj_attr attr_dump_dev_q =
	__ATTR(dump_dev_q, 0664, noa_md_dump_dev_q, NULL);

static struct attribute *default_file_attrs[] = {
	&attr_dump_dev_q.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t noa_md_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct noa_core_entry *entry = to_entry(kobj);
	struct noa_md_client *p_client = (struct noa_md_client *)entry->priv;
	struct noa_md_kobj_attr *noa_md_attr = to_attr(attr);

	if (noa_md_attr->show)
		return noa_md_attr->show(p_client, buf);
	return -EIO;
}

static ssize_t noa_md_sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	struct noa_core_entry *entry = to_entry(kobj);
	struct noa_md_client *p_client = (struct noa_md_client *)entry->priv;
	struct noa_md_kobj_attr *noa_md_attr = to_attr(attr);

	if (noa_md_attr->store)
		return noa_md_attr->store(p_client, buf, count);
	return -EIO;
}

static struct sysfs_ops noa_md_sysfs_ops = {
	.show = noa_md_sysfs_show,
	.store = noa_md_sysfs_store,
};

struct kobj_type noa_md_ktype = {
	.sysfs_ops = &noa_md_sysfs_ops,
	.default_groups = default_file_groups,
};
