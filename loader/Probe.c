/** @file  Probe.c

  fuxi 探针 v9

  实测结论:
    v8 (Depex=GOP): 屏幕变绿 —— 驱动按时加载、GOP 拿到、显存写成功。
                    但没有文字 —— 那时 GraphicsConsoleDxe 还没把 SimpleTextOut
                    接到 GOP 上, 所以 ConOut 里没有可用的图形输出设备。

  v9 的做法:
    1. 入口: 拿 GOP, 先把屏幕刷绿(证明链路通)。
    2. 注册 gEfiSimpleTextOutProtocolGuid 的协议通知:
       等"文字输出设备"真的出现, 再打印 EL/FDT, 然后停住。
       不依赖定时器, 也不猜时间点。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC EFI_GUID  mFdtTableGuid = {
  0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

STATIC UINTN   mFdtAddr;
STATIC UINT32  mFdtSize;

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

STATIC
VOID
PaintGreen (
  VOID
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
  UINT32                        *Pix;
  UINTN                          Count;
  UINTN                          i;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Mode == NULL) {
    return;
  }
  if (Gop->Mode->FrameBufferBase == 0) {
    return;
  }
  Pix   = (UINT32 *) (UINTN) Gop->Mode->FrameBufferBase;
  Count = (UINTN) Gop->Mode->FrameBufferSize / 4;
  for (i = 0; i < Count; i++) {
    Pix[i] = 0xFF00FF00;
  }
}

STATIC
VOID
EFIAPI
OnTextOut (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  CHAR16  Buf[192];

  (VOID) Event;
  (VOID) Context;

  if (gST == NULL || gST->ConOut == NULL) {
    return;
  }

  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"\r\n[fuxi-probe v9] EL%Lu  FDT=0x%Lx (%Lu bytes)\r\n"
    L"long-press power 15s to reboot\r\n",
    CurrentEl (), (UINT64) mFdtAddr, (UINT64) mFdtSize
    );
  gST->ConOut->OutputString (gST->ConOut, Buf);

  Halt ();
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
  UINTN       i;

  (VOID) ImageHandle;
  (VOID) SystemTable;

  /* 先把 ABL 传的设备树地址记下来 */
  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &mFdtTableGuid)) {
      UINT8  *F = (UINT8 *) gST->ConfigurationTable[i].VendorTable;
      mFdtAddr = (UINTN) F;
      if (F != NULL) {
        mFdtSize = ((UINT32) F[4] << 24) | ((UINT32) F[5] << 16) |
                   ((UINT32) F[6] << 8) | (UINT32) F[7];
      }
      break;
    }
  }

  PaintGreen ();

  /* 等"文字输出设备"出现再打印 */
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnTextOut, NULL, &Event);
  if (EFI_ERROR (Status)) {
    Halt ();
  }
  return gBS->RegisterProtocolNotify (&gEfiSimpleTextOutProtocolGuid, Event, &(VOID *){0});
}
