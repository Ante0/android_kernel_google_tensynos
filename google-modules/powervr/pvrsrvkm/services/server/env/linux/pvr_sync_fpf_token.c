/*************************************************************************/ /*!
@File           pvr_sync_fpf_token.c
@Title          Common functions for fast path fence sync token handling.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Common functions for fast path fence sync token operations..
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/bug.h>

#include "img_defs.h"
#include "pvr_intrinsics.h"
#include "pvr_sync_fpf_token.h"

#define BITS_PER_BYTE 8
#define	FILE_NAME "pvr_sync_fpf_token"

#define TOKEN_CONTEXT_SKIPLIST_MAX 16
#define TOKEN_CONTEXT_BITMAP_MAX 1024
#define TOKEN_BLOCK_SIZE_BITS 64
#define TOKEN_BLOCK_NO_ZERO_FOUND -1
#define LOG2_BLOCK_SIZE_SHIFT 6UL
#define EXTRACT_BLOCK_FROM_VALUE(uiToken) ((uiToken) >> (LOG2_BLOCK_SIZE_SHIFT))
#define EXTRACT_BITPOS_FROM_VALUE(uiToken) ((uiToken) & \
                                            (((1UL) << (LOG2_BLOCK_SIZE_SHIFT)) - 1))

struct fpf_token_generator_context
{
	spinlock_t context_lock;
	/* Skip list of uint64 representing 64 bitmap entries in the below list */
	uint64_t skiplist[TOKEN_CONTEXT_SKIPLIST_MAX];
	/* Bit map of tokens in the unsigned uint16 range */
	uint64_t bitmap[TOKEN_CONTEXT_BITMAP_MAX];
};

#if !defined(PVR_CTZLL)

/* Compat implementation of gcc intrinsic __builtin_ctzll */
static
int pvr_sync_fpf_compat_builtin_ctzll(unsigned long entry)
{
	unsigned int i;

	if (entry == 0)
	{
		return TOKEN_BLOCK_NO_ZERO_FOUND;
	}

	/* For each bit in the int */
	for (i = 0; i < sizeof(uint64_t) * BITS_PER_BYTE; i++)
	{
		unsigned long bit_mask = 1UL << i;

		if ((entry & bit_mask) == bit_mask)
		{
			return i;
		}
	}

	/* Should never reach this */
	return TOKEN_BLOCK_NO_ZERO_FOUND;
}

#define PVR_CTZLL(uiEntry) (pvr_sync_fpf_compat_builtin_ctzll(uiEntry))

#endif

static
int pvr_sync_fpf_find_trailing_zero(uint64_t entry)
{
	if (entry == UINT64_MAX)
	{
		return TOKEN_BLOCK_NO_ZERO_FOUND;
	}

	return PVR_CTZLL(~entry);
}


int
pvr_sync_fpf_instantiate_token_generator(struct fpf_token_generator_context **context)
{
	struct fpf_token_generator_context *context_local;
	int err = 0;

	context_local = kzalloc(sizeof(*context_local), GFP_KERNEL);
	if (!context_local)
	{
		pr_err(FILE_NAME ": %s Failed to alloc token context\n", __func__);
		err = -ENOMEM;
		goto fail_alloc;
	}

	spin_lock_init(&context_local->context_lock);

	/* Mark token 0 and 65535 as allocated. These tokens are reserved. */
	context_local->bitmap[0] = 1;
	context_local->bitmap[TOKEN_CONTEXT_BITMAP_MAX - 1] = (uint64_t)1 << 63;

	*context = context_local;

fail_alloc:
	return err;
}

void
pvr_sync_fpf_destroy_token_generator(struct fpf_token_generator_context *context)
{
	if (context)
	{
		kfree(context);
	}
}

int
pvr_sync_fpf_acquire_token(struct fpf_token_generator_context *context,
                           PVRSRV_FAST_PATH_FENCE_TOKEN *token)
{
	unsigned int skiplist_iterator;
	uint64_t *skiplist_entry;
	int skiplist_entry_bitpos;
	unsigned int bitmap_block;
	int bitmap_pos;
	uint64_t *bitmap_entry;
	IMG_UINT16 token_local;
	int err;
	unsigned long flags = 0;

	spin_lock_irqsave(&context->context_lock, flags);

	/* Find the first skiplist entry that isn't full. */
	for (skiplist_iterator = 0;
		 skiplist_iterator < TOKEN_CONTEXT_SKIPLIST_MAX;
		 skiplist_iterator++)
	{
		skiplist_entry = &context->skiplist[skiplist_iterator];

		skiplist_entry_bitpos = pvr_sync_fpf_find_trailing_zero(*skiplist_entry);
		/* This block of 64 is full, check the next */
		if (skiplist_entry_bitpos == TOKEN_BLOCK_NO_ZERO_FOUND)
		{
			continue;
		}

		bitmap_block = (skiplist_iterator * TOKEN_BLOCK_SIZE_BITS) + skiplist_entry_bitpos;
		break;
	}

	/* All entries in skip list are full, we have no Tokens remaining */
	if (skiplist_iterator == TOKEN_CONTEXT_SKIPLIST_MAX)
	{
		pr_warn(FILE_NAME ": %s FPF token in flight limit reached uint16 max", __func__);
		err = -ENOMEM;
		goto fail_unlock;
	}

	/* Find the first free entry within that uint64 using PVR_FFS. */
	bitmap_entry = &context->bitmap[bitmap_block];
	bitmap_pos = pvr_sync_fpf_find_trailing_zero(*bitmap_entry);
	if (bitmap_pos == TOKEN_BLOCK_NO_ZERO_FOUND)
	{
		pr_err(FILE_NAME ": %s error token block full", __func__);
		err = -EINVAL;
		goto fail_unlock;
	}

	token_local = (IMG_UINT16)((bitmap_block * 64) + bitmap_pos);

	/* Mark that token as taken */
	BIT_SET(*bitmap_entry, bitmap_pos);

	/* Mark entry in skiplist block. */
	if (*bitmap_entry == UINT64_MAX)
	{
		BIT_SET(*skiplist_entry, skiplist_entry_bitpos);
	}

	spin_unlock_irqrestore(&context->context_lock, flags);

	*token = token_local;

	return 0;

fail_unlock:
	spin_unlock_irqrestore(&context->context_lock, flags);
	return err;
}

int
pvr_sync_fpf_release_token(struct fpf_token_generator_context *context,
                           PVRSRV_FAST_PATH_FENCE_TOKEN token)
{
	unsigned int bitmap_block = EXTRACT_BLOCK_FROM_VALUE(token);
	unsigned int bitmap_bitpos = EXTRACT_BITPOS_FROM_VALUE(token);
	uint64_t *bitmap_entry_live = &context->bitmap[bitmap_block];
	uint64_t bitmap_entry_snapshot = *bitmap_entry_live;
	unsigned int skiplist_block = EXTRACT_BLOCK_FROM_VALUE(bitmap_block);
	unsigned int skiplist_bitpos = EXTRACT_BITPOS_FROM_VALUE(bitmap_block);
	uint64_t *skiplist_entry = &context->bitmap[skiplist_block];
	int err = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&context->context_lock, flags);

	if (!BIT_ISSET(bitmap_entry_snapshot, bitmap_bitpos))
	{
		pr_err(FILE_NAME ": %s attempt to release free FPF Token", __func__);
		err = -EINVAL;
		goto unlock_return;
	}

	BIT_UNSET(*bitmap_entry_live, bitmap_bitpos);

	/* If old value of Block was not uint max we don't need to update the skiplist as it still won't be maxed */
	if (bitmap_entry_snapshot != UINT64_MAX)
	{
		goto unlock_return;
	}

	/* If the block was full make sure we adjust the skiplist to reflect the new state */
	BIT_UNSET(*skiplist_entry, skiplist_bitpos);

	spin_unlock_irqrestore(&context->context_lock, flags);

	return 0;

unlock_return:
	spin_unlock_irqrestore(&context->context_lock, flags);
	return err;
}
