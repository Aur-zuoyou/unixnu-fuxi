/*
 * os/firehose_buffer_private.h —— fuxi 用最小替身
 *
 * 背景:
 *   XNU 树里 libkern/firehose/ 下有 private.h、chunk_private.h、
 *   tracepoint_private.h、ioctl_private.h、firehose_types_private.h，
 *   只缺 libkern/os/firehose_buffer_private.h（Apple 内部头）。
 *
 * 关键事实（由编译错误反推）:
 *   - firehose_buffer_t / firehose_buffer_map_info_t / firehose_push_reply_t
 *     已在树内头文件定义，这里**不能再定义**（否则 redefinition）
 *   - __firehose_buffer_push_to_logd 在 libkern/os/log.c 里已有实现，只需声明
 *   - __firehose_buffer_tracepoint_reserve 是 5 个参数
 *   - __firehose_buffer_tracepoint_flush 是 2 个参数
 *   - 真正缺的只有一个常量: FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT
 *
 * 因此本文件的职责: 引入树内已有定义 + 补那一个常量。
 */

#ifndef __FIREHOSE_BUFFER_PRIVATE__
#define __FIREHOSE_BUFFER_PRIVATE__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <mach/mach_types.h>
#include <mach/std_types.h>

#include <firehose/firehose_types_private.h>
#include <firehose/tracepoint_private.h>
#include <firehose/chunk_private.h>
#include <firehose/private.h>

__BEGIN_DECLS

/* 树内没有、但内核代码用到的常量 */
#ifndef FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT
#define FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT   16
#endif

#ifndef FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT   16
#endif

#ifndef FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES
#define FIREHOSE_BUFFER_KERNEL_DEFAULT_IO_PAGES      16
#endif

__END_DECLS

#endif /* __FIREHOSE_BUFFER_PRIVATE__ */
