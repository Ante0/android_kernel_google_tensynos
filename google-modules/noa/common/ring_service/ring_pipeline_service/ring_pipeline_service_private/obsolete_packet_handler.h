/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Ring Pipeline Service Obsolete Packet Handler
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_OBSOLETE_PACKET_HANDLER_H
#define NOA_RING_PIPELINE_SERVICE_OBSOLETE_PACKET_HANDLER_H

#ifdef linux
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

/**
 * @brief Initializes the ObsoletePacketHandler's stage.
 *
 * This function initializes the stage within the ObsoletePacketHandler.
 * It sets up the stage by binding it to the provided engine, container,
 * and scheduler. This binding allows the stage to interact with these
 * components during packet processing.
 *
 * @param[in] stage The stage to be initialized.
 * @param[in] scheduler The task scheduler to be associated with the
 * stage.
 * @param[in] container The packet container to be used by the stage.
 * @param[in] engine The engine to be used by the stage.
 */
void ObsoletePacketHandlerStageSeup(struct NepStage *stage, struct NepTaskScheduler *scheduler,
				    struct PktContainer *container, struct NepEngine *engine);
/**
 * @brief Handles obsolete packets by searching for recyclable buffers.
 *
 * This function is a runnable task that searches the recycle_bitmap
 * for buffers that need to be recycled. It then uses the
 * RingServiceObsoletePacketRecycle API to process these buffers,
 * either recycling them or forwarding the associated packets to
 * the driver for further handling.
 *
 * @param[in] task The runnable task instance.
 * @param[in] data Additional data that can be passed to the task.
 *
 * @return 0 on success; negative error code on failure.
 */
int32_t ObsoletePacketHandleTask(struct NepRunnableTask *task, void *data);

#endif /* NOA_RING_PIPELINE_SERVICE_OBSOLETE_PACKET_HANDLER_H */
