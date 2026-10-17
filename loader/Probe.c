/** @file  Probe.c

  fuxi 探针 v5 —— 终于确认的事实(来自 v4 实测):
    驱动"会被 DXE 调度并执行"。v4 在入口 wfi 死循环, 手机卡住、不出菜单。
    但 v2/v3 挂 EndOfDxe 事件都没反应 —— 那个事件在这个平台上靠不住。

  v5 的策略: 入口只注册一个 100ms 周期定时器, 然后返回。
    每次定时器回调里尝试拿 GOP(帧缓冲);
      拿到 -> 整屏刷绿 + 控制台打印 EL/FDT -> 死循环停住
      没拿到 -> 下次再试(最多 30 秒)
    这样既不依赖 EndOfDxe, 也不要求"入口时"控制台/显卡已就绪。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>

/* used: 防止被优化掉, 便于事后在固件里确认版本 */
STATIC CONST CHAR16  mMark[] __attribute__((used)) = L"fuxi-probe-v5-timer";

STATIC EFI_EVENT  mTimer;
STATIC UINT32     mTries;

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

STATIC
VOID
Say (
  IN CONST CHAR16  *Text
  )
{
  if (gST != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *) Text);
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
  UINT8                         *Base;
  UINTN                          Size;
  UINT32                        *Pix;
  UINTN                          i;
  UINTN                          FdtAddr = 0;
  UINT32                         FdtSize = 0;
  CHAR16                         Buf[128];

  (VOID) Event;
  (VOID) Context;
  mTries++;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **) &Gop);
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Mode == NULL) {
    /* 30 秒还拿不到 GOP: 停住(至少证明进过定时器) */
    if (mTries > 300) {
      Halt ();
    }
    return;
  }

  /* --- 拿到帧缓冲了 --- */
  Base = (UINT8 *) (UINTN) Gop->Mode->FrameBufferBase;
  Size = (UINTN) Gop->Mode->FrameBufferSize;
  if (Base != NULL && Size >= 4) {
    Pix = (UINT32 *) Base;
    for (i = 0; i < Size / 4; i++) {
      Pix[i] = 0xFF00FF00;          /* 纯绿 */
    }
  }

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

  UnicodeSPrint (
    Buf, sizeof (Buf),
    L" fuxi-probe v5  EL%Lu  FDT=0x%Lx(%Lu)  GOP=%ux%u\r\n",
    CurrentEl (), (UINT64) FdtAddr, (UINT64) FdtSize,
    (UINT32) Gop->Mode->Info->HorizontalResolution,
    (UINT32) Gop->Mode->Info->VerticalResolution
    );
  Say (Buf);

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

  (VOID) ImageHandle;
  (VOID) SystemTable;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, OnTimer, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    Halt ();
  }
  return gBS->SetTimer (mTimer, TimerPeriodic, 1000000);  /* 100ms */
}
