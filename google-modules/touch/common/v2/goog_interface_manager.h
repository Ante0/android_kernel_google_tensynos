/* SPDX-License-Identifier: GPL */
/*
 * Google Interface Manager for Pixel Input.
 *
 * Copyright 2025 Google LLC.
 */

#ifndef _GIM_H_
#define _GIM_H_

#include <linux/device.h>
#include "goog_touch_interface.h"

#define GIM_NAME "goog_interface_manager"
#define MAX_GTI_DEVICES 3
#define MAX_SBI_DEVICES 0
#define MAX_INTERFACE_DEVICES (MAX_GTI_DEVICES + MAX_SBI_DEVICES)

#ifndef __CODESONAR__
#define ENUM_U32 : u32
#else
#define ENUM_U32
#endif

enum goog_interface_type ENUM_U32 {
	GOOG_INTERFACE_TYPE_TOUCH,
	GOOG_INTERFACE_TYPE_SB,
	GOOG_INTERFACE_TYPE_MAX,
};

#undef ENUM_U32

/**
 * struct goog_interface - Google Interface for Pixel.
 * @name: the name of this interface.
 * @dev_id: the dev_id for this interface from of property "goog,dev-id".
 * @type: the goog_interface_type for this device.
 * @dev: the device struct of this interface.
 * @vendor_dev_node: the of device node for this interface.
 * @context: the specific data pointer to store the interface context.
 */
struct goog_interface {
	char name[64];
	u32 dev_id;
	enum goog_interface_type type;
	struct device *dev;
	struct device_node *vendor_dev_node;
	union {
		void *context;
		struct goog_touch_interface *gti;
	};
};

/**
 * struct goog_interface_manager - Google Interface Manager for Pixel Input.
 * @dev: the device struct of this platform device(GIM).
 * @plat_dev: the platform device struct of this platform device.
 * @interfaces: the goog_interface struct array.
 * @interface_class: the class struct use by class_create().
 * @interface_proc_root: the proc_dir_entry struct use by proc_mkdir().
 */
struct goog_interface_manager {
	struct device *dev;
	struct platform_device *plat_dev;
	struct goog_interface interfaces[MAX_INTERFACE_DEVICES];
	struct class *interface_class[GOOG_INTERFACE_TYPE_MAX];
	struct proc_dir_entry *interface_proc_root[GOOG_INTERFACE_TYPE_MAX];
};

/*-----------------------------------------------------------------------------
 * "gim_" prefix forward declarations and functions.
 */
static inline const char *gim_of_node_full_name(struct device_node *dn)
{
	static char out[2][128];
	static u8 idx;

	idx ^= 1;
	memset(out[idx], 0, sizeof(out[idx]));
	if (dn) {
		scnprintf(out[idx], sizeof(out[idx]), "%s { %s }", of_node_full_name(dn->parent),
			  of_node_full_name(dn));
	} else {
		scnprintf(out[idx], sizeof(out[idx]), "<no-node> { <no-node> }");
	}

	return out[idx];
}

struct class *gim_get_interface_class(enum goog_interface_type type);
struct proc_dir_entry *gim_get_interface_proc_root(enum goog_interface_type type);

/*-----------------------------------------------------------------------------
 * "gim_interface_" prefix forward declarations and functions.
 */
static inline void *gim_interface_get_context(struct device *interface_dev)
{
	if (interface_dev)
		return dev_get_drvdata(interface_dev);

	return NULL;
}

/*-----------------------------------------------------------------------------
 * "gim_vendor_" prefix forward declarations and functions.
 */
struct goog_interface *gim_vendor_get_interface(struct device *vendor_dev);

static inline void *gim_vendor_get_interface_context(struct device *vendor_dev)
{
	struct goog_interface *interface = gim_vendor_get_interface(vendor_dev);

	if (!interface)
		return NULL;

	return interface->context;
}

static inline struct device *gim_vendor_get_interface_device(struct device *vendor_dev)
{
	struct goog_interface *interface = gim_vendor_get_interface(vendor_dev);

	if (!interface)
		return NULL;

	return interface->dev;
}

static inline u32 gim_vendor_get_interface_dev_id(struct device *vendor_dev)
{
	struct goog_interface *interface = gim_vendor_get_interface(vendor_dev);

	if (!interface)
		return -ENODEV;

	return interface->dev_id;
}
#endif // _GIM_H_
