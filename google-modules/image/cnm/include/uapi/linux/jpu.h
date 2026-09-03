/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2025 Google LLC.
 *
 * Author: Anastasia Young <anastasiayoung@google.com>
 */

#ifndef __JPU_DRV_H__
#define __JPU_DRV_H__

#include <linux/types.h>
#ifdef __KERNEL__
#else
#include <stdint.h>
#endif

#define JPU_IOCX_MAGIC  'J'

#define _JPU_IO(nr) _IO(JPU_IOCX_MAGIC, nr)
#define _JPU_IOR(nr, size) _IOR(JPU_IOCX_MAGIC, nr, size)
#define _JPU_IOW(nr, size) _IOW(JPU_IOCX_MAGIC, nr, size)
#define _JPU_IOWR(nr, size) _IOWR(JPU_IOCX_MAGIC, nr, size)

enum cpu_cmd_id {
	JPU_CMD_REG_SZ,
	JPU_GET_IOVA,
	JPU_PUT_IOVA,
	JPU_WAIT_INTERRUPT,
	JPU_OPEN_INSTANCE,
	JPU_CLOSE_INSTANCE,
	JPU_HW_RESET,
};

#define JPU_IOCX_GET_REG_SZ _JPU_IOR(JPU_CMD_REG_SZ, __u32)
#define JPU_IOCX_GET_IOVA _JPU_IOWR(JPU_GET_IOVA, struct jpu_dmabuf)
#define JPU_IOCX_PUT_IOVA _JPU_IOWR(JPU_PUT_IOVA, struct jpu_dmabuf)
#define JPU_IOCX_WAIT_INTERRUPT _JPU_IOWR(JPU_WAIT_INTERRUPT, struct jpudrv_intr_info_t)
#define JPU_IOCX_OPEN_INSTANCE _JPU_IO(JPU_OPEN_INSTANCE)
#define JPU_IOCX_CLOSE_INSTANCE _JPU_IO(JPU_CLOSE_INSTANCE)
#define JPU_IOCX_HW_RESET _JPU_IO(JPU_HW_RESET)

struct jpu_dmabuf {
	__u32 size;
	__s32 fd;
	__u32 iova;
	__u32 skip_cmo;
};

struct jpudrv_buffer_t {
	__kernel_size_t size;
	__u64 virt_addr;
};

struct jpudrv_intr_info_t {
	__u32 timeout;
	__s32 intr_reason;
};
#endif
