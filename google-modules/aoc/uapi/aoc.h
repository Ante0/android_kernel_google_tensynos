/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Google Whitechapel AoC Core Driver
 *
 * Copyright (c) 2020 Google LLC
 *
 */

#define AOC_IOCTL_MAGIC 0xac

#define AOC_IS_ONLINE _IOR(AOC_IOCTL_MAGIC, 5, int)

struct aoc_ion_handle {
	__u32 handle;
	__s32 fd;
};

struct aoc_tpu_offload_chunk_config {
	__s16 offset;    /* relative to the base address of batch */
	__u8 channels;  /* 1, ... */
	__u8 bits;      /* 8, 16, 24, 32 */
	__u32 sample_rate;
	__u8 format;
	__u8 is_interleave;
	__u8 padding[2];
};

#define AOC_IOCTL_TPU_OFFLOAD_BATCH_COUNT_MAX 4

struct aoc_tpu_offload_batch_config {
	/* IIF FDs */
	__s32 aoc_ready_fence;
	__s32 tpu_ready_fence;

	__s16 offset;  /* relative to the base address of buffer */
	__u8 padding[6];
};

struct aoc_tpu_offload_config {
	__s32 tpu_offload_id;
	__s32 dmabuf_fd;  /* FD of the buffer allocated from tpu_offload_heap */
	__s32 batch_count;
	struct aoc_tpu_offload_batch_config batches[AOC_IOCTL_TPU_OFFLOAD_BATCH_COUNT_MAX];

	__s32 period_ms;
	struct aoc_tpu_offload_chunk_config input_chunk_config;
	struct aoc_tpu_offload_chunk_config output_chunk_config;
	__s32 batch_size; /* including padding and alignments */
	__u8 padding[4];
};

struct aoc_tpu_offload_start {
	__s32 tpu_offload_id;
	__s32 feature;
};

struct aoc_tpu_offload_stop {
	__s32 tpu_offload_id;
	__s32 feature;
};

#define AOC_IOCTL_ION_FD_TO_HANDLE _IOWR(AOC_IOCTL_MAGIC, 204, struct aoc_ion_handle)
#define AOC_IOCTL_DISABLE_MM _IOW(AOC_IOCTL_MAGIC, 205, __u32)
#define AOC_IOCTL_FORCE_VNOM _IOW(AOC_IOCTL_MAGIC, 206, __u32)
#define AOC_IOCTL_ENABLE_UART_TX _IOW(AOC_IOCTL_MAGIC, 207, __u32)
#define AOC_IOCTL_DISABLE_AP_RESETS _IOW(AOC_IOCTL_MAGIC, 208, __u32)
#define AOC_IOCTL_FORCE_SPEAKER_ULTRASONIC _IOW(AOC_IOCTL_MAGIC, 209, __u32)
#define AOC_IOCTL_VOLTE_RELEASE_MIF _IOW(AOC_IOCTL_MAGIC, 210, __u32)
/* Configures the TPU offload pipeline */
#define AOC_IOCTL_TPU_OFFLOAD_CONFIG _IOW(AOC_IOCTL_MAGIC, 211, struct aoc_tpu_offload_config)
/* Starts the TPU offload pipeline */
#define AOC_IOCTL_TPU_OFFLOAD_START _IOW(AOC_IOCTL_MAGIC, 212, struct aoc_tpu_offload_start)
/* Stops the TPU offload pipeline */
#define AOC_IOCTL_TPU_OFFLOAD_STOP _IOW(AOC_IOCTL_MAGIC, 213, struct aoc_tpu_offload_stop)
