/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Google LLC */

#ifndef ___DVFS_TARGET_FRONTEND_DEBUG_H__
#define ___DVFS_TARGET_FRONTEND_DEBUG_H__

#ifdef CONFIG_DEBUG_FS
void dvfs_target_frontend_debugfs_init(void);
void dvfs_target_frontend_debugfs_remove(void);
#else
static inline void dvfs_target_frontend_debugfs_init(void) {}
static inline void dvfs_target_frontend_debugfs_remove(void) {}
#endif

#endif /* ___DVFS_TARGET_FRONTEND_DEBUG_H__ */
