/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2022 Google LLC.
 *
 * Author: Vinay Kalia <vinaykalia@google.com>
 */

#ifndef _UAPI_VPU_H_
#define _UAPI_VPU_H_

#include <linux/ioctl.h>

#include <linux/compiler.h>
#include <linux/types.h>

#define VPU_IOC_MAGIC 'B'

#define _VPU_IO(nr) _IO(VPU_IOC_MAGIC, nr)
#define _VPU_IOR(nr, size) _IOR(VPU_IOC_MAGIC, nr, size)
#define _VPU_IOW(nr, size) _IOW(VPU_IOC_MAGIC, nr, size)
#define _VPU_IOWR(nr, size) _IOWR(VPU_IOC_MAGIC, nr, size)

#define MAX_HEAP_NAME 32
#define VPU_CRASH_INFO_LEN 128

enum cpu_cmd_id {
	VPU_CMD_REG_SZ,
	VPU_CMD_OPEN,
	VPU_CMD_CLOSE,
	VPU_WAIT_INTERRUPT,
	VPU_SIGNAL_INTERRUPT,
	VPU_ALLOC_DMABUF,
	VPU_FREE_DMABUF,
	VPU_GET_IOVA,
	VPU_PUT_IOVA,
	VPU_SET_CLK_RATE,
	VPU_SET_BW,
	VPU_RESET,
	VPU_DMA_BUF_SYNC,
	VPU_SSCD_COREDUMP,
	VPU_NOTIFY_IDLE,
	VPU_SEND_FW_CMD,
};
/* <END OF HELPERS> */

#define VPU_NO_INST (__u32)-1

struct vpu_fw_cmd {
	__u32 cmd;
	__u32 codec;
	__u32 index;
};

struct vpu_dmabuf {
	__u32 inst_idx;
	__u32 size;
	__s32 fd;
	__u32 iova;
	char heap_name[MAX_HEAP_NAME];
	__u32 skip_cmo;
};

struct vpu_intr_info {
	__u32 inst_idx;
	__u32 timeout;
	__u32 reason;
};

struct vpu_bandwidth_info {
	// read constraints in MBytes
	__u32 read_avg_bw;
	__u32 read_peak_bw;
	// write constraints in MBytes
	__u32 write_avg_bw;
	__u32 write_peak_bw;
};

/**
 * @flags: Set of access flags defined in uapi/linux/dma-buf.h
 */
struct vpu_buf_sync {
	int fd;
	__u32 offset;
	__u32 size;
	__u64 flags;
};

struct vpu_coredump_info {
	__u32 size;
	__u32 fd;
	char crash_info[VPU_CRASH_INFO_LEN];
};

#define VPU_IOCX_GET_REG_SZ		_VPU_IOR(VPU_CMD_REG_SZ, __u32)
#define VPU_IOCX_OPEN_INSTANCE		_VPU_IOW(VPU_CMD_OPEN, __u32)
#define VPU_IOCX_CLOSE_INSTANCE		_VPU_IOW(VPU_CMD_CLOSE, __u32)
#define VPU_IOCX_ALLOC_DMABUF		_VPU_IOWR(VPU_ALLOC_DMABUF, struct vpu_dmabuf)
#define VPU_IOCX_FREE_DMABUF		_VPU_IOWR(VPU_FREE_DMABUF, struct vpu_dmabuf)
#define VPU_IOCX_GET_IOVA		_VPU_IOWR(VPU_GET_IOVA, struct vpu_dmabuf)
#define VPU_IOCX_PUT_IOVA		_VPU_IOWR(VPU_PUT_IOVA, struct vpu_dmabuf)
#define VPU_IOCX_WAIT_INTERRUPT		_VPU_IOWR(VPU_WAIT_INTERRUPT, struct vpu_intr_info)
#define VPU_IOCX_SIGNAL_INTERRUPT	_VPU_IOW(VPU_SIGNAL_INTERRUPT, struct vpu_intr_info)
#define VPU_IOCX_SET_CLK_RATE		_VPU_IOW(VPU_SET_CLK_RATE, __u32)
#define VPU_IOCX_SET_BW			_VPU_IOW(VPU_SET_BW, struct vpu_bandwidth_info)
#define VPU_IOCX_DMA_BUF_SYNC		_VPU_IOW(VPU_DMA_BUF_SYNC, struct vpu_buf_sync)
#define VPU_IOCX_SSCD_COREDUMP		_VPU_IOW(VPU_SSCD_COREDUMP, struct vpu_coredump_info)
#define VPU_IOCX_NOTIFY_IDLE		_VPU_IOW(VPU_NOTIFY_IDLE, __u32)
#define VPU_IOCX_SEND_FW_CMD		_VPU_IOW(VPU_SEND_FW_CMD, struct vpu_fw_cmd)

#endif /* _UAPI_VPU_H_ */
