/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "dal_vndr_hal_api.h"
#include "dal_vndr_hal_internal.h"
#include "dal_vndr_hal_be.h"
#include "dal_vndr_hal_defines_be.h"

/**
 * dal_vndr_hal_attach_common_ops() - Attach common HAL operations
 * @hal_soc: HAL SoC handle
 *
 * This function attaches common HAL operations that are shared across
 * all target types.
 *
 * Return: void
 */
static void dal_vndr_hal_attach_common_ops(struct dal_vndr_hal_soc *hal_soc)
{
	if (!hal_soc || !hal_soc->ops)
		return;

	/* Attach common ops that are target-independent */
	hal_soc->ops->dal_vndr_hal_tx_comp_get_desc_id =
				dal_vndr_hal_tx_comp_get_desc_id_generic;
	hal_soc->ops->dal_vndr_hal_tx_comp_get_buffer_source =
				dal_vndr_hal_tx_comp_get_buffer_source_generic;
	hal_soc->ops->dal_vndr_hal_tx_comp_get_tx_status =
				dal_vndr_hal_tx_comp_get_tx_status_generic;
}

/**
 * dal_vndr_hal_ops_attach() - Attach HAL operations
 * @hal_soc: HAL SoC handle
 *
 * This function attaches the default HAL operations.
 *
 * Return: void
 */
void dal_vndr_hal_ops_attach(void *hal_soc)
{
	struct dal_vndr_hal_soc *hal_soc_hdl =
		(struct dal_vndr_hal_soc *)hal_soc;

	if (!hal_soc_hdl)
		return;

	/* Attach common ops */
	dal_vndr_hal_attach_common_ops(hal_soc_hdl);
	/*
	 * Currently directly calling BE ops attach
	 * In future, this will be based on target type
	 * and call appropriate ops attach function
	 * based on target type
	 * For now, only BE is supported, so directly calling
	 * BE ops attach function
	 */
	dal_vndr_hal_default_ops_attach_be(hal_soc_hdl);
}
