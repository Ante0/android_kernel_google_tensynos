/* SPDX-License-Identifier: MIT */

#ifndef __VS_DRM_STATE_RECORD_H__
#define __VS_DRM_STATE_RECORD_H__

#include <linux/types.h>
#include <drm/drm_atomic.h>
#include <drm/drm_print.h>

#define VS_RECORD_STATE_MAX 8

struct drm_state_history_record;

/**
 * struct drm_state_history_data - storage for drm state history output
 *
 * This struct holds buffers corresponding to the most recent drm_atomic_state
 * objects that are recorded in a struct drm_state_history_record, as well as
 * the sizes of each of those buffers.
 *
 * It is meant to be populated by vs_drm_recorded_states_prepare() and cleaned
 * up by vs_drm_recorded_states_destroy()
 *
 * @buffers: buffers containing drm_atomic_state print contents
 * @buffer_sizes: the size of each buffer
 * @num_logged_states: how many buffers were allocated and recorded
 */
struct drm_state_history_data {
	char *buffers[VS_RECORD_STATE_MAX];
	size_t buffer_sizes[VS_RECORD_STATE_MAX];
	int num_logged_states;
};

/**
 * vs_drm_prepare_state_history_record() - prepares state history object
 * @drm_dev: drm device onto which to attach state history record
 *
 * This function allocates all required resources and locks in order to keep
 * track of a history of committed struct drm_atomic_states.
 *
 * Must be balanced with a call to vs_drm_destroy_state_history_record().
 *
 * Return: 0 on success, negative value on error
 */
int vs_drm_prepare_state_history_record(struct drm_device *drm_dev);

/**
 * vs_drm_destroy_state_history_record() - cleans up state history object
 * @drm_dev: drm device storing state history record
 *
 * Releases resources prepared by vs_drm_prepare_state_history_record().
 */
void vs_drm_destroy_state_history_record(struct drm_device *drm_dev);

/**
 * vs_drm_record_state() - Keeps the given state around for debug dumping later
 * @state: state to record for the next few frames
 *
 * This keeps a small ring buffer of struct drm_atomic_state objects active,
 * such that they may be printed by vs_drm_dump_recorded_states() later.
 * It increments their reference count until the point where they
 * are dropped out of the ring buffer.
 *
 * Note that it stores the record within the struct drm_device pointed to by
 * the given state.
 */
void vs_drm_record_state(struct drm_atomic_state *state);

/**
 * vs_drm_recorded_states_prepare() - Write out the drm_atomic_state ring buffer to memory
 * @sh_data: struct containing output buffers to be written
 * @drm_dev: drm device containing recorded states within its private struct
 *
 * Sizes and prepares output buffers to store history of recorded drm states.
 * Both returns the number of output buffers and stores the value in the sh_data
 * structure.
 *
 * Must be followed with a call to vs_drm_recorded_states_destroy().
 *
 * Return: number of buffers stored in output, or negative value on error
 */
int vs_drm_recorded_states_prepare(struct drm_state_history_data *sh_data,
				   struct drm_device *drm_dev);

/**
 * vs_drm_recorded_states_destroy() - Cleans up resources from recording state history
 * @sh_data: struct containing output buffer resources to be released
 *
 * Must be called following vs_drm_recorded_states_prepare().
 */
void vs_drm_recorded_states_destroy(struct drm_state_history_data *sh_data);

#endif /* __VS_DRM_STATE_RECORD_H__ */
