/** @file  Probe.c

  fuxi 探针 v6 —— 只分辨"能不能拿到 GOP"。

  已知事实:
    v4: 入口 wfi -> 手机卡住, 证明驱动会被 DXE 调度执行。
    v5: 定时器 + 拿到就刷绿 + halt -> 屏幕最后停在菜单不动(原因不明:
        可能 GOP 没拿到直接 halt, 也可能刷了绿又被别的绘制盖掉)。

  v6: 每 100ms 重刷一次绿, 而且永不 halt。
    屏幕持续绿 -> GOP 能拿到(反复覆盖, 画什么都盖不住)
    菜单照旧   -> GOP 拿不到, 计时器在空转
    两种情况都不会把手机卡死, 便于反复观察。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Protocol/GraphicsOutput.h>

STATIC EFI_EVENT  mTimer;
STATIC UINT32     mTries;
STATIC BOOLEAN    mPainted;

STATIC
VOID
Paint (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  UINT32  *Pix;
  UINTN    Count;
  UINTN    i;

  if (Gop->Mode == NULL) {
    return;
  }
  Pix   = (UINT32 *) (UINTN) Gop->Mode->FrameBufferBase;
  Count = (UINTN) Gop->Mode->FrameBufferSize / 4;
  if (Pix == NULL) {
    return;
  }
  for (i = 0; i < Count; i++) {
    Pix[i] = 0xFF00FF00;     /* 纯绿 */
  }
}

STATIC
VOID
EFIAPI
OnTimer (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;

  (VOID) Event;
  (VOID) Context;
  mTries++;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL) {
    Paint (Gop);
    mPainted = TRUE;
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

  (VOID) ImageHandle;
  (VOID) SystemTable;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, OnTimer, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return gBS->SetTimer (mTimer, TimerPeriodic, 100000);   /* 10ms: 抢在重绘之前覆盖 */
}
