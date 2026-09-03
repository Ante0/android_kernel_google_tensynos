/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2024 Google LLC
 */

#ifndef _DTS_DEFINE_H_
#define _DTS_DEFINE_H_

#define DEFINE(name1, val) \
	asm volatile("\n.ascii \"->"#name1 " %0 " #val "\"" : : "i" (val))
#define DEFINE2(name1, name2, val) \
	asm volatile("\n.ascii \"->"#name1 "_" #name2 " %0 " #val "\"" : : "i" (val))
#define DEFINE3(name1, name2, name3, val) \
	asm volatile("\n.ascii \"->"#name1 "_" #name2 "_" #name3 " %0 " #val "\"" : : "i" (val))

/**
 * @name:               User name
 * @chan:               Channel of mailbox
 * @service_id:         Service id in MBA protocol in local side
 */
#define MBOX_DEFINE2(name, chan, service_id) 	\
	DEFINE3(MBOX, CHAN, name, chan); 	\
	DEFINE3(MBOX, SERVICE_ID, name, service_id);

/**
 * @name:               User name
 * @chan:               Channel of mailbox
 * @service_id:         Service id in MBA protocol in local side
 * @dest_chan:          Service id in MBA protocol in remote side
 */
#define MBOX_DEFINE3(name, chan, service_id, dest_chan) \
	DEFINE3(MBOX, CHAN, name, chan);		\
	DEFINE3(MBOX, SERVICE_ID, name, service_id);	\
	DEFINE3(MBOX, DEST_CHAN, name, dest_chan);

#endif /* _DTS_DEFINE_H_ */
