/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2022 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

#ifndef __scregSC_h__
#define __scregSC_h__

// clang-format off
// common_typos_disable

/*******************************************************************************
**                               ~~~~~~~~~~~~~~                               **
**                               Module G2dHost                               **
**                               ~~~~~~~~~~~~~~                               **
*******************************************************************************/

/* Register scregHiReserved0 (8 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Hi Reserved Register.  */

#define scregHiReserved0RegAddrs                                         0x54000
#define SCREG_HI_RESERVED0_Address                                      0x150000
#define SCREG_HI_RESERVED0_MSB                                                19
#define SCREG_HI_RESERVED0_LSB                                                 3
#define SCREG_HI_RESERVED0_BLK                                                 3
#define SCREG_HI_RESERVED0_Count                                               8
#define SCREG_HI_RESERVED0_FieldMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED0_ReadMask                                   0xFFFFFFFF
#define SCREG_HI_RESERVED0_WriteMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED0_ResetValue                                 0x00000000

/* Reserved. */
#define SCREG_HI_RESERVED0_VALUE                                            31:0
#define SCREG_HI_RESERVED0_VALUE_End                                          31
#define SCREG_HI_RESERVED0_VALUE_Start                                         0
#define SCREG_HI_RESERVED0_VALUE_Type                                        U32

/* Register scregChipId **
** ~~~~~~~~~~~~~~~~~~~~ */

/* Chip Identification Register.  Shows the ID for the chip in BCD.  This     **
** register has no set reset value.  It varies with the implementation. READ  **
** ONLY.                                                                      */

#define scregChipIdRegAddrs                                              0x54008
#define SCREG_CHIP_ID_Address                                           0x150020
#define SCREG_CHIP_ID_MSB                                                     19
#define SCREG_CHIP_ID_LSB                                                      0
#define SCREG_CHIP_ID_BLK                                                      0
#define SCREG_CHIP_ID_Count                                                    1
#define SCREG_CHIP_ID_FieldMask                                       0xFFFFFFFF
#define SCREG_CHIP_ID_ReadMask                                        0xFFFFFFFF
#define SCREG_CHIP_ID_WriteMask                                       0x00000000
#define SCREG_CHIP_ID_ResetValue                                      0x00000000

/* Id. */
#define SCREG_CHIP_ID_ID                                                    31:0
#define SCREG_CHIP_ID_ID_End                                                  31
#define SCREG_CHIP_ID_ID_Start                                                 0
#define SCREG_CHIP_ID_ID_Type                                                U32

/* Register scregChipRev **
** ~~~~~~~~~~~~~~~~~~~~~ */

/* Chip Revision Register.  Shows the revision for the chip in BCD.  This     **
** register has no set reset value.  It varies with the implementation. READ  **
** ONLY.                                                                      */

#define scregChipRevRegAddrs                                             0x54009
#define SCREG_CHIP_REV_Address                                          0x150024
#define SCREG_CHIP_REV_MSB                                                    19
#define SCREG_CHIP_REV_LSB                                                     0
#define SCREG_CHIP_REV_BLK                                                     0
#define SCREG_CHIP_REV_Count                                                   1
#define SCREG_CHIP_REV_FieldMask                                      0xFFFFFFFF
#define SCREG_CHIP_REV_ReadMask                                       0xFFFFFFFF
#define SCREG_CHIP_REV_WriteMask                                      0x00000000
#define SCREG_CHIP_REV_ResetValue                                     0x00000000

/* Revision. This value may vary between releases and can be ignored during   **
** test.                                                                      */
#define SCREG_CHIP_REV_REV                                                  31:0
#define SCREG_CHIP_REV_REV_End                                                31
#define SCREG_CHIP_REV_REV_Start                                               0
#define SCREG_CHIP_REV_REV_Type                                              U32

/* Register scregChipDate **
** ~~~~~~~~~~~~~~~~~~~~~~ */

/* Chip Date Register.  Shows the release date for the IP in YYYYMMDD         **
** (year/month/day) format.  This register has no set reset value.  It varies **
** with the implementation. READ ONLY                                         */

#define scregChipDateRegAddrs                                            0x5400A
#define SCREG_CHIP_DATE_Address                                         0x150028
#define SCREG_CHIP_DATE_MSB                                                   19
#define SCREG_CHIP_DATE_LSB                                                    0
#define SCREG_CHIP_DATE_BLK                                                    0
#define SCREG_CHIP_DATE_Count                                                  1
#define SCREG_CHIP_DATE_FieldMask                                     0xFFFFFFFF
#define SCREG_CHIP_DATE_ReadMask                                      0xFFFFFFFF
#define SCREG_CHIP_DATE_WriteMask                                     0x00000000
#define SCREG_CHIP_DATE_ResetValue                                    0x00000000

/* Date. This value will vary each release and can be ignored during test. */
#define SCREG_CHIP_DATE_DATE                                                31:0
#define SCREG_CHIP_DATE_DATE_End                                              31
#define SCREG_CHIP_DATE_DATE_Start                                             0
#define SCREG_CHIP_DATE_DATE_Type                                            U32

/* Register scregChipTime **
** ~~~~~~~~~~~~~~~~~~~~~~ */

/* Chip Time Register.  Shows the release time for the IP in HHMMSS00         **
** (hour/minute/second/00) format.  This register has no set reset value.  It **
** varies with the implementation. READ ONLY.                                 */

#define scregChipTimeRegAddrs                                            0x5400B
#define SCREG_CHIP_TIME_Address                                         0x15002C
#define SCREG_CHIP_TIME_MSB                                                   19
#define SCREG_CHIP_TIME_LSB                                                    0
#define SCREG_CHIP_TIME_BLK                                                    0
#define SCREG_CHIP_TIME_Count                                                  1
#define SCREG_CHIP_TIME_FieldMask                                     0xFFFFFFFF
#define SCREG_CHIP_TIME_ReadMask                                      0xFFFFFFFF
#define SCREG_CHIP_TIME_WriteMask                                     0x00000000
#define SCREG_CHIP_TIME_ResetValue                                    0x00000000

/* Time */
#define SCREG_CHIP_TIME_TIME                                                31:0
#define SCREG_CHIP_TIME_TIME_End                                              31
#define SCREG_CHIP_TIME_TIME_Start                                             0
#define SCREG_CHIP_TIME_TIME_Type                                            U32

/* Register scregChipCustomer **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Chip Customer Register.  Shows the customer and group for the IP. This     **
** register has no set reset value. It varies with the implementation.        */

#define scregChipCustomerRegAddrs                                        0x5400C
#define SCREG_CHIP_CUSTOMER_Address                                     0x150030
#define SCREG_CHIP_CUSTOMER_MSB                                               19
#define SCREG_CHIP_CUSTOMER_LSB                                                0
#define SCREG_CHIP_CUSTOMER_BLK                                                0
#define SCREG_CHIP_CUSTOMER_Count                                              1
#define SCREG_CHIP_CUSTOMER_FieldMask                                 0xFFFFFFFF
#define SCREG_CHIP_CUSTOMER_ReadMask                                  0xFFFFFFFF
#define SCREG_CHIP_CUSTOMER_WriteMask                                 0x00000000
#define SCREG_CHIP_CUSTOMER_ResetValue                                0x00000000

/* Company. */
#define SCREG_CHIP_CUSTOMER_COMPANY                                        31:16
#define SCREG_CHIP_CUSTOMER_COMPANY_End                                       31
#define SCREG_CHIP_CUSTOMER_COMPANY_Start                                     16
#define SCREG_CHIP_CUSTOMER_COMPANY_Type                                     U16

/* Group. */
#define SCREG_CHIP_CUSTOMER_GROUP                                           15:0
#define SCREG_CHIP_CUSTOMER_GROUP_End                                         15
#define SCREG_CHIP_CUSTOMER_GROUP_Start                                        0
#define SCREG_CHIP_CUSTOMER_GROUP_Type                                       U16

/* Register scregHiReserved1 (25 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Hi Reserved Register.  */

#define scregHiReserved1RegAddrs                                         0x5400D
#define SCREG_HI_RESERVED1_Address                                      0x150034
#define SCREG_HI_RESERVED1_MSB                                                19
#define SCREG_HI_RESERVED1_LSB                                                 5
#define SCREG_HI_RESERVED1_BLK                                                 5
#define SCREG_HI_RESERVED1_Count                                              25
#define SCREG_HI_RESERVED1_FieldMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED1_ReadMask                                   0xFFFFFFFF
#define SCREG_HI_RESERVED1_WriteMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED1_ResetValue                                 0x00000000

/* Reserved. */
#define SCREG_HI_RESERVED1_VALUE                                            31:0
#define SCREG_HI_RESERVED1_VALUE_End                                          31
#define SCREG_HI_RESERVED1_VALUE_Start                                         0
#define SCREG_HI_RESERVED1_VALUE_Type                                        U32

/* Register scregChipPatchRev **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Chip Patch Revision Register.  Shows the revision for the chip in BCD.     **
** This register has no set reset value.  It varies with the implementation.  **
** READ ONLY.                                                                 */

#define scregChipPatchRevRegAddrs                                        0x54026
#define SCREG_CHIP_PATCH_REV_Address                                    0x150098
#define SCREG_CHIP_PATCH_REV_MSB                                              19
#define SCREG_CHIP_PATCH_REV_LSB                                               0
#define SCREG_CHIP_PATCH_REV_BLK                                               0
#define SCREG_CHIP_PATCH_REV_Count                                             1
#define SCREG_CHIP_PATCH_REV_FieldMask                                0x000000FF
#define SCREG_CHIP_PATCH_REV_ReadMask                                 0x000000FF
#define SCREG_CHIP_PATCH_REV_WriteMask                                0x00000000
#define SCREG_CHIP_PATCH_REV_ResetValue                               0x00000000

/* Patch Revision. This value may vary between releases and can be ignored    **
** during test.                                                               */
#define SCREG_CHIP_PATCH_REV_REV                                             7:0
#define SCREG_CHIP_PATCH_REV_REV_End                                           7
#define SCREG_CHIP_PATCH_REV_REV_Start                                         0
#define SCREG_CHIP_PATCH_REV_REV_Type                                        U08

/* Register scregHiReserved2 (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Hi Reserved Register.  */

#define scregHiReserved2RegAddrs                                         0x54027
#define SCREG_HI_RESERVED2_Address                                      0x15009C
#define SCREG_HI_RESERVED2_MSB                                                19
#define SCREG_HI_RESERVED2_LSB                                                 2
#define SCREG_HI_RESERVED2_BLK                                                 2
#define SCREG_HI_RESERVED2_Count                                               3
#define SCREG_HI_RESERVED2_FieldMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED2_ReadMask                                   0xFFFFFFFF
#define SCREG_HI_RESERVED2_WriteMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED2_ResetValue                                 0x00000000

/* Reserved. */
#define SCREG_HI_RESERVED2_VALUE                                            31:0
#define SCREG_HI_RESERVED2_VALUE_End                                          31
#define SCREG_HI_RESERVED2_VALUE_Start                                         0
#define SCREG_HI_RESERVED2_VALUE_Type                                        U32

/* Register scregProductId **
** ~~~~~~~~~~~~~~~~~~~~~~~ */

/* Product Identification Register.  Shows Product ID. It varies with the     **
** product configuration. READ ONLY.                                          */

#define scregProductIdRegAddrs                                           0x5402A
#define SCREG_PRODUCT_ID_Address                                        0x1500A8
#define SCREG_PRODUCT_ID_MSB                                                  19
#define SCREG_PRODUCT_ID_LSB                                                   0
#define SCREG_PRODUCT_ID_BLK                                                   0
#define SCREG_PRODUCT_ID_Count                                                 1
#define SCREG_PRODUCT_ID_FieldMask                                    0x0FFFFFFF
#define SCREG_PRODUCT_ID_ReadMask                                     0x0FFFFFFF
#define SCREG_PRODUCT_ID_WriteMask                                    0x00000000
#define SCREG_PRODUCT_ID_ResetValue                                   0x03090000

/* Product Grade Level, lower 4 bits of 8 bit value. Upper bits are 31:28.    **
** 0:None-no extra letter on the product name;  1:N-Nano;  2:L-Lite;          **
** 3:UL-Ultra Lite;                                                           */
#define SCREG_PRODUCT_ID_GRADE_LEVEL                                         3:0
#define SCREG_PRODUCT_ID_GRADE_LEVEL_End                                       3
#define SCREG_PRODUCT_ID_GRADE_LEVEL_Start                                     0
#define SCREG_PRODUCT_ID_GRADE_LEVEL_Type                                    U04

/* Product Number.  */
#define SCREG_PRODUCT_ID_NUM                                                23:4
#define SCREG_PRODUCT_ID_NUM_End                                              23
#define SCREG_PRODUCT_ID_NUM_Start                                             4
#define SCREG_PRODUCT_ID_NUM_Type                                            U20

/* 0:GC (2D or 3D Graphic Cores);  1:DEC (Decode/Encode engine);  2:DC        **
** (Display Controller);                                                      */
#define SCREG_PRODUCT_ID_TYPE                                              27:24
#define SCREG_PRODUCT_ID_TYPE_End                                             27
#define SCREG_PRODUCT_ID_TYPE_Start                                           24
#define SCREG_PRODUCT_ID_TYPE_Type                                           U04

/* Register scregHiReserved3 (15 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Hi Reserved Register.  */

#define scregHiReserved3RegAddrs                                         0x5402B
#define SCREG_HI_RESERVED3_Address                                      0x1500AC
#define SCREG_HI_RESERVED3_MSB                                                19
#define SCREG_HI_RESERVED3_LSB                                                 4
#define SCREG_HI_RESERVED3_BLK                                                 4
#define SCREG_HI_RESERVED3_Count                                              15
#define SCREG_HI_RESERVED3_FieldMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED3_ReadMask                                   0xFFFFFFFF
#define SCREG_HI_RESERVED3_WriteMask                                  0xFFFFFFFF
#define SCREG_HI_RESERVED3_ResetValue                                 0x00000000

/* Reserved. */
#define SCREG_HI_RESERVED3_VALUE                                            31:0
#define SCREG_HI_RESERVED3_VALUE_End                                          31
#define SCREG_HI_RESERVED3_VALUE_Start                                         0
#define SCREG_HI_RESERVED3_VALUE_Type                                        U32

/* Register scregEcoId **
** ~~~~~~~~~~~~~~~~~~~ */

/* Product Identification Register.  Shows ECO ID. It varies with the product **
** configuration. READ ONLY.                                                  */

#define scregEcoIdRegAddrs                                               0x5403A
#define SCREG_ECO_ID_Address                                            0x1500E8
#define SCREG_ECO_ID_MSB                                                      19
#define SCREG_ECO_ID_LSB                                                       0
#define SCREG_ECO_ID_BLK                                                       0
#define SCREG_ECO_ID_Count                                                     1
#define SCREG_ECO_ID_FieldMask                                        0xFFFFFFFF
#define SCREG_ECO_ID_ReadMask                                         0xFFFFFFFF
#define SCREG_ECO_ID_WriteMask                                        0x00000000
#define SCREG_ECO_ID_ResetValue                                       0x00000000

/* ECO ID.  */
#define SCREG_ECO_ID_ID                                                     31:0
#define SCREG_ECO_ID_ID_End                                                   31
#define SCREG_ECO_ID_ID_Start                                                  0
#define SCREG_ECO_ID_ID_Type                                                 U32

/* Register scregG2dIntrEnable **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Intrerrupt enable.  */

#define scregG2dIntrEnableRegAddrs                                       0x5403B
#define SCREG_G2D_INTR_ENABLE_Address                                   0x1500EC
#define SCREG_G2D_INTR_ENABLE_MSB                                             19
#define SCREG_G2D_INTR_ENABLE_LSB                                              0
#define SCREG_G2D_INTR_ENABLE_BLK                                              0
#define SCREG_G2D_INTR_ENABLE_Count                                            1
#define SCREG_G2D_INTR_ENABLE_FieldMask                               0x00001FFF
#define SCREG_G2D_INTR_ENABLE_ReadMask                                0x00001FFF
#define SCREG_G2D_INTR_ENABLE_WriteMask                               0x00001FFF
#define SCREG_G2D_INTR_ENABLE_ResetValue                              0x00000000

/* g2d sw reset done.  */
#define SCREG_G2D_INTR_ENABLE_SW_RST_DONE                                    0:0
#define SCREG_G2D_INTR_ENABLE_SW_RST_DONE_End                                  0
#define SCREG_G2D_INTR_ENABLE_SW_RST_DONE_Start                                0
#define SCREG_G2D_INTR_ENABLE_SW_RST_DONE_Type                               U01

/* g2d pipe0 frame start.  */
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_START                              1:1
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_START_End                            1
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_START_Start                          1
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_START_Type                         U01

/* g2d pipe1 frame start.  */
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_START                              2:2
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_START_End                            2
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_START_Start                          2
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_START_Type                         U01

/* g2d pipe0 frame done.  */
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_DONE                               3:3
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_DONE_End                             3
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_DONE_Start                           3
#define SCREG_G2D_INTR_ENABLE_PIPE0_FRAME_DONE_Type                          U01

/* g2d pipe1 framedone.  */
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_DONE                               4:4
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_DONE_End                             4
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_DONE_Start                           4
#define SCREG_G2D_INTR_ENABLE_PIPE1_FRAME_DONE_Type                          U01

/* g2d apb hang.  */
#define SCREG_G2D_INTR_ENABLE_APB_HANG                                       5:5
#define SCREG_G2D_INTR_ENABLE_APB_HANG_End                                     5
#define SCREG_G2D_INTR_ENABLE_APB_HANG_Start                                   5
#define SCREG_G2D_INTR_ENABLE_APB_HANG_Type                                  U01

/* g2d axi read bus hang.  */
#define SCREG_G2D_INTR_ENABLE_AXI_RD_BUS_HANG                                6:6
#define SCREG_G2D_INTR_ENABLE_AXI_RD_BUS_HANG_End                              6
#define SCREG_G2D_INTR_ENABLE_AXI_RD_BUS_HANG_Start                            6
#define SCREG_G2D_INTR_ENABLE_AXI_RD_BUS_HANG_Type                           U01

/* g2d axi write bus hang.  */
#define SCREG_G2D_INTR_ENABLE_AXI_WR_BUS_HANG                                7:7
#define SCREG_G2D_INTR_ENABLE_AXI_WR_BUS_HANG_End                              7
#define SCREG_G2D_INTR_ENABLE_AXI_WR_BUS_HANG_Start                            7
#define SCREG_G2D_INTR_ENABLE_AXI_WR_BUS_HANG_Type                           U01

/* g2d axi bus error.  */
#define SCREG_G2D_INTR_ENABLE_AXI_BUS_ERROR                                  8:8
#define SCREG_G2D_INTR_ENABLE_AXI_BUS_ERROR_End                                8
#define SCREG_G2D_INTR_ENABLE_AXI_BUS_ERROR_Start                              8
#define SCREG_G2D_INTR_ENABLE_AXI_BUS_ERROR_Type                             U01

/* g2d pipe0 pvric decode error.  */
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_DECODE_ERROR                       9:9
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_DECODE_ERROR_End                     9
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_DECODE_ERROR_Start                   9
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_DECODE_ERROR_Type                  U01

/* g2d pipe0 pvric encode error.  */
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_ENCODE_ERROR                     10:10
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_ENCODE_ERROR_End                    10
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_ENCODE_ERROR_Start                  10
#define SCREG_G2D_INTR_ENABLE_PIPE0_PVRIC_ENCODE_ERROR_Type                  U01

/* g2d pipe1 pvric decode error.  */
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_DECODE_ERROR                     11:11
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_DECODE_ERROR_End                    11
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_DECODE_ERROR_Start                  11
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_DECODE_ERROR_Type                  U01

/* g2d pipe1 pvric encode error.  */
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_ENCODE_ERROR                     12:12
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_ENCODE_ERROR_End                    12
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_ENCODE_ERROR_Start                  12
#define SCREG_G2D_INTR_ENABLE_PIPE1_PVRIC_ENCODE_ERROR_Type                  U01

/* Register scregG2dIntrStatus **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Intrerrupt status.  */

#define scregG2dIntrStatusRegAddrs                                       0x5403C
#define SCREG_G2D_INTR_STATUS_Address                                   0x1500F0
#define SCREG_G2D_INTR_STATUS_MSB                                             19
#define SCREG_G2D_INTR_STATUS_LSB                                              0
#define SCREG_G2D_INTR_STATUS_BLK                                              0
#define SCREG_G2D_INTR_STATUS_Count                                            1
#define SCREG_G2D_INTR_STATUS_FieldMask                               0x00001FFF
#define SCREG_G2D_INTR_STATUS_ReadMask                                0x00001FFF
#define SCREG_G2D_INTR_STATUS_WriteMask                               0x00001FFF
#define SCREG_G2D_INTR_STATUS_ResetValue                              0x00000000

/* g2d sw reset done.  */
#define SCREG_G2D_INTR_STATUS_SW_RST_DONE                                    0:0
#define SCREG_G2D_INTR_STATUS_SW_RST_DONE_End                                  0
#define SCREG_G2D_INTR_STATUS_SW_RST_DONE_Start                                0
#define SCREG_G2D_INTR_STATUS_SW_RST_DONE_Type                               U01

/* g2d pipe0 frame start.  */
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_START                              1:1
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_START_End                            1
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_START_Start                          1
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_START_Type                         U01

/* g2d pipe1 frame start.  */
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_START                              2:2
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_START_End                            2
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_START_Start                          2
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_START_Type                         U01

/* g2d pipe0 frame done.  */
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_DONE                               3:3
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_DONE_End                             3
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_DONE_Start                           3
#define SCREG_G2D_INTR_STATUS_PIPE0_FRAME_DONE_Type                          U01

/* g2d pipe1 framedone.  */
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_DONE                               4:4
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_DONE_End                             4
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_DONE_Start                           4
#define SCREG_G2D_INTR_STATUS_PIPE1_FRAME_DONE_Type                          U01

/* g2d apb hang.  */
#define SCREG_G2D_INTR_STATUS_APB_HANG                                       5:5
#define SCREG_G2D_INTR_STATUS_APB_HANG_End                                     5
#define SCREG_G2D_INTR_STATUS_APB_HANG_Start                                   5
#define SCREG_G2D_INTR_STATUS_APB_HANG_Type                                  U01

/* g2d axi read bus hang.  */
#define SCREG_G2D_INTR_STATUS_AXI_RD_BUS_HANG                                6:6
#define SCREG_G2D_INTR_STATUS_AXI_RD_BUS_HANG_End                              6
#define SCREG_G2D_INTR_STATUS_AXI_RD_BUS_HANG_Start                            6
#define SCREG_G2D_INTR_STATUS_AXI_RD_BUS_HANG_Type                           U01

/* g2d axi write bus hang.  */
#define SCREG_G2D_INTR_STATUS_AXI_WR_BUS_HANG                                7:7
#define SCREG_G2D_INTR_STATUS_AXI_WR_BUS_HANG_End                              7
#define SCREG_G2D_INTR_STATUS_AXI_WR_BUS_HANG_Start                            7
#define SCREG_G2D_INTR_STATUS_AXI_WR_BUS_HANG_Type                           U01

/* g2d axi bus error.  */
#define SCREG_G2D_INTR_STATUS_AXI_BUS_ERROR                                  8:8
#define SCREG_G2D_INTR_STATUS_AXI_BUS_ERROR_End                                8
#define SCREG_G2D_INTR_STATUS_AXI_BUS_ERROR_Start                              8
#define SCREG_G2D_INTR_STATUS_AXI_BUS_ERROR_Type                             U01

/* g2d pipe0 pvric decode error.  */
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_DECODE_ERROR                       9:9
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_DECODE_ERROR_End                     9
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_DECODE_ERROR_Start                   9
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_DECODE_ERROR_Type                  U01

/* g2d pipe0 pvric encode error.  */
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_ENCODE_ERROR                     10:10
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_ENCODE_ERROR_End                    10
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_ENCODE_ERROR_Start                  10
#define SCREG_G2D_INTR_STATUS_PIPE0_PVRIC_ENCODE_ERROR_Type                  U01

/* g2d pipe1 pvric decode error.  */
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_DECODE_ERROR                     11:11
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_DECODE_ERROR_End                    11
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_DECODE_ERROR_Start                  11
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_DECODE_ERROR_Type                  U01

/* g2d pipe1 pvric encode error.  */
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_ENCODE_ERROR                     12:12
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_ENCODE_ERROR_End                    12
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_ENCODE_ERROR_Start                  12
#define SCREG_G2D_INTR_STATUS_PIPE1_PVRIC_ENCODE_ERROR_Type                  U01

/* Register scregG2dIntrTest **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Intrerrupt test.  */

#define scregG2dIntrTestRegAddrs                                         0x5403D
#define SCREG_G2D_INTR_TEST_Address                                     0x1500F4
#define SCREG_G2D_INTR_TEST_MSB                                               19
#define SCREG_G2D_INTR_TEST_LSB                                                0
#define SCREG_G2D_INTR_TEST_BLK                                                0
#define SCREG_G2D_INTR_TEST_Count                                              1
#define SCREG_G2D_INTR_TEST_FieldMask                                 0x00001FFF
#define SCREG_G2D_INTR_TEST_ReadMask                                  0x00001FFF
#define SCREG_G2D_INTR_TEST_WriteMask                                 0x00001FFF
#define SCREG_G2D_INTR_TEST_ResetValue                                0x00000000

/* g2d sw reset done.  */
#define SCREG_G2D_INTR_TEST_SW_RST_DONE                                      0:0
#define SCREG_G2D_INTR_TEST_SW_RST_DONE_End                                    0
#define SCREG_G2D_INTR_TEST_SW_RST_DONE_Start                                  0
#define SCREG_G2D_INTR_TEST_SW_RST_DONE_Type                                 U01

/* g2d pipe0 frame start.  */
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_START                                1:1
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_START_End                              1
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_START_Start                            1
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_START_Type                           U01

/* g2d pipe1 frame start.  */
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_START                                2:2
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_START_End                              2
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_START_Start                            2
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_START_Type                           U01

/* g2d pipe0 frame done.  */
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_DONE                                 3:3
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_DONE_End                               3
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_DONE_Start                             3
#define SCREG_G2D_INTR_TEST_PIPE0_FRAME_DONE_Type                            U01

/* g2d pipe1 framedone.  */
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_DONE                                 4:4
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_DONE_End                               4
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_DONE_Start                             4
#define SCREG_G2D_INTR_TEST_PIPE1_FRAME_DONE_Type                            U01

/* g2d apb hang.  */
#define SCREG_G2D_INTR_TEST_APB_HANG                                         5:5
#define SCREG_G2D_INTR_TEST_APB_HANG_End                                       5
#define SCREG_G2D_INTR_TEST_APB_HANG_Start                                     5
#define SCREG_G2D_INTR_TEST_APB_HANG_Type                                    U01

/* g2d axi read bus hang.  */
#define SCREG_G2D_INTR_TEST_AXI_RD_BUS_HANG                                  6:6
#define SCREG_G2D_INTR_TEST_AXI_RD_BUS_HANG_End                                6
#define SCREG_G2D_INTR_TEST_AXI_RD_BUS_HANG_Start                              6
#define SCREG_G2D_INTR_TEST_AXI_RD_BUS_HANG_Type                             U01

/* g2d axi write bus hang.  */
#define SCREG_G2D_INTR_TEST_AXI_WR_BUS_HANG                                  7:7
#define SCREG_G2D_INTR_TEST_AXI_WR_BUS_HANG_End                                7
#define SCREG_G2D_INTR_TEST_AXI_WR_BUS_HANG_Start                              7
#define SCREG_G2D_INTR_TEST_AXI_WR_BUS_HANG_Type                             U01

/* g2d axi bus error.  */
#define SCREG_G2D_INTR_TEST_AXI_BUS_ERROR                                    8:8
#define SCREG_G2D_INTR_TEST_AXI_BUS_ERROR_End                                  8
#define SCREG_G2D_INTR_TEST_AXI_BUS_ERROR_Start                                8
#define SCREG_G2D_INTR_TEST_AXI_BUS_ERROR_Type                               U01

/* g2d pipe0 pvric decode error.  */
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_DECODE_ERROR                         9:9
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_DECODE_ERROR_End                       9
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_DECODE_ERROR_Start                     9
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_DECODE_ERROR_Type                    U01

/* g2d pipe0 pvric encode error.  */
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_ENCODE_ERROR                       10:10
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_ENCODE_ERROR_End                      10
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_ENCODE_ERROR_Start                    10
#define SCREG_G2D_INTR_TEST_PIPE0_PVRIC_ENCODE_ERROR_Type                    U01

/* g2d pipe1 pvric decode error.  */
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_DECODE_ERROR                       11:11
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_DECODE_ERROR_End                      11
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_DECODE_ERROR_Start                    11
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_DECODE_ERROR_Type                    U01

/* g2d pipe1 pvric encode error.  */
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_ENCODE_ERROR                       12:12
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_ENCODE_ERROR_End                      12
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_ENCODE_ERROR_Start                    12
#define SCREG_G2D_INTR_TEST_PIPE1_PVRIC_ENCODE_ERROR_Type                    U01

/* Register scregG2dIntrMask **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Intrerrupt mask.  */

#define scregG2dIntrMaskRegAddrs                                         0x5403E
#define SCREG_G2D_INTR_MASK_Address                                     0x1500F8
#define SCREG_G2D_INTR_MASK_MSB                                               19
#define SCREG_G2D_INTR_MASK_LSB                                                0
#define SCREG_G2D_INTR_MASK_BLK                                                0
#define SCREG_G2D_INTR_MASK_Count                                              1
#define SCREG_G2D_INTR_MASK_FieldMask                                 0x00001FFF
#define SCREG_G2D_INTR_MASK_ReadMask                                  0x00001FFF
#define SCREG_G2D_INTR_MASK_WriteMask                                 0x00001FFF
#define SCREG_G2D_INTR_MASK_ResetValue                                0x00000000

/* g2d sw reset done.  */
#define SCREG_G2D_INTR_MASK_SW_RST_DONE                                      0:0
#define SCREG_G2D_INTR_MASK_SW_RST_DONE_End                                    0
#define SCREG_G2D_INTR_MASK_SW_RST_DONE_Start                                  0
#define SCREG_G2D_INTR_MASK_SW_RST_DONE_Type                                 U01

/* g2d pipe0 frame start.  */
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_START                                1:1
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_START_End                              1
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_START_Start                            1
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_START_Type                           U01

/* g2d pipe1 frame start.  */
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_START                                2:2
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_START_End                              2
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_START_Start                            2
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_START_Type                           U01

/* g2d pipe0 frame done.  */
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_DONE                                 3:3
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_DONE_End                               3
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_DONE_Start                             3
#define SCREG_G2D_INTR_MASK_PIPE0_FRAME_DONE_Type                            U01

/* g2d pipe1 framedone.  */
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_DONE                                 4:4
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_DONE_End                               4
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_DONE_Start                             4
#define SCREG_G2D_INTR_MASK_PIPE1_FRAME_DONE_Type                            U01

/* g2d apb hang.  */
#define SCREG_G2D_INTR_MASK_APB_HANG                                         5:5
#define SCREG_G2D_INTR_MASK_APB_HANG_End                                       5
#define SCREG_G2D_INTR_MASK_APB_HANG_Start                                     5
#define SCREG_G2D_INTR_MASK_APB_HANG_Type                                    U01

/* g2d axi read bus hang.  */
#define SCREG_G2D_INTR_MASK_AXI_RD_BUS_HANG                                  6:6
#define SCREG_G2D_INTR_MASK_AXI_RD_BUS_HANG_End                                6
#define SCREG_G2D_INTR_MASK_AXI_RD_BUS_HANG_Start                              6
#define SCREG_G2D_INTR_MASK_AXI_RD_BUS_HANG_Type                             U01

/* g2d axi write bus hang.  */
#define SCREG_G2D_INTR_MASK_AXI_WR_BUS_HANG                                  7:7
#define SCREG_G2D_INTR_MASK_AXI_WR_BUS_HANG_End                                7
#define SCREG_G2D_INTR_MASK_AXI_WR_BUS_HANG_Start                              7
#define SCREG_G2D_INTR_MASK_AXI_WR_BUS_HANG_Type                             U01

/* g2d axi bus error.  */
#define SCREG_G2D_INTR_MASK_AXI_BUS_ERROR                                    8:8
#define SCREG_G2D_INTR_MASK_AXI_BUS_ERROR_End                                  8
#define SCREG_G2D_INTR_MASK_AXI_BUS_ERROR_Start                                8
#define SCREG_G2D_INTR_MASK_AXI_BUS_ERROR_Type                               U01

/* g2d pipe0 pvric decode error.  */
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_DECODE_ERROR                         9:9
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_DECODE_ERROR_End                       9
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_DECODE_ERROR_Start                     9
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_DECODE_ERROR_Type                    U01

/* g2d pipe0 pvric encode error.  */
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_ENCODE_ERROR                       10:10
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_ENCODE_ERROR_End                      10
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_ENCODE_ERROR_Start                    10
#define SCREG_G2D_INTR_MASK_PIPE0_PVRIC_ENCODE_ERROR_Type                    U01

/* g2d pipe1 pvric decode error.  */
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_DECODE_ERROR                       11:11
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_DECODE_ERROR_End                      11
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_DECODE_ERROR_Start                    11
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_DECODE_ERROR_Type                    U01

/* g2d pipe1 pvric encode error.  */
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_ENCODE_ERROR                       12:12
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_ENCODE_ERROR_End                      12
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_ENCODE_ERROR_Start                    12
#define SCREG_G2D_INTR_MASK_PIPE1_PVRIC_ENCODE_ERROR_Type                    U01

/* Register scregG2dIntrOverflow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* G2d Intrerrupt overflow.  */

#define scregG2dIntrOverflowRegAddrs                                     0x5403F
#define SCREG_G2D_INTR_OVERFLOW_Address                                 0x1500FC
#define SCREG_G2D_INTR_OVERFLOW_MSB                                           19
#define SCREG_G2D_INTR_OVERFLOW_LSB                                            0
#define SCREG_G2D_INTR_OVERFLOW_BLK                                            0
#define SCREG_G2D_INTR_OVERFLOW_Count                                          1
#define SCREG_G2D_INTR_OVERFLOW_FieldMask                             0x00001FFF
#define SCREG_G2D_INTR_OVERFLOW_ReadMask                              0x00001FFF
#define SCREG_G2D_INTR_OVERFLOW_WriteMask                             0x00001FFF
#define SCREG_G2D_INTR_OVERFLOW_ResetValue                            0x00000000

/* g2d sw reset done.  */
#define SCREG_G2D_INTR_OVERFLOW_SW_RST_DONE                                  0:0
#define SCREG_G2D_INTR_OVERFLOW_SW_RST_DONE_End                                0
#define SCREG_G2D_INTR_OVERFLOW_SW_RST_DONE_Start                              0
#define SCREG_G2D_INTR_OVERFLOW_SW_RST_DONE_Type                             U01

/* g2d pipe0 frame start.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_START                            1:1
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_START_End                          1
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_START_Start                        1
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_START_Type                       U01

/* g2d pipe1 frame start.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_START                            2:2
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_START_End                          2
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_START_Start                        2
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_START_Type                       U01

/* g2d pipe0 frame done.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_DONE                             3:3
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_DONE_End                           3
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_DONE_Start                         3
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_FRAME_DONE_Type                        U01

/* g2d pipe1 framedone.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_DONE                             4:4
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_DONE_End                           4
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_DONE_Start                         4
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_FRAME_DONE_Type                        U01

/* g2d apb hang.  */
#define SCREG_G2D_INTR_OVERFLOW_APB_HANG                                     5:5
#define SCREG_G2D_INTR_OVERFLOW_APB_HANG_End                                   5
#define SCREG_G2D_INTR_OVERFLOW_APB_HANG_Start                                 5
#define SCREG_G2D_INTR_OVERFLOW_APB_HANG_Type                                U01

/* g2d axi read bus hang.  */
#define SCREG_G2D_INTR_OVERFLOW_AXI_RD_BUS_HANG                              6:6
#define SCREG_G2D_INTR_OVERFLOW_AXI_RD_BUS_HANG_End                            6
#define SCREG_G2D_INTR_OVERFLOW_AXI_RD_BUS_HANG_Start                          6
#define SCREG_G2D_INTR_OVERFLOW_AXI_RD_BUS_HANG_Type                         U01

/* g2d axi write bus hang.  */
#define SCREG_G2D_INTR_OVERFLOW_AXI_WR_BUS_HANG                              7:7
#define SCREG_G2D_INTR_OVERFLOW_AXI_WR_BUS_HANG_End                            7
#define SCREG_G2D_INTR_OVERFLOW_AXI_WR_BUS_HANG_Start                          7
#define SCREG_G2D_INTR_OVERFLOW_AXI_WR_BUS_HANG_Type                         U01

/* g2d axi bus error.  */
#define SCREG_G2D_INTR_OVERFLOW_AXI_BUS_ERROR                                8:8
#define SCREG_G2D_INTR_OVERFLOW_AXI_BUS_ERROR_End                              8
#define SCREG_G2D_INTR_OVERFLOW_AXI_BUS_ERROR_Start                            8
#define SCREG_G2D_INTR_OVERFLOW_AXI_BUS_ERROR_Type                           U01

/* g2d pipe0 pvric decode error.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_DECODE_ERROR                     9:9
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_DECODE_ERROR_End                   9
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_DECODE_ERROR_Start                 9
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_DECODE_ERROR_Type                U01

/* g2d pipe0 pvric encode error.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_ENCODE_ERROR                   10:10
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_ENCODE_ERROR_End                  10
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_ENCODE_ERROR_Start                10
#define SCREG_G2D_INTR_OVERFLOW_PIPE0_PVRIC_ENCODE_ERROR_Type                U01

/* g2d pipe1 pvric decode error.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_DECODE_ERROR                   11:11
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_DECODE_ERROR_End                  11
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_DECODE_ERROR_Start                11
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_DECODE_ERROR_Type                U01

/* g2d pipe1 pvric encode error.  */
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_ENCODE_ERROR                   12:12
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_ENCODE_ERROR_End                  12
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_ENCODE_ERROR_Start                12
#define SCREG_G2D_INTR_OVERFLOW_PIPE1_PVRIC_ENCODE_ERROR_Type                U01

/* Register scregLayer0YReqARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y request ARID.  */

#define scregLayer0YReqARIDRegAddrs                                      0x54040
#define SCREG_LAYER0_YREQ_ARID_Address                                  0x150100
#define SCREG_LAYER0_YREQ_ARID_MSB                                            19
#define SCREG_LAYER0_YREQ_ARID_LSB                                             0
#define SCREG_LAYER0_YREQ_ARID_BLK                                             0
#define SCREG_LAYER0_YREQ_ARID_Count                                           1
#define SCREG_LAYER0_YREQ_ARID_FieldMask                              0x000000FF
#define SCREG_LAYER0_YREQ_ARID_ReadMask                               0x000000FF
#define SCREG_LAYER0_YREQ_ARID_WriteMask                              0x000000FF
#define SCREG_LAYER0_YREQ_ARID_ResetValue                             0x00000000

/* Value.  */
#define SCREG_LAYER0_YREQ_ARID_VALUE                                         7:0
#define SCREG_LAYER0_YREQ_ARID_VALUE_End                                       7
#define SCREG_LAYER0_YREQ_ARID_VALUE_Start                                     0
#define SCREG_LAYER0_YREQ_ARID_VALUE_Type                                    U08

/* Register scregLayer0YDataHeaderARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y data header ARID.  */

#define scregLayer0YDataHeaderARIDRegAddrs                               0x54041
#define SCREG_LAYER0_YDATA_HEADER_ARID_Address                          0x150104
#define SCREG_LAYER0_YDATA_HEADER_ARID_MSB                                    19
#define SCREG_LAYER0_YDATA_HEADER_ARID_LSB                                     0
#define SCREG_LAYER0_YDATA_HEADER_ARID_BLK                                     0
#define SCREG_LAYER0_YDATA_HEADER_ARID_Count                                   1
#define SCREG_LAYER0_YDATA_HEADER_ARID_FieldMask                      0x000000FF
#define SCREG_LAYER0_YDATA_HEADER_ARID_ReadMask                       0x000000FF
#define SCREG_LAYER0_YDATA_HEADER_ARID_WriteMask                      0x000000FF
#define SCREG_LAYER0_YDATA_HEADER_ARID_ResetValue                     0x00000002

/* Value.  */
#define SCREG_LAYER0_YDATA_HEADER_ARID_VALUE                                 7:0
#define SCREG_LAYER0_YDATA_HEADER_ARID_VALUE_End                               7
#define SCREG_LAYER0_YDATA_HEADER_ARID_VALUE_Start                             0
#define SCREG_LAYER0_YDATA_HEADER_ARID_VALUE_Type                            U08

/* Register scregLayer0UVReqARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV request ARID.  */

#define scregLayer0UVReqARIDRegAddrs                                     0x54042
#define SCREG_LAYER0_UV_REQ_ARID_Address                                0x150108
#define SCREG_LAYER0_UV_REQ_ARID_MSB                                          19
#define SCREG_LAYER0_UV_REQ_ARID_LSB                                           0
#define SCREG_LAYER0_UV_REQ_ARID_BLK                                           0
#define SCREG_LAYER0_UV_REQ_ARID_Count                                         1
#define SCREG_LAYER0_UV_REQ_ARID_FieldMask                            0x000000FF
#define SCREG_LAYER0_UV_REQ_ARID_ReadMask                             0x000000FF
#define SCREG_LAYER0_UV_REQ_ARID_WriteMask                            0x000000FF
#define SCREG_LAYER0_UV_REQ_ARID_ResetValue                           0x00000001

/* Value.  */
#define SCREG_LAYER0_UV_REQ_ARID_VALUE                                       7:0
#define SCREG_LAYER0_UV_REQ_ARID_VALUE_End                                     7
#define SCREG_LAYER0_UV_REQ_ARID_VALUE_Start                                   0
#define SCREG_LAYER0_UV_REQ_ARID_VALUE_Type                                  U08

/* Register scregLayer0UVDataHeaderARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV data header request ARID.  */

#define scregLayer0UVDataHeaderARIDRegAddrs                              0x54043
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_Address                        0x15010C
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_MSB                                  19
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_LSB                                   0
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_BLK                                   0
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_Count                                 1
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_FieldMask                    0x000000FF
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_ReadMask                     0x000000FF
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_WriteMask                    0x000000FF
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_ResetValue                   0x00000003

/* Value.  */
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_VALUE                               7:0
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_VALUE_End                             7
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_VALUE_Start                           0
#define SCREG_LAYER0_UV_DATA_HEADER_ARID_VALUE_Type                          U08

/* Register scregLayer0YReqAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y request AWID.  */

#define scregLayer0YReqAWIDRegAddrs                                      0x54044
#define SCREG_LAYER0_YREQ_AWID_Address                                  0x150110
#define SCREG_LAYER0_YREQ_AWID_MSB                                            19
#define SCREG_LAYER0_YREQ_AWID_LSB                                             0
#define SCREG_LAYER0_YREQ_AWID_BLK                                             0
#define SCREG_LAYER0_YREQ_AWID_Count                                           1
#define SCREG_LAYER0_YREQ_AWID_FieldMask                              0x000000FF
#define SCREG_LAYER0_YREQ_AWID_ReadMask                               0x000000FF
#define SCREG_LAYER0_YREQ_AWID_WriteMask                              0x000000FF
#define SCREG_LAYER0_YREQ_AWID_ResetValue                             0x00000004

/* Value.  */
#define SCREG_LAYER0_YREQ_AWID_VALUE                                         7:0
#define SCREG_LAYER0_YREQ_AWID_VALUE_End                                       7
#define SCREG_LAYER0_YREQ_AWID_VALUE_Start                                     0
#define SCREG_LAYER0_YREQ_AWID_VALUE_Type                                    U08

/* Register scregLayer0YDataHeaderAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y data header AWID.  */

#define scregLayer0YDataHeaderAWIDRegAddrs                               0x54045
#define SCREG_LAYER0_YDATA_HEADER_AWID_Address                          0x150114
#define SCREG_LAYER0_YDATA_HEADER_AWID_MSB                                    19
#define SCREG_LAYER0_YDATA_HEADER_AWID_LSB                                     0
#define SCREG_LAYER0_YDATA_HEADER_AWID_BLK                                     0
#define SCREG_LAYER0_YDATA_HEADER_AWID_Count                                   1
#define SCREG_LAYER0_YDATA_HEADER_AWID_FieldMask                      0x000000FF
#define SCREG_LAYER0_YDATA_HEADER_AWID_ReadMask                       0x000000FF
#define SCREG_LAYER0_YDATA_HEADER_AWID_WriteMask                      0x000000FF
#define SCREG_LAYER0_YDATA_HEADER_AWID_ResetValue                     0x00000006

/* Value.  */
#define SCREG_LAYER0_YDATA_HEADER_AWID_VALUE                                 7:0
#define SCREG_LAYER0_YDATA_HEADER_AWID_VALUE_End                               7
#define SCREG_LAYER0_YDATA_HEADER_AWID_VALUE_Start                             0
#define SCREG_LAYER0_YDATA_HEADER_AWID_VALUE_Type                            U08

/* Register scregLayer0UVReqAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV request AWID.  */

#define scregLayer0UVReqAWIDRegAddrs                                     0x54046
#define SCREG_LAYER0_UV_REQ_AWID_Address                                0x150118
#define SCREG_LAYER0_UV_REQ_AWID_MSB                                          19
#define SCREG_LAYER0_UV_REQ_AWID_LSB                                           0
#define SCREG_LAYER0_UV_REQ_AWID_BLK                                           0
#define SCREG_LAYER0_UV_REQ_AWID_Count                                         1
#define SCREG_LAYER0_UV_REQ_AWID_FieldMask                            0x000000FF
#define SCREG_LAYER0_UV_REQ_AWID_ReadMask                             0x000000FF
#define SCREG_LAYER0_UV_REQ_AWID_WriteMask                            0x000000FF
#define SCREG_LAYER0_UV_REQ_AWID_ResetValue                           0x00000005

/* Value.  */
#define SCREG_LAYER0_UV_REQ_AWID_VALUE                                       7:0
#define SCREG_LAYER0_UV_REQ_AWID_VALUE_End                                     7
#define SCREG_LAYER0_UV_REQ_AWID_VALUE_Start                                   0
#define SCREG_LAYER0_UV_REQ_AWID_VALUE_Type                                  U08

/* Register scregLayer0UVDataHeaderAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV data header request AWID.  */

#define scregLayer0UVDataHeaderAWIDRegAddrs                              0x54047
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_Address                        0x15011C
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_MSB                                  19
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_LSB                                   0
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_BLK                                   0
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_Count                                 1
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_FieldMask                    0x000000FF
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_ReadMask                     0x000000FF
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_WriteMask                    0x000000FF
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_ResetValue                   0x00000007

/* Value.  */
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_VALUE                               7:0
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_VALUE_End                             7
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_VALUE_Start                           0
#define SCREG_LAYER0_UV_DATA_HEADER_AWID_VALUE_Type                          U08

/* Register scregLayer0Arqos **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Arqos.  */

#define scregLayer0ArqosRegAddrs                                         0x54048
#define SCREG_LAYER0_ARQOS_Address                                      0x150120
#define SCREG_LAYER0_ARQOS_MSB                                                19
#define SCREG_LAYER0_ARQOS_LSB                                                 0
#define SCREG_LAYER0_ARQOS_BLK                                                 0
#define SCREG_LAYER0_ARQOS_Count                                               1
#define SCREG_LAYER0_ARQOS_FieldMask                                  0x0000000F
#define SCREG_LAYER0_ARQOS_ReadMask                                   0x0000000F
#define SCREG_LAYER0_ARQOS_WriteMask                                  0x0000000F
#define SCREG_LAYER0_ARQOS_ResetValue                                 0x00000000

/* Value.  */
#define SCREG_LAYER0_ARQOS_VALUE                                             3:0
#define SCREG_LAYER0_ARQOS_VALUE_End                                           3
#define SCREG_LAYER0_ARQOS_VALUE_Start                                         0
#define SCREG_LAYER0_ARQOS_VALUE_Type                                        U04

/* Register scregLayer0Awqos **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Awqos.  */

#define scregLayer0AwqosRegAddrs                                         0x54049
#define SCREG_LAYER0_AWQOS_Address                                      0x150124
#define SCREG_LAYER0_AWQOS_MSB                                                19
#define SCREG_LAYER0_AWQOS_LSB                                                 0
#define SCREG_LAYER0_AWQOS_BLK                                                 0
#define SCREG_LAYER0_AWQOS_Count                                               1
#define SCREG_LAYER0_AWQOS_FieldMask                                  0x0000000F
#define SCREG_LAYER0_AWQOS_ReadMask                                   0x0000000F
#define SCREG_LAYER0_AWQOS_WriteMask                                  0x0000000F
#define SCREG_LAYER0_AWQOS_ResetValue                                 0x00000000

/* Value.  */
#define SCREG_LAYER0_AWQOS_VALUE                                             3:0
#define SCREG_LAYER0_AWQOS_VALUE_End                                           3
#define SCREG_LAYER0_AWQOS_VALUE_Start                                         0
#define SCREG_LAYER0_AWQOS_VALUE_Type                                        U04

/* Register scregLayer1YReqARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y request ARID.  */

#define scregLayer1YReqARIDRegAddrs                                      0x5404A
#define SCREG_LAYER1_YREQ_ARID_Address                                  0x150128
#define SCREG_LAYER1_YREQ_ARID_MSB                                            19
#define SCREG_LAYER1_YREQ_ARID_LSB                                             0
#define SCREG_LAYER1_YREQ_ARID_BLK                                             0
#define SCREG_LAYER1_YREQ_ARID_Count                                           1
#define SCREG_LAYER1_YREQ_ARID_FieldMask                              0x000000FF
#define SCREG_LAYER1_YREQ_ARID_ReadMask                               0x000000FF
#define SCREG_LAYER1_YREQ_ARID_WriteMask                              0x000000FF
#define SCREG_LAYER1_YREQ_ARID_ResetValue                             0x00000008

/* Value.  */
#define SCREG_LAYER1_YREQ_ARID_VALUE                                         7:0
#define SCREG_LAYER1_YREQ_ARID_VALUE_End                                       7
#define SCREG_LAYER1_YREQ_ARID_VALUE_Start                                     0
#define SCREG_LAYER1_YREQ_ARID_VALUE_Type                                    U08

/* Register scregLayer1YDataHeaderARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y data header ARID.  */

#define scregLayer1YDataHeaderARIDRegAddrs                               0x5404B
#define SCREG_LAYER1_YDATA_HEADER_ARID_Address                          0x15012C
#define SCREG_LAYER1_YDATA_HEADER_ARID_MSB                                    19
#define SCREG_LAYER1_YDATA_HEADER_ARID_LSB                                     0
#define SCREG_LAYER1_YDATA_HEADER_ARID_BLK                                     0
#define SCREG_LAYER1_YDATA_HEADER_ARID_Count                                   1
#define SCREG_LAYER1_YDATA_HEADER_ARID_FieldMask                      0x000000FF
#define SCREG_LAYER1_YDATA_HEADER_ARID_ReadMask                       0x000000FF
#define SCREG_LAYER1_YDATA_HEADER_ARID_WriteMask                      0x000000FF
#define SCREG_LAYER1_YDATA_HEADER_ARID_ResetValue                     0x0000000A

/* Value.  */
#define SCREG_LAYER1_YDATA_HEADER_ARID_VALUE                                 7:0
#define SCREG_LAYER1_YDATA_HEADER_ARID_VALUE_End                               7
#define SCREG_LAYER1_YDATA_HEADER_ARID_VALUE_Start                             0
#define SCREG_LAYER1_YDATA_HEADER_ARID_VALUE_Type                            U08

/* Register scregLayer1UVReqARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV request ARID.  */

#define scregLayer1UVReqARIDRegAddrs                                     0x5404C
#define SCREG_LAYER1_UV_REQ_ARID_Address                                0x150130
#define SCREG_LAYER1_UV_REQ_ARID_MSB                                          19
#define SCREG_LAYER1_UV_REQ_ARID_LSB                                           0
#define SCREG_LAYER1_UV_REQ_ARID_BLK                                           0
#define SCREG_LAYER1_UV_REQ_ARID_Count                                         1
#define SCREG_LAYER1_UV_REQ_ARID_FieldMask                            0x000000FF
#define SCREG_LAYER1_UV_REQ_ARID_ReadMask                             0x000000FF
#define SCREG_LAYER1_UV_REQ_ARID_WriteMask                            0x000000FF
#define SCREG_LAYER1_UV_REQ_ARID_ResetValue                           0x00000009

/* Value.  */
#define SCREG_LAYER1_UV_REQ_ARID_VALUE                                       7:0
#define SCREG_LAYER1_UV_REQ_ARID_VALUE_End                                     7
#define SCREG_LAYER1_UV_REQ_ARID_VALUE_Start                                   0
#define SCREG_LAYER1_UV_REQ_ARID_VALUE_Type                                  U08

/* Register scregLayer1UVDataHeaderARID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV data header request ARID.  */

#define scregLayer1UVDataHeaderARIDRegAddrs                              0x5404D
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_Address                        0x150134
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_MSB                                  19
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_LSB                                   0
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_BLK                                   0
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_Count                                 1
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_FieldMask                    0x000000FF
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_ReadMask                     0x000000FF
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_WriteMask                    0x000000FF
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_ResetValue                   0x0000000B

/* Value.  */
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_VALUE                               7:0
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_VALUE_End                             7
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_VALUE_Start                           0
#define SCREG_LAYER1_UV_DATA_HEADER_ARID_VALUE_Type                          U08

/* Register scregLayer1YReqAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y request AWID.  */

#define scregLayer1YReqAWIDRegAddrs                                      0x5404E
#define SCREG_LAYER1_YREQ_AWID_Address                                  0x150138
#define SCREG_LAYER1_YREQ_AWID_MSB                                            19
#define SCREG_LAYER1_YREQ_AWID_LSB                                             0
#define SCREG_LAYER1_YREQ_AWID_BLK                                             0
#define SCREG_LAYER1_YREQ_AWID_Count                                           1
#define SCREG_LAYER1_YREQ_AWID_FieldMask                              0x000000FF
#define SCREG_LAYER1_YREQ_AWID_ReadMask                               0x000000FF
#define SCREG_LAYER1_YREQ_AWID_WriteMask                              0x000000FF
#define SCREG_LAYER1_YREQ_AWID_ResetValue                             0x0000000C

/* Value.  */
#define SCREG_LAYER1_YREQ_AWID_VALUE                                         7:0
#define SCREG_LAYER1_YREQ_AWID_VALUE_End                                       7
#define SCREG_LAYER1_YREQ_AWID_VALUE_Start                                     0
#define SCREG_LAYER1_YREQ_AWID_VALUE_Type                                    U08

/* Register scregLayer1YDataHeaderAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer ARGB/Y data header AWID.  */

#define scregLayer1YDataHeaderAWIDRegAddrs                               0x5404F
#define SCREG_LAYER1_YDATA_HEADER_AWID_Address                          0x15013C
#define SCREG_LAYER1_YDATA_HEADER_AWID_MSB                                    19
#define SCREG_LAYER1_YDATA_HEADER_AWID_LSB                                     0
#define SCREG_LAYER1_YDATA_HEADER_AWID_BLK                                     0
#define SCREG_LAYER1_YDATA_HEADER_AWID_Count                                   1
#define SCREG_LAYER1_YDATA_HEADER_AWID_FieldMask                      0x000000FF
#define SCREG_LAYER1_YDATA_HEADER_AWID_ReadMask                       0x000000FF
#define SCREG_LAYER1_YDATA_HEADER_AWID_WriteMask                      0x000000FF
#define SCREG_LAYER1_YDATA_HEADER_AWID_ResetValue                     0x0000000E

/* Value.  */
#define SCREG_LAYER1_YDATA_HEADER_AWID_VALUE                                 7:0
#define SCREG_LAYER1_YDATA_HEADER_AWID_VALUE_End                               7
#define SCREG_LAYER1_YDATA_HEADER_AWID_VALUE_Start                             0
#define SCREG_LAYER1_YDATA_HEADER_AWID_VALUE_Type                            U08

/* Register scregLayer1UVReqAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV request AWID.  */

#define scregLayer1UVReqAWIDRegAddrs                                     0x54050
#define SCREG_LAYER1_UV_REQ_AWID_Address                                0x150140
#define SCREG_LAYER1_UV_REQ_AWID_MSB                                          19
#define SCREG_LAYER1_UV_REQ_AWID_LSB                                           0
#define SCREG_LAYER1_UV_REQ_AWID_BLK                                           0
#define SCREG_LAYER1_UV_REQ_AWID_Count                                         1
#define SCREG_LAYER1_UV_REQ_AWID_FieldMask                            0x000000FF
#define SCREG_LAYER1_UV_REQ_AWID_ReadMask                             0x000000FF
#define SCREG_LAYER1_UV_REQ_AWID_WriteMask                            0x000000FF
#define SCREG_LAYER1_UV_REQ_AWID_ResetValue                           0x0000000D

/* Value.  */
#define SCREG_LAYER1_UV_REQ_AWID_VALUE                                       7:0
#define SCREG_LAYER1_UV_REQ_AWID_VALUE_End                                     7
#define SCREG_LAYER1_UV_REQ_AWID_VALUE_Start                                   0
#define SCREG_LAYER1_UV_REQ_AWID_VALUE_Type                                  U08

/* Register scregLayer1UVDataHeaderAWID **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer UV data header request AWID.  */

#define scregLayer1UVDataHeaderAWIDRegAddrs                              0x54051
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_Address                        0x150144
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_MSB                                  19
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_LSB                                   0
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_BLK                                   0
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_Count                                 1
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_FieldMask                    0x000000FF
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_ReadMask                     0x000000FF
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_WriteMask                    0x000000FF
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_ResetValue                   0x0000000F

/* Value.  */
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_VALUE                               7:0
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_VALUE_End                             7
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_VALUE_Start                           0
#define SCREG_LAYER1_UV_DATA_HEADER_AWID_VALUE_Type                          U08

/* Register scregLayer1Arqos **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Arqos.  */

#define scregLayer1ArqosRegAddrs                                         0x54052
#define SCREG_LAYER1_ARQOS_Address                                      0x150148
#define SCREG_LAYER1_ARQOS_MSB                                                19
#define SCREG_LAYER1_ARQOS_LSB                                                 0
#define SCREG_LAYER1_ARQOS_BLK                                                 0
#define SCREG_LAYER1_ARQOS_Count                                               1
#define SCREG_LAYER1_ARQOS_FieldMask                                  0x0000000F
#define SCREG_LAYER1_ARQOS_ReadMask                                   0x0000000F
#define SCREG_LAYER1_ARQOS_WriteMask                                  0x0000000F
#define SCREG_LAYER1_ARQOS_ResetValue                                 0x00000000

/* Value.  */
#define SCREG_LAYER1_ARQOS_VALUE                                             3:0
#define SCREG_LAYER1_ARQOS_VALUE_End                                           3
#define SCREG_LAYER1_ARQOS_VALUE_Start                                         0
#define SCREG_LAYER1_ARQOS_VALUE_Type                                        U04

/* Register scregLayer1Awqos **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Awqos.  */

#define scregLayer1AwqosRegAddrs                                         0x54053
#define SCREG_LAYER1_AWQOS_Address                                      0x15014C
#define SCREG_LAYER1_AWQOS_MSB                                                19
#define SCREG_LAYER1_AWQOS_LSB                                                 0
#define SCREG_LAYER1_AWQOS_BLK                                                 0
#define SCREG_LAYER1_AWQOS_Count                                               1
#define SCREG_LAYER1_AWQOS_FieldMask                                  0x0000000F
#define SCREG_LAYER1_AWQOS_ReadMask                                   0x0000000F
#define SCREG_LAYER1_AWQOS_WriteMask                                  0x0000000F
#define SCREG_LAYER1_AWQOS_ResetValue                                 0x00000000

/* Value.  */
#define SCREG_LAYER1_AWQOS_VALUE                                             3:0
#define SCREG_LAYER1_AWQOS_VALUE_End                                           3
#define SCREG_LAYER1_AWQOS_VALUE_Start                                         0
#define SCREG_LAYER1_AWQOS_VALUE_Type                                        U04

/* Register scregGcControl **
** ~~~~~~~~~~~~~~~~~~~~~~~ */

/* GC Control.  */

#define scregGcControlRegAddrs                                           0x54054
#define SCREG_GC_CONTROL_Address                                        0x150150
#define SCREG_GC_CONTROL_MSB                                                  19
#define SCREG_GC_CONTROL_LSB                                                   0
#define SCREG_GC_CONTROL_BLK                                                   0
#define SCREG_GC_CONTROL_Count                                                 1
#define SCREG_GC_CONTROL_FieldMask                                    0x0000FFFF
#define SCREG_GC_CONTROL_ReadMask                                     0x0000FFFF
#define SCREG_GC_CONTROL_WriteMask                                    0x0000FFFF
#define SCREG_GC_CONTROL_ResetValue                                   0x00000000

/* Value.  */
#define SCREG_GC_CONTROL_VALUE                                              15:0
#define SCREG_GC_CONTROL_VALUE_End                                            15
#define SCREG_GC_CONTROL_VALUE_Start                                           0
#define SCREG_GC_CONTROL_VALUE_Type                                          U16

/* Register scregTimingControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Register Timing Control.  */

#define scregTimingControlRegAddrs                                       0x54055
#define SCREG_TIMING_CONTROL_Address                                    0x150154
#define SCREG_TIMING_CONTROL_MSB                                              19
#define SCREG_TIMING_CONTROL_LSB                                               0
#define SCREG_TIMING_CONTROL_BLK                                               0
#define SCREG_TIMING_CONTROL_Count                                             1
#define SCREG_TIMING_CONTROL_FieldMask                                0xFFFFFFFF
#define SCREG_TIMING_CONTROL_ReadMask                                 0xFFFFFFFF
#define SCREG_TIMING_CONTROL_WriteMask                                0xFFFFFFFF
#define SCREG_TIMING_CONTROL_ResetValue                               0x00000000

/* Value.  */
#define SCREG_TIMING_CONTROL_VALUE                                          31:0
#define SCREG_TIMING_CONTROL_VALUE_End                                        31
#define SCREG_TIMING_CONTROL_VALUE_Start                                       0
#define SCREG_TIMING_CONTROL_VALUE_Type                                      U32

/* Register scregClockGatorDisable **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Clock Gator Disable.  */

#define scregClockGatorDisableRegAddrs                                   0x54056
#define SCREG_CLOCK_GATOR_DISABLE_Address                               0x150158
#define SCREG_CLOCK_GATOR_DISABLE_MSB                                         19
#define SCREG_CLOCK_GATOR_DISABLE_LSB                                          0
#define SCREG_CLOCK_GATOR_DISABLE_BLK                                          0
#define SCREG_CLOCK_GATOR_DISABLE_Count                                        1
#define SCREG_CLOCK_GATOR_DISABLE_FieldMask                           0x0000003F
#define SCREG_CLOCK_GATOR_DISABLE_ReadMask                            0x0000003F
#define SCREG_CLOCK_GATOR_DISABLE_WriteMask                           0x0000003F
#define SCREG_CLOCK_GATOR_DISABLE_ResetValue                          0x00000000

/* Layer0 clock gator disable. 0 means gator enable, 1 means gator disable.  */
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CLOCK_GATOR_DISABLE                 0:0
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CLOCK_GATOR_DISABLE_End               0
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CLOCK_GATOR_DISABLE_Start             0
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CLOCK_GATOR_DISABLE_Type            U01
#define   SCREG_CLOCK_GATOR_DISABLE_LAYER0_CLOCK_GATOR_DISABLE_OFF           0x0
#define   SCREG_CLOCK_GATOR_DISABLE_LAYER0_CLOCK_GATOR_DISABLE_ON            0x1

/* Layer1 clock gator disable.  */
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CLOCK_GATOR_DISABLE                 1:1
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CLOCK_GATOR_DISABLE_End               1
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CLOCK_GATOR_DISABLE_Start             1
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CLOCK_GATOR_DISABLE_Type            U01

/* Layer0 scl clock gator disable.  */
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_SCL_CLOCK_GATOR_DISABLE             2:2
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_SCL_CLOCK_GATOR_DISABLE_End           2
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_SCL_CLOCK_GATOR_DISABLE_Start         2
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_SCL_CLOCK_GATOR_DISABLE_Type        U01

/* Layer1 scl clock gator disable.  */
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_SCL_CLOCK_GATOR_DISABLE             3:3
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_SCL_CLOCK_GATOR_DISABLE_End           3
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_SCL_CLOCK_GATOR_DISABLE_Start         3
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_SCL_CLOCK_GATOR_DISABLE_Type        U01

/* Layer0 crc clock gator disable.  */
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CRC_CLOCK_GATOR_DISABLE             4:4
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CRC_CLOCK_GATOR_DISABLE_End           4
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CRC_CLOCK_GATOR_DISABLE_Start         4
#define SCREG_CLOCK_GATOR_DISABLE_LAYER0_CRC_CLOCK_GATOR_DISABLE_Type        U01

/* Layer1 crc clock gator disable.  */
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CRC_CLOCK_GATOR_DISABLE             5:5
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CRC_CLOCK_GATOR_DISABLE_End           5
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CRC_CLOCK_GATOR_DISABLE_Start         5
#define SCREG_CLOCK_GATOR_DISABLE_LAYER1_CRC_CLOCK_GATOR_DISABLE_Type        U01

/* Register scregSWReset **
** ~~~~~~~~~~~~~~~~~~~~~ */

/* SC Top soft-reset.  */

#define scregSWResetRegAddrs                                             0x54057
#define SCREG_SW_RESET_Address                                          0x15015C
#define SCREG_SW_RESET_MSB                                                    19
#define SCREG_SW_RESET_LSB                                                     0
#define SCREG_SW_RESET_BLK                                                     0
#define SCREG_SW_RESET_Count                                                   1
#define SCREG_SW_RESET_FieldMask                                      0x00000001
#define SCREG_SW_RESET_ReadMask                                       0x00000000
#define SCREG_SW_RESET_WriteMask                                      0x00000001
#define SCREG_SW_RESET_ResetValue                                     0x00000000

/* Value.  */
#define SCREG_SW_RESET_VALUE                                                 0:0
#define SCREG_SW_RESET_VALUE_End                                               0
#define SCREG_SW_RESET_VALUE_Start                                             0
#define SCREG_SW_RESET_VALUE_Type                                            U01

/* Register scregWatchDogEnable **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog enable.  */

#define scregWatchDogEnableRegAddrs                                      0x54058
#define SCREG_WATCH_DOG_ENABLE_Address                                  0x150160
#define SCREG_WATCH_DOG_ENABLE_MSB                                            19
#define SCREG_WATCH_DOG_ENABLE_LSB                                             0
#define SCREG_WATCH_DOG_ENABLE_BLK                                             0
#define SCREG_WATCH_DOG_ENABLE_Count                                           1
#define SCREG_WATCH_DOG_ENABLE_FieldMask                              0x00000007
#define SCREG_WATCH_DOG_ENABLE_ReadMask                               0x00000007
#define SCREG_WATCH_DOG_ENABLE_WriteMask                              0x00000007
#define SCREG_WATCH_DOG_ENABLE_ResetValue                             0x00000000

/* APB watch-dog enable.  */
#define SCREG_WATCH_DOG_ENABLE_APB                                           0:0
#define SCREG_WATCH_DOG_ENABLE_APB_End                                         0
#define SCREG_WATCH_DOG_ENABLE_APB_Start                                       0
#define SCREG_WATCH_DOG_ENABLE_APB_Type                                      U01

/* AXI RD Bus watch-dog enable.  */
#define SCREG_WATCH_DOG_ENABLE_AXI_RD                                        1:1
#define SCREG_WATCH_DOG_ENABLE_AXI_RD_End                                      1
#define SCREG_WATCH_DOG_ENABLE_AXI_RD_Start                                    1
#define SCREG_WATCH_DOG_ENABLE_AXI_RD_Type                                   U01

/* AXI WR Bus watch-dog enable.  */
#define SCREG_WATCH_DOG_ENABLE_AXI_WR                                        2:2
#define SCREG_WATCH_DOG_ENABLE_AXI_WR_End                                      2
#define SCREG_WATCH_DOG_ENABLE_AXI_WR_Start                                    2
#define SCREG_WATCH_DOG_ENABLE_AXI_WR_Type                                   U01

/* Register scregWatchApbTimeout **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog APB timeout.  */

#define scregWatchApbTimeoutRegAddrs                                     0x54059
#define SCREG_WATCH_APB_TIMEOUT_Address                                 0x150164
#define SCREG_WATCH_APB_TIMEOUT_MSB                                           19
#define SCREG_WATCH_APB_TIMEOUT_LSB                                            0
#define SCREG_WATCH_APB_TIMEOUT_BLK                                            0
#define SCREG_WATCH_APB_TIMEOUT_Count                                          1
#define SCREG_WATCH_APB_TIMEOUT_FieldMask                             0xFFFFFFFF
#define SCREG_WATCH_APB_TIMEOUT_ReadMask                              0xFFFFFFFF
#define SCREG_WATCH_APB_TIMEOUT_WriteMask                             0xFFFFFFFF
#define SCREG_WATCH_APB_TIMEOUT_ResetValue                            0x1DCD6500

/* Value.  */
#define SCREG_WATCH_APB_TIMEOUT_VALUE                                       31:0
#define SCREG_WATCH_APB_TIMEOUT_VALUE_End                                     31
#define SCREG_WATCH_APB_TIMEOUT_VALUE_Start                                    0
#define SCREG_WATCH_APB_TIMEOUT_VALUE_Type                                   U32

/* Register scregWatchAxiARTimeout **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog AXI AR timeout.  */

#define scregWatchAxiARTimeoutRegAddrs                                   0x5405A
#define SCREG_WATCH_AXI_AR_TIMEOUT_Address                              0x150168
#define SCREG_WATCH_AXI_AR_TIMEOUT_MSB                                        19
#define SCREG_WATCH_AXI_AR_TIMEOUT_LSB                                         0
#define SCREG_WATCH_AXI_AR_TIMEOUT_BLK                                         0
#define SCREG_WATCH_AXI_AR_TIMEOUT_Count                                       1
#define SCREG_WATCH_AXI_AR_TIMEOUT_FieldMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_AR_TIMEOUT_ReadMask                           0xFFFFFFFF
#define SCREG_WATCH_AXI_AR_TIMEOUT_WriteMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_AR_TIMEOUT_ResetValue                         0x1DCD6500

/* Value.  */
#define SCREG_WATCH_AXI_AR_TIMEOUT_VALUE                                    31:0
#define SCREG_WATCH_AXI_AR_TIMEOUT_VALUE_End                                  31
#define SCREG_WATCH_AXI_AR_TIMEOUT_VALUE_Start                                 0
#define SCREG_WATCH_AXI_AR_TIMEOUT_VALUE_Type                                U32

/* Register scregWatchAxiRDTimeout **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog AXI RD timeout.  */

#define scregWatchAxiRDTimeoutRegAddrs                                   0x5405B
#define SCREG_WATCH_AXI_RD_TIMEOUT_Address                              0x15016C
#define SCREG_WATCH_AXI_RD_TIMEOUT_MSB                                        19
#define SCREG_WATCH_AXI_RD_TIMEOUT_LSB                                         0
#define SCREG_WATCH_AXI_RD_TIMEOUT_BLK                                         0
#define SCREG_WATCH_AXI_RD_TIMEOUT_Count                                       1
#define SCREG_WATCH_AXI_RD_TIMEOUT_FieldMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_RD_TIMEOUT_ReadMask                           0xFFFFFFFF
#define SCREG_WATCH_AXI_RD_TIMEOUT_WriteMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_RD_TIMEOUT_ResetValue                         0x1DCD6500

/* Value.  */
#define SCREG_WATCH_AXI_RD_TIMEOUT_VALUE                                    31:0
#define SCREG_WATCH_AXI_RD_TIMEOUT_VALUE_End                                  31
#define SCREG_WATCH_AXI_RD_TIMEOUT_VALUE_Start                                 0
#define SCREG_WATCH_AXI_RD_TIMEOUT_VALUE_Type                                U32

/* Register scregWatchAxiAWTimeout **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog AXI AW timeout.  */

#define scregWatchAxiAWTimeoutRegAddrs                                   0x5405C
#define SCREG_WATCH_AXI_AW_TIMEOUT_Address                              0x150170
#define SCREG_WATCH_AXI_AW_TIMEOUT_MSB                                        19
#define SCREG_WATCH_AXI_AW_TIMEOUT_LSB                                         0
#define SCREG_WATCH_AXI_AW_TIMEOUT_BLK                                         0
#define SCREG_WATCH_AXI_AW_TIMEOUT_Count                                       1
#define SCREG_WATCH_AXI_AW_TIMEOUT_FieldMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_AW_TIMEOUT_ReadMask                           0xFFFFFFFF
#define SCREG_WATCH_AXI_AW_TIMEOUT_WriteMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_AW_TIMEOUT_ResetValue                         0x1DCD6500

/* Value.  */
#define SCREG_WATCH_AXI_AW_TIMEOUT_VALUE                                    31:0
#define SCREG_WATCH_AXI_AW_TIMEOUT_VALUE_End                                  31
#define SCREG_WATCH_AXI_AW_TIMEOUT_VALUE_Start                                 0
#define SCREG_WATCH_AXI_AW_TIMEOUT_VALUE_Type                                U32

/* Register scregWatchAxiWRTimeout **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog AXI WR timeout.  */

#define scregWatchAxiWRTimeoutRegAddrs                                   0x5405D
#define SCREG_WATCH_AXI_WR_TIMEOUT_Address                              0x150174
#define SCREG_WATCH_AXI_WR_TIMEOUT_MSB                                        19
#define SCREG_WATCH_AXI_WR_TIMEOUT_LSB                                         0
#define SCREG_WATCH_AXI_WR_TIMEOUT_BLK                                         0
#define SCREG_WATCH_AXI_WR_TIMEOUT_Count                                       1
#define SCREG_WATCH_AXI_WR_TIMEOUT_FieldMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_WR_TIMEOUT_ReadMask                           0xFFFFFFFF
#define SCREG_WATCH_AXI_WR_TIMEOUT_WriteMask                          0xFFFFFFFF
#define SCREG_WATCH_AXI_WR_TIMEOUT_ResetValue                         0x1DCD6500

/* Value.  */
#define SCREG_WATCH_AXI_WR_TIMEOUT_VALUE                                    31:0
#define SCREG_WATCH_AXI_WR_TIMEOUT_VALUE_End                                  31
#define SCREG_WATCH_AXI_WR_TIMEOUT_VALUE_Start                                 0
#define SCREG_WATCH_AXI_WR_TIMEOUT_VALUE_Type                                U32

/* Register scregWatchAxiBRESPTimeout **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer watch-dog AXI BRESP timeout.  */

#define scregWatchAxiBRESPTimeoutRegAddrs                                0x5405E
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_Address                           0x150178
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_MSB                                     19
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_LSB                                      0
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_BLK                                      0
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_Count                                    1
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_FieldMask                       0xFFFFFFFF
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_ReadMask                        0xFFFFFFFF
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_WriteMask                       0xFFFFFFFF
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_ResetValue                      0x1DCD6500

/* Value.  */
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_VALUE                                 31:0
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_VALUE_End                               31
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_VALUE_Start                              0
#define SCREG_WATCH_AXI_BRESP_TIMEOUT_VALUE_Type                             U32

/* Register scregDebugTopARReqNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Top AXI RD Bus Request Number.  */

#define scregDebugTopARReqNumRegAddrs                                    0x5405F
#define SCREG_DEBUG_TOP_AR_REQ_NUM_Address                              0x15017C
#define SCREG_DEBUG_TOP_AR_REQ_NUM_MSB                                        19
#define SCREG_DEBUG_TOP_AR_REQ_NUM_LSB                                         0
#define SCREG_DEBUG_TOP_AR_REQ_NUM_BLK                                         0
#define SCREG_DEBUG_TOP_AR_REQ_NUM_Count                                       1
#define SCREG_DEBUG_TOP_AR_REQ_NUM_FieldMask                          0xFFFFFFFF
#define SCREG_DEBUG_TOP_AR_REQ_NUM_ReadMask                           0xFFFFFFFF
#define SCREG_DEBUG_TOP_AR_REQ_NUM_WriteMask                          0x00000000
#define SCREG_DEBUG_TOP_AR_REQ_NUM_ResetValue                         0x00000000

/* Value.  */
#define SCREG_DEBUG_TOP_AR_REQ_NUM_VALUE                                    31:0
#define SCREG_DEBUG_TOP_AR_REQ_NUM_VALUE_End                                  31
#define SCREG_DEBUG_TOP_AR_REQ_NUM_VALUE_Start                                 0
#define SCREG_DEBUG_TOP_AR_REQ_NUM_VALUE_Type                                U32

/* Register scregDebugTopARLastNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Top AXI RD Bus Response Number.  */

#define scregDebugTopARLastNumRegAddrs                                   0x54060
#define SCREG_DEBUG_TOP_AR_LAST_NUM_Address                             0x150180
#define SCREG_DEBUG_TOP_AR_LAST_NUM_MSB                                       19
#define SCREG_DEBUG_TOP_AR_LAST_NUM_LSB                                        0
#define SCREG_DEBUG_TOP_AR_LAST_NUM_BLK                                        0
#define SCREG_DEBUG_TOP_AR_LAST_NUM_Count                                      1
#define SCREG_DEBUG_TOP_AR_LAST_NUM_FieldMask                         0xFFFFFFFF
#define SCREG_DEBUG_TOP_AR_LAST_NUM_ReadMask                          0xFFFFFFFF
#define SCREG_DEBUG_TOP_AR_LAST_NUM_WriteMask                         0x00000000
#define SCREG_DEBUG_TOP_AR_LAST_NUM_ResetValue                        0x00000000

/* Value.  */
#define SCREG_DEBUG_TOP_AR_LAST_NUM_VALUE                                   31:0
#define SCREG_DEBUG_TOP_AR_LAST_NUM_VALUE_End                                 31
#define SCREG_DEBUG_TOP_AR_LAST_NUM_VALUE_Start                                0
#define SCREG_DEBUG_TOP_AR_LAST_NUM_VALUE_Type                               U32

/* Register scregDebugTopAWReqNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Top AXI WR Bus Request Number.  */

#define scregDebugTopAWReqNumRegAddrs                                    0x54061
#define SCREG_DEBUG_TOP_AW_REQ_NUM_Address                              0x150184
#define SCREG_DEBUG_TOP_AW_REQ_NUM_MSB                                        19
#define SCREG_DEBUG_TOP_AW_REQ_NUM_LSB                                         0
#define SCREG_DEBUG_TOP_AW_REQ_NUM_BLK                                         0
#define SCREG_DEBUG_TOP_AW_REQ_NUM_Count                                       1
#define SCREG_DEBUG_TOP_AW_REQ_NUM_FieldMask                          0xFFFFFFFF
#define SCREG_DEBUG_TOP_AW_REQ_NUM_ReadMask                           0xFFFFFFFF
#define SCREG_DEBUG_TOP_AW_REQ_NUM_WriteMask                          0x00000000
#define SCREG_DEBUG_TOP_AW_REQ_NUM_ResetValue                         0x00000000

/* Value.  */
#define SCREG_DEBUG_TOP_AW_REQ_NUM_VALUE                                    31:0
#define SCREG_DEBUG_TOP_AW_REQ_NUM_VALUE_End                                  31
#define SCREG_DEBUG_TOP_AW_REQ_NUM_VALUE_Start                                 0
#define SCREG_DEBUG_TOP_AW_REQ_NUM_VALUE_Type                                U32

/* Register scregDebugTopAWLastNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Top AXI WR Bus Response Number.  */

#define scregDebugTopAWLastNumRegAddrs                                   0x54062
#define SCREG_DEBUG_TOP_AW_LAST_NUM_Address                             0x150188
#define SCREG_DEBUG_TOP_AW_LAST_NUM_MSB                                       19
#define SCREG_DEBUG_TOP_AW_LAST_NUM_LSB                                        0
#define SCREG_DEBUG_TOP_AW_LAST_NUM_BLK                                        0
#define SCREG_DEBUG_TOP_AW_LAST_NUM_Count                                      1
#define SCREG_DEBUG_TOP_AW_LAST_NUM_FieldMask                         0xFFFFFFFF
#define SCREG_DEBUG_TOP_AW_LAST_NUM_ReadMask                          0xFFFFFFFF
#define SCREG_DEBUG_TOP_AW_LAST_NUM_WriteMask                         0x00000000
#define SCREG_DEBUG_TOP_AW_LAST_NUM_ResetValue                        0x00000000

/* Value.  */
#define SCREG_DEBUG_TOP_AW_LAST_NUM_VALUE                                   31:0
#define SCREG_DEBUG_TOP_AW_LAST_NUM_VALUE_End                                 31
#define SCREG_DEBUG_TOP_AW_LAST_NUM_VALUE_Start                                0
#define SCREG_DEBUG_TOP_AW_LAST_NUM_VALUE_Type                               U32

/* Register scregDebugLayer0ARReqNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 AXI RD Bus Request Number.  */

#define scregDebugLayer0ARReqNumRegAddrs                                 0x54063
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_Address                           0x15018C
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_MSB                                     19
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_LSB                                      0
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_BLK                                      0
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_Count                                    1
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_FieldMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_ReadMask                        0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_WriteMask                       0x00000000
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_ResetValue                      0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_VALUE                                 31:0
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_VALUE_End                               31
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_VALUE_Start                              0
#define SCREG_DEBUG_LAYER0_AR_REQ_NUM_VALUE_Type                             U32

/* Register scregDebugLayer0ARLastNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 AXI RD Bus Response Number.  */

#define scregDebugLayer0ARLastNumRegAddrs                                0x54064
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_Address                          0x150190
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_MSB                                    19
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_LSB                                     0
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_BLK                                     0
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_Count                                   1
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_FieldMask                      0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_ReadMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_WriteMask                      0x00000000
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_ResetValue                     0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_VALUE                                31:0
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_VALUE_End                              31
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_VALUE_Start                             0
#define SCREG_DEBUG_LAYER0_AR_LAST_NUM_VALUE_Type                            U32

/* Register scregDebugLayer1ARReqNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 AXI RD Bus Request Number.  */

#define scregDebugLayer1ARReqNumRegAddrs                                 0x54065
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_Address                           0x150194
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_MSB                                     19
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_LSB                                      0
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_BLK                                      0
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_Count                                    1
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_FieldMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_ReadMask                        0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_WriteMask                       0x00000000
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_ResetValue                      0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_VALUE                                 31:0
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_VALUE_End                               31
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_VALUE_Start                              0
#define SCREG_DEBUG_LAYER1_AR_REQ_NUM_VALUE_Type                             U32

/* Register scregDebugLayer1ARLastNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 AXI RD Bus Response Number.  */

#define scregDebugLayer1ARLastNumRegAddrs                                0x54066
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_Address                          0x150198
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_MSB                                    19
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_LSB                                     0
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_BLK                                     0
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_Count                                   1
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_FieldMask                      0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_ReadMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_WriteMask                      0x00000000
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_ResetValue                     0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_VALUE                                31:0
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_VALUE_End                              31
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_VALUE_Start                             0
#define SCREG_DEBUG_LAYER1_AR_LAST_NUM_VALUE_Type                            U32

/* Register scregDebugLayer0AWReqNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 AXI WR Bus Request Number.  */

#define scregDebugLayer0AWReqNumRegAddrs                                 0x54067
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_Address                           0x15019C
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_MSB                                     19
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_LSB                                      0
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_BLK                                      0
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_Count                                    1
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_FieldMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_ReadMask                        0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_WriteMask                       0x00000000
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_ResetValue                      0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_VALUE                                 31:0
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_VALUE_End                               31
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_VALUE_Start                              0
#define SCREG_DEBUG_LAYER0_AW_REQ_NUM_VALUE_Type                             U32

/* Register scregDebugLayer0AWLastNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 AXI WR Bus Response Number.  */

#define scregDebugLayer0AWLastNumRegAddrs                                0x54068
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_Address                          0x1501A0
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_MSB                                    19
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_LSB                                     0
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_BLK                                     0
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_Count                                   1
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_FieldMask                      0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_ReadMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_WriteMask                      0x00000000
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_ResetValue                     0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_VALUE                                31:0
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_VALUE_End                              31
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_VALUE_Start                             0
#define SCREG_DEBUG_LAYER0_AW_LAST_NUM_VALUE_Type                            U32

/* Register scregDebugLayer1AWReqNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 AXI WR Bus Request Number.  */

#define scregDebugLayer1AWReqNumRegAddrs                                 0x54069
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_Address                           0x1501A4
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_MSB                                     19
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_LSB                                      0
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_BLK                                      0
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_Count                                    1
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_FieldMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_ReadMask                        0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_WriteMask                       0x00000000
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_ResetValue                      0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_VALUE                                 31:0
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_VALUE_End                               31
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_VALUE_Start                              0
#define SCREG_DEBUG_LAYER1_AW_REQ_NUM_VALUE_Type                             U32

/* Register scregDebugLayer1AWLastNum **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 AXI WR Bus Response Number.  */

#define scregDebugLayer1AWLastNumRegAddrs                                0x5406A
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_Address                          0x1501A8
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_MSB                                    19
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_LSB                                     0
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_BLK                                     0
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_Count                                   1
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_FieldMask                      0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_ReadMask                       0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_WriteMask                      0x00000000
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_ResetValue                     0x00000000

/* Value.  */
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_VALUE                                31:0
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_VALUE_End                              31
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_VALUE_Start                             0
#define SCREG_DEBUG_LAYER1_AW_LAST_NUM_VALUE_Type                            U32

/* Register scregDebugLayer0DmaSt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 debug dma state.  */

#define scregDebugLayer0DmaStRegAddrs                                    0x5406B
#define SCREG_DEBUG_LAYER0_DMA_ST_Address                               0x1501AC
#define SCREG_DEBUG_LAYER0_DMA_ST_MSB                                         19
#define SCREG_DEBUG_LAYER0_DMA_ST_LSB                                          0
#define SCREG_DEBUG_LAYER0_DMA_ST_BLK                                          0
#define SCREG_DEBUG_LAYER0_DMA_ST_Count                                        1
#define SCREG_DEBUG_LAYER0_DMA_ST_FieldMask                           0x0000003F
#define SCREG_DEBUG_LAYER0_DMA_ST_ReadMask                            0x0000003F
#define SCREG_DEBUG_LAYER0_DMA_ST_WriteMask                           0x00000000
#define SCREG_DEBUG_LAYER0_DMA_ST_ResetValue                          0x00000000

/* Layer0 debug state.  */
#define SCREG_DEBUG_LAYER0_DMA_ST_STATE                                      2:0
#define SCREG_DEBUG_LAYER0_DMA_ST_STATE_End                                    2
#define SCREG_DEBUG_LAYER0_DMA_ST_STATE_Start                                  0
#define SCREG_DEBUG_LAYER0_DMA_ST_STATE_Type                                 U03
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_IDLE                            0x0
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_DMA_MEM_APPLY                   0x1
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_SCL_MEM_APPLY                   0x2
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_WORK                            0x3
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_SCL_MEM_RLS                     0x4
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_DMA_MEM_RLS                     0x5
#define   SCREG_DEBUG_LAYER0_DMA_ST_STATE_ST_PENDING                         0x6

/* Layer0 debug req done.  */
#define SCREG_DEBUG_LAYER0_DMA_ST_REQ_DONE                                   3:3
#define SCREG_DEBUG_LAYER0_DMA_ST_REQ_DONE_End                                 3
#define SCREG_DEBUG_LAYER0_DMA_ST_REQ_DONE_Start                               3
#define SCREG_DEBUG_LAYER0_DMA_ST_REQ_DONE_Type                              U01

/* Layer0 debug dma done.  */
#define SCREG_DEBUG_LAYER0_DMA_ST_DMA_DONE                                   4:4
#define SCREG_DEBUG_LAYER0_DMA_ST_DMA_DONE_End                                 4
#define SCREG_DEBUG_LAYER0_DMA_ST_DMA_DONE_Start                               4
#define SCREG_DEBUG_LAYER0_DMA_ST_DMA_DONE_Type                              U01

/* Layer0 debug roi right.  */
#define SCREG_DEBUG_LAYER0_DMA_ST_ROI_RIGHT                                  5:5
#define SCREG_DEBUG_LAYER0_DMA_ST_ROI_RIGHT_End                                5
#define SCREG_DEBUG_LAYER0_DMA_ST_ROI_RIGHT_Start                              5
#define SCREG_DEBUG_LAYER0_DMA_ST_ROI_RIGHT_Type                             U01
#define   SCREG_DEBUG_LAYER0_DMA_ST_ROI_RIGHT_LEFT_ROI                       0x0
#define   SCREG_DEBUG_LAYER0_DMA_ST_ROI_RIGHT_RIGHT_ROI                      0x1

/* Register scregDebugLayer0DmaOutCoord **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 debug dma out coordinate.  */

#define scregDebugLayer0DmaOutCoordRegAddrs                              0x5406C
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_Address                        0x1501B0
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_MSB                                  19
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_LSB                                   0
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_BLK                                   0
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_Count                                 1
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_FieldMask                    0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_ReadMask                     0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_WriteMask                    0x00000000
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_ResetValue                   0x00000000

/* Layer0 debug dma out coord x.  */
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_X                            15:0
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_X_End                          15
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_X_Start                         0
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_X_Type                        U16

/* Layer0 debug dma out coord y.  */
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_Y                           31:16
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_Y_End                          31
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_Y_Start                        16
#define SCREG_DEBUG_LAYER0_DMA_OUT_COORD_COORD_Y_Type                        U16

/* Register scregDebugLayer0DmaReqBurstCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 debug req burst count.  */

#define scregDebugLayer0DmaReqBurstCntRegAddrs                           0x5406D
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_Address                    0x1501B4
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_MSB                              19
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_LSB                               0
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_BLK                               0
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_Count                             1
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_FieldMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_ReadMask                 0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_WriteMask                0x00000000
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_ResetValue               0x00000000

/* Layer0 debug  req burst count.  */
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_VALUE                          31:0
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_VALUE_End                        31
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_VALUE_Start                       0
#define SCREG_DEBUG_LAYER0_DMA_REQ_BURST_CNT_VALUE_Type                      U32

/* Register scregDebugLayer0DmaRevBurstCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 debug rev burst count.  */

#define scregDebugLayer0DmaRevBurstCntRegAddrs                           0x5406E
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_Address                    0x1501B8
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_MSB                              19
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_LSB                               0
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_BLK                               0
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_Count                             1
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_FieldMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_ReadMask                 0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_WriteMask                0x00000000
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_ResetValue               0x00000000

/* Layer0 debug  rev burst count.  */
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_VALUE                          31:0
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_VALUE_End                        31
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_VALUE_Start                       0
#define SCREG_DEBUG_LAYER0_DMA_REV_BURST_CNT_VALUE_Type                      U32

/* Register scregDebugLayer0WbWdmaAwReqCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 Wdma Aw req count.  */

#define scregDebugLayer0WbWdmaAwReqCntRegAddrs                           0x5406F
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_Address                   0x1501BC
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_MSB                             19
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_LSB                              0
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_BLK                              0
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_Count                            1
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_FieldMask               0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_ReadMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_WriteMask               0x00000000
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_ResetValue              0x00000000

/* Layer0 Wdma Aw req count.  */
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_VALUE                         31:0
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_VALUE_End                       31
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_VALUE_Start                      0
#define SCREG_DEBUG_LAYER0_WB_WDMA_AW_REQ_CNT_VALUE_Type                     U32

/* Register scregDebugLayer0WbWdmaBrespCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 Wdma bresp count.  */

#define scregDebugLayer0WbWdmaBrespCntRegAddrs                           0x54070
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_Address                    0x1501C0
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_MSB                              19
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_LSB                               0
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_BLK                               0
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_Count                             1
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_FieldMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_ReadMask                 0xFFFFFFFF
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_WriteMask                0x00000000
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_ResetValue               0x00000000

/* Layer0 Wdma bresp count.  */
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_VALUE                          31:0
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_VALUE_End                        31
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_VALUE_Start                       0
#define SCREG_DEBUG_LAYER0_WB_WDMA_BRESP_CNT_VALUE_Type                      U32

/* Register scregDebugLayer1DmaSt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 debug dma state.  */

#define scregDebugLayer1DmaStRegAddrs                                    0x54071
#define SCREG_DEBUG_LAYER1_DMA_ST_Address                               0x1501C4
#define SCREG_DEBUG_LAYER1_DMA_ST_MSB                                         19
#define SCREG_DEBUG_LAYER1_DMA_ST_LSB                                          0
#define SCREG_DEBUG_LAYER1_DMA_ST_BLK                                          0
#define SCREG_DEBUG_LAYER1_DMA_ST_Count                                        1
#define SCREG_DEBUG_LAYER1_DMA_ST_FieldMask                           0x0000003F
#define SCREG_DEBUG_LAYER1_DMA_ST_ReadMask                            0x0000003F
#define SCREG_DEBUG_LAYER1_DMA_ST_WriteMask                           0x00000000
#define SCREG_DEBUG_LAYER1_DMA_ST_ResetValue                          0x00000000

/* Layer1 debug state.  */
#define SCREG_DEBUG_LAYER1_DMA_ST_STATE                                      2:0
#define SCREG_DEBUG_LAYER1_DMA_ST_STATE_End                                    2
#define SCREG_DEBUG_LAYER1_DMA_ST_STATE_Start                                  0
#define SCREG_DEBUG_LAYER1_DMA_ST_STATE_Type                                 U03
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_IDLE                            0x0
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_DMA_MEM_APPLY                   0x1
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_SCL_MEM_APPLY                   0x2
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_WORK                            0x3
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_SCL_MEM_RLS                     0x4
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_DMA_MEM_RLS                     0x5
#define   SCREG_DEBUG_LAYER1_DMA_ST_STATE_ST_PENDING                         0x6

/* Layer1 debug req done.  */
#define SCREG_DEBUG_LAYER1_DMA_ST_REQ_DONE                                   3:3
#define SCREG_DEBUG_LAYER1_DMA_ST_REQ_DONE_End                                 3
#define SCREG_DEBUG_LAYER1_DMA_ST_REQ_DONE_Start                               3
#define SCREG_DEBUG_LAYER1_DMA_ST_REQ_DONE_Type                              U01

/* Layer1 debug dma done.  */
#define SCREG_DEBUG_LAYER1_DMA_ST_DMA_DONE                                   4:4
#define SCREG_DEBUG_LAYER1_DMA_ST_DMA_DONE_End                                 4
#define SCREG_DEBUG_LAYER1_DMA_ST_DMA_DONE_Start                               4
#define SCREG_DEBUG_LAYER1_DMA_ST_DMA_DONE_Type                              U01

/* Layer1 debug roi right.  */
#define SCREG_DEBUG_LAYER1_DMA_ST_ROI_RIGHT                                  5:5
#define SCREG_DEBUG_LAYER1_DMA_ST_ROI_RIGHT_End                                5
#define SCREG_DEBUG_LAYER1_DMA_ST_ROI_RIGHT_Start                              5
#define SCREG_DEBUG_LAYER1_DMA_ST_ROI_RIGHT_Type                             U01
#define   SCREG_DEBUG_LAYER1_DMA_ST_ROI_RIGHT_LEFT_ROI                       0x0
#define   SCREG_DEBUG_LAYER1_DMA_ST_ROI_RIGHT_RIGHT_ROI                      0x1

/* Register scregDebugLayer1DmaOutCoord **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 debug dma out coordinate.  */

#define scregDebugLayer1DmaOutCoordRegAddrs                              0x54072
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_Address                        0x1501C8
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_MSB                                  19
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_LSB                                   0
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_BLK                                   0
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_Count                                 1
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_FieldMask                    0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_ReadMask                     0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_WriteMask                    0x00000000
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_ResetValue                   0x00000000

/* Layer1 debug dma out coord x.  */
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_X                            15:0
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_X_End                          15
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_X_Start                         0
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_X_Type                        U16

/* Layer1 debug dma out coord y.  */
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_Y                           31:16
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_Y_End                          31
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_Y_Start                        16
#define SCREG_DEBUG_LAYER1_DMA_OUT_COORD_COORD_Y_Type                        U16

/* Register scregDebugLayer1DmaReqBurstCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 debug req burst count.  */

#define scregDebugLayer1DmaReqBurstCntRegAddrs                           0x54073
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_Address                    0x1501CC
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_MSB                              19
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_LSB                               0
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_BLK                               0
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_Count                             1
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_FieldMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_ReadMask                 0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_WriteMask                0x00000000
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_ResetValue               0x00000000

/* Layer1 debug  req burst count.  */
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_VALUE                          31:0
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_VALUE_End                        31
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_VALUE_Start                       0
#define SCREG_DEBUG_LAYER1_DMA_REQ_BURST_CNT_VALUE_Type                      U32

/* Register scregDebugLayer1DmaRevBurstCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 debug rev burst count.  */

#define scregDebugLayer1DmaRevBurstCntRegAddrs                           0x54074
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_Address                    0x1501D0
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_MSB                              19
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_LSB                               0
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_BLK                               0
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_Count                             1
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_FieldMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_ReadMask                 0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_WriteMask                0x00000000
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_ResetValue               0x00000000

/* Layer1 debug  rev burst count.  */
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_VALUE                          31:0
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_VALUE_End                        31
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_VALUE_Start                       0
#define SCREG_DEBUG_LAYER1_DMA_REV_BURST_CNT_VALUE_Type                      U32

/* Register scregDebugLayer1WbWdmaAwReqCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 Wdma Aw req count.  */

#define scregDebugLayer1WbWdmaAwReqCntRegAddrs                           0x54075
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_Address                   0x1501D4
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_MSB                             19
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_LSB                              0
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_BLK                              0
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_Count                            1
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_FieldMask               0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_ReadMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_WriteMask               0x00000000
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_ResetValue              0x00000000

/* Layer1 Wdma Aw req count.  */
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_VALUE                         31:0
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_VALUE_End                       31
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_VALUE_Start                      0
#define SCREG_DEBUG_LAYER1_WB_WDMA_AW_REQ_CNT_VALUE_Type                     U32

/* Register scregDebugLayer1WbWdmaBrespCnt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 Wdma bresp count.  */

#define scregDebugLayer1WbWdmaBrespCntRegAddrs                           0x54076
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_Address                    0x1501D8
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_MSB                              19
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_LSB                               0
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_BLK                               0
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_Count                             1
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_FieldMask                0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_ReadMask                 0xFFFFFFFF
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_WriteMask                0x00000000
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_ResetValue               0x00000000

/* Layer1 Wdma bresp count.  */
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_VALUE                          31:0
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_VALUE_End                        31
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_VALUE_Start                       0
#define SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_VALUE_Type                      U32

/* Register scregHostEnd **
** ~~~~~~~~~~~~~~~~~~~~~ */

/* End address of this module.  Reserved.  */

#define scregHostEndRegAddrs                                             0x54077
#define SCREG_HOST_END_Address                                          0x1501DC
#define SCREG_HOST_END_MSB                                                    19
#define SCREG_HOST_END_LSB                                                     0
#define SCREG_HOST_END_BLK                                                     0
#define SCREG_HOST_END_Count                                                   1
#define SCREG_HOST_END_FieldMask                                      0xFFFFFFFF
#define SCREG_HOST_END_ReadMask                                       0xFFFFFFFF
#define SCREG_HOST_END_WriteMask                                      0xFFFFFFFF
#define SCREG_HOST_END_ResetValue                                     0x00000000

#define SCREG_HOST_END_ADDRESS                                              31:0
#define SCREG_HOST_END_ADDRESS_End                                            31
#define SCREG_HOST_END_ADDRESS_Start                                           0
#define SCREG_HOST_END_ADDRESS_Type                                          U32

/*******************************************************************************
**                              ~~~~~~~~~~~~~~~~                              **
**                              Module G2dLayer0                              **
**                              ~~~~~~~~~~~~~~~~                              **
*******************************************************************************/

/* Register scregLayer0Config **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer shadow Configuration Register.  Layer attributes control.  */

#define scregLayer0ConfigRegAddrs                                        0x54400
#define SCREG_LAYER0_CONFIG_Address                                     0x151000
#define SCREG_LAYER0_CONFIG_MSB                                               19
#define SCREG_LAYER0_CONFIG_LSB                                                0
#define SCREG_LAYER0_CONFIG_BLK                                                0
#define SCREG_LAYER0_CONFIG_Count                                              1
#define SCREG_LAYER0_CONFIG_FieldMask                                 0x0FFFFFFC
#define SCREG_LAYER0_CONFIG_ReadMask                                  0x0FFFFFFC
#define SCREG_LAYER0_CONFIG_WriteMask                                 0x0FFFFFFC
#define SCREG_LAYER0_CONFIG_ResetValue                                0x00800000

/* Enable this layer. Reserved: dut tie high. */
#define SCREG_LAYER0_CONFIG_ENABLE                                           2:2
#define SCREG_LAYER0_CONFIG_ENABLE_End                                         2
#define SCREG_LAYER0_CONFIG_ENABLE_Start                                       2
#define SCREG_LAYER0_CONFIG_ENABLE_Type                                      U01
#define   SCREG_LAYER0_CONFIG_ENABLE_DISABLED                                0x0
#define   SCREG_LAYER0_CONFIG_ENABLE_ENABLED                                 0x1

/* Normal mode: read full image, don’t support ROI. One ROI mode: read one  **
** ROI region in one image.                                                   */
#define SCREG_LAYER0_CONFIG_DMA_MODE                                         3:3
#define SCREG_LAYER0_CONFIG_DMA_MODE_End                                       3
#define SCREG_LAYER0_CONFIG_DMA_MODE_Start                                     3
#define SCREG_LAYER0_CONFIG_DMA_MODE_Type                                    U01
#define   SCREG_LAYER0_CONFIG_DMA_MODE_NORMAL                                0x0
#define   SCREG_LAYER0_CONFIG_DMA_MODE_ONE_ROI                               0x1

/* Tile mode.  */
#define SCREG_LAYER0_CONFIG_TILE_MODE                                        7:4
#define SCREG_LAYER0_CONFIG_TILE_MODE_End                                      7
#define SCREG_LAYER0_CONFIG_TILE_MODE_Start                                    4
#define SCREG_LAYER0_CONFIG_TILE_MODE_Type                                   U04
#define   SCREG_LAYER0_CONFIG_TILE_MODE_LINEAR                               0x0
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED32X2                            0x1
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED16X4                            0x2
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED32X4                            0x3
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED32X8                            0x4
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED16X8                            0x5
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED8X8                             0x6
#define   SCREG_LAYER0_CONFIG_TILE_MODE_TILED16X16                           0x7

/* The format of the layer.  */
#define SCREG_LAYER0_CONFIG_FORMAT                                          13:8
#define SCREG_LAYER0_CONFIG_FORMAT_End                                        13
#define SCREG_LAYER0_CONFIG_FORMAT_Start                                       8
#define SCREG_LAYER0_CONFIG_FORMAT_Type                                      U06
#define   SCREG_LAYER0_CONFIG_FORMAT_A8R8G8B8                               0x00
#define   SCREG_LAYER0_CONFIG_FORMAT_X8R8G8B8                               0x01
#define   SCREG_LAYER0_CONFIG_FORMAT_A2R10G10B10                            0x02
#define   SCREG_LAYER0_CONFIG_FORMAT_X2R10G10B10                            0x03
#define   SCREG_LAYER0_CONFIG_FORMAT_R8G8B8                                 0x04
#define   SCREG_LAYER0_CONFIG_FORMAT_R5G6B5                                 0x05
#define   SCREG_LAYER0_CONFIG_FORMAT_A1R5G5B5                               0x06
#define   SCREG_LAYER0_CONFIG_FORMAT_X1R5G5B5                               0x07
#define   SCREG_LAYER0_CONFIG_FORMAT_A4R4G4B4                               0x08
#define   SCREG_LAYER0_CONFIG_FORMAT_X4R4G4B4                               0x09
#define   SCREG_LAYER0_CONFIG_FORMAT_FP16                                   0x0A
#define   SCREG_LAYER0_CONFIG_FORMAT_YUY2                                   0x0B
#define   SCREG_LAYER0_CONFIG_FORMAT_UYVY                                   0x0C
#define   SCREG_LAYER0_CONFIG_FORMAT_YV12                                   0x0D
#define   SCREG_LAYER0_CONFIG_FORMAT_NV12                                   0x0E
#define   SCREG_LAYER0_CONFIG_FORMAT_NV16                                   0x0F
#define   SCREG_LAYER0_CONFIG_FORMAT_P010                                   0x10
#define   SCREG_LAYER0_CONFIG_FORMAT_P210                                   0x11
#define   SCREG_LAYER0_CONFIG_FORMAT_YUV420_PACKED_10BIT                    0x12
#define   SCREG_LAYER0_CONFIG_FORMAT_YV12_10BIT_MSB                         0x13
#define   SCREG_LAYER0_CONFIG_FORMAT_YUY2_10BIT                             0x14
#define   SCREG_LAYER0_CONFIG_FORMAT_UYVY_10BIT                             0x15

/* Compress Dec enable.  */
#define SCREG_LAYER0_CONFIG_COMPRESS_ENABLE                                14:14
#define SCREG_LAYER0_CONFIG_COMPRESS_ENABLE_End                               14
#define SCREG_LAYER0_CONFIG_COMPRESS_ENABLE_Start                             14
#define SCREG_LAYER0_CONFIG_COMPRESS_ENABLE_Type                             U01
#define   SCREG_LAYER0_CONFIG_COMPRESS_ENABLE_DISABLED                       0x0
#define   SCREG_LAYER0_CONFIG_COMPRESS_ENABLE_ENABLED                        0x1

/* Rot angle.  */
#define SCREG_LAYER0_CONFIG_ROT_ANGLE                                      17:15
#define SCREG_LAYER0_CONFIG_ROT_ANGLE_End                                     17
#define SCREG_LAYER0_CONFIG_ROT_ANGLE_Start                                   15
#define SCREG_LAYER0_CONFIG_ROT_ANGLE_Type                                   U03
#define   SCREG_LAYER0_CONFIG_ROT_ANGLE_ROT0                                 0x0
#define   SCREG_LAYER0_CONFIG_ROT_ANGLE_ROT90                                0x1
#define   SCREG_LAYER0_CONFIG_ROT_ANGLE_ROT180                               0x2
#define   SCREG_LAYER0_CONFIG_ROT_ANGLE_ROT270                               0x3
#define   SCREG_LAYER0_CONFIG_ROT_ANGLE_FLIP_X                               0x4
#define   SCREG_LAYER0_CONFIG_ROT_ANGLE_FLIP_Y                               0x5

/* Enable scale or disable scale.  */
#define SCREG_LAYER0_CONFIG_SCALE                                          18:18
#define SCREG_LAYER0_CONFIG_SCALE_End                                         18
#define SCREG_LAYER0_CONFIG_SCALE_Start                                       18
#define SCREG_LAYER0_CONFIG_SCALE_Type                                       U01
#define   SCREG_LAYER0_CONFIG_SCALE_DISABLED                                 0x0
#define   SCREG_LAYER0_CONFIG_SCALE_ENABLED                                  0x1

/* Enable dither or disable dither.  */
#define SCREG_LAYER0_CONFIG_DITHER                                         19:19
#define SCREG_LAYER0_CONFIG_DITHER_End                                        19
#define SCREG_LAYER0_CONFIG_DITHER_Start                                      19
#define SCREG_LAYER0_CONFIG_DITHER_Type                                      U01
#define   SCREG_LAYER0_CONFIG_DITHER_DISABLED                                0x0
#define   SCREG_LAYER0_CONFIG_DITHER_ENABLED                                 0x1

/* Assign UV swizzle, 0 means UV, 1 means VU.  */
#define SCREG_LAYER0_CONFIG_UV_SWIZZLE                                     20:20
#define SCREG_LAYER0_CONFIG_UV_SWIZZLE_End                                    20
#define SCREG_LAYER0_CONFIG_UV_SWIZZLE_Start                                  20
#define SCREG_LAYER0_CONFIG_UV_SWIZZLE_Type                                  U01
#define   SCREG_LAYER0_CONFIG_UV_SWIZZLE_UV                                  0x0
#define   SCREG_LAYER0_CONFIG_UV_SWIZZLE_VU                                  0x1

/* Assign swizzle for ARGB.  */
#define SCREG_LAYER0_CONFIG_SWIZZLE                                        22:21
#define SCREG_LAYER0_CONFIG_SWIZZLE_End                                       22
#define SCREG_LAYER0_CONFIG_SWIZZLE_Start                                     21
#define SCREG_LAYER0_CONFIG_SWIZZLE_Type                                     U02
#define   SCREG_LAYER0_CONFIG_SWIZZLE_ARGB                                   0x0
#define   SCREG_LAYER0_CONFIG_SWIZZLE_RGBA                                   0x1
#define   SCREG_LAYER0_CONFIG_SWIZZLE_ABGR                                   0x2
#define   SCREG_LAYER0_CONFIG_SWIZZLE_BGRA                                   0x3

#define SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE                               24:23
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE_End                              24
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE_Start                            23
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE_Type                            U02
#define   SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE_MODE0                         0x0
#define   SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE_MODE1                         0x1
#define   SCREG_LAYER0_CONFIG_EXTEND_BITS_MODE_MODE2                         0x2

/* YUV to RGB conversion enable bit.  */
#define SCREG_LAYER0_CONFIG_Y2R                                            25:25
#define SCREG_LAYER0_CONFIG_Y2R_End                                           25
#define SCREG_LAYER0_CONFIG_Y2R_Start                                         25
#define SCREG_LAYER0_CONFIG_Y2R_Type                                         U01
#define   SCREG_LAYER0_CONFIG_Y2R_DISABLED                                   0x0
#define   SCREG_LAYER0_CONFIG_Y2R_ENABLED                                    0x1

/* RGB to YUV conversion enable bit.  */
#define SCREG_LAYER0_CONFIG_R2Y                                            26:26
#define SCREG_LAYER0_CONFIG_R2Y_End                                           26
#define SCREG_LAYER0_CONFIG_R2Y_Start                                         26
#define SCREG_LAYER0_CONFIG_R2Y_Type                                         U01
#define   SCREG_LAYER0_CONFIG_R2Y_DISABLED                                   0x0
#define   SCREG_LAYER0_CONFIG_R2Y_ENABLED                                    0x1

/* Extend bits for A2RGB10 alpha channel.  0: Set LSB bits use                **
** ExtendBitsMode.  1: Set LSB bits from register.                            */
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_ALPHA_MODE                         27:27
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_ALPHA_MODE_End                        27
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_ALPHA_MODE_Start                      27
#define SCREG_LAYER0_CONFIG_EXTEND_BITS_ALPHA_MODE_Type                      U01
#define   SCREG_LAYER0_CONFIG_EXTEND_BITS_ALPHA_MODE_DISABLED                0x0
#define   SCREG_LAYER0_CONFIG_EXTEND_BITS_ALPHA_MODE_ENABLED                 0x1

/* Register scregLayer0Start **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Configuration Register.  Layer attributes control.  This register is **
** not buffered.                                                              */

#define scregLayer0StartRegAddrs                                         0x54401
#define SCREG_LAYER0_START_Address                                      0x151004
#define SCREG_LAYER0_START_MSB                                                19
#define SCREG_LAYER0_START_LSB                                                 0
#define SCREG_LAYER0_START_BLK                                                 0
#define SCREG_LAYER0_START_Count                                               1
#define SCREG_LAYER0_START_FieldMask                                  0x00000001
#define SCREG_LAYER0_START_ReadMask                                   0x00000001
#define SCREG_LAYER0_START_WriteMask                                  0x00000001
#define SCREG_LAYER0_START_ResetValue                                 0x00000000

/* Start Layer. This bit is a pulse. */
#define SCREG_LAYER0_START_START                                             0:0
#define SCREG_LAYER0_START_START_End                                           0
#define SCREG_LAYER0_START_START_Start                                         0
#define SCREG_LAYER0_START_START_Type                                        U01
#define   SCREG_LAYER0_START_START_DISABLED                                  0x0
#define   SCREG_LAYER0_START_START_ENABLED                                   0x1

/* Register scregLayer0Reset **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Configuration Register.  Layer attributes control.  This register is **
** not buffered.                                                              */

#define scregLayer0ResetRegAddrs                                         0x54402
#define SCREG_LAYER0_RESET_Address                                      0x151008
#define SCREG_LAYER0_RESET_MSB                                                19
#define SCREG_LAYER0_RESET_LSB                                                 0
#define SCREG_LAYER0_RESET_BLK                                                 0
#define SCREG_LAYER0_RESET_Count                                               1
#define SCREG_LAYER0_RESET_FieldMask                                  0x00000001
#define SCREG_LAYER0_RESET_ReadMask                                   0x00000001
#define SCREG_LAYER0_RESET_WriteMask                                  0x00000001
#define SCREG_LAYER0_RESET_ResetValue                                 0x00000000

/* Reset Layer Registers to default value. */
#define SCREG_LAYER0_RESET_RESET                                             0:0
#define SCREG_LAYER0_RESET_RESET_End                                           0
#define SCREG_LAYER0_RESET_RESET_Start                                         0
#define SCREG_LAYER0_RESET_RESET_Type                                        U01
#define   SCREG_LAYER0_RESET_RESET_RESET                                     0x1

/* Register scregLayer0Size **
** ~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Window Size Register.  Window size of frame buffer in memory in      **
** pixels. If frame buffer is rotated or scaled, this size may be different   **
** from size of display window.                                               */

#define scregLayer0SizeRegAddrs                                          0x54403
#define SCREG_LAYER0_SIZE_Address                                       0x15100C
#define SCREG_LAYER0_SIZE_MSB                                                 19
#define SCREG_LAYER0_SIZE_LSB                                                  0
#define SCREG_LAYER0_SIZE_BLK                                                  0
#define SCREG_LAYER0_SIZE_Count                                                1
#define SCREG_LAYER0_SIZE_FieldMask                                   0xFFFFFFFF
#define SCREG_LAYER0_SIZE_ReadMask                                    0xFFFFFFFF
#define SCREG_LAYER0_SIZE_WriteMask                                   0xFFFFFFFF
#define SCREG_LAYER0_SIZE_ResetValue                                  0x00000000

/* Width.  */
#define SCREG_LAYER0_SIZE_WIDTH                                             15:0
#define SCREG_LAYER0_SIZE_WIDTH_End                                           15
#define SCREG_LAYER0_SIZE_WIDTH_Start                                          0
#define SCREG_LAYER0_SIZE_WIDTH_Type                                         U16

/* Height.  */
#define SCREG_LAYER0_SIZE_HEIGHT                                           31:16
#define SCREG_LAYER0_SIZE_HEIGHT_End                                          31
#define SCREG_LAYER0_SIZE_HEIGHT_Start                                        16
#define SCREG_LAYER0_SIZE_HEIGHT_Type                                        U16

/* Register scregLayer0Address **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Start Address Register.  Starting address of the frame buffer.       **
** Address.                                                                   */

#define scregLayer0AddressRegAddrs                                       0x54404
#define SCREG_LAYER0_ADDRESS_Address                                    0x151010
#define SCREG_LAYER0_ADDRESS_MSB                                              19
#define SCREG_LAYER0_ADDRESS_LSB                                               0
#define SCREG_LAYER0_ADDRESS_BLK                                               0
#define SCREG_LAYER0_ADDRESS_Count                                             1
#define SCREG_LAYER0_ADDRESS_FieldMask                                0xFFFFFFFF
#define SCREG_LAYER0_ADDRESS_ReadMask                                 0xFFFFFFFF
#define SCREG_LAYER0_ADDRESS_WriteMask                                0xFFFFFFFF
#define SCREG_LAYER0_ADDRESS_ResetValue                               0x00000000

#define SCREG_LAYER0_ADDRESS_ADDRESS                                        31:0
#define SCREG_LAYER0_ADDRESS_ADDRESS_End                                      31
#define SCREG_LAYER0_ADDRESS_ADDRESS_Start                                     0
#define SCREG_LAYER0_ADDRESS_ADDRESS_Type                                    U32

/* Register scregLayer0HighAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Start Address Register for high 4 bits.  Starting address of the     **
** frame buffer.  High address.                                               */

#define scregLayer0HighAddressRegAddrs                                   0x54405
#define SCREG_LAYER0_HIGH_ADDRESS_Address                               0x151014
#define SCREG_LAYER0_HIGH_ADDRESS_MSB                                         19
#define SCREG_LAYER0_HIGH_ADDRESS_LSB                                          0
#define SCREG_LAYER0_HIGH_ADDRESS_BLK                                          0
#define SCREG_LAYER0_HIGH_ADDRESS_Count                                        1
#define SCREG_LAYER0_HIGH_ADDRESS_FieldMask                           0x000000FF
#define SCREG_LAYER0_HIGH_ADDRESS_ReadMask                            0x000000FF
#define SCREG_LAYER0_HIGH_ADDRESS_WriteMask                           0x000000FF
#define SCREG_LAYER0_HIGH_ADDRESS_ResetValue                          0x00000000

#define SCREG_LAYER0_HIGH_ADDRESS_ADDRESS                                    7:0
#define SCREG_LAYER0_HIGH_ADDRESS_ADDRESS_End                                  7
#define SCREG_LAYER0_HIGH_ADDRESS_ADDRESS_Start                                0
#define SCREG_LAYER0_HIGH_ADDRESS_ADDRESS_Type                               U08

/* Register scregLayer0UAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Second Plane U Start Address Register.  Starting address of the      **
** second planar (often the U plane) of the Layer if the second plane exists. **
**  Address.                                                                  */

#define scregLayer0UAddressRegAddrs                                      0x54406
#define SCREG_LAYER0_UADDRESS_Address                                   0x151018
#define SCREG_LAYER0_UADDRESS_MSB                                             19
#define SCREG_LAYER0_UADDRESS_LSB                                              0
#define SCREG_LAYER0_UADDRESS_BLK                                              0
#define SCREG_LAYER0_UADDRESS_Count                                            1
#define SCREG_LAYER0_UADDRESS_FieldMask                               0xFFFFFFFF
#define SCREG_LAYER0_UADDRESS_ReadMask                                0xFFFFFFFF
#define SCREG_LAYER0_UADDRESS_WriteMask                               0xFFFFFFFF
#define SCREG_LAYER0_UADDRESS_ResetValue                              0x00000000

#define SCREG_LAYER0_UADDRESS_ADDRESS                                       31:0
#define SCREG_LAYER0_UADDRESS_ADDRESS_End                                     31
#define SCREG_LAYER0_UADDRESS_ADDRESS_Start                                    0
#define SCREG_LAYER0_UADDRESS_ADDRESS_Type                                   U32

/* Register scregLayer0HighUAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Second Plane U Start Address Register for high 4 bits.  Starting     **
** address of the second planar (often the U plane) of the Layer if the       **
** second plane exists.  High address.                                        */

#define scregLayer0HighUAddressRegAddrs                                  0x54407
#define SCREG_LAYER0_HIGH_UADDRESS_Address                              0x15101C
#define SCREG_LAYER0_HIGH_UADDRESS_MSB                                        19
#define SCREG_LAYER0_HIGH_UADDRESS_LSB                                         0
#define SCREG_LAYER0_HIGH_UADDRESS_BLK                                         0
#define SCREG_LAYER0_HIGH_UADDRESS_Count                                       1
#define SCREG_LAYER0_HIGH_UADDRESS_FieldMask                          0x000000FF
#define SCREG_LAYER0_HIGH_UADDRESS_ReadMask                           0x000000FF
#define SCREG_LAYER0_HIGH_UADDRESS_WriteMask                          0x000000FF
#define SCREG_LAYER0_HIGH_UADDRESS_ResetValue                         0x00000000

#define SCREG_LAYER0_HIGH_UADDRESS_ADDRESS                                   7:0
#define SCREG_LAYER0_HIGH_UADDRESS_ADDRESS_End                                 7
#define SCREG_LAYER0_HIGH_UADDRESS_ADDRESS_Start                               0
#define SCREG_LAYER0_HIGH_UADDRESS_ADDRESS_Type                              U08

/* Register scregLayer0VAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer third Plane V Start Address Register.  Starting address of the third **
** planar (often the V plane) of the Layer if the third plane exists.         **
** Address.                                                                   */

#define scregLayer0VAddressRegAddrs                                      0x54408
#define SCREG_LAYER0_VADDRESS_Address                                   0x151020
#define SCREG_LAYER0_VADDRESS_MSB                                             19
#define SCREG_LAYER0_VADDRESS_LSB                                              0
#define SCREG_LAYER0_VADDRESS_BLK                                              0
#define SCREG_LAYER0_VADDRESS_Count                                            1
#define SCREG_LAYER0_VADDRESS_FieldMask                               0xFFFFFFFF
#define SCREG_LAYER0_VADDRESS_ReadMask                                0xFFFFFFFF
#define SCREG_LAYER0_VADDRESS_WriteMask                               0xFFFFFFFF
#define SCREG_LAYER0_VADDRESS_ResetValue                              0x00000000

#define SCREG_LAYER0_VADDRESS_ADDRESS                                       31:0
#define SCREG_LAYER0_VADDRESS_ADDRESS_End                                     31
#define SCREG_LAYER0_VADDRESS_ADDRESS_Start                                    0
#define SCREG_LAYER0_VADDRESS_ADDRESS_Type                                   U32

/* Register scregLayer0HighVAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer third Plane V Start Address Register for high 4 bits.  Starting      **
** address of the third planar (often the V plane) of the Layer if the third  **
** plane exists.  High address.                                               */

#define scregLayer0HighVAddressRegAddrs                                  0x54409
#define SCREG_LAYER0_HIGH_VADDRESS_Address                              0x151024
#define SCREG_LAYER0_HIGH_VADDRESS_MSB                                        19
#define SCREG_LAYER0_HIGH_VADDRESS_LSB                                         0
#define SCREG_LAYER0_HIGH_VADDRESS_BLK                                         0
#define SCREG_LAYER0_HIGH_VADDRESS_Count                                       1
#define SCREG_LAYER0_HIGH_VADDRESS_FieldMask                          0x000000FF
#define SCREG_LAYER0_HIGH_VADDRESS_ReadMask                           0x000000FF
#define SCREG_LAYER0_HIGH_VADDRESS_WriteMask                          0x000000FF
#define SCREG_LAYER0_HIGH_VADDRESS_ResetValue                         0x00000000

#define SCREG_LAYER0_HIGH_VADDRESS_ADDRESS                                   7:0
#define SCREG_LAYER0_HIGH_VADDRESS_ADDRESS_End                                 7
#define SCREG_LAYER0_HIGH_VADDRESS_ADDRESS_Start                               0
#define SCREG_LAYER0_HIGH_VADDRESS_ADDRESS_Type                              U08

/* Register scregLayer0Stride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Stride Register.  Stride of the frame buffer in bytes. */

#define scregLayer0StrideRegAddrs                                        0x5440A
#define SCREG_LAYER0_STRIDE_Address                                     0x151028
#define SCREG_LAYER0_STRIDE_MSB                                               19
#define SCREG_LAYER0_STRIDE_LSB                                                0
#define SCREG_LAYER0_STRIDE_BLK                                                0
#define SCREG_LAYER0_STRIDE_Count                                              1
#define SCREG_LAYER0_STRIDE_FieldMask                                 0x0003FFFF
#define SCREG_LAYER0_STRIDE_ReadMask                                  0x0003FFFF
#define SCREG_LAYER0_STRIDE_WriteMask                                 0x0003FFFF
#define SCREG_LAYER0_STRIDE_ResetValue                                0x00000000

/* Number of bytes from start of one line to the next line.  */
#define SCREG_LAYER0_STRIDE_STRIDE                                          17:0
#define SCREG_LAYER0_STRIDE_STRIDE_End                                        17
#define SCREG_LAYER0_STRIDE_STRIDE_Start                                       0
#define SCREG_LAYER0_STRIDE_STRIDE_Type                                      U18

/* Register scregLayer0UStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Second Plane U Stride Register.  Stride of the second planar (often  **
** the U plane) of Layer if the second plane exists.                          */

#define scregLayer0UStrideRegAddrs                                       0x5440B
#define SCREG_LAYER0_USTRIDE_Address                                    0x15102C
#define SCREG_LAYER0_USTRIDE_MSB                                              19
#define SCREG_LAYER0_USTRIDE_LSB                                               0
#define SCREG_LAYER0_USTRIDE_BLK                                               0
#define SCREG_LAYER0_USTRIDE_Count                                             1
#define SCREG_LAYER0_USTRIDE_FieldMask                                0x0003FFFF
#define SCREG_LAYER0_USTRIDE_ReadMask                                 0x0003FFFF
#define SCREG_LAYER0_USTRIDE_WriteMask                                0x0003FFFF
#define SCREG_LAYER0_USTRIDE_ResetValue                               0x00000000

/* Number of bytes from the start of one line to the next line. */
#define SCREG_LAYER0_USTRIDE_STRIDE                                         17:0
#define SCREG_LAYER0_USTRIDE_STRIDE_End                                       17
#define SCREG_LAYER0_USTRIDE_STRIDE_Start                                      0
#define SCREG_LAYER0_USTRIDE_STRIDE_Type                                     U18

/* Register scregLayer0VStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Third Plane V Stride Register.  Stride of the third planar (often    **
** the V plane) of the Layer if a third plane exists.                         */

#define scregLayer0VStrideRegAddrs                                       0x5440C
#define SCREG_LAYER0_VSTRIDE_Address                                    0x151030
#define SCREG_LAYER0_VSTRIDE_MSB                                              19
#define SCREG_LAYER0_VSTRIDE_LSB                                               0
#define SCREG_LAYER0_VSTRIDE_BLK                                               0
#define SCREG_LAYER0_VSTRIDE_Count                                             1
#define SCREG_LAYER0_VSTRIDE_FieldMask                                0x0003FFFF
#define SCREG_LAYER0_VSTRIDE_ReadMask                                 0x0003FFFF
#define SCREG_LAYER0_VSTRIDE_WriteMask                                0x0003FFFF
#define SCREG_LAYER0_VSTRIDE_ResetValue                               0x00000000

/* Number of bytes from the start of one line to the next line.  */
#define SCREG_LAYER0_VSTRIDE_STRIDE                                         17:0
#define SCREG_LAYER0_VSTRIDE_STRIDE_End                                       17
#define SCREG_LAYER0_VSTRIDE_STRIDE_Start                                      0
#define SCREG_LAYER0_VSTRIDE_STRIDE_Type                                     U18

/* Register scregLayer0InROIOrigin **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Input Region of Interest Origin Register.  */

#define scregLayer0InROIOriginRegAddrs                                   0x5440D
#define SCREG_LAYER0_IN_ROI_ORIGIN_Address                              0x151034
#define SCREG_LAYER0_IN_ROI_ORIGIN_MSB                                        19
#define SCREG_LAYER0_IN_ROI_ORIGIN_LSB                                         0
#define SCREG_LAYER0_IN_ROI_ORIGIN_BLK                                         0
#define SCREG_LAYER0_IN_ROI_ORIGIN_Count                                       1
#define SCREG_LAYER0_IN_ROI_ORIGIN_FieldMask                          0xFFFFFFFF
#define SCREG_LAYER0_IN_ROI_ORIGIN_ReadMask                           0xFFFFFFFF
#define SCREG_LAYER0_IN_ROI_ORIGIN_WriteMask                          0xFFFFFFFF
#define SCREG_LAYER0_IN_ROI_ORIGIN_ResetValue                         0x00000000

/* Rectangle start point X coordinate. */
#define SCREG_LAYER0_IN_ROI_ORIGIN_X                                        15:0
#define SCREG_LAYER0_IN_ROI_ORIGIN_X_End                                      15
#define SCREG_LAYER0_IN_ROI_ORIGIN_X_Start                                     0
#define SCREG_LAYER0_IN_ROI_ORIGIN_X_Type                                    U16

/* Rectangle start point Y coordinate. */
#define SCREG_LAYER0_IN_ROI_ORIGIN_Y                                       31:16
#define SCREG_LAYER0_IN_ROI_ORIGIN_Y_End                                      31
#define SCREG_LAYER0_IN_ROI_ORIGIN_Y_Start                                    16
#define SCREG_LAYER0_IN_ROI_ORIGIN_Y_Type                                    U16

/* Register scregLayer0InROISize **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Input Region of Interest Size Register.  */

#define scregLayer0InROISizeRegAddrs                                     0x5440E
#define SCREG_LAYER0_IN_ROI_SIZE_Address                                0x151038
#define SCREG_LAYER0_IN_ROI_SIZE_MSB                                          19
#define SCREG_LAYER0_IN_ROI_SIZE_LSB                                           0
#define SCREG_LAYER0_IN_ROI_SIZE_BLK                                           0
#define SCREG_LAYER0_IN_ROI_SIZE_Count                                         1
#define SCREG_LAYER0_IN_ROI_SIZE_FieldMask                            0xFFFFFFFF
#define SCREG_LAYER0_IN_ROI_SIZE_ReadMask                             0xFFFFFFFF
#define SCREG_LAYER0_IN_ROI_SIZE_WriteMask                            0xFFFFFFFF
#define SCREG_LAYER0_IN_ROI_SIZE_ResetValue                           0x00000000

/* Rectangle width.  */
#define SCREG_LAYER0_IN_ROI_SIZE_WIDTH                                      15:0
#define SCREG_LAYER0_IN_ROI_SIZE_WIDTH_End                                    15
#define SCREG_LAYER0_IN_ROI_SIZE_WIDTH_Start                                   0
#define SCREG_LAYER0_IN_ROI_SIZE_WIDTH_Type                                  U16

/* Rectangle height.  */
#define SCREG_LAYER0_IN_ROI_SIZE_HEIGHT                                    31:16
#define SCREG_LAYER0_IN_ROI_SIZE_HEIGHT_End                                   31
#define SCREG_LAYER0_IN_ROI_SIZE_HEIGHT_Start                                 16
#define SCREG_LAYER0_IN_ROI_SIZE_HEIGHT_Type                                 U16

/* Register scregLayer0AlphaBitExtend **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer alpha bit extend for A2R10G10B10.  A8 = alpha0 while A2 == 0.  A8 =  **
** alpha1 while A2 == 1.  A8 = alpha2 while A2 == 2.  A8 = alpha3 while A2 == **
** 3.                                                                         */

#define scregLayer0AlphaBitExtendRegAddrs                                0x5440F
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_Address                           0x15103C
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_MSB                                     19
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_LSB                                      0
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_BLK                                      0
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_Count                                    1
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ResetValue                      0x00000000

/* Alpha0.  */
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA0                                 7:0
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA0_End                               7
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA0_Start                             0
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA0_Type                            U08

/* Alpha1.  */
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA1                                15:8
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA1_End                              15
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA1_Start                             8
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA1_Type                            U08

/* Alpha2.  */
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA2                               23:16
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA2_End                              23
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA2_Start                            16
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA2_Type                            U08

/* Alpha3.  */
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA3                               31:24
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA3_End                              31
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA3_Start                            24
#define SCREG_LAYER0_ALPHA_BIT_EXTEND_ALPHA3_Type                            U08

/* Register scregLayer0UVUpSample **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer upsample phase in horizontal direction.  Layer upsample phase in     **
** vertical direction.                                                        */

#define scregLayer0UVUpSampleRegAddrs                                    0x54410
#define SCREG_LAYER0_UV_UP_SAMPLE_Address                               0x151040
#define SCREG_LAYER0_UV_UP_SAMPLE_MSB                                         19
#define SCREG_LAYER0_UV_UP_SAMPLE_LSB                                          0
#define SCREG_LAYER0_UV_UP_SAMPLE_BLK                                          0
#define SCREG_LAYER0_UV_UP_SAMPLE_Count                                        1
#define SCREG_LAYER0_UV_UP_SAMPLE_FieldMask                           0x00001F1F
#define SCREG_LAYER0_UV_UP_SAMPLE_ReadMask                            0x00001F1F
#define SCREG_LAYER0_UV_UP_SAMPLE_WriteMask                           0x00001F1F
#define SCREG_LAYER0_UV_UP_SAMPLE_ResetValue                          0x00000000

/* Value range 0 ~ 16.  */
#define SCREG_LAYER0_UV_UP_SAMPLE_HPHASE                                     4:0
#define SCREG_LAYER0_UV_UP_SAMPLE_HPHASE_End                                   4
#define SCREG_LAYER0_UV_UP_SAMPLE_HPHASE_Start                                 0
#define SCREG_LAYER0_UV_UP_SAMPLE_HPHASE_Type                                U05

/* Value range 0 ~ 16.  */
#define SCREG_LAYER0_UV_UP_SAMPLE_VPHASE                                    12:8
#define SCREG_LAYER0_UV_UP_SAMPLE_VPHASE_End                                  12
#define SCREG_LAYER0_UV_UP_SAMPLE_VPHASE_Start                                 8
#define SCREG_LAYER0_UV_UP_SAMPLE_VPHASE_Type                                U05

/* Register scregLayer0HScaleFactor **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Horizontal Scale Factor Register.  Horizontal scale factor used to   **
** scale the Layer.  15.16: 15 bits integer, 16 bits fraction.                */

#define scregLayer0HScaleFactorRegAddrs                                  0x54411
#define SCREG_LAYER0_HSCALE_FACTOR_Address                              0x151044
#define SCREG_LAYER0_HSCALE_FACTOR_MSB                                        19
#define SCREG_LAYER0_HSCALE_FACTOR_LSB                                         0
#define SCREG_LAYER0_HSCALE_FACTOR_BLK                                         0
#define SCREG_LAYER0_HSCALE_FACTOR_Count                                       1
#define SCREG_LAYER0_HSCALE_FACTOR_FieldMask                          0x7FFFFFFF
#define SCREG_LAYER0_HSCALE_FACTOR_ReadMask                           0x7FFFFFFF
#define SCREG_LAYER0_HSCALE_FACTOR_WriteMask                          0x7FFFFFFF
#define SCREG_LAYER0_HSCALE_FACTOR_ResetValue                         0x00000000

/* X scale factor.  */
#define SCREG_LAYER0_HSCALE_FACTOR_X                                        30:0
#define SCREG_LAYER0_HSCALE_FACTOR_X_End                                      30
#define SCREG_LAYER0_HSCALE_FACTOR_X_Start                                     0
#define SCREG_LAYER0_HSCALE_FACTOR_X_Type                                    U31

/* Register scregLayer0VScaleFactor **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Vertical Scale Factor Register.  Vertical scale factor used to scale **
** the Layer.  15.16: 15 bits integer, 16 bits fraction.                      */

#define scregLayer0VScaleFactorRegAddrs                                  0x54412
#define SCREG_LAYER0_VSCALE_FACTOR_Address                              0x151048
#define SCREG_LAYER0_VSCALE_FACTOR_MSB                                        19
#define SCREG_LAYER0_VSCALE_FACTOR_LSB                                         0
#define SCREG_LAYER0_VSCALE_FACTOR_BLK                                         0
#define SCREG_LAYER0_VSCALE_FACTOR_Count                                       1
#define SCREG_LAYER0_VSCALE_FACTOR_FieldMask                          0x7FFFFFFF
#define SCREG_LAYER0_VSCALE_FACTOR_ReadMask                           0x7FFFFFFF
#define SCREG_LAYER0_VSCALE_FACTOR_WriteMask                          0x7FFFFFFF
#define SCREG_LAYER0_VSCALE_FACTOR_ResetValue                         0x00000000

/* Y scale factor.  */
#define SCREG_LAYER0_VSCALE_FACTOR_Y                                        30:0
#define SCREG_LAYER0_VSCALE_FACTOR_Y_End                                      30
#define SCREG_LAYER0_VSCALE_FACTOR_Y_Start                                     0
#define SCREG_LAYER0_VSCALE_FACTOR_Y_Type                                    U31

/* Register scregLayer0HScaleCoefData (77 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Horizontal Scale Coefficient data Register.  2.14: 2 bits integer,   **
** 14 bits fraction.                                                          */

#define scregLayer0HScaleCoefDataRegAddrs                                0x54413
#define SCREG_LAYER0_HSCALE_COEF_DATA_Address                           0x15104C
#define SCREG_LAYER0_HSCALE_COEF_DATA_MSB                                     19
#define SCREG_LAYER0_HSCALE_COEF_DATA_LSB                                      7
#define SCREG_LAYER0_HSCALE_COEF_DATA_BLK                                      7
#define SCREG_LAYER0_HSCALE_COEF_DATA_Count                                   77
#define SCREG_LAYER0_HSCALE_COEF_DATA_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER0_HSCALE_COEF_DATA_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER0_HSCALE_COEF_DATA_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER0_HSCALE_COEF_DATA_ResetValue                      0x00000000

/* Coefficients low 16 bits. */
#define SCREG_LAYER0_HSCALE_COEF_DATA_LOW                                   15:0
#define SCREG_LAYER0_HSCALE_COEF_DATA_LOW_End                                 15
#define SCREG_LAYER0_HSCALE_COEF_DATA_LOW_Start                                0
#define SCREG_LAYER0_HSCALE_COEF_DATA_LOW_Type                               U16

/* Coefficients high 16 bits. */
#define SCREG_LAYER0_HSCALE_COEF_DATA_HIGH                                 31:16
#define SCREG_LAYER0_HSCALE_COEF_DATA_HIGH_End                                31
#define SCREG_LAYER0_HSCALE_COEF_DATA_HIGH_Start                              16
#define SCREG_LAYER0_HSCALE_COEF_DATA_HIGH_Type                              U16

/* Register scregLayer0VScaleCoefData (43 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Vertical Scale Coefficient data Register.  2.14: 2 bits integer, 14  **
** bits fraction.                                                             */

#define scregLayer0VScaleCoefDataRegAddrs                                0x54460
#define SCREG_LAYER0_VSCALE_COEF_DATA_Address                           0x151180
#define SCREG_LAYER0_VSCALE_COEF_DATA_MSB                                     19
#define SCREG_LAYER0_VSCALE_COEF_DATA_LSB                                      6
#define SCREG_LAYER0_VSCALE_COEF_DATA_BLK                                      6
#define SCREG_LAYER0_VSCALE_COEF_DATA_Count                                   43
#define SCREG_LAYER0_VSCALE_COEF_DATA_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER0_VSCALE_COEF_DATA_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER0_VSCALE_COEF_DATA_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER0_VSCALE_COEF_DATA_ResetValue                      0x00000000

/* Coefficients low 16 bits. */
#define SCREG_LAYER0_VSCALE_COEF_DATA_LOW                                   15:0
#define SCREG_LAYER0_VSCALE_COEF_DATA_LOW_End                                 15
#define SCREG_LAYER0_VSCALE_COEF_DATA_LOW_Start                                0
#define SCREG_LAYER0_VSCALE_COEF_DATA_LOW_Type                               U16

/* Coefficients high 16 bits. */
#define SCREG_LAYER0_VSCALE_COEF_DATA_HIGH                                 31:16
#define SCREG_LAYER0_VSCALE_COEF_DATA_HIGH_End                                31
#define SCREG_LAYER0_VSCALE_COEF_DATA_HIGH_Start                              16
#define SCREG_LAYER0_VSCALE_COEF_DATA_HIGH_Type                              U16

/* Register scregLayer0ScaleInitialOffsetX **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Scaler Source Offset X Register.  */

#define scregLayer0ScaleInitialOffsetXRegAddrs                           0x5448B
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_Address                     0x15122C
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_MSB                               19
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_LSB                                0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_BLK                                0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_Count                              1
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_FieldMask                 0xFFFFFFFF
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_ReadMask                  0xFFFFFFFF
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_WriteMask                 0xFFFFFFFF
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_ResetValue                0x00008000

/* X offset(initial error). */
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_VALUE                           31:0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_VALUE_End                         31
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_VALUE_Start                        0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_X_VALUE_Type                       U32

/* Register scregLayer0ScaleInitialOffsetY **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Scaler Source Offset Y Register.  */

#define scregLayer0ScaleInitialOffsetYRegAddrs                           0x5448C
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_Address                     0x151230
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_MSB                               19
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_LSB                                0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_BLK                                0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_Count                              1
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_FieldMask                 0xFFFFFFFF
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_ReadMask                  0xFFFFFFFF
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_WriteMask                 0xFFFFFFFF
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_ResetValue                0x00008000

/* Y offset(initial error). */
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_VALUE                           31:0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_VALUE_End                         31
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_VALUE_Start                        0
#define SCREG_LAYER0_SCALE_INITIAL_OFFSET_Y_VALUE_Type                       U32

/* Register scregLayer0Y2rConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Y2R configuration.  */

#define scregLayer0Y2rConfigRegAddrs                                     0x5448D
#define SCREG_LAYER0_Y2R_CONFIG_Address                                 0x151234
#define SCREG_LAYER0_Y2R_CONFIG_MSB                                           19
#define SCREG_LAYER0_Y2R_CONFIG_LSB                                            0
#define SCREG_LAYER0_Y2R_CONFIG_BLK                                            0
#define SCREG_LAYER0_Y2R_CONFIG_Count                                          1
#define SCREG_LAYER0_Y2R_CONFIG_FieldMask                             0x000000F7
#define SCREG_LAYER0_Y2R_CONFIG_ReadMask                              0x000000F7
#define SCREG_LAYER0_Y2R_CONFIG_WriteMask                             0x000000F7
#define SCREG_LAYER0_Y2R_CONFIG_ResetValue                            0x00000000

/* The mode of the CSC.  */
#define SCREG_LAYER0_Y2R_CONFIG_MODE                                         2:0
#define SCREG_LAYER0_Y2R_CONFIG_MODE_End                                       2
#define SCREG_LAYER0_Y2R_CONFIG_MODE_Start                                     0
#define SCREG_LAYER0_Y2R_CONFIG_MODE_Type                                    U03
#define   SCREG_LAYER0_Y2R_CONFIG_MODE_PROGRAMMABLE                          0x0
#define   SCREG_LAYER0_Y2R_CONFIG_MODE_LIMIT_YUV_2_LIMIT_RGB                 0x1
#define   SCREG_LAYER0_Y2R_CONFIG_MODE_LIMIT_YUV_2_FULL_RGB                  0x2
#define   SCREG_LAYER0_Y2R_CONFIG_MODE_FULL_YUV_2_LIMIT_RGB                  0x3
#define   SCREG_LAYER0_Y2R_CONFIG_MODE_FULL_YUV_2_FULL_RGB                   0x4

/* The mode of the Color Gamut.  */
#define SCREG_LAYER0_Y2R_CONFIG_GAMUT                                        7:4
#define SCREG_LAYER0_Y2R_CONFIG_GAMUT_End                                      7
#define SCREG_LAYER0_Y2R_CONFIG_GAMUT_Start                                    4
#define SCREG_LAYER0_Y2R_CONFIG_GAMUT_Type                                   U04
#define   SCREG_LAYER0_Y2R_CONFIG_GAMUT_BT601                                0x0
#define   SCREG_LAYER0_Y2R_CONFIG_GAMUT_BT709                                0x1
#define   SCREG_LAYER0_Y2R_CONFIG_GAMUT_BT2020                               0x2
#define   SCREG_LAYER0_Y2R_CONFIG_GAMUT_P3                                   0x3
#define   SCREG_LAYER0_Y2R_CONFIG_GAMUT_SRGB                                 0x4

/* Register scregLayer0YUVToRGBCoef0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 0 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef0RegAddrs                                 0x5448E
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_Address                           0x151238
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_ResetValue                      0x00001000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF0_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 1 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef1RegAddrs                                 0x5448F
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_Address                           0x15123C
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF1_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 2 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef2RegAddrs                                 0x54490
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_Address                           0x151240
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF2_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef3 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 3 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef3RegAddrs                                 0x54491
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_Address                           0x151244
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF3_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef4 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 4 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef4RegAddrs                                 0x54492
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_Address                           0x151248
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_ResetValue                      0x00001000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF4_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef5 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 5 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef5RegAddrs                                 0x54493
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_Address                           0x15124C
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF5_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef6 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 6 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef6RegAddrs                                 0x54494
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_Address                           0x151250
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF6_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef7 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 7 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef7RegAddrs                                 0x54495
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_Address                           0x151254
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF7_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoef8 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficient 8 Register.  User defined YUV2RGB      **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoef8RegAddrs                                 0x54496
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_Address                           0x151258
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_MSB                                     19
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_LSB                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_BLK                                      0
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_Count                                    1
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_ResetValue                      0x00001000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_VALUE                                 18:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_VALUE_End                               18
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_VALUE_Start                              0
#define SCREG_LAYER0_YUV_TO_RGB_COEF8_VALUE_Type                             U19

/* Register scregLayer0YUVToRGBCoefPreOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset0 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoefPreOffset0RegAddrs                        0x54497
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_Address                0x15125C
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_MSB                          19
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_LSB                           0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_BLK                           0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_Count                         1
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_FieldMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_ReadMask             0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_WriteMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE                      12:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE_End                    12
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE_Start                   0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE_Type                  U13

/* Register scregLayer0YUVToRGBCoefPreOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset1 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoefPreOffset1RegAddrs                        0x54498
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_Address                0x151260
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_MSB                          19
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_LSB                           0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_BLK                           0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_Count                         1
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_FieldMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_ReadMask             0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_WriteMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE                      12:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE_End                    12
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE_Start                   0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE_Type                  U13

/* Register scregLayer0YUVToRGBCoefPreOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset2 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer0YUVToRGBCoefPreOffset2RegAddrs                        0x54499
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_Address                0x151264
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_MSB                          19
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_LSB                           0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_BLK                           0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_Count                         1
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_FieldMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_ReadMask             0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_WriteMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE                      12:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE_End                    12
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE_Start                   0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE_Type                  U13

/* Register scregLayer0YUVToRGBCoefPostOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset0 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer0YUVToRGBCoefPostOffset0RegAddrs                       0x5449A
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_Address               0x151268
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_MSB                         19
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_LSB                          0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_BLK                          0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_Count                        1
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_FieldMask           0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_ReadMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_WriteMask           0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE                     12:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE_End                   12
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE_Start                  0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE_Type                 U13

/* Register scregLayer0YUVToRGBCoefPostOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset1 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer0YUVToRGBCoefPostOffset1RegAddrs                       0x5449B
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_Address               0x15126C
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_MSB                         19
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_LSB                          0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_BLK                          0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_Count                        1
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_FieldMask           0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_ReadMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_WriteMask           0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE                     12:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE_End                   12
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE_Start                  0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE_Type                 U13

/* Register scregLayer0YUVToRGBCoefPostOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset2 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer0YUVToRGBCoefPostOffset2RegAddrs                       0x5449C
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_Address               0x151270
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_MSB                         19
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_LSB                          0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_BLK                          0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_Count                        1
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_FieldMask           0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_ReadMask            0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_WriteMask           0x00001FFF
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE                     12:0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE_End                   12
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE_Start                  0
#define SCREG_LAYER0_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE_Type                 U13

/* Register scregLayer0UVDownSample **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Down Sample Configuration Register.  */

#define scregLayer0UVDownSampleRegAddrs                                  0x5449D
#define SCREG_LAYER0_UV_DOWN_SAMPLE_Address                             0x151274
#define SCREG_LAYER0_UV_DOWN_SAMPLE_MSB                                       19
#define SCREG_LAYER0_UV_DOWN_SAMPLE_LSB                                        0
#define SCREG_LAYER0_UV_DOWN_SAMPLE_BLK                                        0
#define SCREG_LAYER0_UV_DOWN_SAMPLE_Count                                      1
#define SCREG_LAYER0_UV_DOWN_SAMPLE_FieldMask                         0x0000000F
#define SCREG_LAYER0_UV_DOWN_SAMPLE_ReadMask                          0x0000000F
#define SCREG_LAYER0_UV_DOWN_SAMPLE_WriteMask                         0x0000000F
#define SCREG_LAYER0_UV_DOWN_SAMPLE_ResetValue                        0x00000000

/* UV down sample mode in horizontal.  0: Drop second UV in horizontal        **
** direction.  1: Use linear interpolation to do down sample.  2: Use filter  **
** to do down sample.                                                         */
#define SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE                             1:0
#define SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE_End                           1
#define SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE_Start                         0
#define SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE_Type                        U02
#define   SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE_DROP                      0x0
#define   SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE_AVERAGE                   0x1
#define   SCREG_LAYER0_UV_DOWN_SAMPLE_HORI_DS_MODE_FILTER                    0x2

/* UV down sample mode in vertical.  0: Drop second UV in vertical direction. **
**  1: Use linear interpolation to do down sample.  2: Use filter to do down  **
** sample.                                                                    */
#define SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE                            3:2
#define SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE_End                          3
#define SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE_Start                        2
#define SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE_Type                       U02
#define   SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE_DROP                     0x0
#define   SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE_AVERAGE                  0x1
#define   SCREG_LAYER0_UV_DOWN_SAMPLE_VERTI_DS_MODE_FILTER                   0x2

/* Register scregLayer0DthCfg **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Dither Configuration.  */

#define scregLayer0DthCfgRegAddrs                                        0x5449E
#define SCREG_LAYER0_DTH_CFG_Address                                    0x151278
#define SCREG_LAYER0_DTH_CFG_MSB                                              19
#define SCREG_LAYER0_DTH_CFG_LSB                                               0
#define SCREG_LAYER0_DTH_CFG_BLK                                               0
#define SCREG_LAYER0_DTH_CFG_Count                                             1
#define SCREG_LAYER0_DTH_CFG_FieldMask                                0x0000001F
#define SCREG_LAYER0_DTH_CFG_ReadMask                                 0x0000001F
#define SCREG_LAYER0_DTH_CFG_WriteMask                                0x0000001F
#define SCREG_LAYER0_DTH_CFG_ResetValue                               0x00000000

/* Bit rounding mode while dither is disabled.  0: Truncation.  1: Rounding.  */
#define SCREG_LAYER0_DTH_CFG_ROUNDING_MODE                                   0:0
#define SCREG_LAYER0_DTH_CFG_ROUNDING_MODE_End                                 0
#define SCREG_LAYER0_DTH_CFG_ROUNDING_MODE_Start                               0
#define SCREG_LAYER0_DTH_CFG_ROUNDING_MODE_Type                              U01
#define   SCREG_LAYER0_DTH_CFG_ROUNDING_MODE_TRUNCATION                      0x0
#define   SCREG_LAYER0_DTH_CFG_ROUNDING_MODE_ROUNDING                        0x1

/* Frame index enable bit.  0: HW.  1: SW.  */
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_ENABLE                              1:1
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_ENABLE_End                            1
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_ENABLE_Start                          1
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_ENABLE_Type                         U01
#define   SCREG_LAYER0_DTH_CFG_FRAME_INDEX_ENABLE_DISABLED                   0x0
#define   SCREG_LAYER0_DTH_CFG_FRAME_INDEX_ENABLE_ENABLED                    0x1

/* Frame index from HW or SW.  0: HW.  1: SW.  */
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_FROM                                2:2
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_FROM_End                              2
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_FROM_Start                            2
#define SCREG_LAYER0_DTH_CFG_FRAME_INDEX_FROM_Type                           U01
#define   SCREG_LAYER0_DTH_CFG_FRAME_INDEX_FROM_HW                           0x0
#define   SCREG_LAYER0_DTH_CFG_FRAME_INDEX_FROM_SW                           0x1

/* Frame index configured by SW.  */
#define SCREG_LAYER0_DTH_CFG_SW_FRAME_INDEX                                  4:3
#define SCREG_LAYER0_DTH_CFG_SW_FRAME_INDEX_End                                4
#define SCREG_LAYER0_DTH_CFG_SW_FRAME_INDEX_Start                              3
#define SCREG_LAYER0_DTH_CFG_SW_FRAME_INDEX_Type                             U02

/* Register scregLayer0DthRTableLow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for R channel low 32bits.  */

#define scregLayer0DthRTableLowRegAddrs                                  0x5449F
#define SCREG_LAYER0_DTH_RTABLE_LOW_Address                             0x15127C
#define SCREG_LAYER0_DTH_RTABLE_LOW_MSB                                       19
#define SCREG_LAYER0_DTH_RTABLE_LOW_LSB                                        0
#define SCREG_LAYER0_DTH_RTABLE_LOW_BLK                                        0
#define SCREG_LAYER0_DTH_RTABLE_LOW_Count                                      1
#define SCREG_LAYER0_DTH_RTABLE_LOW_FieldMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_RTABLE_LOW_ReadMask                          0xFFFFFFFF
#define SCREG_LAYER0_DTH_RTABLE_LOW_WriteMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_RTABLE_LOW_ResetValue                        0x98BDE510

/* Dither threshold value for x,y=0,0.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X0                                    3:0
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X0_End                                  3
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X0_Start                                0
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X0_Type                               U04

/* Dither threshold value for x,y=1,0.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X1                                    7:4
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X1_End                                  7
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X1_Start                                4
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X1_Type                               U04

/* Dither threshold value for x,y=2,0.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X2                                   11:8
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X2_End                                 11
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X2_Start                                8
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X2_Type                               U04

/* Dither threshold value for x,y=3,0.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X3                                  15:12
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X3_End                                 15
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X3_Start                               12
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y0_X3_Type                               U04

/* Dither threshold value for x,y=0,1.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X0                                  19:16
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X0_End                                 19
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X0_Start                               16
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X0_Type                               U04

/* Dither threshold value for x,y=1,1.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X1                                  23:20
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X1_End                                 23
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X1_Start                               20
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X1_Type                               U04

/* Dither threshold value for x,y=2,1.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X2                                  27:24
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X2_End                                 27
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X2_Start                               24
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X2_Type                               U04

/* Dither threshold value for x,y=3,1.  */
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X3                                  31:28
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X3_End                                 31
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X3_Start                               28
#define SCREG_LAYER0_DTH_RTABLE_LOW_Y1_X3_Type                               U04

/* Register scregLayer0DthRTableHigh **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for R channel high 32bits.  */

#define scregLayer0DthRTableHighRegAddrs                                 0x544A0
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Address                            0x151280
#define SCREG_LAYER0_DTH_RTABLE_HIGH_MSB                                      19
#define SCREG_LAYER0_DTH_RTABLE_HIGH_LSB                                       0
#define SCREG_LAYER0_DTH_RTABLE_HIGH_BLK                                       0
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Count                                     1
#define SCREG_LAYER0_DTH_RTABLE_HIGH_FieldMask                        0xFFFFFFFF
#define SCREG_LAYER0_DTH_RTABLE_HIGH_ReadMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_RTABLE_HIGH_WriteMask                        0xFFFFFFFF
#define SCREG_LAYER0_DTH_RTABLE_HIGH_ResetValue                       0x63CA74F2

/* Dither threshold value for x,y=0,2.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X0                                   3:0
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X0_End                                 3
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X0_Start                               0
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X0_Type                              U04

/* Dither threshold value for x,y=1,2.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X1                                   7:4
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X1_End                                 7
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X1_Start                               4
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X1_Type                              U04

/* Dither threshold value for x,y=2,2.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X2                                  11:8
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X2_End                                11
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X2_Start                               8
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X2_Type                              U04

/* Dither threshold value for x,y=3,2.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X3                                 15:12
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X3_End                                15
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X3_Start                              12
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y2_X3_Type                              U04

/* Dither threshold value for x,y=0,3.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X0                                 19:16
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X0_End                                19
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X0_Start                              16
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X0_Type                              U04

/* Dither threshold value for x,y=1,3.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X1                                 23:20
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X1_End                                23
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X1_Start                              20
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X1_Type                              U04

/* Dither threshold value for x,y=2,3.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X2                                 27:24
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X2_End                                27
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X2_Start                              24
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X2_Type                              U04

/* Dither threshold value for x,y=3,3.  */
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X3                                 31:28
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X3_End                                31
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X3_Start                              28
#define SCREG_LAYER0_DTH_RTABLE_HIGH_Y3_X3_Type                              U04

/* Register scregLayer0DthGTableLow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for G channel low 32bits.  */

#define scregLayer0DthGTableLowRegAddrs                                  0x544A1
#define SCREG_LAYER0_DTH_GTABLE_LOW_Address                             0x151284
#define SCREG_LAYER0_DTH_GTABLE_LOW_MSB                                       19
#define SCREG_LAYER0_DTH_GTABLE_LOW_LSB                                        0
#define SCREG_LAYER0_DTH_GTABLE_LOW_BLK                                        0
#define SCREG_LAYER0_DTH_GTABLE_LOW_Count                                      1
#define SCREG_LAYER0_DTH_GTABLE_LOW_FieldMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_GTABLE_LOW_ReadMask                          0xFFFFFFFF
#define SCREG_LAYER0_DTH_GTABLE_LOW_WriteMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_GTABLE_LOW_ResetValue                        0x3AC9D826

/* Dither threshold value for x,y=0,0.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X0                                    3:0
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X0_End                                  3
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X0_Start                                0
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X0_Type                               U04

/* Dither threshold value for x,y=1,0.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X1                                    7:4
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X1_End                                  7
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X1_Start                                4
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X1_Type                               U04

/* Dither threshold value for x,y=2,0.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X2                                   11:8
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X2_End                                 11
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X2_Start                                8
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X2_Type                               U04

/* Dither threshold value for x,y=3,0.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X3                                  15:12
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X3_End                                 15
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X3_Start                               12
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y0_X3_Type                               U04

/* Dither threshold value for x,y=0,1.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X0                                  19:16
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X0_End                                 19
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X0_Start                               16
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X0_Type                               U04

/* Dither threshold value for x,y=1,1.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X1                                  23:20
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X1_End                                 23
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X1_Start                               20
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X1_Type                               U04

/* Dither threshold value for x,y=2,1.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X2                                  27:24
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X2_End                                 27
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X2_Start                               24
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X2_Type                               U04

/* Dither threshold value for x,y=3,1.  */
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X3                                  31:28
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X3_End                                 31
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X3_Start                               28
#define SCREG_LAYER0_DTH_GTABLE_LOW_Y1_X3_Type                               U04

/* Register scregLayer0DthGTableHigh **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for G channel high 32bits.  */

#define scregLayer0DthGTableHighRegAddrs                                 0x544A2
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Address                            0x151288
#define SCREG_LAYER0_DTH_GTABLE_HIGH_MSB                                      19
#define SCREG_LAYER0_DTH_GTABLE_HIGH_LSB                                       0
#define SCREG_LAYER0_DTH_GTABLE_HIGH_BLK                                       0
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Count                                     1
#define SCREG_LAYER0_DTH_GTABLE_HIGH_FieldMask                        0xFFFFFFFF
#define SCREG_LAYER0_DTH_GTABLE_HIGH_ReadMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_GTABLE_HIGH_WriteMask                        0xFFFFFFFF
#define SCREG_LAYER0_DTH_GTABLE_HIGH_ResetValue                       0x417B5EF0

/* Dither threshold value for x,y=0,2.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X0                                   3:0
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X0_End                                 3
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X0_Start                               0
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X0_Type                              U04

/* Dither threshold value for x,y=1,2.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X1                                   7:4
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X1_End                                 7
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X1_Start                               4
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X1_Type                              U04

/* Dither threshold value for x,y=2,2.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X2                                  11:8
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X2_End                                11
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X2_Start                               8
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X2_Type                              U04

/* Dither threshold value for x,y=3,2.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X3                                 15:12
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X3_End                                15
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X3_Start                              12
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y2_X3_Type                              U04

/* Dither threshold value for x,y=0,3.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X0                                 19:16
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X0_End                                19
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X0_Start                              16
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X0_Type                              U04

/* Dither threshold value for x,y=1,3.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X1                                 23:20
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X1_End                                23
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X1_Start                              20
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X1_Type                              U04

/* Dither threshold value for x,y=2,3.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X2                                 27:24
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X2_End                                27
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X2_Start                              24
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X2_Type                              U04

/* Dither threshold value for x,y=3,3.  */
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X3                                 31:28
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X3_End                                31
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X3_Start                              28
#define SCREG_LAYER0_DTH_GTABLE_HIGH_Y3_X3_Type                              U04

/* Register scregLayer0DthBTableLow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for B channel low 32bits.  */

#define scregLayer0DthBTableLowRegAddrs                                  0x544A3
#define SCREG_LAYER0_DTH_BTABLE_LOW_Address                             0x15128C
#define SCREG_LAYER0_DTH_BTABLE_LOW_MSB                                       19
#define SCREG_LAYER0_DTH_BTABLE_LOW_LSB                                        0
#define SCREG_LAYER0_DTH_BTABLE_LOW_BLK                                        0
#define SCREG_LAYER0_DTH_BTABLE_LOW_Count                                      1
#define SCREG_LAYER0_DTH_BTABLE_LOW_FieldMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_BTABLE_LOW_ReadMask                          0xFFFFFFFF
#define SCREG_LAYER0_DTH_BTABLE_LOW_WriteMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_BTABLE_LOW_ResetValue                        0x70E458D1

/* Dither threshold value for x,y=0,0.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X0                                    3:0
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X0_End                                  3
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X0_Start                                0
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X0_Type                               U04

/* Dither threshold value for x,y=1,0.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X1                                    7:4
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X1_End                                  7
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X1_Start                                4
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X1_Type                               U04

/* Dither threshold value for x,y=2,0.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X2                                   11:8
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X2_End                                 11
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X2_Start                                8
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X2_Type                               U04

/* Dither threshold value for x,y=3,0.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X3                                  15:12
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X3_End                                 15
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X3_Start                               12
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y0_X3_Type                               U04

/* Dither threshold value for x,y=0,1.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X0                                  19:16
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X0_End                                 19
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X0_Start                               16
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X0_Type                               U04

/* Dither threshold value for x,y=1,1.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X1                                  23:20
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X1_End                                 23
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X1_Start                               20
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X1_Type                               U04

/* Dither threshold value for x,y=2,1.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X2                                  27:24
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X2_End                                 27
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X2_Start                               24
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X2_Type                               U04

/* Dither threshold value for x,y=3,1.  */
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X3                                  31:28
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X3_End                                 31
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X3_Start                               28
#define SCREG_LAYER0_DTH_BTABLE_LOW_Y1_X3_Type                               U04

/* Register scregLayer0DthBTableHigh **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for B channel high 32bits.  */

#define scregLayer0DthBTableHighRegAddrs                                 0x544A4
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Address                            0x151290
#define SCREG_LAYER0_DTH_BTABLE_HIGH_MSB                                      19
#define SCREG_LAYER0_DTH_BTABLE_HIGH_LSB                                       0
#define SCREG_LAYER0_DTH_BTABLE_HIGH_BLK                                       0
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Count                                     1
#define SCREG_LAYER0_DTH_BTABLE_HIGH_FieldMask                        0xFFFFFFFF
#define SCREG_LAYER0_DTH_BTABLE_HIGH_ReadMask                         0xFFFFFFFF
#define SCREG_LAYER0_DTH_BTABLE_HIGH_WriteMask                        0xFFFFFFFF
#define SCREG_LAYER0_DTH_BTABLE_HIGH_ResetValue                       0xAF9BC263

/* Dither threshold value for x,y=0,2.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X0                                   3:0
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X0_End                                 3
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X0_Start                               0
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X0_Type                              U04

/* Dither threshold value for x,y=1,2.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X1                                   7:4
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X1_End                                 7
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X1_Start                               4
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X1_Type                              U04

/* Dither threshold value for x,y=2,2.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X2                                  11:8
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X2_End                                11
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X2_Start                               8
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X2_Type                              U04

/* Dither threshold value for x,y=3,2.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X3                                 15:12
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X3_End                                15
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X3_Start                              12
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y2_X3_Type                              U04

/* Dither threshold value for x,y=0,3.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X0                                 19:16
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X0_End                                19
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X0_Start                              16
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X0_Type                              U04

/* Dither threshold value for x,y=1,3.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X1                                 23:20
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X1_End                                23
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X1_Start                              20
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X1_Type                              U04

/* Dither threshold value for x,y=2,3.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X2                                 27:24
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X2_End                                27
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X2_Start                              24
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X2_Type                              U04

/* Dither threshold value for x,y=3,3.  */
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X3                                 31:28
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X3_End                                31
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X3_Start                              28
#define SCREG_LAYER0_DTH_BTABLE_HIGH_Y3_X3_Type                              U04

/* Register scregLayer0WbConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer write back Configuration Register.  */

#define scregLayer0WbConfigRegAddrs                                      0x544A5
#define SCREG_LAYER0_WB_CONFIG_Address                                  0x151294
#define SCREG_LAYER0_WB_CONFIG_MSB                                            19
#define SCREG_LAYER0_WB_CONFIG_LSB                                             0
#define SCREG_LAYER0_WB_CONFIG_BLK                                             0
#define SCREG_LAYER0_WB_CONFIG_Count                                           1
#define SCREG_LAYER0_WB_CONFIG_FieldMask                              0x003807FF
#define SCREG_LAYER0_WB_CONFIG_ReadMask                               0x003807FF
#define SCREG_LAYER0_WB_CONFIG_WriteMask                              0x003807FF
#define SCREG_LAYER0_WB_CONFIG_ResetValue                             0x00000000

/* Layer tile mode  */
#define SCREG_LAYER0_WB_CONFIG_TILE_MODE                                     3:0
#define SCREG_LAYER0_WB_CONFIG_TILE_MODE_End                                   3
#define SCREG_LAYER0_WB_CONFIG_TILE_MODE_Start                                 0
#define SCREG_LAYER0_WB_CONFIG_TILE_MODE_Type                                U04
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_LINEAR                            0x0
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_TILED32X2                         0x1
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_TILED16X4                         0x2
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_TILED32X4                         0x3
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_TILED32X8                         0x4
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_TILED16X8                         0x5
#define   SCREG_LAYER0_WB_CONFIG_TILE_MODE_TILED8X8                          0x6

/* The format of the layer.  */
#define SCREG_LAYER0_WB_CONFIG_FORMAT                                        9:4
#define SCREG_LAYER0_WB_CONFIG_FORMAT_End                                      9
#define SCREG_LAYER0_WB_CONFIG_FORMAT_Start                                    4
#define SCREG_LAYER0_WB_CONFIG_FORMAT_Type                                   U06
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_A8R8G8B8                            0x00
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_X8R8G8B8                            0x01
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_A2R10G10B10                         0x02
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_X2R10G10B10                         0x03
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_R8G8B8                              0x04
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_R5G6B5                              0x05
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_A1R5G5B5                            0x06
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_X1R5G5B5                            0x07
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_A4R4G4B4                            0x08
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_X4R4G4B4                            0x09
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_FP16                                0x0A
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_YUY2                                0x0B
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_UYVY                                0x0C
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_YV12                                0x0D
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_NV12                                0x0E
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_NV16                                0x0F
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_P010                                0x10
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_P210                                0x11
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_YUV420_PACKED_10BIT                 0x12
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_YV12_10BIT_MSB                      0x13
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_YUY2_10BIT                          0x14
#define   SCREG_LAYER0_WB_CONFIG_FORMAT_UYVY_10BIT                          0x15

/* Compress enable.  */
#define SCREG_LAYER0_WB_CONFIG_COMPRESS_ENC                                10:10
#define SCREG_LAYER0_WB_CONFIG_COMPRESS_ENC_End                               10
#define SCREG_LAYER0_WB_CONFIG_COMPRESS_ENC_Start                             10
#define SCREG_LAYER0_WB_CONFIG_COMPRESS_ENC_Type                             U01
#define   SCREG_LAYER0_WB_CONFIG_COMPRESS_ENC_DISABLED                       0x0
#define   SCREG_LAYER0_WB_CONFIG_COMPRESS_ENC_ENABLED                        0x1

/* Swizzle.  */
#define SCREG_LAYER0_WB_CONFIG_SWIZZLE                                     20:19
#define SCREG_LAYER0_WB_CONFIG_SWIZZLE_End                                    20
#define SCREG_LAYER0_WB_CONFIG_SWIZZLE_Start                                  19
#define SCREG_LAYER0_WB_CONFIG_SWIZZLE_Type                                  U02
#define   SCREG_LAYER0_WB_CONFIG_SWIZZLE_ARGB                                0x0
#define   SCREG_LAYER0_WB_CONFIG_SWIZZLE_RGBA                                0x1
#define   SCREG_LAYER0_WB_CONFIG_SWIZZLE_ABGR                                0x2
#define   SCREG_LAYER0_WB_CONFIG_SWIZZLE_BGRA                                0x3

/* UV swizzle.  */
#define SCREG_LAYER0_WB_CONFIG_UV_SWIZZLE                                  21:21
#define SCREG_LAYER0_WB_CONFIG_UV_SWIZZLE_End                                 21
#define SCREG_LAYER0_WB_CONFIG_UV_SWIZZLE_Start                               21
#define SCREG_LAYER0_WB_CONFIG_UV_SWIZZLE_Type                               U01
#define   SCREG_LAYER0_WB_CONFIG_UV_SWIZZLE_UV                               0x0
#define   SCREG_LAYER0_WB_CONFIG_UV_SWIZZLE_VU                               0x1

/* Register scregLayer0WbSize **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Write back image size.  */

#define scregLayer0WbSizeRegAddrs                                        0x544A6
#define SCREG_LAYER0_WB_SIZE_Address                                    0x151298
#define SCREG_LAYER0_WB_SIZE_MSB                                              19
#define SCREG_LAYER0_WB_SIZE_LSB                                               0
#define SCREG_LAYER0_WB_SIZE_BLK                                               0
#define SCREG_LAYER0_WB_SIZE_Count                                             1
#define SCREG_LAYER0_WB_SIZE_FieldMask                                0xFFFFFFFF
#define SCREG_LAYER0_WB_SIZE_ReadMask                                 0xFFFFFFFF
#define SCREG_LAYER0_WB_SIZE_WriteMask                                0xFFFFFFFF
#define SCREG_LAYER0_WB_SIZE_ResetValue                               0x00000000

/* Image width.  */
#define SCREG_LAYER0_WB_SIZE_WIDTH                                          15:0
#define SCREG_LAYER0_WB_SIZE_WIDTH_End                                        15
#define SCREG_LAYER0_WB_SIZE_WIDTH_Start                                       0
#define SCREG_LAYER0_WB_SIZE_WIDTH_Type                                      U16

/* Image height.  */
#define SCREG_LAYER0_WB_SIZE_HEIGHT                                        31:16
#define SCREG_LAYER0_WB_SIZE_HEIGHT_End                                       31
#define SCREG_LAYER0_WB_SIZE_HEIGHT_Start                                     16
#define SCREG_LAYER0_WB_SIZE_HEIGHT_Type                                     U16

/* Register scregLayer0R2yConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer R2Y configuration.  */

#define scregLayer0R2yConfigRegAddrs                                     0x544A7
#define SCREG_LAYER0_R2Y_CONFIG_Address                                 0x15129C
#define SCREG_LAYER0_R2Y_CONFIG_MSB                                           19
#define SCREG_LAYER0_R2Y_CONFIG_LSB                                            0
#define SCREG_LAYER0_R2Y_CONFIG_BLK                                            0
#define SCREG_LAYER0_R2Y_CONFIG_Count                                          1
#define SCREG_LAYER0_R2Y_CONFIG_FieldMask                             0x000000F7
#define SCREG_LAYER0_R2Y_CONFIG_ReadMask                              0x000000F7
#define SCREG_LAYER0_R2Y_CONFIG_WriteMask                             0x000000F7
#define SCREG_LAYER0_R2Y_CONFIG_ResetValue                            0x00000000

/* The mode of the CSC.  */
#define SCREG_LAYER0_R2Y_CONFIG_MODE                                         2:0
#define SCREG_LAYER0_R2Y_CONFIG_MODE_End                                       2
#define SCREG_LAYER0_R2Y_CONFIG_MODE_Start                                     0
#define SCREG_LAYER0_R2Y_CONFIG_MODE_Type                                    U03
#define   SCREG_LAYER0_R2Y_CONFIG_MODE_PROGRAMMABLE                          0x0
#define   SCREG_LAYER0_R2Y_CONFIG_MODE_LIMIT_RGB_2_LIMIT_YUV                 0x1
#define   SCREG_LAYER0_R2Y_CONFIG_MODE_LIMIT_RGB_2_FULL_YUV                  0x2
#define   SCREG_LAYER0_R2Y_CONFIG_MODE_FULL_RGB_2_LIMIT_YUV                  0x3
#define   SCREG_LAYER0_R2Y_CONFIG_MODE_FULL_RGB_2_FULL_YUV                   0x4

/* The mode of the Color Gamut.  */
#define SCREG_LAYER0_R2Y_CONFIG_GAMUT                                        7:4
#define SCREG_LAYER0_R2Y_CONFIG_GAMUT_End                                      7
#define SCREG_LAYER0_R2Y_CONFIG_GAMUT_Start                                    4
#define SCREG_LAYER0_R2Y_CONFIG_GAMUT_Type                                   U04
#define   SCREG_LAYER0_R2Y_CONFIG_GAMUT_BT601                                0x0
#define   SCREG_LAYER0_R2Y_CONFIG_GAMUT_BT709                                0x1
#define   SCREG_LAYER0_R2Y_CONFIG_GAMUT_BT2020                               0x2
#define   SCREG_LAYER0_R2Y_CONFIG_GAMUT_P3                                   0x3
#define   SCREG_LAYER0_R2Y_CONFIG_GAMUT_SRGB                                 0x4

/* Register scregLayer0RGBToYUVCoef0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 0 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef0RegAddrs                                 0x544A8
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_Address                           0x1512A0
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_ResetValue                      0x00000400

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF0_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 1 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef1RegAddrs                                 0x544A9
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_Address                           0x1512A4
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF1_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 2 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef2RegAddrs                                 0x544AA
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_Address                           0x1512A8
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF2_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef3 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 3 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef3RegAddrs                                 0x544AB
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_Address                           0x1512AC
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF3_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef4 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 4 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef4RegAddrs                                 0x544AC
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_Address                           0x1512B0
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_ResetValue                      0x00000400

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF4_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef5 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 5 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef5RegAddrs                                 0x544AD
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_Address                           0x1512B4
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF5_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef6 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 6 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef6RegAddrs                                 0x544AE
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_Address                           0x1512B8
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF6_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef7 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 7 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef7RegAddrs                                 0x544AF
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_Address                           0x1512BC
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF7_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoef8 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 8 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoef8RegAddrs                                 0x544B0
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_Address                           0x1512C0
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_MSB                                     19
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_LSB                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_BLK                                      0
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_Count                                    1
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_FieldMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_ReadMask                        0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_WriteMask                       0x0007FFFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_ResetValue                      0x00000400

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_VALUE                                 18:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_VALUE_End                               18
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_VALUE_Start                              0
#define SCREG_LAYER0_RGB_TO_YUV_COEF8_VALUE_Type                             U19

/* Register scregLayer0RGBToYUVCoefPreOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset0 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoefPreOffset0RegAddrs                        0x544B1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_Address                0x1512C4
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_MSB                          19
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_LSB                           0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_BLK                           0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_Count                         1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_FieldMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_ReadMask             0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_WriteMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE                      12:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE_End                    12
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE_Start                   0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE_Type                  U13

/* Register scregLayer0RGBToYUVCoefPreOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset1 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoefPreOffset1RegAddrs                        0x544B2
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_Address                0x1512C8
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_MSB                          19
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_LSB                           0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_BLK                           0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_Count                         1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_FieldMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_ReadMask             0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_WriteMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE                      12:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE_End                    12
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE_Start                   0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE_Type                  U13

/* Register scregLayer0RGBToYUVCoefPreOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset2 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer0RGBToYUVCoefPreOffset2RegAddrs                        0x544B3
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_Address                0x1512CC
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_MSB                          19
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_LSB                           0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_BLK                           0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_Count                         1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_FieldMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_ReadMask             0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_WriteMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE                      12:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE_End                    12
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE_Start                   0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE_Type                  U13

/* Register scregLayer0RGBToYUVCoefPostOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset0 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer0RGBToYUVCoefPostOffset0RegAddrs                       0x544B4
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_Address               0x1512D0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_MSB                         19
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_LSB                          0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_BLK                          0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_Count                        1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_FieldMask           0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_ReadMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_WriteMask           0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE                     12:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE_End                   12
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE_Start                  0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE_Type                 U13

/* Register scregLayer0RGBToYUVCoefPostOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset1 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer0RGBToYUVCoefPostOffset1RegAddrs                       0x544B5
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_Address               0x1512D4
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_MSB                         19
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_LSB                          0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_BLK                          0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_Count                        1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_FieldMask           0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_ReadMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_WriteMask           0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE                     12:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE_End                   12
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE_Start                  0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE_Type                 U13

/* Register scregLayer0RGBToYUVCoefPostOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset2 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer0RGBToYUVCoefPostOffset2RegAddrs                       0x544B6
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_Address               0x1512D8
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_MSB                         19
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_LSB                          0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_BLK                          0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_Count                        1
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_FieldMask           0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_ReadMask            0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_WriteMask           0x00001FFF
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE                     12:0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE_End                   12
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE_Start                  0
#define SCREG_LAYER0_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE_Type                 U13

/* Register scregLayer0WdmaAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register.  Address  */

#define scregLayer0WdmaAddressRegAddrs                                   0x544B7
#define SCREG_LAYER0_WDMA_ADDRESS_Address                               0x1512DC
#define SCREG_LAYER0_WDMA_ADDRESS_MSB                                         19
#define SCREG_LAYER0_WDMA_ADDRESS_LSB                                          0
#define SCREG_LAYER0_WDMA_ADDRESS_BLK                                          0
#define SCREG_LAYER0_WDMA_ADDRESS_Count                                        1
#define SCREG_LAYER0_WDMA_ADDRESS_FieldMask                           0xFFFFFFFF
#define SCREG_LAYER0_WDMA_ADDRESS_ReadMask                            0xFFFFFFFF
#define SCREG_LAYER0_WDMA_ADDRESS_WriteMask                           0xFFFFFFFF
#define SCREG_LAYER0_WDMA_ADDRESS_ResetValue                          0x00000000

#define SCREG_LAYER0_WDMA_ADDRESS_ADDRESS                                   31:0
#define SCREG_LAYER0_WDMA_ADDRESS_ADDRESS_End                                 31
#define SCREG_LAYER0_WDMA_ADDRESS_ADDRESS_Start                                0
#define SCREG_LAYER0_WDMA_ADDRESS_ADDRESS_Type                               U32

/* Register scregLayer0WdmaHAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for high 4 bits.  High address  */

#define scregLayer0WdmaHAddressRegAddrs                                  0x544B8
#define SCREG_LAYER0_WDMA_HADDRESS_Address                              0x1512E0
#define SCREG_LAYER0_WDMA_HADDRESS_MSB                                        19
#define SCREG_LAYER0_WDMA_HADDRESS_LSB                                         0
#define SCREG_LAYER0_WDMA_HADDRESS_BLK                                         0
#define SCREG_LAYER0_WDMA_HADDRESS_Count                                       1
#define SCREG_LAYER0_WDMA_HADDRESS_FieldMask                          0x000000FF
#define SCREG_LAYER0_WDMA_HADDRESS_ReadMask                           0x000000FF
#define SCREG_LAYER0_WDMA_HADDRESS_WriteMask                          0x000000FF
#define SCREG_LAYER0_WDMA_HADDRESS_ResetValue                         0x00000000

#define SCREG_LAYER0_WDMA_HADDRESS_ADDRESS                                   7:0
#define SCREG_LAYER0_WDMA_HADDRESS_ADDRESS_End                                 7
#define SCREG_LAYER0_WDMA_HADDRESS_ADDRESS_Start                               0
#define SCREG_LAYER0_WDMA_HADDRESS_ADDRESS_Type                              U08

/* Register scregLayer0WdmaUPlaneAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for U plane.  Address  */

#define scregLayer0WdmaUPlaneAddressRegAddrs                             0x544B9
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_Address                        0x1512E4
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_MSB                                  19
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_LSB                                   0
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_BLK                                   0
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_Count                                 1
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_WriteMask                    0xFFFFFFFF
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_ResetValue                   0x00000000

#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_ADDRESS                            31:0
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_ADDRESS_End                          31
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_ADDRESS_Start                         0
#define SCREG_LAYER0_WDMA_UPLANE_ADDRESS_ADDRESS_Type                        U32

/* Register scregLayer0WdmaUPlaneHAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for U plane high 4 bits.  High address  */

#define scregLayer0WdmaUPlaneHAddressRegAddrs                            0x544BA
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_Address                       0x1512E8
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_MSB                                 19
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_LSB                                  0
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_BLK                                  0
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_Count                                1
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_FieldMask                   0x000000FF
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_ReadMask                    0x000000FF
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_WriteMask                   0x000000FF
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_ResetValue                  0x00000000

#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_ADDRESS                            7:0
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_ADDRESS_End                          7
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_ADDRESS_Start                        0
#define SCREG_LAYER0_WDMA_UPLANE_HADDRESS_ADDRESS_Type                       U08

/* Register scregLayer0WdmaVPlaneAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for V plane.  Address  */

#define scregLayer0WdmaVPlaneAddressRegAddrs                             0x544BB
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_Address                        0x1512EC
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_MSB                                  19
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_LSB                                   0
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_BLK                                   0
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_Count                                 1
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_WriteMask                    0xFFFFFFFF
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_ResetValue                   0x00000000

#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_ADDRESS                            31:0
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_ADDRESS_End                          31
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_ADDRESS_Start                         0
#define SCREG_LAYER0_WDMA_VPLANE_ADDRESS_ADDRESS_Type                        U32

/* Register scregLayer0WdmaVPlaneHAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for V plane high 4 bits.  High address  */

#define scregLayer0WdmaVPlaneHAddressRegAddrs                            0x544BC
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_Address                       0x1512F0
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_MSB                                 19
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_LSB                                  0
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_BLK                                  0
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_Count                                1
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_FieldMask                   0x000000FF
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_ReadMask                    0x000000FF
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_WriteMask                   0x000000FF
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_ResetValue                  0x00000000

#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_ADDRESS                            7:0
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_ADDRESS_End                          7
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_ADDRESS_Start                        0
#define SCREG_LAYER0_WDMA_VPLANE_HADDRESS_ADDRESS_Type                       U08

/* Register scregLayer0WdmaStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Stride Register.  */

#define scregLayer0WdmaStrideRegAddrs                                    0x544BD
#define SCREG_LAYER0_WDMA_STRIDE_Address                                0x1512F4
#define SCREG_LAYER0_WDMA_STRIDE_MSB                                          19
#define SCREG_LAYER0_WDMA_STRIDE_LSB                                           0
#define SCREG_LAYER0_WDMA_STRIDE_BLK                                           0
#define SCREG_LAYER0_WDMA_STRIDE_Count                                         1
#define SCREG_LAYER0_WDMA_STRIDE_FieldMask                            0x0003FFFF
#define SCREG_LAYER0_WDMA_STRIDE_ReadMask                             0x0003FFFF
#define SCREG_LAYER0_WDMA_STRIDE_WriteMask                            0x0003FFFF
#define SCREG_LAYER0_WDMA_STRIDE_ResetValue                           0x00000000

/* Destination stride.  */
#define SCREG_LAYER0_WDMA_STRIDE_VALUE                                      17:0
#define SCREG_LAYER0_WDMA_STRIDE_VALUE_End                                    17
#define SCREG_LAYER0_WDMA_STRIDE_VALUE_Start                                   0
#define SCREG_LAYER0_WDMA_STRIDE_VALUE_Type                                  U18

/* Register scregLayer0WdmaUPlaneStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA U Plane Stride Register.  */

#define scregLayer0WdmaUPlaneStrideRegAddrs                              0x544BE
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_Address                         0x1512F8
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_MSB                                   19
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_LSB                                    0
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_BLK                                    0
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_Count                                  1
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_FieldMask                     0x0003FFFF
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_ReadMask                      0x0003FFFF
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_WriteMask                     0x0003FFFF
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_ResetValue                    0x00000000

/* U plane stride.  */
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_VALUE                               17:0
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_VALUE_End                             17
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_VALUE_Start                            0
#define SCREG_LAYER0_WDMA_UPLANE_STRIDE_VALUE_Type                           U18

/* Register scregLayer0WdmaVPlaneStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA V Plane Stride Register.  */

#define scregLayer0WdmaVPlaneStrideRegAddrs                              0x544BF
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_Address                         0x1512FC
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_MSB                                   19
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_LSB                                    0
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_BLK                                    0
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_Count                                  1
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_FieldMask                     0x0003FFFF
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_ReadMask                      0x0003FFFF
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_WriteMask                     0x0003FFFF
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_ResetValue                    0x00000000

/* V plane stride.  */
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_VALUE                               17:0
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_VALUE_End                             17
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_VALUE_Start                            0
#define SCREG_LAYER0_WDMA_VPLANE_STRIDE_VALUE_Type                           U18

/* Register scregLayer0CrcStart **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 CRC start signal.  */

#define scregLayer0CrcStartRegAddrs                                      0x544C0
#define SCREG_LAYER0_CRC_START_Address                                  0x151300
#define SCREG_LAYER0_CRC_START_MSB                                            19
#define SCREG_LAYER0_CRC_START_LSB                                             0
#define SCREG_LAYER0_CRC_START_BLK                                             0
#define SCREG_LAYER0_CRC_START_Count                                           1
#define SCREG_LAYER0_CRC_START_FieldMask                              0x00000001
#define SCREG_LAYER0_CRC_START_ReadMask                               0x00000001
#define SCREG_LAYER0_CRC_START_WriteMask                              0x00000001
#define SCREG_LAYER0_CRC_START_ResetValue                             0x00000000

/* Value.  */
#define SCREG_LAYER0_CRC_START_START                                         0:0
#define SCREG_LAYER0_CRC_START_START_End                                       0
#define SCREG_LAYER0_CRC_START_START_Start                                     0
#define SCREG_LAYER0_CRC_START_START_Type                                    U01

/* Register scregLayer0DfcAlphaCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 walker alpha CRC seed.  */

#define scregLayer0DfcAlphaCrcSeedRegAddrs                               0x544C1
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_Address                         0x151304
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_MSB                                   19
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_LSB                                    0
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_BLK                                    0
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_Count                                  1
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_FieldMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_ReadMask                      0xFFFFFFFF
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_WriteMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_ResetValue                    0x00000000

/* Value.  */
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_VALUE                               31:0
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_VALUE_End                             31
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_VALUE_Start                            0
#define SCREG_LAYER0_DFC_ALPHA_CRC_SEED_VALUE_Type                           U32

/* Register scregLayer0DfcRedCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 walker red CRC seed.  */

#define scregLayer0DfcRedCrcSeedRegAddrs                                 0x544C2
#define SCREG_LAYER0_DFC_RED_CRC_SEED_Address                           0x151308
#define SCREG_LAYER0_DFC_RED_CRC_SEED_MSB                                     19
#define SCREG_LAYER0_DFC_RED_CRC_SEED_LSB                                      0
#define SCREG_LAYER0_DFC_RED_CRC_SEED_BLK                                      0
#define SCREG_LAYER0_DFC_RED_CRC_SEED_Count                                    1
#define SCREG_LAYER0_DFC_RED_CRC_SEED_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER0_DFC_RED_CRC_SEED_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER0_DFC_RED_CRC_SEED_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER0_DFC_RED_CRC_SEED_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_DFC_RED_CRC_SEED_VALUE                                 31:0
#define SCREG_LAYER0_DFC_RED_CRC_SEED_VALUE_End                               31
#define SCREG_LAYER0_DFC_RED_CRC_SEED_VALUE_Start                              0
#define SCREG_LAYER0_DFC_RED_CRC_SEED_VALUE_Type                             U32

/* Register scregLayer0DfcGreenCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 walker green CRC seed.  */

#define scregLayer0DfcGreenCrcSeedRegAddrs                               0x544C3
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_Address                         0x15130C
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_MSB                                   19
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_LSB                                    0
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_BLK                                    0
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_Count                                  1
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_FieldMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_ReadMask                      0xFFFFFFFF
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_WriteMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_ResetValue                    0x00000000

/* Value.  */
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_VALUE                               31:0
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_VALUE_End                             31
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_VALUE_Start                            0
#define SCREG_LAYER0_DFC_GREEN_CRC_SEED_VALUE_Type                           U32

/* Register scregLayer0DfcBlueCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer0 walker blue CRC seed.  */

#define scregLayer0DfcBlueCrcSeedRegAddrs                                0x544C4
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_Address                          0x151310
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_MSB                                    19
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_LSB                                     0
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_BLK                                     0
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_Count                                   1
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_FieldMask                      0xFFFFFFFF
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_ReadMask                       0xFFFFFFFF
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_WriteMask                      0xFFFFFFFF
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_ResetValue                     0x00000000

/* Value.  */
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_VALUE                                31:0
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_VALUE_End                              31
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_VALUE_Start                             0
#define SCREG_LAYER0_DFC_BLUE_CRC_SEED_VALUE_Type                            U32

/* Register scregLayer0DfcAlphaCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer0 Walker Alpha CRC value.  */

#define scregLayer0DfcAlphaCrcValueRegAddrs                              0x544C5
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_Address                        0x151314
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_MSB                                  19
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_LSB                                   0
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_BLK                                   0
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_Count                                 1
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_WriteMask                    0x00000000
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_ResetValue                   0x00000000

/* Layer0 calculated alpha crc value.  */
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_DATA                               31:0
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_DATA_End                             31
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_DATA_Start                            0
#define SCREG_LAYER0_DFC_ALPHA_CRC_VALUE_DATA_Type                           U32

/* Register scregLayer0DfcRedCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer0 Walker Red CRC value.  */

#define scregLayer0DfcRedCrcValueRegAddrs                                0x544C6
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_Address                          0x151318
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_MSB                                    19
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_LSB                                     0
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_BLK                                     0
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_Count                                   1
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_FieldMask                      0xFFFFFFFF
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_ReadMask                       0xFFFFFFFF
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_WriteMask                      0x00000000
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_ResetValue                     0x00000000

/* Layer0 calculated red crc value.  */
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_DATA                                 31:0
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_DATA_End                               31
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_DATA_Start                              0
#define SCREG_LAYER0_DFC_RED_CRC_VALUE_DATA_Type                             U32

/* Register scregLayer0DfcGreenCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer0 Walker Green CRC value.  */

#define scregLayer0DfcGreenCrcValueRegAddrs                              0x544C7
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_Address                        0x15131C
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_MSB                                  19
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_LSB                                   0
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_BLK                                   0
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_Count                                 1
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_WriteMask                    0x00000000
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_ResetValue                   0x00000000

/* Layer0 calculated green crc value.  */
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_DATA                               31:0
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_DATA_End                             31
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_DATA_Start                            0
#define SCREG_LAYER0_DFC_GREEN_CRC_VALUE_DATA_Type                           U32

/* Register scregLayer0DfcBlueCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer0 Walker Blue CRC value.  */

#define scregLayer0DfcBlueCrcValueRegAddrs                               0x544C8
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_Address                         0x151320
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_MSB                                   19
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_LSB                                    0
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_BLK                                    0
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_Count                                  1
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_FieldMask                     0xFFFFFFFF
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_ReadMask                      0xFFFFFFFF
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_WriteMask                     0x00000000
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_ResetValue                    0x00000000

/* Layer0 calculated blue crc value.  */
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_DATA                                31:0
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_DATA_End                              31
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_DATA_Start                             0
#define SCREG_LAYER0_DFC_BLUE_CRC_VALUE_DATA_Type                            U32

/* Register scregLayer0WbSwizzleAlphaCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb alpha CRC seed.  */

#define scregLayer0WbSwizzleAlphaCrcSeedRegAddrs                         0x544C9
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_Address                  0x151324
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_MSB                            19
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_LSB                             0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_BLK                             0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_Count                           1
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_ResetValue             0x00000000

/* Value.  */
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE                        31:0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE_End                      31
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE_Start                     0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE_Type                    U32

/* Register scregLayer0WbSwizzleRedCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb red CRC seed.  */

#define scregLayer0WbSwizzleRedCrcSeedRegAddrs                           0x544CA
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_Address                    0x151328
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_MSB                              19
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_LSB                               0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_BLK                               0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_Count                             1
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_FieldMask                0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_ReadMask                 0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_WriteMask                0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_ResetValue               0x00000000

/* Value.  */
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_VALUE                          31:0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_VALUE_End                        31
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_VALUE_Start                       0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_SEED_VALUE_Type                      U32

/* Register scregLayer0WbSwizzleGreenCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb green CRC seed.  */

#define scregLayer0WbSwizzleGreenCrcSeedRegAddrs                         0x544CB
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_Address                  0x15132C
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_MSB                            19
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_LSB                             0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_BLK                             0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_Count                           1
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_ResetValue             0x00000000

/* Value.  */
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_VALUE                        31:0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_VALUE_End                      31
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_VALUE_Start                     0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_SEED_VALUE_Type                    U32

/* Register scregLayer0WbSwizzleBlueCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb blue CRC seed.  */

#define scregLayer0WbSwizzleBlueCrcSeedRegAddrs                          0x544CC
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_Address                   0x151330
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_MSB                             19
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_LSB                              0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_BLK                              0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_Count                            1
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_FieldMask               0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_ReadMask                0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_WriteMask               0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_ResetValue              0x00000000

/* Value.  */
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_VALUE                         31:0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_VALUE_End                       31
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_VALUE_Start                      0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_SEED_VALUE_Type                     U32

/* Register scregLayer0WbSwizzleAlphaCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Alpha CRC value.  */

#define scregLayer0WbSwizzleAlphaCrcValueRegAddrs                        0x544CD
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_Address                 0x151334
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_MSB                           19
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_LSB                            0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_BLK                            0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_Count                          1
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_WriteMask             0x00000000
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_ResetValue            0x00000000

/* Wb calculated alpha crc value.  */
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA                        31:0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA_End                      31
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA_Start                     0
#define SCREG_LAYER0_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA_Type                    U32

/* Register scregLayer0WbSwizzleRedCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Red CRC value.  */

#define scregLayer0WbSwizzleRedCrcValueRegAddrs                          0x544CE
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_Address                   0x151338
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_MSB                             19
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_LSB                              0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_BLK                              0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_Count                            1
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_FieldMask               0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_ReadMask                0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_WriteMask               0x00000000
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_ResetValue              0x00000000

/* Wb calculated red crc value.  */
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_DATA                          31:0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_DATA_End                        31
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_DATA_Start                       0
#define SCREG_LAYER0_WB_SWIZZLE_RED_CRC_VALUE_DATA_Type                      U32

/* Register scregLayer0WbSwizzleGreenCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Green CRC value.  */

#define scregLayer0WbSwizzleGreenCrcValueRegAddrs                        0x544CF
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_Address                 0x15133C
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_MSB                           19
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_LSB                            0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_BLK                            0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_Count                          1
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_WriteMask             0x00000000
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_ResetValue            0x00000000

/* Wb calculated green crc value.  */
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_DATA                        31:0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_DATA_End                      31
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_DATA_Start                     0
#define SCREG_LAYER0_WB_SWIZZLE_GREEN_CRC_VALUE_DATA_Type                    U32

/* Register scregLayer0WbSwizzleBlueCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Blue CRC value.  */

#define scregLayer0WbSwizzleBlueCrcValueRegAddrs                         0x544D0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_Address                  0x151340
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_MSB                            19
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_LSB                             0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_BLK                             0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_Count                           1
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_WriteMask              0x00000000
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_ResetValue             0x00000000

/* Wb calculated blue crc value.  */
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_DATA                         31:0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_DATA_End                       31
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_DATA_Start                      0
#define SCREG_LAYER0_WB_SWIZZLE_BLUE_CRC_VALUE_DATA_Type                     U32

/* Register scregLayer0RdEarlyWkupNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI RD Bus once early wakeup bursty number.  */

#define scregLayer0RdEarlyWkupNumberRegAddrs                             0x544D1
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_Address                       0x151344
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_MSB                                 19
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_LSB                                  0
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_BLK                                  0
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_Count                                1
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_FieldMask                   0x0000007F
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_ReadMask                    0x0000007F
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_WriteMask                   0x0000007F
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_ResetValue                  0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_VALUE                              6:0
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_VALUE_End                            6
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_VALUE_Start                          0
#define SCREG_LAYER0_RD_EARLY_WKUP_NUMBER_VALUE_Type                         U07

/* Register scregLayer0RdBurstyNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI RD Bus once bursty number.  */

#define scregLayer0RdBurstyNumberRegAddrs                                0x544D2
#define SCREG_LAYER0_RD_BURSTY_NUMBER_Address                           0x151348
#define SCREG_LAYER0_RD_BURSTY_NUMBER_MSB                                     19
#define SCREG_LAYER0_RD_BURSTY_NUMBER_LSB                                      0
#define SCREG_LAYER0_RD_BURSTY_NUMBER_BLK                                      0
#define SCREG_LAYER0_RD_BURSTY_NUMBER_Count                                    1
#define SCREG_LAYER0_RD_BURSTY_NUMBER_FieldMask                       0x0000007F
#define SCREG_LAYER0_RD_BURSTY_NUMBER_ReadMask                        0x0000007F
#define SCREG_LAYER0_RD_BURSTY_NUMBER_WriteMask                       0x0000007F
#define SCREG_LAYER0_RD_BURSTY_NUMBER_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_BURSTY_NUMBER_VALUE                                  6:0
#define SCREG_LAYER0_RD_BURSTY_NUMBER_VALUE_End                                6
#define SCREG_LAYER0_RD_BURSTY_NUMBER_VALUE_Start                              0
#define SCREG_LAYER0_RD_BURSTY_NUMBER_VALUE_Type                             U07

/* Register scregLayer0RdOTNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI RD Bus ot number threshold.   */

#define scregLayer0RdOTNumberRegAddrs                                    0x544D3
#define SCREG_LAYER0_RD_OT_NUMBER_Address                               0x15134C
#define SCREG_LAYER0_RD_OT_NUMBER_MSB                                         19
#define SCREG_LAYER0_RD_OT_NUMBER_LSB                                          0
#define SCREG_LAYER0_RD_OT_NUMBER_BLK                                          0
#define SCREG_LAYER0_RD_OT_NUMBER_Count                                        1
#define SCREG_LAYER0_RD_OT_NUMBER_FieldMask                           0x0000007F
#define SCREG_LAYER0_RD_OT_NUMBER_ReadMask                            0x0000007F
#define SCREG_LAYER0_RD_OT_NUMBER_WriteMask                           0x0000007F
#define SCREG_LAYER0_RD_OT_NUMBER_ResetValue                          0x00000010

/* Value.  */
#define SCREG_LAYER0_RD_OT_NUMBER_VALUE                                      6:0
#define SCREG_LAYER0_RD_OT_NUMBER_VALUE_End                                    6
#define SCREG_LAYER0_RD_OT_NUMBER_VALUE_Start                                  0
#define SCREG_LAYER0_RD_OT_NUMBER_VALUE_Type                                 U07

/* Register scregLayer0WrOTNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI WR Bus ot number threshold.   */

#define scregLayer0WrOTNumberRegAddrs                                    0x544D4
#define SCREG_LAYER0_WR_OT_NUMBER_Address                               0x151350
#define SCREG_LAYER0_WR_OT_NUMBER_MSB                                         19
#define SCREG_LAYER0_WR_OT_NUMBER_LSB                                          0
#define SCREG_LAYER0_WR_OT_NUMBER_BLK                                          0
#define SCREG_LAYER0_WR_OT_NUMBER_Count                                        1
#define SCREG_LAYER0_WR_OT_NUMBER_FieldMask                           0x0000007F
#define SCREG_LAYER0_WR_OT_NUMBER_ReadMask                            0x0000007F
#define SCREG_LAYER0_WR_OT_NUMBER_WriteMask                           0x0000007F
#define SCREG_LAYER0_WR_OT_NUMBER_ResetValue                          0x00000010

/* Value.  */
#define SCREG_LAYER0_WR_OT_NUMBER_VALUE                                      6:0
#define SCREG_LAYER0_WR_OT_NUMBER_VALUE_End                                    6
#define SCREG_LAYER0_WR_OT_NUMBER_VALUE_Start                                  0
#define SCREG_LAYER0_WR_OT_NUMBER_VALUE_Type                                 U07

/* Register scregLayer0RdBwConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer axi read bandwidth Configuration Register.  */

#define scregLayer0RdBwConfigRegAddrs                                    0x544D5
#define SCREG_LAYER0_RD_BW_CONFIG_Address                               0x151354
#define SCREG_LAYER0_RD_BW_CONFIG_MSB                                         19
#define SCREG_LAYER0_RD_BW_CONFIG_LSB                                          0
#define SCREG_LAYER0_RD_BW_CONFIG_BLK                                          0
#define SCREG_LAYER0_RD_BW_CONFIG_Count                                        1
#define SCREG_LAYER0_RD_BW_CONFIG_FieldMask                           0x00000003
#define SCREG_LAYER0_RD_BW_CONFIG_ReadMask                            0x00000003
#define SCREG_LAYER0_RD_BW_CONFIG_WriteMask                           0x00000003
#define SCREG_LAYER0_RD_BW_CONFIG_ResetValue                          0x00000000

/* Bandwidth monitor enable.  */
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE                   0:0
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_End                 0
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Start               0
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Type              U01

/* Bandwidth monitor mode. 0 means output average count, 1 means ooutput peak **
** count.                                                                     */
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE                     1:1
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE_End                   1
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Start                 1
#define SCREG_LAYER0_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Type                U01

/* Register scregLayer0RdBwWindow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor indicates number of blocks. 0 means 1 block, 1means 2    **
** block...f means 16 block.largest value is f.                               */

#define scregLayer0RdBwWindowRegAddrs                                    0x544D6
#define SCREG_LAYER0_RD_BW_WINDOW_Address                               0x151358
#define SCREG_LAYER0_RD_BW_WINDOW_MSB                                         19
#define SCREG_LAYER0_RD_BW_WINDOW_LSB                                          0
#define SCREG_LAYER0_RD_BW_WINDOW_BLK                                          0
#define SCREG_LAYER0_RD_BW_WINDOW_Count                                        1
#define SCREG_LAYER0_RD_BW_WINDOW_FieldMask                           0x0000000F
#define SCREG_LAYER0_RD_BW_WINDOW_ReadMask                            0x0000000F
#define SCREG_LAYER0_RD_BW_WINDOW_WriteMask                           0x0000000F
#define SCREG_LAYER0_RD_BW_WINDOW_ResetValue                          0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_BW_WINDOW_VALUE                                      3:0
#define SCREG_LAYER0_RD_BW_WINDOW_VALUE_End                                    3
#define SCREG_LAYER0_RD_BW_WINDOW_VALUE_Start                                  0
#define SCREG_LAYER0_RD_BW_WINDOW_VALUE_Type                                 U04

/* Register scregLayer0RdBwStep **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor step config.  step must up-align to each format require  **
** block count unit.  linear_norot:  RGB/YUV422:1xN, YUV420:2xN.  tile_norot: **
** RGB32BPP/RGB16BPP:4xN, FP16:2xN, NV12/P010:16xN.  linear_rot:              **
** RGB32BPP:8xN, RGB16BPP:16xN, YV12:64xN, NV12/NV16:32xN, P010/P210:16xN,    **
** PACK10:24xN.  tile_rot: RGB32BPP:16xN, RGB16BPP:32xN, NV12:32xN,           **
** P010:16xN.                                                                 */

#define scregLayer0RdBwStepRegAddrs                                      0x544D7
#define SCREG_LAYER0_RD_BW_STEP_Address                                 0x15135C
#define SCREG_LAYER0_RD_BW_STEP_MSB                                           19
#define SCREG_LAYER0_RD_BW_STEP_LSB                                            0
#define SCREG_LAYER0_RD_BW_STEP_BLK                                            0
#define SCREG_LAYER0_RD_BW_STEP_Count                                          1
#define SCREG_LAYER0_RD_BW_STEP_FieldMask                             0x0000FFFF
#define SCREG_LAYER0_RD_BW_STEP_ReadMask                              0x0000FFFF
#define SCREG_LAYER0_RD_BW_STEP_WriteMask                             0x0000FFFF
#define SCREG_LAYER0_RD_BW_STEP_ResetValue                            0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_BW_STEP_VALUE                                       15:0
#define SCREG_LAYER0_RD_BW_STEP_VALUE_End                                     15
#define SCREG_LAYER0_RD_BW_STEP_VALUE_Start                                    0
#define SCREG_LAYER0_RD_BW_STEP_VALUE_Type                                   U16

/* Register scregLayer0RdBwAverageCounter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor average counter out.  */

#define scregLayer0RdBwAverageCounterRegAddrs                            0x544D8
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_Address                      0x151360
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_MSB                                19
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_LSB                                 0
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_BLK                                 0
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_Count                               1
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_FieldMask                  0xFFFFFFFF
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_ReadMask                   0xFFFFFFFF
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_WriteMask                  0x00000000
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_ResetValue                 0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_VALUE                            31:0
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_VALUE_End                          31
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_VALUE_Start                         0
#define SCREG_LAYER0_RD_BW_AVERAGE_COUNTER_VALUE_Type                        U32

/* Register scregLayer0RdBwPeak0Counter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor peak0 counter out.  */

#define scregLayer0RdBwPeak0CounterRegAddrs                              0x544D9
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_Address                        0x151364
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_MSB                                  19
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_LSB                                   0
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_BLK                                   0
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_Count                                 1
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_WriteMask                    0x00000000
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_ResetValue                   0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_VALUE                              31:0
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_VALUE_End                            31
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_VALUE_Start                           0
#define SCREG_LAYER0_RD_BW_PEAK0_COUNTER_VALUE_Type                          U32

/* Register scregLayer0RdBwPeak1Counter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor peak1 counter out.  */

#define scregLayer0RdBwPeak1CounterRegAddrs                              0x544DA
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_Address                        0x151368
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_MSB                                  19
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_LSB                                   0
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_BLK                                   0
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_Count                                 1
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_WriteMask                    0x00000000
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_ResetValue                   0x00000000

/* Value.  */
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_VALUE                              31:0
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_VALUE_End                            31
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_VALUE_Start                           0
#define SCREG_LAYER0_RD_BW_PEAK1_COUNTER_VALUE_Type                          U32

/* Register scregLayer0WrBwConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer axi write bandwidth Configuration Register.  */

#define scregLayer0WrBwConfigRegAddrs                                    0x544DB
#define SCREG_LAYER0_WR_BW_CONFIG_Address                               0x15136C
#define SCREG_LAYER0_WR_BW_CONFIG_MSB                                         19
#define SCREG_LAYER0_WR_BW_CONFIG_LSB                                          0
#define SCREG_LAYER0_WR_BW_CONFIG_BLK                                          0
#define SCREG_LAYER0_WR_BW_CONFIG_Count                                        1
#define SCREG_LAYER0_WR_BW_CONFIG_FieldMask                           0x00000003
#define SCREG_LAYER0_WR_BW_CONFIG_ReadMask                            0x00000003
#define SCREG_LAYER0_WR_BW_CONFIG_WriteMask                           0x00000003
#define SCREG_LAYER0_WR_BW_CONFIG_ResetValue                          0x00000000

/* Bandwidth monitor enable.  */
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE                   0:0
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_End                 0
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Start               0
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Type              U01

/* Bandwidth monitor mode. 0 means output average count, 1 means ooutput peak **
** count.                                                                     */
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE                     1:1
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE_End                   1
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Start                 1
#define SCREG_LAYER0_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Type                U01

/* Register scregLayer0WrBwWindow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor indicates number of blocks. 0 means 1 block, 1means 2    **
** block...f means 16 block.largest value is f.                               */

#define scregLayer0WrBwWindowRegAddrs                                    0x544DC
#define SCREG_LAYER0_WR_BW_WINDOW_Address                               0x151370
#define SCREG_LAYER0_WR_BW_WINDOW_MSB                                         19
#define SCREG_LAYER0_WR_BW_WINDOW_LSB                                          0
#define SCREG_LAYER0_WR_BW_WINDOW_BLK                                          0
#define SCREG_LAYER0_WR_BW_WINDOW_Count                                        1
#define SCREG_LAYER0_WR_BW_WINDOW_FieldMask                           0x0000000F
#define SCREG_LAYER0_WR_BW_WINDOW_ReadMask                            0x0000000F
#define SCREG_LAYER0_WR_BW_WINDOW_WriteMask                           0x0000000F
#define SCREG_LAYER0_WR_BW_WINDOW_ResetValue                          0x00000000

/* Value.  */
#define SCREG_LAYER0_WR_BW_WINDOW_VALUE                                      3:0
#define SCREG_LAYER0_WR_BW_WINDOW_VALUE_End                                    3
#define SCREG_LAYER0_WR_BW_WINDOW_VALUE_Start                                  0
#define SCREG_LAYER0_WR_BW_WINDOW_VALUE_Type                                 U04

/* Register scregLayer0WrBwStep **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor step config.  step must up-align to each format require  **
** block count unit.  linear_norot:  RGB/YUV422:1xN, YUV420:2xN.  tile_norot: **
** RGB32BPP/RGB16BPP:4xN, FP16:2xN, NV12/P010:16xN.  linear_rot:              **
** RGB32BPP:8xN, RGB16BPP:16xN, YV12:64xN, NV12/NV16:32xN, P010/P210:16xN,    **
** PACK10:24xN.  tile_rot: RGB32BPP:16xN, RGB16BPP:32xN, NV12:32xN,           **
** P010:16xN.                                                                 */

#define scregLayer0WrBwStepRegAddrs                                      0x544DD
#define SCREG_LAYER0_WR_BW_STEP_Address                                 0x151374
#define SCREG_LAYER0_WR_BW_STEP_MSB                                           19
#define SCREG_LAYER0_WR_BW_STEP_LSB                                            0
#define SCREG_LAYER0_WR_BW_STEP_BLK                                            0
#define SCREG_LAYER0_WR_BW_STEP_Count                                          1
#define SCREG_LAYER0_WR_BW_STEP_FieldMask                             0x0000FFFF
#define SCREG_LAYER0_WR_BW_STEP_ReadMask                              0x0000FFFF
#define SCREG_LAYER0_WR_BW_STEP_WriteMask                             0x0000FFFF
#define SCREG_LAYER0_WR_BW_STEP_ResetValue                            0x00000000

/* Value.  */
#define SCREG_LAYER0_WR_BW_STEP_VALUE                                       15:0
#define SCREG_LAYER0_WR_BW_STEP_VALUE_End                                     15
#define SCREG_LAYER0_WR_BW_STEP_VALUE_Start                                    0
#define SCREG_LAYER0_WR_BW_STEP_VALUE_Type                                   U16

/* Register scregLayer0WrBwAverageCounter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor average counter out.  */

#define scregLayer0WrBwAverageCounterRegAddrs                            0x544DE
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_Address                      0x151378
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_MSB                                19
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_LSB                                 0
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_BLK                                 0
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_Count                               1
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_FieldMask                  0xFFFFFFFF
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_ReadMask                   0xFFFFFFFF
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_WriteMask                  0x00000000
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_ResetValue                 0x00000000

/* Value.  */
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_VALUE                            31:0
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_VALUE_End                          31
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_VALUE_Start                         0
#define SCREG_LAYER0_WR_BW_AVERAGE_COUNTER_VALUE_Type                        U32

/* Register scregLayer0WrBwPeak0Counter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor peak0 counter out.  */

#define scregLayer0WrBwPeak0CounterRegAddrs                              0x544DF
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_Address                        0x15137C
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_MSB                                  19
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_LSB                                   0
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_BLK                                   0
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_Count                                 1
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_WriteMask                    0x00000000
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_ResetValue                   0x00000000

/* Value.  */
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_VALUE                              31:0
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_VALUE_End                            31
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_VALUE_Start                           0
#define SCREG_LAYER0_WR_BW_PEAK0_COUNTER_VALUE_Type                          U32

/* Register scregLayer0BldWbLoc **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer blend wdma dst location, used for picture split.  */

#define scregLayer0BldWbLocRegAddrs                                      0x544E0
#define SCREG_LAYER0_BLD_WB_LOC_Address                                 0x151380
#define SCREG_LAYER0_BLD_WB_LOC_MSB                                           19
#define SCREG_LAYER0_BLD_WB_LOC_LSB                                            0
#define SCREG_LAYER0_BLD_WB_LOC_BLK                                            0
#define SCREG_LAYER0_BLD_WB_LOC_Count                                          1
#define SCREG_LAYER0_BLD_WB_LOC_FieldMask                             0xFFFFFFFF
#define SCREG_LAYER0_BLD_WB_LOC_ReadMask                              0xFFFFFFFF
#define SCREG_LAYER0_BLD_WB_LOC_WriteMask                             0xFFFFFFFF
#define SCREG_LAYER0_BLD_WB_LOC_ResetValue                            0x00000000

/* Locx.  */
#define SCREG_LAYER0_BLD_WB_LOC_LOCX                                        15:0
#define SCREG_LAYER0_BLD_WB_LOC_LOCX_End                                      15
#define SCREG_LAYER0_BLD_WB_LOC_LOCX_Start                                     0
#define SCREG_LAYER0_BLD_WB_LOC_LOCX_Type                                    U16

/* Locy.  */
#define SCREG_LAYER0_BLD_WB_LOC_LOCY                                       31:16
#define SCREG_LAYER0_BLD_WB_LOC_LOCY_End                                      31
#define SCREG_LAYER0_BLD_WB_LOC_LOCY_Start                                    16
#define SCREG_LAYER0_BLD_WB_LOC_LOCY_Type                                    U16

/* Register scregLayer0DecPVRICControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC control register.  */

#define scregLayer0DecPVRICControlRegAddrs                               0x544E1
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_Address                          0x151384
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_MSB                                    19
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_LSB                                     0
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_BLK                                     0
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_Count                                   1
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_FieldMask                      0xFFFFFFC0
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ReadMask                       0x7FFFFFC0
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_WriteMask                      0xFFFFFFC0
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ResetValue                     0x00000080

/* Disable Clock Gating.  */
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING                          6:6
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_End                        6
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_Start                      6
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_Type                     U01

/* Clock gating takes modules Idle signal into consideration.  */
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE                     7:7
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE_End                   7
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Start                 7
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Type                U01

/* Enable debug registeres.  */
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_DEBUG                          8:8
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_DEBUG_End                        8
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_DEBUG_Start                      8
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_DEBUG_Type                     U01

/* Enable interrupt.  */
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_INTR                           9:9
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_INTR_End                         9
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_INTR_Start                       9
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_ENABLE_INTR_Type                      U01

/* Reserved registeres.  */
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_RESERVED                            30:10
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_RESERVED_End                           30
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_RESERVED_Start                         10
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_RESERVED_Type                         U21

/* Soft Reset. This bit is volatile.  */
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_SOFT_RESET                          31:31
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_SOFT_RESET_End                         31
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_SOFT_RESET_Start                       31
#define SCREG_LAYER0_DEC_PVRIC_CONTROL_SOFT_RESET_Type                       U01

/* Register scregLayer0DecPVRICInvalidationControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC invalidation control register.  */

#define scregLayer0DecPVRICInvalidationControlRegAddrs                   0x544E2
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_Address             0x151388
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_MSB                       19
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_LSB                        0
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_BLK                        0
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_Count                      1
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_FieldMask         0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_ReadMask          0xFFFF7FFE
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_WriteMask         0x00FF81FB
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_ResetValue        0x01000600

/* Trigger the invalidation. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY                   0:0
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY_End                 0
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Start               0
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Type              U01

/* Invalidate all the Header cache lines. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL           1:1
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_End         1
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Start       1
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Type      U01

/* Invalidation has been done. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE      2:2
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_End    2
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Start  2
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Type U01

/* Invalidation by Requester. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER  8:3
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_End 8
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Start 3
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Type U06

/* Invalidation by Requester have been done. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE 14:9
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_End 14
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Start 9
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Type U06

/* Cause invalidation of all headers associated with the.                     **
** fbc_fbdc_inval_context when there is an fbc to fbdc invalidation cycle.    */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE    15:15
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_End   15
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Start 15
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Type U01

/* Invalidation by Context. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT  23:16
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_End 23
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Start 16
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Type U08

/* Invalidation by Context Done. */
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE 31:24
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_End 31
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Start 24
#define SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Type U08

/* Register scregLayer0DecPVRICFilterConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC filter config register.  */

#define scregLayer0DecPVRICFilterConfigRegAddrs                          0x544E3
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_Address                    0x15138C
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_MSB                              19
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_LSB                               0
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_BLK                               0
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_Count                             1
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_FieldMask                0x00003FFD
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_ReadMask                 0x000000FD
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_WriteMask                0x00003F01
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_ResetValue               0x00000001

/* Enable Filter. */
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_ENABLE                          0:0
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_ENABLE_End                        0
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_ENABLE_Start                      0
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_ENABLE_Type                     U01

/* Filter Status. */
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_STATUS                          7:2
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_STATUS_End                        7
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_STATUS_Start                      2
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_STATUS_Type                     U06

/* Clear the Filter Status. */
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_CLEAR                          13:8
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_CLEAR_End                        13
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_CLEAR_Start                       8
#define SCREG_LAYER0_DEC_PVRIC_FILTER_CONFIG_CLEAR_Type                      U06

/* Register scregLayer0DecPVRICSignatureConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC signature config register.  */

#define scregLayer0DecPVRICSignatureConfigRegAddrs                       0x544E4
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_Address                 0x151390
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_MSB                           19
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_LSB                            0
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_BLK                            0
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_Count                          1
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_FieldMask             0x00003FFD
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_ReadMask              0x000000FD
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_WriteMask             0x00003F01
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_ResetValue            0x00000000

/* Enable Signature. */
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE                       0:0
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE_End                     0
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE_Start                   0
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE_Type                  U01

/* Signature Status. */
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_STATUS                       7:2
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_STATUS_End                     7
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_STATUS_Start                   2
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_STATUS_Type                  U06

/* Clear the Signature Status. */
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR                       13:8
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR_End                     13
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR_Start                    8
#define SCREG_LAYER0_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR_Type                   U06

/* Register scregLayer0DecPVRICClearValueHighReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer0DecPVRICClearValueHighReqt0RegAddrs                   0x544E5
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Address           0x151394
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_MSB                     19
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_LSB                      0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_BLK                      0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Count                    1
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_FieldMask       0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ReadMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_WriteMask       0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ResetValue      0x00000000

/* Value.  */
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE                 31:0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_End               31
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Start              0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Type             U32

/* Register scregLayer0DecPVRICClearValueLowReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer0DecPVRICClearValueLowReqt0RegAddrs                    0x544E6
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_Address            0x151398
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_MSB                      19
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_LSB                       0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_BLK                       0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_Count                     1
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_FieldMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_ReadMask         0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_WriteMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_ResetValue       0x00000000

/* Value.  */
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE                  31:0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_End                31
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Start               0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Type              U32

/* Register scregLayer0DecPVRICClearValueHighReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer0DecPVRICClearValueHighReqt1RegAddrs                   0x544E7
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Address           0x15139C
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_MSB                     19
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_LSB                      0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_BLK                      0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Count                    1
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_FieldMask       0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ReadMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_WriteMask       0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ResetValue      0x00000000

/* Value.  */
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE                 31:0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_End               31
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Start              0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Type             U32

/* Register scregLayer0DecPVRICClearValueLowReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer0DecPVRICClearValueLowReqt1RegAddrs                    0x544E8
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_Address            0x1513A0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_MSB                      19
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_LSB                       0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_BLK                       0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_Count                     1
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_FieldMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_ReadMask         0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_WriteMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_ResetValue       0x00000000

/* Value.  */
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE                  31:0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_End                31
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Start               0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Type              U32

/* Register scregLayer0DecPVRICClearValueHighReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer0DecPVRICClearValueHighReqt2RegAddrs                   0x544E9
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Address           0x1513A4
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_MSB                     19
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_LSB                      0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_BLK                      0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Count                    1
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_FieldMask       0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ReadMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_WriteMask       0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ResetValue      0x00000000

/* Value.  */
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE                 31:0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_End               31
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Start              0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Type             U32

/* Register scregLayer0DecPVRICClearValueLowReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer0DecPVRICClearValueLowReqt2RegAddrs                    0x544EA
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_Address            0x1513A8
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_MSB                      19
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_LSB                       0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_BLK                       0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_Count                     1
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_FieldMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_ReadMask         0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_WriteMask        0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_ResetValue       0x00000000

/* Value.  */
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE                  31:0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_End                31
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Start               0
#define SCREG_LAYER0_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Type              U32

/* Register scregLayer0DecPVRICRequesterControlReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control0.  */

#define scregLayer0DecPVRICRequesterControlReqt0RegAddrs                 0x544EB
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_Address          0x1513AC
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_MSB                    19
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_LSB                     0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_BLK                     0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_Count                   1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FieldMask      0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ReadMask       0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_WriteMask      0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY          0:0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_End        0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Start      0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT                7:1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_End              7
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Start            1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Type           U07
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_U8          0x00
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_RGB565      0x05
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A8          0x28
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE                  9:8
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_End                9
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Start              8
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Type             U02
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_RESERVED       0x0
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_8X8       0x1
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_16X4      0x2
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE             13:10
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_End            13
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Start          10
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Type          U04
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BRGA        0xD

/* Register scregLayer0DecPVRICRequesterControlReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control1.  */

#define scregLayer0DecPVRICRequesterControlReqt1RegAddrs                 0x544EC
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_Address          0x1513B0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_MSB                    19
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_LSB                     0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_BLK                     0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_Count                   1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FieldMask      0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ReadMask       0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_WriteMask      0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY          0:0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_End        0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Start      0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT                7:1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_End              7
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Start            1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Type           U07
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_U8          0x00
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_RGB565      0x05
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A8          0x28
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE                  9:8
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_End                9
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Start              8
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Type             U02
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_RESERVED       0x0
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_8X8       0x1
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_16X4      0x2
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE             13:10
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_End            13
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Start          10
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Type          U04
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BRGA        0xD

/* Register scregLayer0DecPVRICRequesterControlReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control2.  */

#define scregLayer0DecPVRICRequesterControlReqt2RegAddrs                 0x544ED
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_Address          0x1513B4
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_MSB                    19
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_LSB                     0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_BLK                     0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_Count                   1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FieldMask      0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ReadMask       0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_WriteMask      0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY          0:0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_End        0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Start      0
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT                7:1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_End              7
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Start            1
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Type           U07
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_U8          0x00
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_RGB565      0x05
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A8          0x28
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE                  9:8
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_End                9
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Start              8
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Type             U02
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_RESERVED       0x0
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_8X8       0x1
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_16X4      0x2
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE             13:10
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_End            13
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Start          10
#define SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Type          U04
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BRGA        0xD

/* Register scregLayer0DecPVRICBaseAddrHighReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* High 32 bits of Base Address.  */

#define scregLayer0DecPVRICBaseAddrHighReqtRegAddrs                      0x544EE
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_Address              0x1513B8
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_MSB                        19
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_LSB                         2
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_BLK                         2
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_Count                       3
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_FieldMask          0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ReadMask           0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_WriteMask          0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ResetValue         0x00000000

/* High 32 bit address.  */
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS                  31:0
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_End                31
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Start               0
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Type              U32

/* Register scregLayer0DecPVRICBaseAddrLowReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Low 32 bits of Base Address.  */

#define scregLayer0DecPVRICBaseAddrLowReqtRegAddrs                       0x544F1
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_Address               0x1513C4
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_MSB                         19
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_LSB                          2
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_BLK                          2
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_Count                        3
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_FieldMask           0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_ReadMask            0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_WriteMask           0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_ResetValue          0x00000000

/* Low 32 bit address.  */
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS                   31:0
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_End                 31
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Start                0
#define SCREG_LAYER0_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Type               U32

/* Register scregLayer0DecPVRICConstColorConfig0Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration0 (for Non-video pixels).  */

#define scregLayer0DecPVRICConstColorConfig0ReqtRegAddrs                 0x544F4
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address         0x1513D0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_MSB                   19
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_LSB                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_BLK                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_Count                  1
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_ResetValue    0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE               31:0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_End             31
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Start            0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Type           U32

/* Register scregLayer0DecPVRICConstColorConfig1Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration1 (for Non-video pixels).  */

#define scregLayer0DecPVRICConstColorConfig1ReqtRegAddrs                 0x544F5
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_Address         0x1513D4
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_MSB                   19
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_LSB                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_BLK                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_Count                  1
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_ResetValue    0x01000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE               31:0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_End             31
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Start            0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Type           U32

/* Register scregLayer0DecPVRICConstColorConfig2Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration2 (for video pixels).  */

#define scregLayer0DecPVRICConstColorConfig2ReqtRegAddrs                 0x544F6
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_Address         0x1513D8
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_MSB                   19
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_LSB                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_BLK                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_Count                  1
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_ResetValue    0x00000000

/* ValueY. */
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y            25:16
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_End           25
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Start         16
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV             9:0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_End           9
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Start         0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Type        U10

/* Register scregLayer0DecPVRICConstColorConfig3Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration3 (for video pixels).  */

#define scregLayer0DecPVRICConstColorConfig3ReqtRegAddrs                 0x544F7
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_Address         0x1513DC
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_MSB                   19
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_LSB                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_BLK                    0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_Count                  1
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_ResetValue    0x03FF0000

/* ValueY. */
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y            25:16
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_End           25
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Start         16
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV             9:0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_End           9
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Start         0
#define SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Type        U10

/* Register scregLayer0DecPVRICThreshold0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 0.  */

#define scregLayer0DecPVRICThreshold0RegAddrs                            0x544F8
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_Address                       0x1513E0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_MSB                                 19
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_LSB                                  0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_BLK                                  0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_Count                                1
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_FieldMask                   0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_ReadMask                    0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_WriteMask                   0x00003FFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_ResetValue                  0x00000123

/* ThresholdArgb10. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10                   5:0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_End                 5
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Start               0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Type              U06

/* ThresholdAlpha. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA                   13:6
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_End                 13
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Start                6
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Type               U08

/* Register scregLayer0DecPVRICThreshold1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 1.  */

#define scregLayer0DecPVRICThreshold1RegAddrs                            0x544F9
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_Address                       0x1513E4
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_MSB                                 19
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_LSB                                  0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_BLK                                  0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_Count                                1
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_FieldMask                   0x3FFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_ReadMask                    0x3FFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_WriteMask                   0x3FFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_ResetValue                  0x0C45550B

/* ThresholdYuv8. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8                     5:0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_End                   5
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Start                 0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Type                U06

/* Yuv10P10. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P10                         17:6
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P10_End                       17
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P10_Start                      6
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P10_Type                     U12

/* Yuv10P16. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P16                        29:18
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P16_End                       29
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P16_Start                     18
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD1_YUV10_P16_Type                     U12

/* Register scregLayer0DecPVRICThreshold2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 2.  */

#define scregLayer0DecPVRICThreshold2RegAddrs                            0x544FA
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_Address                       0x1513E8
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_MSB                                 19
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_LSB                                  0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_BLK                                  0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_Count                                1
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_FieldMask                   0x00FFFFFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_ReadMask                    0x00FFFFFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_WriteMask                   0x00FFFFFF
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_ResetValue                  0x00429329

/* ColorDiff8. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8                       11:0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8_End                     11
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8_Type                   U12

/* ColorDiff10. */
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10                     23:12
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10_End                    23
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10_Start                  12
#define SCREG_LAYER0_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10_Type                  U12

/* Register scregLayer0DecPVRICCoreIdP **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id P.  */

#define scregLayer0DecPVRICCoreIdPRegAddrs                               0x544FB
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_Address                        0x1513EC
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_MSB                                  19
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_LSB                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_BLK                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_Count                                 1
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_WriteMask                    0x00000000
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_ResetValue                   0x00000847

/* ProductCode. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE                       15:0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE_End                     15
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_RESERVED                          31:16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_RESERVED_End                         31
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_RESERVED_Start                       16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_P_RESERVED_Type                       U16

/* Register scregLayer0DecPVRICCoreIdB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id B.  */

#define scregLayer0DecPVRICCoreIdBRegAddrs                               0x544FC
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_Address                        0x1513F0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_MSB                                  19
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_LSB                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_BLK                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_Count                                 1
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_WriteMask                    0x00000000
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_ResetValue                   0x00000000

/* BranchCode. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_BRANCH_CODE                        15:0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_BRANCH_CODE_End                      15
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_BRANCH_CODE_Start                     0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_BRANCH_CODE_Type                    U16

/* Reserved. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_RESERVED                          31:16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_RESERVED_End                         31
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_RESERVED_Start                       16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_B_RESERVED_Type                       U16

/* Register scregLayer0DecPVRICCoreIdV **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id V.  */

#define scregLayer0DecPVRICCoreIdVRegAddrs                               0x544FD
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_Address                        0x1513F4
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_MSB                                  19
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_LSB                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_BLK                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_Count                                 1
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_WriteMask                    0x00000000
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_ResetValue                   0x00000000

/* VersionCode. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_VERSION_CODE                       15:0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_VERSION_CODE_End                     15
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_VERSION_CODE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_VERSION_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_RESERVED                          31:16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_RESERVED_End                         31
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_RESERVED_Start                       16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_V_RESERVED_Type                       U16

/* Register scregLayer0DecPVRICCoreIdN **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id N.  */

#define scregLayer0DecPVRICCoreIdNRegAddrs                               0x544FE
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_Address                        0x1513F8
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_MSB                                  19
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_LSB                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_BLK                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_Count                                 1
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_WriteMask                    0x00000000
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_ResetValue                   0x00000000

/* ScalableCoreCode. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE                 15:0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_End               15
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Start              0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_RESERVED                          31:16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_RESERVED_End                         31
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_RESERVED_Start                       16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_N_RESERVED_Type                       U16

/* Register scregLayer0DecPVRICCoreIdC **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id C.  */

#define scregLayer0DecPVRICCoreIdCRegAddrs                               0x544FF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_Address                        0x1513FC
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_MSB                                  19
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_LSB                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_BLK                                   0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_Count                                 1
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_WriteMask                    0x00000000
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_ResetValue                   0x00000000

/* ConfigurationCode. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE                 15:0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_End               15
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Start              0
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_RESERVED                          31:16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_RESERVED_End                         31
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_RESERVED_Start                       16
#define SCREG_LAYER0_DEC_PVRIC_CORE_ID_C_RESERVED_Type                       U16

/* Register scregLayer0DecPVRICCoreIpChangelist **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Ip Changelist.  */

#define scregLayer0DecPVRICCoreIpChangelistRegAddrs                      0x54500
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_Address               0x151400
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_MSB                         19
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_LSB                          0
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_BLK                          0
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_Count                        1
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_FieldMask           0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_ReadMask            0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_WriteMask           0x00000000
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_ResetValue          0x00000000

/* ChangelistCode. */
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE           31:0
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_End         31
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Start        0
#define SCREG_LAYER0_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Type       U32

/* Register scregLayer0DecPVRICDebugStatus **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Debug Status.  */

#define scregLayer0DecPVRICDebugStatusRegAddrs                           0x54501
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_Address                     0x151404
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_MSB                               19
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_LSB                                0
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_BLK                                0
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_Count                              1
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_FieldMask                 0x00000003
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_ReadMask                  0x00000003
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_WriteMask                 0x00000002
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_ResetValue                0x00000001

/* Idle. */
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_IDLE                             0:0
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_IDLE_End                           0
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_IDLE_Start                         0
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_IDLE_Type                        U01

/* BusError. */
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR                        1:1
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR_End                      1
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR_Start                    1
#define SCREG_LAYER0_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR_Type                   U01

/* Register scregLayer0DecPVRICCounterMasterAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AW. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterMasterAWRegAddrs                       0x54502
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_Address                0x151408
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_MSB                          19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_LSB                           0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_BLK                           0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_Count                         1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_FieldMask            0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_ReadMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_WriteMask            0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_VALUE                      31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_VALUE_End                    31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_VALUE_Start                   0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AW_VALUE_Type                  U32

/* Register scregLayer0DecPVRICCounterMasterW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master W. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterMasterWRegAddrs                        0x54503
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_Address                 0x15140C
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_MSB                           19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_LSB                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_BLK                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_Count                          1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_VALUE                       31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_VALUE_End                     31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_VALUE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_W_VALUE_Type                   U32

/* Register scregLayer0DecPVRICCounterMasterB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master B. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterMasterBRegAddrs                        0x54504
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_Address                 0x151410
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_MSB                           19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_LSB                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_BLK                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_Count                          1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_VALUE                       31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_VALUE_End                     31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_VALUE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_B_VALUE_Type                   U32

/* Register scregLayer0DecPVRICCounterMasterAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AR. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterMasterARRegAddrs                       0x54505
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_Address                0x151414
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_MSB                          19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_LSB                           0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_BLK                           0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_Count                         1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_FieldMask            0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_ReadMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_WriteMask            0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_VALUE                      31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_VALUE_End                    31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_VALUE_Start                   0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_AR_VALUE_Type                  U32

/* Register scregLayer0DecPVRICCounterMasterR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master R. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterMasterRRegAddrs                        0x54506
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_Address                 0x151418
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_MSB                           19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_LSB                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_BLK                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_Count                          1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_VALUE                       31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_VALUE_End                     31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_VALUE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_MASTER_R_VALUE_Type                   U32

/* Register scregLayer0DecPVRICCounterSlaveAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AW. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterSlaveAWRegAddrs                        0x54507
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_Address                 0x15141C
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_MSB                           19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_LSB                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_BLK                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_Count                          1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE                       31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE_End                     31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE_Type                   U32

/* Register scregLayer0DecPVRICCounterSlaveW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave W. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterSlaveWRegAddrs                         0x54508
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_Address                  0x151420
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_MSB                            19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_LSB                             0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_BLK                             0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_Count                           1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_VALUE                        31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_VALUE_End                      31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_VALUE_Start                     0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_W_VALUE_Type                    U32

/* Register scregLayer0DecPVRICCounterSlaveB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave B. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterSlaveBRegAddrs                         0x54509
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_Address                  0x151424
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_MSB                            19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_LSB                             0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_BLK                             0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_Count                           1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_VALUE                        31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_VALUE_End                      31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_VALUE_Start                     0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_B_VALUE_Type                    U32

/* Register scregLayer0DecPVRICCounterSlaveAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AR. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterSlaveARRegAddrs                        0x5450A
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_Address                 0x151428
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_MSB                           19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_LSB                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_BLK                            0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_Count                          1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE                       31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE_End                     31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE_Start                    0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE_Type                   U32

/* Register scregLayer0DecPVRICCounterSlaveR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave R. Toggled only when enableDebug asserted. */

#define scregLayer0DecPVRICCounterSlaveRRegAddrs                         0x5450B
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_Address                  0x15142C
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_MSB                            19
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_LSB                             0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_BLK                             0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_Count                           1
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_VALUE                        31:0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_VALUE_End                      31
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_VALUE_Start                     0
#define SCREG_LAYER0_DEC_PVRIC_COUNTER_SLAVE_R_VALUE_Type                    U32

/* Register scregLayer0EncPVRICControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC control register.  */

#define scregLayer0EncPVRICControlRegAddrs                               0x5450C
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_Address                          0x151430
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_MSB                                    19
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_LSB                                     0
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_BLK                                     0
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_Count                                   1
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_FieldMask                      0xFFFFFFC0
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ReadMask                       0x7FFFFFC0
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_WriteMask                      0xFFFFFFC0
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ResetValue                     0x00000080

/* Disable Clock Gating.  */
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING                          6:6
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_End                        6
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_Start                      6
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_Type                     U01

/* Clock gating takes modules Idle signal into consideration.  */
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE                     7:7
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE_End                   7
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Start                 7
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Type                U01

/* Enable debug registeres.  */
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_DEBUG                          8:8
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_DEBUG_End                        8
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_DEBUG_Start                      8
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_DEBUG_Type                     U01

/* Enable interrupt.  */
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_INTR                           9:9
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_INTR_End                         9
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_INTR_Start                       9
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_ENABLE_INTR_Type                      U01

/* Reserved registeres.  */
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_RESERVED                            30:10
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_RESERVED_End                           30
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_RESERVED_Start                         10
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_RESERVED_Type                         U21

/* Soft Reset. This bit is volatile.  */
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_SOFT_RESET                          31:31
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_SOFT_RESET_End                         31
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_SOFT_RESET_Start                       31
#define SCREG_LAYER0_ENC_PVRIC_CONTROL_SOFT_RESET_Type                       U01

/* Register scregLayer0EncPVRICInvalidationControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC invalidation control register.  */

#define scregLayer0EncPVRICInvalidationControlRegAddrs                   0x5450D
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_Address             0x151434
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_MSB                       19
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_LSB                        0
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_BLK                        0
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_Count                      1
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_FieldMask         0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_ReadMask          0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_WriteMask         0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_ResetValue        0x01000600

/* Trigger the invalidation. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY                   0:0
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY_End                 0
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Start               0
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Type              U01

/* Invalidate all the Header cache lines. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL           1:1
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_End         1
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Start       1
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Type      U01

/* Invalidation has been done. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE      2:2
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_End    2
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Start  2
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Type U01

/* Invalidation by Requester. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER  8:3
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_End 8
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Start 3
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Type U06

/* Invalidation by Requester have been done. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE 14:9
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_End 14
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Start 9
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Type U06

/* Cause invalidation of all headers associated with the.                     **
** fbc_fbdc_inval_context when there is an fbc to fbdc invalidation cycle.    */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE    15:15
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_End   15
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Start 15
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Type U01

/* Invalidation by Context. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT  23:16
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_End 23
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Start 16
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Type U08

/* Invalidation by Context Done. */
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE 31:24
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_End 31
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Start 24
#define SCREG_LAYER0_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Type U08

/* Register scregLayer0EncPVRICFilterConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC filter config register.  */

#define scregLayer0EncPVRICFilterConfigRegAddrs                          0x5450E
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_Address                    0x151438
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_MSB                              19
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_LSB                               0
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_BLK                               0
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_Count                             1
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_FieldMask                0x00003FFD
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_ReadMask                 0x00003FFD
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_WriteMask                0x00003FFD
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_ResetValue               0x00000001

/* Enable Filter. */
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_ENABLE                          0:0
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_ENABLE_End                        0
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_ENABLE_Start                      0
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_ENABLE_Type                     U01

/* Filter Status. */
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_STATUS                          7:2
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_STATUS_End                        7
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_STATUS_Start                      2
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_STATUS_Type                     U06

/* Clear the Filter Status. */
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_CLEAR                          13:8
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_CLEAR_End                        13
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_CLEAR_Start                       8
#define SCREG_LAYER0_ENC_PVRIC_FILTER_CONFIG_CLEAR_Type                      U06

/* Register scregLayer0EncPVRICSignatureConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC signature config register.  */

#define scregLayer0EncPVRICSignatureConfigRegAddrs                       0x5450F
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_Address                 0x15143C
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_MSB                           19
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_LSB                            0
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_BLK                            0
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_Count                          1
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_FieldMask             0x00003FFD
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_ReadMask              0x00003FFD
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_WriteMask             0x00003FFD
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_ResetValue            0x00000000

/* Enable Signature. */
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE                       0:0
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE_End                     0
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE_Start                   0
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE_Type                  U01

/* Signature Status. */
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_STATUS                       7:2
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_STATUS_End                     7
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_STATUS_Start                   2
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_STATUS_Type                  U06

/* Clear the Signature Status. */
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR                       13:8
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR_End                     13
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR_Start                    8
#define SCREG_LAYER0_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR_Type                   U06

/* Register scregLayer0EncPVRICClearValueHighReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer0EncPVRICClearValueHighReqt0RegAddrs                   0x54510
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Address           0x151440
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_MSB                     19
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_LSB                      0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_BLK                      0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Count                    1
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_FieldMask       0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ReadMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_WriteMask       0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE                 31:0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_End               31
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Start              0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Type             U32

/* Register scregLayer0EncPVRICClearValueLowReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer0EncPVRICClearValueLowReqt0RegAddrs                    0x54511
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_Address            0x151444
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_MSB                      19
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_LSB                       0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_BLK                       0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_Count                     1
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_FieldMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_ReadMask         0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_WriteMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE                  31:0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_End                31
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Start               0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Type              U32

/* Register scregLayer0EncPVRICClearValueHighReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer0EncPVRICClearValueHighReqt1RegAddrs                   0x54512
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Address           0x151448
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_MSB                     19
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_LSB                      0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_BLK                      0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Count                    1
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_FieldMask       0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ReadMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_WriteMask       0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE                 31:0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_End               31
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Start              0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Type             U32

/* Register scregLayer0EncPVRICClearValueLowReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer0EncPVRICClearValueLowReqt1RegAddrs                    0x54513
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_Address            0x15144C
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_MSB                      19
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_LSB                       0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_BLK                       0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_Count                     1
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_FieldMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_ReadMask         0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_WriteMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE                  31:0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_End                31
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Start               0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Type              U32

/* Register scregLayer0EncPVRICClearValueHighReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer0EncPVRICClearValueHighReqt2RegAddrs                   0x54514
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Address           0x151450
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_MSB                     19
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_LSB                      0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_BLK                      0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Count                    1
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_FieldMask       0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ReadMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_WriteMask       0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE                 31:0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_End               31
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Start              0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Type             U32

/* Register scregLayer0EncPVRICClearValueLowReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer0EncPVRICClearValueLowReqt2RegAddrs                    0x54515
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_Address            0x151454
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_MSB                      19
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_LSB                       0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_BLK                       0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_Count                     1
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_FieldMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_ReadMask         0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_WriteMask        0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE                  31:0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_End                31
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Start               0
#define SCREG_LAYER0_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Type              U32

/* Register scregLayer0EncPVRICRequesterControlReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control0.  */

#define scregLayer0EncPVRICRequesterControlReqt0RegAddrs                 0x54516
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_Address          0x151458
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_MSB                    19
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_LSB                     0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_BLK                     0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_Count                   1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FieldMask      0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ReadMask       0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_WriteMask      0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY          0:0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_End        0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Start      0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT                7:1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_End              7
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Start            1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Type           U07
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_U8          0x00
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_RGB565      0x05
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A8          0x28
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE                  9:8
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_End                9
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Start              8
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Type             U02
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_RESERVED       0x0
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_8X8       0x1
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_16X4      0x2
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE             13:10
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_End            13
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Start          10
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Type          U04
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BRGA        0xD

/* Register scregLayer0EncPVRICRequesterControlReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control1.  */

#define scregLayer0EncPVRICRequesterControlReqt1RegAddrs                 0x54517
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_Address          0x15145C
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_MSB                    19
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_LSB                     0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_BLK                     0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_Count                   1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FieldMask      0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ReadMask       0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_WriteMask      0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY          0:0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_End        0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Start      0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT                7:1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_End              7
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Start            1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Type           U07
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_U8          0x00
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_RGB565      0x05
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A8          0x28
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE                  9:8
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_End                9
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Start              8
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Type             U02
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_RESERVED       0x0
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_8X8       0x1
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_16X4      0x2
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE             13:10
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_End            13
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Start          10
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Type          U04
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BRGA        0xD

/* Register scregLayer0EncPVRICRequesterControlReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control2.  */

#define scregLayer0EncPVRICRequesterControlReqt2RegAddrs                 0x54518
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_Address          0x151460
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_MSB                    19
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_LSB                     0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_BLK                     0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_Count                   1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FieldMask      0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ReadMask       0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_WriteMask      0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY          0:0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_End        0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Start      0
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT                7:1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_End              7
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Start            1
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Type           U07
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_U8          0x00
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_RGB565      0x05
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A8          0x28
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE                  9:8
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_End                9
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Start              8
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Type             U02
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_RESERVED       0x0
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_8X8       0x1
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_16X4      0x2
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE             13:10
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_End            13
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Start          10
#define SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Type          U04
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER0_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BRGA        0xD

/* Register scregLayer0EncPVRICBaseAddrHighReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* High 32 bits of Base Address.  */

#define scregLayer0EncPVRICBaseAddrHighReqtRegAddrs                      0x54519
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_Address              0x151464
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_MSB                        19
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_LSB                         2
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_BLK                         2
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_Count                       3
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_FieldMask          0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ReadMask           0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_WriteMask          0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ResetValue         0x00000000

/* High 32 bit address.  */
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS                  31:0
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_End                31
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Start               0
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Type              U32

/* Register scregLayer0EncPVRICBaseAddrLowReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Low 32 bits of Base Address.  */

#define scregLayer0EncPVRICBaseAddrLowReqtRegAddrs                       0x5451C
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_Address               0x151470
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_MSB                         19
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_LSB                          2
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_BLK                          2
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_Count                        3
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_FieldMask           0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_ReadMask            0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_WriteMask           0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_ResetValue          0x00000000

/* Low 32 bit address.  */
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS                   31:0
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_End                 31
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Start                0
#define SCREG_LAYER0_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Type               U32

/* Register scregLayer0EncPVRICConstColorConfig0Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration0 (for Non-video pixels).  */

#define scregLayer0EncPVRICConstColorConfig0ReqtRegAddrs                 0x5451F
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address         0x15147C
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_MSB                   19
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_LSB                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_BLK                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_Count                  1
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_ResetValue    0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE               31:0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_End             31
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Start            0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Type           U32

/* Register scregLayer0EncPVRICConstColorConfig1Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration1 (for Non-video pixels).  */

#define scregLayer0EncPVRICConstColorConfig1ReqtRegAddrs                 0x54520
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_Address         0x151480
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_MSB                   19
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_LSB                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_BLK                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_Count                  1
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_ResetValue    0x01000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE               31:0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_End             31
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Start            0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Type           U32

/* Register scregLayer0EncPVRICConstColorConfig2Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration2 (for video pixels).  */

#define scregLayer0EncPVRICConstColorConfig2ReqtRegAddrs                 0x54521
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_Address         0x151484
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_MSB                   19
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_LSB                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_BLK                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_Count                  1
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_ResetValue    0x00000000

/* ValueY. */
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y            25:16
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_End           25
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Start         16
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV             9:0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_End           9
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Start         0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Type        U10

/* Register scregLayer0EncPVRICConstColorConfig3Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration3 (for video pixels).  */

#define scregLayer0EncPVRICConstColorConfig3ReqtRegAddrs                 0x54522
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_Address         0x151488
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_MSB                   19
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_LSB                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_BLK                    0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_Count                  1
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_ResetValue    0x03FF0000

/* ValueY. */
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y            25:16
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_End           25
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Start         16
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV             9:0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_End           9
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Start         0
#define SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Type        U10

/* Register scregLayer0EncPVRICThreshold0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 0.  */

#define scregLayer0EncPVRICThreshold0RegAddrs                            0x54523
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_Address                       0x15148C
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_MSB                                 19
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_LSB                                  0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_BLK                                  0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_Count                                1
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_FieldMask                   0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_ReadMask                    0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_WriteMask                   0x00003FFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_ResetValue                  0x00000123

/* ThresholdArgb10. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10                   5:0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_End                 5
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Start               0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Type              U06

/* ThresholdAlpha. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA                   13:6
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_End                 13
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Start                6
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Type               U08

/* Register scregLayer0EncPVRICThreshold1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 1.  */

#define scregLayer0EncPVRICThreshold1RegAddrs                            0x54524
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_Address                       0x151490
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_MSB                                 19
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_LSB                                  0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_BLK                                  0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_Count                                1
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_FieldMask                   0x3FFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_ReadMask                    0x3FFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_WriteMask                   0x3FFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_ResetValue                  0x0C45550B

/* ThresholdYuv8. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8                     5:0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_End                   5
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Start                 0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Type                U06

/* Yuv10P10. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P10                         17:6
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P10_End                       17
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P10_Start                      6
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P10_Type                     U12

/* Yuv10P16. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P16                        29:18
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P16_End                       29
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P16_Start                     18
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD1_YUV10_P16_Type                     U12

/* Register scregLayer0EncPVRICThreshold2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 2.  */

#define scregLayer0EncPVRICThreshold2RegAddrs                            0x54525
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_Address                       0x151494
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_MSB                                 19
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_LSB                                  0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_BLK                                  0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_Count                                1
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_FieldMask                   0x00FFFFFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_ReadMask                    0x00FFFFFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_WriteMask                   0x00FFFFFF
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_ResetValue                  0x00429329

/* ColorDiff8. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8                       11:0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8_End                     11
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8_Type                   U12

/* ColorDiff10. */
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10                     23:12
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10_End                    23
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10_Start                  12
#define SCREG_LAYER0_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10_Type                  U12

/* Register scregLayer0EncPVRICCoreIdP **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id P.  */

#define scregLayer0EncPVRICCoreIdPRegAddrs                               0x54526
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_Address                        0x151498
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_MSB                                  19
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_LSB                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_BLK                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_Count                                 1
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_WriteMask                    0x00000000
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_ResetValue                   0x00000847

/* ProductCode. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE                       15:0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE_End                     15
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_RESERVED                          31:16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_RESERVED_End                         31
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_RESERVED_Start                       16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_P_RESERVED_Type                       U16

/* Register scregLayer0EncPVRICCoreIdB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id B.  */

#define scregLayer0EncPVRICCoreIdBRegAddrs                               0x54527
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_Address                        0x15149C
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_MSB                                  19
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_LSB                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_BLK                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_Count                                 1
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_WriteMask                    0x00000000
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_ResetValue                   0x00000000

/* BranchCode. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_BRANCH_CODE                        15:0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_BRANCH_CODE_End                      15
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_BRANCH_CODE_Start                     0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_BRANCH_CODE_Type                    U16

/* Reserved. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_RESERVED                          31:16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_RESERVED_End                         31
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_RESERVED_Start                       16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_B_RESERVED_Type                       U16

/* Register scregLayer0EncPVRICCoreIdV **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id V.  */

#define scregLayer0EncPVRICCoreIdVRegAddrs                               0x54528
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_Address                        0x1514A0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_MSB                                  19
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_LSB                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_BLK                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_Count                                 1
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_WriteMask                    0x00000000
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_ResetValue                   0x00000000

/* VersionCode. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_VERSION_CODE                       15:0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_VERSION_CODE_End                     15
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_VERSION_CODE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_VERSION_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_RESERVED                          31:16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_RESERVED_End                         31
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_RESERVED_Start                       16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_V_RESERVED_Type                       U16

/* Register scregLayer0EncPVRICCoreIdN **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id N.  */

#define scregLayer0EncPVRICCoreIdNRegAddrs                               0x54529
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_Address                        0x1514A4
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_MSB                                  19
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_LSB                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_BLK                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_Count                                 1
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_WriteMask                    0x00000000
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_ResetValue                   0x00000000

/* ScalableCoreCode. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE                 15:0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_End               15
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Start              0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_RESERVED                          31:16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_RESERVED_End                         31
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_RESERVED_Start                       16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_N_RESERVED_Type                       U16

/* Register scregLayer0EncPVRICCoreIdC **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id C.  */

#define scregLayer0EncPVRICCoreIdCRegAddrs                               0x5452A
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_Address                        0x1514A8
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_MSB                                  19
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_LSB                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_BLK                                   0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_Count                                 1
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_WriteMask                    0x00000000
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_ResetValue                   0x00000000

/* ConfigurationCode. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE                 15:0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_End               15
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Start              0
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_RESERVED                          31:16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_RESERVED_End                         31
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_RESERVED_Start                       16
#define SCREG_LAYER0_ENC_PVRIC_CORE_ID_C_RESERVED_Type                       U16

/* Register scregLayer0EncPVRICCoreIpChangelist **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Ip Changelist.  */

#define scregLayer0EncPVRICCoreIpChangelistRegAddrs                      0x5452B
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_Address               0x1514AC
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_MSB                         19
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_LSB                          0
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_BLK                          0
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_Count                        1
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_FieldMask           0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_ReadMask            0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_WriteMask           0x00000000
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_ResetValue          0x00000000

/* ChangelistCode. */
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE           31:0
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_End         31
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Start        0
#define SCREG_LAYER0_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Type       U32

/* Register scregLayer0EncPVRICDebugStatus **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Debug Status.  */

#define scregLayer0EncPVRICDebugStatusRegAddrs                           0x5452C
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_Address                     0x1514B0
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_MSB                               19
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_LSB                                0
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_BLK                                0
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_Count                              1
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_FieldMask                 0x00000003
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_ReadMask                  0x00000003
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_WriteMask                 0x00000002
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_ResetValue                0x00000001

/* Idle. */
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_IDLE                             0:0
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_IDLE_End                           0
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_IDLE_Start                         0
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_IDLE_Type                        U01

/* BusError. */
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR                        1:1
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR_End                      1
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR_Start                    1
#define SCREG_LAYER0_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR_Type                   U01

/* Register scregLayer0EncPVRICCounterMasterAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AW. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterMasterAWRegAddrs                       0x5452D
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_Address                0x1514B4
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_MSB                          19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_LSB                           0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_BLK                           0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_Count                         1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_FieldMask            0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_ReadMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_WriteMask            0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_VALUE                      31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_VALUE_End                    31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_VALUE_Start                   0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AW_VALUE_Type                  U32

/* Register scregLayer0EncPVRICCounterMasterW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master W. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterMasterWRegAddrs                        0x5452E
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_Address                 0x1514B8
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_MSB                           19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_LSB                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_BLK                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_Count                          1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_VALUE                       31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_VALUE_End                     31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_VALUE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_W_VALUE_Type                   U32

/* Register scregLayer0EncPVRICCounterMasterB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master B. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterMasterBRegAddrs                        0x5452F
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_Address                 0x1514BC
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_MSB                           19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_LSB                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_BLK                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_Count                          1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_VALUE                       31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_VALUE_End                     31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_VALUE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_B_VALUE_Type                   U32

/* Register scregLayer0EncPVRICCounterMasterAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AR. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterMasterARRegAddrs                       0x54530
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_Address                0x1514C0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_MSB                          19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_LSB                           0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_BLK                           0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_Count                         1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_FieldMask            0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_ReadMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_WriteMask            0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_VALUE                      31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_VALUE_End                    31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_VALUE_Start                   0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_AR_VALUE_Type                  U32

/* Register scregLayer0EncPVRICCounterMasterR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master R. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterMasterRRegAddrs                        0x54531
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_Address                 0x1514C4
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_MSB                           19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_LSB                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_BLK                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_Count                          1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_VALUE                       31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_VALUE_End                     31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_VALUE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_MASTER_R_VALUE_Type                   U32

/* Register scregLayer0EncPVRICCounterSlaveAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AW. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterSlaveAWRegAddrs                        0x54532
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_Address                 0x1514C8
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_MSB                           19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_LSB                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_BLK                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_Count                          1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE                       31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE_End                     31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE_Type                   U32

/* Register scregLayer0EncPVRICCounterSlaveW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave W. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterSlaveWRegAddrs                         0x54533
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_Address                  0x1514CC
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_MSB                            19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_LSB                             0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_BLK                             0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_Count                           1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_VALUE                        31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_VALUE_End                      31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_VALUE_Start                     0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_W_VALUE_Type                    U32

/* Register scregLayer0EncPVRICCounterSlaveB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave B. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterSlaveBRegAddrs                         0x54534
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_Address                  0x1514D0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_MSB                            19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_LSB                             0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_BLK                             0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_Count                           1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_VALUE                        31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_VALUE_End                      31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_VALUE_Start                     0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_B_VALUE_Type                    U32

/* Register scregLayer0EncPVRICCounterSlaveAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AR. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterSlaveARRegAddrs                        0x54535
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_Address                 0x1514D4
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_MSB                           19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_LSB                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_BLK                            0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_Count                          1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_FieldMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_ReadMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_WriteMask             0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE                       31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE_End                     31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE_Start                    0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE_Type                   U32

/* Register scregLayer0EncPVRICCounterSlaveR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave R. Toggled only when enableDebug asserted. */

#define scregLayer0EncPVRICCounterSlaveRRegAddrs                         0x54536
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_Address                  0x1514D8
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_MSB                            19
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_LSB                             0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_BLK                             0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_Count                           1
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_FieldMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_ReadMask               0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_WriteMask              0xFFFFFFFF
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_VALUE                        31:0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_VALUE_End                      31
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_VALUE_Start                     0
#define SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_VALUE_Type                    U32

/* Register scregLayer0End **
** ~~~~~~~~~~~~~~~~~~~~~~~ */

/* End address of this module.  Reserved.  */

#define scregLayer0EndRegAddrs                                           0x54537
#define SCREG_LAYER0_END_Address                                        0x1514DC
#define SCREG_LAYER0_END_MSB                                                  19
#define SCREG_LAYER0_END_LSB                                                   0
#define SCREG_LAYER0_END_BLK                                                   0
#define SCREG_LAYER0_END_Count                                                 1
#define SCREG_LAYER0_END_FieldMask                                    0xFFFFFFFF
#define SCREG_LAYER0_END_ReadMask                                     0xFFFFFFFF
#define SCREG_LAYER0_END_WriteMask                                    0xFFFFFFFF
#define SCREG_LAYER0_END_ResetValue                                   0x00000000

#define SCREG_LAYER0_END_ADDRESS                                            31:0
#define SCREG_LAYER0_END_ADDRESS_End                                          31
#define SCREG_LAYER0_END_ADDRESS_Start                                         0
#define SCREG_LAYER0_END_ADDRESS_Type                                        U32

/*******************************************************************************
**                              ~~~~~~~~~~~~~~~~                              **
**                              Module G2dLayer1                              **
**                              ~~~~~~~~~~~~~~~~                              **
*******************************************************************************/

/* Register scregLayer1Config **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer shadow Configuration Register.  Layer attributes control.  */

#define scregLayer1ConfigRegAddrs                                        0x58400
#define SCREG_LAYER1_CONFIG_Address                                     0x161000
#define SCREG_LAYER1_CONFIG_MSB                                               19
#define SCREG_LAYER1_CONFIG_LSB                                                0
#define SCREG_LAYER1_CONFIG_BLK                                                0
#define SCREG_LAYER1_CONFIG_Count                                              1
#define SCREG_LAYER1_CONFIG_FieldMask                                 0x0FFFFFFC
#define SCREG_LAYER1_CONFIG_ReadMask                                  0x0FFFFFFC
#define SCREG_LAYER1_CONFIG_WriteMask                                 0x0FFFFFFC
#define SCREG_LAYER1_CONFIG_ResetValue                                0x00800000

/* Enable this layer. Reserved: dut tie high. */
#define SCREG_LAYER1_CONFIG_ENABLE                                           2:2
#define SCREG_LAYER1_CONFIG_ENABLE_End                                         2
#define SCREG_LAYER1_CONFIG_ENABLE_Start                                       2
#define SCREG_LAYER1_CONFIG_ENABLE_Type                                      U01
#define   SCREG_LAYER1_CONFIG_ENABLE_DISABLED                                0x0
#define   SCREG_LAYER1_CONFIG_ENABLE_ENABLED                                 0x1

/* Normal mode: read full image, don’t support ROI. One ROI mode: read one  **
** ROI region in one image.                                                   */
#define SCREG_LAYER1_CONFIG_DMA_MODE                                         3:3
#define SCREG_LAYER1_CONFIG_DMA_MODE_End                                       3
#define SCREG_LAYER1_CONFIG_DMA_MODE_Start                                     3
#define SCREG_LAYER1_CONFIG_DMA_MODE_Type                                    U01
#define   SCREG_LAYER1_CONFIG_DMA_MODE_NORMAL                                0x0
#define   SCREG_LAYER1_CONFIG_DMA_MODE_ONE_ROI                               0x1

/* Tile mode.  */
#define SCREG_LAYER1_CONFIG_TILE_MODE                                        7:4
#define SCREG_LAYER1_CONFIG_TILE_MODE_End                                      7
#define SCREG_LAYER1_CONFIG_TILE_MODE_Start                                    4
#define SCREG_LAYER1_CONFIG_TILE_MODE_Type                                   U04
#define   SCREG_LAYER1_CONFIG_TILE_MODE_LINEAR                               0x0
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED32X2                            0x1
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED16X4                            0x2
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED32X4                            0x3
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED32X8                            0x4
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED16X8                            0x5
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED8X8                             0x6
#define   SCREG_LAYER1_CONFIG_TILE_MODE_TILED16X16                           0x7

/* The format of the layer.  */
#define SCREG_LAYER1_CONFIG_FORMAT                                          13:8
#define SCREG_LAYER1_CONFIG_FORMAT_End                                        13
#define SCREG_LAYER1_CONFIG_FORMAT_Start                                       8
#define SCREG_LAYER1_CONFIG_FORMAT_Type                                      U06
#define   SCREG_LAYER1_CONFIG_FORMAT_A8R8G8B8                               0x00
#define   SCREG_LAYER1_CONFIG_FORMAT_X8R8G8B8                               0x01
#define   SCREG_LAYER1_CONFIG_FORMAT_A2R10G10B10                            0x02
#define   SCREG_LAYER1_CONFIG_FORMAT_X2R10G10B10                            0x03
#define   SCREG_LAYER1_CONFIG_FORMAT_R8G8B8                                 0x04
#define   SCREG_LAYER1_CONFIG_FORMAT_R5G6B5                                 0x05
#define   SCREG_LAYER1_CONFIG_FORMAT_A1R5G5B5                               0x06
#define   SCREG_LAYER1_CONFIG_FORMAT_X1R5G5B5                               0x07
#define   SCREG_LAYER1_CONFIG_FORMAT_A4R4G4B4                               0x08
#define   SCREG_LAYER1_CONFIG_FORMAT_X4R4G4B4                               0x09
#define   SCREG_LAYER1_CONFIG_FORMAT_FP16                                   0x0A
#define   SCREG_LAYER1_CONFIG_FORMAT_YUY2                                   0x0B
#define   SCREG_LAYER1_CONFIG_FORMAT_UYVY                                   0x0C
#define   SCREG_LAYER1_CONFIG_FORMAT_YV12                                   0x0D
#define   SCREG_LAYER1_CONFIG_FORMAT_NV12                                   0x0E
#define   SCREG_LAYER1_CONFIG_FORMAT_NV16                                   0x0F
#define   SCREG_LAYER1_CONFIG_FORMAT_P010                                   0x10
#define   SCREG_LAYER1_CONFIG_FORMAT_P210                                   0x11
#define   SCREG_LAYER1_CONFIG_FORMAT_YUV420_PACKED_10BIT                    0x12
#define   SCREG_LAYER1_CONFIG_FORMAT_YV12_10BIT_MSB                         0x13
#define   SCREG_LAYER1_CONFIG_FORMAT_YUY2_10BIT                             0x14
#define   SCREG_LAYER1_CONFIG_FORMAT_UYVY_10BIT                             0x15

/* Compress Dec enable.  */
#define SCREG_LAYER1_CONFIG_COMPRESS_ENABLE                                14:14
#define SCREG_LAYER1_CONFIG_COMPRESS_ENABLE_End                               14
#define SCREG_LAYER1_CONFIG_COMPRESS_ENABLE_Start                             14
#define SCREG_LAYER1_CONFIG_COMPRESS_ENABLE_Type                             U01
#define   SCREG_LAYER1_CONFIG_COMPRESS_ENABLE_DISABLED                       0x0
#define   SCREG_LAYER1_CONFIG_COMPRESS_ENABLE_ENABLED                        0x1

/* Rot angle.  */
#define SCREG_LAYER1_CONFIG_ROT_ANGLE                                      17:15
#define SCREG_LAYER1_CONFIG_ROT_ANGLE_End                                     17
#define SCREG_LAYER1_CONFIG_ROT_ANGLE_Start                                   15
#define SCREG_LAYER1_CONFIG_ROT_ANGLE_Type                                   U03
#define   SCREG_LAYER1_CONFIG_ROT_ANGLE_ROT0                                 0x0
#define   SCREG_LAYER1_CONFIG_ROT_ANGLE_ROT90                                0x1
#define   SCREG_LAYER1_CONFIG_ROT_ANGLE_ROT180                               0x2
#define   SCREG_LAYER1_CONFIG_ROT_ANGLE_ROT270                               0x3
#define   SCREG_LAYER1_CONFIG_ROT_ANGLE_FLIP_X                               0x4
#define   SCREG_LAYER1_CONFIG_ROT_ANGLE_FLIP_Y                               0x5

/* Enable scale or disable scale.  */
#define SCREG_LAYER1_CONFIG_SCALE                                          18:18
#define SCREG_LAYER1_CONFIG_SCALE_End                                         18
#define SCREG_LAYER1_CONFIG_SCALE_Start                                       18
#define SCREG_LAYER1_CONFIG_SCALE_Type                                       U01
#define   SCREG_LAYER1_CONFIG_SCALE_DISABLED                                 0x0
#define   SCREG_LAYER1_CONFIG_SCALE_ENABLED                                  0x1

/* Enable dither or disable dither.  */
#define SCREG_LAYER1_CONFIG_DITHER                                         19:19
#define SCREG_LAYER1_CONFIG_DITHER_End                                        19
#define SCREG_LAYER1_CONFIG_DITHER_Start                                      19
#define SCREG_LAYER1_CONFIG_DITHER_Type                                      U01
#define   SCREG_LAYER1_CONFIG_DITHER_DISABLED                                0x0
#define   SCREG_LAYER1_CONFIG_DITHER_ENABLED                                 0x1

/* Assign UV swizzle, 0 means UV, 1 means VU.  */
#define SCREG_LAYER1_CONFIG_UV_SWIZZLE                                     20:20
#define SCREG_LAYER1_CONFIG_UV_SWIZZLE_End                                    20
#define SCREG_LAYER1_CONFIG_UV_SWIZZLE_Start                                  20
#define SCREG_LAYER1_CONFIG_UV_SWIZZLE_Type                                  U01
#define   SCREG_LAYER1_CONFIG_UV_SWIZZLE_UV                                  0x0
#define   SCREG_LAYER1_CONFIG_UV_SWIZZLE_VU                                  0x1

/* Assign swizzle for ARGB.  */
#define SCREG_LAYER1_CONFIG_SWIZZLE                                        22:21
#define SCREG_LAYER1_CONFIG_SWIZZLE_End                                       22
#define SCREG_LAYER1_CONFIG_SWIZZLE_Start                                     21
#define SCREG_LAYER1_CONFIG_SWIZZLE_Type                                     U02
#define   SCREG_LAYER1_CONFIG_SWIZZLE_ARGB                                   0x0
#define   SCREG_LAYER1_CONFIG_SWIZZLE_RGBA                                   0x1
#define   SCREG_LAYER1_CONFIG_SWIZZLE_ABGR                                   0x2
#define   SCREG_LAYER1_CONFIG_SWIZZLE_BGRA                                   0x3

#define SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE                               24:23
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE_End                              24
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE_Start                            23
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE_Type                            U02
#define   SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE_MODE0                         0x0
#define   SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE_MODE1                         0x1
#define   SCREG_LAYER1_CONFIG_EXTEND_BITS_MODE_MODE2                         0x2

/* YUV to RGB conversion enable bit.  */
#define SCREG_LAYER1_CONFIG_Y2R                                            25:25
#define SCREG_LAYER1_CONFIG_Y2R_End                                           25
#define SCREG_LAYER1_CONFIG_Y2R_Start                                         25
#define SCREG_LAYER1_CONFIG_Y2R_Type                                         U01
#define   SCREG_LAYER1_CONFIG_Y2R_DISABLED                                   0x0
#define   SCREG_LAYER1_CONFIG_Y2R_ENABLED                                    0x1

/* RGB to YUV conversion enable bit.  */
#define SCREG_LAYER1_CONFIG_R2Y                                            26:26
#define SCREG_LAYER1_CONFIG_R2Y_End                                           26
#define SCREG_LAYER1_CONFIG_R2Y_Start                                         26
#define SCREG_LAYER1_CONFIG_R2Y_Type                                         U01
#define   SCREG_LAYER1_CONFIG_R2Y_DISABLED                                   0x0
#define   SCREG_LAYER1_CONFIG_R2Y_ENABLED                                    0x1

/* Extend bits for A2RGB10 alpha channel.  0: Set LSB bits use                **
** ExtendBitsMode.  1: Set LSB bits from register.                            */
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_ALPHA_MODE                         27:27
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_ALPHA_MODE_End                        27
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_ALPHA_MODE_Start                      27
#define SCREG_LAYER1_CONFIG_EXTEND_BITS_ALPHA_MODE_Type                      U01
#define   SCREG_LAYER1_CONFIG_EXTEND_BITS_ALPHA_MODE_DISABLED                0x0
#define   SCREG_LAYER1_CONFIG_EXTEND_BITS_ALPHA_MODE_ENABLED                 0x1

/* Register scregLayer1Start **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Configuration Register.  Layer attributes control.  This register is **
** not buffered.                                                              */

#define scregLayer1StartRegAddrs                                         0x58401
#define SCREG_LAYER1_START_Address                                      0x161004
#define SCREG_LAYER1_START_MSB                                                19
#define SCREG_LAYER1_START_LSB                                                 0
#define SCREG_LAYER1_START_BLK                                                 0
#define SCREG_LAYER1_START_Count                                               1
#define SCREG_LAYER1_START_FieldMask                                  0x00000001
#define SCREG_LAYER1_START_ReadMask                                   0x00000001
#define SCREG_LAYER1_START_WriteMask                                  0x00000001
#define SCREG_LAYER1_START_ResetValue                                 0x00000000

/* Start Layer. This bit is a pulse. */
#define SCREG_LAYER1_START_START                                             0:0
#define SCREG_LAYER1_START_START_End                                           0
#define SCREG_LAYER1_START_START_Start                                         0
#define SCREG_LAYER1_START_START_Type                                        U01
#define   SCREG_LAYER1_START_START_DISABLED                                  0x0
#define   SCREG_LAYER1_START_START_ENABLED                                   0x1

/* Register scregLayer1Reset **
** ~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Configuration Register.  Layer attributes control.  This register is **
** not buffered.                                                              */

#define scregLayer1ResetRegAddrs                                         0x58402
#define SCREG_LAYER1_RESET_Address                                      0x161008
#define SCREG_LAYER1_RESET_MSB                                                19
#define SCREG_LAYER1_RESET_LSB                                                 0
#define SCREG_LAYER1_RESET_BLK                                                 0
#define SCREG_LAYER1_RESET_Count                                               1
#define SCREG_LAYER1_RESET_FieldMask                                  0x00000001
#define SCREG_LAYER1_RESET_ReadMask                                   0x00000001
#define SCREG_LAYER1_RESET_WriteMask                                  0x00000001
#define SCREG_LAYER1_RESET_ResetValue                                 0x00000000

/* Reset Layer Registers to default value. */
#define SCREG_LAYER1_RESET_RESET                                             0:0
#define SCREG_LAYER1_RESET_RESET_End                                           0
#define SCREG_LAYER1_RESET_RESET_Start                                         0
#define SCREG_LAYER1_RESET_RESET_Type                                        U01
#define   SCREG_LAYER1_RESET_RESET_RESET                                     0x1

/* Register scregLayer1Size **
** ~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Window Size Register.  Window size of frame buffer in memory in      **
** pixels. If frame buffer is rotated or scaled, this size may be different   **
** from size of display window.                                               */

#define scregLayer1SizeRegAddrs                                          0x58403
#define SCREG_LAYER1_SIZE_Address                                       0x16100C
#define SCREG_LAYER1_SIZE_MSB                                                 19
#define SCREG_LAYER1_SIZE_LSB                                                  0
#define SCREG_LAYER1_SIZE_BLK                                                  0
#define SCREG_LAYER1_SIZE_Count                                                1
#define SCREG_LAYER1_SIZE_FieldMask                                   0xFFFFFFFF
#define SCREG_LAYER1_SIZE_ReadMask                                    0xFFFFFFFF
#define SCREG_LAYER1_SIZE_WriteMask                                   0xFFFFFFFF
#define SCREG_LAYER1_SIZE_ResetValue                                  0x00000000

/* Width. */
#define SCREG_LAYER1_SIZE_WIDTH                                             15:0
#define SCREG_LAYER1_SIZE_WIDTH_End                                           15
#define SCREG_LAYER1_SIZE_WIDTH_Start                                          0
#define SCREG_LAYER1_SIZE_WIDTH_Type                                         U16

/* Height. */
#define SCREG_LAYER1_SIZE_HEIGHT                                           31:16
#define SCREG_LAYER1_SIZE_HEIGHT_End                                          31
#define SCREG_LAYER1_SIZE_HEIGHT_Start                                        16
#define SCREG_LAYER1_SIZE_HEIGHT_Type                                        U16

/* Register scregLayer1Address **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Start Address Register.  Starting address of the frame buffer.       **
** Address.                                                                   */

#define scregLayer1AddressRegAddrs                                       0x58404
#define SCREG_LAYER1_ADDRESS_Address                                    0x161010
#define SCREG_LAYER1_ADDRESS_MSB                                              19
#define SCREG_LAYER1_ADDRESS_LSB                                               0
#define SCREG_LAYER1_ADDRESS_BLK                                               0
#define SCREG_LAYER1_ADDRESS_Count                                             1
#define SCREG_LAYER1_ADDRESS_FieldMask                                0xFFFFFFFF
#define SCREG_LAYER1_ADDRESS_ReadMask                                 0xFFFFFFFF
#define SCREG_LAYER1_ADDRESS_WriteMask                                0xFFFFFFFF
#define SCREG_LAYER1_ADDRESS_ResetValue                               0x00000000

#define SCREG_LAYER1_ADDRESS_ADDRESS                                        31:0
#define SCREG_LAYER1_ADDRESS_ADDRESS_End                                      31
#define SCREG_LAYER1_ADDRESS_ADDRESS_Start                                     0
#define SCREG_LAYER1_ADDRESS_ADDRESS_Type                                    U32

/* Register scregLayer1HighAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Start Address Register for high 4 bits.  Starting address of the     **
** frame buffer.  High address.                                               */

#define scregLayer1HighAddressRegAddrs                                   0x58405
#define SCREG_LAYER1_HIGH_ADDRESS_Address                               0x161014
#define SCREG_LAYER1_HIGH_ADDRESS_MSB                                         19
#define SCREG_LAYER1_HIGH_ADDRESS_LSB                                          0
#define SCREG_LAYER1_HIGH_ADDRESS_BLK                                          0
#define SCREG_LAYER1_HIGH_ADDRESS_Count                                        1
#define SCREG_LAYER1_HIGH_ADDRESS_FieldMask                           0x000000FF
#define SCREG_LAYER1_HIGH_ADDRESS_ReadMask                            0x000000FF
#define SCREG_LAYER1_HIGH_ADDRESS_WriteMask                           0x000000FF
#define SCREG_LAYER1_HIGH_ADDRESS_ResetValue                          0x00000000

#define SCREG_LAYER1_HIGH_ADDRESS_ADDRESS                                    7:0
#define SCREG_LAYER1_HIGH_ADDRESS_ADDRESS_End                                  7
#define SCREG_LAYER1_HIGH_ADDRESS_ADDRESS_Start                                0
#define SCREG_LAYER1_HIGH_ADDRESS_ADDRESS_Type                               U08

/* Register scregLayer1UAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Second Plane U Start Address Register.  Starting address of the      **
** second planar (often the U plane) of the Layer if the second plane exists. **
**  Address.                                                                  */

#define scregLayer1UAddressRegAddrs                                      0x58406
#define SCREG_LAYER1_UADDRESS_Address                                   0x161018
#define SCREG_LAYER1_UADDRESS_MSB                                             19
#define SCREG_LAYER1_UADDRESS_LSB                                              0
#define SCREG_LAYER1_UADDRESS_BLK                                              0
#define SCREG_LAYER1_UADDRESS_Count                                            1
#define SCREG_LAYER1_UADDRESS_FieldMask                               0xFFFFFFFF
#define SCREG_LAYER1_UADDRESS_ReadMask                                0xFFFFFFFF
#define SCREG_LAYER1_UADDRESS_WriteMask                               0xFFFFFFFF
#define SCREG_LAYER1_UADDRESS_ResetValue                              0x00000000

#define SCREG_LAYER1_UADDRESS_ADDRESS                                       31:0
#define SCREG_LAYER1_UADDRESS_ADDRESS_End                                     31
#define SCREG_LAYER1_UADDRESS_ADDRESS_Start                                    0
#define SCREG_LAYER1_UADDRESS_ADDRESS_Type                                   U32

/* Register scregLayer1HighUAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Second Plane U Start Address Register for high 4 bits.  Starting     **
** address of the second planar (often the U plane) of the Layer if the       **
** second plane exists.  High address.                                        */

#define scregLayer1HighUAddressRegAddrs                                  0x58407
#define SCREG_LAYER1_HIGH_UADDRESS_Address                              0x16101C
#define SCREG_LAYER1_HIGH_UADDRESS_MSB                                        19
#define SCREG_LAYER1_HIGH_UADDRESS_LSB                                         0
#define SCREG_LAYER1_HIGH_UADDRESS_BLK                                         0
#define SCREG_LAYER1_HIGH_UADDRESS_Count                                       1
#define SCREG_LAYER1_HIGH_UADDRESS_FieldMask                          0x000000FF
#define SCREG_LAYER1_HIGH_UADDRESS_ReadMask                           0x000000FF
#define SCREG_LAYER1_HIGH_UADDRESS_WriteMask                          0x000000FF
#define SCREG_LAYER1_HIGH_UADDRESS_ResetValue                         0x00000000

#define SCREG_LAYER1_HIGH_UADDRESS_ADDRESS                                   7:0
#define SCREG_LAYER1_HIGH_UADDRESS_ADDRESS_End                                 7
#define SCREG_LAYER1_HIGH_UADDRESS_ADDRESS_Start                               0
#define SCREG_LAYER1_HIGH_UADDRESS_ADDRESS_Type                              U08

/* Register scregLayer1VAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer third Plane V Start Address Register.  Starting address of the third **
** planar (often the V plane) of the Layer if the third plane exists.         **
** Address.                                                                   */

#define scregLayer1VAddressRegAddrs                                      0x58408
#define SCREG_LAYER1_VADDRESS_Address                                   0x161020
#define SCREG_LAYER1_VADDRESS_MSB                                             19
#define SCREG_LAYER1_VADDRESS_LSB                                              0
#define SCREG_LAYER1_VADDRESS_BLK                                              0
#define SCREG_LAYER1_VADDRESS_Count                                            1
#define SCREG_LAYER1_VADDRESS_FieldMask                               0xFFFFFFFF
#define SCREG_LAYER1_VADDRESS_ReadMask                                0xFFFFFFFF
#define SCREG_LAYER1_VADDRESS_WriteMask                               0xFFFFFFFF
#define SCREG_LAYER1_VADDRESS_ResetValue                              0x00000000

#define SCREG_LAYER1_VADDRESS_ADDRESS                                       31:0
#define SCREG_LAYER1_VADDRESS_ADDRESS_End                                     31
#define SCREG_LAYER1_VADDRESS_ADDRESS_Start                                    0
#define SCREG_LAYER1_VADDRESS_ADDRESS_Type                                   U32

/* Register scregLayer1HighVAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer third Plane V Start Address Register for high 4 bits.  Starting      **
** address of the third planar (often the V plane) of the Layer if the third  **
** plane exists.  High address.                                               */

#define scregLayer1HighVAddressRegAddrs                                  0x58409
#define SCREG_LAYER1_HIGH_VADDRESS_Address                              0x161024
#define SCREG_LAYER1_HIGH_VADDRESS_MSB                                        19
#define SCREG_LAYER1_HIGH_VADDRESS_LSB                                         0
#define SCREG_LAYER1_HIGH_VADDRESS_BLK                                         0
#define SCREG_LAYER1_HIGH_VADDRESS_Count                                       1
#define SCREG_LAYER1_HIGH_VADDRESS_FieldMask                          0x000000FF
#define SCREG_LAYER1_HIGH_VADDRESS_ReadMask                           0x000000FF
#define SCREG_LAYER1_HIGH_VADDRESS_WriteMask                          0x000000FF
#define SCREG_LAYER1_HIGH_VADDRESS_ResetValue                         0x00000000

#define SCREG_LAYER1_HIGH_VADDRESS_ADDRESS                                   7:0
#define SCREG_LAYER1_HIGH_VADDRESS_ADDRESS_End                                 7
#define SCREG_LAYER1_HIGH_VADDRESS_ADDRESS_Start                               0
#define SCREG_LAYER1_HIGH_VADDRESS_ADDRESS_Type                              U08

/* Register scregLayer1Stride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Stride Register.  Stride of the frame buffer in bytes. */

#define scregLayer1StrideRegAddrs                                        0x5840A
#define SCREG_LAYER1_STRIDE_Address                                     0x161028
#define SCREG_LAYER1_STRIDE_MSB                                               19
#define SCREG_LAYER1_STRIDE_LSB                                                0
#define SCREG_LAYER1_STRIDE_BLK                                                0
#define SCREG_LAYER1_STRIDE_Count                                              1
#define SCREG_LAYER1_STRIDE_FieldMask                                 0x0003FFFF
#define SCREG_LAYER1_STRIDE_ReadMask                                  0x0003FFFF
#define SCREG_LAYER1_STRIDE_WriteMask                                 0x0003FFFF
#define SCREG_LAYER1_STRIDE_ResetValue                                0x00000000

/* Number of bytes from start of one line to the next line.  */
#define SCREG_LAYER1_STRIDE_STRIDE                                          17:0
#define SCREG_LAYER1_STRIDE_STRIDE_End                                        17
#define SCREG_LAYER1_STRIDE_STRIDE_Start                                       0
#define SCREG_LAYER1_STRIDE_STRIDE_Type                                      U18

/* Register scregLayer1UStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Second Plane U Stride Register.  Stride of the second planar (often  **
** the U plane) of Layer if the second plane exists.                          */

#define scregLayer1UStrideRegAddrs                                       0x5840B
#define SCREG_LAYER1_USTRIDE_Address                                    0x16102C
#define SCREG_LAYER1_USTRIDE_MSB                                              19
#define SCREG_LAYER1_USTRIDE_LSB                                               0
#define SCREG_LAYER1_USTRIDE_BLK                                               0
#define SCREG_LAYER1_USTRIDE_Count                                             1
#define SCREG_LAYER1_USTRIDE_FieldMask                                0x0003FFFF
#define SCREG_LAYER1_USTRIDE_ReadMask                                 0x0003FFFF
#define SCREG_LAYER1_USTRIDE_WriteMask                                0x0003FFFF
#define SCREG_LAYER1_USTRIDE_ResetValue                               0x00000000

/* Number of bytes from the start of one line to the next line. */
#define SCREG_LAYER1_USTRIDE_STRIDE                                         17:0
#define SCREG_LAYER1_USTRIDE_STRIDE_End                                       17
#define SCREG_LAYER1_USTRIDE_STRIDE_Start                                      0
#define SCREG_LAYER1_USTRIDE_STRIDE_Type                                     U18

/* Register scregLayer1VStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Third Plane V Stride Register.  Stride of the third planar (often    **
** the V plane) of the Layer if a third plane exists.                         */

#define scregLayer1VStrideRegAddrs                                       0x5840C
#define SCREG_LAYER1_VSTRIDE_Address                                    0x161030
#define SCREG_LAYER1_VSTRIDE_MSB                                              19
#define SCREG_LAYER1_VSTRIDE_LSB                                               0
#define SCREG_LAYER1_VSTRIDE_BLK                                               0
#define SCREG_LAYER1_VSTRIDE_Count                                             1
#define SCREG_LAYER1_VSTRIDE_FieldMask                                0x0003FFFF
#define SCREG_LAYER1_VSTRIDE_ReadMask                                 0x0003FFFF
#define SCREG_LAYER1_VSTRIDE_WriteMask                                0x0003FFFF
#define SCREG_LAYER1_VSTRIDE_ResetValue                               0x00000000

/* Number of bytes from the start of one line to the next line.  */
#define SCREG_LAYER1_VSTRIDE_STRIDE                                         17:0
#define SCREG_LAYER1_VSTRIDE_STRIDE_End                                       17
#define SCREG_LAYER1_VSTRIDE_STRIDE_Start                                      0
#define SCREG_LAYER1_VSTRIDE_STRIDE_Type                                     U18

/* Register scregLayer1InROIOrigin **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Input Region of Interest Origin Register.  */

#define scregLayer1InROIOriginRegAddrs                                   0x5840D
#define SCREG_LAYER1_IN_ROI_ORIGIN_Address                              0x161034
#define SCREG_LAYER1_IN_ROI_ORIGIN_MSB                                        19
#define SCREG_LAYER1_IN_ROI_ORIGIN_LSB                                         0
#define SCREG_LAYER1_IN_ROI_ORIGIN_BLK                                         0
#define SCREG_LAYER1_IN_ROI_ORIGIN_Count                                       1
#define SCREG_LAYER1_IN_ROI_ORIGIN_FieldMask                          0xFFFFFFFF
#define SCREG_LAYER1_IN_ROI_ORIGIN_ReadMask                           0xFFFFFFFF
#define SCREG_LAYER1_IN_ROI_ORIGIN_WriteMask                          0xFFFFFFFF
#define SCREG_LAYER1_IN_ROI_ORIGIN_ResetValue                         0x00000000

/* Rectangle start point X coordinate. */
#define SCREG_LAYER1_IN_ROI_ORIGIN_X                                        15:0
#define SCREG_LAYER1_IN_ROI_ORIGIN_X_End                                      15
#define SCREG_LAYER1_IN_ROI_ORIGIN_X_Start                                     0
#define SCREG_LAYER1_IN_ROI_ORIGIN_X_Type                                    U16

/* Rectangle start point Y coordinate. */
#define SCREG_LAYER1_IN_ROI_ORIGIN_Y                                       31:16
#define SCREG_LAYER1_IN_ROI_ORIGIN_Y_End                                      31
#define SCREG_LAYER1_IN_ROI_ORIGIN_Y_Start                                    16
#define SCREG_LAYER1_IN_ROI_ORIGIN_Y_Type                                    U16

/* Register scregLayer1InROISize **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Input Region of Interest Size Register.  */

#define scregLayer1InROISizeRegAddrs                                     0x5840E
#define SCREG_LAYER1_IN_ROI_SIZE_Address                                0x161038
#define SCREG_LAYER1_IN_ROI_SIZE_MSB                                          19
#define SCREG_LAYER1_IN_ROI_SIZE_LSB                                           0
#define SCREG_LAYER1_IN_ROI_SIZE_BLK                                           0
#define SCREG_LAYER1_IN_ROI_SIZE_Count                                         1
#define SCREG_LAYER1_IN_ROI_SIZE_FieldMask                            0xFFFFFFFF
#define SCREG_LAYER1_IN_ROI_SIZE_ReadMask                             0xFFFFFFFF
#define SCREG_LAYER1_IN_ROI_SIZE_WriteMask                            0xFFFFFFFF
#define SCREG_LAYER1_IN_ROI_SIZE_ResetValue                           0x00000000

/* Rectangle width.  */
#define SCREG_LAYER1_IN_ROI_SIZE_WIDTH                                      15:0
#define SCREG_LAYER1_IN_ROI_SIZE_WIDTH_End                                    15
#define SCREG_LAYER1_IN_ROI_SIZE_WIDTH_Start                                   0
#define SCREG_LAYER1_IN_ROI_SIZE_WIDTH_Type                                  U16

/* Rectangle height.  */
#define SCREG_LAYER1_IN_ROI_SIZE_HEIGHT                                    31:16
#define SCREG_LAYER1_IN_ROI_SIZE_HEIGHT_End                                   31
#define SCREG_LAYER1_IN_ROI_SIZE_HEIGHT_Start                                 16
#define SCREG_LAYER1_IN_ROI_SIZE_HEIGHT_Type                                 U16

/* Register scregLayer1AlphaBitExtend **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer alpha bit extend for A2R10G10B10.  A8 = alpha0 while A2 == 0.  A8 =  **
** alpha1 while A2 == 1.  A8 = alpha2 while A2 == 2.  A8 = alpha3 while A2 == **
** 3.                                                                         */

#define scregLayer1AlphaBitExtendRegAddrs                                0x5840F
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_Address                           0x16103C
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_MSB                                     19
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_LSB                                      0
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_BLK                                      0
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_Count                                    1
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ResetValue                      0x00000000

/* Alpha0.  */
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA0                                 7:0
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA0_End                               7
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA0_Start                             0
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA0_Type                            U08

/* Alpha1.  */
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA1                                15:8
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA1_End                              15
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA1_Start                             8
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA1_Type                            U08

/* Alpha2.  */
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA2                               23:16
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA2_End                              23
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA2_Start                            16
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA2_Type                            U08

/* Alpha3.  */
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA3                               31:24
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA3_End                              31
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA3_Start                            24
#define SCREG_LAYER1_ALPHA_BIT_EXTEND_ALPHA3_Type                            U08

/* Register scregLayer1UVUpSample **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer upsample phase in horizontal direction.  Layer upsample phase in     **
** vertical direction.                                                        */

#define scregLayer1UVUpSampleRegAddrs                                    0x58410
#define SCREG_LAYER1_UV_UP_SAMPLE_Address                               0x161040
#define SCREG_LAYER1_UV_UP_SAMPLE_MSB                                         19
#define SCREG_LAYER1_UV_UP_SAMPLE_LSB                                          0
#define SCREG_LAYER1_UV_UP_SAMPLE_BLK                                          0
#define SCREG_LAYER1_UV_UP_SAMPLE_Count                                        1
#define SCREG_LAYER1_UV_UP_SAMPLE_FieldMask                           0x00001F1F
#define SCREG_LAYER1_UV_UP_SAMPLE_ReadMask                            0x00001F1F
#define SCREG_LAYER1_UV_UP_SAMPLE_WriteMask                           0x00001F1F
#define SCREG_LAYER1_UV_UP_SAMPLE_ResetValue                          0x00000000

/* Value range 0 ~ 16.  */
#define SCREG_LAYER1_UV_UP_SAMPLE_HPHASE                                     4:0
#define SCREG_LAYER1_UV_UP_SAMPLE_HPHASE_End                                   4
#define SCREG_LAYER1_UV_UP_SAMPLE_HPHASE_Start                                 0
#define SCREG_LAYER1_UV_UP_SAMPLE_HPHASE_Type                                U05

/* Value range 0 ~ 16.  */
#define SCREG_LAYER1_UV_UP_SAMPLE_VPHASE                                    12:8
#define SCREG_LAYER1_UV_UP_SAMPLE_VPHASE_End                                  12
#define SCREG_LAYER1_UV_UP_SAMPLE_VPHASE_Start                                 8
#define SCREG_LAYER1_UV_UP_SAMPLE_VPHASE_Type                                U05

/* Register scregLayer1HScaleFactor **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Horizontal Scale Factor Register.  Horizontal scale factor used to   **
** scale the Layer.  15.16: 15 bits integer, 16 bits fraction.                */

#define scregLayer1HScaleFactorRegAddrs                                  0x58411
#define SCREG_LAYER1_HSCALE_FACTOR_Address                              0x161044
#define SCREG_LAYER1_HSCALE_FACTOR_MSB                                        19
#define SCREG_LAYER1_HSCALE_FACTOR_LSB                                         0
#define SCREG_LAYER1_HSCALE_FACTOR_BLK                                         0
#define SCREG_LAYER1_HSCALE_FACTOR_Count                                       1
#define SCREG_LAYER1_HSCALE_FACTOR_FieldMask                          0x7FFFFFFF
#define SCREG_LAYER1_HSCALE_FACTOR_ReadMask                           0x7FFFFFFF
#define SCREG_LAYER1_HSCALE_FACTOR_WriteMask                          0x7FFFFFFF
#define SCREG_LAYER1_HSCALE_FACTOR_ResetValue                         0x00000000

/* X scale factor.  */
#define SCREG_LAYER1_HSCALE_FACTOR_X                                        30:0
#define SCREG_LAYER1_HSCALE_FACTOR_X_End                                      30
#define SCREG_LAYER1_HSCALE_FACTOR_X_Start                                     0
#define SCREG_LAYER1_HSCALE_FACTOR_X_Type                                    U31

/* Register scregLayer1VScaleFactor **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Vertical Scale Factor Register.  Vertical scale factor used to scale **
** the Layer.  15.16: 15 bits integer, 16 bits fraction.                      */

#define scregLayer1VScaleFactorRegAddrs                                  0x58412
#define SCREG_LAYER1_VSCALE_FACTOR_Address                              0x161048
#define SCREG_LAYER1_VSCALE_FACTOR_MSB                                        19
#define SCREG_LAYER1_VSCALE_FACTOR_LSB                                         0
#define SCREG_LAYER1_VSCALE_FACTOR_BLK                                         0
#define SCREG_LAYER1_VSCALE_FACTOR_Count                                       1
#define SCREG_LAYER1_VSCALE_FACTOR_FieldMask                          0x7FFFFFFF
#define SCREG_LAYER1_VSCALE_FACTOR_ReadMask                           0x7FFFFFFF
#define SCREG_LAYER1_VSCALE_FACTOR_WriteMask                          0x7FFFFFFF
#define SCREG_LAYER1_VSCALE_FACTOR_ResetValue                         0x00000000

/* Y scale factor.  */
#define SCREG_LAYER1_VSCALE_FACTOR_Y                                        30:0
#define SCREG_LAYER1_VSCALE_FACTOR_Y_End                                      30
#define SCREG_LAYER1_VSCALE_FACTOR_Y_Start                                     0
#define SCREG_LAYER1_VSCALE_FACTOR_Y_Type                                    U31

/* Register scregLayer1HScaleCoefData (77 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Horizontal Scale Coefficient data Register.  2.14: 2 bits integer,   **
** 14 bits fraction.                                                          */

#define scregLayer1HScaleCoefDataRegAddrs                                0x58413
#define SCREG_LAYER1_HSCALE_COEF_DATA_Address                           0x16104C
#define SCREG_LAYER1_HSCALE_COEF_DATA_MSB                                     19
#define SCREG_LAYER1_HSCALE_COEF_DATA_LSB                                      7
#define SCREG_LAYER1_HSCALE_COEF_DATA_BLK                                      7
#define SCREG_LAYER1_HSCALE_COEF_DATA_Count                                   77
#define SCREG_LAYER1_HSCALE_COEF_DATA_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER1_HSCALE_COEF_DATA_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER1_HSCALE_COEF_DATA_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER1_HSCALE_COEF_DATA_ResetValue                      0x00000000

/* Coefficients low 16 bits. */
#define SCREG_LAYER1_HSCALE_COEF_DATA_LOW                                   15:0
#define SCREG_LAYER1_HSCALE_COEF_DATA_LOW_End                                 15
#define SCREG_LAYER1_HSCALE_COEF_DATA_LOW_Start                                0
#define SCREG_LAYER1_HSCALE_COEF_DATA_LOW_Type                               U16

/* Coefficients high 16 bits. */
#define SCREG_LAYER1_HSCALE_COEF_DATA_HIGH                                 31:16
#define SCREG_LAYER1_HSCALE_COEF_DATA_HIGH_End                                31
#define SCREG_LAYER1_HSCALE_COEF_DATA_HIGH_Start                              16
#define SCREG_LAYER1_HSCALE_COEF_DATA_HIGH_Type                              U16

/* Register scregLayer1VScaleCoefData (43 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Vertical Scale Coefficient data Register.  2.14: 2 bits integer, 14  **
** bits fraction.                                                             */

#define scregLayer1VScaleCoefDataRegAddrs                                0x58460
#define SCREG_LAYER1_VSCALE_COEF_DATA_Address                           0x161180
#define SCREG_LAYER1_VSCALE_COEF_DATA_MSB                                     19
#define SCREG_LAYER1_VSCALE_COEF_DATA_LSB                                      6
#define SCREG_LAYER1_VSCALE_COEF_DATA_BLK                                      6
#define SCREG_LAYER1_VSCALE_COEF_DATA_Count                                   43
#define SCREG_LAYER1_VSCALE_COEF_DATA_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER1_VSCALE_COEF_DATA_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER1_VSCALE_COEF_DATA_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER1_VSCALE_COEF_DATA_ResetValue                      0x00000000

/* Coefficients low 16 bits. */
#define SCREG_LAYER1_VSCALE_COEF_DATA_LOW                                   15:0
#define SCREG_LAYER1_VSCALE_COEF_DATA_LOW_End                                 15
#define SCREG_LAYER1_VSCALE_COEF_DATA_LOW_Start                                0
#define SCREG_LAYER1_VSCALE_COEF_DATA_LOW_Type                               U16

/* Coefficients high 16 bits. */
#define SCREG_LAYER1_VSCALE_COEF_DATA_HIGH                                 31:16
#define SCREG_LAYER1_VSCALE_COEF_DATA_HIGH_End                                31
#define SCREG_LAYER1_VSCALE_COEF_DATA_HIGH_Start                              16
#define SCREG_LAYER1_VSCALE_COEF_DATA_HIGH_Type                              U16

/* Register scregLayer1ScaleInitialOffsetX **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Scaler Source Offset X Register.  */

#define scregLayer1ScaleInitialOffsetXRegAddrs                           0x5848B
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_Address                     0x16122C
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_MSB                               19
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_LSB                                0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_BLK                                0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_Count                              1
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_FieldMask                 0xFFFFFFFF
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_ReadMask                  0xFFFFFFFF
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_WriteMask                 0xFFFFFFFF
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_ResetValue                0x00008000

/* X offset(initial error). */
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_VALUE                           31:0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_VALUE_End                         31
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_VALUE_Start                        0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_X_VALUE_Type                       U32

/* Register scregLayer1ScaleInitialOffsetY **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Scaler Source Offset Y Register.  */

#define scregLayer1ScaleInitialOffsetYRegAddrs                           0x5848C
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_Address                     0x161230
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_MSB                               19
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_LSB                                0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_BLK                                0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_Count                              1
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_FieldMask                 0xFFFFFFFF
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_ReadMask                  0xFFFFFFFF
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_WriteMask                 0xFFFFFFFF
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_ResetValue                0x00008000

/* Y offset(initial error). */
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_VALUE                           31:0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_VALUE_End                         31
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_VALUE_Start                        0
#define SCREG_LAYER1_SCALE_INITIAL_OFFSET_Y_VALUE_Type                       U32

/* Register scregLayer1Y2rConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Y2R configuration.  */

#define scregLayer1Y2rConfigRegAddrs                                     0x5848D
#define SCREG_LAYER1_Y2R_CONFIG_Address                                 0x161234
#define SCREG_LAYER1_Y2R_CONFIG_MSB                                           19
#define SCREG_LAYER1_Y2R_CONFIG_LSB                                            0
#define SCREG_LAYER1_Y2R_CONFIG_BLK                                            0
#define SCREG_LAYER1_Y2R_CONFIG_Count                                          1
#define SCREG_LAYER1_Y2R_CONFIG_FieldMask                             0x000000F7
#define SCREG_LAYER1_Y2R_CONFIG_ReadMask                              0x000000F7
#define SCREG_LAYER1_Y2R_CONFIG_WriteMask                             0x000000F7
#define SCREG_LAYER1_Y2R_CONFIG_ResetValue                            0x00000000

/* The mode of the CSC.  */
#define SCREG_LAYER1_Y2R_CONFIG_MODE                                         2:0
#define SCREG_LAYER1_Y2R_CONFIG_MODE_End                                       2
#define SCREG_LAYER1_Y2R_CONFIG_MODE_Start                                     0
#define SCREG_LAYER1_Y2R_CONFIG_MODE_Type                                    U03
#define   SCREG_LAYER1_Y2R_CONFIG_MODE_PROGRAMMABLE                          0x0
#define   SCREG_LAYER1_Y2R_CONFIG_MODE_LIMIT_YUV_2_LIMIT_RGB                 0x1
#define   SCREG_LAYER1_Y2R_CONFIG_MODE_LIMIT_YUV_2_FULL_RGB                  0x2
#define   SCREG_LAYER1_Y2R_CONFIG_MODE_FULL_YUV_2_LIMIT_RGB                  0x3
#define   SCREG_LAYER1_Y2R_CONFIG_MODE_FULL_YUV_2_FULL_RGB                   0x4

/* The mode of the Color Gamut.  */
#define SCREG_LAYER1_Y2R_CONFIG_GAMUT                                        7:4
#define SCREG_LAYER1_Y2R_CONFIG_GAMUT_End                                      7
#define SCREG_LAYER1_Y2R_CONFIG_GAMUT_Start                                    4
#define SCREG_LAYER1_Y2R_CONFIG_GAMUT_Type                                   U04
#define   SCREG_LAYER1_Y2R_CONFIG_GAMUT_BT601                                0x0
#define   SCREG_LAYER1_Y2R_CONFIG_GAMUT_BT709                                0x1
#define   SCREG_LAYER1_Y2R_CONFIG_GAMUT_BT2020                               0x2
#define   SCREG_LAYER1_Y2R_CONFIG_GAMUT_P3                                   0x3
#define   SCREG_LAYER1_Y2R_CONFIG_GAMUT_SRGB                                 0x4

/* Register scregLayer1YUVToRGBCoef0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 0 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef0RegAddrs                                 0x5848E
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_Address                           0x161238
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_ResetValue                      0x00001000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF0_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 1 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef1RegAddrs                                 0x5848F
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_Address                           0x16123C
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF1_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 2 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef2RegAddrs                                 0x58490
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_Address                           0x161240
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF2_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef3 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 3 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef3RegAddrs                                 0x58491
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_Address                           0x161244
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF3_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef4 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 4 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef4RegAddrs                                 0x58492
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_Address                           0x161248
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_ResetValue                      0x00001000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF4_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef5 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 5 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef5RegAddrs                                 0x58493
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_Address                           0x16124C
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF5_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef6 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 6 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef6RegAddrs                                 0x58494
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_Address                           0x161250
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF6_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef7 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficients 7 Register.  User defined YUV2RGB     **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef7RegAddrs                                 0x58495
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_Address                           0x161254
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF7_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoef8 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Coefficient 8 Register.  User defined YUV2RGB      **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoef8RegAddrs                                 0x58496
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_Address                           0x161258
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_MSB                                     19
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_LSB                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_BLK                                      0
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_Count                                    1
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_ResetValue                      0x00001000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_VALUE                                 18:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_VALUE_End                               18
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_VALUE_Start                              0
#define SCREG_LAYER1_YUV_TO_RGB_COEF8_VALUE_Type                             U19

/* Register scregLayer1YUVToRGBCoefPreOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset0 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoefPreOffset0RegAddrs                        0x58497
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_Address                0x16125C
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_MSB                          19
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_LSB                           0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_BLK                           0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_Count                         1
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_FieldMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_ReadMask             0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_WriteMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE                      12:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE_End                    12
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE_Start                   0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET0_VALUE_Type                  U13

/* Register scregLayer1YUVToRGBCoefPreOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset1 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoefPreOffset1RegAddrs                        0x58498
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_Address                0x161260
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_MSB                          19
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_LSB                           0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_BLK                           0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_Count                         1
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_FieldMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_ReadMask             0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_WriteMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE                      12:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE_End                    12
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE_Start                   0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET1_VALUE_Type                  U13

/* Register scregLayer1YUVToRGBCoefPreOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset2 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer1YUVToRGBCoefPreOffset2RegAddrs                        0x58499
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_Address                0x161264
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_MSB                          19
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_LSB                           0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_BLK                           0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_Count                         1
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_FieldMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_ReadMask             0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_WriteMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE                      12:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE_End                    12
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE_Start                   0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_PRE_OFFSET2_VALUE_Type                  U13

/* Register scregLayer1YUVToRGBCoefPostOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset0 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer1YUVToRGBCoefPostOffset0RegAddrs                       0x5849A
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_Address               0x161268
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_MSB                         19
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_LSB                          0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_BLK                          0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_Count                        1
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_FieldMask           0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_ReadMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_WriteMask           0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE                     12:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE_End                   12
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE_Start                  0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET0_VALUE_Type                 U13

/* Register scregLayer1YUVToRGBCoefPostOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset1 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer1YUVToRGBCoefPostOffset1RegAddrs                       0x5849B
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_Address               0x16126C
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_MSB                         19
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_LSB                          0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_BLK                          0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_Count                        1
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_FieldMask           0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_ReadMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_WriteMask           0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE                     12:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE_End                   12
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE_Start                  0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET1_VALUE_Type                 U13

/* Register scregLayer1YUVToRGBCoefPostOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset2 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer1YUVToRGBCoefPostOffset2RegAddrs                       0x5849C
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_Address               0x161270
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_MSB                         19
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_LSB                          0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_BLK                          0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_Count                        1
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_FieldMask           0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_ReadMask            0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_WriteMask           0x00001FFF
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE                     12:0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE_End                   12
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE_Start                  0
#define SCREG_LAYER1_YUV_TO_RGB_COEF_POST_OFFSET2_VALUE_Type                 U13

/* Register scregLayer1UVDownSample **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Down Sample Configuration Register.  */

#define scregLayer1UVDownSampleRegAddrs                                  0x5849D
#define SCREG_LAYER1_UV_DOWN_SAMPLE_Address                             0x161274
#define SCREG_LAYER1_UV_DOWN_SAMPLE_MSB                                       19
#define SCREG_LAYER1_UV_DOWN_SAMPLE_LSB                                        0
#define SCREG_LAYER1_UV_DOWN_SAMPLE_BLK                                        0
#define SCREG_LAYER1_UV_DOWN_SAMPLE_Count                                      1
#define SCREG_LAYER1_UV_DOWN_SAMPLE_FieldMask                         0x0000000F
#define SCREG_LAYER1_UV_DOWN_SAMPLE_ReadMask                          0x0000000F
#define SCREG_LAYER1_UV_DOWN_SAMPLE_WriteMask                         0x0000000F
#define SCREG_LAYER1_UV_DOWN_SAMPLE_ResetValue                        0x00000000

/* UV down sample mode in horizontal.  0: Drop second UV in horizontal        **
** direction.  1: Use linear interpolation to do down sample.  2: Use filter  **
** to do down sample.                                                         */
#define SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE                             1:0
#define SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE_End                           1
#define SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE_Start                         0
#define SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE_Type                        U02
#define   SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE_DROP                      0x0
#define   SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE_AVERAGE                   0x1
#define   SCREG_LAYER1_UV_DOWN_SAMPLE_HORI_DS_MODE_FILTER                    0x2

/* UV down sample mode in vertical.  0: Drop second UV in vertical direction. **
**  1: Use linear interpolation to do down sample.  2: Use filter to do down  **
** sample.                                                                    */
#define SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE                            3:2
#define SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE_End                          3
#define SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE_Start                        2
#define SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE_Type                       U02
#define   SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE_DROP                     0x0
#define   SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE_AVERAGE                  0x1
#define   SCREG_LAYER1_UV_DOWN_SAMPLE_VERTI_DS_MODE_FILTER                   0x2

/* Register scregLayer1DthCfg **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer Dither Configuration.  */

#define scregLayer1DthCfgRegAddrs                                        0x5849E
#define SCREG_LAYER1_DTH_CFG_Address                                    0x161278
#define SCREG_LAYER1_DTH_CFG_MSB                                              19
#define SCREG_LAYER1_DTH_CFG_LSB                                               0
#define SCREG_LAYER1_DTH_CFG_BLK                                               0
#define SCREG_LAYER1_DTH_CFG_Count                                             1
#define SCREG_LAYER1_DTH_CFG_FieldMask                                0x0000001F
#define SCREG_LAYER1_DTH_CFG_ReadMask                                 0x0000001F
#define SCREG_LAYER1_DTH_CFG_WriteMask                                0x0000001F
#define SCREG_LAYER1_DTH_CFG_ResetValue                               0x00000000

/* Bit rounding mode while dither is disabled.  0: Truncation.  1: Rounding.  */
#define SCREG_LAYER1_DTH_CFG_ROUNDING_MODE                                   0:0
#define SCREG_LAYER1_DTH_CFG_ROUNDING_MODE_End                                 0
#define SCREG_LAYER1_DTH_CFG_ROUNDING_MODE_Start                               0
#define SCREG_LAYER1_DTH_CFG_ROUNDING_MODE_Type                              U01
#define   SCREG_LAYER1_DTH_CFG_ROUNDING_MODE_TRUNCATION                      0x0
#define   SCREG_LAYER1_DTH_CFG_ROUNDING_MODE_ROUNDING                        0x1

/* Frame index enable bit.  0: HW.  1: SW.  */
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_ENABLE                              1:1
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_ENABLE_End                            1
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_ENABLE_Start                          1
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_ENABLE_Type                         U01
#define   SCREG_LAYER1_DTH_CFG_FRAME_INDEX_ENABLE_DISABLED                   0x0
#define   SCREG_LAYER1_DTH_CFG_FRAME_INDEX_ENABLE_ENABLED                    0x1

/* Frame index from HW or SW.  0: HW.  1: SW.  */
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_FROM                                2:2
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_FROM_End                              2
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_FROM_Start                            2
#define SCREG_LAYER1_DTH_CFG_FRAME_INDEX_FROM_Type                           U01
#define   SCREG_LAYER1_DTH_CFG_FRAME_INDEX_FROM_HW                           0x0
#define   SCREG_LAYER1_DTH_CFG_FRAME_INDEX_FROM_SW                           0x1

/* Frame index configured by SW.  */
#define SCREG_LAYER1_DTH_CFG_SW_FRAME_INDEX                                  4:3
#define SCREG_LAYER1_DTH_CFG_SW_FRAME_INDEX_End                                4
#define SCREG_LAYER1_DTH_CFG_SW_FRAME_INDEX_Start                              3
#define SCREG_LAYER1_DTH_CFG_SW_FRAME_INDEX_Type                             U02

/* Register scregLayer1DthRTableLow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for R channel low 32bits.  */

#define scregLayer1DthRTableLowRegAddrs                                  0x5849F
#define SCREG_LAYER1_DTH_RTABLE_LOW_Address                             0x16127C
#define SCREG_LAYER1_DTH_RTABLE_LOW_MSB                                       19
#define SCREG_LAYER1_DTH_RTABLE_LOW_LSB                                        0
#define SCREG_LAYER1_DTH_RTABLE_LOW_BLK                                        0
#define SCREG_LAYER1_DTH_RTABLE_LOW_Count                                      1
#define SCREG_LAYER1_DTH_RTABLE_LOW_FieldMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_RTABLE_LOW_ReadMask                          0xFFFFFFFF
#define SCREG_LAYER1_DTH_RTABLE_LOW_WriteMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_RTABLE_LOW_ResetValue                        0x98BDE510

/* Dither threshold value for x,y=0,0.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X0                                    3:0
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X0_End                                  3
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X0_Start                                0
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X0_Type                               U04

/* Dither threshold value for x,y=1,0.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X1                                    7:4
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X1_End                                  7
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X1_Start                                4
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X1_Type                               U04

/* Dither threshold value for x,y=2,0.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X2                                   11:8
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X2_End                                 11
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X2_Start                                8
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X2_Type                               U04

/* Dither threshold value for x,y=3,0.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X3                                  15:12
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X3_End                                 15
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X3_Start                               12
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y0_X3_Type                               U04

/* Dither threshold value for x,y=0,1.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X0                                  19:16
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X0_End                                 19
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X0_Start                               16
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X0_Type                               U04

/* Dither threshold value for x,y=1,1.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X1                                  23:20
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X1_End                                 23
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X1_Start                               20
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X1_Type                               U04

/* Dither threshold value for x,y=2,1.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X2                                  27:24
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X2_End                                 27
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X2_Start                               24
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X2_Type                               U04

/* Dither threshold value for x,y=3,1.  */
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X3                                  31:28
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X3_End                                 31
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X3_Start                               28
#define SCREG_LAYER1_DTH_RTABLE_LOW_Y1_X3_Type                               U04

/* Register scregLayer1DthRTableHigh **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for R channel high 32bits.  */

#define scregLayer1DthRTableHighRegAddrs                                 0x584A0
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Address                            0x161280
#define SCREG_LAYER1_DTH_RTABLE_HIGH_MSB                                      19
#define SCREG_LAYER1_DTH_RTABLE_HIGH_LSB                                       0
#define SCREG_LAYER1_DTH_RTABLE_HIGH_BLK                                       0
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Count                                     1
#define SCREG_LAYER1_DTH_RTABLE_HIGH_FieldMask                        0xFFFFFFFF
#define SCREG_LAYER1_DTH_RTABLE_HIGH_ReadMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_RTABLE_HIGH_WriteMask                        0xFFFFFFFF
#define SCREG_LAYER1_DTH_RTABLE_HIGH_ResetValue                       0x63CA74F2

/* Dither threshold value for x,y=0,2.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X0                                   3:0
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X0_End                                 3
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X0_Start                               0
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X0_Type                              U04

/* Dither threshold value for x,y=1,2.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X1                                   7:4
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X1_End                                 7
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X1_Start                               4
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X1_Type                              U04

/* Dither threshold value for x,y=2,2.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X2                                  11:8
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X2_End                                11
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X2_Start                               8
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X2_Type                              U04

/* Dither threshold value for x,y=3,2.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X3                                 15:12
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X3_End                                15
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X3_Start                              12
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y2_X3_Type                              U04

/* Dither threshold value for x,y=0,3.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X0                                 19:16
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X0_End                                19
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X0_Start                              16
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X0_Type                              U04

/* Dither threshold value for x,y=1,3.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X1                                 23:20
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X1_End                                23
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X1_Start                              20
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X1_Type                              U04

/* Dither threshold value for x,y=2,3.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X2                                 27:24
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X2_End                                27
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X2_Start                              24
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X2_Type                              U04

/* Dither threshold value for x,y=3,3.  */
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X3                                 31:28
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X3_End                                31
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X3_Start                              28
#define SCREG_LAYER1_DTH_RTABLE_HIGH_Y3_X3_Type                              U04

/* Register scregLayer1DthGTableLow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for G channel low 32bits.  */

#define scregLayer1DthGTableLowRegAddrs                                  0x584A1
#define SCREG_LAYER1_DTH_GTABLE_LOW_Address                             0x161284
#define SCREG_LAYER1_DTH_GTABLE_LOW_MSB                                       19
#define SCREG_LAYER1_DTH_GTABLE_LOW_LSB                                        0
#define SCREG_LAYER1_DTH_GTABLE_LOW_BLK                                        0
#define SCREG_LAYER1_DTH_GTABLE_LOW_Count                                      1
#define SCREG_LAYER1_DTH_GTABLE_LOW_FieldMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_GTABLE_LOW_ReadMask                          0xFFFFFFFF
#define SCREG_LAYER1_DTH_GTABLE_LOW_WriteMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_GTABLE_LOW_ResetValue                        0x3AC9D826

/* Dither threshold value for x,y=0,0.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X0                                    3:0
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X0_End                                  3
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X0_Start                                0
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X0_Type                               U04

/* Dither threshold value for x,y=1,0.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X1                                    7:4
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X1_End                                  7
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X1_Start                                4
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X1_Type                               U04

/* Dither threshold value for x,y=2,0.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X2                                   11:8
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X2_End                                 11
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X2_Start                                8
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X2_Type                               U04

/* Dither threshold value for x,y=3,0.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X3                                  15:12
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X3_End                                 15
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X3_Start                               12
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y0_X3_Type                               U04

/* Dither threshold value for x,y=0,1.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X0                                  19:16
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X0_End                                 19
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X0_Start                               16
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X0_Type                               U04

/* Dither threshold value for x,y=1,1.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X1                                  23:20
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X1_End                                 23
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X1_Start                               20
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X1_Type                               U04

/* Dither threshold value for x,y=2,1.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X2                                  27:24
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X2_End                                 27
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X2_Start                               24
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X2_Type                               U04

/* Dither threshold value for x,y=3,1.  */
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X3                                  31:28
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X3_End                                 31
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X3_Start                               28
#define SCREG_LAYER1_DTH_GTABLE_LOW_Y1_X3_Type                               U04

/* Register scregLayer1DthGTableHigh **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for G channel high 32bits.  */

#define scregLayer1DthGTableHighRegAddrs                                 0x584A2
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Address                            0x161288
#define SCREG_LAYER1_DTH_GTABLE_HIGH_MSB                                      19
#define SCREG_LAYER1_DTH_GTABLE_HIGH_LSB                                       0
#define SCREG_LAYER1_DTH_GTABLE_HIGH_BLK                                       0
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Count                                     1
#define SCREG_LAYER1_DTH_GTABLE_HIGH_FieldMask                        0xFFFFFFFF
#define SCREG_LAYER1_DTH_GTABLE_HIGH_ReadMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_GTABLE_HIGH_WriteMask                        0xFFFFFFFF
#define SCREG_LAYER1_DTH_GTABLE_HIGH_ResetValue                       0x417B5EF0

/* Dither threshold value for x,y=0,2.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X0                                   3:0
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X0_End                                 3
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X0_Start                               0
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X0_Type                              U04

/* Dither threshold value for x,y=1,2.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X1                                   7:4
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X1_End                                 7
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X1_Start                               4
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X1_Type                              U04

/* Dither threshold value for x,y=2,2.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X2                                  11:8
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X2_End                                11
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X2_Start                               8
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X2_Type                              U04

/* Dither threshold value for x,y=3,2.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X3                                 15:12
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X3_End                                15
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X3_Start                              12
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y2_X3_Type                              U04

/* Dither threshold value for x,y=0,3.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X0                                 19:16
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X0_End                                19
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X0_Start                              16
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X0_Type                              U04

/* Dither threshold value for x,y=1,3.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X1                                 23:20
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X1_End                                23
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X1_Start                              20
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X1_Type                              U04

/* Dither threshold value for x,y=2,3.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X2                                 27:24
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X2_End                                27
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X2_Start                              24
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X2_Type                              U04

/* Dither threshold value for x,y=3,3.  */
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X3                                 31:28
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X3_End                                31
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X3_Start                              28
#define SCREG_LAYER1_DTH_GTABLE_HIGH_Y3_X3_Type                              U04

/* Register scregLayer1DthBTableLow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for B channel low 32bits.  */

#define scregLayer1DthBTableLowRegAddrs                                  0x584A3
#define SCREG_LAYER1_DTH_BTABLE_LOW_Address                             0x16128C
#define SCREG_LAYER1_DTH_BTABLE_LOW_MSB                                       19
#define SCREG_LAYER1_DTH_BTABLE_LOW_LSB                                        0
#define SCREG_LAYER1_DTH_BTABLE_LOW_BLK                                        0
#define SCREG_LAYER1_DTH_BTABLE_LOW_Count                                      1
#define SCREG_LAYER1_DTH_BTABLE_LOW_FieldMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_BTABLE_LOW_ReadMask                          0xFFFFFFFF
#define SCREG_LAYER1_DTH_BTABLE_LOW_WriteMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_BTABLE_LOW_ResetValue                        0x70E458D1

/* Dither threshold value for x,y=0,0.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X0                                    3:0
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X0_End                                  3
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X0_Start                                0
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X0_Type                               U04

/* Dither threshold value for x,y=1,0.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X1                                    7:4
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X1_End                                  7
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X1_Start                                4
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X1_Type                               U04

/* Dither threshold value for x,y=2,0.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X2                                   11:8
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X2_End                                 11
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X2_Start                                8
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X2_Type                               U04

/* Dither threshold value for x,y=3,0.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X3                                  15:12
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X3_End                                 15
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X3_Start                               12
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y0_X3_Type                               U04

/* Dither threshold value for x,y=0,1.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X0                                  19:16
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X0_End                                 19
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X0_Start                               16
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X0_Type                               U04

/* Dither threshold value for x,y=1,1.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X1                                  23:20
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X1_End                                 23
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X1_Start                               20
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X1_Type                               U04

/* Dither threshold value for x,y=2,1.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X2                                  27:24
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X2_End                                 27
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X2_Start                               24
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X2_Type                               U04

/* Dither threshold value for x,y=3,1.  */
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X3                                  31:28
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X3_End                                 31
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X3_Start                               28
#define SCREG_LAYER1_DTH_BTABLE_LOW_Y1_X3_Type                               U04

/* Register scregLayer1DthBTableHigh **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Display Dither Table Register for B channel high 32bits.  */

#define scregLayer1DthBTableHighRegAddrs                                 0x584A4
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Address                            0x161290
#define SCREG_LAYER1_DTH_BTABLE_HIGH_MSB                                      19
#define SCREG_LAYER1_DTH_BTABLE_HIGH_LSB                                       0
#define SCREG_LAYER1_DTH_BTABLE_HIGH_BLK                                       0
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Count                                     1
#define SCREG_LAYER1_DTH_BTABLE_HIGH_FieldMask                        0xFFFFFFFF
#define SCREG_LAYER1_DTH_BTABLE_HIGH_ReadMask                         0xFFFFFFFF
#define SCREG_LAYER1_DTH_BTABLE_HIGH_WriteMask                        0xFFFFFFFF
#define SCREG_LAYER1_DTH_BTABLE_HIGH_ResetValue                       0xAF9BC263

/* Dither threshold value for x,y=0,2.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X0                                   3:0
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X0_End                                 3
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X0_Start                               0
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X0_Type                              U04

/* Dither threshold value for x,y=1,2.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X1                                   7:4
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X1_End                                 7
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X1_Start                               4
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X1_Type                              U04

/* Dither threshold value for x,y=2,2.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X2                                  11:8
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X2_End                                11
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X2_Start                               8
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X2_Type                              U04

/* Dither threshold value for x,y=3,2.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X3                                 15:12
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X3_End                                15
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X3_Start                              12
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y2_X3_Type                              U04

/* Dither threshold value for x,y=0,3.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X0                                 19:16
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X0_End                                19
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X0_Start                              16
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X0_Type                              U04

/* Dither threshold value for x,y=1,3.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X1                                 23:20
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X1_End                                23
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X1_Start                              20
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X1_Type                              U04

/* Dither threshold value for x,y=2,3.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X2                                 27:24
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X2_End                                27
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X2_Start                              24
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X2_Type                              U04

/* Dither threshold value for x,y=3,3.  */
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X3                                 31:28
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X3_End                                31
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X3_Start                              28
#define SCREG_LAYER1_DTH_BTABLE_HIGH_Y3_X3_Type                              U04

/* Register scregLayer1WbConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer write back Configuration Register.  */

#define scregLayer1WbConfigRegAddrs                                      0x584A5
#define SCREG_LAYER1_WB_CONFIG_Address                                  0x161294
#define SCREG_LAYER1_WB_CONFIG_MSB                                            19
#define SCREG_LAYER1_WB_CONFIG_LSB                                             0
#define SCREG_LAYER1_WB_CONFIG_BLK                                             0
#define SCREG_LAYER1_WB_CONFIG_Count                                           1
#define SCREG_LAYER1_WB_CONFIG_FieldMask                              0x003807FF
#define SCREG_LAYER1_WB_CONFIG_ReadMask                               0x003807FF
#define SCREG_LAYER1_WB_CONFIG_WriteMask                              0x003807FF
#define SCREG_LAYER1_WB_CONFIG_ResetValue                             0x00000000

/* Layer tile mode  */
#define SCREG_LAYER1_WB_CONFIG_TILE_MODE                                     3:0
#define SCREG_LAYER1_WB_CONFIG_TILE_MODE_End                                   3
#define SCREG_LAYER1_WB_CONFIG_TILE_MODE_Start                                 0
#define SCREG_LAYER1_WB_CONFIG_TILE_MODE_Type                                U04
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_LINEAR                            0x0
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_TILED32X2                         0x1
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_TILED16X4                         0x2
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_TILED32X4                         0x3
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_TILED32X8                         0x4
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_TILED16X8                         0x5
#define   SCREG_LAYER1_WB_CONFIG_TILE_MODE_TILED8X8                          0x6

/* The format of the layer.  */
#define SCREG_LAYER1_WB_CONFIG_FORMAT                                        9:4
#define SCREG_LAYER1_WB_CONFIG_FORMAT_End                                      9
#define SCREG_LAYER1_WB_CONFIG_FORMAT_Start                                    4
#define SCREG_LAYER1_WB_CONFIG_FORMAT_Type                                   U06
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_A8R8G8B8                            0x00
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_X8R8G8B8                            0x01
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_A2R10G10B10                         0x02
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_X2R10G10B10                         0x03
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_R8G8B8                              0x04
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_R5G6B5                              0x05
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_A1R5G5B5                            0x06
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_X1R5G5B5                            0x07
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_A4R4G4B4                            0x08
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_X4R4G4B4                            0x09
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_FP16                                0x0A
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_YUY2                                0x0B
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_UYVY                                0x0C
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_YV12                                0x0D
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_NV12                                0x0E
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_NV16                                0x0F
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_P010                                0x10
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_P210                                0x11
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_YUV420_PACKED_10BIT                 0x12
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_YV12_10BIT_MSB                      0x13
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_YUY2_10BIT                          0x14
#define   SCREG_LAYER1_WB_CONFIG_FORMAT_UYVY_10BIT                          0x15

/* Compress enable.  */
#define SCREG_LAYER1_WB_CONFIG_COMPRESS_ENC                                10:10
#define SCREG_LAYER1_WB_CONFIG_COMPRESS_ENC_End                               10
#define SCREG_LAYER1_WB_CONFIG_COMPRESS_ENC_Start                             10
#define SCREG_LAYER1_WB_CONFIG_COMPRESS_ENC_Type                             U01
#define   SCREG_LAYER1_WB_CONFIG_COMPRESS_ENC_DISABLED                       0x0
#define   SCREG_LAYER1_WB_CONFIG_COMPRESS_ENC_ENABLED                        0x1

/* Swizzle.  */
#define SCREG_LAYER1_WB_CONFIG_SWIZZLE                                     20:19
#define SCREG_LAYER1_WB_CONFIG_SWIZZLE_End                                    20
#define SCREG_LAYER1_WB_CONFIG_SWIZZLE_Start                                  19
#define SCREG_LAYER1_WB_CONFIG_SWIZZLE_Type                                  U02
#define   SCREG_LAYER1_WB_CONFIG_SWIZZLE_ARGB                                0x0
#define   SCREG_LAYER1_WB_CONFIG_SWIZZLE_RGBA                                0x1
#define   SCREG_LAYER1_WB_CONFIG_SWIZZLE_ABGR                                0x2
#define   SCREG_LAYER1_WB_CONFIG_SWIZZLE_BGRA                                0x3

/* UV swizzle.  */
#define SCREG_LAYER1_WB_CONFIG_UV_SWIZZLE                                  21:21
#define SCREG_LAYER1_WB_CONFIG_UV_SWIZZLE_End                                 21
#define SCREG_LAYER1_WB_CONFIG_UV_SWIZZLE_Start                               21
#define SCREG_LAYER1_WB_CONFIG_UV_SWIZZLE_Type                               U01
#define   SCREG_LAYER1_WB_CONFIG_UV_SWIZZLE_UV                               0x0
#define   SCREG_LAYER1_WB_CONFIG_UV_SWIZZLE_VU                               0x1

/* Register scregLayer1WbSize **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Write back image size.  */

#define scregLayer1WbSizeRegAddrs                                        0x584A6
#define SCREG_LAYER1_WB_SIZE_Address                                    0x161298
#define SCREG_LAYER1_WB_SIZE_MSB                                              19
#define SCREG_LAYER1_WB_SIZE_LSB                                               0
#define SCREG_LAYER1_WB_SIZE_BLK                                               0
#define SCREG_LAYER1_WB_SIZE_Count                                             1
#define SCREG_LAYER1_WB_SIZE_FieldMask                                0xFFFFFFFF
#define SCREG_LAYER1_WB_SIZE_ReadMask                                 0xFFFFFFFF
#define SCREG_LAYER1_WB_SIZE_WriteMask                                0xFFFFFFFF
#define SCREG_LAYER1_WB_SIZE_ResetValue                               0x00000000

/* Image width.  */
#define SCREG_LAYER1_WB_SIZE_WIDTH                                          15:0
#define SCREG_LAYER1_WB_SIZE_WIDTH_End                                        15
#define SCREG_LAYER1_WB_SIZE_WIDTH_Start                                       0
#define SCREG_LAYER1_WB_SIZE_WIDTH_Type                                      U16

/* Image height.  */
#define SCREG_LAYER1_WB_SIZE_HEIGHT                                        31:16
#define SCREG_LAYER1_WB_SIZE_HEIGHT_End                                       31
#define SCREG_LAYER1_WB_SIZE_HEIGHT_Start                                     16
#define SCREG_LAYER1_WB_SIZE_HEIGHT_Type                                     U16

/* Register scregLayer1R2yConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer R2Y configuration.  */

#define scregLayer1R2yConfigRegAddrs                                     0x584A7
#define SCREG_LAYER1_R2Y_CONFIG_Address                                 0x16129C
#define SCREG_LAYER1_R2Y_CONFIG_MSB                                           19
#define SCREG_LAYER1_R2Y_CONFIG_LSB                                            0
#define SCREG_LAYER1_R2Y_CONFIG_BLK                                            0
#define SCREG_LAYER1_R2Y_CONFIG_Count                                          1
#define SCREG_LAYER1_R2Y_CONFIG_FieldMask                             0x000000F7
#define SCREG_LAYER1_R2Y_CONFIG_ReadMask                              0x000000F7
#define SCREG_LAYER1_R2Y_CONFIG_WriteMask                             0x000000F7
#define SCREG_LAYER1_R2Y_CONFIG_ResetValue                            0x00000000

/* The mode of the CSC.  */
#define SCREG_LAYER1_R2Y_CONFIG_MODE                                         2:0
#define SCREG_LAYER1_R2Y_CONFIG_MODE_End                                       2
#define SCREG_LAYER1_R2Y_CONFIG_MODE_Start                                     0
#define SCREG_LAYER1_R2Y_CONFIG_MODE_Type                                    U03
#define   SCREG_LAYER1_R2Y_CONFIG_MODE_PROGRAMMABLE                          0x0
#define   SCREG_LAYER1_R2Y_CONFIG_MODE_LIMIT_RGB_2_LIMIT_YUV                 0x1
#define   SCREG_LAYER1_R2Y_CONFIG_MODE_LIMIT_RGB_2_FULL_YUV                  0x2
#define   SCREG_LAYER1_R2Y_CONFIG_MODE_FULL_RGB_2_LIMIT_YUV                  0x3
#define   SCREG_LAYER1_R2Y_CONFIG_MODE_FULL_RGB_2_FULL_YUV                   0x4

/* The mode of the Color Gamut.  */
#define SCREG_LAYER1_R2Y_CONFIG_GAMUT                                        7:4
#define SCREG_LAYER1_R2Y_CONFIG_GAMUT_End                                      7
#define SCREG_LAYER1_R2Y_CONFIG_GAMUT_Start                                    4
#define SCREG_LAYER1_R2Y_CONFIG_GAMUT_Type                                   U04
#define   SCREG_LAYER1_R2Y_CONFIG_GAMUT_BT601                                0x0
#define   SCREG_LAYER1_R2Y_CONFIG_GAMUT_BT709                                0x1
#define   SCREG_LAYER1_R2Y_CONFIG_GAMUT_BT2020                               0x2
#define   SCREG_LAYER1_R2Y_CONFIG_GAMUT_P3                                   0x3
#define   SCREG_LAYER1_R2Y_CONFIG_GAMUT_SRGB                                 0x4

/* Register scregLayer1RGBToYUVCoef0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 0 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef0RegAddrs                                 0x584A8
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_Address                           0x1612A0
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_ResetValue                      0x00000400

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF0_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 1 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef1RegAddrs                                 0x584A9
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_Address                           0x1612A4
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF1_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 2 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef2RegAddrs                                 0x584AA
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_Address                           0x1612A8
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF2_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef3 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 3 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef3RegAddrs                                 0x584AB
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_Address                           0x1612AC
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF3_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef4 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 4 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef4RegAddrs                                 0x584AC
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_Address                           0x1612B0
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_ResetValue                      0x00000400

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF4_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef5 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 5 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef5RegAddrs                                 0x584AD
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_Address                           0x1612B4
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF5_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef6 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 6 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef6RegAddrs                                 0x584AE
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_Address                           0x1612B8
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF6_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef7 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 7 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef7RegAddrs                                 0x584AF
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_Address                           0x1612BC
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF7_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoef8 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Frame RGB to YUV Custom Coefficients 8 Register.  User defined RGB2YUV     **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoef8RegAddrs                                 0x584B0
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_Address                           0x1612C0
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_MSB                                     19
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_LSB                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_BLK                                      0
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_Count                                    1
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_FieldMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_ReadMask                        0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_WriteMask                       0x0007FFFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_ResetValue                      0x00000400

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_VALUE                                 18:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_VALUE_End                               18
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_VALUE_Start                              0
#define SCREG_LAYER1_RGB_TO_YUV_COEF8_VALUE_Type                             U19

/* Register scregLayer1RGBToYUVCoefPreOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset0 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoefPreOffset0RegAddrs                        0x584B1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_Address                0x1612C4
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_MSB                          19
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_LSB                           0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_BLK                           0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_Count                         1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_FieldMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_ReadMask             0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_WriteMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE                      12:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE_End                    12
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE_Start                   0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET0_VALUE_Type                  U13

/* Register scregLayer1RGBToYUVCoefPreOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset1 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoefPreOffset1RegAddrs                        0x584B2
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_Address                0x1612C8
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_MSB                          19
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_LSB                           0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_BLK                           0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_Count                         1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_FieldMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_ReadMask             0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_WriteMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE                      12:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE_End                    12
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE_Start                   0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET1_VALUE_Type                  U13

/* Register scregLayer1RGBToYUVCoefPreOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Pre Offset2 Register.  User defined YUV2RGB **
** coefficient.                                                               */

#define scregLayer1RGBToYUVCoefPreOffset2RegAddrs                        0x584B3
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_Address                0x1612CC
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_MSB                          19
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_LSB                           0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_BLK                           0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_Count                         1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_FieldMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_ReadMask             0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_WriteMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_ResetValue           0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE                      12:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE_End                    12
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE_Start                   0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_PRE_OFFSET2_VALUE_Type                  U13

/* Register scregLayer1RGBToYUVCoefPostOffset0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset0 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer1RGBToYUVCoefPostOffset0RegAddrs                       0x584B4
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_Address               0x1612D0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_MSB                         19
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_LSB                          0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_BLK                          0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_Count                        1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_FieldMask           0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_ReadMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_WriteMask           0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE                     12:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE_End                   12
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE_Start                  0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET0_VALUE_Type                 U13

/* Register scregLayer1RGBToYUVCoefPostOffset1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset1 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer1RGBToYUVCoefPostOffset1RegAddrs                       0x584B5
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_Address               0x1612D4
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_MSB                         19
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_LSB                          0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_BLK                          0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_Count                        1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_FieldMask           0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_ReadMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_WriteMask           0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE                     12:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE_End                   12
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE_Start                  0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET1_VALUE_Type                 U13

/* Register scregLayer1RGBToYUVCoefPostOffset2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer YUV to RGB Custom Matrix Post Offset2 Register.  User defined        **
** YUV2RGB coefficient.                                                       */

#define scregLayer1RGBToYUVCoefPostOffset2RegAddrs                       0x584B6
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_Address               0x1612D8
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_MSB                         19
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_LSB                          0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_BLK                          0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_Count                        1
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_FieldMask           0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_ReadMask            0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_WriteMask           0x00001FFF
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_ResetValue          0x00000000

/* Value.  */
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE                     12:0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE_End                   12
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE_Start                  0
#define SCREG_LAYER1_RGB_TO_YUV_COEF_POST_OFFSET2_VALUE_Type                 U13

/* Register scregLayer1WdmaAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register.  Address.  */

#define scregLayer1WdmaAddressRegAddrs                                   0x584B7
#define SCREG_LAYER1_WDMA_ADDRESS_Address                               0x1612DC
#define SCREG_LAYER1_WDMA_ADDRESS_MSB                                         19
#define SCREG_LAYER1_WDMA_ADDRESS_LSB                                          0
#define SCREG_LAYER1_WDMA_ADDRESS_BLK                                          0
#define SCREG_LAYER1_WDMA_ADDRESS_Count                                        1
#define SCREG_LAYER1_WDMA_ADDRESS_FieldMask                           0xFFFFFFFF
#define SCREG_LAYER1_WDMA_ADDRESS_ReadMask                            0xFFFFFFFF
#define SCREG_LAYER1_WDMA_ADDRESS_WriteMask                           0xFFFFFFFF
#define SCREG_LAYER1_WDMA_ADDRESS_ResetValue                          0x00000000

#define SCREG_LAYER1_WDMA_ADDRESS_ADDRESS                                   31:0
#define SCREG_LAYER1_WDMA_ADDRESS_ADDRESS_End                                 31
#define SCREG_LAYER1_WDMA_ADDRESS_ADDRESS_Start                                0
#define SCREG_LAYER1_WDMA_ADDRESS_ADDRESS_Type                               U32

/* Register scregLayer1WdmaHAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for high 4 bits.  High address.  */

#define scregLayer1WdmaHAddressRegAddrs                                  0x584B8
#define SCREG_LAYER1_WDMA_HADDRESS_Address                              0x1612E0
#define SCREG_LAYER1_WDMA_HADDRESS_MSB                                        19
#define SCREG_LAYER1_WDMA_HADDRESS_LSB                                         0
#define SCREG_LAYER1_WDMA_HADDRESS_BLK                                         0
#define SCREG_LAYER1_WDMA_HADDRESS_Count                                       1
#define SCREG_LAYER1_WDMA_HADDRESS_FieldMask                          0x000000FF
#define SCREG_LAYER1_WDMA_HADDRESS_ReadMask                           0x000000FF
#define SCREG_LAYER1_WDMA_HADDRESS_WriteMask                          0x000000FF
#define SCREG_LAYER1_WDMA_HADDRESS_ResetValue                         0x00000000

#define SCREG_LAYER1_WDMA_HADDRESS_ADDRESS                                   7:0
#define SCREG_LAYER1_WDMA_HADDRESS_ADDRESS_End                                 7
#define SCREG_LAYER1_WDMA_HADDRESS_ADDRESS_Start                               0
#define SCREG_LAYER1_WDMA_HADDRESS_ADDRESS_Type                              U08

/* Register scregLayer1WdmaUPlaneAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for U plane.  Address.  */

#define scregLayer1WdmaUPlaneAddressRegAddrs                             0x584B9
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_Address                        0x1612E4
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_MSB                                  19
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_LSB                                   0
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_BLK                                   0
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_Count                                 1
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_WriteMask                    0xFFFFFFFF
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_ResetValue                   0x00000000

#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_ADDRESS                            31:0
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_ADDRESS_End                          31
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_ADDRESS_Start                         0
#define SCREG_LAYER1_WDMA_UPLANE_ADDRESS_ADDRESS_Type                        U32

/* Register scregLayer1WdmaUPlaneHAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for U plane high 4 bits.  High address.  */

#define scregLayer1WdmaUPlaneHAddressRegAddrs                            0x584BA
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_Address                       0x1612E8
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_MSB                                 19
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_LSB                                  0
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_BLK                                  0
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_Count                                1
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_FieldMask                   0x000000FF
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_ReadMask                    0x000000FF
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_WriteMask                   0x000000FF
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_ResetValue                  0x00000000

#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_ADDRESS                            7:0
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_ADDRESS_End                          7
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_ADDRESS_Start                        0
#define SCREG_LAYER1_WDMA_UPLANE_HADDRESS_ADDRESS_Type                       U08

/* Register scregLayer1WdmaVPlaneAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for V plane.  Address.  */

#define scregLayer1WdmaVPlaneAddressRegAddrs                             0x584BB
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_Address                        0x1612EC
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_MSB                                  19
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_LSB                                   0
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_BLK                                   0
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_Count                                 1
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_WriteMask                    0xFFFFFFFF
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_ResetValue                   0x00000000

#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_ADDRESS                            31:0
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_ADDRESS_End                          31
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_ADDRESS_Start                         0
#define SCREG_LAYER1_WDMA_VPLANE_ADDRESS_ADDRESS_Type                        U32

/* Register scregLayer1WdmaVPlaneHAddress **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Start Address Register for V plane high 4 bits.  High address.  */

#define scregLayer1WdmaVPlaneHAddressRegAddrs                            0x584BC
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_Address                       0x1612F0
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_MSB                                 19
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_LSB                                  0
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_BLK                                  0
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_Count                                1
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_FieldMask                   0x000000FF
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_ReadMask                    0x000000FF
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_WriteMask                   0x000000FF
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_ResetValue                  0x00000000

#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_ADDRESS                            7:0
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_ADDRESS_End                          7
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_ADDRESS_Start                        0
#define SCREG_LAYER1_WDMA_VPLANE_HADDRESS_ADDRESS_Type                       U08

/* Register scregLayer1WdmaStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA Stride Register.  */

#define scregLayer1WdmaStrideRegAddrs                                    0x584BD
#define SCREG_LAYER1_WDMA_STRIDE_Address                                0x1612F4
#define SCREG_LAYER1_WDMA_STRIDE_MSB                                          19
#define SCREG_LAYER1_WDMA_STRIDE_LSB                                           0
#define SCREG_LAYER1_WDMA_STRIDE_BLK                                           0
#define SCREG_LAYER1_WDMA_STRIDE_Count                                         1
#define SCREG_LAYER1_WDMA_STRIDE_FieldMask                            0x0003FFFF
#define SCREG_LAYER1_WDMA_STRIDE_ReadMask                             0x0003FFFF
#define SCREG_LAYER1_WDMA_STRIDE_WriteMask                            0x0003FFFF
#define SCREG_LAYER1_WDMA_STRIDE_ResetValue                           0x00000000

/* Destination stride.  */
#define SCREG_LAYER1_WDMA_STRIDE_VALUE                                      17:0
#define SCREG_LAYER1_WDMA_STRIDE_VALUE_End                                    17
#define SCREG_LAYER1_WDMA_STRIDE_VALUE_Start                                   0
#define SCREG_LAYER1_WDMA_STRIDE_VALUE_Type                                  U18

/* Register scregLayer1WdmaUPlaneStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA U Plane Stride Register.  */

#define scregLayer1WdmaUPlaneStrideRegAddrs                              0x584BE
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_Address                         0x1612F8
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_MSB                                   19
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_LSB                                    0
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_BLK                                    0
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_Count                                  1
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_FieldMask                     0x0003FFFF
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_ReadMask                      0x0003FFFF
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_WriteMask                     0x0003FFFF
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_ResetValue                    0x00000000

/* U plane stride.  */
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_VALUE                               17:0
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_VALUE_End                             17
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_VALUE_Start                            0
#define SCREG_LAYER1_WDMA_UPLANE_STRIDE_VALUE_Type                           U18

/* Register scregLayer1WdmaVPlaneStride **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* WDMA V Plane Stride Register.  */

#define scregLayer1WdmaVPlaneStrideRegAddrs                              0x584BF
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_Address                         0x1612FC
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_MSB                                   19
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_LSB                                    0
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_BLK                                    0
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_Count                                  1
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_FieldMask                     0x0003FFFF
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_ReadMask                      0x0003FFFF
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_WriteMask                     0x0003FFFF
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_ResetValue                    0x00000000

/* V plane stride.  */
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_VALUE                               17:0
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_VALUE_End                             17
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_VALUE_Start                            0
#define SCREG_LAYER1_WDMA_VPLANE_STRIDE_VALUE_Type                           U18

/* Register scregLayer1CrcStart **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 CRC start signal.  */

#define scregLayer1CrcStartRegAddrs                                      0x584C0
#define SCREG_LAYER1_CRC_START_Address                                  0x161300
#define SCREG_LAYER1_CRC_START_MSB                                            19
#define SCREG_LAYER1_CRC_START_LSB                                             0
#define SCREG_LAYER1_CRC_START_BLK                                             0
#define SCREG_LAYER1_CRC_START_Count                                           1
#define SCREG_LAYER1_CRC_START_FieldMask                              0x00000001
#define SCREG_LAYER1_CRC_START_ReadMask                               0x00000001
#define SCREG_LAYER1_CRC_START_WriteMask                              0x00000001
#define SCREG_LAYER1_CRC_START_ResetValue                             0x00000000

/* Value.  */
#define SCREG_LAYER1_CRC_START_START                                         0:0
#define SCREG_LAYER1_CRC_START_START_End                                       0
#define SCREG_LAYER1_CRC_START_START_Start                                     0
#define SCREG_LAYER1_CRC_START_START_Type                                    U01

/* Register scregLayer1DfcAlphaCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 walker alpha CRC seed.  */

#define scregLayer1DfcAlphaCrcSeedRegAddrs                               0x584C1
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_Address                         0x161304
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_MSB                                   19
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_LSB                                    0
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_BLK                                    0
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_Count                                  1
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_FieldMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_ReadMask                      0xFFFFFFFF
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_WriteMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_ResetValue                    0x00000000

/* Value.  */
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_VALUE                               31:0
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_VALUE_End                             31
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_VALUE_Start                            0
#define SCREG_LAYER1_DFC_ALPHA_CRC_SEED_VALUE_Type                           U32

/* Register scregLayer1DfcRedCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 walker red CRC seed.  */

#define scregLayer1DfcRedCrcSeedRegAddrs                                 0x584C2
#define SCREG_LAYER1_DFC_RED_CRC_SEED_Address                           0x161308
#define SCREG_LAYER1_DFC_RED_CRC_SEED_MSB                                     19
#define SCREG_LAYER1_DFC_RED_CRC_SEED_LSB                                      0
#define SCREG_LAYER1_DFC_RED_CRC_SEED_BLK                                      0
#define SCREG_LAYER1_DFC_RED_CRC_SEED_Count                                    1
#define SCREG_LAYER1_DFC_RED_CRC_SEED_FieldMask                       0xFFFFFFFF
#define SCREG_LAYER1_DFC_RED_CRC_SEED_ReadMask                        0xFFFFFFFF
#define SCREG_LAYER1_DFC_RED_CRC_SEED_WriteMask                       0xFFFFFFFF
#define SCREG_LAYER1_DFC_RED_CRC_SEED_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_DFC_RED_CRC_SEED_VALUE                                 31:0
#define SCREG_LAYER1_DFC_RED_CRC_SEED_VALUE_End                               31
#define SCREG_LAYER1_DFC_RED_CRC_SEED_VALUE_Start                              0
#define SCREG_LAYER1_DFC_RED_CRC_SEED_VALUE_Type                             U32

/* Register scregLayer1DfcGreenCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 walker green CRC seed.  */

#define scregLayer1DfcGreenCrcSeedRegAddrs                               0x584C3
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_Address                         0x16130C
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_MSB                                   19
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_LSB                                    0
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_BLK                                    0
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_Count                                  1
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_FieldMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_ReadMask                      0xFFFFFFFF
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_WriteMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_ResetValue                    0x00000000

/* Value.  */
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_VALUE                               31:0
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_VALUE_End                             31
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_VALUE_Start                            0
#define SCREG_LAYER1_DFC_GREEN_CRC_SEED_VALUE_Type                           U32

/* Register scregLayer1DfcBlueCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer1 walker blue CRC seed.  */

#define scregLayer1DfcBlueCrcSeedRegAddrs                                0x584C4
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_Address                          0x161310
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_MSB                                    19
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_LSB                                     0
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_BLK                                     0
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_Count                                   1
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_FieldMask                      0xFFFFFFFF
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_ReadMask                       0xFFFFFFFF
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_WriteMask                      0xFFFFFFFF
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_ResetValue                     0x00000000

/* Value.  */
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_VALUE                                31:0
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_VALUE_End                              31
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_VALUE_Start                             0
#define SCREG_LAYER1_DFC_BLUE_CRC_SEED_VALUE_Type                            U32

/* Register scregLayer1DfcAlphaCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer1 Walker Alpha CRC value.  */

#define scregLayer1DfcAlphaCrcValueRegAddrs                              0x584C5
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_Address                        0x161314
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_MSB                                  19
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_LSB                                   0
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_BLK                                   0
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_Count                                 1
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_WriteMask                    0x00000000
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_ResetValue                   0x00000000

/* Layer1 calculated alpha crc value.  */
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_DATA                               31:0
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_DATA_End                             31
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_DATA_Start                            0
#define SCREG_LAYER1_DFC_ALPHA_CRC_VALUE_DATA_Type                           U32

/* Register scregLayer1DfcRedCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer1 Walker Red CRC value.  */

#define scregLayer1DfcRedCrcValueRegAddrs                                0x584C6
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_Address                          0x161318
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_MSB                                    19
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_LSB                                     0
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_BLK                                     0
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_Count                                   1
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_FieldMask                      0xFFFFFFFF
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_ReadMask                       0xFFFFFFFF
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_WriteMask                      0x00000000
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_ResetValue                     0x00000000

/* Layer1 calculated red crc value.  */
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_DATA                                 31:0
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_DATA_End                               31
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_DATA_Start                              0
#define SCREG_LAYER1_DFC_RED_CRC_VALUE_DATA_Type                             U32

/* Register scregLayer1DfcGreenCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer1 Walker Green CRC value.  */

#define scregLayer1DfcGreenCrcValueRegAddrs                              0x584C7
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_Address                        0x16131C
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_MSB                                  19
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_LSB                                   0
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_BLK                                   0
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_Count                                 1
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_WriteMask                    0x00000000
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_ResetValue                   0x00000000

/* Layer1 calculated green crc value.  */
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_DATA                               31:0
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_DATA_End                             31
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_DATA_Start                            0
#define SCREG_LAYER1_DFC_GREEN_CRC_VALUE_DATA_Type                           U32

/* Register scregLayer1DfcBlueCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated layer1 Walker Blue CRC value.  */

#define scregLayer1DfcBlueCrcValueRegAddrs                               0x584C8
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_Address                         0x161320
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_MSB                                   19
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_LSB                                    0
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_BLK                                    0
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_Count                                  1
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_FieldMask                     0xFFFFFFFF
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_ReadMask                      0xFFFFFFFF
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_WriteMask                     0x00000000
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_ResetValue                    0x00000000

/* Layer1 calculated blue crc value.  */
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_DATA                                31:0
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_DATA_End                              31
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_DATA_Start                             0
#define SCREG_LAYER1_DFC_BLUE_CRC_VALUE_DATA_Type                            U32

/* Register scregLayer1WbSwizzleAlphaCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb alpha CRC seed.  */

#define scregLayer1WbSwizzleAlphaCrcSeedRegAddrs                         0x584C9
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_Address                  0x161324
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_MSB                            19
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_LSB                             0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_BLK                             0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_Count                           1
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_ResetValue             0x00000000

/* Value.  */
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE                        31:0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE_End                      31
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE_Start                     0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_SEED_VALUE_Type                    U32

/* Register scregLayer1WbSwizzleRedCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb red CRC seed.  */

#define scregLayer1WbSwizzleRedCrcSeedRegAddrs                           0x584CA
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_Address                    0x161328
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_MSB                              19
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_LSB                               0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_BLK                               0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_Count                             1
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_FieldMask                0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_ReadMask                 0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_WriteMask                0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_ResetValue               0x00000000

/* Value.  */
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_VALUE                          31:0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_VALUE_End                        31
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_VALUE_Start                       0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_SEED_VALUE_Type                      U32

/* Register scregLayer1WbSwizzleGreenCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb green CRC seed.  */

#define scregLayer1WbSwizzleGreenCrcSeedRegAddrs                         0x584CB
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_Address                  0x16132C
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_MSB                            19
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_LSB                             0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_BLK                             0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_Count                           1
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_ResetValue             0x00000000

/* Value.  */
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_VALUE                        31:0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_VALUE_End                      31
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_VALUE_Start                     0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_SEED_VALUE_Type                    U32

/* Register scregLayer1WbSwizzleBlueCrcSeed **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Wb blue CRC seed.  */

#define scregLayer1WbSwizzleBlueCrcSeedRegAddrs                          0x584CC
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_Address                   0x161330
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_MSB                             19
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_LSB                              0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_BLK                              0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_Count                            1
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_FieldMask               0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_ReadMask                0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_WriteMask               0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_ResetValue              0x00000000

/* Value.  */
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_VALUE                         31:0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_VALUE_End                       31
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_VALUE_Start                      0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_SEED_VALUE_Type                     U32

/* Register scregLayer1WbSwizzleAlphaCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Alpha CRC value.  */

#define scregLayer1WbSwizzleAlphaCrcValueRegAddrs                        0x584CD
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_Address                 0x161334
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_MSB                           19
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_LSB                            0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_BLK                            0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_Count                          1
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_WriteMask             0x00000000
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_ResetValue            0x00000000

/* Wb calculated alpha crc value.  */
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA                        31:0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA_End                      31
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA_Start                     0
#define SCREG_LAYER1_WB_SWIZZLE_ALPHA_CRC_VALUE_DATA_Type                    U32

/* Register scregLayer1WbSwizzleRedCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Red CRC value.  */

#define scregLayer1WbSwizzleRedCrcValueRegAddrs                          0x584CE
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_Address                   0x161338
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_MSB                             19
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_LSB                              0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_BLK                              0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_Count                            1
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_FieldMask               0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_ReadMask                0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_WriteMask               0x00000000
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_ResetValue              0x00000000

/* Wb calculated red crc value.  */
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_DATA                          31:0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_DATA_End                        31
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_DATA_Start                       0
#define SCREG_LAYER1_WB_SWIZZLE_RED_CRC_VALUE_DATA_Type                      U32

/* Register scregLayer1WbSwizzleGreenCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Green CRC value.  */

#define scregLayer1WbSwizzleGreenCrcValueRegAddrs                        0x584CF
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_Address                 0x16133C
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_MSB                           19
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_LSB                            0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_BLK                            0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_Count                          1
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_WriteMask             0x00000000
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_ResetValue            0x00000000

/* Wb calculated green crc value.  */
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_DATA                        31:0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_DATA_End                      31
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_DATA_Start                     0
#define SCREG_LAYER1_WB_SWIZZLE_GREEN_CRC_VALUE_DATA_Type                    U32

/* Register scregLayer1WbSwizzleBlueCrcValue **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Calculated Wb Blue CRC value.  */

#define scregLayer1WbSwizzleBlueCrcValueRegAddrs                         0x584D0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_Address                  0x161340
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_MSB                            19
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_LSB                             0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_BLK                             0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_Count                           1
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_WriteMask              0x00000000
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_ResetValue             0x00000000

/* Wb calculated blue crc value.  */
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_DATA                         31:0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_DATA_End                       31
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_DATA_Start                      0
#define SCREG_LAYER1_WB_SWIZZLE_BLUE_CRC_VALUE_DATA_Type                     U32

/* Register scregLayer1RdEarlyWkupNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI RD Bus once early wakeup bursty number.  */

#define scregLayer1RdEarlyWkupNumberRegAddrs                             0x584D1
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_Address                       0x161344
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_MSB                                 19
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_LSB                                  0
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_BLK                                  0
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_Count                                1
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_FieldMask                   0x0000007F
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_ReadMask                    0x0000007F
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_WriteMask                   0x0000007F
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_ResetValue                  0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_VALUE                              6:0
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_VALUE_End                            6
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_VALUE_Start                          0
#define SCREG_LAYER1_RD_EARLY_WKUP_NUMBER_VALUE_Type                         U07

/* Register scregLayer1RdBurstyNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI RD Bus once bursty number.  */

#define scregLayer1RdBurstyNumberRegAddrs                                0x584D2
#define SCREG_LAYER1_RD_BURSTY_NUMBER_Address                           0x161348
#define SCREG_LAYER1_RD_BURSTY_NUMBER_MSB                                     19
#define SCREG_LAYER1_RD_BURSTY_NUMBER_LSB                                      0
#define SCREG_LAYER1_RD_BURSTY_NUMBER_BLK                                      0
#define SCREG_LAYER1_RD_BURSTY_NUMBER_Count                                    1
#define SCREG_LAYER1_RD_BURSTY_NUMBER_FieldMask                       0x0000007F
#define SCREG_LAYER1_RD_BURSTY_NUMBER_ReadMask                        0x0000007F
#define SCREG_LAYER1_RD_BURSTY_NUMBER_WriteMask                       0x0000007F
#define SCREG_LAYER1_RD_BURSTY_NUMBER_ResetValue                      0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_BURSTY_NUMBER_VALUE                                  6:0
#define SCREG_LAYER1_RD_BURSTY_NUMBER_VALUE_End                                6
#define SCREG_LAYER1_RD_BURSTY_NUMBER_VALUE_Start                              0
#define SCREG_LAYER1_RD_BURSTY_NUMBER_VALUE_Type                             U07

/* Register scregLayer1RdOTNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI RD Bus ot number threshold.  */

#define scregLayer1RdOTNumberRegAddrs                                    0x584D3
#define SCREG_LAYER1_RD_OT_NUMBER_Address                               0x16134C
#define SCREG_LAYER1_RD_OT_NUMBER_MSB                                         19
#define SCREG_LAYER1_RD_OT_NUMBER_LSB                                          0
#define SCREG_LAYER1_RD_OT_NUMBER_BLK                                          0
#define SCREG_LAYER1_RD_OT_NUMBER_Count                                        1
#define SCREG_LAYER1_RD_OT_NUMBER_FieldMask                           0x0000007F
#define SCREG_LAYER1_RD_OT_NUMBER_ReadMask                            0x0000007F
#define SCREG_LAYER1_RD_OT_NUMBER_WriteMask                           0x0000007F
#define SCREG_LAYER1_RD_OT_NUMBER_ResetValue                          0x00000010

/* Value.  */
#define SCREG_LAYER1_RD_OT_NUMBER_VALUE                                      6:0
#define SCREG_LAYER1_RD_OT_NUMBER_VALUE_End                                    6
#define SCREG_LAYER1_RD_OT_NUMBER_VALUE_Start                                  0
#define SCREG_LAYER1_RD_OT_NUMBER_VALUE_Type                                 U07

/* Register scregLayer1WrOTNumber **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* AXI WR Bus ot number threshold.  */

#define scregLayer1WrOTNumberRegAddrs                                    0x584D4
#define SCREG_LAYER1_WR_OT_NUMBER_Address                               0x161350
#define SCREG_LAYER1_WR_OT_NUMBER_MSB                                         19
#define SCREG_LAYER1_WR_OT_NUMBER_LSB                                          0
#define SCREG_LAYER1_WR_OT_NUMBER_BLK                                          0
#define SCREG_LAYER1_WR_OT_NUMBER_Count                                        1
#define SCREG_LAYER1_WR_OT_NUMBER_FieldMask                           0x0000007F
#define SCREG_LAYER1_WR_OT_NUMBER_ReadMask                            0x0000007F
#define SCREG_LAYER1_WR_OT_NUMBER_WriteMask                           0x0000007F
#define SCREG_LAYER1_WR_OT_NUMBER_ResetValue                          0x00000010

/* Value.  */
#define SCREG_LAYER1_WR_OT_NUMBER_VALUE                                      6:0
#define SCREG_LAYER1_WR_OT_NUMBER_VALUE_End                                    6
#define SCREG_LAYER1_WR_OT_NUMBER_VALUE_Start                                  0
#define SCREG_LAYER1_WR_OT_NUMBER_VALUE_Type                                 U07

/* Register scregLayer1RdBwConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer axi read bandwidth Configuration Register.  */

#define scregLayer1RdBwConfigRegAddrs                                    0x584D5
#define SCREG_LAYER1_RD_BW_CONFIG_Address                               0x161354
#define SCREG_LAYER1_RD_BW_CONFIG_MSB                                         19
#define SCREG_LAYER1_RD_BW_CONFIG_LSB                                          0
#define SCREG_LAYER1_RD_BW_CONFIG_BLK                                          0
#define SCREG_LAYER1_RD_BW_CONFIG_Count                                        1
#define SCREG_LAYER1_RD_BW_CONFIG_FieldMask                           0x00000003
#define SCREG_LAYER1_RD_BW_CONFIG_ReadMask                            0x00000003
#define SCREG_LAYER1_RD_BW_CONFIG_WriteMask                           0x00000003
#define SCREG_LAYER1_RD_BW_CONFIG_ResetValue                          0x00000000

/* Bandwidth monitor enable.  */
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE                   0:0
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_End                 0
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Start               0
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Type              U01

/* Bandwidth monitor mode. 0 means output average count, 1 means ooutput peak **
** count.                                                                     */
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE                     1:1
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE_End                   1
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Start                 1
#define SCREG_LAYER1_RD_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Type                U01

/* Register scregLayer1RdBwWindow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor indicates number of blocks. 0 means 1 block, 1means 2    **
** block...f means 16 block.largest value is f.                               */

#define scregLayer1RdBwWindowRegAddrs                                    0x584D6
#define SCREG_LAYER1_RD_BW_WINDOW_Address                               0x161358
#define SCREG_LAYER1_RD_BW_WINDOW_MSB                                         19
#define SCREG_LAYER1_RD_BW_WINDOW_LSB                                          0
#define SCREG_LAYER1_RD_BW_WINDOW_BLK                                          0
#define SCREG_LAYER1_RD_BW_WINDOW_Count                                        1
#define SCREG_LAYER1_RD_BW_WINDOW_FieldMask                           0x0000000F
#define SCREG_LAYER1_RD_BW_WINDOW_ReadMask                            0x0000000F
#define SCREG_LAYER1_RD_BW_WINDOW_WriteMask                           0x0000000F
#define SCREG_LAYER1_RD_BW_WINDOW_ResetValue                          0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_BW_WINDOW_VALUE                                      3:0
#define SCREG_LAYER1_RD_BW_WINDOW_VALUE_End                                    3
#define SCREG_LAYER1_RD_BW_WINDOW_VALUE_Start                                  0
#define SCREG_LAYER1_RD_BW_WINDOW_VALUE_Type                                 U04

/* Register scregLayer1RdBwStep **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor step config.  step must up-align to each format require  **
** block count unit.  linear_norot:  RGB/YUV422:1xN, YUV420:2xN.  tile_norot: **
** RGB32BPP/RGB16BPP:4xN, FP16:2xN, NV12/P010:16xN.  linear_rot:              **
** RGB32BPP:8xN, RGB16BPP:16xN, YV12:64xN, NV12/NV16:32xN, P010/P210:16xN,    **
** PACK10:24xN.  tile_rot: RGB32BPP:16xN, RGB16BPP:32xN, NV12:32xN,           **
** P010:16xN.                                                                 */

#define scregLayer1RdBwStepRegAddrs                                      0x584D7
#define SCREG_LAYER1_RD_BW_STEP_Address                                 0x16135C
#define SCREG_LAYER1_RD_BW_STEP_MSB                                           19
#define SCREG_LAYER1_RD_BW_STEP_LSB                                            0
#define SCREG_LAYER1_RD_BW_STEP_BLK                                            0
#define SCREG_LAYER1_RD_BW_STEP_Count                                          1
#define SCREG_LAYER1_RD_BW_STEP_FieldMask                             0x0000FFFF
#define SCREG_LAYER1_RD_BW_STEP_ReadMask                              0x0000FFFF
#define SCREG_LAYER1_RD_BW_STEP_WriteMask                             0x0000FFFF
#define SCREG_LAYER1_RD_BW_STEP_ResetValue                            0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_BW_STEP_VALUE                                       15:0
#define SCREG_LAYER1_RD_BW_STEP_VALUE_End                                     15
#define SCREG_LAYER1_RD_BW_STEP_VALUE_Start                                    0
#define SCREG_LAYER1_RD_BW_STEP_VALUE_Type                                   U16

/* Register scregLayer1RdBwAverageCounter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor average counter out.  */

#define scregLayer1RdBwAverageCounterRegAddrs                            0x584D8
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_Address                      0x161360
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_MSB                                19
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_LSB                                 0
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_BLK                                 0
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_Count                               1
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_FieldMask                  0xFFFFFFFF
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_ReadMask                   0xFFFFFFFF
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_WriteMask                  0x00000000
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_ResetValue                 0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_VALUE                            31:0
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_VALUE_End                          31
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_VALUE_Start                         0
#define SCREG_LAYER1_RD_BW_AVERAGE_COUNTER_VALUE_Type                        U32

/* Register scregLayer1RdBwPeak0Counter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor peak0 counter out.  */

#define scregLayer1RdBwPeak0CounterRegAddrs                              0x584D9
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_Address                        0x161364
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_MSB                                  19
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_LSB                                   0
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_BLK                                   0
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_Count                                 1
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_WriteMask                    0x00000000
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_ResetValue                   0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_VALUE                              31:0
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_VALUE_End                            31
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_VALUE_Start                           0
#define SCREG_LAYER1_RD_BW_PEAK0_COUNTER_VALUE_Type                          U32

/* Register scregLayer1RdBwPeak1Counter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor peak1 counter out.  */

#define scregLayer1RdBwPeak1CounterRegAddrs                              0x584DA
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_Address                        0x161368
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_MSB                                  19
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_LSB                                   0
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_BLK                                   0
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_Count                                 1
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_WriteMask                    0x00000000
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_ResetValue                   0x00000000

/* Value.  */
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_VALUE                              31:0
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_VALUE_End                            31
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_VALUE_Start                           0
#define SCREG_LAYER1_RD_BW_PEAK1_COUNTER_VALUE_Type                          U32

/* Register scregLayer1WrBwConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer axi write bandwidth Configuration Register.  */

#define scregLayer1WrBwConfigRegAddrs                                    0x584DB
#define SCREG_LAYER1_WR_BW_CONFIG_Address                               0x16136C
#define SCREG_LAYER1_WR_BW_CONFIG_MSB                                         19
#define SCREG_LAYER1_WR_BW_CONFIG_LSB                                          0
#define SCREG_LAYER1_WR_BW_CONFIG_BLK                                          0
#define SCREG_LAYER1_WR_BW_CONFIG_Count                                        1
#define SCREG_LAYER1_WR_BW_CONFIG_FieldMask                           0x00000003
#define SCREG_LAYER1_WR_BW_CONFIG_ReadMask                            0x00000003
#define SCREG_LAYER1_WR_BW_CONFIG_WriteMask                           0x00000003
#define SCREG_LAYER1_WR_BW_CONFIG_ResetValue                          0x00000000

/* Bandwidth monitor enable.  */
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE                   0:0
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_End                 0
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Start               0
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_ENABLE_Type              U01

/* Bandwidth monitor mode. 0 means output average count, 1 means ooutput peak **
** count.                                                                     */
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE                     1:1
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE_End                   1
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Start                 1
#define SCREG_LAYER1_WR_BW_CONFIG_BANDWIDTH_MONITOR_MODE_Type                U01

/* Register scregLayer1WrBwWindow **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor indicates number of blocks. 0 means 1 block, 1means 2    **
** block...f means 16 block.largest value is f.                               */

#define scregLayer1WrBwWindowRegAddrs                                    0x584DC
#define SCREG_LAYER1_WR_BW_WINDOW_Address                               0x161370
#define SCREG_LAYER1_WR_BW_WINDOW_MSB                                         19
#define SCREG_LAYER1_WR_BW_WINDOW_LSB                                          0
#define SCREG_LAYER1_WR_BW_WINDOW_BLK                                          0
#define SCREG_LAYER1_WR_BW_WINDOW_Count                                        1
#define SCREG_LAYER1_WR_BW_WINDOW_FieldMask                           0x0000000F
#define SCREG_LAYER1_WR_BW_WINDOW_ReadMask                            0x0000000F
#define SCREG_LAYER1_WR_BW_WINDOW_WriteMask                           0x0000000F
#define SCREG_LAYER1_WR_BW_WINDOW_ResetValue                          0x00000000

/* Value.  */
#define SCREG_LAYER1_WR_BW_WINDOW_VALUE                                      3:0
#define SCREG_LAYER1_WR_BW_WINDOW_VALUE_End                                    3
#define SCREG_LAYER1_WR_BW_WINDOW_VALUE_Start                                  0
#define SCREG_LAYER1_WR_BW_WINDOW_VALUE_Type                                 U04

/* Register scregLayer1WrBwStep **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor step config.  step must up-align to each format require  **
** block count unit.  linear_norot:  RGB/YUV422:1xN, YUV420:2xN.  tile_norot: **
** RGB32BPP/RGB16BPP:4xN, FP16:2xN, NV12/P010:16xN.  linear_rot:              **
** RGB32BPP:8xN, RGB16BPP:16xN, YV12:64xN, NV12/NV16:32xN, P010/P210:16xN,    **
** PACK10:24xN.  tile_rot: RGB32BPP:16xN, RGB16BPP:32xN, NV12:32xN,           **
** P010:16xN.                                                                 */

#define scregLayer1WrBwStepRegAddrs                                      0x584DD
#define SCREG_LAYER1_WR_BW_STEP_Address                                 0x161374
#define SCREG_LAYER1_WR_BW_STEP_MSB                                           19
#define SCREG_LAYER1_WR_BW_STEP_LSB                                            0
#define SCREG_LAYER1_WR_BW_STEP_BLK                                            0
#define SCREG_LAYER1_WR_BW_STEP_Count                                          1
#define SCREG_LAYER1_WR_BW_STEP_FieldMask                             0x0000FFFF
#define SCREG_LAYER1_WR_BW_STEP_ReadMask                              0x0000FFFF
#define SCREG_LAYER1_WR_BW_STEP_WriteMask                             0x0000FFFF
#define SCREG_LAYER1_WR_BW_STEP_ResetValue                            0x00000000

/* Value.  */
#define SCREG_LAYER1_WR_BW_STEP_VALUE                                       15:0
#define SCREG_LAYER1_WR_BW_STEP_VALUE_End                                     15
#define SCREG_LAYER1_WR_BW_STEP_VALUE_Start                                    0
#define SCREG_LAYER1_WR_BW_STEP_VALUE_Type                                   U16

/* Register scregLayer1WrBwAverageCounter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor average counter out.  */

#define scregLayer1WrBwAverageCounterRegAddrs                            0x584DE
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_Address                      0x161378
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_MSB                                19
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_LSB                                 0
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_BLK                                 0
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_Count                               1
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_FieldMask                  0xFFFFFFFF
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_ReadMask                   0xFFFFFFFF
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_WriteMask                  0x00000000
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_ResetValue                 0x00000000

/* Value.  */
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_VALUE                            31:0
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_VALUE_End                          31
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_VALUE_Start                         0
#define SCREG_LAYER1_WR_BW_AVERAGE_COUNTER_VALUE_Type                        U32

/* Register scregLayer1WrBwPeak0Counter **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* bandwidth monitor peak0 counter out.  */

#define scregLayer1WrBwPeak0CounterRegAddrs                              0x584DF
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_Address                        0x16137C
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_MSB                                  19
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_LSB                                   0
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_BLK                                   0
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_Count                                 1
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_WriteMask                    0x00000000
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_ResetValue                   0x00000000

/* Value.  */
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_VALUE                              31:0
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_VALUE_End                            31
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_VALUE_Start                           0
#define SCREG_LAYER1_WR_BW_PEAK0_COUNTER_VALUE_Type                          U32

/* Register scregLayer1BldWbLoc **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer blend wdma dst location, used for picture split.  */

#define scregLayer1BldWbLocRegAddrs                                      0x584E0
#define SCREG_LAYER1_BLD_WB_LOC_Address                                 0x161380
#define SCREG_LAYER1_BLD_WB_LOC_MSB                                           19
#define SCREG_LAYER1_BLD_WB_LOC_LSB                                            0
#define SCREG_LAYER1_BLD_WB_LOC_BLK                                            0
#define SCREG_LAYER1_BLD_WB_LOC_Count                                          1
#define SCREG_LAYER1_BLD_WB_LOC_FieldMask                             0xFFFFFFFF
#define SCREG_LAYER1_BLD_WB_LOC_ReadMask                              0xFFFFFFFF
#define SCREG_LAYER1_BLD_WB_LOC_WriteMask                             0xFFFFFFFF
#define SCREG_LAYER1_BLD_WB_LOC_ResetValue                            0x00000000

/* Locx.  */
#define SCREG_LAYER1_BLD_WB_LOC_LOCX                                        15:0
#define SCREG_LAYER1_BLD_WB_LOC_LOCX_End                                      15
#define SCREG_LAYER1_BLD_WB_LOC_LOCX_Start                                     0
#define SCREG_LAYER1_BLD_WB_LOC_LOCX_Type                                    U16

/* Locy.  */
#define SCREG_LAYER1_BLD_WB_LOC_LOCY                                       31:16
#define SCREG_LAYER1_BLD_WB_LOC_LOCY_End                                      31
#define SCREG_LAYER1_BLD_WB_LOC_LOCY_Start                                    16
#define SCREG_LAYER1_BLD_WB_LOC_LOCY_Type                                    U16

/* Register scregLayer1DecPVRICControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC control register.  */

#define scregLayer1DecPVRICControlRegAddrs                               0x584E1
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_Address                          0x161384
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_MSB                                    19
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_LSB                                     0
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_BLK                                     0
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_Count                                   1
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_FieldMask                      0xFFFFFFC0
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ReadMask                       0x7FFFFFC0
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_WriteMask                      0xFFFFFFC0
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ResetValue                     0x00000080

/* Disable Clock Gating.  */
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING                          6:6
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_End                        6
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_Start                      6
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_Type                     U01

/* Clock gating takes modules Idle signal into consideration.  */
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE                     7:7
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE_End                   7
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Start                 7
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Type                U01

/* Enable debug registeres.  */
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_DEBUG                          8:8
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_DEBUG_End                        8
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_DEBUG_Start                      8
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_DEBUG_Type                     U01

/* Enable interrupt.  */
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_INTR                           9:9
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_INTR_End                         9
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_INTR_Start                       9
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_ENABLE_INTR_Type                      U01

/* Reserved registeres.  */
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_RESERVED                            30:10
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_RESERVED_End                           30
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_RESERVED_Start                         10
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_RESERVED_Type                         U21

/* Soft Reset. This bit is volatile.  */
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_SOFT_RESET                          31:31
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_SOFT_RESET_End                         31
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_SOFT_RESET_Start                       31
#define SCREG_LAYER1_DEC_PVRIC_CONTROL_SOFT_RESET_Type                       U01

/* Register scregLayer1DecPVRICInvalidationControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC invalidation control register.  */

#define scregLayer1DecPVRICInvalidationControlRegAddrs                   0x584E2
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_Address             0x161388
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_MSB                       19
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_LSB                        0
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_BLK                        0
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_Count                      1
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_FieldMask         0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_ReadMask          0xFFFF7FFE
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_WriteMask         0x00FF81FB
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_ResetValue        0x01000600

/* Trigger the invalidation. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY                   0:0
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY_End                 0
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Start               0
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Type              U01

/* Invalidate all the Header cache lines. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL           1:1
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_End         1
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Start       1
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Type      U01

/* Invalidation has been done. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE      2:2
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_End    2
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Start  2
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Type U01

/* Invalidation by Requester. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER  8:3
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_End 8
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Start 3
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Type U06

/* Invalidation by Requester have been done. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE 14:9
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_End 14
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Start 9
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Type U06

/* Cause invalidation of all headers associated with the.                     **
** fbc_fbdc_inval_context when there is an fbc to fbdc invalidation cycle.    */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE    15:15
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_End   15
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Start 15
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Type U01

/* Invalidation by Context. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT  23:16
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_End 23
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Start 16
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Type U08

/* Invalidation by Context Done. */
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE 31:24
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_End 31
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Start 24
#define SCREG_LAYER1_DEC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Type U08

/* Register scregLayer1DecPVRICFilterConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC filter config register.  */

#define scregLayer1DecPVRICFilterConfigRegAddrs                          0x584E3
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_Address                    0x16138C
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_MSB                              19
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_LSB                               0
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_BLK                               0
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_Count                             1
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_FieldMask                0x00003FFD
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_ReadMask                 0x000000FD
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_WriteMask                0x00003F01
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_ResetValue               0x00000001

/* Enable Filter. */
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_ENABLE                          0:0
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_ENABLE_End                        0
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_ENABLE_Start                      0
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_ENABLE_Type                     U01

/* Filter Status. */
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_STATUS                          7:2
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_STATUS_End                        7
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_STATUS_Start                      2
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_STATUS_Type                     U06

/* Clear the Filter Status. */
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_CLEAR                          13:8
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_CLEAR_End                        13
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_CLEAR_Start                       8
#define SCREG_LAYER1_DEC_PVRIC_FILTER_CONFIG_CLEAR_Type                      U06

/* Register scregLayer1DecPVRICSignatureConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC signature config register.  */

#define scregLayer1DecPVRICSignatureConfigRegAddrs                       0x584E4
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_Address                 0x161390
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_MSB                           19
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_LSB                            0
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_BLK                            0
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_Count                          1
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_FieldMask             0x00003FFD
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_ReadMask              0x000000FD
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_WriteMask             0x00003F01
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_ResetValue            0x00000000

/* Enable Signature. */
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE                       0:0
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE_End                     0
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE_Start                   0
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_ENABLE_Type                  U01

/* Signature Status. */
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_STATUS                       7:2
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_STATUS_End                     7
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_STATUS_Start                   2
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_STATUS_Type                  U06

/* Clear the Signature Status. */
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR                       13:8
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR_End                     13
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR_Start                    8
#define SCREG_LAYER1_DEC_PVRIC_SIGNATURE_CONFIG_CLEAR_Type                   U06

/* Register scregLayer1DecPVRICClearValueHighReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer1DecPVRICClearValueHighReqt0RegAddrs                   0x584E5
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Address           0x161394
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_MSB                     19
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_LSB                      0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_BLK                      0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Count                    1
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_FieldMask       0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ReadMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_WriteMask       0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE                 31:0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_End               31
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Start              0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Type             U32

/* Register scregLayer1DecPVRICClearValueLowReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer1DecPVRICClearValueLowReqt0RegAddrs                    0x584E6
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_Address            0x161398
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_MSB                      19
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_LSB                       0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_BLK                       0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_Count                     1
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_FieldMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_ReadMask         0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_WriteMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE                  31:0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_End                31
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Start               0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Type              U32

/* Register scregLayer1DecPVRICClearValueHighReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer1DecPVRICClearValueHighReqt1RegAddrs                   0x584E7
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Address           0x16139C
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_MSB                     19
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_LSB                      0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_BLK                      0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Count                    1
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_FieldMask       0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ReadMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_WriteMask       0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE                 31:0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_End               31
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Start              0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Type             U32

/* Register scregLayer1DecPVRICClearValueLowReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer1DecPVRICClearValueLowReqt1RegAddrs                    0x584E8
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_Address            0x1613A0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_MSB                      19
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_LSB                       0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_BLK                       0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_Count                     1
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_FieldMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_ReadMask         0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_WriteMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE                  31:0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_End                31
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Start               0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Type              U32

/* Register scregLayer1DecPVRICClearValueHighReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer1DecPVRICClearValueHighReqt2RegAddrs                   0x584E9
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Address           0x1613A4
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_MSB                     19
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_LSB                      0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_BLK                      0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Count                    1
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_FieldMask       0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ReadMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_WriteMask       0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE                 31:0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_End               31
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Start              0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Type             U32

/* Register scregLayer1DecPVRICClearValueLowReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer1DecPVRICClearValueLowReqt2RegAddrs                    0x584EA
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_Address            0x1613A8
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_MSB                      19
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_LSB                       0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_BLK                       0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_Count                     1
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_FieldMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_ReadMask         0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_WriteMask        0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE                  31:0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_End                31
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Start               0
#define SCREG_LAYER1_DEC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Type              U32

/* Register scregLayer1DecPVRICRequesterControlReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control0.  */

#define scregLayer1DecPVRICRequesterControlReqt0RegAddrs                 0x584EB
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_Address          0x1613AC
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_MSB                    19
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_LSB                     0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_BLK                     0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_Count                   1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FieldMask      0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ReadMask       0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_WriteMask      0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY          0:0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_End        0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Start      0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT                7:1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_End              7
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Start            1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Type           U07
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_U8          0x00
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_RGB565      0x05
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A8          0x28
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE                  9:8
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_End                9
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Start              8
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Type             U02
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_RESERVED       0x0
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_8X8       0x1
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_16X4      0x2
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE             13:10
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_End            13
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Start          10
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Type          U04
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BRGA        0xD

/* Register scregLayer1DecPVRICRequesterControlReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control1.  */

#define scregLayer1DecPVRICRequesterControlReqt1RegAddrs                 0x584EC
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_Address          0x1613B0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_MSB                    19
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_LSB                     0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_BLK                     0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_Count                   1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FieldMask      0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ReadMask       0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_WriteMask      0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY          0:0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_End        0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Start      0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT                7:1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_End              7
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Start            1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Type           U07
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_U8          0x00
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_RGB565      0x05
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A8          0x28
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE                  9:8
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_End                9
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Start              8
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Type             U02
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_RESERVED       0x0
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_8X8       0x1
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_16X4      0x2
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE             13:10
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_End            13
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Start          10
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Type          U04
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BRGA        0xD

/* Register scregLayer1DecPVRICRequesterControlReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control2.  */

#define scregLayer1DecPVRICRequesterControlReqt2RegAddrs                 0x584ED
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_Address          0x1613B4
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_MSB                    19
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_LSB                     0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_BLK                     0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_Count                   1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FieldMask      0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ReadMask       0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_WriteMask      0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY          0:0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_End        0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Start      0
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT                7:1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_End              7
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Start            1
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Type           U07
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_U8          0x00
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_RGB565      0x05
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A8          0x28
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE                  9:8
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_End                9
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Start              8
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Type             U02
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_RESERVED       0x0
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_8X8       0x1
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_16X4      0x2
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE             13:10
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_End            13
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Start          10
#define SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Type          U04
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER1_DEC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BRGA        0xD

/* Register scregLayer1DecPVRICBaseAddrHighReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* High 32 bits of Base Address.  */

#define scregLayer1DecPVRICBaseAddrHighReqtRegAddrs                      0x584EE
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_Address              0x1613B8
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_MSB                        19
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_LSB                         2
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_BLK                         2
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_Count                       3
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_FieldMask          0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ReadMask           0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_WriteMask          0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ResetValue         0x00000000

/* High 32 bit address.  */
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS                  31:0
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_End                31
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Start               0
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Type              U32

/* Register scregLayer1DecPVRICBaseAddrLowReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Low 32 bits of Base Address.  */

#define scregLayer1DecPVRICBaseAddrLowReqtRegAddrs                       0x584F1
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_Address               0x1613C4
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_MSB                         19
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_LSB                          2
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_BLK                          2
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_Count                        3
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_FieldMask           0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_ReadMask            0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_WriteMask           0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_ResetValue          0x00000000

/* Low 32 bit address.  */
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS                   31:0
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_End                 31
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Start                0
#define SCREG_LAYER1_DEC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Type               U32

/* Register scregLayer1DecPVRICConstColorConfig0Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration0 (for Non-video pixels).  */

#define scregLayer1DecPVRICConstColorConfig0ReqtRegAddrs                 0x584F4
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address         0x1613D0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_MSB                   19
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_LSB                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_BLK                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_Count                  1
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_ResetValue    0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE               31:0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_End             31
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Start            0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Type           U32

/* Register scregLayer1DecPVRICConstColorConfig1Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration1 (for Non-video pixels).  */

#define scregLayer1DecPVRICConstColorConfig1ReqtRegAddrs                 0x584F5
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_Address         0x1613D4
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_MSB                   19
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_LSB                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_BLK                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_Count                  1
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_ResetValue    0x01000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE               31:0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_End             31
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Start            0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Type           U32

/* Register scregLayer1DecPVRICConstColorConfig2Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration2 (for video pixels).  */

#define scregLayer1DecPVRICConstColorConfig2ReqtRegAddrs                 0x584F6
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_Address         0x1613D8
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_MSB                   19
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_LSB                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_BLK                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_Count                  1
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_ResetValue    0x00000000

/* ValueY. */
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y            25:16
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_End           25
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Start         16
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV             9:0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_End           9
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Start         0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Type        U10

/* Register scregLayer1DecPVRICConstColorConfig3Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration3 (for video pixels).  */

#define scregLayer1DecPVRICConstColorConfig3ReqtRegAddrs                 0x584F7
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_Address         0x1613DC
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_MSB                   19
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_LSB                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_BLK                    0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_Count                  1
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_ResetValue    0x03FF0000

/* ValueY. */
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y            25:16
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_End           25
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Start         16
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV             9:0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_End           9
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Start         0
#define SCREG_LAYER1_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Type        U10

/* Register scregLayer1DecPVRICThreshold0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 0.  */

#define scregLayer1DecPVRICThreshold0RegAddrs                            0x584F8
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_Address                       0x1613E0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_MSB                                 19
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_LSB                                  0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_BLK                                  0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_Count                                1
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_FieldMask                   0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_ReadMask                    0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_WriteMask                   0x00003FFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_ResetValue                  0x00000123

/* ThresholdArgb10. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10                   5:0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_End                 5
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Start               0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Type              U06

/* ThresholdAlpha. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA                   13:6
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_End                 13
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Start                6
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Type               U08

/* Register scregLayer1DecPVRICThreshold1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 1.  */

#define scregLayer1DecPVRICThreshold1RegAddrs                            0x584F9
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_Address                       0x1613E4
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_MSB                                 19
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_LSB                                  0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_BLK                                  0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_Count                                1
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_FieldMask                   0x3FFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_ReadMask                    0x3FFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_WriteMask                   0x3FFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_ResetValue                  0x0C45550B

/* ThresholdYuv8. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8                     5:0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_End                   5
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Start                 0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Type                U06

/* Yuv10P10. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P10                         17:6
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P10_End                       17
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P10_Start                      6
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P10_Type                     U12

/* Yuv10P16. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P16                        29:18
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P16_End                       29
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P16_Start                     18
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD1_YUV10_P16_Type                     U12

/* Register scregLayer1DecPVRICThreshold2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 2.  */

#define scregLayer1DecPVRICThreshold2RegAddrs                            0x584FA
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_Address                       0x1613E8
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_MSB                                 19
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_LSB                                  0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_BLK                                  0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_Count                                1
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_FieldMask                   0x00FFFFFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_ReadMask                    0x00FFFFFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_WriteMask                   0x00FFFFFF
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_ResetValue                  0x00429329

/* ColorDiff8. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8                       11:0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8_End                     11
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF8_Type                   U12

/* ColorDiff10. */
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10                     23:12
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10_End                    23
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10_Start                  12
#define SCREG_LAYER1_DEC_PVRIC_THRESHOLD2_COLOR_DIFF10_Type                  U12

/* Register scregLayer1DecPVRICCoreIdP **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id P.  */

#define scregLayer1DecPVRICCoreIdPRegAddrs                               0x584FB
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_Address                        0x1613EC
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_MSB                                  19
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_LSB                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_BLK                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_Count                                 1
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_WriteMask                    0x00000000
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_ResetValue                   0x00000847

/* ProductCode. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE                       15:0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE_End                     15
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_PRODUCT_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_RESERVED                          31:16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_RESERVED_End                         31
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_RESERVED_Start                       16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_P_RESERVED_Type                       U16

/* Register scregLayer1DecPVRICCoreIdB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id B.  */

#define scregLayer1DecPVRICCoreIdBRegAddrs                               0x584FC
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_Address                        0x1613F0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_MSB                                  19
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_LSB                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_BLK                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_Count                                 1
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_WriteMask                    0x00000000
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_ResetValue                   0x00000000

/* BranchCode. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_BRANCH_CODE                        15:0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_BRANCH_CODE_End                      15
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_BRANCH_CODE_Start                     0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_BRANCH_CODE_Type                    U16

/* Reserved. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_RESERVED                          31:16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_RESERVED_End                         31
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_RESERVED_Start                       16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_B_RESERVED_Type                       U16

/* Register scregLayer1DecPVRICCoreIdV **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id V.  */

#define scregLayer1DecPVRICCoreIdVRegAddrs                               0x584FD
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_Address                        0x1613F4
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_MSB                                  19
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_LSB                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_BLK                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_Count                                 1
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_WriteMask                    0x00000000
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_ResetValue                   0x00000000

/* VersionCode. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_VERSION_CODE                       15:0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_VERSION_CODE_End                     15
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_VERSION_CODE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_VERSION_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_RESERVED                          31:16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_RESERVED_End                         31
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_RESERVED_Start                       16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_V_RESERVED_Type                       U16

/* Register scregLayer1DecPVRICCoreIdN **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id N.  */

#define scregLayer1DecPVRICCoreIdNRegAddrs                               0x584FE
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_Address                        0x1613F8
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_MSB                                  19
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_LSB                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_BLK                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_Count                                 1
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_WriteMask                    0x00000000
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_ResetValue                   0x00000000

/* ScalableCoreCode. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE                 15:0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_End               15
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Start              0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_RESERVED                          31:16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_RESERVED_End                         31
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_RESERVED_Start                       16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_N_RESERVED_Type                       U16

/* Register scregLayer1DecPVRICCoreIdC **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id C.  */

#define scregLayer1DecPVRICCoreIdCRegAddrs                               0x584FF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_Address                        0x1613FC
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_MSB                                  19
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_LSB                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_BLK                                   0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_Count                                 1
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_WriteMask                    0x00000000
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_ResetValue                   0x00000000

/* ConfigurationCode. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE                 15:0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_End               15
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Start              0
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_RESERVED                          31:16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_RESERVED_End                         31
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_RESERVED_Start                       16
#define SCREG_LAYER1_DEC_PVRIC_CORE_ID_C_RESERVED_Type                       U16

/* Register scregLayer1DecPVRICCoreIpChangelist **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Ip Changelist.  */

#define scregLayer1DecPVRICCoreIpChangelistRegAddrs                      0x58500
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_Address               0x161400
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_MSB                         19
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_LSB                          0
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_BLK                          0
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_Count                        1
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_FieldMask           0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_ReadMask            0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_WriteMask           0x00000000
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_ResetValue          0x00000000

/* ChangelistCode. */
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE           31:0
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_End         31
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Start        0
#define SCREG_LAYER1_DEC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Type       U32

/* Register scregLayer1DecPVRICDebugStatus **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Debug Status.  */

#define scregLayer1DecPVRICDebugStatusRegAddrs                           0x58501
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_Address                     0x161404
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_MSB                               19
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_LSB                                0
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_BLK                                0
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_Count                              1
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_FieldMask                 0x00000003
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_ReadMask                  0x00000003
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_WriteMask                 0x00000002
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_ResetValue                0x00000001

/* Idle. */
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_IDLE                             0:0
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_IDLE_End                           0
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_IDLE_Start                         0
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_IDLE_Type                        U01

/* BusError. */
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR                        1:1
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR_End                      1
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR_Start                    1
#define SCREG_LAYER1_DEC_PVRIC_DEBUG_STATUS_BUS_ERROR_Type                   U01

/* Register scregLayer1DecPVRICCounterMasterAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AW. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterMasterAWRegAddrs                       0x58502
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_Address                0x161408
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_MSB                          19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_LSB                           0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_BLK                           0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_Count                         1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_FieldMask            0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_ReadMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_WriteMask            0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_VALUE                      31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_VALUE_End                    31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_VALUE_Start                   0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AW_VALUE_Type                  U32

/* Register scregLayer1DecPVRICCounterMasterW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master W. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterMasterWRegAddrs                        0x58503
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_Address                 0x16140C
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_MSB                           19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_LSB                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_BLK                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_Count                          1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_VALUE                       31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_VALUE_End                     31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_VALUE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_W_VALUE_Type                   U32

/* Register scregLayer1DecPVRICCounterMasterB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master B. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterMasterBRegAddrs                        0x58504
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_Address                 0x161410
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_MSB                           19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_LSB                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_BLK                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_Count                          1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_VALUE                       31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_VALUE_End                     31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_VALUE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_B_VALUE_Type                   U32

/* Register scregLayer1DecPVRICCounterMasterAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AR. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterMasterARRegAddrs                       0x58505
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_Address                0x161414
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_MSB                          19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_LSB                           0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_BLK                           0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_Count                         1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_FieldMask            0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_ReadMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_WriteMask            0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_VALUE                      31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_VALUE_End                    31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_VALUE_Start                   0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_AR_VALUE_Type                  U32

/* Register scregLayer1DecPVRICCounterMasterR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master R. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterMasterRRegAddrs                        0x58506
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_Address                 0x161418
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_MSB                           19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_LSB                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_BLK                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_Count                          1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_VALUE                       31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_VALUE_End                     31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_VALUE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_MASTER_R_VALUE_Type                   U32

/* Register scregLayer1DecPVRICCounterSlaveAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AW. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterSlaveAWRegAddrs                        0x58507
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_Address                 0x16141C
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_MSB                           19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_LSB                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_BLK                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_Count                          1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE                       31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE_End                     31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AW_VALUE_Type                   U32

/* Register scregLayer1DecPVRICCounterSlaveW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave W. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterSlaveWRegAddrs                         0x58508
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_Address                  0x161420
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_MSB                            19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_LSB                             0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_BLK                             0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_Count                           1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_VALUE                        31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_VALUE_End                      31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_VALUE_Start                     0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_W_VALUE_Type                    U32

/* Register scregLayer1DecPVRICCounterSlaveB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave B. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterSlaveBRegAddrs                         0x58509
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_Address                  0x161424
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_MSB                            19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_LSB                             0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_BLK                             0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_Count                           1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_VALUE                        31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_VALUE_End                      31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_VALUE_Start                     0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_B_VALUE_Type                    U32

/* Register scregLayer1DecPVRICCounterSlaveAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AR. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterSlaveARRegAddrs                        0x5850A
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_Address                 0x161428
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_MSB                           19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_LSB                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_BLK                            0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_Count                          1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE                       31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE_End                     31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE_Start                    0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_AR_VALUE_Type                   U32

/* Register scregLayer1DecPVRICCounterSlaveR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave R. Toggled only when enableDebug asserted. */

#define scregLayer1DecPVRICCounterSlaveRRegAddrs                         0x5850B
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_Address                  0x16142C
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_MSB                            19
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_LSB                             0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_BLK                             0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_Count                           1
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_VALUE                        31:0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_VALUE_End                      31
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_VALUE_Start                     0
#define SCREG_LAYER1_DEC_PVRIC_COUNTER_SLAVE_R_VALUE_Type                    U32

/* Register scregLayer1EncPVRICControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC control register.  */

#define scregLayer1EncPVRICControlRegAddrs                               0x5850C
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_Address                          0x161430
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_MSB                                    19
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_LSB                                     0
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_BLK                                     0
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_Count                                   1
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_FieldMask                      0xFFFFFFC0
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ReadMask                       0x7FFFFFC0
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_WriteMask                      0xFFFFFFC0
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ResetValue                     0x00000080

/* Disable Clock Gating.  */
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING                          6:6
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_End                        6
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_Start                      6
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_Type                     U01

/* Clock gating takes modules Idle signal into consideration.  */
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE                     7:7
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE_End                   7
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Start                 7
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_CLOCK_GATING_IDLE_Type                U01

/* Enable debug registeres.  */
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_DEBUG                          8:8
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_DEBUG_End                        8
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_DEBUG_Start                      8
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_DEBUG_Type                     U01

/* Enable interrupt.  */
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_INTR                           9:9
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_INTR_End                         9
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_INTR_Start                       9
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_ENABLE_INTR_Type                      U01

/* Reserved registeres.  */
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_RESERVED                            30:10
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_RESERVED_End                           30
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_RESERVED_Start                         10
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_RESERVED_Type                         U21

/* Soft Reset. This bit is volatile.  */
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_SOFT_RESET                          31:31
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_SOFT_RESET_End                         31
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_SOFT_RESET_Start                       31
#define SCREG_LAYER1_ENC_PVRIC_CONTROL_SOFT_RESET_Type                       U01

/* Register scregLayer1EncPVRICInvalidationControl **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC invalidation control register.  */

#define scregLayer1EncPVRICInvalidationControlRegAddrs                   0x5850D
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_Address             0x161434
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_MSB                       19
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_LSB                        0
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_BLK                        0
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_Count                      1
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_FieldMask         0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_ReadMask          0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_WriteMask         0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_ResetValue        0x01000600

/* Trigger the invalidation. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY                   0:0
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY_End                 0
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Start               0
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_NOTIFY_Type              U01

/* Invalidate all the Header cache lines. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL           1:1
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_End         1
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Start       1
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_Type      U01

/* Invalidation has been done. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE      2:2
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_End    2
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Start  2
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_ALL_DONE_Type U01

/* Invalidation by Requester. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER  8:3
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_End 8
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Start 3
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_Type U06

/* Invalidation by Requester have been done. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE 14:9
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_End 14
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Start 9
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_REQUESTER_DONE_Type U06

/* Cause invalidation of all headers associated with the.                     **
** fbc_fbdc_inval_context when there is an fbc to fbdc invalidation cycle.    */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE    15:15
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_End   15
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Start 15
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_OVERRIDE_Type U01

/* Invalidation by Context. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT  23:16
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_End 23
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Start 16
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_Type U08

/* Invalidation by Context Done. */
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE 31:24
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_End 31
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Start 24
#define SCREG_LAYER1_ENC_PVRIC_INVALIDATION_CONTROL_INVALIDATE_BY_CONTEXT_DONE_Type U08

/* Register scregLayer1EncPVRICFilterConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC filter config register.  */

#define scregLayer1EncPVRICFilterConfigRegAddrs                          0x5850E
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_Address                    0x161438
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_MSB                              19
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_LSB                               0
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_BLK                               0
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_Count                             1
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_FieldMask                0x00003FFD
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_ReadMask                 0x00003FFD
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_WriteMask                0x00003FFD
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_ResetValue               0x00000001

/* Enable Filter. */
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_ENABLE                          0:0
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_ENABLE_End                        0
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_ENABLE_Start                      0
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_ENABLE_Type                     U01

/* Filter Status. */
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_STATUS                          7:2
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_STATUS_End                        7
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_STATUS_Start                      2
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_STATUS_Type                     U06

/* Clear the Filter Status. */
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_CLEAR                          13:8
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_CLEAR_End                        13
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_CLEAR_Start                       8
#define SCREG_LAYER1_ENC_PVRIC_FILTER_CONFIG_CLEAR_Type                      U06

/* Register scregLayer1EncPVRICSignatureConfig **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Layer PVRIC signature config register.  */

#define scregLayer1EncPVRICSignatureConfigRegAddrs                       0x5850F
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_Address                 0x16143C
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_MSB                           19
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_LSB                            0
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_BLK                            0
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_Count                          1
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_FieldMask             0x00003FFD
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_ReadMask              0x00003FFD
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_WriteMask             0x00003FFD
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_ResetValue            0x00000000

/* Enable Signature. */
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE                       0:0
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE_End                     0
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE_Start                   0
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_ENABLE_Type                  U01

/* Signature Status. */
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_STATUS                       7:2
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_STATUS_End                     7
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_STATUS_Start                   2
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_STATUS_Type                  U06

/* Clear the Signature Status. */
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR                       13:8
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR_End                     13
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR_Start                    8
#define SCREG_LAYER1_ENC_PVRIC_SIGNATURE_CONFIG_CLEAR_Type                   U06

/* Register scregLayer1EncPVRICClearValueHighReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer1EncPVRICClearValueHighReqt0RegAddrs                   0x58510
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Address           0x161440
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_MSB                     19
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_LSB                      0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_BLK                      0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_Count                    1
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_FieldMask       0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ReadMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_WriteMask       0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE                 31:0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_End               31
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Start              0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT0_VALUE_Type             U32

/* Register scregLayer1EncPVRICClearValueLowReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer1EncPVRICClearValueLowReqt0RegAddrs                    0x58511
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_Address            0x161444
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_MSB                      19
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_LSB                       0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_BLK                       0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_Count                     1
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_FieldMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_ReadMask         0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_WriteMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE                  31:0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_End                31
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Start               0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT0_VALUE_Type              U32

/* Register scregLayer1EncPVRICClearValueHighReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer1EncPVRICClearValueHighReqt1RegAddrs                   0x58512
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Address           0x161448
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_MSB                     19
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_LSB                      0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_BLK                      0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_Count                    1
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_FieldMask       0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ReadMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_WriteMask       0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE                 31:0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_End               31
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Start              0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT1_VALUE_Type             U32

/* Register scregLayer1EncPVRICClearValueLowReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer1EncPVRICClearValueLowReqt1RegAddrs                    0x58513
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_Address            0x16144C
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_MSB                      19
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_LSB                       0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_BLK                       0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_Count                     1
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_FieldMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_ReadMask         0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_WriteMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE                  31:0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_End                31
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Start               0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT1_VALUE_Type              U32

/* Register scregLayer1EncPVRICClearValueHighReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register High 32bits.  */

#define scregLayer1EncPVRICClearValueHighReqt2RegAddrs                   0x58514
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Address           0x161450
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_MSB                     19
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_LSB                      0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_BLK                      0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_Count                    1
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_FieldMask       0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ReadMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_WriteMask       0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_ResetValue      0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE                 31:0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_End               31
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Start              0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_HIGH_REQT2_VALUE_Type             U32

/* Register scregLayer1EncPVRICClearValueLowReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Clear Value Register Low 32bits.  */

#define scregLayer1EncPVRICClearValueLowReqt2RegAddrs                    0x58515
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_Address            0x161454
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_MSB                      19
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_LSB                       0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_BLK                       0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_Count                     1
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_FieldMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_ReadMask         0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_WriteMask        0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_ResetValue       0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE                  31:0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_End                31
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Start               0
#define SCREG_LAYER1_ENC_PVRIC_CLEAR_VALUE_LOW_REQT2_VALUE_Type              U32

/* Register scregLayer1EncPVRICRequesterControlReqt0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control0.  */

#define scregLayer1EncPVRICRequesterControlReqt0RegAddrs                 0x58516
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_Address          0x161458
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_MSB                    19
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_LSB                     0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_BLK                     0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_Count                   1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FieldMask      0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ReadMask       0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_WriteMask      0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY          0:0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_End        0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Start      0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT                7:1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_End              7
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Start            1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_Type           U07
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_U8          0x00
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_RGB565      0x05
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_A8          0x28
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE                  9:8
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_End                9
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Start              8
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_Type             U02
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_RESERVED       0x0
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_8X8       0x1
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_16X4      0x2
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE             13:10
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_End            13
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Start          10
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_Type          U04
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT0_SWIZZLE_BRGA        0xD

/* Register scregLayer1EncPVRICRequesterControlReqt1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control1.  */

#define scregLayer1EncPVRICRequesterControlReqt1RegAddrs                 0x58517
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_Address          0x16145C
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_MSB                    19
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_LSB                     0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_BLK                     0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_Count                   1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FieldMask      0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ReadMask       0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_WriteMask      0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY          0:0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_End        0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Start      0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT                7:1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_End              7
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Start            1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_Type           U07
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_U8          0x00
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_RGB565      0x05
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_A8          0x28
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE                  9:8
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_End                9
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Start              8
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_Type             U02
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_RESERVED       0x0
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_8X8       0x1
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_16X4      0x2
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE             13:10
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_End            13
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Start          10
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_Type          U04
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT1_SWIZZLE_BRGA        0xD

/* Register scregLayer1EncPVRICRequesterControlReqt2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Requester Control2.  */

#define scregLayer1EncPVRICRequesterControlReqt2RegAddrs                 0x58518
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_Address          0x161460
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_MSB                    19
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_LSB                     0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_BLK                     0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_Count                   1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FieldMask      0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ReadMask       0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_WriteMask      0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ResetValue     0x00000101

/* Enable Lossy Compression. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY          0:0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_End        0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Start      0
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_ENABLE_LOSSY_Type     U01

/* Input Format. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT                7:1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_End              7
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Start            1
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_Type           U07
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_U8          0x00
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_RGB565      0x05
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A2R10B10G10 0x0E
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_FP16F16F16F16 0x1C
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_A8          0x28
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_ARGB8888    0x29
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420_2PLANE 0x36
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YVU420_2PLANE 0x37
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_FORMAT_YUV420BIT10PACK16 0x65

/* Tile Type. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE                  9:8
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_End                9
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Start              8
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_Type             U02
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_RESERVED       0x0
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_8X8       0x1
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_16X4      0x2
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_TILE_TILE_32X2      0x3

/* ARGB Channel Swizzle. */
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE             13:10
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_End            13
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Start          10
#define SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_Type          U04
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARGB        0x0
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ARBG        0x1
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGRB        0x2
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_AGBR        0x3
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABGR        0x4
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_ABRG        0x5
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RGBA        0x8
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_RBGA        0x9
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GRBA        0xA
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_GBRA        0xB
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BGRA        0xC
#define   SCREG_LAYER1_ENC_PVRIC_REQUESTER_CONTROL_REQT2_SWIZZLE_BRGA        0xD

/* Register scregLayer1EncPVRICBaseAddrHighReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* High 32 bits of Base Address.  */

#define scregLayer1EncPVRICBaseAddrHighReqtRegAddrs                      0x58519
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_Address              0x161464
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_MSB                        19
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_LSB                         2
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_BLK                         2
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_Count                       3
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_FieldMask          0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ReadMask           0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_WriteMask          0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ResetValue         0x00000000

/* High 32 bit address.  */
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS                  31:0
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_End                31
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Start               0
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_HIGH_REQT_ADDRESS_Type              U32

/* Register scregLayer1EncPVRICBaseAddrLowReqt (3 in total) **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Low 32 bits of Base Address.  */

#define scregLayer1EncPVRICBaseAddrLowReqtRegAddrs                       0x5851C
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_Address               0x161470
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_MSB                         19
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_LSB                          2
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_BLK                          2
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_Count                        3
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_FieldMask           0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_ReadMask            0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_WriteMask           0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_ResetValue          0x00000000

/* Low 32 bit address.  */
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS                   31:0
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_End                 31
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Start                0
#define SCREG_LAYER1_ENC_PVRIC_BASE_ADDR_LOW_REQT_ADDRESS_Type               U32

/* Register scregLayer1EncPVRICConstColorConfig0Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration0 (for Non-video pixels).  */

#define scregLayer1EncPVRICConstColorConfig0ReqtRegAddrs                 0x5851F
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address         0x16147C
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_MSB                   19
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_LSB                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_BLK                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_Count                  1
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_ResetValue    0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE               31:0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_End             31
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Start            0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_VALUE_Type           U32

/* Register scregLayer1EncPVRICConstColorConfig1Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration1 (for Non-video pixels).  */

#define scregLayer1EncPVRICConstColorConfig1ReqtRegAddrs                 0x58520
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_Address         0x161480
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_MSB                   19
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_LSB                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_BLK                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_Count                  1
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_FieldMask     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_ReadMask      0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_WriteMask     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_ResetValue    0x01000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE               31:0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_End             31
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Start            0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG1_REQT_VALUE_Type           U32

/* Register scregLayer1EncPVRICConstColorConfig2Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration2 (for video pixels).  */

#define scregLayer1EncPVRICConstColorConfig2ReqtRegAddrs                 0x58521
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_Address         0x161484
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_MSB                   19
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_LSB                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_BLK                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_Count                  1
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_ResetValue    0x00000000

/* ValueY. */
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y            25:16
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_End           25
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Start         16
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV             9:0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_End           9
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Start         0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG2_REQT_VALUE_UV_Type        U10

/* Register scregLayer1EncPVRICConstColorConfig3Reqt **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* Constant Color configuration3 (for video pixels).  */

#define scregLayer1EncPVRICConstColorConfig3ReqtRegAddrs                 0x58522
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_Address         0x161488
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_MSB                   19
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_LSB                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_BLK                    0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_Count                  1
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_FieldMask     0x03FF03FF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_ReadMask      0x03FF03FF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_WriteMask     0x03FF03FF
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_ResetValue    0x03FF0000

/* ValueY. */
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y            25:16
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_End           25
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Start         16
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_Y_Type         U10

/* ValueUV. */
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV             9:0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_End           9
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Start         0
#define SCREG_LAYER1_ENC_PVRIC_CONST_COLOR_CONFIG3_REQT_VALUE_UV_Type        U10

/* Register scregLayer1EncPVRICThreshold0 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 0.  */

#define scregLayer1EncPVRICThreshold0RegAddrs                            0x58523
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_Address                       0x16148C
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_MSB                                 19
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_LSB                                  0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_BLK                                  0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_Count                                1
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_FieldMask                   0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_ReadMask                    0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_WriteMask                   0x00003FFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_ResetValue                  0x00000123

/* ThresholdArgb10. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10                   5:0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_End                 5
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Start               0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ARGB10_Type              U06

/* ThresholdAlpha. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA                   13:6
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_End                 13
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Start                6
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD0_THRESHOLD_ALPHA_Type               U08

/* Register scregLayer1EncPVRICThreshold1 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 1.  */

#define scregLayer1EncPVRICThreshold1RegAddrs                            0x58524
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_Address                       0x161490
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_MSB                                 19
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_LSB                                  0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_BLK                                  0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_Count                                1
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_FieldMask                   0x3FFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_ReadMask                    0x3FFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_WriteMask                   0x3FFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_ResetValue                  0x0C45550B

/* ThresholdYuv8. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8                     5:0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_End                   5
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Start                 0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_THRESHOLD_YUV8_Type                U06

/* Yuv10P10. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P10                         17:6
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P10_End                       17
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P10_Start                      6
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P10_Type                     U12

/* Yuv10P16. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P16                        29:18
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P16_End                       29
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P16_Start                     18
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD1_YUV10_P16_Type                     U12

/* Register scregLayer1EncPVRICThreshold2 **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Threshold 2.  */

#define scregLayer1EncPVRICThreshold2RegAddrs                            0x58525
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_Address                       0x161494
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_MSB                                 19
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_LSB                                  0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_BLK                                  0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_Count                                1
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_FieldMask                   0x00FFFFFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_ReadMask                    0x00FFFFFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_WriteMask                   0x00FFFFFF
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_ResetValue                  0x00429329

/* ColorDiff8. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8                       11:0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8_End                     11
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF8_Type                   U12

/* ColorDiff10. */
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10                     23:12
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10_End                    23
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10_Start                  12
#define SCREG_LAYER1_ENC_PVRIC_THRESHOLD2_COLOR_DIFF10_Type                  U12

/* Register scregLayer1EncPVRICCoreIdP **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id P.  */

#define scregLayer1EncPVRICCoreIdPRegAddrs                               0x58526
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_Address                        0x161498
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_MSB                                  19
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_LSB                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_BLK                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_Count                                 1
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_WriteMask                    0x00000000
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_ResetValue                   0x00000847

/* ProductCode. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE                       15:0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE_End                     15
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_PRODUCT_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_RESERVED                          31:16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_RESERVED_End                         31
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_RESERVED_Start                       16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_P_RESERVED_Type                       U16

/* Register scregLayer1EncPVRICCoreIdB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id B.  */

#define scregLayer1EncPVRICCoreIdBRegAddrs                               0x58527
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_Address                        0x16149C
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_MSB                                  19
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_LSB                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_BLK                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_Count                                 1
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_WriteMask                    0x00000000
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_ResetValue                   0x00000000

/* BranchCode. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_BRANCH_CODE                        15:0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_BRANCH_CODE_End                      15
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_BRANCH_CODE_Start                     0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_BRANCH_CODE_Type                    U16

/* Reserved. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_RESERVED                          31:16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_RESERVED_End                         31
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_RESERVED_Start                       16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_B_RESERVED_Type                       U16

/* Register scregLayer1EncPVRICCoreIdV **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id V.  */

#define scregLayer1EncPVRICCoreIdVRegAddrs                               0x58528
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_Address                        0x1614A0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_MSB                                  19
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_LSB                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_BLK                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_Count                                 1
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_WriteMask                    0x00000000
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_ResetValue                   0x00000000

/* VersionCode. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_VERSION_CODE                       15:0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_VERSION_CODE_End                     15
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_VERSION_CODE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_VERSION_CODE_Type                   U16

/* Reserved. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_RESERVED                          31:16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_RESERVED_End                         31
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_RESERVED_Start                       16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_V_RESERVED_Type                       U16

/* Register scregLayer1EncPVRICCoreIdN **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id N.  */

#define scregLayer1EncPVRICCoreIdNRegAddrs                               0x58529
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_Address                        0x1614A4
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_MSB                                  19
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_LSB                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_BLK                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_Count                                 1
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_WriteMask                    0x00000000
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_ResetValue                   0x00000000

/* ScalableCoreCode. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE                 15:0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_End               15
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Start              0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_SCALABLE_CORE_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_RESERVED                          31:16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_RESERVED_End                         31
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_RESERVED_Start                       16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_N_RESERVED_Type                       U16

/* Register scregLayer1EncPVRICCoreIdC **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Id C.  */

#define scregLayer1EncPVRICCoreIdCRegAddrs                               0x5852A
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_Address                        0x1614A8
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_MSB                                  19
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_LSB                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_BLK                                   0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_Count                                 1
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_FieldMask                    0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_ReadMask                     0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_WriteMask                    0x00000000
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_ResetValue                   0x00000000

/* ConfigurationCode. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE                 15:0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_End               15
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Start              0
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_CONFIGURATION_CODE_Type             U16

/* Reserved. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_RESERVED                          31:16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_RESERVED_End                         31
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_RESERVED_Start                       16
#define SCREG_LAYER1_ENC_PVRIC_CORE_ID_C_RESERVED_Type                       U16

/* Register scregLayer1EncPVRICCoreIpChangelist **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Core Ip Changelist.  */

#define scregLayer1EncPVRICCoreIpChangelistRegAddrs                      0x5852B
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_Address               0x1614AC
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_MSB                         19
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_LSB                          0
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_BLK                          0
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_Count                        1
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_FieldMask           0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_ReadMask            0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_WriteMask           0x00000000
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_ResetValue          0x00000000

/* ChangelistCode. */
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE           31:0
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_End         31
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Start        0
#define SCREG_LAYER1_ENC_PVRIC_CORE_IP_CHANGELIST_CHANGELIST_CODE_Type       U32

/* Register scregLayer1EncPVRICDebugStatus **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Debug Status.  */

#define scregLayer1EncPVRICDebugStatusRegAddrs                           0x5852C
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_Address                     0x1614B0
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_MSB                               19
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_LSB                                0
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_BLK                                0
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_Count                              1
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_FieldMask                 0x00000003
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_ReadMask                  0x00000003
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_WriteMask                 0x00000002
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_ResetValue                0x00000001

/* Idle. */
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_IDLE                             0:0
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_IDLE_End                           0
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_IDLE_Start                         0
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_IDLE_Type                        U01

/* BusError. */
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR                        1:1
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR_End                      1
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR_Start                    1
#define SCREG_LAYER1_ENC_PVRIC_DEBUG_STATUS_BUS_ERROR_Type                   U01

/* Register scregLayer1EncPVRICCounterMasterAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AW. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterMasterAWRegAddrs                       0x5852D
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_Address                0x1614B4
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_MSB                          19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_LSB                           0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_BLK                           0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_Count                         1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_FieldMask            0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_ReadMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_WriteMask            0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_VALUE                      31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_VALUE_End                    31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_VALUE_Start                   0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AW_VALUE_Type                  U32

/* Register scregLayer1EncPVRICCounterMasterW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master W. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterMasterWRegAddrs                        0x5852E
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_Address                 0x1614B8
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_MSB                           19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_LSB                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_BLK                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_Count                          1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_VALUE                       31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_VALUE_End                     31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_VALUE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_W_VALUE_Type                   U32

/* Register scregLayer1EncPVRICCounterMasterB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master B. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterMasterBRegAddrs                        0x5852F
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_Address                 0x1614BC
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_MSB                           19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_LSB                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_BLK                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_Count                          1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_VALUE                       31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_VALUE_End                     31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_VALUE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_B_VALUE_Type                   U32

/* Register scregLayer1EncPVRICCounterMasterAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master AR. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterMasterARRegAddrs                       0x58530
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_Address                0x1614C0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_MSB                          19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_LSB                           0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_BLK                           0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_Count                         1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_FieldMask            0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_ReadMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_WriteMask            0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_ResetValue           0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_VALUE                      31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_VALUE_End                    31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_VALUE_Start                   0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_AR_VALUE_Type                  U32

/* Register scregLayer1EncPVRICCounterMasterR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Master R. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterMasterRRegAddrs                        0x58531
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_Address                 0x1614C4
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_MSB                           19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_LSB                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_BLK                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_Count                          1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_VALUE                       31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_VALUE_End                     31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_VALUE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_MASTER_R_VALUE_Type                   U32

/* Register scregLayer1EncPVRICCounterSlaveAW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AW. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterSlaveAWRegAddrs                        0x58532
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_Address                 0x1614C8
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_MSB                           19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_LSB                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_BLK                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_Count                          1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE                       31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE_End                     31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AW_VALUE_Type                   U32

/* Register scregLayer1EncPVRICCounterSlaveW **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave W. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterSlaveWRegAddrs                         0x58533
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_Address                  0x1614CC
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_MSB                            19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_LSB                             0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_BLK                             0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_Count                           1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_VALUE                        31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_VALUE_End                      31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_VALUE_Start                     0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_W_VALUE_Type                    U32

/* Register scregLayer1EncPVRICCounterSlaveB **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave B. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterSlaveBRegAddrs                         0x58534
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_Address                  0x1614D0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_MSB                            19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_LSB                             0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_BLK                             0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_Count                           1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_VALUE                        31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_VALUE_End                      31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_VALUE_Start                     0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_B_VALUE_Type                    U32

/* Register scregLayer1EncPVRICCounterSlaveAR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave AR. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterSlaveARRegAddrs                        0x58535
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_Address                 0x1614D4
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_MSB                           19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_LSB                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_BLK                            0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_Count                          1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_FieldMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_ReadMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_WriteMask             0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_ResetValue            0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE                       31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE_End                     31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE_Start                    0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_AR_VALUE_Type                   U32

/* Register scregLayer1EncPVRICCounterSlaveR **
** ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* PVRIC Counter Slave R. Toggled only when enableDebug asserted. */

#define scregLayer1EncPVRICCounterSlaveRRegAddrs                         0x58536
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_Address                  0x1614D8
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_MSB                            19
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_LSB                             0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_BLK                             0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_Count                           1
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_FieldMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_ReadMask               0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_WriteMask              0xFFFFFFFF
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_ResetValue             0x00000000

/* Value. */
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_VALUE                        31:0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_VALUE_End                      31
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_VALUE_Start                     0
#define SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_VALUE_Type                    U32

/* Register scregLayer1End **
** ~~~~~~~~~~~~~~~~~~~~~~~ */

/* End address of this module.  Reserved.  */

#define scregLayer1EndRegAddrs                                           0x58537
#define SCREG_LAYER1_END_Address                                        0x1614DC
#define SCREG_LAYER1_END_MSB                                                  19
#define SCREG_LAYER1_END_LSB                                                   0
#define SCREG_LAYER1_END_BLK                                                   0
#define SCREG_LAYER1_END_Count                                                 1
#define SCREG_LAYER1_END_FieldMask                                    0xFFFFFFFF
#define SCREG_LAYER1_END_ReadMask                                     0xFFFFFFFF
#define SCREG_LAYER1_END_WriteMask                                    0xFFFFFFFF
#define SCREG_LAYER1_END_ResetValue                                   0x00000000

#define SCREG_LAYER1_END_ADDRESS                                            31:0
#define SCREG_LAYER1_END_ADDRESS_End                                          31
#define SCREG_LAYER1_END_ADDRESS_Start                                         0
#define SCREG_LAYER1_END_ADDRESS_Type                                        U32

// clang-format on

#endif /* __scregSC_h__ */
