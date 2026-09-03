/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Synaptics TouchComm C library
 *
 * Copyright (C) 2017-2024 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

/**
 * @file synaptics_touchcom_func_reflash_tddi.h
 *
 * This file declares relevant functions and structures being used for TDDI products.
 */

#ifndef _SYNAPTICS_TOUCHCOM_REFLASH_TDDI_FUNCS_H_
#define _SYNAPTICS_TOUCHCOM_REFLASH_TDDI_FUNCS_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_func_reflash_image.h"
#include "synaptics_touchcom_func_reflash_ihex.h"

/*
 * Common Definitions
 */

enum update_tddi_area {
	UPDATE_TDDI_NONE = 0,
	UPDATE_TDDI_FIRMWARE_AND_CONFIG,
	UPDATE_TDDI_CONFIG,
};


/*
 * Standard API Definitions
 */



 /*
  * Standard API Definitions
  */

  /**
   * @brief   Request to read out data from the flash memory.
   *
   * @param
   *    [ in] tcm_dev:       the TouchComm device handle
   *    [ in] area:          flash area to read
   *    [out] rd_data:       buffer storing the retrieved data
   *    [ in] resp_reading:  method to read in the response
   *                          a positive value presents the us time delay for the processing
   *                          of each WORDs in the flash to read;
   *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
   * @return
   *    0 or positive value in case of success, a negative value otherwise.
   */
int syna_tcm_tddi_read_flash_area(struct tcm_dev *tcm_dev, enum flash_area area,
	struct tcm_buffer *rd_data, unsigned int resp_reading);

/**
 * @brief   The entry function to perform firmware update by the firmware image file.
 *
 * @param
 *    [ in] tcm_dev:                the TouchComm device handle
 *    [ in] image:                  binary data to write
 *    [ in] image_size:             size of data array
 *    [ in] flash_delay_settings:   set up the us delay time to wait for the completion of flash access
 *                                    for polling,     set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] force_reflash:          '1' to do reflash anyway
 *                                  '0' to compare ID info before doing reflash.
 *    [ in] is_multichip:           flag to indicate a multi-chip product used
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_do_fw_update(struct tcm_dev *tcm_dev, const unsigned char *image,
	unsigned int image_size, unsigned int flash_delay_settings, bool force_reflash,
	bool is_multichip);

/**
 * @brief   The entry function to perform firmware update by the firmware ihex file.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] ihex:            ihex data to write
 *    [ in] ihex_size:       size of ihex data
 *    [ in] delay_setting:   set up the us delay time to wait for the completion of flash access
 *                            for polling,     set a value formatted with [erase ms | write us];
 *                            for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] is_multichip:    flag to indicate a multi-chip product used
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_do_fw_update_ihex(struct tcm_dev *tcm_dev, const unsigned char *ihex,
	unsigned int ihex_size, unsigned int delay_setting, bool is_multichip);

/**
 * @brief   Query the TDDI ROM-Boot information.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [out] rom_boot_info: the romboot info packet returned
 *    [ in] resp_reading:  delay time for response reading.
 *                          a positive value presents the time for polling;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_get_romboot_info(struct tcm_dev *tcm_dev,
	struct tcm_romboot_info *rom_boot_info, unsigned int resp_reading);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* end of _SYNAPTICS_TOUCHCOM_REFLASH_TDDI_FUNCS_H_ */
