/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC
 */

#ifndef SYSPM_INTERFACE_TYPES_H
#define SYSPM_INTERFACE_TYPES_H

/**
 * File should mirror interfaces/protocols/syspm/syspm_interface_types.h from
 * the source firmware repository.
 */

/*
 * @brief Enumerates synchronization events related to resource power state.
 */
enum syspm_sync_event_t {
	/*
	* Indicates that a resource activation sequence has started.
	* Clients receiving this event can prepare for the upcoming
	* activation but should not yet access the resource. Clients
	* receiving this event implicitly acquire a Sync Hold on the
	* resource. The client shall release this Sync Hold when its
	* operation is complete by calling the
	* `syspm_release_sync_hold` API.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_ACTIVATION_INTENT,

	/*
	* Indicates that a resource is about to become active.
	* The resource is transitioning to an active state and is available
	* for final initialization. Clients may perform last-minute setup
	* before the resource becomes fully operational. Clients
	* receiving this event implicitly acquire a Sync Hold on the
	* resource. The client shall release this Sync Hold when its
	* operation is complete by calling the
	* `syspm_release_sync_hold` API.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_ACTIVATION_IMMINENT,

	/*
	* Indicates that a resource activation sequence has been terminated
	* before completion. Clients should clean up any preparatory actions
	* taken in response to a previous activation intent event.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_ACTIVATION_ABORTED,

	/*
	* Indicates that a resource is now active and ready for use.
	* Clients can now freely access and utilize the resource.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_ACTIVATED,

	/*
	* Indicates that a resource was already active when the client registered
	* to receive synchronization events.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_ACTIVE_AT_REGISTRATION,

	/*
	* Indicates that a resource deactivation sequence has started.
	* The resource is still accessible, but clients should begin to prepare
	* for its eventual deactivation. Clients might perform final cleanup
	* operations while the resource is still accessible. Clients
	* receiving this event implicitly acquire a Sync Hold on the
	* resource. The client shall release this Sync Hold when its
	* operation is complete by calling the
	* `syspm_release_sync_hold` API.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_DEACTIVATION_INTENT,

	/*
	* Indicates that a resource is about to become inactive.
	* The resource is no longer accessible and clients can only perform
	* cleanup operations that do not require resource access. Clients
	* receiving this event implicitly acquire a Sync Hold on the
	* resource. The client shall release this Sync Hold when its
	* operation is complete by calling the
	* `syspm_release_sync_hold` API.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_DEACTIVATION_IMMINENT,

	/*
	* Indicates that a resource deactivation sequence has been terminated
	* before completion. Clients may need to adjust their operations based
	* on the fact that the resource will remain active for the time being.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_DEACTIVATION_ABORTED,

	/*
	* Indicates that a resource is now inactive.
	* The resource is no longer valid for access or use. Clients must not
	* attempt to access the resource.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_DEACTIVATED,

	/*
	* Indicates that a resource was already inactive when the client registered
	* to receive synchronization events.
	*/
	SYSPM_SYNC_EVENT_RESOURCE_INACTIVE_AT_REGISTRATION,

	SYSPM_SYNC_EVENT_COUNT
};

/*
 * @brief Enumerates possible resource status values.
 */
enum syspm_resource_status_t {
	/* Resource status is unknown. */
	SYSPM_RESOURCE_STATUS_UNKNOWN,

	/* Resource is currently active. */
	SYSPM_RESOURCE_STATUS_ACTIVE,

	/* Resource is currently inactive. */
	SYSPM_RESOURCE_STATUS_INACTIVE,
	/* Resource is waiting for release of sync holds before activation. */
	SYSPM_RESOURCE_STATUS_ACTIVATION_PENDING,

	/* Resource is actively changing state to active. */
	SYSPM_RESOURCE_STATUS_ACTIVATION_IN_PROGRESS,

	/* Resource is waiting for release of sync holds before deactivation. */
	SYSPM_RESOURCE_STATUS_DEACTIVATION_PENDING,

	/* Resource is actively changing state to inactive. */
	SYSPM_RESOURCE_STATUS_DEACTIVATION_IN_PROGRESS
};

/*
 * @brief Enumeration of result codes for vote operations.
 */
enum syspm_vote_result_t {
	/* The synchronous vote operation was successful. */
	SYSPM_VOTE_RESULT_SYNC_OK,

	/* The asynchronous vote operation was initiated. */
	SYSPM_VOTE_RESULT_ASYNC_INITIATED,

	/* The conditional vote request has been granted because the resource is
	inactive. */
	SYSPM_VOTE_RESULT_RESOURCE_INACTIVE,

	/* The client is not authorized to request a vote. */
	SYSPM_VOTE_RESULT_VOTE_UNAUTHORIZED,

	/* The client is not authorized to request a conditional vote. */
	SYSPM_VOTE_RESULT_CONDITIONAL_VOTE_UNAUTHORIZED,

	/* The client already has a vote on the resource. */
	SYSPM_VOTE_RESULT_ALREADY_VOTED,

	/* The client is attempting to remove a vote that does not exist. */
	SYSPM_VOTE_RESULT_VOTE_NOT_FOUND,

	/* The client ID or resource ID provided for the vote operation is invalid. */
	SYSPM_VOTE_RESULT_INVALID_ARGS,
};

/*
 * @brief This enumeration defines a client's vote states.
 */
enum syspm_client_vote_state_t {
	/* Client does not have a vote for the resource. */
	SYSPM_CLIENT_VOTE_STATE_NO_VOTE = 0,

	/* Client has a vote for the resource. */
	SYSPM_CLIENT_VOTE_STATE_HAS_VOTE = 1,

	/* Client has a pending vote for the resource. */
	SYSPM_CLIENT_VOTE_STATE_VOTE_PENDING = 2,

	/* The client ID or resource ID provided for the operation is invalid. */
	SYSPM_CLIENT_VOTE_STATE_INVALID_ARGS_ID,
};

#endif /* SYSPM_INTERFACE_TYPES_H */
