/* SPDX-License-Identifier: GPL-2.0
 *
 * Synaptics TouchCom touchscreen driver
 *
 * Copyright (C) 2017-2024 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

/**
 * @file syna_tcm2_runtime.h
 *
 * This file abstracts platform-specific headers and C runtime APIs.
 */

#ifndef _SYNAPTICS_TCM2_C_RUNTIME_H_
#define _SYNAPTICS_TCM2_C_RUNTIME_H_

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/input/mt.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/crc32.h>
#include <linux/firmware.h>
#if defined(CONFIG_DRM_BRIDGE)
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#elif defined(CONFIG_FB)
#include <linux/fb.h>
#include <linux/notifier.h>
#endif
#include <linux/fs.h>
#include <linux/moduleparam.h>


#if defined(__LP64__) || defined(_LP64)
#define BUILD_64
#endif

/**
 * Build for the target TouchComm protocol interface;
 * typically, these are controlled by the deconfig file
 *
 * TOUCHCOMM_VERSION_1_ONLY   activate when interacting with TouchComm version 1 only
 * TOUCHCOMM_VERSION_2_ONLY   activate when interacting with TouchComm version 2 only
 *
 */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TOUCHCOMM_VERSION_1) && \
	!defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TOUCHCOMM_VERSION_2)
#define TOUCHCOMM_VERSION_1_ONLY
#endif
#if !defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TOUCHCOMM_VERSION_1) && \
	defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TOUCHCOMM_VERSION_2)
#define TOUCHCOMM_VERSION_2_ONLY
#endif

#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TOUCHCOMM_VERSION_2_LEGACY)
#define TOUCHCOMM_VERSION_2_LEGACY_FW
#endif


/**
 * For linux kernel, managed interface was created for resources commonly
 * used by device drivers using devres.
 *
 * Open if willing to use managed-APIs rather than legacy APIs.
 */
#define DEV_MANAGED_API

#if defined(DEV_MANAGED_API)
extern struct device *syna_request_managed_device(void);
#endif

/**
 * For few specific platforms, data alignment on the bus is required.
 * The followings are a few configurations.
 *      ALIGNMENT_BASE          : the base value against which the alignment
 *                                of the value is determined.
 *      ALIGNMENT_SIZE_BOUNDARY : the minimum size of data requiring to do
 *                                alignment.
 *
 * Open if willing to enable data alignment.
 */
#define DATA_ALIGNMENT
#if defined(DATA_ALIGNMENT)
#if IS_ENABLED(CONFIG_SPI_S3C64XX_GS)
#define ALIGNMENT_BASE (4)
#define ALIGNMENT_SIZE_BOUNDARY (64)
#else
#define ALIGNMENT_BASE (16)
#define ALIGNMENT_SIZE_BOUNDARY (256)
#endif /* IS_ENABLED(CONFIG_SPI_S3C64XX_GS) */
#endif /* DATA_ALIGNMENT */

/*
 * Abstractions of LOG features
 */
#undef pr_fmt
#define pr_fmt(fmt) "gtd: syna: " fmt

#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_DEBUG_MSG)
#define LOGD(log, ...) pr_info(log, ##__VA_ARGS__)
#else
#define LOGD(log, ...) pr_debug(log, ##__VA_ARGS__)
#endif
#define LOGI(log, ...) pr_info(log, ##__VA_ARGS__)
#define LOGN(log, ...) pr_notice(log, ##__VA_ARGS__)
#define LOGW(log, ...) pr_warn(log, ##__VA_ARGS__)
#define LOGE(log, ...) pr_err(log, ##__VA_ARGS__)



/*
 * Abstractions of data comparison
 */
#define GET_BIT(var, pos) \
	(((var) & (1 << (pos))) >> (pos))

/*
 * Abstractions of C Atomic operations
 */
typedef atomic_t syna_pal_atomic_t;

#define ATOMIC_SET(atomic, value) \
	atomic_set(&atomic, value)

#define ATOMIC_GET(atomic) \
	atomic_read(&atomic)


/*
 * Abstractions of integer calculation
 */

/**
 * @brief  convert 2-byte data in little-endianness to an unsigned integer
 *
 * @param
 *    [ in] src: 2-byte data in little-endianness
 *
 * @return
 *    an unsigned integer being converted
 */
static inline unsigned int syna_pal_le2_to_uint(const unsigned char *src)
{
	return (unsigned int)src[0] +
		(unsigned int)src[1] * 0x100;
}
/**
 * @brief  Convert 4-byte data in little-endianness to an unsigned integer
 *
 * @param
 *    [ in] src: 4-byte data in little-endianness
 *
 * @return
 *    an unsigned integer being converted
 */
static inline unsigned int syna_pal_le4_to_uint(const unsigned char *src)
{
	return (unsigned int)src[0] +
		(unsigned int)src[1] * 0x100 +
		(unsigned int)src[2] * 0x10000 +
		(unsigned int)src[3] * 0x1000000;
}
/**
 * @brief  Calculate the ceiling of the integer division
 *
 * @param
 *    [ in] dividend: the dividend value
 *    [ in] divisor:  the divisor value
 *
 * @return
 *    the ceiling of the integer division
 */
static inline unsigned int syna_pal_ceil_div(unsigned int dividend,
		unsigned int divisor)
{
	return (dividend + divisor - 1) / divisor;
}
#ifdef DATA_ALIGNMENT
/**
 * @brief  Find the maximum multiple of M that is less than or equal to N
 *
 * @param
 *    [ in] N: the upper limit
 *    [ in] M: the divisor
 *
 * @return
 *    an unsigned integer representing the maximum multiple of M
 *    that is less than or equal to N.
 */
static inline unsigned int syna_pal_get_max_multiple(unsigned int N,
	unsigned int M)
{
	return (N / M) * M;
}
/**
 * @brief  Retrieves the alignment of a value.
 *
 * @param
 *    [ in] value: the value whose alignment is to be calculated.
 *    [ in] base: the base value against which the alignment of the value is determined.
 *
 * @return
 *   an unsigned integer representing the alignment of the value within the base.
 */
static inline unsigned int syna_pal_get_alignment(unsigned int value,
	unsigned int base)
{
	if ((value % base) == 0)
		return value;
	return syna_pal_get_max_multiple(value, base);
}

#endif
/*
 * Abstractions of C runtime for memory management
 */

/**
 * @brief  Allocate a block of memory.
 *
 * @param
 *    [ in] num:  number of elements for an array
 *    [ in] size: number of bytes for each elements
 *
 * @return
 *    On success, a pointer to the memory block allocated by the function.
 */
static inline void *syna_pal_mem_alloc(unsigned int num, unsigned int size)
{
#ifdef DEV_MANAGED_API
	struct device *dev = syna_request_managed_device();

	if (!dev) {
		LOGE("Invalid managed device\n");
		return NULL;
	}
#endif

	if ((int)(num * size) <= 0) {
		LOGE("Invalid parameter\n");
		return NULL;
	}

#ifdef DEV_MANAGED_API
	return devm_kcalloc(dev, num, size, GFP_KERNEL);
#else /* Legacy API */
	return kcalloc(num, size, GFP_KERNEL);
#endif
}
/**
 * @brief  Deallocate a block of memory previously allocated.
 *
 * @param
 *    [ in] ptr: a memory block  previously allocated
 *
 * @return
 *    void.
 */
static inline void syna_pal_mem_free(void *ptr)
{
#ifdef DEV_MANAGED_API
	struct device *dev = syna_request_managed_device();

	if (!dev) {
		LOGE("Invalid managed device\n");
		return;
	}

	if (ptr)
		devm_kfree(dev, ptr);
#else /* Legacy API */
	kfree(ptr);
#endif
}
/**
 * @brief  Fill memory with a constant byte
 *
 * @param
 *    [ in] ptr: pointer to a memory block
 *    [ in] c:   the constant value
 *    [ in] n:   number of byte being set
 *
 * @return
 *    void.
 */
static inline void syna_pal_mem_set(void *ptr, int c, unsigned int n)
{
	memset(ptr, c, n);
}
/**
 * @brief  Copy the data from the location to the memory block pointed
 *         to by destination.
 *
 * @param
 *    [out] dest:      pointer to the destination space
 *    [ in] dest_size: size of destination array
 *    [ in] src:       pointer to the source of data to be copied
 *    [ in] src_size:  size of source array
 *    [ in] num:       number of bytes to copy
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static inline int syna_pal_mem_cpy(void *dest, unsigned int dest_size,
		const void *src, unsigned int src_size, unsigned int num)
{
	if (dest == NULL || src == NULL)
		return -EINVAL;

	if (num > dest_size || num > src_size) {
		LOGE("Invalid size. src:%d, dest:%d, size to copy:%d\n",
			src_size, dest_size, num);
		return -EINVAL;
	}

	memcpy((void *)dest, (const void *)src, num);

	return 0;
}



/*
 * Abstractions of C runtime for mutex
 */

typedef struct mutex syna_pal_mutex_t;

/**
 * @brief  Create a mutex object.
 *
 * @param
 *    [out] ptr: pointer to the mutex handle being allocated
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
#define syna_pal_mutex_alloc(ptr) ({ mutex_init(ptr); 0; })
/**
 * @brief  Release the mutex object previously allocated.
 *
 * @param
 *    [ in] ptr: mutex handle previously allocated
 *
 * @return
 *    void.
 */
static inline void syna_pal_mutex_free(syna_pal_mutex_t *ptr)
{
	/* do nothing */
}
/**
 * @brief  Acquire/lock the mutex.
 *
 * @param
 *    [ in] ptr: a mutex handle
 *
 * @return
 *    void.
 */
static inline void syna_pal_mutex_lock(syna_pal_mutex_t *ptr)
{
	mutex_lock(ptr);
}
/**
 * @brief  Unlock the locked mutex.
 *
 * @param
 *    [ in] ptr: a mutex handle
 *
 * @return
 *    void.
 */
static inline void syna_pal_mutex_unlock(syna_pal_mutex_t *ptr)
{
	mutex_unlock(ptr);
}


/*
 * Abstractions of completion event
 */

typedef struct completion syna_pal_completion_t;

/**
 * @brief  Allocate a completion event, and the default state is not set.
 *         Caller must reset the event before each use.
 *
 * @param
 *    [out] ptr: pointer to the completion handle being allocated
 *
 * @return
 *   0 in case of success, a negative value otherwise.
 */
static inline int syna_pal_completion_alloc(syna_pal_completion_t *ptr)
{
	init_completion((struct completion *)ptr);
	return 0;
}
/**
 * @brief  Release the completion event previously allocated
 *
 * @param
 *    [ in] ptr: the completion event previously allocated
 event
 * @return
 *    void.
 */
static inline void syna_pal_completion_free(syna_pal_completion_t *ptr)
{
	/* do nothing */
}
/**
 * @brief  Complete the completion event being waiting for
 *
 * @param
 *    [ in] ptr: the completion event
 *
 * @return
 *    void.
 */
static inline void syna_pal_completion_complete(syna_pal_completion_t *ptr)
{
	if (!completion_done((struct completion *)ptr))
		complete_all((struct completion *)ptr);
}
/**
 * @brief  Reset or reinitialize the completion event
 *
 * @param
 *    [ in] ptr: the completion event
 *
 * @return
 *    void.
 */
static inline void syna_pal_completion_reset(syna_pal_completion_t *ptr)
{
#if (KERNEL_VERSION(3, 13, 0) > LINUX_VERSION_CODE)
		init_completion((struct completion *)ptr);
#else
		reinit_completion((struct completion *)ptr);
#endif
}
/**
 * @brief  Wait for the completion event during the given time slot
 *
 * @param
 *    [ in] ptr:        the completion event
 *    [ in] timeout_ms: time frame in milliseconds
 *
 * @return
 *    0 if a signal is received; otherwise, on timeout or error occurs.
 */
static inline int syna_pal_completion_wait_for(syna_pal_completion_t *ptr,
		unsigned int timeout_ms)
{
	int retval;

	retval = wait_for_completion_timeout((struct completion *)ptr,
			msecs_to_jiffies(timeout_ms));
	if (retval == 0) /* timeout occurs */
		return -1;

	return 0;
}



/*
 * Abstractions of sleep function
 */

/**
 * @brief  Sleep for a fixed amount of time in milliseconds
 *
 * @param
 *    [ in] time_ms: time frame in milliseconds
 *
 * @return
 *    void.
 */
static inline void syna_pal_sleep_ms(int time_ms)
{
	if (time_ms <= 0)
		return;

	if (time_ms > 20)
		msleep(time_ms);
	else
		usleep_range(time_ms * USEC_PER_MSEC, time_ms * USEC_PER_MSEC + 1);
}
/**
 * @brief  Sleep for a range of time in microseconds
 *
 * @param
 *    [ in] time_us_min: the min. time frame in microseconds
 *    [ in] time_us_max: the max. time frame in microseconds
 *
 * @return
 *    void.
 */
static inline void syna_pal_sleep_us(int time_us_min, int time_us_max)
{
	if ((time_us_min <= 0) || (time_us_max <= 0))
		return;

	if (time_us_max < time_us_min)
		time_us_max = time_us_min;

	usleep_range(time_us_min, time_us_max);
}
/**
 * @brief  Busy wait for a fixed amount of time in milliseconds
 *
 * @param
 *    [ in] time_ms: time frame in milliseconds
 *
 * @return
 *    void.
 */
static inline void syna_pal_busy_delay_ms(int time_ms)
{
	if (time_ms <= 0)
		return;

	mdelay(time_ms);
}


/*
 * Abstractions of string operations
 */

/**
 * @brief  Return the length of C string
 *
 * @param
 *    [ in] str:  an array of characters
 *
 * @return
 *    the length of given string
 */
static inline unsigned int syna_pal_str_len(const char *str)
{
	return (unsigned int)strlen(str);
}
/**
 * @brief  Copy the C string pointed by source into the array pointed by destination.
 *
 * @param
 *    [ in] dest:      pointer to the destination C string
 *    [ in] dest_size: size of destination C string
 *    [out] src:       pointer to the source of C string to be copied
 *    [ in] src_size:  size of source C string
 *    [ in] num:       number of bytes to copy
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static inline int syna_pal_str_cpy(char *dest, unsigned int dest_size,
		const char *src, unsigned int src_size, unsigned int num)
{
	if (dest == NULL || src == NULL)
		return -EINVAL;

	if (num > dest_size || num > src_size) {
		LOGE("Invalid size. src:%d, dest:%d, num:%d\n",
			src_size, dest_size, num);
		return -EINVAL;
	}

	strncpy(dest, src, num);

	return 0;
}
/**
 * @brief  Compares up to num characters between two C strings.
 *
 * @param
 *    [ in] str1: C string to be compared
 *    [ in] str2: C string to be compared
 *    [ in] num:  number of characters to compare
 *
 * @return
 *    0 if both strings are equal; otherwise, not equal.
 */
static inline int syna_pal_str_cmp(const char *str1, const char *str2,
		unsigned int num)
{
	return strncmp(str1, str2, num);
}
/**
 * @brief  Convert the given string in hex to an integer returned
 *
 * @param
 *    [ in] str:    C string to be converted
 *    [ in] length: target length
 *
 * @return
 *    An integer converted
 */
static inline unsigned int syna_pal_hex_to_uint(char *str, int length)
{
	unsigned int result = 0;
	char *ptr = NULL;

	for (ptr = str; ptr != str + length; ++ptr) {
		result <<= 4;
		if (*ptr >= 'A')
			result += *ptr - 'A' + 10;
		else
			result += *ptr - '0';
	}

	return result;
}


/*
 * Abstractions of CRC functions
 */

/**
 * @brief  Calculates the CRC32 value of the data
 *
 * @param
 *    [ in] seed: the previous crc32 value
 *    [ in] data: byte data for the calculation
 *    [ in] len:  the byte length of the data.
 *
 * @return
 *    0 if both strings are equal; otherwise, not equal.
 */
static inline unsigned int syna_pal_crc32(unsigned int seed,
		const char *data, unsigned int len)
{
	return crc32(seed, data, len);
}


#endif /* end of _SYNAPTICS_TCM2_C_RUNTIME_H_ */
