/*************************************************************************/ /*!
@File           rgx_fwif_oss_common.h
@Title          Helpers to create static asserts for RGX firmware interface
                structures used by the open source (OSS) driver
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Static asserts for RGX firmware interface structures used by
                open source (OSS) driver
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

#if !defined(RGX_FWIF_OSS_COMMON_H)
#define RGX_FWIF_OSS_COMMON_H

#if defined(SUPPORT_OPEN_SOURCE_DRIVER_FIRMWARE)

#include "img_defs.h"

#define rgx_fwif_oss_assert(expr_, value_, expected_) \
	static_assert(expr_, "SUPPORT_OPEN_SOURCE_DRIVER_FIRMWARE: " value_ " is incorrect (expected " expected_ ")")

#define rgx_fwif_oss_assert_eq(expr_, value_) rgx_fwif_oss_assert(expr_ == value_, #expr_, #value_)
#define rgx_fwif_oss_assert_le(expr_, value_) rgx_fwif_oss_assert(expr_ <= value_, #expr_, "<= " #value_)

#define rgx_fwif_oss_assert_size(type_, size_) rgx_fwif_oss_assert_eq(sizeof(type_), size_)
#define rgx_fwif_oss_assert_align(type_, align_) rgx_fwif_oss_assert_le(__alignof__(type_), align_)
#define rgx_fwif_oss_assert_offset(struct_, member_, offset_) rgx_fwif_oss_assert_eq(offsetof(struct_, member_), offset_)

/*! A shortcut for enum types to ensure they're properly sized (no -fshort-enums or similar). */
#define rgx_fwif_oss_assert_enum(type_) rgx_fwif_oss_assert_size(type_, 4); rgx_fwif_oss_assert_align(type_, 4)

/* Don't use RGX_FWIF_OSSMAX here since it causes expansion of macro_. */
#define rgx_fwif_oss_assert_max(macro_) rgx_fwif_oss_assert_le(macro_, macro_ ## _OSSMAX)

#define RGX_FWIF_OSSMAX(X_) X_ ## _OSSMAX

#else

#define RGX_FWIF_OSSMAX(X_) X_

#endif

#endif /* RGX_FWIF_OSS_COMMON_H */

/******************************************************************************
 End of file (rgx_fwif_oss_common.h)
******************************************************************************/
