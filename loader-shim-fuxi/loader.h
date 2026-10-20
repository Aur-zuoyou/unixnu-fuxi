#pragma once
#include <stdint.h>

/* Boot the FUXIXNU payload found in the ABL ramdisk; returns only on error. */
const char *loader_boot(uintptr_t dtb);

/*
 * 按 UniXNU DarwinBoot 的做法，从 ABL 的 memory map 里给内核挑一个连续窗口。
 * need = 内核跨度 + ADT + boot_args；align_low = virt_base 的低位，必须保留。
 * 返回 1 表示成功。
 */
int shim_pick_window(uintptr_t fdt, uint64_t need, uint64_t align_low,
                     uint64_t *base, uint64_t *size);
