/** @file  Probe.c

  fuxi 探针 v3 —— 目的: 判断"驱动到底有没有被执行"。

  v1 失败: 在 DriverEntry 里打印, 那时 ConOut 还没初始化。
  v2 失败: 挂 EndOfDxe 打印, 屏幕上没看到任何东西。
          —— 可能是驱动没被执行, 也可能是执行了但控制台没输出。

  v3 的做法: 不依赖控制台。在 EndOfDxe 里直接往 GOP 显存刷一整屏纯色。
  显存是物理内存, 只要驱动跑起来、GOP 能拿到, 屏幕必然变色。

  判断方法:
    屏幕变绿           -> 驱动执行了, GOP 可用, 问题只在控制台输出
    仍是启动菜单/黑屏  -> 驱动没被执行(dispatch 问题), 要换注入点
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC EFI_GUID  mEndOfDxeGuid = {
  0x02ce967a, 0xdd7e, 0x4ffc, { 0x9e, 0xe7, 0x81, 0x0c, 0xf0, 0xc8, 0x4c, 0x1b }
};

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

/** 把整屏刷成一个颜色。不依赖控制台, 直接写显存。 */
STATIC
EFI_STATUS
PaintScreen (
  OUT UINT32  *Width,
  OUT UINT32  *Height
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
  UINT8                         *Base;
  UINTN                          Size;
  UINT32                         W;
  UINT32                         H;
  UINT32                         *Pix;
  UINTN                          Count;
  UINTN                          i;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Mode == NULL) {
    return EFI_NOT_FOUND;
  }

  Base = (UINT8 *) (UINTN) Gop->Mode->FrameBufferBase;
  Size = (UINTN) Gop->Mode->FrameBufferSize;
  W    = Gop->Mode->Info->HorizontalResolution;
  H    = Gop->Mode->Info->VerticalResolution;
  *Width  = W;
  *Height = H;
  if (Base == NULL || Size < 4) {
    return EFI_NOT_FOUND;
  }

  Pix   = (UINT32 *) Base;
  Count = Size / 4;
  /* 纯绿: 像素格式不管 RGB/BGR, 都不会是黑的, 便于肉眼确认 */
  for (i = 0; i < Count; i++) {
    Pix[i] = 0xFF00FF00;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  EFI_STATUS  Status;
  UINT32      W = 0;
  UINT32      H = 0;
  UINTN       i;
  UINTN       FdtAddr = 0;
  UINT32      FdtSize = 0;

  (VOID) Event;
  (VOID) Context;

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

  /* 先尝试控制台(可能没用), 再刷屏(一定可见) */
  if (gST->ConOut != NULL) {
    CHAR16  Buf[256];
    UnicodeSPrint (
      Buf, sizeof (Buf),
      L"[fuxi-probe] EL%Lu FDT=0x%Lx(%Lu) memmap-ok\r\n",
      CurrentEl (), (UINT64) FdtAddr, (UINT64) FdtSize
      );
    gST->ConOut->OutputString (gST->ConOut, Buf);
  }

  Status = PaintScreen (&W, &H);
  if (EFI_ERROR (Status)) {
    /* 拿不到 GOP 就退而求其次: 直接重启, 屏幕上表现为反复重启 */
    gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  }

  /* 停住: 绿屏保持不动, 方便确认 */
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
  EFI_EVENT  Event;

  (VOID) ImageHandle;
  (VOID) SystemTable;

  return gBS->CreateEventEx (
                EVT_NOTIFY_SIGNAL,
                TPL_NOTIFY,
                OnEndOfDxe,
                NULL,
                &mEndOfDxeGuid,
                &Event
                );
}
