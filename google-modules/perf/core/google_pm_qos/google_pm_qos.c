// SPDX-License-Identifier: GPL-2.0-only
/*
 * Auditability layer for PM QoS
 *
 * Copyright (C) 2024 Google LLC
 *
 * This module create a layer for frequency vote registrations (and unregistrations)
 * such that we can easily monitor what driver is registering votes against
 * pm_qos domains through a set of google_pm_qos API.
 *
 * This information is accessible for each devfreq through debugfs.
 *
 */

#include <linux/debugfs.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <trace/events/power.h>
#include <perf/core/google_pm_qos.h>

static struct dentry *debugfs_dir;

enum vote_type {
	GOOGLE_PM_QOS_MIN = 1,
	GOOGLE_PM_QOS_MAX,
};

struct google_freq_request {
	enum vote_type type;
	union {
		struct dev_pm_qos_request *dev_req;
		struct freq_qos_request *cpu_req;
	} req;
	const char *func;
	unsigned int line;
	struct list_head list;
};

enum domain_type {
	CPUFREQ_PM_QOS = 1,
	DEVFREQ_PM_QOS,
};
struct google_freq_tracker {
	enum domain_type type;
	union {
		struct devfreq *devfreq;
		struct cpufreq_policy *policy;
	} domain;
	struct dentry *debugfs_node;
	bool externally_registered;
	struct list_head freq_requests;
	struct list_head list;
};

LIST_HEAD(google_freq_tracker_list);

/*
 * locking rule: all changes to google_freq_tracker_list
 * or google_freq_tracker and google_freq_request need to
 * happen with google_pm_qos_lock held.
 */
static DEFINE_MUTEX(google_pm_qos_lock);


static int get_freq_request_count(struct list_head *req_list,
			enum vote_type type)
{
	struct google_freq_request *google_req;
	int tot_reqs = 0;

	list_for_each_entry(google_req, req_list, list) {
		if (google_req->type == type)
			tot_reqs++;
	}
	return tot_reqs;
}

static s32 read_freq_value(struct pm_qos_constraints *c)
{
	return READ_ONCE(c->target_value);
}

static int get_freq_tracker_value(struct google_freq_tracker *tracker,
				enum vote_type req_type,
				enum domain_type type)
{
	struct freq_constraints *freq_constraints;
	struct device *dev;

	switch (type) {
	case CPUFREQ_PM_QOS:
		freq_constraints = &tracker->domain.policy->constraints;
		break;
	case DEVFREQ_PM_QOS:
		dev = tracker->domain.devfreq->dev.parent;
		freq_constraints = &dev->power.qos->freq;
		break;
	default:
		BUG();
		break;
	}

	switch (req_type) {
	case GOOGLE_PM_QOS_MIN:
		return read_freq_value(&freq_constraints->min_freq);

	case GOOGLE_PM_QOS_MAX:
		return read_freq_value(&freq_constraints->max_freq);

	default:
		WARN(1, "Unknown PM QoS type in %s\n", __func__);
		return -EINVAL;
	}
}

static void google_pm_qos_debug_helper(struct seq_file *s,
					struct list_head *req_list,
					enum vote_type req_type,
					enum domain_type type)
{
	struct google_freq_request *google_req;

	switch (type) {
	case DEVFREQ_PM_QOS:
		list_for_each_entry(google_req, req_list, list) {
			if (google_req->type == req_type) {
				struct dev_pm_qos_request *req = google_req->req.dev_req;
				seq_printf(s, "  %s:%d %d\n",
					google_req->func,
					google_req->line,
					req->data.freq.pnode.prio);
			}
	}
		break;
	case CPUFREQ_PM_QOS:
		list_for_each_entry(google_req, req_list, list) {
			if (google_req->type == req_type) {
				struct freq_qos_request *req = google_req->req.cpu_req;
				seq_printf(s, "  %s:%d %d\n",
						google_req->func,
						google_req->line,
						req->pnode.prio);
			}
		}
		break;
	default:
		BUG();
		break;
	}

}

static int google_pm_qos_debug_show(struct seq_file *s, void *unused)
{
	struct google_freq_tracker *tracker;
	int ret = 0;

	mutex_lock(&google_pm_qos_lock);
	tracker = (struct google_freq_tracker *)s->private;
	if (!tracker) {
		ret = -EINVAL;
		goto out;
	}

	seq_printf(s, "Min requests in KHz (%d request(s), aggregated min vote: %d):\n",
			get_freq_request_count(&tracker->freq_requests, GOOGLE_PM_QOS_MIN),
			get_freq_tracker_value(tracker, GOOGLE_PM_QOS_MIN, tracker->type));

	google_pm_qos_debug_helper(s, &tracker->freq_requests, GOOGLE_PM_QOS_MIN, tracker->type);

	seq_printf(s, "\nMax requests in KHz (%d request(s), aggregated max vote: %d):\n",
			get_freq_request_count(&tracker->freq_requests, GOOGLE_PM_QOS_MAX),
			get_freq_tracker_value(tracker, GOOGLE_PM_QOS_MAX, tracker->type));

	google_pm_qos_debug_helper(s, &tracker->freq_requests, GOOGLE_PM_QOS_MAX, tracker->type);

out:
	mutex_unlock(&google_pm_qos_lock);
	return ret;
}

DEFINE_SHOW_ATTRIBUTE(google_pm_qos_debug);

static struct google_freq_tracker *get_devfreq_tracker(struct devfreq *devfreq)
{
	struct google_freq_tracker *tracker;

	list_for_each_entry(tracker, &google_freq_tracker_list, list) {
		if (tracker->type == DEVFREQ_PM_QOS && tracker->domain.devfreq == devfreq) {
			return tracker;
		}
	}
	return NULL;
}


static struct google_freq_request *get_google_pm_qos_dev_request(
					struct dev_pm_qos_request *req,
					struct list_head *req_list)
{
	struct google_freq_request *google_req;

	list_for_each_entry(google_req, req_list, list) {
		if (google_req->req.dev_req == req) {
			return google_req;
		}
	}

	return NULL;
}

static struct google_freq_tracker *get_cpufreq_tracker(struct cpufreq_policy *policy)
{
	struct google_freq_tracker *tracker;

	list_for_each_entry(tracker, &google_freq_tracker_list, list) {
		if (tracker->type == CPUFREQ_PM_QOS && tracker->domain.policy == policy) {
			return tracker;
		}
	}
	return NULL;
}

static struct google_freq_request *get_google_pm_qos_cpu_request(
					struct freq_qos_request *req,
					struct list_head *req_list)
{
	struct google_freq_request *google_req;

	list_for_each_entry(google_req, req_list, list) {
		if (google_req->req.cpu_req == req) {
			return google_req;
		}
	}

	return NULL;
}

static void init_debug_folder(struct device *dev,
					struct google_freq_tracker *tracker)
{
	struct dentry *debugfs_file;

	debugfs_file = debugfs_lookup(dev_name(dev), debugfs_dir);
	if (!debugfs_file) {
		debugfs_file = debugfs_create_file(dev_name(dev), 0444, debugfs_dir,
					(void *)tracker,
					&google_pm_qos_debug_fops);
		tracker->debugfs_node = debugfs_file;
	}
}

static struct google_freq_tracker *__google_register_devfreq(struct devfreq *devfreq)
{
	struct google_freq_tracker *tracker;
	int err;

	tracker = get_devfreq_tracker(devfreq);
	if (tracker)
		return tracker;

	tracker = kzalloc(sizeof(struct google_freq_tracker), GFP_KERNEL);
	if (!tracker)
		return NULL;

	tracker->type = DEVFREQ_PM_QOS;
	tracker->domain.devfreq = devfreq;
	INIT_LIST_HEAD(&tracker->freq_requests);

	list_add_tail(&tracker->list, &google_freq_tracker_list);

	err = __google_pm_qos_register_devfreq_request("default_min", 0, devfreq,
					&devfreq->user_min_freq_req);
	if (err < 0) {
		pr_err("%s: devfreq %s fail to register default_min request.\n", __func__,
				dev_name(devfreq->dev.parent));
		goto err_out;
	}
	err = __google_pm_qos_register_devfreq_request("default_max", 0, devfreq,
					&devfreq->user_max_freq_req);
	if (err < 0) {
		pr_err("%s: devfreq %s fail to register default_max request.\n", __func__,
				dev_name(devfreq->dev.parent));
		__google_pm_qos_unregister_devfreq_request(devfreq, &devfreq->user_min_freq_req);
		goto err_out;
	}
	return tracker;

err_out:
	list_del(&tracker->list);
	kfree(tracker);
	return NULL;
}

/**
 * google_register_devfreq - register devfreq into google_freq_tracker
 * @devfreq: target devfreq for the constraint
 *
 * Should come in pair with google_unregister_devfreq()
 *
 * An effective call of this function
 * 1. enable google_freq_tracker to track for the devfreq
 * 2. automatically register upstream pm_qos devfreq request(user_min_freq_req, user_max_freq_req)
 * 3. mark externally_registered as true
 *
 * If a module calls this function successfully, it should also call
 * google_unregister_devfreq when it's unloading
 *
 * Return 0 if the devfreq is effectively registered, or a negative error code on failures.
 */
int google_register_devfreq(struct devfreq *devfreq)
{
	struct google_freq_tracker *tracker;
	int ret = 0;

	if (!devfreq)
		return -EINVAL;

	mutex_lock(&google_pm_qos_lock);
	tracker = __google_register_devfreq(devfreq);
	if (!tracker){
		ret = -ENOMEM;
		goto out;
	}
	if (tracker->externally_registered) {
		pr_err("%s: devfreq %s already registered.\n", __func__,
				dev_name(devfreq->dev.parent));
		ret = -EINVAL;
		goto out;
	}

	tracker->externally_registered = true;
out:
	mutex_unlock(&google_pm_qos_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_register_devfreq);


/**
 * __google_pm_qos_register_devfreq_request - register existing pm_qos devfreq request
 * @func: the caller of the API
 * @line: the line number for calling the API
 * @devfreq: target devfreq for the constraint
 * @req: pointer to a preallocated handle
 *
 * This function should be used to register votes created in upstream code
 * against pm_qos domains.
 *
 * Some pre-existing pm_qos request that potentially impact final voting decision can be
 * registered in this module for monitoring purpose.
 *
 * This function should not be used for votes created in vendor code (which should use
 * the google_pm_qos_add_and_register_request API instead), and does not need to be called
 * on the user_min_freq_req and user_max_freq_req requests automatically created with the
 * devfreq itself (which are already handled during devfreq registration).
 *
 * Return 0 if the request is effective registered, or a negative error code on failures.
 */
int __google_pm_qos_register_devfreq_request(const char *func,
					unsigned int line,
					struct devfreq *devfreq,
					struct dev_pm_qos_request *req)
{
	struct device *dev;
	struct google_freq_tracker *tracker;
	struct google_freq_request *google_req;

	if (!devfreq || !req)
		return -EINVAL;

	tracker = __google_register_devfreq(devfreq);
	if (!tracker)
		return -ENOMEM;

	dev = devfreq->dev.parent;
	google_req = get_google_pm_qos_dev_request(req, &tracker->freq_requests);
	if (google_req) {
		pr_err("%s: %d : %s google_freq_request already registered.\n",
			func, line, dev_name(dev));
		return -EINVAL;
	}

	google_req = kzalloc(sizeof(struct google_freq_request), GFP_KERNEL);
	if (!google_req)
		return -ENOMEM;

	google_req->req.dev_req = req;
	google_req->func = func;
	google_req->line = line;

	switch(req->type) {
	case DEV_PM_QOS_MIN_FREQUENCY:
		google_req->type = GOOGLE_PM_QOS_MIN;
		break;
	case DEV_PM_QOS_MAX_FREQUENCY:
		google_req->type = GOOGLE_PM_QOS_MAX;
		break;
	default:
		break;
	}
	list_add_tail(&google_req->list, &tracker->freq_requests);

	init_debug_folder(dev, tracker);
	return 0;
}
EXPORT_SYMBOL_GPL(__google_pm_qos_register_devfreq_request);

/**
 * __google_pm_qos_add_and_register_devfreq_request - add and register new pm_qos
 * devfreq request
 * @func: the caller of the API
 * @line: the line number for calling the API
 * @devfreq: target devfreq for the constraint
 * @req: pointer to a preallocated handle
 * @type: type of the request
 * @value: initial value(in Khz) for the qos request
 *
 * It is the intended primary way to register votes (add and register altogether).
 * Internally calling dev_pm_qos_add_request defined in upstream, this function also
 * registers votes against pm_qos domains.
 *
 * If a module calls this function successfully, it should also call
 * google_pm_qos_unregister_and_remove_devfreq_request when it's unloading.
 *
 * Upstream PM Qos frequencies are in kHz. The value here should also be in kHz
 * to keep values consistent. Usually frequencies in devfreq are in Hz so a
 * conversion (with HZ_PER_KHZ) may be necessary before calling this function.
 *
 * Return 0 if the request is effective add and registered, or a negative
 * error code on failures.
 */
int __google_pm_qos_add_and_register_devfreq_request(const char *func,
					unsigned int line,
					struct devfreq *devfreq,
					struct dev_pm_qos_request *req,
					enum dev_pm_qos_req_type type,
					s32 value)
{
	int ret;
	struct device *dev;

	if (!devfreq)
		return -EINVAL;
	dev = devfreq->dev.parent;

	ret = dev_pm_qos_add_request(dev, req, type, value);
	if (ret < 0)
		return ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_register_devfreq_request(func, line, devfreq, req);
	if (ret < 0)
		dev_pm_qos_remove_request(req);
	mutex_unlock(&google_pm_qos_lock);
	return ret;

}
EXPORT_SYMBOL_GPL(__google_pm_qos_add_and_register_devfreq_request);

/**
 * google_pm_qos_register_devfreq_request- register pm_qos devfreq request
 * @devfreq: target devfreq for the constraint
 * @req: pointer to a preallocated handle
 * @name: name as identifier of the req
 *
 * This function only registers existing votes against pm_qos domains
 * Return 0 if the request is effective registered,
 * or a negative error code on failures.
 */
int google_pm_qos_register_devfreq_request(struct devfreq *devfreq,
					struct dev_pm_qos_request *req,
					const char *name)
{
	int ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_register_devfreq_request(name, 0, devfreq, req);
	mutex_unlock(&google_pm_qos_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(google_pm_qos_register_devfreq_request);

static void __google_unregister_devfreq(struct devfreq *devfreq,
					struct google_freq_tracker *tracker)
{

	__google_pm_qos_unregister_devfreq_request(devfreq, &devfreq->user_min_freq_req);
	__google_pm_qos_unregister_devfreq_request(devfreq, &devfreq->user_max_freq_req);

	if (list_empty(&tracker->freq_requests)) {
		debugfs_remove(tracker->debugfs_node);
		list_del(&tracker->list);
		kfree(tracker);
	}
}

int __google_pm_qos_unregister_devfreq_request(struct devfreq *devfreq,
					struct dev_pm_qos_request *req)
{
	struct google_freq_tracker *tracker;
	struct google_freq_request *google_req;

	if (!devfreq)
		return -EINVAL;

	tracker = get_devfreq_tracker(devfreq);
	if (!tracker || !req)
		return -EINVAL;

	google_req = get_google_pm_qos_dev_request(req, &tracker->freq_requests);

	if (!google_req) {
		pr_err("%s: %s google_freq_request is not registered.\n", __func__,
				dev_name(devfreq->dev.parent));
		return -EINVAL;
	}

	list_del(&google_req->list);
	kfree(google_req);

	if (tracker->externally_registered)
		return 0;

	__google_unregister_devfreq(devfreq, tracker);
	return 0;
}

/**
 * google_unregister_devfreq - unregister devfreq from google_freq_tracker
 * @devfreq: target devfreq for the constraint
 *
 * Should come in pair with google_register_devfreq()
 *
 * An effective call of this function
 * 1. unregister upstream pm_qos devfreq request(user_min_freq_req, user_max_freq_req)
 * 2. remove tracking from google_freq_tracker
 * 3. mark externally_registered as false
 *
 * Return 0 if the devfreq is effectively unregistered, or a negative error code on failures.
 */
int google_unregister_devfreq(struct devfreq *devfreq)
{
	struct google_freq_tracker *tracker;
	int ret = 0;

	if (!devfreq)
		return -EINVAL;

	mutex_lock(&google_pm_qos_lock);
	tracker = get_devfreq_tracker(devfreq);
	if (!tracker) {
		pr_err("%s: devfreq %s is not registered.\n",  __func__,
				dev_name(devfreq->dev.parent));
		ret = -EINVAL;
		goto out;
	}
	if (!tracker->externally_registered) {
		pr_err("%s: devfreq  %s is not externally registered.\n",  __func__,
				dev_name(devfreq->dev.parent));
		ret = -EINVAL;
		goto out;
	}
	__google_unregister_devfreq(devfreq, tracker);

	tracker->externally_registered = false;
out:
	mutex_unlock(&google_pm_qos_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_unregister_devfreq);


/**
 * google_pm_qos_unregister_devfreq_request- unregister pm_qos devfreq request
 * @devfreq: target devfreq for the constraint
 * @req: pointer to a preallocated handle
 *
 * This function only unregisters existing votes against pm_qos domains,
 * i.e. without also actually removing them from the devfreq.
 * Return 0 if the request is effective unregistered,
 * or a negative error code on failures.
 */
int google_pm_qos_unregister_devfreq_request(struct devfreq *devfreq,
					struct dev_pm_qos_request *req)
{
	int ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_unregister_devfreq_request(devfreq, req);
	mutex_unlock(&google_pm_qos_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(google_pm_qos_unregister_devfreq_request);

/**
 * google_pm_qos_unregister_and_remove_devfreq_request - remove and unregister
 * pm_qos devfreq request
 * @devfreq: target devfreq for the constraint
 * @req: pointer to a preallocated handle
 *
 * Internally calling dev_pm_qos_remove_request defined in upstream, this function
 * also unregister votes against pm_qos domains.
 *
 * Return 0 if the request is effective unregistered and removed,
 * or a negative error code on failures.
 */
int google_pm_qos_unregister_and_remove_devfreq_request(struct devfreq *devfreq,
					struct dev_pm_qos_request *req)
{
	int ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_unregister_devfreq_request(devfreq, req);
	mutex_unlock(&google_pm_qos_lock);

	if (ret < 0)
		return ret;
	return dev_pm_qos_remove_request(req);
}

EXPORT_SYMBOL_GPL(google_pm_qos_unregister_and_remove_devfreq_request);


static struct google_freq_tracker *__google_register_cpufreq(struct cpufreq_policy *policy)
{
	struct google_freq_tracker *tracker;
	struct device *dev;
	int err;

	tracker = get_cpufreq_tracker(policy);
	if (tracker)
		return tracker;

	tracker = kzalloc(sizeof(struct google_freq_tracker), GFP_KERNEL);
	if (!tracker)
		return NULL;

	tracker->type = CPUFREQ_PM_QOS;
	tracker->domain.policy = policy;
	INIT_LIST_HEAD(&tracker->freq_requests);

	list_add_tail(&tracker->list, &google_freq_tracker_list);

	dev = get_cpu_device(policy->cpu);

	err = __google_pm_qos_register_cpufreq_request("default_min", 0, policy,
					policy->min_freq_req);
	if (err < 0) {
		pr_err("%s: cpufreq %s fail to register default_min request.\n", __func__,
				dev_name(dev));
		goto err_out;
	}
	err = __google_pm_qos_register_cpufreq_request("default_max", 0, policy,
					policy->max_freq_req);
	if (err < 0) {
		pr_err("%s: cpufreq %s fail to register default_max request.\n", __func__,
				dev_name(dev));
		__google_pm_qos_unregister_cpufreq_request(policy, policy->min_freq_req);
		goto err_out;
	}
	return tracker;

err_out:
	list_del(&tracker->list);
	kfree(tracker);
	return NULL;
}

/**
 * google_register_cpufreq - register cpufreq into google_freq_tracker
 * @policy: target cpufreq policy for the constraint
 *
 * Should come in pair with google_unregister_cpufreq()
 *
 * An effective call of this function
 * 1. enable google_freq_tracker to track for the cpufreq
 * 2. automatically register upstream pm_qos cpufreq request(min_freq_req, max_freq_req)
 * 3. mark externally_registered as true
 *
 * If a module calls this function successfully, it should also call
 * google_unregister_cpufreq when it's unloading
 *
 * Return 0 if the cpufreq is effectively registered, or a negative error code on failures.
 */
int google_register_cpufreq(struct cpufreq_policy *policy)
{
	struct google_freq_tracker *tracker;
	int ret = 0;

	if (!policy)
		return -EINVAL;

	mutex_lock(&google_pm_qos_lock);
	tracker = __google_register_cpufreq(policy);
	if (!tracker) {
		ret = -ENOMEM;
		goto out;
	}
	if (tracker->externally_registered) {
		pr_err("%s: cpufreq %s already registered.\n", __func__,
				dev_name(get_cpu_device(policy->cpu)));
		ret = -EINVAL;
		goto out;
	}

	tracker->externally_registered = true;
out:
	mutex_unlock(&google_pm_qos_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_register_cpufreq);

/**
 * __google_pm_qos_register_cpufreq_request - register existing pm_qos cpufreq request
 * @func: the caller of the API
 * @line: the line number for calling the API
 * @policy: target cpufreq policy for the constraint
 * @req: pointer to a preallocated handle
 *
 * This function should be used to register votes created in upstream code
 * against pm_qos domains.
 *
 * Some pre-existing pm_qos request that potentially impact final voting decision can be
 * registered in this module for monitoring purpose.
 *
 * This function should not be used for votes created in vendor code (which should use
 * the google_pm_qos_add_and_register_request API instead), and does not need to be called
 * on the min_freq_req and max_freq_req requests automatically created with the
 * cpufreq itself (which are already handled during cpufreq registration).
 *
 * Return 0 if the request is effective registered, or a negative error code on failures.
 */
int __google_pm_qos_register_cpufreq_request(const char *func,
					unsigned int line,
					struct cpufreq_policy *policy,
					struct freq_qos_request *req)
{
	struct device *dev;
	struct google_freq_tracker *tracker;
	struct google_freq_request *google_req;

	if (!policy || !req)
		return -EINVAL;

	tracker = __google_register_cpufreq(policy);
	if (!tracker)
		return -ENOMEM;

	dev = get_cpu_device(policy->cpu);
	google_req = get_google_pm_qos_cpu_request(req, &tracker->freq_requests);
	if (google_req) {
		pr_err("%s: %d : cpu%u: google_freq_request already registered.\n",
			func, line, policy->cpu);
		return -EINVAL;
	}

	google_req = kzalloc(sizeof(struct google_freq_request), GFP_KERNEL);
	if (!google_req)
		return -ENOMEM;

	switch (req->type) {
	case FREQ_QOS_MIN:
		google_req->type = GOOGLE_PM_QOS_MIN;
		break;
	case FREQ_QOS_MAX:
		google_req->type = GOOGLE_PM_QOS_MAX;
		break;
	default:
		break;
	}
	google_req->req.cpu_req = req;

	google_req->func = func;
	google_req->line = line;

	list_add_tail(&google_req->list, &tracker->freq_requests);
	init_debug_folder(dev, tracker);
	return 0;
}
EXPORT_SYMBOL_GPL(__google_pm_qos_register_cpufreq_request);

/**
 * __google_pm_qos_add_and_register_cpufreq_request - add and register new pm_qos
 * cpufreq request
 * @func: the caller of the API
 * @line: the line number for calling the API
 * @policy: target cpufreq policy for the constraint
 * @req: pointer to a preallocated handle
 * @type: type of the request
 * @value: initial value for the qos request
 *
 * It is the intended primary way to register votes (add and register altogether).
 * Internally calling freq_qos_add_request defined in upstream, this function also
 * registers votes against pm_qos domains.
 *
 * If a module calls this function successfully, it should also call
 * google_pm_qos_unregister_and_remove_cpufreq_request when it's unloading.
 *
 * Return 0 if the request is effective add and registered, or a negative
 * error code on failures.
 */
int __google_pm_qos_add_and_register_cpufreq_request(const char *func,
					unsigned int line,
					struct cpufreq_policy *policy,
					struct freq_qos_request *req,
					enum freq_qos_req_type type,
					s32 value)
{
	int ret;

	if (!policy)
		return -EINVAL;

	ret = freq_qos_add_request(&policy->constraints, req, type, value);
	if (ret < 0)
		return ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_register_cpufreq_request(func, line, policy, req);
	if (ret < 0)
		freq_qos_remove_request(req);
	mutex_unlock(&google_pm_qos_lock);
	return ret;

}
EXPORT_SYMBOL_GPL(__google_pm_qos_add_and_register_cpufreq_request);

/**
 * google_pm_qos_register_cpufreq_request- register pm_qos cpufreq request
 * @policy: target cpufreq policy for the constraint
 * @req: pointer to a preallocated handle
 * @name: name as identifier of the req
 *
 * This function only registers existing votes against pm_qos domains
 * Return 0 if the request is effective registered,
 * or a negative error code on failures.
 */
int google_pm_qos_register_cpufreq_request(struct cpufreq_policy *policy,
					struct freq_qos_request *req,
					const char *name)
{
	int ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_register_cpufreq_request(name, 0, policy, req);
	mutex_unlock(&google_pm_qos_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(google_pm_qos_register_cpufreq_request);

static void __google_unregister_cpufreq(struct cpufreq_policy *policy,
					struct google_freq_tracker *tracker)
{

	__google_pm_qos_unregister_cpufreq_request(policy, policy->min_freq_req);
	__google_pm_qos_unregister_cpufreq_request(policy, policy->max_freq_req);

	if (list_empty(&tracker->freq_requests)) {
		debugfs_remove(tracker->debugfs_node);
		list_del(&tracker->list);
		kfree(tracker);
	}
}

int __google_pm_qos_unregister_cpufreq_request(struct cpufreq_policy *policy,
					struct freq_qos_request *req)
{
	struct google_freq_tracker *tracker;
	struct google_freq_request *google_req;

	if (!policy)
		return -EINVAL;

	tracker = get_cpufreq_tracker(policy);
	if (!tracker || !req)
		return -EINVAL;

	google_req = get_google_pm_qos_cpu_request(req, &tracker->freq_requests);

	if (!google_req) {
		pr_err("%s: %s google_freq_request is not registered.\n", __func__,
				dev_name(get_cpu_device(policy->cpu)));
		return -EINVAL;
	}

	list_del(&google_req->list);
	kfree(google_req);

	if (tracker->externally_registered)
		return 0;

	__google_unregister_cpufreq(policy, tracker);
	return 0;
}

/**
 * google_unregister_cpufreq - unregister cpufreq from google_freq_tracker
 * @policy: target cpufreq policy for the constraint
 *
 * Should come in pair with google_register_cpufreq()
 *
 * An effective call of this function
 * 1. unregister upstream pm_qos cpufreq request(min_freq_req, max_freq_req)
 * 2. remove tracking from google_freq_tracker
 * 3. mark externally_registered as false
 *
 * Return 0 if the cpufreq is effectively unregistered, or a negative error code on failures.
 */
int google_unregister_cpufreq(struct cpufreq_policy *policy)
{
	struct google_freq_tracker *tracker;
	struct device *dev;
	int ret = 0;

	if (!policy)
		return -EINVAL;
	dev = get_cpu_device(policy->cpu);

	mutex_lock(&google_pm_qos_lock);
	tracker = get_cpufreq_tracker(policy);
	if (!tracker) {
		pr_err("%s: cpufreq %s is not registered.\n",  __func__,
				dev_name(dev));
		ret = -EINVAL;
		goto out;
	}
	if (!tracker->externally_registered) {
		pr_err("%s: cpufreq  %s is not externally registered.\n",  __func__,
				dev_name(dev));
		ret = -EINVAL;
		goto out;
	}
	__google_unregister_cpufreq(policy, tracker);

	tracker->externally_registered = false;
out:
	mutex_unlock(&google_pm_qos_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_unregister_cpufreq);

/**
 * google_pm_qos_unregister_cpufreq_request- unregister pm_qos cpufreq request
 * @policy: target cpufreq policy for the constraint
 * @req: pointer to a preallocated handle
 *
 * This function only unregisters existing votes against pm_qos domains,
 * i.e. without also actually removing them from the cpufreq.
 * Return 0 if the request is effective unregistered,
 * or a negative error code on failures.
 */
int google_pm_qos_unregister_cpufreq_request(struct cpufreq_policy *policy,
					struct freq_qos_request *req)
{
	int ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_unregister_cpufreq_request(policy, req);
	mutex_unlock(&google_pm_qos_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(google_pm_qos_unregister_cpufreq_request);

/**
 * google_pm_qos_unregister_and_remove_cpufreq_request - remove and unregister
 * pm_qos cpufreq request
 * @policy: target cpufreq policy for the constraint
 * @req: pointer to a preallocated handle
 *
 * Internally calling freq_qos_remove_request defined in upstream, this function
 * also unregister votes against pm_qos domains.
 *
 * Return 0 if the request is effective unregistered and removed,
 * or a negative error code on failures.
 */
int google_pm_qos_unregister_and_remove_cpufreq_request(struct cpufreq_policy *policy,
					struct freq_qos_request *req)
{
	int ret;

	mutex_lock(&google_pm_qos_lock);
	ret = __google_pm_qos_unregister_cpufreq_request(policy, req);
	mutex_unlock(&google_pm_qos_lock);

	if (ret < 0)
		return ret;
	return freq_qos_remove_request(req);
}

EXPORT_SYMBOL_GPL(google_pm_qos_unregister_and_remove_cpufreq_request);

static int __init google_pm_qos_init(void)
{
	debugfs_dir = debugfs_create_dir("google_pm_qos", NULL);
	return 0;
}

module_init(google_pm_qos_init);
MODULE_AUTHOR("Ziyi Cui <ziyic@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google PM QoS");
