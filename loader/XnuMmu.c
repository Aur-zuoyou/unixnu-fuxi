/** @file
  XnuMmu.c —— 自己建页表并接管 MMU（照搬 ios-sweet 的 shim/mmu.c）

  为什么需要：UEFI 的页表只映射了它关心的地址，4GB 以上访问会 external abort。
  我们接管 TTBR0_EL1 之后，就能像 ios-sweet 那样把 0x1_00000000 当 dram 用。

  页表结构（16K 粒度，和 XNU 一致）：
    T0SZ = 28  → 36 位 VA，从 level 2 开始走
    L2 = 2048 个 32MB 块，覆盖 64GB，恒等映射（VA == PA）
    前 8GB 全部映射为 Device-nGnRnE（高通对保护区投机读会 fault，所以不开 cache）
    帧缓冲那块映射成 Normal non-cacheable（否则屏幕写不出去）
    我们自己的镜像 + 目标内存用 L3 表映射成 Normal write-back（拷贝快）
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#define PG_SHIFT     14
#define PG_SIZE      (1UL << PG_SHIFT)          /* 16K */
#define BLK_SHIFT    25
#define BLK_SIZE     (1UL << BLK_SHIFT)         /* 32MB */
#define NENT         2048

#define ATTR_DEV     0
#define ATTR_WB      1
#define ATTR_NC      2
#define MAIR_VAL     0x44ff00UL

#define D_BLOCK      1UL
#define D_TABLE      3UL
#define D_PAGE       3UL
#define D_ATTR(i)    ((UINT64)(i) << 2)
#define D_SH_OUTER   (2UL << 8)
#define D_SH_INNER   (3UL << 8)
#define D_AF         (1UL << 10)
#define D_PXN        (1UL << 53)
#define D_UXN        (1UL << 54)

#define DEV_FLAGS    (D_ATTR(ATTR_DEV) | D_AF | D_PXN | D_UXN)
#define WB_FLAGS     (D_ATTR(ATTR_WB) | D_SH_INNER | D_AF | D_UXN)
#define NC_FLAGS     (D_ATTR(ATTR_NC) | D_SH_OUTER | D_AF | D_PXN | D_UXN)

/* T0SZ=28, TG0=16K, IRGN0/ORGN0=WBWA, SH0=inner, IPS=36bit */
#define TCR_VAL      (28UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | \
                      (28UL << 16) | (1UL << 23) | (1UL << 30))

#define SCTLR_M      (1UL << 0)
#define SCTLR_C      (1UL << 2)
#define SCTLR_I      (1UL << 12)

#define RSR(r)  ({ UINT64 _v; __asm__ volatile ("mrs %0, " r : "=r"(_v)); _v; })
#define WSR(r, v) __asm__ volatile ("msr " r ", %0" : : "r"((UINT64)(v)))

/* 1 个 L2 表 + 最多 8 个 L3 表，全部 16K 对齐 */
STATIC UINT64  mPtPool[9 * NENT] __attribute__((aligned(16384)));

STATIC
VOID
DcInvalidateRange (
  IN UINTN  Start,
  IN UINTN  End
  )
{
  UINT64  Line;
  UINTN   A;

  Line = 4UL << ((RSR ("ctr_el0") >> 16) & 0xf);
  for (A = Start & ~(UINTN)(Line - 1); A < End; A += (UINTN)Line) {
    __asm__ volatile ("dc ivac, %0" : : "r"(A) : "memory");
  }
  __asm__ volatile ("dsb sy" ::: "memory");
}

/**
  建一张恒等映射的页表并启用。

  @param[in] WbLo   需要 Normal write-back 的区间低端
  @param[in] WbHi   高端（用于我们的镜像和目标内存）

  @retval 0  成功
**/
UINT32
XnuMmuInstall (
  IN UINTN  WbLo,
  IN UINTN  WbHi
  )
{
  UINT64  *L2;
  UINT64  *Pool;
  UINT64  Blk;
  UINT64  Lo;
  UINT64  Hi;
  UINT64  Sctlr;
  UINT32  NL3;
  UINTN   I;

  Pool = (UINT64 *)(((UINTN)mPtPool + PG_SIZE - 1) & ~(UINTN)(PG_SIZE - 1));
  L2   = Pool;

  /* L2: 前 8GB 全部 Device 块，其余不映射 */
  for (I = 0; I < NENT; I++) {
    L2[I] = 0;
  }
  for (I = 0; I < ((UINTN)(8UL << 30) >> BLK_SHIFT); I++) {
    L2[I] = ((UINT64)I << BLK_SHIFT) | DEV_FLAGS | D_BLOCK;
  }

  /* 帧缓冲那块改成 non-cacheable（GOP 给的是 0xB0000000，但按区间算） */
  {
    UINTN  Fc = 0xB0000000UL;

    if ((Fc >> BLK_SHIFT) < NENT) {
      L2[Fc >> BLK_SHIFT] = (Fc & ~(UINTN)(BLK_SIZE - 1)) | NC_FLAGS | D_BLOCK;
    }
  }

  /* 我们的镜像和目标内存：用 L3 表映射成 WB（否则 16MB 拷贝会非常慢） */
  NL3 = 0;
  Lo  = WbLo & ~(UINTN)(BLK_SIZE - 1);
  Hi  = (WbHi + BLK_SIZE - 1) & ~(UINTN)(BLK_SIZE - 1);
  for (Blk = Lo; Blk < Hi && NL3 < 8; Blk += BLK_SIZE) {
    UINT64  *L3 = (UINT64 *)((UINTN)Pool + (1 + NL3) * PG_SIZE);
    UINTN   BlkIdx = (UINTN)(Blk >> BLK_SHIFT);

    for (I = 0; I < NENT; I++) {
      UINTN  Pa = (UINTN)Blk + (I << PG_SHIFT);

      if (Pa >= WbLo && Pa < WbHi) {
        L3[I] = (UINT64)Pa | WB_FLAGS | D_PAGE;
      } else {
        L3[I] = (UINT64)Pa | DEV_FLAGS | D_PAGE;
      }
    }
    if (BlkIdx < NENT) {
      L2[BlkIdx] = (UINT64)(UINTN)L3 | D_TABLE;
    }
    NL3++;
  }

  DcInvalidateRange ((UINTN)Pool, (UINTN)Pool + (1 + NL3) * PG_SIZE);
  __asm__ volatile ("ic iallu; dsb sy; isb" ::: "memory");

  WSR ("mair_el1", MAIR_VAL);
  WSR ("tcr_el1", TCR_VAL);
  WSR ("ttbr0_el1", (UINT64)(UINTN)Pool);
  __asm__ volatile ("isb; tlbi vmalle1; dsb nsh; isb" ::: "memory");

  Sctlr  = RSR ("sctlr_el1");
  Sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;
  WSR ("sctlr_el1", Sctlr);
  __asm__ volatile ("isb" ::: "memory");

  return 0;
}
