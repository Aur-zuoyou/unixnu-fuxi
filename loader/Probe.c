/** @file  Probe.c

  fuxi 探针 v10 —— M2 的第一步: 离开 Boot Services 之后还能不能跑?

  已知:
    EL1, GOP 可用, 帧缓冲可写, 文字输出在 SimpleTextOut 出现后可用。

  v10 流程:
    1. 刷绿 + 打印 EL/FDT           (Boot Services 还活着)
    2. 忙等约 3 秒                   (让人看清绿屏和文字)
    3. ExitBootServices             (此后 gBS/gRT 全部失效)
    4. 跳到自己的 payload: 把屏幕刷成蓝色, 然后停住
    5. payload 里只做"直接写物理内存"这一件事, 不调用任何 UEFI 服务

  判据:
    绿屏+文字 -> 3 秒后变蓝  => 我们在"没有 Boot Services"的环境里跑起来了,
                                 这就是内核将来所处的环境。
    只变绿不出字 / 不变蓝     => ExitBootServices 失败或跳转出错(见代码注释)。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC EFI_GUID  mFdtTableGuid = {
  0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }
};

STATIC UINTN   mFdtAddr;
STATIC UINT32  mFdtSize;
STATIC UINT8  *mFb;
STATIC EFI_HANDLE  mImageHandle;
STATIC UINTN   mFbSize;

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
FillColor (
  IN UINT32  Color
  )
{
  UINT32  *Pix;
  UINTN    Count;
  UINTN    i;

  if (mFb == NULL || mFbSize < 4) {
    return;
  }
  Pix   = (UINT32 *) mFb;
  Count = mFbSize / 4;
  for (i = 0; i < Count; i++) {
    Pix[i] = Color;
  }
}

/** 离开 Boot Services 之后的"内核式"入口: 只用物理内存, 不碰任何 UEFI 服务 */
STATIC
VOID
__attribute__((noreturn))
Payload (
  IN UINT8  *Fb,
  IN UINTN   FbSize
  )
{
  volatile UINT32  *Pix   = (volatile UINT32 *) Fb;
  volatile UINTN    Count = FbSize / 4;
  volatile UINTN    i;

  for (i = 0; i < Count; i++) {
    Pix[i] = 0xFF0000FF;      /* 蓝: 与"绿"区分, 表示已是裸机环境 */
  }
  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
}

STATIC
EFI_STATUS
LeaveBootServices (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS              Status = EFI_SUCCESS;
  UINTN                   MapSize = 0;
  UINTN                   MapKey;
  UINTN                   DescSize;
  UINT32                  DescVer;
  EFI_MEMORY_DESCRIPTOR  *Map = NULL;
  UINTN                   Try;

  for (Try = 0; Try < 4; Try++) {
    MapSize = 0;
    Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR (Status) && Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }
    if (Map != NULL) {
      FreePool (Map);
    }
    MapSize += 2 * DescSize;
    Map      = AllocatePool (MapSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Status = gBS->ExitBootServices (ImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }
  }
  return Status;
}

STATIC
VOID
EFIAPI
OnTextOut (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  CHAR16      Buf[192];
  EFI_STATUS  Status;
  volatile UINTN j;

  (VOID) Event;
  (VOID) Context;
  (VOID) Status;

  if (gST == NULL || gST->ConOut == NULL) {
    return;
  }

  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"\r\n[fuxi-probe v10] EL%Lu  FDT=0x%Lx (%Lu bytes)\r\n"
    L"leaving boot services in ~3s, screen will turn BLUE\r\n",
    CurrentEl (), (UINT64) mFdtAddr, (UINT64) mFdtSize
    );
  gST->ConOut->OutputString (gST->ConOut, Buf);

  /* 忙等, 让人看清绿屏和文字 */
  for (j = 0; j < 200000000UL; j++) {
    __asm__ __volatile__ ("");
  }

  Status = LeaveBootServices (mImageHandle);
  (VOID) Status;

  /* 到这里 Boot Services 已失效, 只能直接写物理内存 */
  Payload (mFb, mFbSize);
}

EFI_STATUS
EFIAPI
ProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_EVENT                      Event;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
  UINTN                          i;

  (VOID) SystemTable;
  mImageHandle = ImageHandle;

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

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL) {
    mFb     = (UINT8 *) (UINTN) Gop->Mode->FrameBufferBase;
    mFbSize = (UINTN) Gop->Mode->FrameBufferSize;
  }
  FillColor (0xFF00FF00);

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, OnTextOut, NULL, &Event);
  if (EFI_ERROR (Status)) {
    Halt ();
  }
  return gBS->RegisterProtocolNotify (&gEfiSimpleTextOutProtocolGuid, Event, &(VOID *){0});
}
