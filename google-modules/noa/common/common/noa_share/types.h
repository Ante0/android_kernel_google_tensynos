/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header that align API between NOA mode and Kernel mode
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __LINUX_PORT_NOA_SHARE_TYPES__
#define __LINUX_PORT_NOA_SHARE_TYPES__

#ifdef __cplusplus
#define NOA_CONST_CAST(type, addr) (const_cast<type>(addr))
#define NOA_REINTERPRET_CAST(type, addr) (reinterpret_cast<type>(addr))
#define NOA_STATIC_CAST(type, addr) (static_cast<type>(addr))
#else /* __cplusplus */
#define NOA_C_STYLE_CAST(type, addr) ((type)(addr))
#define NOA_CONST_CAST(type, addr) NOA_C_STYLE_CAST(type, addr)
#define NOA_REINTERPRET_CAST(type, addr) NOA_C_STYLE_CAST(type, addr)
#define NOA_STATIC_CAST(type, addr) NOA_C_STYLE_CAST(type, addr)
#endif /* __cplusplus */

#endif /* __LINUX_PORT_NOA_SHARE_TYPES__ */
