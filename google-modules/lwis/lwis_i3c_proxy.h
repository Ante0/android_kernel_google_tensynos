/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I3C Interface with I2C
 *
 * Copyright (c) 2024 Google, LLC
 */

#ifndef LWIS_I3C_H_
#define LWIS_I3C_H_

#include "lwis_commands.h"
#include "lwis_device_i2c.h"

/*
 *  lwis_i3c_io_entry_rw: Read/Write from i3c bus via io_entry request.
 *  The readback values will be stored in the entry.
 */
int lwis_i3c_io_entry_rw(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entry);

int lwis_i3c_io_entries_rw(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entries,
			   int entries_cnt);

/*
 *  lwis_i3c_io_entry_mod: lwis_io_modify operation consists of
 *  an io_read followed by an io_write, where the io_write is dependent
 *  on the io_read result. Not compatible to asynchronous process.
 */
int lwis_i3c_io_entry_mod(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entry);

#endif /* LWIS_I3C_H_ */
