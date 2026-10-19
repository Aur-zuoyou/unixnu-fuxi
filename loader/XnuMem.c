/** @file
  XnuMem.c —— 为 XNU 挑一块 UEFI 认可的内存，并改写 ADT 的 dram-base/dram-size

  问题：ADT 里硬编码 dram-base = 0x100000000，但 UEFI 的页表没映射那块内存，
        拷贝内核段时触发 "Synchronous external abort"（实测）。

  做法：
    1. 退出 Boot Services 之前扫描 EFI 内存图，找一块足够大的 EfiConventionalMemory
    2. 把 ADT blob 里的 u64 0x100000000 / 0xfcc00000 原地替换成选中的值
       （ADT 是我们自己生成的，值已知，按字节模式替换是可靠的）
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>

#define ADT_OLD_BASE  0x100000000ULL
#define ADT_OLD_SIZE  0xfcc00000ULL
#define NEED_BYTES    (48ULL * 1024 * 1024)   /* 内核 16MB + ADT + 余量 */

UINT64  mXnuPhysBase;
UINT64  mXnuPhysSize;

typedef VOID (*XNU_LOG_FN2)(CONST CHAR8 *Tag, UINT64 Value);

/**
  挑一块可用内存。

  @retval 0  找到
  @retval 1  没找到
**/
UINT32
XnuPickPhysRegion (
  IN  XNU_LOG_FN2  Log
  )
{
  EFI_STATUS             Status;
  UINTN                  Size = 0;
  UINTN                  MapKey;
  UINTN                  DescSize = 0;
  UINT32                 DescVer = 0;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  Count;
  UINTN                  I;
  UINT64                 BestSize = 0;
  UINT64                 BestBase = 0;

  mXnuPhysBase = 0;
  mXnuPhysSize = 0;

  Status = gBS->GetMemoryMap (&Size, NULL, &MapKey, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return 2;
  }
  Size += 4 * DescSize;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    return 3;
  }
  if (EFI_ERROR (gBS->GetMemoryMap (&Size, Map, &MapKey, &DescSize, &DescVer))) {
    FreePool (Map);
    return 4;
  }

  Count = Size / DescSize;
  for (I = 0; I < Count; I++) {
    EFI_MEMORY_DESCRIPTOR  *D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + I * DescSize);
    UINT64                 Bytes;

    if (D->Type != EfiConventionalMemory) {
      continue;
    }
    Bytes = (UINT64)D->NumberOfPages * 4096ULL;
    if (Bytes < NEED_BYTES) {
      continue;
    }
    /* 优先选最高的那块（通常在高位内存，避开固件自己用的低地址） */
    if ((D->PhysicalStart > BestBase) || (BestBase == 0)) {
      BestBase = D->PhysicalStart;
      BestSize = Bytes;
    }
  }

  FreePool (Map);

  if (BestBase == 0) {
    if (Log != NULL) {
      Log ("NO RAM", 0);
    }
    return 1;
  }

  /* 对齐到 2MB，大小取整到 2MB */
  mXnuPhysBase = (BestBase + 0x1FFFFFULL) & ~0x1FFFFFULL;
  mXnuPhysSize = (BestSize - (mXnuPhysBase - BestBase)) & ~0x1FFFFFULL;

  if (Log != NULL) {
    Log ("PHYS PICKED", mXnuPhysBase);
    Log ("PHYS SIZE", mXnuPhysSize);
  }
  return 0;
}

/** 在 blob 里把所有 8 字节 LE 值 Old 换成 New */
STATIC
UINT32
PatchU64 (
  IN OUT UINT8   *Blob,
  IN     UINTN    Len,
  IN     UINT64   Old,
  IN     UINT64   New
  )
{
  UINTN   I;
  UINT32  Hits = 0;

  for (I = 0; I + 8 <= Len; I += 8) {
    UINT64  V;

    CopyMem (&V, Blob + I, 8);
    if (V == Old) {
      CopyMem (Blob + I, &New, 8);
      Hits++;
    }
  }
  return Hits;
}

/**
  把 ADT 里的 dram-base / dram-size 改成选中的内存。
  返回替换的处数（0 表示没找到，可能 ADT 不是我们生成的格式）。
**/
UINT32
XnuPatchAdt (
  IN OUT UINT8   *Adt,
  IN     UINTN    Len,
  IN     UINT64   Base,
  IN     UINT64   Size
  )
{
  UINT32  A;
  UINT32  B;

  A = PatchU64 (Adt, Len, ADT_OLD_BASE, Base);
  B = PatchU64 (Adt, Len, ADT_OLD_SIZE, Size);
  return A + B;
}
