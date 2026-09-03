/* SPDX-License-Identifier: GPL */
#ifndef _GTI_INTERNAL_H_
#define _GTI_INTERNAL_H_

#include "gim_drm.h"
#include "goog_touch_interface.h"
#include "gti_debug.h"
#include "gti_fs.h"

/**
 * Conditionally set the function pointer with default nop function in
 * "gti->options". And, combine with GTI_OPT_FUNC_WITH_TYPE to init all
 * options in struct gti_optional_configuration.
 */
#define GTI_COND_SET_WITH_DEFAULT(name, type) { \
	gti->options.name = (options && options->name) ? \
		options->name : goog_##name##_nop;  \
}

/**
 * Creates the simple no-operation function. And, combine with
 * GTI_OPT_FUNC_WITH_TYPE to declar all nop functions in
 * struct gti_optional_configuration.
 */
#define GTI_DECLARE_NOP_FUNC(name, type) \
static int goog_##name##_nop(void *private_data, type cmd) \
{ \
	return -ESRCH; \
}

struct device *goog_touch_interface_device_create(char *name, struct goog_touch_interface *gti);
void goog_touch_interface_device_destroy(struct goog_touch_interface *gti);

void gti_lookup_touch_report_rate(struct goog_touch_interface *gti);
void gti_offload_set_running(struct goog_touch_interface *gti, bool running);

#endif	// _GTI_INTERNAL_H_

