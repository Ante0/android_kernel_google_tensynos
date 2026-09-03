/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sound

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SOUND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SOUND_H

#include <trace/hooks/vendor_hooks.h>

/*
 * This hook allows the vendor driver to tell the kernel:
 * "Don't allocate a buffer for this stream."
 */
struct snd_pcm_substream;

DECLARE_HOOK(android_vh_snd_pcm_check_no_buffer,
	TP_PROTO(struct snd_pcm_substream *substream, bool *no_buffer),
	TP_ARGS(substream, no_buffer));

#endif /* _TRACE_HOOK_SOUND_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
