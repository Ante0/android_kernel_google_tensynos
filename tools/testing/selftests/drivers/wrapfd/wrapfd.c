/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC.
 * Author: Suren Baghdasaryan <surenb@google.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * WrapFD API tests
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/align.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/wrapfd.h>
#include "../../kselftest_harness.h"

#define offset_in_page(addr, page_size) ((addr) & (page_size - 1))

/* ioctl wrappers */
static inline int wrapfd_wrap(int dev_fd, int fd, int prot)
{
	struct wrapfd_wrap wrap = {
		.fd = fd,
		.prot = prot,
	};

	return ioctl(dev_fd, WRAPFD_DEV_IOC_WRAP, &wrap);
}

static inline int wrapfd_get_state(int wrapfd, unsigned int *state)
{
	struct wrapfd_get_state wrap_state = { 0 };
	int ret;

	ret = ioctl(wrapfd, WRAPFD_DEV_IOC_GET_STATE, &wrap_state);
	if (!ret && state)
		*state = wrap_state.state;

	return ret;
}

static inline int wrapfd_acquire_ownership(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_ACQUIRE_OWNERSHIP, NULL);
}

static inline int wrapfd_release_ownership(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_RELEASE_OWNERSHIP, NULL);
}

static inline int wrapfd_load(int wrapfd, int fd, unsigned long file_offs,
			      unsigned long buf_offs, unsigned long len)
{
	struct wrapfd_load load = {
		.fd = fd,
		.file_offs = file_offs,
		.buf_offs = buf_offs,
		.len = len,
	};

	return ioctl(wrapfd, WRAPFD_DEV_IOC_LOAD, &load);
}

static inline int wrapfd_rewrap(int wrapfd, int prot)
{
	struct wrapfd_rewrap rewrap = {
		.prot = prot,
	};

	return ioctl(wrapfd, WRAPFD_DEV_IOC_REWRAP, &rewrap);
}

static inline int wrapfd_empty(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_EMPTY, NULL);
}

static inline int wrapfd_allow_guests(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_ALLOW_GUESTS, NULL);
}

static inline int wrapfd_prohibit_guests(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_PROHIBIT_GUESTS, NULL);
}

/* test utility functions */
static int dmabuf_heap_alloc(int heap_fd, size_t len, int fd_flags)
{
	struct dma_heap_allocation_data data = {
		.len = len,
		.fd = 0,
		.fd_flags = fd_flags | O_CLOEXEC,
		.heap_flags = 0,
	};
	int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data);

	return ret < 0 ? ret : data.fd;
}

static int dmabuf_sync(int dmabuf_fd, bool start, int access_type)
{
	struct dma_buf_sync sync = {
		.flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) | access_type,
	};

	return ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

static int dmabuf_sync_start(int dmabuf_fd, int access_type)
{
	return dmabuf_sync(dmabuf_fd, true, access_type);
}

static int dmabuf_sync_end(int dmabuf_fd, int access_type)
{
	return dmabuf_sync(dmabuf_fd, false, access_type);
}

static void generate_file_content(char *content, size_t size)
{
	srand(time(NULL));
	/* Generate only printable characters in the range of [32, 126] */
	for (size_t i = 0; i < size; i++)
		content[i] = 32 + rand() % 95;
}

FIXTURE(wrapfd_tests)
{
	size_t page_size;
	char *content;
	size_t size;
	int dev_fd;
	int fd;
};

#define FILE_SZ_PAGES	100

FIXTURE_SETUP(wrapfd_tests)
{
	ssize_t total_wr;
	FILE *ftmp;

	EXPECT_EQ(getuid(), 0)
		SKIP(return, "Skipping all tests as non-root");

	EXPECT_EQ(access("/dev/wrapfd", F_OK), 0)
		SKIP(return, "Skipping all tests; kernel lacks wrapfd support");

	self->page_size = (size_t)sysconf(_SC_PAGESIZE);
	/* Intentionally make the file size page unaligned */
	self->size = self->page_size * FILE_SZ_PAGES - self->page_size / 2;

	self->dev_fd = open("/dev/wrapfd", O_RDONLY);
	ASSERT_TRUE(self->dev_fd >= 0);

	/* Prepare random content buffer */
	self->content = malloc(self->size);
	generate_file_content(self->content, self->size);

	/* Prepare temporary file with the same content */
	ftmp = tmpfile();
	ASSERT_NE(ftmp, NULL);
	self->fd = dup(fileno(ftmp));
	fclose(ftmp);

	total_wr = 0;
	do {
		ssize_t wr = write(self->fd, self->content + total_wr,
				   self->size - total_wr);
		ASSERT_TRUE(wr >= 0);
		total_wr += wr;
	} while (total_wr < self->size);
}

FIXTURE_TEARDOWN(wrapfd_tests)
{
	close(self->fd);
	free(self->content);
	close(self->dev_fd);
}

static int cmp_content(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int wrapfd, unsigned long file_offs,
		       unsigned long buf_offs, unsigned long len)
{
	/* mmap() operates on page aligned offsets and lengths, so handle that here. */
	unsigned long aligned_buf_offs = ALIGN_DOWN(buf_offs, self->page_size);
	unsigned long buf_pg_offs = offset_in_page(buf_offs, self->page_size);
	unsigned long aligned_len = ALIGN(buf_offs + len, self->page_size) - aligned_buf_offs;
	char *ptr;
	int ret;

	ptr = mmap(NULL, aligned_len, PROT_READ, MAP_SHARED, wrapfd, aligned_buf_offs);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ), 0);
	ret = memcmp(self->content + file_offs, ptr + buf_pg_offs, len);
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ), 0);
	ASSERT_EQ(munmap(ptr, aligned_len), 0);

	return ret;
}

static void clear_content(struct __test_metadata *_metadata,
			  FIXTURE_DATA(wrapfd_tests) *self, int wrapfd)
{
	char *ptr;

	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_WRITE), 0);
	memset(ptr, 0, self->size);
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_WRITE), 0);
	ASSERT_EQ(munmap(ptr, self->size), 0);
}

static void test_wrap(struct __test_metadata *_metadata,
		      FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	struct stat sb;

	/* Get state of a non-wrapped fd */
	ASSERT_TRUE(wrapfd_get_state(fd, NULL) &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_get_state(self->dev_fd, NULL) &&
		    errno == ENOTTY);

	/* Wrap and get state of a wrapped fd */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_get_state(wrapfd, NULL), 0);

	/* Ensure that the size of the wrapfd matches the size of the underlying buffer. */
	ASSERT_EQ(fstat(wrapfd, &sb), 0);
	ASSERT_EQ(sb.st_size, ALIGN(self->size, self->page_size));

	close(wrapfd);
}

static void test_wrap_rdonly_file(struct __test_metadata *_metadata,
				  FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	unsigned int state;

	/* Wrapping a read-only file in a read/write wrap should fail. */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_NE(wrapfd, 0);

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_get_state(wrapfd, &state), 0);
	ASSERT_EQ(state, WRAPFD_CONTENT_RDONLY);

	close(wrapfd);
}

static const void *memchr_inv(const void *start, int c, size_t bytes)
{
	const char *src = start;
	char val = c;
	int i;

	for (i = 0; i < bytes; i++)
		if (src[i] != val)
			return &src[i];

	return NULL;
}

static int poison_region(int wrapfd, void *start, int poison, size_t bytes)
{
	int ret = dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_WRITE);

	if (ret)
		return ret;

	memset(start, poison, bytes);

	return dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_WRITE);
}

static int verify_region_poison(int wrapfd, void *start, int poison, size_t bytes)
{
	int ret = dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ);
	bool poisoned;

	if (ret)
		return ret;

	poisoned = memchr_inv(start, poison, bytes) == NULL;

	ret = dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ);
	if (ret)
		return ret;

	return poisoned ? 0 : -1;
}

static int load_and_cmp(struct __test_metadata *_metadata, FIXTURE_DATA(wrapfd_tests) *self,
			int wrapfd, unsigned long file_offs, unsigned long buf_offs,
			unsigned long len)
{
	const char poison = 0xaa;
	int ret;
	char *head_ptr = NULL;
	unsigned long tail_len, tail_aligned_offset, tail_map_len;
	char *tail_page_ptr = NULL;
	char *tail_ptr = NULL;

	clear_content(_metadata, self, wrapfd);

	ret = cmp_content(_metadata, self, wrapfd, 0, 0, self->size);
	EXPECT_NE(ret, 0)
		return ret;

	if (buf_offs) {
		head_ptr = mmap(NULL, buf_offs, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd, 0);
		EXPECT_NE(head_ptr, MAP_FAILED)
			return -1;

		ret = poison_region(wrapfd, head_ptr, poison, buf_offs);
		EXPECT_EQ(ret, 0)
			goto out_unmap_head;
	}

	if ((buf_offs + len) < self->size) {
		tail_len = self->size - buf_offs - len;
		tail_aligned_offset = ALIGN_DOWN(buf_offs + len, self->page_size);
		tail_map_len = self->size - tail_aligned_offset;
		tail_page_ptr = mmap(NULL, tail_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd,
				     tail_aligned_offset);
		EXPECT_NE(tail_page_ptr, MAP_FAILED) {
			ret = -1;
			goto out_unmap_head;
		}

		tail_ptr = tail_page_ptr + offset_in_page(buf_offs + len, self->page_size);
		ret = poison_region(wrapfd, tail_ptr, poison, tail_len);
		EXPECT_EQ(ret, 0)
			goto out_unmap_tail;
	}

	ret = wrapfd_load(wrapfd, self->fd, file_offs, buf_offs, len);
	EXPECT_EQ(ret, 0)
		goto out_unmap_tail;

	if (head_ptr) {
		ret = verify_region_poison(wrapfd, head_ptr, poison, buf_offs);
		EXPECT_EQ(ret, 0)
			goto out_unmap_tail;
	}

	ret = cmp_content(_metadata, self, wrapfd, file_offs, buf_offs, len);
	EXPECT_EQ(ret, 0)
		goto out_unmap_tail;

	if (tail_ptr) {
		ret = verify_region_poison(wrapfd, tail_ptr, poison, tail_len);
		EXPECT_EQ(ret, 0);
	}

out_unmap_tail:
	if (tail_page_ptr)
		EXPECT_EQ(munmap(tail_page_ptr, tail_map_len), 0);
out_unmap_head:
	if (head_ptr)
		EXPECT_EQ(munmap(head_ptr, buf_offs), 0);
	return ret;
}

static int __test_loads(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int wrapfd)
{
	int ret;

	/* Test a sub-page load and ensure only the requested part was written to. */
	ret = load_and_cmp(_metadata, self, wrapfd, 0, 0, self->page_size / 2);
	EXPECT_EQ(ret, 0)
		return ret;

	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, 0, self->page_size / 2);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Test tiny read at random offsets. */
	ret = load_and_cmp(_metadata, self, wrapfd, 100, 0, 50);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Cross a page boundary in the file and then in the buffer. */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size - 1, 0, self->page_size);
	EXPECT_EQ(ret, 0)
		return ret;

	ret = load_and_cmp(_metadata, self, wrapfd, 0, self->page_size - 1, self->page_size);
	EXPECT_EQ(ret, 0)
		return ret;

	/*
	 * Start bounce buffer, large O_DIRECT read and memmove() logic along with end bounce
	 * buffer.
	 */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, self->page_size / 4,
			   self->page_size * 3);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Test logic for fitting everything within the first bounce page. */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, self->page_size / 2,
			   self->page_size / 2);
	EXPECT_EQ(ret, 0)
		return ret;

	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size, self->page_size,
			   self->size - self->page_size);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Save this one for last since subsequent tests run comparisons on the entire buffer. */
	ret = load_and_cmp(_metadata, self, wrapfd, 0, 0, self->size);
	EXPECT_EQ(ret, 0);

	return ret;
}

static void test_load_mapped(struct __test_metadata *_metadata,
			     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Loading should still work while a buffer is mapped. */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	ASSERT_EQ(__test_loads(_metadata, self, wrapfd), 0);

	ASSERT_EQ(munmap(ptr, self->size), 0);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_load_unmapped(struct __test_metadata *_metadata,
			       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	ASSERT_EQ(__test_loads(_metadata, self, wrapfd), 0);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_load_rdonly_file(struct __test_metadata *_metadata,
				  FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	ASSERT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 && errno == EACCES);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_wrap_rdonly(struct __test_metadata *_metadata,
			     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Try mapping as writable */
	ASSERT_EQ(mmap(NULL, self->size, PROT_READ | PROT_WRITE,
		       MAP_SHARED, wrapfd, 0), MAP_FAILED);
	ASSERT_EQ(errno, EACCES);

	close(wrapfd);
}

static void test_wrap_rdwr(struct __test_metadata *_metadata,
			   FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer before modification */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Modify buffer content */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_RW), 0);
	ptr[0]++;

	/* Check content of the buffer after modification */
	ASSERT_NE(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Restore buffer content */
	ptr[0]--;
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_RW), 0);


	/* Confirm the final content */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	ASSERT_EQ(munmap(ptr, self->size), 0);

	close(wrapfd);
}

/* Bionic's implementation of libc does not have a wrapper for remap_file_pages(). */
#ifdef __ANDROID__
static int remap_file_pages(void *addr, size_t size, int prot, size_t pgoff, int flags)
{
	return syscall(__NR_remap_file_pages, addr, size, prot, pgoff, flags);
}
#endif

static void test_remap_file_pages(struct __test_metadata *_metadata,
				  FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	/* remap_file_pages() on the content should succeed */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   fd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(remap_file_pages(ptr, self->page_size, 0, 1, 0), 0);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* remap_file_pages() on the wrapfd should fail with EINVAL error */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(remap_file_pages(ptr, self->page_size, 0, 1, 0), -1);
	ASSERT_EQ(errno, EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	close(wrapfd);
}

static void test_wrap_remap(struct __test_metadata *_metadata,
			    FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	char *ptr, *new_ptr;
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer before modification */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Modify buffer content */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Remap to a new address */
	new_ptr = mremap(ptr, self->size, self->size, MREMAP_MAYMOVE);
	ASSERT_NE(new_ptr, MAP_FAILED);
	ASSERT_EQ(memcmp(self->content, new_ptr, self->size), 0);
	ptr = new_ptr;

	/* Resize the mapping */
	new_ptr = mremap(ptr, self->size, self->size / 2, MREMAP_MAYMOVE);
	ASSERT_NE(new_ptr, MAP_FAILED);
	ASSERT_EQ(memcmp(self->content, new_ptr, self->size / 2), 0);
	ptr = new_ptr;
	ASSERT_EQ(munmap(ptr, self->size / 2), 0);

	close(wrapfd);
}

static void test_wrap_fork(struct __test_metadata *_metadata,
			   FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd, status;
	char *ptr;
	pid_t pid;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer before modification */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Modify buffer content */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	pid = fork();
	ASSERT_FALSE(pid < 0);
	if (pid == 0) {
		/* Check the content from the child */
		ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ), 0);
		ASSERT_EQ(memcmp(self->content, ptr, self->size), 0);
		ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ), 0);
		exit(EXIT_SUCCESS);
	} else {
		ASSERT_NE(wait(&status), -1);
		ASSERT_TRUE(WIFEXITED(status));
		ASSERT_EQ(WEXITSTATUS(status), 0);
	}

	ASSERT_EQ(munmap(ptr, self->size), 0);

	close(wrapfd);
}

static void test_dup(struct __test_metadata *_metadata,
		     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd, wrapfd2;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	wrapfd2 = dup(wrapfd);
	ASSERT_TRUE(wrapfd2 >= 0);

	/* Check content of the buffer using both fds */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd2, 0, 0, self->size), 0);

	close(wrapfd2);
	close(wrapfd);
}

static void test_owner(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try taking ownership of a mapped wrapfd */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED | MAP_POPULATE,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_TRUE(wrapfd_acquire_ownership(wrapfd) && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Take ownership of an unmapped wrapfd */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Map owned wrapfd */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED | MAP_POPULATE,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Try releasing ownership while still mapped */
	ASSERT_TRUE(wrapfd_release_ownership(wrapfd) && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Release ownership of an unmapped wrapfd */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_rewrap(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd, wrapfd2, wrapfd3;
	unsigned int state;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	ASSERT_TRUE(wrapfd_get_state(wrapfd, &state) == 0 &&
		    state == WRAPFD_CONTENT_RDONLY);

	/* Try rewrapping unowned buffer */
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 < 0 && errno == EBUSY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Try rewrapping a mapped buffer */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED | MAP_POPULATE,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 < 0 && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Rewrap the buffer to writable */
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 >= 0);

	/* Check the states of the new and original buffers */
	ASSERT_TRUE(wrapfd_get_state(wrapfd2, &state) == 0 &&
		    state == WRAPFD_CONTENT_RDWR);
	ASSERT_TRUE(wrapfd_get_state(wrapfd, &state) == 0 &&
		    state == WRAPFD_CONTENT_EMPTY);

	/* Check rewrapped content */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd2, 0, 0, self->size), 0);

	/* Take ownership of the rewrapped buffer */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd2), 0);

	/* Convert back to read-only */
	wrapfd3 = wrapfd_rewrap(wrapfd2, PROT_READ);
	ASSERT_TRUE(wrapfd3 >= 0);
	ASSERT_TRUE(wrapfd_get_state(wrapfd3, &state) == 0 &&
		    state == WRAPFD_CONTENT_RDONLY);
	ASSERT_TRUE(wrapfd_get_state(wrapfd2, &state) == 0 &&
		    state == WRAPFD_CONTENT_EMPTY);

	/* Try mapping as writable */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd3, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == EACCES);

	/* Check rewrapped content */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd3, 0, 0, self->size), 0);

	/* Try mapping the original empty wrap file */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == ENOENT);

	/* Release ownership of the buffers */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd2), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd3);
	close(wrapfd2);
	close(wrapfd);
}

static void test_rewrap_rdonly_file(struct __test_metadata *_metadata,
				    FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	unsigned int state;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	ASSERT_EQ(wrapfd_get_state(wrapfd, &state), 0);
	ASSERT_EQ(state, WRAPFD_CONTENT_RDONLY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Rewrapping the buffer as writable should fail. */
	ASSERT_TRUE(wrapfd_rewrap(wrapfd, PROT_WRITE) < 0 && errno == EACCES);

	/* Mapping the buffer as writable should also fail. */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == EACCES);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd);
}

static void test_empty(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	unsigned int state;
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try emptying unowned buffer */
	ASSERT_TRUE(wrapfd_empty(wrapfd) < 0 && errno == EBUSY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Try emptying a mapped buffer */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_TRUE(wrapfd_empty(wrapfd) < 0 && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Empty the buffer */
	ASSERT_EQ(wrapfd_empty(wrapfd), 0);
	ASSERT_TRUE(wrapfd_get_state(wrapfd, &state) == 0 &&
		    state == WRAPFD_CONTENT_EMPTY);

	/* Try mapping the empty wrap file */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == ENOENT);

	/* Try loading into an empty wrap file. */
	ASSERT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 && errno == ENOENT);

	/* Release buffer ownership */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd);
}

static int is_close_on_exec(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		return -1;

	return (flags & FD_CLOEXEC) ? 1 : 0;
}

static int set_close_on_exec(int fd, bool set) {
	int flags;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		return -1;

	if (set)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;

	return fcntl(fd, F_SETFD, flags);
}

static void __test_close_on_exec(struct __test_metadata *_metadata,
				 FIXTURE_DATA(wrapfd_tests) *self,
				 int fd, int close_on_exec)
{
	int wrapfd, wrapfd2;

	/* Wrap's attribute should match its content */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(close_on_exec, is_close_on_exec(wrapfd));

	/* Rewrapping should preserve the attribute */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 >= 0);
	ASSERT_EQ(close_on_exec, is_close_on_exec(wrapfd2));

	close(wrapfd2);
	close(wrapfd);
}

static void test_close_on_exec(struct __test_metadata *_metadata,
			       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int close_on_exec;

	close_on_exec = is_close_on_exec(fd);
	ASSERT_NE(close_on_exec, -1);

	/* Test FD_CLOEXEC inheritance */
	__test_close_on_exec(_metadata, self, fd, close_on_exec);

	/* Test FD_CLOEXEC inheritance after toggling the attribute */
	set_close_on_exec(fd, !close_on_exec);
	__test_close_on_exec(_metadata, self, fd, !close_on_exec);

	/* Reset attribute to the original value */
	set_close_on_exec(fd, close_on_exec);
}

static void test_guests(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try allowing guests for unowned buffer */
	ASSERT_TRUE(wrapfd_allow_guests(wrapfd) < 0 && errno == EBUSY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Try allowing guests for a mapped buffer */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_TRUE(wrapfd_allow_guests(wrapfd) < 0 && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Allow guests */
	ASSERT_EQ(wrapfd_allow_guests(wrapfd), 0);
	ASSERT_EQ(wrapfd_prohibit_guests(wrapfd), 0);
	ASSERT_EQ(wrapfd_allow_guests(wrapfd), 0);

	/* Release buffer ownership */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd);
}

#define FDINFO_BUF_SIZE	4096

static void test_ioctl(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	const char *buf_name = "test_dmabuf";
	char str[FDINFO_BUF_SIZE];
	char fdinfo_name[64];
	int fdinfo_fd;
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(ioctl(wrapfd, DMA_BUF_SET_NAME, buf_name), 0);

	/* Verify the dmabuf name is changed */
	sprintf(fdinfo_name, "/proc/%d/fdinfo/%d", getpid(), fd);
	fdinfo_fd = open(fdinfo_name, O_RDONLY);
	ASSERT_TRUE(fdinfo_fd >= 0);
	ASSERT_TRUE(read(fdinfo_fd, str, FDINFO_BUF_SIZE) > 0);
	ASSERT_NE(strstr(str, buf_name), NULL);
	close(fdinfo_fd);

	/* Verify the dmabuf name is changed in wrapfd fdinfo too */
	sprintf(fdinfo_name, "/proc/%d/fdinfo/%d", getpid(), wrapfd);
	fdinfo_fd = open(fdinfo_name, O_RDONLY);
	ASSERT_TRUE(fdinfo_fd >= 0);
	ASSERT_TRUE(read(fdinfo_fd, str, FDINFO_BUF_SIZE) > 0);
	ASSERT_NE(strstr(str, buf_name), NULL);
	close(fdinfo_fd);

	close(wrapfd);
}

static void test_lseek(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Verify the dmabuf size is what we expect; dmabuf sizes are page aligned. */
	ASSERT_EQ(lseek(wrapfd, 0, SEEK_END), ALIGN(self->size, self->page_size));
	ASSERT_EQ(lseek(wrapfd, 0, SEEK_SET), 0);

	close(wrapfd);
}


static void run_tests(struct __test_metadata *_metadata,
		      FIXTURE_DATA(wrapfd_tests) *self, int fd, int rdonly_fd)
{
	test_wrap(_metadata, self, fd);
	test_wrap_rdonly_file(_metadata, self, rdonly_fd);
	test_load_mapped(_metadata, self, fd);
	test_load_unmapped(_metadata, self, fd);
	test_load_rdonly_file(_metadata, self, rdonly_fd);
	test_wrap_rdonly(_metadata, self, fd);
	test_wrap_rdwr(_metadata, self, fd);
	test_remap_file_pages(_metadata, self, fd);
	test_wrap_remap(_metadata, self, fd);
	test_wrap_fork(_metadata, self, fd);
	test_dup(_metadata, self, fd);
	test_owner(_metadata, self, fd);
	test_rewrap(_metadata, self, fd);
	test_rewrap_rdonly_file(_metadata, self, rdonly_fd);
	test_empty(_metadata, self, fd);
	test_close_on_exec(_metadata, self, fd);
	test_guests(_metadata, self, fd);
	test_ioctl(_metadata, self, fd);
	test_lseek(_metadata, self, fd);
}

TEST_F(wrapfd_tests, wrapfd_test_dmabuf_system_heap)
{
	int dmabuf_fd;
	int rdonly_dmabuf_fd;
	int heap_fd;

	heap_fd = open("/dev/dma_heap/system", O_RDONLY);
	ASSERT_TRUE(heap_fd >= 0);
	dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDWR);
	ASSERT_TRUE(dmabuf_fd >= 0);
	rdonly_dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDONLY);
	ASSERT_TRUE(rdonly_dmabuf_fd >= 0);
	close(heap_fd);
	run_tests(_metadata, self, dmabuf_fd, rdonly_dmabuf_fd);
	close(rdonly_dmabuf_fd);
	close(dmabuf_fd);
}

TEST_F(wrapfd_tests, wrapfd_test_dmabuf_cma_heap)
{
	int dmabuf_fd;
	int rdonly_dmabuf_fd;
	int heap_fd;

	heap_fd = open("/dev/dma_heap/default_cma_region", O_RDONLY);
	if (heap_fd < 0 && errno == ENOENT)
		SKIP(return, "Skipping CMA tests as the CMA heap is missing");
	ASSERT_TRUE(heap_fd >= 0);
	dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDWR);
	ASSERT_TRUE(dmabuf_fd >= 0);
	rdonly_dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDONLY);
	ASSERT_TRUE(rdonly_dmabuf_fd >= 0);
	close(heap_fd);
	run_tests(_metadata, self, dmabuf_fd, rdonly_dmabuf_fd);
	close(rdonly_dmabuf_fd);
	close(dmabuf_fd);
}

TEST_HARNESS_MAIN
