/** @file  Probe.c

  fuxi 探针 v7 —— 结论已明确:
    v4 证明驱动会被执行;
    v6 证明 LocateProtocol(GOP) 拿不到帧缓冲(屏幕不变绿), 这版固件的显示
       不走标准 GOP, 而屏幕上那行 "Image at ... start failed" 说明
       ConOut(文字输出) 是通的。

  v7 策略: 完全放弃 GOP, 用 ConOut。
    每 500ms 打一行, 反复打, 保证 BDS 画完菜单之后依然能看到。
    同时打印次数上限, 避免刷屏到无法使用。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

STATIC EFI_EVENT  mTimer;
STATIC UINT32     mTries;
STATIC UINT32     mPrinted;

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
EFIAPI
OnTimer (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  CHAR16  Buf[160];

  (VOID) Event;
  (VOID) Context;
  mTries++;

  if (gST == NULL || gST->ConOut == NULL) {
    return;                     /* 控制台还没好, 下次再试 */
  }
  if (mPrinted >= 40) {
    return;                     /* 打够 40 行(约 20 秒)就安静 */
  }

  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"[fuxi-probe v7] EL%Lu  ConOut OK  tries=%Lu\r\n",
    CurrentEl (), (UINT64) mTries
    );
  gST->ConOut->OutputString (gST->ConOut, Buf);
  mPrinted++;
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
  return gBS->SetTimer (mTimer, TimerPeriodic, 5000000);   /* 500ms */
}
