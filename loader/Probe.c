/** @file  Probe.c

  fuxi 探针 v8

  前面失败的真正原因(v7 之后才想明白):
    我们的 INF 依赖是 [Depex] TRUE, 于是驱动在 DXE 最早阶段就被加载。
    那时 gBS->SetTimer 需要的 定时器协议(TimerDxe) 可能还没装好,
    SetTimer 返回失败 -> 定时器回调永远不触发 -> v5/v6/v7 全部"什么都没发生"。
    v4 之所以有效, 是因为它在 DriverEntry 里直接死循环, 不依赖任何服务。

  v8 的做法: 把 INF 的 Depex 改成依赖 GOP。
    这样一来, 驱动会在"帧缓冲可用"的那一刻才被加载,
    入口处就能直接刷屏/打印, 不需要定时器、不需要事件。

  判据:
    屏幕变绿 + 出现 [fuxi-probe v8] -> 成功, 且拿到了 EL
    仍是普通菜单                     -> GOP 协议从未出现(说明显示不走 GOP)
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC EFI_GUID  mFdtTableGuid = {
  0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

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
Halt (
  VOID
  )
{
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
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
  CHAR16                         Buf[192];
  UINTN                          FdtAddr = 0;
  UINT32                         FdtSize = 0;
  UINTN                          i;

  (VOID) ImageHandle;
  (VOID) SystemTable;

  /* 此时 GOP 一定已经存在(Depex 保证) */
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);

  /* 找 ABL 传进来的设备树 */
  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &mFdtTableGuid)) {
      UINT8  *F = (UINT8 *) gST->ConfigurationTable[i].VendorTable;
      FdtAddr = (UINTN) F;
      if (F != NULL) {
        FdtSize = ((UINT32) F[4] << 24) | ((UINT32) F[5] << 16) |
                  ((UINT32) F[6] << 8) | (UINT32) F[7];
      }
      break;
    }
  }

  /* 先刷绿: 不依赖控制台 */
  if (!EFI_ERROR (Status) && Gop != NULL) {
    if (Gop->Mode != NULL && Gop->Mode->FrameBufferBase != 0) {
      UINT32  *Pix   = (UINT32 *) (UINTN) Gop->Mode->FrameBufferBase;
      UINTN    Count = (UINTN) Gop->Mode->FrameBufferSize / 4;
      for (i = 0; i < Count; i++) {
        Pix[i] = 0xFF00FF00;
      }
    }
  }

  /* 再尝试打印 */
  if (gST->ConOut != NULL) {
    UnicodeSPrint (
      Buf, sizeof (Buf),
      L"\r\n[fuxi-probe v8] EL%Lu  FDT=0x%Lx (%Lu)  GOP=%s %ux%u\r\n",
      CurrentEl (), (UINT64) FdtAddr, (UINT64) FdtSize,
      EFI_ERROR (Status) ? L"no" : L"yes",
      (UINT32) (Gop && Gop->Mode ? Gop->Mode->Info->HorizontalResolution : 0),
      (UINT32) (Gop && Gop->Mode ? Gop->Mode->Info->VerticalResolution : 0)
      );
    gST->ConOut->OutputString (gST->ConOut, Buf);
  }

  Halt ();
  return EFI_SUCCESS;
}
