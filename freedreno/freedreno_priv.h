/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_PRIV_H_
#define FREEDRENO_PRIV_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

#include "libdrm_macros.h"
#include "xf86drm.h"
#include "xf86atomic.h"

#include "util_double_list.h"

#include "freedreno_drmif.h"
#include "freedreno_ringbuffer.h"
#include "drm.h"

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

struct fd_device_funcs {
	int (*bo_new_handle)(struct fd_device *dev, uint32_t size,
			uint32_t flags, uint32_t *handle);
	struct fd_bo * (*bo_from_handle)(struct fd_device *dev,
			uint32_t size, uint32_t handle);
	struct fd_pipe * (*pipe_new)(struct fd_device *dev, enum fd_pipe_id id);
	void (*destroy)(struct fd_device *dev);
};

struct fd_bo_bucket {
	uint32_t size;
	struct list_head list;
};

struct fd_bo_cache {
	struct fd_bo_bucket cache_bucket[14 * 4];
	int num_buckets;
	time_t time;
};

struct fd_device {
	int fd;
	enum fd_version version;
	atomic_t refcnt;

	/* tables to keep track of bo's, to avoid "evil-twin" fd_bo objects:
	 *
	 *   handle_table: maps handle to fd_bo
	 *   name_table: maps flink name to fd_bo
	 *
	 * We end up needing two tables, because DRM_IOCTL_GEM_OPEN always
	 * returns a new handle.  So we need to figure out if the bo is already
	 * open in the process first, before calling gem-open.
	 */
	void *handle_table, *name_table;

	const struct fd_device_funcs *funcs;

	struct fd_bo_cache bo_cache;

	int closefd;        /* call close(fd) upon destruction */
};

drm_private void fd_bo_cache_init(struct fd_bo_cache *cache, int coarse);
drm_private void fd_bo_cache_cleanup(struct fd_bo_cache *cache, time_t time);
drm_private struct fd_bo * fd_bo_cache_alloc(struct fd_bo_cache *cache,
		uint32_t *size, uint32_t flags);
drm_private int fd_bo_cache_free(struct fd_bo_cache *cache, struct fd_bo *bo);

/* for where @table_lock is already held: */
drm_private void fd_device_del_locked(struct fd_device *dev);

struct fd_pipe_funcs {
	struct fd_ringbuffer * (*ringbuffer_new)(struct fd_pipe *pipe, uint32_t size);
	int (*get_param)(struct fd_pipe *pipe, enum fd_param_id param, uint64_t *value);
	int (*wait)(struct fd_pipe *pipe, uint32_t timestamp, uint64_t timeout);
	void (*destroy)(struct fd_pipe *pipe);
};

struct fd_pipe {
	struct fd_device *dev;
	enum fd_pipe_id id;
	uint32_t gpu_id;
	const struct fd_pipe_funcs *funcs;
};

struct fd_ringmarker {
	struct fd_ringbuffer *ring;
	uint32_t *cur;
};

struct fd_ringbuffer_funcs {
	void * (*hostptr)(struct fd_ringbuffer *ring);
	int (*flush)(struct fd_ringbuffer *ring, uint32_t *last_start,
			int in_fence_fd, int *out_fence_fd);
	void (*grow)(struct fd_ringbuffer *ring, uint32_t size);
	void (*reset)(struct fd_ringbuffer *ring);
	void (*emit_reloc)(struct fd_ringbuffer *ring,
			const struct fd_reloc *reloc);
	uint32_t (*emit_reloc_ring)(struct fd_ringbuffer *ring,
			struct fd_ringbuffer *target, uint32_t cmd_idx,
			uint32_t submit_offset, uint32_t size);
	uint32_t (*cmd_count)(struct fd_ringbuffer *ring);
	void (*destroy)(struct fd_ringbuffer *ring);
};

struct fd_bo_funcs {
	int (*offset)(struct fd_bo *bo, uint64_t *offset);
	int (*cpu_prep)(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t op);
	void (*cpu_fini)(struct fd_bo *bo);
	int (*madvise)(struct fd_bo *bo, int willneed);
	void (*destroy)(struct fd_bo *bo);
};

struct fd_bo {
	struct fd_device *dev;
	uint32_t size;
	uint32_t handle;
	uint32_t name;
	void *map;
	atomic_t refcnt;
	const struct fd_bo_funcs *funcs;

	int bo_reuse;
	struct list_head list;   /* bucket-list entry */
	time_t free_time;        /* time when added to bucket-list */
};

#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define enable_debug 0  /* TODO make dynamic */

#define INFO_MSG(fmt, ...) \
		do { drmMsg("[I] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define DEBUG_MSG(fmt, ...) \
		do if (enable_debug) { drmMsg("[D] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define WARN_MSG(fmt, ...) \
		do { drmMsg("[W] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { drmMsg("[E] " fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)

#define U642VOID(x) ((void *)(unsigned long)(x))
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

static inline uint32_t
offset_bytes(void *end, void *start)
{
	return ((char *)end) - ((char *)start);
}

#endif /* FREEDRENO_PRIV_H_ */
