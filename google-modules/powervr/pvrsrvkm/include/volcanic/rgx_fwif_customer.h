/*************************************************************************/ /*!
@File
@Title          RGX firmware interface structures used by pvrsrvkm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware interface structures used by pvrsrvkm
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(RGX_FWIF_CUSTOMER_H)
#define RGX_FWIF_CUSTOMER_H

/*
 * Custom firmware data structures here
 */
#include "rgx_common.h"

/**
 * enum pixel_rgxfwif_cmd_type - pixel platform specific command types
 */
enum pixel_rgxfwif_cmd_type {
	/** PIXEL_RGXFWIF_PLATFORM_CMD_DVFS_SET_RATE: FW clock rate instruction */
	PIXEL_RGXFWIF_PLATFORM_CMD_DVFS_SET_RATE,
	/** PIXEL_RGXFWIF_PLATFORM_CMD_SET_MAX_DEFERRAL_LIMIT: max defer limit */
	PIXEL_RGXFWIF_PLATFORM_CMD_SET_MAX_DEFERRAL_LIMIT,
};

/**
 * struct pixel_rgxfwif_dvfs_set_rate_data - data for set rate command
 */
struct pixel_rgxfwif_dvfs_set_rate_data {
	/** opp: desired operating point clock rate in Hz */
	uint32_t opp;
} UNCACHED_ALIGN;

/**
 * struct pixel_rgxfwif_sched_max_deferral_limit - data for setting the max deferral limit in the firmware.
 */
struct pixel_rgxfwif_sched_max_deferral_limit {
	/** max_deferral: the maximum context deferral limit to allow */
	uint32_t max_deferral;
} UNCACHED_ALIGN;

#define PIXEL_RGXFWIF_IIF_HANDLE_ID_WIDTH (14)

/**
 * struct pixel_rgxfwif_iif_handle - inter-IP fence handle
 */
union pixel_rgxfwif_iif_handle {
	struct {
		/** _padding: reserved for tracking waiting fences in the firmware.
		 * Musn't be used for anything else.
		 */
		uint16_t _padding : 1;

		/** valid: whether the handle is valid or null, iif IDs can be zero */
		uint16_t valid : 1;

		/** id: the iif-id */
		uint16_t id : PIXEL_RGXFWIF_IIF_HANDLE_ID_WIDTH;
	};

	/** data: plain data */
	uint16_t data;
};


#endif
