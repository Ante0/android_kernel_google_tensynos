/*
 * Copyright (c) 2018-2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "linux/device.h"
#include "linux/netdevice.h"
#include "__osif_psoc_sync.h"
#include "__osif_vdev_sync.h"
#include "osif_vdev_sync.h"
#include "qdf_lock.h"
#include "qdf_status.h"
#include "qdf_types.h"
#include <qdf_trace.h>
#include <wlan_cfg80211.h>

static struct osif_vdev_sync __osif_vdev_sync_arr[WLAN_MAX_VDEVS +
						  WLAN_MAX_ML_VDEVS];
static qdf_spinlock_t __osif_vdev_sync_lock;

#define osif_vdev_sync_lock_create() qdf_spinlock_create(&__osif_vdev_sync_lock)
#define osif_vdev_sync_lock_destroy() \
	qdf_spinlock_destroy(&__osif_vdev_sync_lock)
#define osif_vdev_sync_lock() qdf_spin_lock_bh(&__osif_vdev_sync_lock)
#define osif_vdev_sync_unlock() qdf_spin_unlock_bh(&__osif_vdev_sync_lock)

static struct osif_vdev_sync *osif_vdev_sync_lookup(struct net_device *net_dev)
{
	int i;

	for (i = 0; i < QDF_ARRAY_SIZE(__osif_vdev_sync_arr); i++) {
		struct osif_vdev_sync *vdev_sync = __osif_vdev_sync_arr + i;

		if (!vdev_sync->in_use)
			continue;

		if (vdev_sync->net_dev == net_dev)
			return vdev_sync;
	}

	return NULL;
}

/**
 * osif_vdev_sync_lookup_by_wdev() - look up the vdev sync context for @wdev
 * @wdev: the wireless_dev to look up
 *
 * First attempts an exact match on the registered wireless_dev pointer.
 * Falls back to matching via the associated net_device if @wdev carries one.
 *
 * Caller must hold __osif_vdev_sync_lock.
 *
 * Return: the vdev synchronization context registered for @wdev, or NULL
 */
static struct osif_vdev_sync *osif_vdev_sync_lookup_by_wdev(
						struct wireless_dev *wdev)
{
	int i;

	for (i = 0; i < QDF_ARRAY_SIZE(__osif_vdev_sync_arr); i++) {
		struct osif_vdev_sync *vdev_sync = __osif_vdev_sync_arr + i;

		if (!vdev_sync->in_use)
			continue;

		/* First try exact wdev match */
		if (vdev_sync->wdev == wdev)
			return vdev_sync;

		/* Fallback: if wdev has netdev, try netdev match */
		if (wdev->netdev && vdev_sync->net_dev == wdev->netdev)
			return vdev_sync;
	}

	return NULL;
}

struct osif_vdev_sync *osif_get_vdev_sync_arr(void)
{
	return __osif_vdev_sync_arr;
}

static struct osif_vdev_sync *osif_vdev_sync_get(void)
{
	int i;

	for (i = 0; i < QDF_ARRAY_SIZE(__osif_vdev_sync_arr); i++) {
		struct osif_vdev_sync *vdev_sync = __osif_vdev_sync_arr + i;

		if (!vdev_sync->in_use) {
			vdev_sync->in_use = true;
			return vdev_sync;
		}
	}

	return NULL;
}

static void osif_vdev_sync_put(struct osif_vdev_sync *vdev_sync)
{
	qdf_mem_zero(vdev_sync, sizeof(*vdev_sync));
}

int osif_vdev_sync_create(struct device *dev,
			  struct osif_vdev_sync **out_vdev_sync)
{
	struct osif_vdev_sync *vdev_sync;
	QDF_STATUS status;

	QDF_BUG(dev);
	if (!dev)
		return -EINVAL;

	QDF_BUG(out_vdev_sync);
	if (!out_vdev_sync)
		return -EINVAL;

	osif_vdev_sync_lock();
	vdev_sync = osif_vdev_sync_get();
	osif_vdev_sync_unlock();
	if (!vdev_sync)
		return -ENOMEM;

	status = osif_psoc_sync_dsc_vdev_create(dev, &vdev_sync->dsc_vdev);
	if (QDF_IS_STATUS_ERROR(status))
		goto sync_put;

	*out_vdev_sync = vdev_sync;

	return 0;

sync_put:
	osif_vdev_sync_lock();
	osif_vdev_sync_put(vdev_sync);
	osif_vdev_sync_unlock();

	return qdf_status_to_os_return(status);
}

int __osif_vdev_sync_create_and_trans(struct device *dev,
				      struct osif_vdev_sync **out_vdev_sync,
				      const char *desc)
{
	struct osif_vdev_sync *vdev_sync;
	QDF_STATUS status;
	int errno;

	errno = osif_vdev_sync_create(dev, &vdev_sync);
	if (errno)
		return errno;

	status = dsc_vdev_trans_start(vdev_sync->dsc_vdev, desc);
	if (QDF_IS_STATUS_ERROR(status))
		goto sync_destroy;

	*out_vdev_sync = vdev_sync;

	return 0;

sync_destroy:
	osif_vdev_sync_destroy(vdev_sync);

	return qdf_status_to_os_return(status);
}

void osif_vdev_sync_destroy(struct osif_vdev_sync *vdev_sync)
{
	QDF_BUG(vdev_sync);
	if (!vdev_sync)
		return;

	dsc_vdev_destroy(&vdev_sync->dsc_vdev);

	osif_vdev_sync_lock();
	osif_vdev_sync_put(vdev_sync);
	osif_vdev_sync_unlock();
}

void osif_vdev_sync_register(struct net_device *net_dev,
			     struct wireless_dev *wdev,
			     struct osif_vdev_sync *vdev_sync)
{
	QDF_BUG(net_dev);
	QDF_BUG(wdev);
	QDF_BUG(vdev_sync);
	if (!vdev_sync)
		return;

	osif_vdev_sync_lock();
	vdev_sync->net_dev = net_dev;
	vdev_sync->wdev = wdev;
	osif_vdev_sync_unlock();
}

struct osif_vdev_sync *osif_vdev_sync_unregister(struct net_device *net_dev)
{
	struct osif_vdev_sync *vdev_sync;

	QDF_BUG(net_dev);
	if (!net_dev)
		return NULL;

	osif_vdev_sync_lock();
	vdev_sync = osif_vdev_sync_lookup(net_dev);
	if (vdev_sync) {
		vdev_sync->net_dev = NULL;
		vdev_sync->wdev = NULL;
	}

	osif_vdev_sync_unlock();

	return vdev_sync;
}

typedef QDF_STATUS (*vdev_start_func)(struct dsc_vdev *, const char *);

static int
__osif_vdev_sync_start_callback(struct net_device *net_dev,
				struct osif_vdev_sync **out_vdev_sync,
				const char *desc,
				vdev_start_func vdev_start_cb)
{
	QDF_STATUS status;
	struct osif_vdev_sync *vdev_sync;

	*out_vdev_sync = NULL;

	vdev_sync = osif_vdev_sync_lookup(net_dev);
	if (!vdev_sync)
		return -EAGAIN;

	*out_vdev_sync = vdev_sync;

	status = vdev_start_cb(vdev_sync->dsc_vdev, desc);
	if (QDF_IS_STATUS_ERROR(status))
		return qdf_status_to_os_return(status);

	return 0;
}

static int
__osif_vdev_sync_start_wait_callback(struct net_device *net_dev,
				     struct osif_vdev_sync **out_vdev_sync,
				     const char *desc,
				     vdev_start_func vdev_start_cb)
{
	QDF_STATUS status;
	struct osif_vdev_sync *vdev_sync;

	*out_vdev_sync = NULL;

	osif_vdev_sync_lock();
	vdev_sync = osif_vdev_sync_lookup(net_dev);
	osif_vdev_sync_unlock();
	if (!vdev_sync)
		return -EAGAIN;

	status = vdev_start_cb(vdev_sync->dsc_vdev, desc);
	if (QDF_IS_STATUS_ERROR(status))
		return qdf_status_to_os_return(status);

	*out_vdev_sync = vdev_sync;

	return 0;
}

/**
 * osif_vdev_sync_wait_for_uptree_ops - Wait for psoc/driver operations
 * @vdev_sync: vdev sync pointer
 *
 * If there are any psoc/driver operations are taking place, then vdev
 * trans/ops should wait for these operations to be completed to avoid
 * memory domain mismatch issues. For example, if modules are closed
 * because of idle shutdown, memory domain will be init domain and at
 * that time if some psoc ops starts, memory allocated as part of this
 * ops will be allocated in init domain and if at the same time if vdev
 * up starts which will trigger the vdev trans and will start the
 * modules and change the memory domain to active domain, now when the
 * memory allocated as part of psoc operation is release on psoc ops
 * completion will be released in the active domain which leads the
 * memory domain mismatch.
 *
 * Return: None.
 */
static void osif_vdev_sync_wait_for_uptree_ops(struct osif_vdev_sync *vdev_sync)
{
	dsc_vdev_wait_for_uptree_ops(vdev_sync->dsc_vdev);
}

int __osif_vdev_sync_trans_start(struct net_device *net_dev,
				 struct osif_vdev_sync **out_vdev_sync,
				 const char *desc)
{
	int errno;

	osif_vdev_sync_lock();
	errno = __osif_vdev_sync_start_callback(net_dev, out_vdev_sync, desc,
						dsc_vdev_trans_start);
	osif_vdev_sync_unlock();

	if (!errno) {
		osif_vdev_sync_wait_for_ops(*out_vdev_sync);
		osif_vdev_sync_wait_for_uptree_ops(*out_vdev_sync);
	}

	return errno;
}

int __osif_vdev_sync_trans_start_wait(struct net_device *net_dev,
				      struct osif_vdev_sync **out_vdev_sync,
				      const char *desc)
{
	int errno;

	/* since dsc_vdev_trans_start_wait may sleep do not take lock here */
	errno = __osif_vdev_sync_start_wait_callback(net_dev,
						     out_vdev_sync, desc,
						     dsc_vdev_trans_start_wait);

	if (!errno) {
		osif_vdev_sync_wait_for_ops(*out_vdev_sync);
		osif_vdev_sync_wait_for_uptree_ops(*out_vdev_sync);
	}

	return errno;
}

void osif_vdev_sync_trans_stop(struct osif_vdev_sync *vdev_sync)
{
	dsc_vdev_trans_stop(vdev_sync->dsc_vdev);
}

void osif_vdev_sync_assert_trans_protected(struct net_device *net_dev)
{
	struct osif_vdev_sync *vdev_sync;

	osif_vdev_sync_lock();

	vdev_sync = osif_vdev_sync_lookup(net_dev);
	QDF_BUG(vdev_sync);
	if (vdev_sync)
		dsc_vdev_assert_trans_protected(vdev_sync->dsc_vdev);

	osif_vdev_sync_unlock();
}

int __osif_vdev_sync_op_start(struct net_device *net_dev,
			      struct osif_vdev_sync **out_vdev_sync,
			      const char *func)
{
	int errno;

	osif_vdev_sync_lock();
	errno = __osif_vdev_sync_start_callback(net_dev, out_vdev_sync, func,
						_dsc_vdev_op_start);
	osif_vdev_sync_unlock();

	return errno;
}

void __osif_vdev_sync_op_stop(struct osif_vdev_sync *vdev_sync,
			      const char *func)
{
	_dsc_vdev_op_stop(vdev_sync->dsc_vdev, func);
}

void osif_vdev_sync_wait_for_ops(struct osif_vdev_sync *vdev_sync)
{
	dsc_vdev_wait_for_ops(vdev_sync->dsc_vdev);
}

void osif_vdev_sync_init(void)
{
	osif_vdev_sync_lock_create();
}

void osif_vdev_sync_deinit(void)
{
	osif_vdev_sync_lock_destroy();
}

uint8_t osif_vdev_get_cached_cmd(struct osif_vdev_sync *vdev_sync)
{
	return dsc_vdev_get_cached_cmd(vdev_sync->dsc_vdev);
}

void osif_vdev_cache_command(struct osif_vdev_sync *vdev_sync, uint8_t cmd_id)
{
	dsc_vdev_cache_command(vdev_sync->dsc_vdev, cmd_id);
	osif_debug("Set cache cmd to %d", cmd_id);
}

/**
 * __osif_vdev_sync_wdev_start_callback() - internal helper to start a vdev
 *	operation or transition on @wdev using a caller-supplied start function
 * @wdev: the wireless_dev to operate/transition against
 * @out_vdev_sync: out parameter for the synchronization context, populated
 *	on success
 * @desc: description string passed to the DSC start function
 * @vdev_start_cb: the DSC vdev start function to invoke
 *
 * Looks up the vdev sync context associated with @wdev and invokes
 * @vdev_start_cb to start the operation or transition.
 *
 * Caller must hold __osif_vdev_sync_lock.
 *
 * Return: 0 on success, -EAGAIN if no context is registered for @wdev,
 *	or a negative errno from @vdev_start_cb on failure
 */
static int
__osif_vdev_sync_wdev_start_callback(struct wireless_dev *wdev,
				     struct osif_vdev_sync **out_vdev_sync,
				     const char *desc,
				     vdev_start_func vdev_start_cb)
{
	QDF_STATUS status;
	struct osif_vdev_sync *vdev_sync;

	*out_vdev_sync = NULL;

	vdev_sync = osif_vdev_sync_lookup_by_wdev(wdev);
	if (!vdev_sync)
		return -EAGAIN;

	*out_vdev_sync = vdev_sync;

	status = vdev_start_cb(vdev_sync->dsc_vdev, desc);
	if (QDF_IS_STATUS_ERROR(status))
		return qdf_status_to_os_return(status);

	return 0;
}

/**
 * __osif_vdev_sync_wdev_start_wait_callback() - internal helper to start a
 *	vdev operation or transition on @wdev, potentially blocking until the
 *	start function succeeds
 * @wdev: the wireless_dev to operate/transition against
 * @out_vdev_sync: out parameter for the synchronization context, populated
 *	on success
 * @desc: description string passed to the DSC start function
 * @vdev_start_cb: the DSC vdev start function to invoke (may sleep)
 *
 * Looks up the vdev sync context associated with @wdev under the spinlock,
 * then releases the lock before invoking @vdev_start_cb, which may block.
 *
 * Return: 0 on success, -EAGAIN if no context is registered for @wdev,
 *	or a negative errno from @vdev_start_cb on failure
 */
static int
__osif_vdev_sync_wdev_start_wait_callback(struct wireless_dev *wdev,
					  struct osif_vdev_sync **out_vdev_sync,
					  const char *desc,
					  vdev_start_func vdev_start_cb)
{
	QDF_STATUS status;
	struct osif_vdev_sync *vdev_sync;

	*out_vdev_sync = NULL;

	osif_vdev_sync_lock();
	vdev_sync = osif_vdev_sync_lookup_by_wdev(wdev);
	osif_vdev_sync_unlock();
	if (!vdev_sync)
		return -EAGAIN;

	status = vdev_start_cb(vdev_sync->dsc_vdev, desc);
	if (QDF_IS_STATUS_ERROR(status))
		return qdf_status_to_os_return(status);

	*out_vdev_sync = vdev_sync;

	return 0;
}

/**
 * __osif_vdev_sync_wdev_op_start() - attempt to start an operation on @wdev
 * @wdev: the wireless_dev to operate against
 * @out_vdev_sync: out parameter for the synchronization context registered
 *	with @wdev, populated on success
 * @func: name of the calling function, used for debug tracking
 *
 * Acquires the vdev sync spinlock and attempts to start a DSC operation on
 * the vdev sync context registered for @wdev.  This is the underlying
 * implementation invoked by the osif_vdev_sync_wdev_op_start() macro.
 *
 * Return: 0 on success, -EAGAIN if @wdev is not yet registered, or a
 *	negative errno on other failures
 */
int __osif_vdev_sync_wdev_op_start(struct wireless_dev *wdev,
				   struct osif_vdev_sync **out_vdev_sync,
				   const char *func)
{
	int errno;

	osif_vdev_sync_lock();
	errno = __osif_vdev_sync_wdev_start_callback(wdev, out_vdev_sync, func,
						     _dsc_vdev_op_start);
	osif_vdev_sync_unlock();

	return errno;
}

/**
 * __osif_vdev_sync_wdev_trans_start() - attempt to start a transition on @wdev
 * @wdev: the wireless_dev to transition
 * @out_vdev_sync: out parameter for the synchronization context registered
 *	with @wdev, populated on success
 * @desc: description of the transition, used for debug tracking
 *
 * Acquires the vdev sync spinlock and attempts to start a DSC transition on
 * the vdev sync context registered for @wdev.  On success, waits for any
 * in-flight operations and uptree (psoc/driver) operations to complete before
 * returning.  This is the underlying implementation invoked by the
 * osif_vdev_sync_wdev_trans_start() macro.
 *
 * Return: 0 on success, -EAGAIN if @wdev is not yet registered, or a
 *	negative errno on other failures
 */
int __osif_vdev_sync_wdev_trans_start(struct wireless_dev *wdev,
				      struct osif_vdev_sync **out_vdev_sync,
				      const char *desc)
{
	int errno;

	osif_vdev_sync_lock();
	errno = __osif_vdev_sync_wdev_start_callback(wdev, out_vdev_sync, desc,
						     dsc_vdev_trans_start);
	osif_vdev_sync_unlock();

	if (!errno) {
		osif_vdev_sync_wait_for_ops(*out_vdev_sync);
		osif_vdev_sync_wait_for_uptree_ops(*out_vdev_sync);
	}

	return errno;
}

/**
 * __osif_vdev_sync_wdev_trans_start_wait() - attempt to start a transition on
 *	@wdev, blocking if a conflicting transition is in flight
 * @wdev: the wireless_dev to transition
 * @out_vdev_sync: out parameter for the synchronization context registered
 *	with @wdev, populated on success
 * @desc: description of the transition, used for debug tracking
 *
 * Attempts to start a DSC transition on the vdev sync context registered for
 * @wdev.  Unlike __osif_vdev_sync_wdev_trans_start(), the spinlock is NOT held
 * while invoking the DSC start function because dsc_vdev_trans_start_wait()
 * may sleep.  On success, waits for any in-flight operations and uptree
 * (psoc/driver) operations to complete before returning.  This is the
 * underlying implementation invoked by the
 * osif_vdev_sync_wdev_trans_start_wait() macro.
 *
 * Return: 0 on success, -EAGAIN if @wdev is not yet registered, or a
 *	negative errno on other failures
 */
int __osif_vdev_sync_wdev_trans_start_wait(
					struct wireless_dev *wdev,
					struct osif_vdev_sync **out_vdev_sync,
					const char *desc)
{
	int errno;

	/* since dsc_vdev_trans_start_wait may sleep do not take lock here */
	errno = __osif_vdev_sync_wdev_start_wait_callback(
						wdev, out_vdev_sync, desc,
						dsc_vdev_trans_start_wait);

	if (!errno) {
		osif_vdev_sync_wait_for_ops(*out_vdev_sync);
		osif_vdev_sync_wait_for_uptree_ops(*out_vdev_sync);
	}

	return errno;
}
