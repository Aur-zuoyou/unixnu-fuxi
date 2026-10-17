/** @file  Probe.c

  fuxi 探针 v4 —— 只回答一个问题: "这个驱动到底有没有被 DXE 调度器执行?"

  前面几版失败在"看得见"这件事上:
    v1 DriverEntry 打印      -> 控制台没初始化
    v2 EndOfDxe 打印        -> 事件可能没触发
    v3 EndOfDxe 刷绿屏      -> 屏幕没变，说明回调根本没跑

  v4 不再依赖任何输出通道: 在 DriverEntry 里直接死循环。
  判断方法(屏幕本身不用看):
    开机后卡在 logo/黑屏, 永远进不了启动菜单 -> 驱动被执行了
    照常出现启动菜单、10 秒后进 Shell       -> 驱动根本没被 dispatch

  这一步通过之后, 才值得去做"画字/刷屏"这些事。
**/

#include <Uefi.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>

/* 留一个可被识别的标记: 便于事后在固件里确认刷的是哪一版 */
STATIC CONST CHAR16  mMark[] = L"fuxi-probe-v4-halt";

EFI_STATUS
EFIAPI
ProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  (VOID) ImageHandle;
  (VOID) SystemTable;
  (VOID) mMark;

  /* 驱动一被调度就停在这里。
     注意: 这里是 DXE 早期, 屏幕可能还停在引导 logo, 这是预期的。 */
  for ( ; ; ) {
    __asm__ __volatile__ ("wfi");
  }
}
