/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_ELF_LOADER_H
#define _GOOGLE_DPA_ELF_LOADER_H

#include "google_dpa_internal.h"

int google_dpa_handle_resources(struct google_dpa *dpa, struct google_dpa_mcu *mcu);

void google_dpa_release_resources(struct google_dpa *dpa, struct google_dpa_mcu *mcu);

void google_dpa_release_resource_table(struct google_dpa_mcu *mcu);

int google_dpa_elf_sanity_check(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				const struct firmware *fw);

int google_dpa_elf_load_segments(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				 const struct firmware *fw);

int google_dpa_elf_load_rsc_table(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				  const struct firmware *fw);

#endif /* _GOOGLE_DPA_ELF_LOADER_H */
