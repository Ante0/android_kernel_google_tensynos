#define pr_fmt(fmt) "pixel_trim: " fmt

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "pixel_trim_core.h"
#include "pixel_trim_sysfs.h"

extern struct kobject *vendor_mm_kobj;
static struct kobject pixel_trim_kobj;
extern struct task_struct *trim_task;
static DEFINE_MUTEX(sysfs_lock);

/*
 * This all compiles without CONFIG_SYSFS, but is a waste of space.
 */
#define PIXEL_TRIM_ATTR_RO(_name) \
        static struct kobj_attribute _name##_attr = __ATTR_RO(_name)

#define PIXEL_TRIM_ATTR_RW(_name) \
        static struct kobj_attribute _name##_attr = __ATTR_RW(_name)

#define PIXEL_TRIM_ATTR_WO(_name) \
        static struct kobj_attribute _name##_attr = __ATTR_WO(_name)

static ssize_t trim_store(struct kobject *kobj,
                          struct kobj_attribute *attr,
                          const char *buf, size_t len)
{
        unsigned long reclaim_kb;

        if (kstrtoul(buf, 10, &reclaim_kb))
                return -EINVAL;

	if (reclaim_kb)
		run_trim();

        return len;
}
PIXEL_TRIM_ATTR_WO(trim);

static ssize_t cpu_affinity_store(struct kobject *kobj,
				  struct kobj_attribute *attr, const char *buf, size_t len)
{
	cpumask_t cpumask;
	int ret;

	mutex_lock(&sysfs_lock);
	ret = cpumask_parse(buf, &cpumask);
	if (ret < 0 || cpumask_empty(&cpumask)) {
		mutex_unlock(&sysfs_lock);
		return -EINVAL;
	}

	trim_set_cpu_affinity(&cpumask);
	mutex_unlock(&sysfs_lock);

	return len;
}

static ssize_t cpu_affinity_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&sysfs_lock);
	ret = cpumap_print_to_pagebuf(false, buf, trim_get_cpu_affinity());
	mutex_unlock(&sysfs_lock);

	return ret;
}
PIXEL_TRIM_ATTR_RW(cpu_affinity);

static struct attribute *pixel_trim_attrs[] = {
        &trim_attr.attr,
	&cpu_affinity_attr.attr,
        NULL,
};

static const struct attribute_group pixel_trim_attr_group = {
        .attrs = pixel_trim_attrs,
};

static const struct attribute_group *pixel_trim_attr_groups[] = {
        &pixel_trim_attr_group,
        NULL,
};

static void pixel_trim_kobj_release(struct kobject *obj)
{
        /* Never released the static objects */
}

static struct kobj_type pixel_trim_ktype = {
        .release = pixel_trim_kobj_release,
        .sysfs_ops = &kobj_sysfs_ops,
        .default_groups = pixel_trim_attr_groups,
};

int __init pixel_trim_sysfs_init(void)
{
	int err = kobject_init_and_add(&pixel_trim_kobj, &pixel_trim_ktype, vendor_mm_kobj, "pixel_trim");
	if (err)
		kobject_put(&pixel_trim_kobj);
	return err;
}
