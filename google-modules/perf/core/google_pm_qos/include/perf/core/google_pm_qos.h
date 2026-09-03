/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC
 */

/*
 * NB: google_pm_qos api is only for long-live request.
 * The expectation is that requests are only added (and removed) rarely, for instance
 * at probe time. Modifications to requests can happen as often as needed.
 */

#include <linux/devfreq.h>
#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)

extern int google_register_devfreq(struct devfreq *devfreq);
extern int google_unregister_devfreq(struct devfreq *devfreq);

extern int __google_pm_qos_add_and_register_devfreq_request(const char *func, unsigned int line,
							    struct devfreq *devfreq,
							    struct dev_pm_qos_request *req,
							    enum dev_pm_qos_req_type type,
							    s32 value);

extern int __google_pm_qos_register_devfreq_request(const char *func, unsigned int line,
						    struct devfreq *devfreq,
						    struct dev_pm_qos_request *req);

extern int __google_pm_qos_unregister_devfreq_request(struct devfreq *devfreq,
						      struct dev_pm_qos_request *req);

#define google_pm_qos_add_devfreq_request(devfreq, req, type, value)                             \
	__google_pm_qos_add_and_register_devfreq_request(__func__, __LINE__, devfreq, req, type, \
							 value)

extern int google_pm_qos_register_devfreq_request(struct devfreq *devfreq,
						  struct dev_pm_qos_request *req, const char *name);

#define google_pm_qos_remove_devfreq_request(devfreq, req) \
	google_pm_qos_unregister_and_remove_devfreq_request(devfreq, req)

extern int google_pm_qos_unregister_and_remove_devfreq_request(struct devfreq *devfreq,
							       struct dev_pm_qos_request *req);
extern int google_pm_qos_unregister_devfreq_request(struct devfreq *devfreq,
						    struct dev_pm_qos_request *req);

extern int google_register_cpufreq(struct cpufreq_policy *policy);
extern int google_unregister_cpufreq(struct cpufreq_policy *policy);

int __google_pm_qos_add_and_register_cpufreq_request(const char *func, unsigned int line,
						     struct cpufreq_policy *policy,
						     struct freq_qos_request *req,
						     enum freq_qos_req_type type, s32 value);

int __google_pm_qos_register_cpufreq_request(const char *func, unsigned int line,
					     struct cpufreq_policy *policy,
					     struct freq_qos_request *req);

int __google_pm_qos_unregister_cpufreq_request(struct cpufreq_policy *policy,
					       struct freq_qos_request *req);

#define google_pm_qos_add_cpufreq_request(policy, req, type, value)                             \
	__google_pm_qos_add_and_register_cpufreq_request(__func__, __LINE__, policy, req, type, \
							 value)

int google_pm_qos_register_cpufreq_request(struct cpufreq_policy *policy,
					   struct freq_qos_request *req, const char *name);

#define google_pm_qos_remove_cpufreq_request(policy, req) \
	google_pm_qos_unregister_and_remove_cpufreq_request(policy, req)

extern int google_pm_qos_unregister_and_remove_cpufreq_request(struct cpufreq_policy *policy,
							       struct freq_qos_request *req);
extern int google_pm_qos_unregister_cpufreq_request(struct cpufreq_policy *policy,
						    struct freq_qos_request *req);
#else

static inline int google_register_devfreq(struct devfreq *devfreq)
{
	return 0;
}
static inline int google_unregister_devfreq(struct devfreq *devfreq)
{
	return 0;
}

static inline int google_pm_qos_register_devfreq_request(struct devfreq *devfreq,
							 struct dev_pm_qos_request *req,
							 const char *name)
{
	return 0;
}
static inline int google_pm_qos_unregister_devfreq_request(struct devfreq *devfreq,
							   struct dev_pm_qos_request *req)
{
	return 0;
}

#define google_pm_qos_add_devfreq_request(devfreq, req, type, value)                   \
	({                                                                             \
		if (devfreq)                                                           \
			dev_pm_qos_add_request(devfreq->dev.parent, req, type, value); \
		0;                                                                     \
	})
#define google_pm_qos_remove_devfreq_request(devfreq, req) \
	({ (void)(devfreq); dev_pm_qos_remove_request(req); })

static inline int google_register_cpufreq(struct cpufreq_policy *policy)
{
	return 0;
}
static inline int google_unregister_cpufreq(struct cpufreq_policy *policy)
{
	return 0;
}

static inline int google_pm_qos_register_cpufreq_request(struct cpufreq_policy *policy,
							 struct freq_qos_request *req,
							 const char *name)
{
	return 0;
}
static inline int google_pm_qos_unregister_cpufreq_request(struct cpufreq_policy *policy,
							   struct freq_qos_request *req)
{
	return 0;
}

#define google_pm_qos_add_cpufreq_request(policy, req, type, value)                   \
	({                                                                            \
		if (policy)                                                           \
			freq_qos_add_request(&policy->constraints, req, type, value); \
		0;                                                                    \
	})
#define google_pm_qos_remove_cpufreq_request(policy, req) \
	({ (void)(policy); freq_qos_remove_request(req); })

#endif // enabled(GOOGLE_PM_QOS)
