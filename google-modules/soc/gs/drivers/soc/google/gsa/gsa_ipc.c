// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for the Google GSA IPC subsystem.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/cdev.h>
#include <linux/dma-buf.h>
#include <linux/gsa.h>
#include <linux/gsa/gsa_ipc.h>
#include <linux/file.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/rbtree.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#include <uapi/linux/trusty/ipc.h>

#include "gsa_mbox.h"
#include "gsa_priv.h"

#define MAX_DEVICES 1

static struct class *gsa_cdev_class;
static dev_t gsa_cdev_base_num;
static DEFINE_IDR(gsa_cdev_devices);

/* DMA buf integration callbacks  */
static u64 (*dma_buf_get_ffa_tag)(struct dma_buf *dma_buf);
static int (*dma_buf_get_shared_mem_id)(struct dma_buf *dma_buf, u64 *id, u64 *poff);

struct gsa_ipc_dev_state {
	struct device *dev;
	struct gsa_cdev cdn;
	void *bbuf;
	size_t bbuf_sz;
	size_t bbuf_len;
	dma_addr_t bbuf_da;
	struct mutex ipc_lock;
	struct list_head chan_list;
	struct work_struct ipc_irq_work;
	struct rb_root shared_handles;
	struct idr shared_handles_idr;
	struct mutex idr_lock; /* protects IDR operations */
	struct mutex rb_lock; /* protects RB tree */
};

#define MAX_SRV_NAME_LEN	128

struct gsa_ipc_chan {
	struct list_head node;
	struct device *ipc_dev;
	u32 chan_id;
	bool connected;
	bool send_blocked;
	bool recv_has_msg;
	bool peer_closed;
	wait_queue_head_t sendq;
	wait_queue_head_t recvq;
	char srv_name[MAX_SRV_NAME_LEN];
};

static int translate_ipc_chan_err(struct gsa_ipc_chan *ch, int32_t ipc_err)
{
	/* Check Error */
	switch (ipc_err) {
	case GSA_IPC_ERR_NONE:
		return 0;

	case GSA_IPC_ERR_CHANNEL_NOT_READY:
		ch->connected = false;
		dev_dbg(ch->ipc_dev, "Chan (%u): connection is not ready\n", ch->chan_id);
		return -ENOTCONN;

	case GSA_IPC_ERR_CHANNEL_CLOSED:
		ch->connected = true;
		ch->peer_closed = true;
		wake_up_interruptible(&ch->sendq);
		wake_up_interruptible(&ch->recvq);
		dev_dbg(ch->ipc_dev, "Chan (%u): closed by peer\n", ch->chan_id);
		return -ESHUTDOWN;

	case GSA_IPC_ERR_CHANNEL_NO_MSG:
		ch->recv_has_msg = false;
		dev_dbg(ch->ipc_dev, "Chan (%u): no msg\n", ch->chan_id);
		return -EAGAIN;

	case GSA_IPC_ERR_CHANNEL_SEND_BLOCKED:
		ch->send_blocked = true;
		dev_dbg(ch->ipc_dev, "Chan (%u): send blocked\n", ch->chan_id);
		return -EAGAIN;

	default:
		dev_err(ch->ipc_dev, "Chan (%u): unhandled err (%d)\n", ch->chan_id, ipc_err);
		return -EIO;
	}
}

struct gsa_ipc_chan *gsa_ipc_chan_create(struct device *ipc_dev)
{
	struct gsa_ipc_chan *chan;
	struct gsa_ipc_dev_state *st;

	if (!ipc_dev)
		return NULL;

	get_device(ipc_dev);

	st = dev_get_drvdata(ipc_dev);
	if (!st)
		goto err_bad_dev;

	chan = kzalloc(sizeof(struct gsa_ipc_chan), GFP_KERNEL);
	if (!chan)
		goto err_alloc;

	chan->ipc_dev = ipc_dev;

	/* Add to channel list */
	mutex_lock(&st->ipc_lock);
	list_add_tail(&chan->node, &st->chan_list);
	init_waitqueue_head(&chan->sendq);
	init_waitqueue_head(&chan->recvq);
	mutex_unlock(&st->ipc_lock);

	return chan;

err_alloc:
err_bad_dev:
	put_device(ipc_dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(gsa_ipc_chan_create);

/*
 *  Channel Connect
 */
static int ipc_chan_connect_locked(struct gsa_ipc_chan *chan)
{
	int rc;
	ssize_t len;
	u32 ipc_err;
	u32 req[GSA_IPC_CONNECT_REQ_ARGC] = {};
	u32 rsp[GSA_IPC_CONNECT_RSP_ARGC] = {};
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	dev_dbg(chan->ipc_dev, "Connecting to '%s'\n", chan->srv_name);

	/* Make sure SRV name fits the buffer we are using to pass it to GSA  */
	len = strscpy(st->bbuf, chan->srv_name, st->bbuf_sz);
	if (len < 0) {
		dev_err_ratelimited(chan->ipc_dev, "IPC service name is too long\n");
		return -ENAMETOOLONG;
	}

	/* Build GSA mbox request */
	req[GSA_IPC_CONNECT_REQ_ADDR_LO_IDX] = lower_32_bits(st->bbuf_da);
	req[GSA_IPC_CONNECT_REQ_ADDR_HI_IDX] = upper_32_bits(st->bbuf_da);
	req[GSA_IPC_CONNECT_REQ_LEN_IDX] = len;
	req[GSA_IPC_CONNECT_REQ_COOKIE_LO_IDX] = 0; /* unused */
	req[GSA_IPC_CONNECT_REQ_COOKIE_HI_IDX] = 0; /* unused */

	rc = gsa_send_cmd(chan->ipc_dev->parent, GSA_MB_CMD_IPC_CONNECT,
			  req, ARRAY_SIZE(req), rsp, ARRAY_SIZE(rsp));
	if (rc < 0) {
		dev_err_ratelimited(chan->ipc_dev, "Connect to '%s' failed (%d)\n",
				    chan->srv_name, rc);
		return rc;
	}

	if (rc != GSA_IPC_CONNECT_RSP_ARGC) {
		dev_err_ratelimited(chan->ipc_dev, "Unexpected (%d) response arg count\n", rc);
		return -EIO;
	}

	/* Check result */
	ipc_err = rsp[GSA_IPC_CONNECT_RSP_ERR_IDX];

	if (ipc_err != GSA_IPC_ERR_NONE) {
		if (ipc_err == GSA_IPC_ERR_PORT_NOT_FOUND) {
			dev_err_ratelimited(chan->ipc_dev, "Port '%s' not found\n",
					    chan->srv_name);
			return -ENOENT;
		}

		dev_err_ratelimited(chan->ipc_dev, "Unhandled ipc err (%u)\n", ipc_err);
		return -EIO;
	}

	chan->chan_id = rsp[GSA_IPC_CONNECT_RSP_HANDLE_IDX];

	dev_dbg(chan->ipc_dev, "IPC chan %u is connected to '%s'\n",
		chan->chan_id, chan->srv_name);

	return 0;
}

static int ipc_chan_connect_from_user_async(struct gsa_ipc_chan *chan, const char __user *usr_name)
{
	int rc;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	mutex_lock(&st->ipc_lock);

	/* Check if we already connected */
	if (chan->chan_id) {
		dev_err_ratelimited(chan->ipc_dev, "Channel (%u) is already connected to '%s'\n",
			chan->chan_id, chan->srv_name);
		rc = -EEXIST;
		goto err;
	}

	/* copy in service name from user space */
	rc = strncpy_from_user(chan->srv_name, usr_name, sizeof(chan->srv_name));
	if (rc < 0) {
		dev_err_ratelimited(chan->ipc_dev, "Failed (%d) to copy in srv name\n", rc);
		goto err;
	}

	if (rc == sizeof(chan->srv_name)) {
		dev_err_ratelimited(chan->ipc_dev, "Requested IPC SRV name is too long\n");
		rc = -ENAMETOOLONG;
		goto err;
	}

	/* Call GSA to connect */
	rc = ipc_chan_connect_locked(chan);

err:
	mutex_unlock(&st->ipc_lock);
	return  rc;
}

static int ipc_chan_connect_from_user(struct gsa_ipc_chan *chan,
				      const char __user *usr_name,
				      bool nonblock)
{
	int rc;

	rc = ipc_chan_connect_from_user_async(chan, usr_name);
	if (rc < 0)
		return rc;

	if (nonblock)
		return rc;

	/* It is sufficient to only check  "connected" condition because
	 * by protocol, "peer_close" can be only set in combination with
	 * "connected".
	 */
	rc = wait_event_interruptible(chan->sendq, chan->connected);
	if (rc == -ERESTARTSYS) {
		/* Initiating async connection cannot be auto restarted
		 * without making it unnecessary complicated. Return -EINT
		 * to force user space to deal with this very unlikely event.
		 */
		return -EINTR; /* should not be restarted */
	}

	return rc;
}

int gsa_ipc_chan_connect(struct gsa_ipc_chan *chan, const char *srv_name)
{
	int rc;
	ssize_t len;
	struct gsa_ipc_dev_state *st;

	if (!chan)
		return -EINVAL;

	if (!srv_name)
		return -EINVAL;

	st = dev_get_drvdata(chan->ipc_dev);

	mutex_lock(&st->ipc_lock);

	/* Check if we already connected */
	if (chan->chan_id) {
		dev_err(chan->ipc_dev, "Channel (%u) is already connected to '%s'\n",
			chan->chan_id, chan->srv_name);
		rc = -EEXIST;
		goto err;
	}

	/* Copy in service name */
	len = strscpy(chan->srv_name, srv_name, sizeof(chan->srv_name));
	if (len < 0) {
		dev_err(chan->ipc_dev, "IPC service name is too long\n");
		rc = -ENAMETOOLONG;
		goto err;
	}

	/* Call GSA to connect */
	rc = ipc_chan_connect_locked(chan);

err:
	mutex_unlock(&st->ipc_lock);
	return  rc;
}
EXPORT_SYMBOL_GPL(gsa_ipc_chan_connect);

/*
 * SHM
 */
#define MAX_SHM_CNT 4

struct ipc_shm_handle {
	struct rb_node node;
	struct dma_buf *dma_buf;
	struct device *ipc_dev;
	u64 mem_id;
	u64 mem_id_off;
	u64 obj_id;
	u64 ffa_tag;
	size_t size;
	bool shared;
	bool writable;
	/*
	 * Following fields are only used if dma_buf does not own a
	 * trusty_shared_mem_id_t.
	 */
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
};

static int encode_shm_handle(u64 *inf, struct ipc_shm_handle *h)
{
	u64 size;
	u64 prot;
	u64 offset;

	*inf = 0;

	/* MemID */
	if (h->mem_id & ~SHM_INF_MEM_ID_MASK) {
		dev_err(h->ipc_dev, "Unsupported shm mem_id range (mem_id = 0x%llx)\n",
			h->mem_id);
		return -EINVAL;
	}
	*inf |= (h->mem_id & SHM_INF_MEM_ID_MASK) << SHM_INF_MEM_ID_SHIFT;

	/* ObjID */
	if (h->obj_id & ~SHM_INF_OBJ_ID_MASK) {
		dev_err(h->ipc_dev, "Unsupported shm obj_id range (obj_id = 0x%llx)\n",
			h->obj_id);
		return -EINVAL;
	}
	*inf |= (h->obj_id & SHM_INF_OBJ_ID_MASK) << SHM_INF_OBJ_ID_SHIFT;

	/* Offset */
	if (!IS_ALIGNED(h->mem_id_off, 1u << SHM_INF_PG_SZ_SHIFT))
		return -EINVAL; /* Not aligned */

	offset = h->mem_id_off >> SHM_INF_PG_SZ_SHIFT;
	if (offset & ~SHM_INF_OFFSET_MASK) {
		dev_err(h->ipc_dev, "Unsupported shm offset (off = 0x%llx)\n", offset);
		return -EINVAL;
	}
	*inf |= (offset & SHM_INF_OFFSET_MASK) << SHM_INF_OFFSET_SHIFT;

	/* Size */
	if (!IS_ALIGNED(h->size, 1u << SHM_INF_PG_SZ_SHIFT))
		return -EINVAL; /* Not aligned */

	size = h->size >> SHM_INF_PG_SZ_SHIFT;
	if (size & ~SHM_INF_SIZE_MASK) {
		dev_err(h->ipc_dev, "Unsupported shm size (size = 0x%llx)\n", size);
		return -EINVAL;
	}
	*inf |= (size & SHM_INF_SIZE_MASK) << SHM_INF_SIZE_SHIFT;

	/* Prot */
	prot = SHM_INF_PROT_RD;
	if (h->writable)
		prot |= SHM_INF_PROT_WR;
	*inf |= prot << SHM_INF_PROT_SHIFT;

	return 0;
}

static u64 gsa_dma_buf_get_ffa_tag(struct dma_buf *dma_buf)
{
	if (dma_buf_get_ffa_tag)
		return dma_buf_get_ffa_tag(dma_buf);

	return 0;
}

static int gsa_dma_buf_get_shared_mem_id(struct dma_buf *dma_buf, u64 *id, u64 *poff)
{
	if (dma_buf_get_shared_mem_id)
		return dma_buf_get_shared_mem_id(dma_buf, id, poff);

	return -ENODATA;
}

static int ipc_shm_protect(struct device *ipc_dev, u64 pa, u64 va, size_t sz, u32 prot_id)
{
	int rc;
	u32 req[GSA_EXT_MEM_PROTECT_REQ_ARGC] = {};
	u32 rsp[GSA_EXT_MEM_PROTECT_RSP_ARGC] = {};

	dev_dbg(ipc_dev, "%s: 0x%zx @ 0x%llx (prot_id = 0x%x)\n", __func__, sz, pa, prot_id);

	if (sz > U32_MAX)
		return -E2BIG;

	/* Build  GSA mbox request */
	req[GSA_EXT_MEM_PROTECT_REQ_PA_BASE_ADDR_LO_IDX] = (u32)(pa);
	req[GSA_EXT_MEM_PROTECT_REQ_PA_BASE_ADDR_HI_IDX] = (u32)(pa >> 32);
	req[GSA_EXT_MEM_PROTECT_REQ_VA_BASE_ADDR_LO_IDX] = (u32)(va);
	req[GSA_EXT_MEM_PROTECT_REQ_VA_BASE_ADDR_HI_IDX] = (u32)(va >> 32);
	req[GSA_EXT_MEM_PROTECT_REQ_SIZE_IDX] = sz;
	req[GSA_EXT_MEM_PROTECT_REQ_PROT_ID_IDX] = prot_id;

	rc = gsa_send_cmd(ipc_dev->parent, GSA_MB_CMD_MEM_PROTECT,
			  req, ARRAY_SIZE(req), rsp, ARRAY_SIZE(rsp));
	if (rc < 0) {
		dev_err(ipc_dev, "Mem Protect failed (%d)\n", rc);
		return rc;
	}

	if (rc != GSA_EXT_MEM_PROTECT_RSP_ARGC) {
		dev_err(ipc_dev, "Unexpected (%d) response arg count\n", rc);
		return -EIO;
	}

	dev_dbg(ipc_dev, "%s: MEM_ID=%d\n", __func__,
		rsp[GSA_EXT_MEM_PROTECT_RSP_HANDLE_IDX]);

	return rsp[GSA_EXT_MEM_PROTECT_RSP_HANDLE_IDX];
}

static int ipc_shm_unprotect(struct device *ipc_dev, u32 mem_id)
{
	int rc;
	u32 req[GSA_EXT_MEM_UNPROTECT_REQ_ARGC] = {};

	dev_dbg(ipc_dev, "%s: MEM_ID=%u\n", __func__, mem_id);

	/* Build  GSA mbox request */
	req[GSA_EXT_MEM_UNPROTECT_REQ_HANDLE_IDX] = mem_id;

	rc = gsa_send_cmd(ipc_dev->parent, GSA_MB_CMD_MEM_UNPROTECT,
			  req, ARRAY_SIZE(req), NULL, 0);
	if (rc < 0) {
		dev_err(ipc_dev, "Mem Unprotect failed (%d)\n", rc);
		return rc;
	}

	return 0;
}

static void split_tag(u64 tag, u32 *prot_id, u64 *pva)
{
	*prot_id = (u32)tag;
	*pva = (tag >> 32);
}

int gsa_transfer_memory(struct device *ipc_dev, u64 *id,
			struct scatterlist *sglist, unsigned int nents,
			pgprot_t pgprot, u64 tag, bool lend)
{
	u64 pa;
	u64 va;
	int rc;
	u32 prot_id;
	size_t len = 0;

	dev_dbg(ipc_dev, "%s: nents = %u, tag = %llx\n", __func__, nents, tag);

	if (WARN_ON(nents < 1))
		return -EINVAL;

	if (nents != 1) {
		dev_err(ipc_dev, "Non-contiguous (%u) memory objects are not supported\n", nents);
		return -EOPNOTSUPP;
	}

	/*
	 * We have exactly one entry in sglist
	 *
	 * The Shared memory GSA traffic always bypass IOMMU and so we do not need
	 * any IOMMU mappings and need raw physical addresses here.
	 */
	pa = sg_phys(sglist);
	len = sglist->length;

	dev_dbg(ipc_dev, "%s: pa = 0x%llx len = 0x%zx\n", __func__, pa, len);

	/* split tag */
	split_tag(tag, &prot_id, &va);

	if (!prot_id || prot_id == PROT_ID_GENERIC_MEM_SHARE) {
		/* override protection id */
		if (lend)
			prot_id = PROT_ID_GENERIC_MEM_LEND;
		else
			prot_id = PROT_ID_GENERIC_MEM_SHARE;
	}

	rc = ipc_shm_protect(ipc_dev, pa, va, len, prot_id);
	if (rc < 0) {
		dev_err(ipc_dev, "Failed (%d) to protect region\n", rc);
		return rc;
	}
	*id = rc;
	dev_dbg(ipc_dev, "%s: id = 0x%llx\n", __func__, *id);

	return 0;
}
EXPORT_SYMBOL_GPL(gsa_transfer_memory);

int gsa_reclaim_memory(struct device *ipc_dev, u64 id,
		       struct scatterlist *sglist, unsigned int nents)
{
	int rc;

	dev_dbg(ipc_dev, "%s: id = 0x%llx\n", __func__, id);

	rc = ipc_shm_unprotect(ipc_dev, id);
	if (rc < 0)
		dev_err(ipc_dev, "Failed (%d) to unprotect region (%u)\n", rc, (u32)id);

	return rc;
}
EXPORT_SYMBOL_GPL(gsa_reclaim_memory);

static void ipc_shm_register_handle_locked(struct gsa_ipc_dev_state *st,
					   struct ipc_shm_handle *h)
{
	struct rb_node **next;
	struct rb_node *parent = NULL;

	mutex_lock(&st->rb_lock);
	next = &st->shared_handles.rb_node;
	while (*next) {
		struct ipc_shm_handle *tmp = rb_entry(*next, struct ipc_shm_handle, node);

		parent = *next;

		if (tmp->obj_id == h->obj_id) {
			/*
			 * All obj_ids are allocated from idr per ipc_shm_handle object
			 * and expected to be unique. We should not be able to get here.
			 */
			WARN(1, "This handle is already registered");
			goto out;
		}

		if (tmp->obj_id > h->obj_id)
			next = &((*next)->rb_left);
		else
			next = &((*next)->rb_right);
	}

	rb_link_node(&h->node, parent, next);
	rb_insert_color(&h->node, &st->shared_handles);

out:
	mutex_unlock(&st->rb_lock);
}

static struct ipc_shm_handle *ipc_shm_unregister_handle_locked(struct gsa_ipc_dev_state *st,
							       u64 obj_id)
{
	struct rb_node *node;

	mutex_lock(&st->rb_lock);
	node = st->shared_handles.rb_node;
	while (node) {
		struct ipc_shm_handle *h = rb_entry(node, struct ipc_shm_handle, node);

		if (obj_id == h->obj_id) {
			rb_erase(node, &st->shared_handles);
			mutex_unlock(&st->rb_lock);
			return h;
		}

		if (obj_id < h->obj_id)
			node = node->rb_left;
		else
			node = node->rb_right;
	}
	mutex_unlock(&st->rb_lock);

	return NULL;
}

static int ipc_shm_handle_drop(struct ipc_shm_handle *h)
{
	int ret;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(h->ipc_dev);

	dev_dbg(h->ipc_dev, "%s: obj_id = %llu\n", __func__, h->obj_id);

	if (h->shared) {
		ret = gsa_reclaim_memory(h->ipc_dev, h->mem_id, h->sgt->sgl, 1);
		if (ret) {
			/*
			 * We can't safely release this, it may still be in
			 * use outside Linux.
			 */
			dev_warn(h->ipc_dev, "Failed to drop handle, leaking...\n");
			return ret;
		}
	}

	if (h->sgt)
		dma_buf_unmap_attachment_unlocked(h->attach, h->sgt, DMA_BIDIRECTIONAL);

	if (h->attach)
		dma_buf_detach(h->dma_buf, h->attach);

	if (h->dma_buf)
		dma_buf_put(h->dma_buf);

	if (h->obj_id) {
		mutex_lock(&st->idr_lock);
		idr_remove(&st->shared_handles_idr, h->obj_id);
		mutex_unlock(&st->idr_lock);
	}

	put_device(h->ipc_dev);

	kfree(h);

	return 0;
}

static int ipc_shm_attach_dma_buf(struct ipc_shm_handle *h,
				  struct device *ipc_dev,
				  struct dma_buf *dma_buf,
				  enum transfer_kind transfer_kind)
{
	int ret;

	/* attach ipc device */
	get_device(ipc_dev);
	h->ipc_dev = ipc_dev;

	/* attach dma buf */
	h->dma_buf = dma_buf;

	/* set writable */
	h->writable = (dma_buf->file->f_mode & FMODE_WRITE) ? 1 : 0;

	/* Attach size */
	h->size = h->dma_buf->size;

	/* Attach tag */
	h->ffa_tag = gsa_dma_buf_get_ffa_tag(h->dma_buf);
	dev_dbg(h->ipc_dev, "%s: ffa_tag = %llx\n", __func__, h->ffa_tag);

	/* Check if we already have mem ID attached to dma_buf */
	ret = gsa_dma_buf_get_shared_mem_id(h->dma_buf, &h->mem_id, &h->mem_id_off);
	dev_dbg(h->ipc_dev, "%s: ret = %d: mem_id = %llx\n", __func__, ret, h->mem_id);

	/*
	 * Buffers with a preallocated mem_id should only be sent to GSA
	 * using TRUSTY_SEND_SECURE. And conversely, TRUSTY_SEND_SECURE should
	 * only be used to send buffers with preallocated mem_id.
	 */
	if (!ret) {
		/* Use shared memory ID owned by dma_buf */
		if (transfer_kind != TRUSTY_SEND_SECURE) {
			dev_err(h->ipc_dev, "transfer_kind: %d, must be TRUSTY_SEND_SECURE\n",
				transfer_kind);
			return -EINVAL;
		}
		return 0;
	}

	if (ret != -ENODATA) {
		dev_err(h->ipc_dev, "dma_buf can't be transferred (%d)\n", ret);
		return ret;
	}

	/* Reset mem_id and offset - just in case */
	h->mem_id = 0;
	h->mem_id_off = 0;

	if (transfer_kind == TRUSTY_SEND_SECURE) {
		dev_err(h->ipc_dev, "No mem ID for TRUSTY_SEND_SECURE\n");
		return ret;
	}

	h->attach = dma_buf_attach(h->dma_buf, h->ipc_dev);
	if (IS_ERR(h->attach)) {
		ret = PTR_ERR(h->attach);
		h->attach = NULL;
		dev_err(h->ipc_dev, "Unable to attach to dma_buf (%d)\n", ret);
		return ret;
	}

	h->sgt = dma_buf_map_attachment_unlocked(h->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(h->sgt)) {
		ret = PTR_ERR(h->sgt);
		h->sgt = NULL;
		dev_err(h->ipc_dev, "Failed to match attachment (%d)\n", ret);
		return ret;
	}

	/* We only support physically contiguous memory */
	if (WARN_ON(h->sgt->orig_nents < 1))
		return -EINVAL;

	if (h->sgt->orig_nents != 1) {
		dev_err(h->ipc_dev, "Non-contiguous (%u) memory objects are not supported\n",
			h->sgt->orig_nents);
		return -EOPNOTSUPP;
	}

	ret = gsa_transfer_memory(h->ipc_dev, &h->mem_id, h->sgt->sgl, 1,
				  h->writable ? PAGE_KERNEL : PAGE_KERNEL_RO,
				  h->ffa_tag, (transfer_kind == TRUSTY_LEND));
	if (ret < 0) {
		dev_err(h->ipc_dev, "Transferring memory failed: %d\n", ret);
		return ret;
	}
	h->shared = true;

	return 0;
}

static int ipc_share_fd(struct gsa_ipc_chan *chan, int fd,
			enum transfer_kind transfer_kind,
			struct ipc_shm_handle **out)
{
	int ret;
	struct dma_buf *dma_buf;
	struct ipc_shm_handle *h = NULL;
	struct device *dev = chan->ipc_dev;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(dev);

	dev_dbg(dev, "%s: %d\n", __func__, fd);

	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		ret = PTR_ERR(dma_buf);
		dev_err_ratelimited(dev, "Unable to get dma buf from fd (%d)\n", ret);
		return ret;
	}

	if (!dma_buf->file) {
		dev_err_ratelimited(dev, "Invalid fd (%d)\n", fd);
		ret = -EBADF;
		goto bad_dma_buf;
	}

	if (!(dma_buf->file->f_mode & FMODE_READ)) {
		dev_err_ratelimited(dev, "Cannot create write-only mapping\n");
		ret = -EACCES;
		goto bad_dma_buf;
	}

	h = kzalloc(sizeof(struct ipc_shm_handle), GFP_KERNEL);
	if (!h) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_lock(&st->idr_lock);
	ret = idr_alloc(&st->shared_handles_idr, h, 1, SHM_INF_OBJ_ID_MASK + 1, GFP_KERNEL);
	mutex_unlock(&st->idr_lock);
	if (ret < 0) {
		dev_err_ratelimited(dev, "Failed (%d) to alloc shm obj ID\n", ret);
		goto idr_err;
	}
	h->obj_id = ret;

	ret = ipc_shm_attach_dma_buf(h, dev, dma_buf, transfer_kind);
	if (ret < 0) {
		dev_err(dev, "Failed (%d) to attach dma_buf\n", ret);
		goto cleanup_handle;
	}

	dev_dbg(dev, "%s: obj_id = %llu\n", __func__, h->obj_id);

	*out = h;
	return 0;

cleanup_handle:
	ipc_shm_handle_drop(h);
	return ret;

idr_err:
	kfree(h);
alloc_err:
bad_dma_buf:
	dma_buf_put(dma_buf);
	return ret;
}

static struct ipc_shm_handle **ipc_build_shm_handles(struct gsa_ipc_chan *chan,
						     struct trusty_shm *shm,
						     size_t shm_cnt)
{
	int ret;
	size_t shm_idx = 0;
	struct ipc_shm_handle **shm_handles = NULL;

	shm_handles = kmalloc_array(shm_cnt, sizeof(*shm_handles), GFP_KERNEL);
	if (!shm_handles)
		return ERR_PTR(-ENOMEM);

	for (shm_idx = 0; shm_idx < shm_cnt; shm_idx++) {
		switch (shm[shm_idx].transfer) {
		case TRUSTY_SHARE:
		case TRUSTY_LEND:
		case TRUSTY_SEND_SECURE:
			break;
		default:
			dev_err(chan->ipc_dev, "Unknown transfer type: 0x%x\n",
				shm[shm_idx].transfer);
			ret = -EINVAL;
			goto shm_share_failed;
		}

		ret = ipc_share_fd(chan, shm[shm_idx].fd, shm[shm_idx].transfer,
				   &shm_handles[shm_idx]);
		if (ret) {
			dev_dbg(chan->ipc_dev, "Forwarding memory failed\n");
			goto shm_share_failed;
		}
	}

	return shm_handles;

shm_share_failed:
	while (shm_idx)
		ipc_shm_handle_drop(shm_handles[--shm_idx]);
	kfree(shm_handles);

	return ERR_PTR(ret);
}

/*
 * Channel Send
 */
static int do_send_msg_locked(struct gsa_ipc_dev_state *st,
			      struct gsa_ipc_chan *chan,
			      struct ipc_shm_handle **handles,
			      size_t shm_cnt)
{
	int rc;
	int req_argc;
	size_t shm_idx;
	u32 req[GSA_IPC_SEND_REQ_MAX_ARGC] = {};
	u32 rsp[GSA_IPC_SEND_RSP_ARGC] = {};

	if (!chan->chan_id) {
		dev_err(chan->ipc_dev, "Channel is not connected\n");
		return -EINVAL;
	}

	if (WARN_ON(st->bbuf_len > st->bbuf_sz)) {
		dev_err(chan->ipc_dev, "Unexpected data length (%zu)\n", st->bbuf_len);
		return -EINVAL;
	}

	req[GSA_IPC_SEND_REQ_HANDLE_IDX] = chan->chan_id;
	req[GSA_IPC_SEND_REQ_MSG_BUF_ADDR_LO_IDX] = lower_32_bits(st->bbuf_da);
	req[GSA_IPC_SEND_REQ_MSG_BUF_ADDR_HI_IDX] = upper_32_bits(st->bbuf_da);
	req[GSA_IPC_SEND_REQ_MSG_LEN_IDX] = (u32)(st->bbuf_len);

	req_argc = GSA_IPC_SEND_REQ_MSG_SHM_BASE_IDX;
	for (shm_idx = 0; shm_idx < shm_cnt; shm_idx++) {
		u64 inf;

		/* we need 2 entries in req[] per shm handle  */
		if ((req_argc + 2) > GSA_IPC_SEND_REQ_MAX_ARGC)
			return -E2BIG;

		rc = encode_shm_handle(&inf, handles[shm_idx]);
		if (rc < 0) {
			dev_err(chan->ipc_dev, "Failed (%d) to encode IPC handle\n", rc);
			return rc;
		}
		req[req_argc++] = (u32)(inf);
		req[req_argc++] = (u32)(inf >> 32);
	}

	rc = gsa_send_cmd(chan->ipc_dev->parent, GSA_MB_CMD_IPC_SEND,
			  req, req_argc, rsp, ARRAY_SIZE(rsp));
	if (rc < 0) {
		dev_err(chan->ipc_dev, "Send msg failed (%d)\n", rc);
		return rc;
	}

	if (rc != GSA_IPC_SEND_RSP_ARGC) {
		dev_err(chan->ipc_dev, "Unexpected (%d) response arg count\n", rc);
		return -EIO;
	}

	/* Check IPC err */
	rc = translate_ipc_chan_err(chan, (int32_t)rsp[GSA_IPC_SEND_RSP_ERR_IDX]);
	if (rc < 0)
		return rc;

	/*
	 * Since we are effectively single threaded it is OK to register handles
	 * after sending message as it is not possible to get release event before
	 * we are done here.
	 *
	 * We are doing it before check for partial message as in this implementation
	 * a partial message gets delivered to the other side along with handles
	 * so we have to follow with normal handle release protocol.
	 */
	for (shm_idx = 0; shm_idx < shm_cnt; shm_idx++) {
		ipc_shm_register_handle_locked(st, handles[shm_idx]);

		/* Detach handle */
		handles[shm_idx] = NULL;
	}

	/* Check for partial send */
	if (rsp[GSA_IPC_SEND_RSP_MSG_LEN_IDX] != st->bbuf_len) {
		dev_err(chan->ipc_dev, "Chan (%u): Partial send\n", chan->chan_id);
		return -EIO;
	}

	return st->bbuf_len;
}

static int chan_check_send_state(struct gsa_ipc_chan *chan)
{
	if (!chan->chan_id)
		return -EINVAL; /* Channel does not exist */

	if (!chan->connected)
		return -EAGAIN; /* Connection is still pending */

	if (chan->peer_closed)
		return -ESHUTDOWN; /* Channel closed by peer */

	if (chan->send_blocked)
		return -EAGAIN;  /* Unable to send */

	return 0;
}

static ssize_t ipc_chan_write_iter_locked(struct gsa_ipc_chan *chan,
					  struct iov_iter *iter,
					  struct ipc_shm_handle **shm_handles,
					  size_t shm_cnt)
{
	int rc;
	size_t len;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	rc = chan_check_send_state(chan);
	if (rc < 0)
		return rc;

	/* message length */
	len = iov_iter_count(iter);

	/* check available space */
	if (len > st->bbuf_sz)
		return -EMSGSIZE;

	/* copy in message data */
	if (copy_from_iter(st->bbuf, len, iter) != len)
		return -EFAULT;
	st->bbuf_len = len;

	/* Send message */
	return do_send_msg_locked(st, chan, shm_handles, shm_cnt);
}

static ssize_t ipc_chan_write_iter(struct gsa_ipc_chan *chan,
				   struct iov_iter *iter,
				   struct trusty_shm *shm,
				   size_t shm_cnt,
				   bool nonblock)
{
	ssize_t ret;
	struct iov_iter curr_iter;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct ipc_shm_handle **shm_handles = NULL;

	dev_dbg(chan->ipc_dev, "write iter: enter\n");

	if (shm_cnt) {
		/* Build handle array */
		shm_handles = ipc_build_shm_handles(chan, shm, shm_cnt);
		if (IS_ERR(shm_handles))
			return PTR_ERR(shm_handles);
	}

	add_wait_queue(&chan->sendq, &wait);
	for (;;) {
		/* copy iter because we might need to reuse it */
		curr_iter = *iter;

		mutex_lock(&st->ipc_lock);
		ret = ipc_chan_write_iter_locked(chan, &curr_iter, shm_handles, shm_cnt);
		mutex_unlock(&st->ipc_lock);

		if (ret != -EAGAIN && ret != -ENOTCONN) {
			*iter = curr_iter;
			break;
		}

		if (nonblock)
			break;

		wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&chan->sendq, &wait);

	dev_dbg(chan->ipc_dev, "write iter: ret = %zd\n", ret);

	/* Destroy shm handles regardless of send result */
	if (shm_handles) {
		size_t i;

		for (i = 0; i < shm_cnt; i++) {
			if (shm_handles[i])
				ipc_shm_handle_drop(shm_handles[i]);
		}
		kfree(shm_handles);
	}

	return ret;
}

static int ipc_chan_send_msg_locked(struct gsa_ipc_dev_state *st,
				    struct gsa_ipc_chan *chan,
				    const void *msg,
				    size_t msg_len)
{
	int rc;

	rc = chan_check_send_state(chan);
	if (rc < 0)
		return rc;

	if (msg_len > st->bbuf_sz) {
		/* Message is too long */
		return -EMSGSIZE;
	}

	/* Copy in data */
	memcpy(st->bbuf, msg, msg_len);
	st->bbuf_len = msg_len;

	return do_send_msg_locked(st, chan, NULL, 0);
}

int gsa_ipc_chan_send_msg(struct gsa_ipc_chan *chan, const void *msg, size_t msg_len)
{
	int rc;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	mutex_lock(&st->ipc_lock);
	rc = ipc_chan_send_msg_locked(st, chan, msg, msg_len);
	mutex_unlock(&st->ipc_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(gsa_ipc_chan_send_msg);

/*
 *  Chan Recv
 */
static int chan_check_recv_state(struct gsa_ipc_chan *chan)
{
	if (!chan->chan_id)
		return -EINVAL;

	if (!chan->connected)
		return -EAGAIN;

	/*
	 * Note: call recv even if channel is in peer_closed state as
	 * it still might contain messages that can be retrieved.
	 */

	return 0;
}

static int do_recv_msg_locked(struct gsa_ipc_dev_state *st, struct gsa_ipc_chan *chan)
{
	int rc;
	u32 req[GSA_IPC_RECV_REQ_ARGC] = {};
	u32 rsp[GSA_IPC_RECV_RSP_ARGC] = {};

	rc = chan_check_recv_state(chan);
	if (rc < 0)
		return rc;

	req[GSA_IPC_RECV_REQ_HANDLE_IDX] = chan->chan_id;
	req[GSA_IPC_RECV_REQ_MSG_BUF_ADDR_LO_IDX] = lower_32_bits(st->bbuf_da);
	req[GSA_IPC_RECV_REQ_MSG_BUF_ADDR_HI_IDX] = upper_32_bits(st->bbuf_da);
	req[GSA_IPC_RECV_REQ_MSG_BUF_SIZE_IDX] = (u32)(st->bbuf_sz);

	rc = gsa_send_cmd(chan->ipc_dev->parent, GSA_MB_CMD_IPC_RECV,
			  req, ARRAY_SIZE(req), rsp, ARRAY_SIZE(rsp));
	if (rc < 0) {
		dev_err(chan->ipc_dev, "Recv msg failed (%d)\n", rc);
		return rc;
	}

	if (rc != GSA_IPC_RECV_RSP_ARGC) {
		dev_err(chan->ipc_dev, "Unexpected (%d) response arg count\n", rc);
		return -EIO;
	}

	/* Check IPC err */
	rc = translate_ipc_chan_err(chan, (int32_t)rsp[GSA_IPC_RECV_RSP_ERR_IDX]);
	if (rc < 0)
		return rc;

	/* there is a message */
	st->bbuf_len = rsp[GSA_IPC_RECV_RSP_MSG_LEN_IDX];
	if (st->bbuf_len > st->bbuf_sz) {
		dev_err(chan->ipc_dev, "Unexpected data length (%zu)\n",
			st->bbuf_len);
		st->bbuf_len = 0;
		return -EIO;
	}

	return 0;
}

static ssize_t ipc_chan_read_iter_locked(struct gsa_ipc_chan *chan, struct iov_iter *iter)
{
	int rc;
	size_t len;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	len = iov_iter_count(iter);

	/* Read msg */
	rc = do_recv_msg_locked(st, chan);
	if (rc < 0) {
		if (rc == -EAGAIN) {
			/* No messages left */
			if (chan->peer_closed) {
				/* Now we can return connection closed error */
				rc = -ESHUTDOWN;
			}
		}
		return rc;
	}

	/*
	 * Note that the partial message receive is not supported by this
	 * implementation. It is not a part of an existing Trusty interface
	 * this device is compatible with.
	 */
	if (st->bbuf_len > len)
		return -EMSGSIZE;

	if (copy_to_iter(st->bbuf, st->bbuf_len, iter) != st->bbuf_len)
		return -EFAULT;

	return st->bbuf_len;
}

static ssize_t ipc_chan_read_iter(struct gsa_ipc_chan *chan, struct iov_iter *iter, bool nonblock)
{
	ssize_t ret;
	struct iov_iter curr_iter;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	dev_dbg(chan->ipc_dev, "read iter: enter\n");

	add_wait_queue(&chan->recvq, &wait);
	for (;;) {
		/* copy iter because we might need to reuse it */
		curr_iter = *iter;

		mutex_lock(&st->ipc_lock);
		ret = ipc_chan_read_iter_locked(chan, &curr_iter);
		mutex_unlock(&st->ipc_lock);

		if (ret != -EAGAIN && ret != -ENOTCONN) {
			*iter = curr_iter;
			break;
		}

		if (nonblock)
			break;

		wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&chan->recvq, &wait);

	dev_dbg(chan->ipc_dev, "read iter: ret = %zd\n", ret);

	return ret;
}

static int ipc_chan_recv_msg_locked(struct gsa_ipc_dev_state *st,
				    struct gsa_ipc_chan *chan,
				    void *msg_buf, size_t msg_buf_sz)
{
	int rc;

	rc = do_recv_msg_locked(st, chan);
	if (rc < 0)
		return rc;

	if (st->bbuf_len > msg_buf_sz)
		return -EMSGSIZE;

	/* Copy out data */
	memcpy(msg_buf, st->bbuf, st->bbuf_len);

	return st->bbuf_len;
}

int gsa_ipc_chan_recv_msg(struct gsa_ipc_chan *chan, void *msg_buf, size_t msg_buf_sz)
{
	int rc;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	mutex_lock(&st->ipc_lock);
	rc = ipc_chan_recv_msg_locked(st, chan, msg_buf, msg_buf_sz);
	mutex_unlock(&st->ipc_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(gsa_ipc_chan_recv_msg);

/*
 * Channel close
 */
static int ipc_chan_close_locked(struct gsa_ipc_chan *chan)
{
	int rc;
	u32 req[GSA_IPC_DISCONNECT_REQ_ARGC] = {};

	dev_dbg(chan->ipc_dev, "%s: chan %u\n", __func__, chan->chan_id);

	if (!chan->chan_id) {
		/* Already closed */
		dev_dbg(chan->ipc_dev, "Channel is not connected\n");
		return 0;
	}

	req[GSA_IPC_DISCONNECT_REQ_HANDLE_IDX] = chan->chan_id;

	rc = gsa_send_cmd(chan->ipc_dev->parent, GSA_MB_CMD_IPC_DISCONNECT,
			  req, ARRAY_SIZE(req), NULL, 0);
	if (rc < 0) {
		dev_err(chan->ipc_dev, "GSA IPC Disconnect failed (%d).\n", rc);
		return rc;
	}
	chan->chan_id = 0;
	chan->connected = false;
	chan->send_blocked = false;
	chan->recv_has_msg = false;
	chan->peer_closed = false;

	wake_up_interruptible(&chan->sendq);
	wake_up_interruptible(&chan->recvq);

	return 0;
}

int gsa_ipc_chan_close(struct gsa_ipc_chan *chan)
{
	int rc;
	struct gsa_ipc_dev_state *st;

	if (!chan)
		return -EINVAL;

	st = dev_get_drvdata(chan->ipc_dev);

	mutex_lock(&st->ipc_lock);
	rc = ipc_chan_close_locked(chan);
	mutex_unlock(&st->ipc_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(gsa_ipc_chan_close);

/*
 * Channel destroy
 */
int gsa_ipc_chan_destroy(struct gsa_ipc_chan *chan)
{
	int rc;
	struct device *ipc_dev;
	struct gsa_ipc_dev_state *st;

	if (!chan)
		return -EINVAL;

	ipc_dev = chan->ipc_dev;
	st = dev_get_drvdata(ipc_dev);

	mutex_lock(&st->ipc_lock);

	/* Make sure that IPC chan is closed */
	rc = ipc_chan_close_locked(chan);
	if (WARN_ON(rc < 0))
		dev_err(chan->ipc_dev, "Failed to close channel. Leaking GSA IPC connection\n");

	/* Remove it from channel list */
	list_del(&chan->node);

	/* Destroy it */
	kfree(chan);

	mutex_unlock(&st->ipc_lock);

	put_device(ipc_dev);

	return rc;
}
EXPORT_SYMBOL_GPL(gsa_ipc_chan_destroy);

void gsa_register_func_for_dma_buf(
	u64 (*get_ffa_tag)(struct dma_buf *dma_buf),
	int (*get_shared_mem_id)(struct dma_buf *dma_buf, u64 *id, u64 *poff))
{
	if (WARN_ON(dma_buf_get_ffa_tag || dma_buf_get_shared_mem_id))
		pr_err("gsa_ipc: dma buf callbacks are already registered\n");

	dma_buf_get_ffa_tag = get_ffa_tag;
	dma_buf_get_shared_mem_id = get_shared_mem_id;
}
EXPORT_SYMBOL_GPL(gsa_register_func_for_dma_buf);

/***********************************************************************************/

struct gsa_ipc_event {
	u32 event;
	u32 handle;
	u64 cookie;
};

static int ipc_get_event_locked(struct gsa_ipc_dev_state *st, struct gsa_ipc_event *ev)
{
	int rc;
	u32 req[GSA_IPC_GET_EVENT_REQ_ARGC] = {};
	u32 rsp[GSA_IPC_GET_EVENT_RSP_ARGC] = {};

	req[GSA_IPC_GET_EVENT_REQ_HANDLE_IDX] = 0;

	rc = gsa_send_cmd(st->dev->parent, GSA_MB_CMD_IPC_GET_EVENT,
			  req, ARRAY_SIZE(req), rsp, ARRAY_SIZE(rsp));
	if (rc < 0) {
		dev_err(st->dev, "Failed (%d) to get ipc event\n", rc);
		return rc;
	}

	if (rc != GSA_IPC_GET_EVENT_RSP_ARGC) {
		dev_err(st->dev, "Unexpected (%d) response arg count\n", rc);
		return -EIO;
	}

	ev->event = rsp[GSA_IPC_GET_EVENT_RSP_EVENT_IDX];
	ev->handle = rsp[GSA_IPC_GET_EVENT_RSP_HANDLE_IDX];
	ev->cookie = ((u64)rsp[GSA_IPC_GET_EVENT_RSP_COOKIE_HI_IDX] << 32)
			| rsp[GSA_IPC_GET_EVENT_RSP_COOKIE_LO_IDX];
	return 0;
}

static struct gsa_ipc_chan *lookup_ipc_chan_locked(struct gsa_ipc_dev_state *st,
						   struct gsa_ipc_event *ev)
{
	struct gsa_ipc_chan *chan;

	list_for_each_entry(chan, &st->chan_list, node) {
		if (chan->chan_id == ev->handle)
			return chan;
	}

	return NULL;
}

static void ipc_handle_channel_event_locked(struct gsa_ipc_chan *chan,
					    struct gsa_ipc_event *ev)
{
	u32 send_mask = GSA_IPC_EVENT_CONNECTED	| GSA_IPC_EVENT_HUP | GSA_IPC_EVENT_ERROR
			| GSA_IPC_EVENT_SEND_UNBLOCKED;

	u32 recv_mask = GSA_IPC_EVENT_CONNECTED	| GSA_IPC_EVENT_HUP | GSA_IPC_EVENT_ERROR
			| GSA_IPC_EVENT_MSG;

	if (WARN_ON(ev->event & GSA_IPC_EVENT_ERROR)) {
		/* Should never happen */
		dev_err_ratelimited(chan->ipc_dev, "Got Error event: 0x%x\n", ev->event);

		/* Force connection closure */
		chan->connected = true;
		chan->peer_closed = true;
	}

	if (ev->event & GSA_IPC_EVENT_HUP) {
		/* Treat it as equivalent of connection
		 * was established but then closed by peer.
		 */
		chan->connected = true;
		chan->peer_closed = true;
	}

	if (ev->event & GSA_IPC_EVENT_CONNECTED)
		chan->connected = true;

	if (ev->event & GSA_IPC_EVENT_SEND_UNBLOCKED)
		chan->send_blocked = false;

	if (ev->event & GSA_IPC_EVENT_MSG)
		chan->recv_has_msg = true;

	if (ev->event & send_mask) {
		wake_up_interruptible(&chan->sendq);
		dev_dbg(chan->ipc_dev, "wakeup sendq: 0x%x\n", ev->event);
	}

	if (ev->event & recv_mask) {
		wake_up_interruptible(&chan->recvq);
		dev_dbg(chan->ipc_dev, "wakeup recvq: 0x%x\n", ev->event);
	}
}

static bool is_shm_handle_obj(u32 handle)
{
	return (handle <= SHM_INF_OBJ_ID_MASK);
}

static void ipc_handle_shm_obj_release(struct gsa_ipc_dev_state *st, struct gsa_ipc_event *ev)
{
	struct ipc_shm_handle *h;

	h = ipc_shm_unregister_handle_locked(st, ev->handle);
	if (h)
		ipc_shm_handle_drop(h);
}

static bool ipc_handle_ipc_events_locked(struct gsa_ipc_dev_state *st)
{
	int rc;
	int cnt;
	struct gsa_ipc_event ev;
	struct gsa_ipc_chan *chan;

	for (cnt = 0; cnt < 64; cnt++) {
		rc = ipc_get_event_locked(st, &ev);
		if (WARN_ON(rc < 0)) {
			/* This should never happen */
			dev_err(st->dev, "Failed (%d) to get event\n", rc);
			break;
		}

		if (!ev.handle)
			return true; /* No events - nothing to do */

		if (is_shm_handle_obj(ev.handle)) {
			ipc_handle_shm_obj_release(st, &ev);
			continue;
		}

		/* Regular IPC channel */
		chan = lookup_ipc_chan_locked(st, &ev);
		if (WARN_ON(!chan)) {
			/* It means that GSA side is out of sync with linux.
			 * It should never happen
			 */
			dev_err(st->dev, "Invalid handle\n");
			continue;
		}

		ipc_handle_channel_event_locked(chan, &ev);
	}

	return false;
}

static void ipc_irq_work_func(struct work_struct *work)
{
	int done;
	struct gsa_ipc_dev_state *st = container_of(work, struct gsa_ipc_dev_state, ipc_irq_work);

	dev_dbg(st->dev, "Got IPC IRQ\n");

	mutex_lock(&st->ipc_lock);
	done = ipc_handle_ipc_events_locked(st);
	mutex_unlock(&st->ipc_lock);

	if (!done)
		schedule_work(work);
}

static void ipc_irq_handler(void *args)
{
	struct gsa_ipc_dev_state *st = dev_get_drvdata(args);

	schedule_work(&st->ipc_irq_work);
}

/***********************************************************************************/

static struct device *filp_to_ipc_dev(struct file *filp)
{
	struct gsa_ipc_chan *chan = filp->private_data;

	return chan->ipc_dev;
}

static int gsa_cdev_open(struct inode *inode, struct file *filp)
{
	struct gsa_ipc_chan *chan;
	struct gsa_cdev *cdev = container_of(inode->i_cdev, struct gsa_cdev, cdev);

	chan = gsa_ipc_chan_create(cdev->device->parent);
	if (!chan)
		return -ENOMEM;

	/* Attach IPC chan */
	filp->private_data = chan;

	return nonseekable_open(inode, filp);
}

static long gsa_cdev_handle_load_app(struct device *ipc_dev, unsigned long arg)
{
	struct gsa_ioc_load_app_req req;
	u32 gsa_mbox_req[APP_PKG_LOAD_REQ_ARGC];
	dma_addr_t outbuf_dma;
	void *outbuf_va = NULL;
	int rc = 0;

	if (copy_from_user(&req, (const void __user *)arg, sizeof(req))) {
		dev_err(ipc_dev, "load_app failed to copy request from user space.\n");
		return -EFAULT;
	}

	if (!req.len)
		return -EINVAL;

	/* Allocate memory needed by GSA app */
	outbuf_va = memdup_user((const void __user *)req.buf, req.len);
	if (IS_ERR(outbuf_va)) {
		dev_err(ipc_dev, "load_app handler failed to copy app from userspace.\n");
		return PTR_ERR(outbuf_va);
	}

	outbuf_dma = dma_map_single(ipc_dev->parent, outbuf_va, req.len, DMA_TO_DEVICE);
	if (dma_mapping_error(ipc_dev->parent, outbuf_dma)) {
		dev_err(ipc_dev, "load_app handler failed to allocate dma.\n");
		rc = -ENOMEM;
		goto map_err;
	}

	gsa_mbox_req[APP_PKG_ADDR_LO_IDX] = (u32)outbuf_dma;
	gsa_mbox_req[APP_PKG_ADDR_HI_IDX] = (u32)(outbuf_dma >> 32);
	gsa_mbox_req[APP_PKG_SIZE_IDX] = req.len;
	rc = gsa_send_cmd(ipc_dev->parent, GSA_MB_CMD_LOAD_APP_PKG, gsa_mbox_req, 3, NULL, 0);

	if (rc < 0) {
		dev_err(ipc_dev, "load_app handler received error response from GSA mbox (%d).\n",
			rc);
		goto send_err;
	}

send_err:
	dma_unmap_single(ipc_dev->parent, outbuf_dma, req.len, DMA_TO_DEVICE);
map_err:
	kfree(outbuf_va);
	return rc;
}

static long handle_gsa_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct device *ipc_dev = filp_to_ipc_dev(filp);

	switch (cmd) {
	case GSA_IOC_LOAD_APP:
		return gsa_cdev_handle_load_app(ipc_dev, arg);

	default:
		dev_err_ratelimited(ipc_dev, "Unhandled ioctl cmd: 0x%x\n", cmd);
		return -ENOTTY;
	}
}

static long handle_tipc_connect_ioctl(struct file *filp, const char __user *usr_name)
{
	struct gsa_ipc_chan *chan = filp->private_data;

	return ipc_chan_connect_from_user(chan, usr_name, !!(filp->f_flags & O_NONBLOCK));
}

static long handle_tipc_send_ioctl(struct file *filp,
				   const struct tipc_send_msg_req __user *arg)
{
	long ret;
	struct iov_iter iter;
	struct tipc_send_msg_req req;
	struct iovec fast_iovs[UIO_FASTIOV];
	struct iovec *iov = fast_iovs;
	struct gsa_ipc_chan *chan = filp->private_data;
	struct device *dev = chan->ipc_dev;
	struct trusty_shm *shm = NULL;

	/* Read request */
	if (copy_from_user(&req, arg, sizeof(req))) {
		dev_err_ratelimited(dev, "Failed to copy in request\n");
		return -EFAULT;
	}

	if (req.shm_cnt > MAX_SHM_CNT) {
		dev_err_ratelimited(dev, "too many SHM handles\n");
		return -E2BIG;
	}

	/* Read SHM */
	if (req.shm_cnt) {
		shm = kmalloc_array(req.shm_cnt, sizeof(*shm), GFP_KERNEL);
		if (!shm)
			return -ENOMEM;

		if (copy_from_user(shm, u64_to_user_ptr(req.shm),
				   req.shm_cnt * sizeof(struct trusty_shm))) {
			ret = -EFAULT;
			goto load_shm_args_failed;
		}
	}

	/* Read IOVs */
	ret = import_iovec(ITER_SOURCE, u64_to_user_ptr(req.iov), req.iov_cnt,
			   ARRAY_SIZE(fast_iovs), &iov, &iter);
	if (ret < 0) {
		dev_err_ratelimited(dev, "Failed (%d) to import iovec\n", (int)ret);
		goto import_iovec_failed;
	}

	ret = ipc_chan_write_iter(chan, &iter, shm, (size_t)req.shm_cnt,
				  !!(filp->f_flags & O_NONBLOCK));

	if (iov != fast_iovs)
		kfree(iov);

import_iovec_failed:
load_shm_args_failed:
	kfree(shm);

	return ret;
}

static long handle_tipc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct device *ipc_dev = filp_to_ipc_dev(filp);

	switch (cmd) {
	case TIPC_IOC_CONNECT:
		return handle_tipc_connect_ioctl(filp, (const char __user *)arg);

	case TIPC_IOC_SEND_MSG:
		return handle_tipc_send_ioctl(filp, (const struct tipc_send_msg_req __user *)arg);

	default:
		dev_err_ratelimited(ipc_dev, "Unhandled ioctl cmd: 0x%x\n", cmd);
		return -ENOTTY;
	}
}

static long gsa_cdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct device *ipc_dev = filp_to_ipc_dev(filp);

	switch (_IOC_TYPE(cmd)) {
	case GSA_IOC_MAGIC:
		return handle_gsa_ioctl(filp, cmd, arg);

	case TIPC_IOC_MAGIC:
		return handle_tipc_ioctl(filp, cmd, arg);

	default:
		dev_err_ratelimited(ipc_dev, "Unsupported (%u) IOCTL magic number\n",
				    _IOC_TYPE(cmd));
		return -ENOTTY;
	}
}

static ssize_t gsa_cdev_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *filp = iocb->ki_filp;
	struct gsa_ipc_chan *chan = filp->private_data;

	return ipc_chan_write_iter(chan, iter, NULL, 0, !!(filp->f_flags & O_NONBLOCK));
}

static ssize_t gsa_cdev_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *filp = iocb->ki_filp;
	struct gsa_ipc_chan *chan = filp->private_data;

	return ipc_chan_read_iter(chan, iter, !!(filp->f_flags & O_NONBLOCK));
}

static __poll_t gsa_cdev_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = 0;
	struct gsa_ipc_chan *chan = filp->private_data;
	struct gsa_ipc_dev_state *st = dev_get_drvdata(chan->ipc_dev);

	poll_wait(filp, &chan->sendq, wait);
	poll_wait(filp, &chan->recvq, wait);

	mutex_lock(&st->ipc_lock);

	if (chan->connected) {
		if (!chan->send_blocked) {
			/* OK to send */
			mask |= EPOLLOUT | EPOLLWRNORM;
		}

		if (chan->recv_has_msg) {
			/* Has message */
			mask |= EPOLLIN | EPOLLRDNORM;
		}

		if (chan->peer_closed) {
			/* closed by peer */
			mask |= EPOLLHUP | EPOLLERR;
		}
	}

	dev_dbg(st->dev, "poll mask = 0x%x\n", mask);

	mutex_unlock(&st->ipc_lock);

	return mask;
}

static int gsa_cdev_release(struct inode *inode, struct file *filp)
{
	struct gsa_ipc_chan *chan = filp->private_data;

	(void)gsa_ipc_chan_destroy(chan);
	return 0;
}

static const struct file_operations gsa_cdev_fops = {
	.open		= gsa_cdev_open,
	.release	= gsa_cdev_release,
	.unlocked_ioctl	= gsa_cdev_ioctl,
	.read_iter	= gsa_cdev_read_iter,
	.write_iter	= gsa_cdev_write_iter,
	.poll		= gsa_cdev_poll,
	.owner = THIS_MODULE,
};

static int gsa_cdev_init(void)
{
	int ret = alloc_chrdev_region(&gsa_cdev_base_num, 0, MAX_DEVICES, KBUILD_MODNAME);

	if (ret) {
		pr_err("%s: failed (%d) to alloc chdev region\n", __func__, ret);
		return ret;
	}

	gsa_cdev_class = class_create(KBUILD_MODNAME);
	if (IS_ERR(gsa_cdev_class)) {
		ret = PTR_ERR(gsa_cdev_class);
		unregister_chrdev_region(gsa_cdev_base_num, MAX_DEVICES);
		return ret;
	}

	return 0;
}

static int gsa_cdev_create(struct device *parent, struct gsa_cdev *cdev_node,
			   const char *name, const struct file_operations *fops)
{
	int ret;
	int minor;

	/* allocate minor */
	minor = idr_alloc(&gsa_cdev_devices, cdev_node, 0, MAX_DEVICES, GFP_KERNEL);
	if (minor < 0) {
		dev_err(parent, "%s: failed (%d) to get id\n", __func__, minor);
		return minor;
	}
	cdev_node->device_num = MKDEV(MAJOR(gsa_cdev_base_num), minor);

	/* Create device node */
	cdev_node->device = device_create(gsa_cdev_class, parent,
					  cdev_node->device_num,
					  NULL, "%s%d", name, minor);
	if (IS_ERR(cdev_node->device)) {
		ret = PTR_ERR(cdev_node->device);
		dev_err(parent, "%s: device_create failed: %d\n", __func__, ret);
		goto err_device_create;
	}

	/* Add character device */
	cdev_node->cdev.owner = THIS_MODULE;
	cdev_init(&cdev_node->cdev, fops);
	ret = cdev_add(&cdev_node->cdev, cdev_node->device_num, 1);
	if (ret) {
		dev_err(parent, "%s: cdev_add failed (%d)\n", __func__, ret);
		goto err_add_cdev;
	}

	pr_debug("GSA cdev created.\n");
	return 0;

err_add_cdev:
	device_destroy(gsa_cdev_class, cdev_node->device_num);
err_device_create:
	idr_remove(&gsa_cdev_devices, MINOR(cdev_node->device_num));
	return ret;
}

static void gsa_cdev_remove(struct gsa_cdev *cdev_node)
{
	cdev_del(&cdev_node->cdev);
	device_destroy(gsa_cdev_class, cdev_node->device_num);
	idr_remove(&gsa_cdev_devices, MINOR(cdev_node->device_num));
}

static void gsa_cdev_exit(void)
{
	class_destroy(gsa_cdev_class);
	unregister_chrdev_region(gsa_cdev_base_num, MAX_DEVICES);
}

static int gsa_ipc_dev_probe(struct platform_device *pdev)
{
	int err;
	struct gsa_ipc_dev_state *s;
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "Initializing\n");

	/* Check if we have parent */
	if (!dev->parent)
		return -ENODEV;

	if (dev->parent->dma_mask)
		dma_coerce_mask_and_coherent(dev, *dev->parent->dma_mask);

	/* allocate driver state */
	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	/* Allocate bounce buffer */
	s->bbuf_sz = PAGE_SIZE;
	s->bbuf = dmam_alloc_coherent(dev->parent, PAGE_SIZE,
				      &s->bbuf_da, GFP_KERNEL);
	if (!s->bbuf)
		return -ENOMEM;

	s->dev = dev;
	mutex_init(&s->ipc_lock);
	INIT_LIST_HEAD(&s->chan_list);
	INIT_WORK(&s->ipc_irq_work, ipc_irq_work_func);
	s->shared_handles = RB_ROOT;
	idr_init(&s->shared_handles_idr);
	mutex_init(&s->idr_lock);
	mutex_init(&s->rb_lock);
	platform_set_drvdata(pdev, s);

	/* Initialize character devices */
	err = gsa_cdev_create(dev, &s->cdn, "gsa", &gsa_cdev_fops);
	if (err != 0) {
		dev_err(dev, "Failed to create gsa char device (%d)\n", err);
		return err;
	}

	err = gsa_set_ipc_irq_handler(dev->parent, ipc_irq_handler, dev);
	if (err < 0) {
		dev_err(dev, "Failed to register IPC IRQ (%d)\n", err);
		goto err_ipc_irq;
	}

	dev_dbg(dev, "Initialized\n");

	return 0;

err_ipc_irq:
	gsa_cdev_remove(&s->cdn);
	return err;
}

static void gsa_ipc_dev_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gsa_ipc_dev_state *s = platform_get_drvdata(pdev);

	/* Unregister interrupt handler */
	gsa_set_ipc_irq_handler(dev->parent, NULL, NULL);
	cancel_work_sync(&s->ipc_irq_work);

	/* There is no way to revoke handles that are still shared.
	 * Just report it and leak corresponding structures
	 */
	if (WARN_ON(!RB_EMPTY_ROOT(&s->shared_handles)))
		dev_err(dev, "Some memory handles are still shared...leaking\n");

	/* destroy IDRs */
	idr_destroy(&s->shared_handles_idr);

	gsa_cdev_remove(&s->cdn);
}

static const struct of_device_id gsa_ipc_of_match[] = {
	{ .compatible = "google,gsa-ipc-v1", },
	{},
};
MODULE_DEVICE_TABLE(of, gsa_ipc_of_match);

static struct platform_driver gsa_ipc_driver = {
	.probe = gsa_ipc_dev_probe,
	.remove = gsa_ipc_dev_remove,
	.driver = {
		.name = "gsa-ipc",
		.of_match_table = gsa_ipc_of_match,
	},
};

static int __init gsa_ipc_driver_init(void)
{
	int ret;

	ret = gsa_cdev_init();
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&gsa_ipc_driver);
	if (ret < 0)
		gsa_cdev_exit();

	return ret;
}

static void __exit gsa_ipc_driver_exit(void)
{
	platform_driver_unregister(&gsa_ipc_driver);
	gsa_cdev_exit();
}

module_init(gsa_ipc_driver_init);
module_exit(gsa_ipc_driver_exit);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,13,0)
MODULE_IMPORT_NS("DMA_BUF");
#else
MODULE_IMPORT_NS(DMA_BUF);
#endif

MODULE_DESCRIPTION("Google GSA IPC platform driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Ryleev <gmar@google.com>");
