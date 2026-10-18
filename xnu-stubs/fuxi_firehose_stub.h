/*
 * fuxi 用的 firehose 替身头文件
 *
 * 背景:
 *   XNU 的 libkern/os/log.c 与 bsd/kern/subr_log.c 依赖 Apple 内部的
 *   firehose 追踪基础设施，其头文件不在开源树里（也不在公开 SDK 里）:
 *       <firehose/tracepoint_private.h>
 *       <firehose/chunk_private.h>
 *       <firehose/ioctl_private.h>
 *       <os/firehose_buffer_private.h>
 *
 * 做法:
 *   不去伪造整套私有接口，而是把它们"切断"：
 *   提供最小可编译的类型与空实现，让 XNU 能编过、能链接；
 *   日志相关功能退化为普通输出（后续由我们把 printf 接到帧显示）。
 *
 * 注意:
 *   这只是为了让内核能构建并启动，firehose 追踪功能不可用。
 */

#ifndef __FUXI_FIREHOSE_STUB_H__
#define __FUXI_FIREHOSE_STUB_H__

#include <mach/mach_types.h>
#include <mach/std_types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

__BEGIN_DECLS

/* ---------------- 常量 ---------------- */

#define FIREHOSE_CHUNK_SIZE                          0x40000     /* 256KB */
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT   16
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES      16
#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT           16

#define FIREHOSE_TRACEPOINT_PC_DYNAMIC_BIT           0x80000000u
#define FIREHOSE_TRACE_ID_MAKE(ns, code)             (((uint32_t)(ns) << 24) | ((uint32_t)(code) & 0xffffff))

/* ---------------- 基本类型 ---------------- */

typedef uint8_t  firehose_stream_t;
enum {
	firehose_stream_persist  = 0,
	firehose_stream_memory   = 1,
	firehose_stream_metadata = 2,
};

typedef uint32_t firehose_tracepoint_code_t;
enum {
	firehose_tracepoint_code_invalid            = 0,
	firehose_tracepoint_code_load_memory        = 1,
	firehose_tracepoint_code_load_filesystem    = 2,
	firehose_tracepoint_code_load_filesystem_ftab = 3,
};

typedef uint32_t firehose_tracepoint_namespace_t;
enum {
	firehose_tracepoint_namespace_log      = 1,
	firehose_tracepoint_namespace_metadata = 2,
};

typedef uint16_t firehose_tracepoint_flags_t;
typedef uint64_t firehose_tracepoint_id_t;

typedef union {
	uint64_t                 ftid;
	firehose_tracepoint_id_t id;
} firehose_tracepoint_id_u;

typedef struct firehose_chunk_pos_s {
	uint32_t        fcp_next_entry_offs;
	uint32_t        fcp_private_offs;
	uint32_t        fcp_refcnt;
	firehose_stream_t fcp_stream;
	uint32_t        fcp_flag_io;
} firehose_chunk_pos_t;

typedef struct firehose_chunk_s {
	firehose_chunk_pos_t  fc_pos;
	uint8_t               fc_data[FIREHOSE_CHUNK_SIZE];
} firehose_chunk_t;

/* 注意: XNU 里 firehose_chunk_t 被当成指针用（firehose_chunk_t func = &x） */

typedef struct firehose_buffer_s  *firehose_buffer_t;

typedef struct {
	mach_vm_address_t  addr;
	mach_vm_size_t     size;
} firehose_buffer_map_info_t;

typedef struct {
	uint32_t  fpr_status;
	uint32_t  fpr_padding;
} firehose_push_reply_t;

typedef struct {
	uint8_t   flags[16];
} firehose_trace_uuid_info_s;

typedef struct {
	uint64_t  flags;
} firehose_trace_metadata_t;

/* ---------------- 函数 ---------------- */

static inline uint64_t
firehose_tracepoint_time(firehose_tracepoint_flags_t flags __unused)
{
	return 0;
}

static inline firehose_chunk_t
firehose_chunk_tracepoint_try_reserve(firehose_buffer_t fb __unused,
    firehose_stream_t stream __unused, uint16_t flags __unused, uint8_t size __unused)
{
	return (firehose_chunk_t)0;
}

static inline void
firehose_chunk_tracepoint_begin(firehose_buffer_t fb __unused,
    firehose_tracepoint_id_u ftid __unused, uint64_t ts __unused)
{
}

static inline void
firehose_chunk_tracepoint_end(firehose_buffer_t fb __unused,
    firehose_chunk_t chunk __unused, firehose_tracepoint_id_u ftid __unused,
    uint64_t ts __unused, const void *buf __unused, size_t sz __unused)
{
}

static inline void
__firehose_allocate(vm_offset_t *addr __unused, vm_size_t size __unused)
{
}

static inline void
__firehose_critical_region_enter(void)
{
}

static inline void
__firehose_critical_region_leave(void)
{
}

static inline void
__firehose_buffer_push_to_logd(firehose_buffer_t fb __unused, bool for_io __unused)
{
}

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
__firehose_kernel_configuration_valid(uint8_t chunks __unused, uint8_t pages __unused)
{
	return true;
}

static inline void
oslog_stream(bool kernel __unused, firehose_tracepoint_id_u ftid __unused,
    uint64_t ts __unused, const void *buf __unused, size_t sz __unused)
{
}

__END_DECLS
#endif /* __FUXI_FIREHOSE_STUB_H__ */
