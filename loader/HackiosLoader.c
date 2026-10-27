/** @file  HackiosLoader.c
  Fuxi bring-up shim: start HackiOS prebuilt/loader.efi as a UEFI application.

  This file is copied into edk2-msm/Platform/Xiaomi/sm8550/probe/ by the
  patch script.  The complete XNUP payload is supplied in the Android boot
  image's ramdisk field; the bootloader places it at 0xF0000000 and xnuloader
  reads it there.  This shim deliberately does not write, allocate or zero
  0xF0000000.  A timer delays LoadImage/StartImage so that fuxi's DXE/BDS
  drivers have been dispatched first.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#define HACKIOS_DELAY_100NS (5ULL * 1000ULL * 1000ULL * 10ULL) /* 5 s */

extern CONST UINT8   gHackiosLoaderData[];
extern CONST UINT32  gHackiosLoaderSize;

STATIC EFI_EVENT   mHackiosTimer;
STATIC EFI_HANDLE  mParentImageHandle;
STATIC BOOLEAN     mHackiosArmed;
STATIC BOOLEAN     mHackiosStarted;

STATIC
VOID
HackiosPrint (
  IN CONST CHAR16  *Text
  )
{
  if (gST != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, (CHAR16 *) Text);
  }
}

STATIC
VOID
HackiosPrintStatus (
  IN CONST CHAR16  *What,
  IN EFI_STATUS     Status
  )
{
  CHAR16  Buf[160];

  UnicodeSPrint (Buf, sizeof (Buf), L"hackios: %s: %r\r\n", What, Status);
  HackiosPrint (Buf);
}

STATIC
VOID
EFIAPI
HackiosTimer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  LoaderHandle;

  (VOID) Event;
  (VOID) Context;

  if (mHackiosStarted) {
    return;
  }
  mHackiosStarted = TRUE;
  gBS->SetTimer (mHackiosTimer, TimerCancel, 0);

  HackiosPrint (L"hackios: starting embedded loader.efi\r\n");
  HackiosPrint (L"hackios: XNUP payload is expected at 0xF0000000 (ramdisk)\r\n");

  Status = gBS->LoadImage (
                   FALSE,
                   mParentImageHandle,
                   NULL,
                   (VOID *) gHackiosLoaderData,
                   (UINTN) gHackiosLoaderSize,
                   &LoaderHandle
                   );
  if (EFI_ERROR (Status)) {
    HackiosPrintStatus (L"LoadImage(loader.efi)", Status);
    CpuDeadLoop ();
  }

  Status = gBS->StartImage (LoaderHandle, NULL, NULL);
  HackiosPrintStatus (L"loader.efi returned", Status);
  CpuDeadLoop ();
}

/** Arm the delayed hand-off.  Called from ProbeEntry. */
VOID
HackiosLoaderInit (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;

  mParentImageHandle = ImageHandle;
  Status = gBS->CreateEvent (
                   EVT_TIMER | EVT_NOTIFY_SIGNAL,
                   TPL_CALLBACK,
                   HackiosTimer,
                   NULL,
                   &mHackiosTimer
                   );
  if (EFI_ERROR (Status)) {
    return;
  }
  Status = gBS->SetTimer (mHackiosTimer, TimerRelative, HACKIOS_DELAY_100NS);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mHackiosTimer);
    return;
  }
  mHackiosArmed = TRUE;
}

/** TRUE while the old Probe/ExitBootServices path must stay out of the way. */
BOOLEAN
HackiosLoaderPending (
  VOID
  )
{
  return mHackiosArmed;
}
