/*
 * os/firehose_buffer_private.h —— fuxi 用替身
 *
 * 背景:
 *   XNU 树里 libkern/firehose/ 下有 chunk_private.h、tracepoint_private.h、
 *   ioctl_private.h、private.h，但缺 libkern/os/firehose_buffer_private.h
 *   （Apple 内部头，开源树和公开 SDK 都没有）。
 *   libkern/os/log.c 与 bsd/kern/subr_log.c 都需要它。
 *
 * 本文件按两个 .c 的实际用法提供最小可编译定义：
 *   - firehose_buffer_t （真实类型是 union firehose_buffer_u *）
 *   - firehose_buffer_map_info_t / firehose_push_reply_t
 *   - FIREHOSE_BUFFER_KERNEL_DEFAULT_* 常量
 *   - 若干函数原型 + 空实现（firehose 追踪功能不可用，不影响内核主流程）
 */

#ifndef __FIREHOSE_BUFFER_PRIVATE__
#define __FIREHOSE_BUFFER_PRIVATE__

#include <mach/mach_types.h>
#include <mach/std_types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <firehose/firehose_types_private.h>
#include <firehose/tracepoint_private.h>

__BEGIN_DECLS

/* ---------------- 常量 ---------------- */

#ifndef FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT   16
#endif

#ifndef FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES      16
#endif

/* ---------------- 类型 ---------------- */

/* 真实定义是 union；内核代码只把它当不透明句柄传递 */
union firehose_buffer_u {
	struct {
		uint32_t  fb_flags;
		uint32_t  fb_padding;
	} fb_hdr;
};

typedef union firehose_buffer_u *firehose_buffer_t;

typedef struct firehose_buffer_map_info_s {
	mach_vm_address_t  addr;
	mach_vm_size_t     size;
} firehose_buffer_map_info_t;

typedef struct firehose_push_reply_s {
	uint32_t  fpr_status;
	uint32_t  fpr_padding;
} firehose_push_reply_t;

/* ---------------- 函数 ---------------- */

/* 全部做成 static inline 空实现：既满足编译，也避免链接期缺符号。
   firehose 追踪功能不可用，但不影响内核主流程。 */

static inline firehose_buffer_t
__firehose_buffer_create(size_t *size __unused)
{
	return (firehose_buffer_t)0;
}

static inline bool
__firehose_merge_updates(firehose_push_reply_t reply __unused)
{
	return false;
}

static inline bool
__firehose_kernel_configuration_valid(uint8_t chunk_count __unused,
    uint8_t io_pages __unused)
{
	return true;
}

static inline void
__firehose_buffer_push_to_logd(firehose_buffer_t fb __unused, bool for_io __unused)
{
}

static inline struct firehose_tracepoint_s *
__firehose_buffer_tracepoint_reserve(firehose_buffer_t fb __unused,
    bool for_io __unused, uint64_t stamp __unused,
    firehose_stream_t stream __unused, uint16_t flags __unused,
    uint8_t size __unused)
{
	return (struct firehose_tracepoint_s *)0;
}

static inline void
__firehose_buffer_tracepoint_flush(firehose_buffer_t fb __unused,
    struct firehose_tracepoint_s *tp __unused, uint64_t stamp __unused)
{
}

__END_DECLS

#endif /* __FIREHOSE_BUFFER_PRIVATE__ */
