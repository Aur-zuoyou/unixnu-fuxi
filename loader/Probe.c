/** @file  Probe.c

  fuxi 探针 —— 编进固件、开机自动执行的 DXE 驱动。

  为什么需要它: fuxi 这版 Renegade 固件没有输入驱动(触摸和按键都不响应),
  UEFI Shell 里敲不了字、菜单也选不了。所以把要跑的代码直接放进固件,
  由 BDS 自动加载, 不需要任何交互。

  它做的事:
    1. 打印 CurrentEL   —— 决定后面跳 XNU 要不要从 EL2 降级
    2. 找 FDT 配置表     —— ABL 传给 UEFI 的设备树在哪
    3. 打印内存图摘要    —— 有多少段、多少可用内存
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>

#define LOG_BUF 512

/* EFI_FDT_GUID —— 设备树配置表。直接写在这里, 免得为了一个 GUID 依赖 EmbeddedPkg */
STATIC EFI_GUID  mFdtTableGuid = {
  0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

STATIC
VOID
Log (
  IN CONST CHAR16  *Fmt,
  ...
  )
{
  VA_LIST  Args;
  CHAR16   Buf[LOG_BUF];

  VA_START (Args, Fmt);
  UnicodeVSPrint (Buf, sizeof (Buf), Fmt, Args);
  VA_END (Args);
  if (gST != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, Buf);
  }
}

STATIC
UINT64
CurrentEl (
  VOID
  )
{
  UINT64  Val;
  __asm__ __volatile__ ("mrs %0, CurrentEL" : "=r" (Val));
  return (Val >> 2) & 0x3;
}

STATIC
VOID
ReportMemoryMap (
  VOID
  )
{
  EFI_STATUS             Status;
  UINTN                  Size = 0;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVer;
  EFI_MEMORY_DESCRIPTOR *Map;
  EFI_MEMORY_DESCRIPTOR *Entry;
  UINTN                  Count;
  UINTN                  ConventionalBytes = 0;
  UINTN                  i;

  Status = gBS->GetMemoryMap (&Size, NULL, &Key, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL && EFI_ERROR (Status)) {
    Log (L"[fuxi-probe] GetMemoryMap(size) failed: %r\n", Status);
    return;
  }
  Size += 2 * DescSize;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    Log (L"[fuxi-probe] out of memory for memory map\n");
    return;
  }
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer);
  if (EFI_ERROR (Status)) {
    Log (L"[fuxi-probe] GetMemoryMap failed: %r\n", Status);
    FreePool (Map);
    return;
  }

  Count = Size / DescSize;
  for (i = 0; i < Count; i++) {
    Entry = (EFI_MEMORY_DESCRIPTOR *) ((UINT8 *) Map + i * DescSize);
    if (Entry->Type == EfiConventionalMemory) {
      ConventionalBytes += (UINTN) Entry->NumberOfPages * EFI_PAGE_SIZE;
    }
  }

  Log (L"[fuxi-probe] memmap: %Lu descriptors, conventional %Lu MB\n",
       (UINT64) Count, (UINT64) (ConventionalBytes / (1024 * 1024)));
  FreePool (Map);
}

STATIC
VOID
ReportFdt (
  VOID
  )
{
  UINTN  i;

  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &mFdtTableGuid)) {
      UINT8  *Fdt = (UINT8 *) gST->ConfigurationTable[i].VendorTable;
      UINT32  Total = 0;
      UINT32  Magic = 0;

      if (Fdt != NULL) {
        Magic = (UINT32) (((UINT32) Fdt[0] << 24) | ((UINT32) Fdt[1] << 16) |
                          ((UINT32) Fdt[2] << 8) | (UINT32) Fdt[3]);
        Total = (UINT32) (((UINT32) Fdt[4] << 24) | ((UINT32) Fdt[5] << 16) |
                          ((UINT32) Fdt[6] << 8) | (UINT32) Fdt[7]);
      }
      Log (L"[fuxi-probe] FDT @ 0x%Lx, magic 0x%08x, size %Lu\n",
           (UINT64) (UINTN) Fdt, (UINT64) Magic, (UINT64) Total);
      return;
    }
  }
  Log (L"[fuxi-probe] FDT config table NOT found (%Lu entries total)\n",
       (UINT64) gST->NumberOfTableEntries);
}

EFI_STATUS
EFIAPI
ProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  (VOID) ImageHandle;
  (VOID) SystemTable;

  Log (L"\n");
  Log (L"========================================\n");
  Log (L" fuxi-probe: running inside real UEFI\n");
  Log (L"========================================\n");
  Log (L"[fuxi-probe] CurrentEL = EL%Lu\n", CurrentEl ());
  ReportFdt ();
  ReportMemoryMap ();
  Log (L"[fuxi-probe] done (driver stays resident)\n");

  return EFI_SUCCESS;
}
