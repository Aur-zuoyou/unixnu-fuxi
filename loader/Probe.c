/** @file  Probe.c

  fuxi 探针 —— 编进固件、开机自动执行, 不需要任何输入。

  关键点(踩过两次坑):

  1. 不能在 DriverEntry 里直接干活。Depex 为 TRUE 的驱动加载得非常早,
     那时 ConOut 可能还没初始化, 打印等于扔进黑洞。
     正确做法: 挂到 EndOfDxe 事件 —— 控制台已就绪、BDS 还没开始画菜单。

  2. 打印完不能返回。返回后 BDS 会清屏画启动菜单, 我们的输出被覆盖。
     所以最后死循环(wfi), 让屏幕停在我们的输出上。
     代价: 只能长按电源键重启。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>

#define LOG_BUF 512

/* EFI_END_OF_DXE_EVENT_GROUP_GUID —— 硬编码, 免得为它依赖 MdeModulePkg */
STATIC EFI_GUID  mEndOfDxeGuid = {
  0x02ce967a, 0xdd7e, 0x4ffc, { 0x9e, 0xe7, 0x81, 0x0c, 0xf0, 0xc8, 0x4c, 0x1b }
};

/* EFI_FDT_GUID —— 设备树配置表 */
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
        Magic = ((UINT32) Fdt[0] << 24) | ((UINT32) Fdt[1] << 16) |
                ((UINT32) Fdt[2] << 8) | (UINT32) Fdt[3];
        Total = ((UINT32) Fdt[4] << 24) | ((UINT32) Fdt[5] << 16) |
                ((UINT32) Fdt[6] << 8) | (UINT32) Fdt[7];
      }
      Log (L"[fuxi-probe] FDT @ 0x%Lx magic 0x%08x size %Lu\n",
           (UINT64) (UINTN) Fdt, (UINT64) Magic, (UINT64) Total);
      return;
    }
  }
  Log (L"[fuxi-probe] FDT config table NOT found (%Lu entries)\n",
       (UINT64) gST->NumberOfTableEntries);
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
    Log (L"[fuxi-probe] alloc memmap failed\n");
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
    EFI_MEMORY_DESCRIPTOR *E = (EFI_MEMORY_DESCRIPTOR *) ((UINT8 *) Map + i * DescSize);
    if (E->Type == EfiConventionalMemory) {
      ConventionalBytes += (UINTN) E->NumberOfPages * EFI_PAGE_SIZE;
    }
  }

  Log (L"[fuxi-probe] memmap %Lu desc, conventional %Lu MB\n",
       (UINT64) Count, (UINT64) (ConventionalBytes / (1024 * 1024)));
  FreePool (Map);
}

STATIC
VOID
EFIAPI
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  (VOID) Event;
  (VOID) Context;

  Log (L"\n");
  Log (L"==========================================\n");
  Log (L" fuxi-probe: alive inside real UEFI\n");
  Log (L"==========================================\n");
  Log (L"[fuxi-probe] CurrentEL = EL%Lu\n", CurrentEl ());
  ReportFdt ();
  ReportMemoryMap ();
  Log (L"[fuxi-probe] staying here on purpose.\n");
  Log (L"[fuxi-probe] long-press power to reboot.\n");

  /* 停住: 返回的话 BDS 会清屏画菜单, 输出会被覆盖 */
  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
}

EFI_STATUS
EFIAPI
ProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  (VOID) ImageHandle;
  (VOID) SystemTable;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  OnEndOfDxe,
                  NULL,
                  &mEndOfDxeGuid,
                  &Event
                  );
  return Status;
}
