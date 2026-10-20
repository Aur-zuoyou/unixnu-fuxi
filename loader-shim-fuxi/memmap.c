/*
 * 从 ABL 给的 memory map 里挑内核窗口 —— 照 UniXNU DarwinBoot 的 AllocateWindow：
 *   在内存描述符里找「最大的连续可用段」，必须容得下窗口大小，
 *   起始地址对齐到 BLOCK_SIZE，并保持 (virt_base & (BLOCK_SIZE-1)) 的低位不变，
 *   然后从该段里切出窗口。
 *
 * fuxi 的 ABL 用 FDT + efi-memory-map / memory 节点描述可用 RAM；这里两种都认。
 */
#include <stdint.h>
#include "console.h"

#define BLOCK_SIZE	(64UL << 20)	/* 与 XNU 的 L2 块映射粒度一致 */
#define FDT_MAGIC	0xd00dfeed

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

/*
 * 遍历 FDT 里的 /memory 节点，累加最大的连续可用区间。
 * 返回 1 表示找到窗口，填 *base / *size（size 至少需要的 need，且尽量大）。
 */
int shim_pick_window(uintptr_t fdt, uint64_t need, uint64_t align_low,
                     uint64_t avoid_lo, uint64_t avoid_hi,
                     uint64_t *base, uint64_t *size)
{
	uintptr_t p, strings;
	int depth = 0;
	int in_memory = 0;
	uint64_t best_lo = 0, best_hi = 0;
	uint64_t best_bytes = 0;

	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;

	p = fdt + be32(fdt + 8);		/* off_dt_struct */
	strings = fdt + be32(fdt + 12);		/* off_dt_strings */

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case 1: {			/* FDT_BEGIN_NODE */
			const char *name = (const char *)p;
			depth++;
			in_memory = depth == 2 &&
			    (streq(name, "memory") || streq(name, "memory@0"));
			p = align4(skip_str(p));
			break;
		}
		case 2:				/* FDT_END_NODE */
			if (in_memory)
				in_memory = 0;
			depth--;
			break;
		case 3: {			/* FDT_PROP */
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;

			if (in_memory && streq((const char *)(strings + nameoff), "reg")) {
				/* reg = <addr size> 或 <addr_hi addr_lo size_hi size_lo> */
				for (uint32_t off = 0; off + 16 <= len; off += 16) {
					uint64_t addr = (uint64_t)be32(val + off) << 32 |
					    be32(val + off + 4);
					uint64_t sz = (uint64_t)be32(val + off + 8) << 32 |
					    be32(val + off + 12);
					uint64_t hi = addr + sz;

					if (sz > best_bytes) {
						best_bytes = sz;
						best_lo = addr;
						best_hi = hi;
					}
				}
			}
			p = align4(val + len);
			break;
		}
		case 4:				/* FDT_NOP */
			break;
		default:			/* FDT_END */
			goto done;
		}
	}

done:
	if (best_bytes < need)
		return 0;

	/*
	 * 在最大连续段里步进扫描，找到第一个不和自己重叠的位置。
	 * payload 内嵌在 shim 镜像末尾，内核窗口必须完全避开它，
	 * 否则拷贝过程会把仍在读取的源数据冲掉。
	 */
	{
		uint64_t lo = (best_lo + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
		uint64_t lowbits = align_low & (BLOCK_SIZE - 1);

		lo |= lowbits;
		if (lo < best_lo)
			lo += BLOCK_SIZE;

		for (uint64_t cand = lo; cand + need <= best_hi; cand += BLOCK_SIZE) {
			int clash = avoid_hi > avoid_lo &&
			    cand < avoid_hi && cand + need > avoid_lo;

			if (clash)
				continue;
			*base = cand;
			*size = need;	/* 只占实际需要的量，别把整段都圈进来 */
			return 1;
		}
		return 0;
	}
}
