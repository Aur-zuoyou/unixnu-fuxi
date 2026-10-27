/** @file  HackiosLoader.c
  Fuxi bring-up shim: start HackiOS prebuilt/loader.efi as a UEFI application.

  The complete XNUP payload is supplied in the Android boot image's ramdisk
  field; the bootloader places it at 0xF0000000 and xnuloader reads it there.
  This shim does not touch 0xF0000000.

  We deliberately start the loader synchronously from ProbeEntry after a
  short Stall.  A timer callback would run after ProbeEntry returns and the
  firmware may have unloaded this image, which causes a Data Abort in the
  stale FuxiProbe code.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

extern CONST UINT8   gHackiosLoaderData[];
extern CONST UINT32  gHackiosLoaderSize;

STATIC EFI_HANDLE  mParentImageHandle;
STATIC BOOLEAN     mHackiosArmed;

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

/** Synchronously load and start the embedded HackiOS loader. */
VOID
HackiosLoaderStart (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  LoaderHandle;

  mParentImageHandle = ImageHandle;
  mHackiosArmed      = FALSE;

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

/** Mark the old Probe path as inactive and remember the parent image. */
VOID
HackiosLoaderInit (
  IN EFI_HANDLE  ImageHandle
  )
{
  mParentImageHandle = ImageHandle;
  mHackiosArmed      = TRUE;
}

/** TRUE while the old Probe/ExitBootServices path must stay out of the way. */
BOOLEAN
HackiosLoaderPending (
  VOID
  )
{
  return mHackiosArmed;
}
