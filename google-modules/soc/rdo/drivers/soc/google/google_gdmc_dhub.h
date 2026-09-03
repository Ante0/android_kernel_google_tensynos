/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 */

#ifndef _GOOGLE_GDMC_DHUB_H
#define _GOOGLE_GDMC_DHUB_H

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <kunit/visibility.h>
#include <soc/google/goog-mba-gdmc-iface.h>

struct uart_id_name {
	u8 id;
	const char *name;
};

struct dhub_uart_list {
	unsigned int id_names_length;
	const struct uart_id_name *id_names;
};

struct gdmc_dhub {
	struct gdmc_dhub_iface *dhub_iface;
	const struct dhub_uart_list *dhub_uart_list;
};

/*
 * The function prototypes and data declarations below are only made visible
 * when KUnit is enabled. This allows the KUnit test module to link against
 * the static functions and data in the driver for testing purposes.
 */
#if IS_ENABLED(CONFIG_KUNIT)
/* Functions made visible for testing */
void gdmc_dhub_init(struct gdmc_dhub *gdmc_dhub,
			 struct gdmc_dhub_iface *dhub_iface,
			 const struct dhub_uart_list *dhub_uart_list);
const char *mux_get(struct gdmc_dhub *gdmc_dhub);
int mux_set(struct gdmc_dhub *gdmc_dhub, const char *buf);
int baudrate_get(struct gdmc_dhub *gdmc_dhub, u32 uart_num, u32 *baudrate);
int baudrate_set(struct gdmc_dhub *gdmc_dhub, u32 uart_num, u32 baudrate);
int virt_en_get(struct gdmc_dhub *gdmc_dhub, u32 uart_num, bool *enable);
int virt_en_set(struct gdmc_dhub *gdmc_dhub, u32 uart_num, bool enable);

/* Data made visible for testing */
extern const struct dhub_uart_list dhub_uart_list_gen1;
extern const struct dhub_uart_list dhub_uart_list_gen2;

#endif /* IS_ENABLED(CONFIG_KUNIT) */

#endif /* _GOOGLE_GDMC_DHUB_H */
