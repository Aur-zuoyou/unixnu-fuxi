/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "FUXIXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

extern const void *g_payload_ptr;

const char *loader_boot(uintptr_t dtb)
{
	const struct payload *p;
	uintptr_t src;		/* payload 起始物理地址 */
	uintptr_t src_end;	/* 重叠检查用 */

	if (g_payload_ptr != 0) {
		/* fuxi: payload 内嵌在镜像末尾（ABL 的 ramdisk 不可用） */
		src = (uintptr_t)g_payload_ptr;
		src_end = src + PAGE_16K;	/* 保守估计，够做重叠检查 */
		con_hex("EMBED", src);
	} else {
		uint64_t rd_start, rd_end;

		if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
		    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
			return "no initrd in /chosen";
		con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);
		src = (uintptr_t)rd_start;
		src_end = (uintptr_t)rd_end;
	}

	p = (const struct payload *)src;
	if (!magic_ok(p->magic))
		return "bad payload magic";
	if (p->version != 1 || (p->adt_off & 7) || (src & 7))
		return "bad payload header";

	/*
	 * FUXI_LOW_PHYS：4GB 以上的地址在这台机器上访问会触发同步外部中止
	 * （ESR 0x96000010 / FAR 0x101004000），所以内核放到低区。
	 */
	const uint64_t FUXI_LOW_PHYS = 0x90000000ULL;
	const uint64_t FUXI_LOW_SIZE = 0x04000000ULL;   /* 只覆盖内核(约 35MB)，避开后面的基带/媒体保留区 */
	uint64_t phys_base = FUXI_LOW_PHYS;
	uint64_t mem_size  = FUXI_LOW_SIZE;

	uint64_t pa_of_va_base = phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", phys_base, "size=", mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* 目标内核区域不能和 payload 自己重叠 */

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, src + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, src + p->adt_off, p->adt_size);

	/* ADT 里的 dram-base/dram-size 也要跟着改（XNU 用它建 physmap） */
	{
		volatile uint64_t *w = (volatile uint64_t *)adt_pa;
		uint64_t n = p->adt_size / 8;
		for (uint64_t i = 0; i < n; i++) {
			if (w[i] == p->phys_base)
				w[i] = phys_base;
			else if (w[i] == p->mem_size)
				w[i] = mem_size;
		}
	}
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	/* payload 必须落在内核写入区间之外（它会边拷边被读） */
	if (src_end > phys_base && src < top)
		return "payload overlaps kernel memory";

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = phys_base;
	ba->memSize = mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);

	/* 跳转前确认：入口处真的是代码吗？MMU 真的是关的吗？ */
	{
		volatile const uint64_t *e = (volatile const uint64_t *)entry_pa;
		uint64_t sctlr;

		con_hex("CODE@ENTRY", e[0]);
		con_hex("CODE@+8", e[1]);

		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
		con_hex("SCTLR", sctlr);
	}
	con_str("XNU", "jumping (fbcon clears screen)");

	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
